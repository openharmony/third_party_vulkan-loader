/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, Hardware
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <gtest/gtest.h>
#include "bundle_mgr_helper/vk_bundle_mgr_helper.h"

using namespace testing;
using namespace testing::ext;

namespace OHOS::Rosen {
class VkBundleMgrHelperTest : public testing::Test {
public:
    static void SetUpTestCase() {}
    static void TearDownTestCase() {}
    void SetUp() {}
    void TearDown() {}
};
/**
 * @tc.name: Init001
 * @tc.desc:
 * @tc.type: FUNC
 */
HWTEST_F(VkBundleMgrHelperTest, VkBundleMgrHelperTest, Level1)
{
    AppExecFwk::BundleInfo bundleInfo;
    auto vkBundleMgrHelper = DelayedSingleton<AppExecFwk::VKBundleMgrHelper>::GetInstance();
    auto result = vkBundleMgrHelper->GetBundleInfoForSelf(
        AppExecFwk::GetBundleInfoFlag::GET_BUNDLE_INFO_WITH_APPLICATION, bundleInfo);
    EXPECT_NE(result, ERR_OK);

    bool ret = InitBundleInfo(NULL);
    EXPECT_EQ(ret, false);

    string name = "\0";
    ret = InitBundleInfo(const_cast<char*>(name.c_str())); 
    EXPECT_EQ(ret, false);

    string fatalName = "randomFatalName";
    ret = InitBundleInfo(const_cast<char*>(fatalName.c_str()));
    EXPECT_EQ(ret, false); 

    ret = InitBundleInfo((const_cast<char*>(bundleInfo.name.c_str())));
    EXPECT_EQ(ret, false);

    char* path = GetDebugLayerLibPath();
    EXPECT_NE(path, nullptr);
    free(path);
}
}