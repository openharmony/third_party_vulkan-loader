/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

#include <cstring>
#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <vulkan/vulkan_core.h>

#include "vk_debug_trace_layer.h"

using namespace testing;
using namespace testing::ext;
using namespace OHOS::Rosen;

// Mock Vulkan structures and functions
struct MockVulkanFunctions {
    // Instance functions
    PFN_vkCreateInstance createInstance = nullptr;
    PFN_vkDestroyInstance destroyInstance = nullptr;
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;

    // Device functions
    PFN_vkCreateDevice createDevice = nullptr;
    PFN_vkDestroyDevice destroyDevice = nullptr;
    PFN_vkGetDeviceProcAddr getDeviceProcAddr = nullptr;

    // Enumeration functions
    PFN_vkEnumerateInstanceExtensionProperties enumerateInstanceExtensionProperties = nullptr;
    PFN_vkEnumerateDeviceExtensionProperties enumerateDeviceExtensionProperties = nullptr;
    PFN_vkEnumerateInstanceLayerProperties enumerateInstanceLayerProperties = nullptr;
};

// Global mock instance
static MockVulkanFunctions g_mockFunctions;

// Mock implementation of Vulkan functions
VKAPI_ATTR VkResult VKAPI_CALL MockCreateInstance(
    const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance)
{
    *pInstance = reinterpret_cast<VkInstance>(0x12345678);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL MockDestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator)
{
    // Do nothing for mock
}

VKAPI_ATTR VkResult VKAPI_CALL MockCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkDevice* pDevice)
{
    *pDevice = reinterpret_cast<VkDevice>(0x87654321);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL MockDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator)
{
    // Do nothing for mock
}

VKAPI_ATTR VkResult VKAPI_CALL MockEnumerateInstanceExtensionProperties(
    const char* pLayerName, uint32_t* pCount, VkExtensionProperties* pProperties)
{
    if (pLayerName && std::strcmp(pLayerName, DEBUG_LAYER_NAME) == 0) {
        *pCount = 0;
        return VK_SUCCESS;
    }
    return VK_ERROR_LAYER_NOT_PRESENT;
}

VKAPI_ATTR VkResult VKAPI_CALL MockEnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pCount, VkExtensionProperties* pProperties)
{
    if (pLayerName && std::strcmp(pLayerName, DEBUG_LAYER_NAME) == 0) {
        *pCount = 0;
        return VK_SUCCESS;
    }
    return VK_ERROR_LAYER_NOT_PRESENT;
}

