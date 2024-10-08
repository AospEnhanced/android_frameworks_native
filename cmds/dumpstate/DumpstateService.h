/**
 * Copyright (c) 2016, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_OS_DUMPSTATE_H_
#define ANDROID_OS_DUMPSTATE_H_

#include <mutex>
#include <vector>

#include <android-base/unique_fd.h>
#include <binder/BinderService.h>

#include "android/os/BnDumpstate.h"
#include "dumpstate.h"

namespace android {
namespace os {

class DumpstateService : public BinderService<DumpstateService>, public BnDumpstate {
  public:
    DumpstateService();

    static status_t Start();
    static char const* getServiceName();

    status_t dump(int fd, const Vector<String16>& args) override;

    binder::Status preDumpUiData(const std::string& callingPackage) override;

    binder::Status startBugreport(int32_t calling_uid, const std::string& calling_package,
                                  android::base::unique_fd bugreport_fd,
                                  android::base::unique_fd screenshot_fd, int bugreport_mode,
                                  int bugreport_flags, const sp<IDumpstateListener>& listener,
                                  bool is_screenshot_requested, bool skip_user_consent) override;

    binder::Status retrieveBugreport(int32_t calling_uid,
                                     const std::string& calling_package,
                                     int32_t user_id,
                                     android::base::unique_fd bugreport_fd,
                                     const std::string& bugreport_file,
                                     const bool keep_bugreport_on_retrieval,
                                     const bool skip_user_consent,
                                     const sp<IDumpstateListener>& listener)
                                     override;

    binder::Status cancelBugreport(int32_t calling_uid,
                                   const std::string& calling_package) override;

  private:
    // Dumpstate object which contains all the bugreporting logic.
    // Note that dumpstate is a oneshot service, so this object is meant to be used at most for
    // one bugreport.
    // This service does not own this object.
    Dumpstate* ds_;
    int32_t calling_uid_;
    std::string calling_package_;
    std::mutex lock_;
};

}  // namespace os
}  // namespace android

#endif  // ANDROID_OS_DUMPSTATE_H_
