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
#include "vk_bundle_mgr_helper.h"

#include "iservice_registry.h"
#include "system_ability_definition.h"
#include "../loader_hilog.h"
#include <securec.h>
#define EOK 0

extern "C" {
    void *loader_instance_heap_calloc(const struct loader_instance *instance,
                                      size_t size, VkSystemAllocationScope allocation_scope);
}
constexpr const char *DEBUG_SANDBOX_DIR = "/data/storage/el1/bundle/";

bool InitBundleInfo(char* debugHapName)
{
    if (NULL == debugHapName || '\0' == debugHapName[0]) {
        VKHILOGE("debug_hap_name is NULL!");
        return false;
    }
    std::string debugHap(debugHapName);
    auto vkBundleMgrHelper = OHOS::DelayedSingleton<OHOS::AppExecFwk::VKBundleMgrHelper>::GetInstance();
    if (vkBundleMgrHelper == nullptr) {
        VKHILOGE("vkBundleMgrHelper is null!");
        return false;
    }

    if (vkBundleMgrHelper->GetBundleInfoForSelf(
        OHOS::AppExecFwk::GetBundleInfoFlag::GET_BUNDLE_INFO_WITH_APPLICATION, vkBundleMgrHelper->g_bundleInfo) ==
        OHOS::ERR_OK) {
        if (vkBundleMgrHelper->g_bundleInfo.name == debugHap) {
            return true;
        } else {
            VKHILOGE("this hap is %{public}s, the debug hap is %{public}s",
                vkBundleMgrHelper->g_bundleInfo.name.c_str(), debugHap.c_str());
        }
    } else {
        VKHILOGE("Call GetBundleInfoForSelf func failed!");
    }
    return false;
}

bool CheckAppProvisionTypeIsDebug()
{
    auto vkBundleMgrHelper = OHOS::DelayedSingleton<OHOS::AppExecFwk::VKBundleMgrHelper>::GetInstance();
    if (vkBundleMgrHelper == nullptr) {
        VKHILOGE("vkBundleMgrHelper is null!");
        return false;
    }
    VKHILOGD("this hap is %{public}s, the debug hap appProvisionType is %{public}s",
            vkBundleMgrHelper->g_bundleInfo.name.c_str(), vkBundleMgrHelper->g_bundleInfo.applicationInfo.appProvisionType.c_str());
    if (vkBundleMgrHelper->g_bundleInfo.applicationInfo.appProvisionType == "release") {
        return false;
    }
    return true;
}

char* GetDebugLayerLibPath(const struct loader_instance *inst, VkSystemAllocationScope allocation_sacope)
{
    auto vkBundleMgrHelper = OHOS::DelayedSingleton<OHOS::AppExecFwk::VKBundleMgrHelper>::GetInstance();
    std::string pathStr(DEBUG_SANDBOX_DIR);
    std::string appLibPath = pathStr + vkBundleMgrHelper->g_bundleInfo.applicationInfo.nativeLibraryPath + "/";
    const char* fullPath = appLibPath.c_str();
    size_t len = strlen(fullPath) + 1;
    char* libPath = static_cast<char*>(loader_instance_heap_calloc(inst, len, allocation_sacope));
    if (libPath == NULL) {
        VKHILOGE("malloc libPath fail");
        return NULL;
    }
    if (memcpy_s(libPath, len, fullPath, len) != EOK) {
        VKHILOGE("memcpy_s libPath fail, fullPath: %{public}s", fullPath);
        return NULL;
    }
    VKHILOGD("GetDebugLayerLibPath(): the libPath is %{public}s", libPath);
    return libPath;
}

namespace OHOS {
namespace AppExecFwk {
VKBundleMgrHelper::VKBundleMgrHelper() {}

VKBundleMgrHelper::~VKBundleMgrHelper()
{
    if (bundleMgr_ != nullptr && bundleMgr_->AsObject() != nullptr && deathRecipient_ != nullptr) {
        bundleMgr_->AsObject()->RemoveDeathRecipient(deathRecipient_);
    }
}

sptr<AppExecFwk::IBundleMgr> VKBundleMgrHelper::Connect()
{
    VKHILOGD("Call VKBundleMgrHelper::Connect");
    std::lock_guard<std::mutex> lock(mutex_);
    if (bundleMgr_ == nullptr) {
        sptr<ISystemAbilityManager> systemAbilityManager =
                SystemAbilityManagerClient::GetInstance().GetSystemAbilityManager();
        if (systemAbilityManager == nullptr) {
            VKHILOGE("Failed to get system ability manager.");
            return nullptr;
        }
        sptr<IRemoteObject> remoteObject_ = systemAbilityManager->GetSystemAbility(
            BUNDLE_MGR_SERVICE_SYS_ABILITY_ID);
        if (remoteObject_ == nullptr || (bundleMgr_ = iface_cast<IBundleMgr>(remoteObject_)) == nullptr) {
            VKHILOGE("Failed to get bundle mgr service remote object");
            return nullptr;
        }
        std::weak_ptr<VKBundleMgrHelper> weakPtr = shared_from_this();
        auto deathCallback = [weakPtr](const wptr<IRemoteObject> &object) {
            auto sharedPtr = weakPtr.lock();
            if (sharedPtr == nullptr) {
                VKHILOGE("Bundle helper instance is nullptr");
                return;
            }
            sharedPtr->OnDeath();
        };
        deathRecipient_ = new(std::nothrow) VKBundleMgrServiceDeathRecipient(deathCallback);
        if (deathRecipient_ == nullptr) {
            VKHILOGE("Failed to create death recipient ptr deathRecipient_!");
            return nullptr;
        }
        if (bundleMgr_->AsObject() != nullptr) {
            bundleMgr_->AsObject()->AddDeathRecipient(deathRecipient_);
        }
    }
    return bundleMgr_;
}

void VKBundleMgrHelper::OnDeath()
{
    VKHILOGD("Call VKBundleMgrHelper::OnDeath");
    std::lock_guard<std::mutex> lock(mutex_);
    if (bundleMgr_ == nullptr || bundleMgr_->AsObject() == nullptr) {
        VKHILOGE("bundleMgr_ is nullptr!");
        return;
    }
    bundleMgr_->AsObject()->RemoveDeathRecipient(deathRecipient_);
    bundleMgr_ = nullptr;
}

ErrCode VKBundleMgrHelper::GetBundleInfoForSelf(AppExecFwk::GetBundleInfoFlag flags, BundleInfo &bundleInfo)
{
    VKHILOGD("Call VKBundleMgrHealper::GetBundleInfoForSelf.");
    auto bundleMgr_ = Connect();
    if (bundleMgr_ == nullptr) {
        VKHILOGE("Failed to connect.");
        return -1;
    }
    return bundleMgr_->GetBundleInfoForSelf(static_cast<int32_t>(flags), bundleInfo);
}

VKBundleMgrServiceDeathRecipient::VKBundleMgrServiceDeathRecipient(
    const std::function<void(const wptr<IRemoteObject>& object)>& deathCallback)
    : deathCallback_(deathCallback) {}

void VKBundleMgrServiceDeathRecipient::OnRemoteDied(const wptr<IRemoteObject>& object)
{
    if (deathCallback_ != nullptr) {
        deathCallback_(object);
    }
}
}
}