VKAPI_ATTR VkResult VKAPI_CALL MockEnumerateInstanceLayerProperties(uint32_t* pCount, VkLayerProperties* pProperties)
{
    if (pProperties == nullptr) {
        *pCount = 1;
        return VK_SUCCESS;
    } else {
        if (*pCount >= 1) {
            VkLayerProperties prop = { DEBUG_LAYER_NAME, VK_LAYER_API_VERSION, 1, "Vulkan Debug Layer" };
            *pProperties = prop;
            *pCount = 1;
            return VK_SUCCESS;
        } else {
            *pCount = 1;
            return VK_INCOMPLETE;
        }
    }
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL MockGetInstanceProcAddr(VkInstance instance, const char* funcName)
{
    if (std::strcmp(funcName, "vkCreateInstance") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(MockCreateInstance);
    }
    if (std::strcmp(funcName, "vkDestroyInstance") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(MockDestroyInstance);
    }
    if (std::strcmp(funcName, "vkCreateDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(MockCreateDevice);
    }
    if (std::strcmp(funcName, "vkDestroyDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(MockDestroyDevice);
    }
    return nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL MockGetDeviceProcAddr(VkDevice device, const char* funcName)
{
    if (std::strcmp(funcName, "vkDestroyDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(MockDestroyDevice);
    }
    return nullptr;
}

class VulkanLayerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Initialize mock functions
        g_mockFunctions.createInstance = MockCreateInstance;
        g_mockFunctions.destroyInstance = MockDestroyInstance;
        g_mockFunctions.getInstanceProcAddr = MockGetInstanceProcAddr;
        g_mockFunctions.createDevice = MockCreateDevice;
        g_mockFunctions.destroyDevice = MockDestroyDevice;
        g_mockFunctions.getDeviceProcAddr = MockGetDeviceProcAddr;
        g_mockFunctions.enumerateInstanceExtensionProperties = MockEnumerateInstanceExtensionProperties;
        g_mockFunctions.enumerateDeviceExtensionProperties = MockEnumerateDeviceExtensionProperties;
        g_mockFunctions.enumerateInstanceLayerProperties = MockEnumerateInstanceLayerProperties;

        // Clear any existing layer data
        g_layerDataMap.clear();
    }

    void TearDown() override
    {
        g_layerDataMap.clear();
    }

    VkLayerInstanceCreateInfo CreateInstanceChainInfo()
    {
        VkLayerInstanceCreateInfo chainInfo = {};
        chainInfo.sType = VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO;
        chainInfo.function = VK_LAYER_LINK_INFO;

        static VkLayerInstanceLink layerLink = {};
        layerLink.pNext = nullptr;
        layerLink.pfnNextGetInstanceProcAddr = MockGetInstanceProcAddr;

        chainInfo.u.pLayerInfo = &layerLink;
        return chainInfo;
    }

    VkLayerDeviceCreateInfo CreateDeviceChainInfo()
    {
        VkLayerDeviceCreateInfo chainInfo = {};
        chainInfo.sType = VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO;
        chainInfo.function = VK_LAYER_LINK_INFO;

        static VkLayerDeviceLink layerLink = {};
        layerLink.pNext = nullptr;
        layerLink.pfnNextGetInstanceProcAddr = MockGetInstanceProcAddr;
        layerLink.pfnNextGetDeviceProcAddr = MockGetDeviceProcAddr;

        chainInfo.u.pLayerInfo = &layerLink;
        return chainInfo;
    }
};

/**
 * @tc.name: test GetInstanceProcAddrReturnsValidFunctions
 * @tc.desc: test func GetInstanceProcAddr Return Valid Functions
 * @tc.type: FUNC
 * @tc.require: issueICXU7L
 */
HWTEST_F(VulkanLayerTest, GetInstanceProcAddrReturnsValidFunctions, TestSize.Level1)
{
    PFN_vkGetInstanceProcAddr func = GetInstanceProcAddr;

    // Test core functions
    EXPECT_NE(func(nullptr, "vkCreateInstance"), nullptr);
    EXPECT_NE(func(nullptr, "vkDestroyInstance"), nullptr);
    EXPECT_NE(func(nullptr, "vkCreateDevice"), nullptr);

    // Test enumeration functions
    EXPECT_NE(func(nullptr, "vkEnumerateInstanceExtensionProperties"), nullptr);
    EXPECT_NE(func(nullptr, "vkEnumerateDeviceExtensionProperties"), nullptr);
    EXPECT_NE(func(nullptr, "vkEnumerateInstanceLayerProperties"), nullptr);

    // Test self-reference
    EXPECT_NE(func(nullptr, "vkGetInstanceProcAddr"), nullptr);
}

/**
 * @tc.name: test GetDeviceProcAddrReturnsValidFunctions
 * @tc.desc: test func GetDeviceProcAddr Return Valid Functions
 * @tc.type: FUNC
 * @tc.require: issueICXU7L
 */
HWTEST_F(VulkanLayerTest, GetDeviceProcAddrReturnsValidFunctions, TestSize.Level1)
{
    VkDevice mockDevice = reinterpret_cast<VkDevice>(0x12345678);
    PFN_vkGetDeviceProcAddr func = GetDeviceProcAddr;

    // Test core functions
    EXPECT_NE(func(mockDevice, "vkDestroyDevice"), nullptr);

    // Test self-reference
    EXPECT_NE(func(mockDevice, "vkGetDeviceProcAddr"), nullptr);
}

/**
 * @tc.name: test CreateAndDestroyInstance
 * @tc.desc: test func Create And Destroy Instance
 * @tc.type: FUNC
 * @tc.require: issueICXU7L
 */
HWTEST_F(VulkanLayerTest, CreateAndDestroyInstance, TestSize.Level1)
{
    VkLayerInstanceCreateInfo chainInfo = CreateInstanceChainInfo();

    VkInstanceCreateInfo instanceInfo = {};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pNext = &chainInfo;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = CreateInstance(&instanceInfo, nullptr, &instance);

    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_NE(instance, VK_NULL_HANDLE);
    EXPECT_FALSE(g_layerDataMap.empty());

    // Test destruction
    DestroyInstance(instance, nullptr);
    EXPECT_TRUE(g_layerDataMap.empty());
}

/**
 * @tc.name: test CreateAndDestroyDevice
 * @tc.desc: test func Create And Destroy Device
 * @tc.type: FUNC
 * @tc.require: issueICXU7L
 */
HWTEST_F(VulkanLayerTest, CreateAndDestroyDevice, TestSize.Level1)
{
    // First create an instance
    VkLayerInstanceCreateInfo instanceChainInfo = CreateInstanceChainInfo();
    VkInstanceCreateInfo instanceInfo = {};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pNext = &instanceChainInfo;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = CreateInstance(&instanceInfo, nullptr, &instance);
    ASSERT_EQ(result, VK_SUCCESS);

    // Now create a device
    VkLayerDeviceCreateInfo deviceChainInfo = CreateDeviceChainInfo();
    VkDeviceCreateInfo deviceInfo = {};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = &deviceChainInfo;

    VkPhysicalDevice physicalDevice = reinterpret_cast<VkPhysicalDevice>(0x11111111);
    VkDevice device = VK_NULL_HANDLE;
    result = CreateDevice(physicalDevice, &deviceInfo, nullptr, &device);

    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_NE(device, VK_NULL_HANDLE);

    // Test device destruction
    DestroyDevice(device, nullptr);

    // Cleanup instance
    DestroyInstance(instance, nullptr);
}

/**
 * @tc.name: test EnumerateInstanceLayerProperties
 * @tc.desc: test func EnumerateInstanceLayerProperties return valid ptr
 * @tc.type: FUNC
 * @tc.require: issueICXU7L
 */
HWTEST_F(VulkanLayerTest, EnumerateInstanceLayerProperties, TestSize.Level1)
{
    uint32_t count = 0;
    VkResult result = EnumerateInstanceLayerProperties(&count, nullptr);

    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 1);

    VkLayerProperties properties;
    result = EnumerateInstanceLayerProperties(&count, &properties);

    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 1);
    EXPECT_STREQ(properties.layerName, DEBUG_LAYER_NAME);
    EXPECT_STREQ(properties.description, "Vulkan Debug Layer");
}

/**
 * @tc.name: test EnumerateInstanceExtensionProperties
 * @tc.desc: test func EnumerateInstanceExtensionProperties return valid ptr
 * @tc.type: FUNC
 * @tc.require: issueICXU7L
 */
HWTEST_F(VulkanLayerTest, EnumerateInstanceExtensionProperties, TestSize.Level1)
{
    uint32_t count = 0;

    // Test with our layer name
    VkResult result = EnumerateInstanceExtensionProperties(DEBUG_LAYER_NAME, &count, nullptr);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 0);

    // Test with null layer name (should return error)
    result = EnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    EXPECT_EQ(result, VK_ERROR_LAYER_NOT_PRESENT);

    // Test with wrong layer name
    result = EnumerateInstanceExtensionProperties("WrongLayerName", &count, nullptr);
    EXPECT_EQ(result, VK_ERROR_LAYER_NOT_PRESENT);
}

