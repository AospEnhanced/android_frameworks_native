/*
 * Copyright (C) 2019 The Android Open Source Project
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

#pragma once

#include "InputMapper.h"

namespace android {

class JoystickInputMapper : public InputMapper {
public:
    template <class T, class... Args>
    friend std::unique_ptr<T> createInputMapper(InputDeviceContext& deviceContext,
                                                const InputReaderConfiguration& readerConfig,
                                                Args... args);
    virtual ~JoystickInputMapper();

    virtual uint32_t getSources() const override;
    virtual void populateDeviceInfo(InputDeviceInfo& deviceInfo) override;
    virtual void dump(std::string& dump) override;
    [[nodiscard]] std::list<NotifyArgs> reconfigure(nsecs_t when,
                                                    const InputReaderConfiguration& config,
                                                    ConfigurationChanges changes) override;
    [[nodiscard]] std::list<NotifyArgs> reset(nsecs_t when) override;
    [[nodiscard]] std::list<NotifyArgs> process(const RawEvent& rawEvent) override;

private:
    struct Axis {
        explicit Axis(const RawAbsoluteAxisInfo& rawAxisInfo, const AxisInfo& axisInfo,
                      bool explicitlyMapped, float scale, float offset, float highScale,
                      float highOffset, float min, float max, float flat, float fuzz,
                      float resolution, float filter)
              : rawAxisInfo(rawAxisInfo),
                axisInfo(axisInfo),
                explicitlyMapped(explicitlyMapped),
                scale(scale),
                offset(offset),
                highScale(highScale),
                highOffset(highOffset),
                min(min),
                max(max),
                flat(flat),
                fuzz(fuzz),
                resolution(resolution),
                filter(filter) {
            resetValue();
        }

        RawAbsoluteAxisInfo rawAxisInfo;
        AxisInfo axisInfo;

        bool explicitlyMapped; // true if the axis was explicitly assigned an axis id

        float scale;      // scale factor from raw to normalized values
        float offset;     // offset to add after scaling for normalization
        float highScale;  // scale factor from raw to normalized values of high split
        float highOffset; // offset to add after scaling for normalization of high split

        float min;        // normalized inclusive minimum
        float max;        // normalized inclusive maximum
        float flat;       // normalized flat region size
        float fuzz;       // normalized error tolerance
        float resolution; // normalized resolution in units/mm

        float filter;           // filter out small variations of this size
        float currentValue;     // current value
        float newValue;         // most recent value
        float highCurrentValue; // current value of high split
        float highNewValue;     // most recent value of high split

        void resetValue() {
            this->currentValue = 0;
            this->newValue = 0;
            this->highCurrentValue = 0;
            this->highNewValue = 0;
        }
    };

    explicit JoystickInputMapper(InputDeviceContext& deviceContext,
                                 const InputReaderConfiguration& readerConfig);

    static Axis createAxis(const AxisInfo& AxisInfo, const RawAbsoluteAxisInfo& rawAxisInfo,
                           bool explicitlyMapped);

    // Axes indexed by raw ABS_* axis index.
    std::unordered_map<int32_t, Axis> mAxes;

    [[nodiscard]] std::list<NotifyArgs> sync(nsecs_t when, nsecs_t readTime, bool force);

    bool haveAxis(int32_t axisId);
    void pruneAxes(bool ignoreExplicitlyMappedAxes);
    bool filterAxes(bool force);

    static bool hasValueChangedSignificantly(float filter, float newValue, float currentValue,
                                             float min, float max);
    static bool hasMovedNearerToValueWithinFilteredRange(float filter, float newValue,
                                                         float currentValue, float thresholdValue);

    static bool isCenteredAxis(int32_t axis);
    static int32_t getCompatAxis(int32_t axis);

    static void addMotionRange(int32_t axisId, const Axis& axis, InputDeviceInfo& info);
    static void setPointerCoordsAxisValue(PointerCoords* pointerCoords, int32_t axis, float value);
};

} // namespace android
