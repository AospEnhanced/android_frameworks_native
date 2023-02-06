/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "TfLiteMotionPredictor"
#include <input/TfLiteMotionPredictor.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <utility>

#define ATRACE_TAG ATRACE_TAG_INPUT
#include <cutils/trace.h>
#include <log/log.h>

#include "tensorflow/lite/core/api/error_reporter.h"
#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/kernels/register.h"
#include "tensorflow/lite/model.h"

namespace android {
namespace {

constexpr char SIGNATURE_KEY[] = "serving_default";

// Input tensor names.
constexpr char INPUT_R[] = "r";
constexpr char INPUT_PHI[] = "phi";
constexpr char INPUT_PRESSURE[] = "pressure";
constexpr char INPUT_TILT[] = "tilt";
constexpr char INPUT_ORIENTATION[] = "orientation";

// Output tensor names.
constexpr char OUTPUT_R[] = "r";
constexpr char OUTPUT_PHI[] = "phi";
constexpr char OUTPUT_PRESSURE[] = "pressure";

// A TFLite ErrorReporter that logs to logcat.
class LoggingErrorReporter : public tflite::ErrorReporter {
public:
    int Report(const char* format, va_list args) override {
        return LOG_PRI_VA(ANDROID_LOG_ERROR, LOG_TAG, format, args);
    }
};

// Searches a runner for an input tensor.
TfLiteTensor* findInputTensor(const char* name, tflite::SignatureRunner* runner) {
    TfLiteTensor* tensor = runner->input_tensor(name);
    LOG_ALWAYS_FATAL_IF(!tensor, "Failed to find input tensor '%s'", name);
    return tensor;
}

// Searches a runner for an output tensor.
const TfLiteTensor* findOutputTensor(const char* name, tflite::SignatureRunner* runner) {
    const TfLiteTensor* tensor = runner->output_tensor(name);
    LOG_ALWAYS_FATAL_IF(!tensor, "Failed to find output tensor '%s'", name);
    return tensor;
}

// Returns the buffer for a tensor of type T.
template <typename T>
std::span<T> getTensorBuffer(typename std::conditional<std::is_const<T>::value, const TfLiteTensor*,
                                                       TfLiteTensor*>::type tensor) {
    LOG_ALWAYS_FATAL_IF(!tensor);

    const TfLiteType type = tflite::typeToTfLiteType<typename std::remove_cv<T>::type>();
    LOG_ALWAYS_FATAL_IF(tensor->type != type, "Unexpected type for '%s' tensor: %s (expected %s)",
                        tensor->name, TfLiteTypeGetName(tensor->type), TfLiteTypeGetName(type));

    LOG_ALWAYS_FATAL_IF(!tensor->data.data);
    return {reinterpret_cast<T*>(tensor->data.data),
            static_cast<typename std::span<T>::index_type>(tensor->bytes / sizeof(T))};
}

// Verifies that a tensor exists and has an underlying buffer of type T.
template <typename T>
void checkTensor(const TfLiteTensor* tensor) {
    LOG_ALWAYS_FATAL_IF(!tensor);

    const auto buffer = getTensorBuffer<const T>(tensor);
    LOG_ALWAYS_FATAL_IF(buffer.empty(), "No buffer for tensor '%s'", tensor->name);
}

} // namespace

TfLiteMotionPredictorBuffers::TfLiteMotionPredictorBuffers(size_t inputLength)
      : mInputR(inputLength, 0),
        mInputPhi(inputLength, 0),
        mInputPressure(inputLength, 0),
        mInputTilt(inputLength, 0),
        mInputOrientation(inputLength, 0) {
    LOG_ALWAYS_FATAL_IF(inputLength == 0, "Buffer input size must be greater than 0");
}

void TfLiteMotionPredictorBuffers::reset() {
    std::fill(mInputR.begin(), mInputR.end(), 0);
    std::fill(mInputPhi.begin(), mInputPhi.end(), 0);
    std::fill(mInputPressure.begin(), mInputPressure.end(), 0);
    std::fill(mInputTilt.begin(), mInputTilt.end(), 0);
    std::fill(mInputOrientation.begin(), mInputOrientation.end(), 0);
    mAxisFrom.reset();
    mAxisTo.reset();
}

void TfLiteMotionPredictorBuffers::copyTo(TfLiteMotionPredictorModel& model) const {
    LOG_ALWAYS_FATAL_IF(mInputR.size() != model.inputLength(),
                        "Buffer length %zu doesn't match model input length %zu", mInputR.size(),
                        model.inputLength());
    LOG_ALWAYS_FATAL_IF(!isReady(), "Buffers are incomplete");

    std::copy(mInputR.begin(), mInputR.end(), model.inputR().begin());
    std::copy(mInputPhi.begin(), mInputPhi.end(), model.inputPhi().begin());
    std::copy(mInputPressure.begin(), mInputPressure.end(), model.inputPressure().begin());
    std::copy(mInputTilt.begin(), mInputTilt.end(), model.inputTilt().begin());
    std::copy(mInputOrientation.begin(), mInputOrientation.end(), model.inputOrientation().begin());
}

void TfLiteMotionPredictorBuffers::pushSample(int64_t timestamp,
                                              const TfLiteMotionPredictorSample sample) {
    // Convert the sample (x, y) into polar (r, φ) based on a reference axis
    // from the preceding two points (mAxisFrom/mAxisTo).

    mTimestamp = timestamp;

    if (!mAxisTo) { // First point.
        mAxisTo = sample;
        return;
    }

    // Vector from the last point to the current sample point.
    const TfLiteMotionPredictorSample::Point v = sample.position - mAxisTo->position;

    const float r = std::hypot(v.x, v.y);
    float phi = 0;
    float orientation = 0;

    // Ignore the sample if there is no movement. These samples can occur when there's change to a
    // property other than the coordinates and pollute the input to the model.
    if (r == 0) {
        return;
    }

    if (!mAxisFrom) { // Second point.
        // We can only determine the distance from the first point, and not any
        // angle. However, if the second point forms an axis, the orientation can
        // be transformed relative to that axis.
        const float axisPhi = std::atan2(v.y, v.x);
        // A MotionEvent's orientation is measured clockwise from the vertical
        // axis, but axisPhi is measured counter-clockwise from the horizontal
        // axis.
        orientation = M_PI_2 - sample.orientation - axisPhi;
    } else {
        const TfLiteMotionPredictorSample::Point axis = mAxisTo->position - mAxisFrom->position;
        const float axisPhi = std::atan2(axis.y, axis.x);
        phi = std::atan2(v.y, v.x) - axisPhi;

        if (std::hypot(axis.x, axis.y) > 0) {
            // See note above.
            orientation = M_PI_2 - sample.orientation - axisPhi;
        }
    }

    // Update the axis for the next point.
    mAxisFrom = mAxisTo;
    mAxisTo = sample;

    // Push the current sample onto the end of the input buffers.
    mInputR.pushBack(r);
    mInputPhi.pushBack(phi);
    mInputPressure.pushBack(sample.pressure);
    mInputTilt.pushBack(sample.tilt);
    mInputOrientation.pushBack(orientation);
}

std::unique_ptr<TfLiteMotionPredictorModel> TfLiteMotionPredictorModel::create(
        const char* modelPath) {
    std::ifstream f(modelPath, std::ios::binary);
    LOG_ALWAYS_FATAL_IF(!f, "Could not read model from %s", modelPath);

    std::string data;
    data.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());