/**
 * @tc.name: test EnumerateDeviceExtensionProperties
 * @tc.desc: test func EnumerateDeviceExtensionProperties return valid ptr
 * @tc.type: FUNC
 * @tc.require: issueICXU7L
 */
HWTEST_F(VulkanLayerTest, EnumerateDeviceExtensionProperties, TestSize.Level1)
{
    uint32_t count = 0;
    VkPhysicalDevice physicalDevice = reinterpret_cast<VkPhysicalDevice>(0x11111111);

    // Test with our layer name
    VkResult result = EnumerateDeviceExtensionProperties(physicalDevice, DEBUG_LAYER_NAME, &count, nullptr);
    EXPECT_EQ(result, VK_SUCCESS);
    EXPECT_EQ(count, 0);

    // Test with null layer name (should return error)
    result = EnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr);
    EXPECT_EQ(result, VK_ERROR_LAYER_NOT_PRESENT);

    // Test with wrong layer name
    result = EnumerateDeviceExtensionProperties(physicalDevice, "WrongLayerName", &count, nullptr);
    EXPECT_EQ(result, VK_ERROR_LAYER_NOT_PRESENT);
}

/**
 * @tc.name: test GetDispatchKey
 * @tc.desc: test func GetDispatchKey return valid key
 * @tc.type: FUNC
 * @tc.require: issueICXU7L
 */
HWTEST_F(VulkanLayerTest, GetDispatchKey, TestSize.Level1)
{
    VkInstance instance = reinterpret_cast<VkInstance>(0x12345678);
    VkDevice device = reinterpret_cast<VkDevice>(0x87654321);

    DispatchKey instanceKey = GetDispatchKey(instance);
    DispatchKey deviceKey = GetDispatchKey(device);

    EXPECT_EQ(instanceKey, 0x12345678);
    EXPECT_EQ(deviceKey, 0x87654321);
}

/**
 * @tc.name: test LayerDataManagement
 * @tc.desc: test func LayerDataManagement get key
 * @tc.type: FUNC
 * @tc.require: issueICXU7L
 */
HWTEST_F(VulkanLayerTest, LayerDataManagement, TestSize.Level1)
{
    DispatchKey key = 0x12345678;

    // Test creating new layer data
    LayerData* data1 = GetLayerData(key);
    EXPECT_NE(data1, nullptr);
    EXPECT_FALSE(g_layerDataMap.empty());

    // Test getting existing layer data
    LayerData* data2 = GetLayerData(key);
    EXPECT_EQ(data1, data2);

    // Test freeing layer data
    FreeLayerData(key);
    EXPECT_TRUE(g_layerDataMap.empty());
}

/**
 * @tc.name: test ExportedFunctions
 * @tc.desc: test func Exported Functions
 * @tc.type: FUNC
 * @tc.require: issueICXU7L
 */
HWTEST_F(VulkanLayerTest, ExportedFunctions, TestSize.Level1)
{
    // Test exported GetInstanceProcAddr
    PFN_vkVoidFunction func = vkGetInstanceProcAddr(nullptr, "vkCreateInstance");
    EXPECT_NE(func, nullptr);

    // Test exported GetDeviceProcAddr
    VkDevice device = reinterpret_cast<VkDevice>(0x12345678);
    func = vkGetDeviceProcAddr(device, "vkDestroyDevice");
    EXPECT_NE(func, nullptr);

    // Test exported enumeration functions
    uint32_t count = 0;
    VkResult result = vkEnumerateInstanceLayerProperties(&count, nullptr);
    EXPECT_EQ(result, VK_SUCCESS);
}