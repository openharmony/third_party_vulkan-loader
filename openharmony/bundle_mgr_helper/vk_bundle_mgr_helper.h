/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.. All rights reserved.
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
#include "vulkan/vulkan_core.h"
#ifdef __cplusplus
#include <singleton.h>
#include "bundle_mgr_interface.h"

extern "C" {
#endif // __cplusplus

bool InitBundleInfo(char* debugHapName);
char* GetDebugLayerLibPath(const struct loader_instance *inst, VkSystemAllocationScope allocation_sacope);

#ifdef __cplusplus
}
namespace OHOS {
namespace AppExecFwk {
class VKBundleMgrHelper : public std::enable_shared_from_this<VKBundleMgrHelper> {
public:
    DISALLOW_COPY_AND_MOVE(VKBundleMgrHelper);

    BundleInfo g_bundleInfo;
    ErrCode GetBundleInfoForSelf(AppExecFwk::GetBundleInfoFlag flags, BundleInfo& bundleInfo);

private:
    sptr<AppExecFwk::IBundleMgr> Connect();

    void OnDeath();

private:
    DECLARE_DELAYED_SINGLETON(VKBundleMgrHelper);

    std::mutex mutex_;
    sptr<AppExecFwk::IBundleMgr> bundleMgr_;
    sptr<IRemoteObject::DeathRecipient> deathRecipient_ = nullptr;
};

class VKBundleMgrServiceDeathRecipient : public IRemoteObject::DeathRecipient {
public:
    explicit VKBundleMgrServiceDeathRecipient(
        const std::function<void(const wptr<IRemoteObject>& object)>& deathCallback);
    DISALLOW_COPY_AND_MOVE(VKBundleMgrServiceDeathRecipient);
    virtual ~VKBundleMgrServiceDeathRecipient() = default;
    void OnRemoteDied(const wptr<IRemoteObject>& object) override;

private:
    std::function<void (const wptr<IRemoteObject>& object)> deathCallback_;
};
}
}
#endif // __cplusplus