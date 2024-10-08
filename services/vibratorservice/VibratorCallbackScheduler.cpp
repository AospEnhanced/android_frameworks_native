/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include <chrono>
#include <thread>

#include <vibratorservice/VibratorCallbackScheduler.h>

namespace android {

namespace vibrator {

// -------------------------------------------------------------------------------------------------

bool DelayedCallback::isExpired() const {
    return mExpiration <= std::chrono::steady_clock::now();
}

std::chrono::milliseconds DelayedCallback::getWaitForExpirationDuration() const {
    std::chrono::milliseconds delta = std::chrono::duration_cast<std::chrono::milliseconds>(
            mExpiration - std::chrono::steady_clock::now());
    // Return zero if this is already expired.
    return delta > delta.zero() ? delta : delta.zero();
}

void DelayedCallback::run() const {
    mCallback();
}

bool DelayedCallback::operator<(const DelayedCallback& other) const {
    return mExpiration < other.mExpiration;
}

bool DelayedCallback::operator>(const DelayedCallback& other) const {
    return mExpiration > other.mExpiration;
}

// -------------------------------------------------------------------------------------------------

CallbackScheduler::~CallbackScheduler() {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mFinished = true;
    }
    mCondition.notify_all();
    if (mCallbackThread && mCallbackThread->joinable()) {
        mCallbackThread->join();
    }
}

void CallbackScheduler::schedule(std::function<void()> callback, std::chrono::milliseconds delay) {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mCallbackThread == nullptr) {
            mCallbackThread = std::make_unique<std::thread>(&CallbackScheduler::loop, this);
        }
        mQueue.emplace(DelayedCallback(callback, delay));
    }
    mCondition.notify_all();
}

void CallbackScheduler::loop() {
    while (true) {
        std::unique_lock<std::mutex> lock(mMutex);
        if (mFinished) {
            // Destructor was called, so let the callback thread die.
            break;
        }
        while (!mQueue.empty() && mQueue.top().isExpired()) {
            DelayedCallback callback = mQueue.top();
            mQueue.pop();
            lock.unlock();
            callback.run();
            lock.lock();
        }
        if (mQueue.empty()) {
            // Wait until a new callback is scheduled or destructor was called.
            mCondition.wait(lock, [this] { return mFinished || !mQueue.empty(); });
        } else {
            // Wait until next callback expires or a new one is scheduled.
            // Use the monotonic steady clock to wait for the measured delay interval via wait_for
            // instead of using a wall clock via wait_until.
            mCondition.wait_for(lock, mQueue.top().getWaitForExpirationDuration());
        }
    }
}

// -------------------------------------------------------------------------------------------------

}; // namespace vibrator

}; // namespace android
