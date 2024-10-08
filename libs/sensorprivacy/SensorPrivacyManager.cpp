/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <mutex>
#include <unistd.h>

#include <binder/Binder.h>
#include <binder/IServiceManager.h>
#include <sensorprivacy/SensorPrivacyManager.h>

#include <utils/SystemClock.h>

namespace android {

SensorPrivacyManager::SensorPrivacyManager()
{
}

sp<hardware::ISensorPrivacyManager> SensorPrivacyManager::getService()
{
    std::lock_guard<Mutex> scoped_lock(mLock);
    sp<hardware::ISensorPrivacyManager> service = mService;
    if (service == nullptr || !IInterface::asBinder(service)->isBinderAlive()) {
      sp<IBinder> binder = defaultServiceManager()->waitForService(String16("sensor_privacy"));
      mService = interface_cast<hardware::ISensorPrivacyManager>(binder);
    }
    return mService;
}

bool SensorPrivacyManager::supportsSensorToggle(int toggleType, int sensor) {
    if (mSupportedCache.find(sensor) == mSupportedCache.end()) {
        sp<hardware::ISensorPrivacyManager> service = getService();
        if (service != nullptr) {
            bool result;
            service->supportsSensorToggle(toggleType, sensor, &result);
            mSupportedCache[sensor] = result;
            return result;
        }
        // if the SensorPrivacyManager is not available then assume sensor privacy feature isn't
        // supported
        return false;
    }
    return mSupportedCache[sensor];
}

void SensorPrivacyManager::addSensorPrivacyListener(
        const sp<hardware::ISensorPrivacyListener>& listener)
{
    sp<hardware::ISensorPrivacyManager> service = getService();
    if (service != nullptr) {
        service->addSensorPrivacyListener(listener);
    }
}

status_t SensorPrivacyManager::addToggleSensorPrivacyListener(
        const sp<hardware::ISensorPrivacyListener>& listener)
{
    sp<hardware::ISensorPrivacyManager> service = getService();
    if (service != nullptr) {
        return service->addToggleSensorPrivacyListener(listener)
                .transactionError();
    }
    return UNEXPECTED_NULL;
}

void SensorPrivacyManager::removeSensorPrivacyListener(
        const sp<hardware::ISensorPrivacyListener>& listener)
{
    sp<hardware::ISensorPrivacyManager> service = getService();
    if (service != nullptr) {
        service->removeSensorPrivacyListener(listener);
    }
}

void SensorPrivacyManager::removeToggleSensorPrivacyListener(
        const sp<hardware::ISensorPrivacyListener>& listener)
{
    sp<hardware::ISensorPrivacyManager> service = getService();
    if (service != nullptr) {
        service->removeToggleSensorPrivacyListener(listener);
    }
}

bool SensorPrivacyManager::isSensorPrivacyEnabled()
{
    sp<hardware::ISensorPrivacyManager> service = getService();
    if (service != nullptr) {
        bool result;
        service->isSensorPrivacyEnabled(&result);
        return result;
    }
    // if the SensorPrivacyManager is not available then assume sensor privacy is disabled
    return false;
}

bool SensorPrivacyManager::isToggleSensorPrivacyEnabled(int sensor)
{
    sp<hardware::ISensorPrivacyManager> service = getService();
    if (service != nullptr) {
        bool result;
        service->isCombinedToggleSensorPrivacyEnabled(sensor, &result);
        return result;
    }
    // if the SensorPrivacyManager is not available then assume sensor privacy is disabled
    return false;
}

bool SensorPrivacyManager::isToggleSensorPrivacyEnabled(int toggleType, int sensor)
{
    sp<hardware::ISensorPrivacyManager> service = getService();
    if (service != nullptr) {
        bool result;
        service->isToggleSensorPrivacyEnabled(toggleType, sensor, &result);
        return result;
    }
    // if the SensorPrivacyManager is not available then assume sensor privacy is disabled
    return false;
}

status_t SensorPrivacyManager::isToggleSensorPrivacyEnabled(int toggleType, int sensor,
        bool &returnVal)
{
    sp<hardware::ISensorPrivacyManager> service = getService();
    if (service != nullptr) {
        binder::Status res = service->isToggleSensorPrivacyEnabled(toggleType, sensor, &returnVal);
        return res.transactionError();
    }
    // if the SensorPrivacyManager is not available then assume sensor privacy is disabled
    returnVal = false;
    return UNKNOWN_ERROR;
}

int SensorPrivacyManager::getToggleSensorPrivacyState(int toggleType, int sensor)
{
    sp<hardware::ISensorPrivacyManager> service = getService();
    if (service != nullptr) {
        int result;
        service->getToggleSensorPrivacyState(toggleType, sensor, &result);
        return result;
    }
    // if the SensorPrivacyManager is not available then assume sensor privacy is disabled
    return DISABLED;
}

std::vector<String16> SensorPrivacyManager::getCameraPrivacyAllowlist(){
    sp<hardware::ISensorPrivacyManager> service = getService();
    std::vector<String16> result;
    if (service != nullptr) {
        service->getCameraPrivacyAllowlist(&result);
        return result;
    }
    return result;
}

bool SensorPrivacyManager::isCameraPrivacyEnabled(String16 packageName){
    sp<hardware::ISensorPrivacyManager> service = getService();
    if (service != nullptr) {
        bool result;
        service->isCameraPrivacyEnabled(packageName, &result);
        return result;
    }
    return false;
}

status_t SensorPrivacyManager::linkToDeath(const sp<IBinder::DeathRecipient>& recipient)
{
    sp<hardware::ISensorPrivacyManager> service = getService();
    if (service != nullptr) {
        return IInterface::asBinder(service)->linkToDeath(recipient);
    }
    return INVALID_OPERATION;
}

status_t SensorPrivacyManager::unlinkToDeath(const sp<IBinder::DeathRecipient>& recipient)
{
    sp<hardware::ISensorPrivacyManager> service = getService();
    if (service != nullptr) {
        return IInterface::asBinder(service)->unlinkToDeath(recipient);
    }
    return INVALID_OPERATION;
}

}; // namespace android