    return std::unique_ptr<TfLiteMotionPredictorModel>(
            new TfLiteMotionPredictorModel(std::move(data)));
}

TfLiteMotionPredictorModel::TfLiteMotionPredictorModel(std::string model)
      : mFlatBuffer(std::move(model)) {
    mErrorReporter = std::make_unique<LoggingErrorReporter>();
    mModel = tflite::FlatBufferModel::VerifyAndBuildFromBuffer(mFlatBuffer.data(),
                                                               mFlatBuffer.length(),
                                                               /*extra_verifier=*/nullptr,
                                                               mErrorReporter.get());
    LOG_ALWAYS_FATAL_IF(!mModel);

    tflite::ops::builtin::BuiltinOpResolver resolver;
    tflite::InterpreterBuilder builder(*mModel, resolver);

    if (builder(&mInterpreter) != kTfLiteOk || !mInterpreter) {
        LOG_ALWAYS_FATAL("Failed to build interpreter");
    }

    mRunner = mInterpreter->GetSignatureRunner(SIGNATURE_KEY);
    LOG_ALWAYS_FATAL_IF(!mRunner, "Failed to find runner for signature '%s'", SIGNATURE_KEY);

    allocateTensors();
}

void TfLiteMotionPredictorModel::allocateTensors() {
    if (mRunner->AllocateTensors() != kTfLiteOk) {
        LOG_ALWAYS_FATAL("Failed to allocate tensors");
    }

    attachInputTensors();
    attachOutputTensors();

    checkTensor<float>(mInputR);
    checkTensor<float>(mInputPhi);
    checkTensor<float>(mInputPressure);
    checkTensor<float>(mInputTilt);
    checkTensor<float>(mInputOrientation);
    checkTensor<float>(mOutputR);
    checkTensor<float>(mOutputPhi);
    checkTensor<float>(mOutputPressure);

    const auto checkInputTensorSize = [this](const TfLiteTensor* tensor) {
        const size_t size = getTensorBuffer<const float>(tensor).size();
        LOG_ALWAYS_FATAL_IF(size != inputLength(),
                            "Tensor '%s' length %zu does not match input length %zu", tensor->name,
                            size, inputLength());
    };

    checkInputTensorSize(mInputR);
    checkInputTensorSize(mInputPhi);
    checkInputTensorSize(mInputPressure);
    checkInputTensorSize(mInputTilt);
    checkInputTensorSize(mInputOrientation);
}

void TfLiteMotionPredictorModel::attachInputTensors() {
    mInputR = findInputTensor(INPUT_R, mRunner);
    mInputPhi = findInputTensor(INPUT_PHI, mRunner);
    mInputPressure = findInputTensor(INPUT_PRESSURE, mRunner);
    mInputTilt = findInputTensor(INPUT_TILT, mRunner);
    mInputOrientation = findInputTensor(INPUT_ORIENTATION, mRunner);
}

void TfLiteMotionPredictorModel::attachOutputTensors() {
    mOutputR = findOutputTensor(OUTPUT_R, mRunner);
    mOutputPhi = findOutputTensor(OUTPUT_PHI, mRunner);
    mOutputPressure = findOutputTensor(OUTPUT_PRESSURE, mRunner);
}

bool TfLiteMotionPredictorModel::invoke() {
    ATRACE_BEGIN("TfLiteMotionPredictorModel::invoke");
    TfLiteStatus result = mRunner->Invoke();
    ATRACE_END();

    if (result != kTfLiteOk) {
        return false;
    }

    // Invoke() might reallocate tensors, so they need to be reattached.
    attachInputTensors();
    attachOutputTensors();

    if (outputR().size() != outputPhi().size() || outputR().size() != outputPressure().size()) {
        LOG_ALWAYS_FATAL("Output size mismatch: (r: %zu, phi: %zu, pressure: %zu)",
                         outputR().size(), outputPhi().size(), outputPressure().size());
    }

    return true;
}

size_t TfLiteMotionPredictorModel::inputLength() const {
    return getTensorBuffer<const float>(mInputR).size();
}

std::span<float> TfLiteMotionPredictorModel::inputR() {
    return getTensorBuffer<float>(mInputR);
}

std::span<float> TfLiteMotionPredictorModel::inputPhi() {
    return getTensorBuffer<float>(mInputPhi);
}

std::span<float> TfLiteMotionPredictorModel::inputPressure() {
    return getTensorBuffer<float>(mInputPressure);
}

std::span<float> TfLiteMotionPredictorModel::inputTilt() {
    return getTensorBuffer<float>(mInputTilt);
}

std::span<float> TfLiteMotionPredictorModel::inputOrientation() {
    return getTensorBuffer<float>(mInputOrientation);
}

std::span<const float> TfLiteMotionPredictorModel::outputR() const {
    return getTensorBuffer<const float>(mOutputR);
}

std::span<const float> TfLiteMotionPredictorModel::outputPhi() const {
    return getTensorBuffer<const float>(mOutputPhi);
}

std::span<const float> TfLiteMotionPredictorModel::outputPressure() const {
    return getTensorBuffer<const float>(mOutputPressure);
}

} // namespace android
