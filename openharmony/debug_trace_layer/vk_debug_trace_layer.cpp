/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

#include "vk_debug_trace_layer.h"

#include <cstring>
#include <memory>
#include <scoped_bytrace.h>
#include <string>
#include <thread>
#include <unordered_map>

struct LayerData {
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t instanceVersion = VK_API_VERSION_1_0;
    std::unique_ptr<VkLayerDispatchTable> deviceDispatchTable;
    std::unique_ptr<VkLayerInstanceDispatchTable> instanceDispatchTable;
    std::unordered_map<VkDebugUtilsMessengerEXT, VkDebugUtilsMessengerCreateInfoEXT> debugCallbacks;
    PFN_vkSetDeviceLoaderData fpSetDeviceLoaderData = nullptr;
    std::bitset<Extension::EXTENSION_COUNT> enabledExtensions;
};

typedef uintptr_t DispatchKey;
static std::mutex g_layerDataMutex;
static std::unordered_map<DispatchKey, LayerData*> g_layerDataMap;

static const VkLayerProperties g_layerProperties = { DEBUG_LAYER_NAME, VK_LAYER_API_VERSION, 1, "Vulkan Debug Layer" };

template<typename T>
DispatchKey GetDispatchKey(const T object)
{
    return reinterpret_cast<DispatchKey>(*reinterpret_cast<void* const*>(object));
}

VkLayerInstanceCreateInfo* GetChainInfo(const VkInstanceCreateInfo* pCreateInfo, VkLayerFunction func)
{
    auto chainInfo = static_cast<const VkLayerInstanceCreateInfo*>(pCreateInfo->pNext);
    while (chainInfo) {
        if (chainInfo->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && chainInfo->function == func) {
            return const_cast<VkLayerInstanceCreateInfo*>(chainInfo);
        }
        chainInfo = static_cast<const VkLayerInstanceCreateInfo*>(chainInfo->pNext);
    }
    return nullptr;
}

VkLayerDeviceCreateInfo* GetChainInfo(const VkDeviceCreateInfo* pCreateInfo, VkLayerFunction func)
{
    auto chainInfo = static_cast<const VkLayerDeviceCreateInfo*>(pCreateInfo->pNext);
    while (chainInfo) {
        if (chainInfo->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && chainInfo->function == func) {
            return const_cast<VkLayerDeviceCreateInfo*>(chainInfo);
        }
        chainInfo = static_cast<const VkLayerDeviceCreateInfo*>(chainInfo->pNext);
    }
    return nullptr;
}

LayerData* GetLayerData(DispatchKey dataKey)
{
    std::lock_guard<std::mutex> lock(g_layerDataMutex);
    LayerData* layerData = nullptr;
    auto it = g_layerDataMap.find(dataKey);
    if (it == g_layerDataMap.end()) {
        layerData = new LayerData();
        g_layerDataMap[dataKey] = layerData;
    } else {
        layerData = it->second;
    }
    return layerData;
}

void FreeLayerData(DispatchKey key)
{
    std::lock_guard<std::mutex> lock(g_layerDataMutex);
    auto it = g_layerDataMap.find(key);
    if (it != g_layerDataMap.end()) {
        delete it->second;
        g_layerDataMap.erase(it);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(
    const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance)
{
    ScopedBytrace trace(__func__);
    if (!pCreateInfo || !pAllocator || !pInstance) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkLayerInstanceCreateInfo* chainInfo = GetChainInfo(pCreateInfo, VK_LAYER_LINK_INFO);
    if (!chainInfo || !chainInfo->u.pLayerInfo) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    PFN_vkGetInstanceProcAddr getInstanceProc = chainInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkCreateInstance createInstanceFunc =
        reinterpret_cast<PFN_vkCreateInstance>(getInstanceProc(nullptr, "vkCreateInstance"));
    if (!createInstanceFunc) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    chainInfo->u.pLayerInfo = chainInfo->u.pLayerInfo->pNext;
    VkResult result = createInstanceFunc(pCreateInfo, pAllocator, pInstance);
    if (result != VK_SUCCESS) {
        return result;
    }
    LayerData* instanceLayerData = GetLayerData(GetDispatchKey(*pInstance));
    instanceLayerData->instance = *pInstance;
    instanceLayerData->instanceDispatchTable = std::make_unique<VkLayerInstanceDispatchTable>();
    layer_init_instance_dispatch_table(
        *pInstance, instanceLayerData->instanceDispatchTable.get(), getInstanceProc);
    return result;
}

VKAPI_ATTR void VKAPI_CALL DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator)
{
    ScopedBytrace trace(__func__);
    if (instance == VK_NULL_HANDLE) {
        return;
    }

    DispatchKey instanceKey = GetDispatchKey(instance);
    LayerData* curLayerData = GetLayerData(instanceKey);
    curLayerData->instanceDispatchTable->DestroyInstance(instance, pAllocator);
    FreeLayerData(instanceKey);
}

VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkDevice* pDevice)
{
    ScopedBytrace trace(__func__);
    if (!pCreateInfo || !pAllocator || !pDevice) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    DispatchKey physicalDeviceKey = GetDispatchKey(physicalDevice);
    LayerData* physicalDeviceData = GetLayerData(physicalDeviceKey);

    VkDeviceCreateInfo createInfo = *pCreateInfo;
    std::vector<const char*> enabledExtensions = {};
    for (uint32_t i = 0; i < createInfo.enabledExtensionCount; i++) {
        enabledExtensions.push_back(createInfo.ppEnabledExtensionNames[i]);
    }
    createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
    createInfo.ppEnabledExtensionNames = enabledExtensions.data();

    VkLayerDeviceCreateInfo* chainInfo = GetChainInfo(pCreateInfo, VK_LAYER_LINK_INFO);
    if (!chainInfo || !chainInfo->u.pLayerInfo) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = chainInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr fpGetDeviceProcAddr = chainInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    PFN_vkCreateDevice fpCreateDevice =
        (PFN_vkCreateDevice)fpGetInstanceProcAddr(gpuLayerData->instance, "vkCreateDevice");
    if (fpCreateDevice == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    chainInfo->u.pLayerInfo = chainInfo->u.pLayerInfo->pNext;

    VkResult result = fpCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
    if (result != VK_SUCCESS) {
        return result;
    }

    LayerData* deviceData = GetLayerData(GetDispatchKey(*pDevice));
    deviceData->device = *pDevice;
    deviceData->instance = physicalDeviceData->instance;
    deviceData->deviceDispatchTable = std::make_unique<VkLayerDispatchTable>();
    layer_init_device_dispatch_table(*pDevice, deviceData->deviceDispatchTable.get(), fpGetDeviceProcAddr);

    VkLayerDeviceCreateInfo* callbackInfo = GetChainInfo(pCreateInfo, VK_LOADER_DATA_CALLBACK);
    if (callbackInfo == nullptr || callbackInfo->u.pfnSetDeviceLoaderData == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    deviceData->fpSetDeviceLoaderData = callbackInfo->u.pfnSetDeviceLoaderData;

    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator)
{
    ScopedBytrace trace(__func__);
    DispatchKey deviceKey = GetDispatchKey(device);
    LayerData* deviceData = GetLayerData(deviceKey);
    deviceData->deviceDispatchTable->DestroyDevice(device, pAllocator);
    FreeLayerData(deviceKey);
}

VKAPI_ATTR VkResult VKAPI_CALL EnumerateInstanceExtensionProperties(
    const char* pLayerName, uint32_t* pCount, VkExtensionProperties* pProperties)
{
    if (pLayerName == nullptr || pCount == nullptr) {
        return VK_ERROR_LAYER_NOT_PRESENT;
    }
    if (std::strcmp(pLayerName, DEBUG_LAYER_NAME) != 0) {
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

    if (pProperties == nullptr) {
        *pCount = 0;
        return VK_SUCCESS;
    } else {
        if (*pCount > 0) {
            *pCount = 0;
        }
        return VK_SUCCESS;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL EnumerateInstanceLayerProperties(uint32_t* pCount, VkLayerProperties* pProperties)
{
    if (pCount == nullptr) {
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    if (pProperties == nullptr) {
        *pCount = 1;
        return VK_SUCCESS;
    } else if (*pCount >= 1) {
        *pProperties = g_layerProperties;
        *pCount = 1;
        return VK_SUCCESS;
    } else {
        *pCount = 1;
        return VK_INCOMPLETE;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL EnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pCount, VkExtensionProperties* pProperties)
{
    if (pLayerName == nullptr || pCount == nullptr) {
        return VK_ERROR_LAYER_NOT_PRESENT;
    }
    if (std::strcmp(pLayerName, DEBUG_LAYER_NAME) != 0) {
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

    if (pProperties == nullptr) {
        *pCount = 0;
        return VK_SUCCESS;
    } else {
        if (*pCount > 0) {
            *pCount = 0;
        }
        return VK_SUCCESS;
    }
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device, const char* funcName)
{
    if (!funcName)
        return nullptr;

    if (std::strcmp(funcName, "vkGetDeviceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(GetDeviceProcAddr);
    }
    if (std::strcmp(funcName, "vkDestroyDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(DestroyDevice);
    }

    LayerData* devData = GetLayerData(GetDispatchKey(device));
    VkLayerDispatchTable* pTable = devData->deviceDispatchTable.get();
    if (!pTable || pTable->GetDeviceProcAddr == nullptr) {
        return nullptr;
    }
    return pTable->GetDeviceProcAddr(device, funcName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance, const char* funcName)
{
    if (!funcName)
        return nullptr;

    if (std::strcmp(funcName, "vkGetInstanceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(GetInstanceProcAddr);
    }
    if (std::strcmp(funcName, "vkCreateInstance") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(CreateInstance);
    }
    if (std::strcmp(funcName, "vkDestroyInstance") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(DestroyInstance);
    }
    if (std::strcmp(funcName, "vkCreateDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(CreateDevice);
    }
    if (std::strcmp(funcName, "vkEnumerateInstanceExtensionProperties") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(EnumerateInstanceExtensionProperties);
    }
    if (std::strcmp(funcName, "vkEnumerateInstanceLayerProperties") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(EnumerateInstanceLayerProperties);
    }
    if (std::strcmp(funcName, "vkEnumerateDeviceExtensionProperties") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(EnumerateDeviceExtensionProperties);
    }

    LayerData* layerData = GetLayerData(GetDispatchKey(instance));
    VkLayerInstanceDispatchTable* pTable = layerData->instanceDispatchTable.get();
    if (pTable == nullptr) {
        return nullptr;
    }
    if (pTable->GetInstanceProcAddr == nullptr) {
        return nullptr;
    }
    return pTable->GetInstanceProcAddr(instance, funcName);
}

#if defined(__GNUC__) && __GNUC__ >= 4
#define DEBUG_LAYER_EXPORT __attribute__((visibility("default")))
#elif defined(_WIN32)
#define DEBUG_LAYER_EXPORT __declspec(dllexport)
#else
#define DEBUG_LAYER_EXPORT
#endif

extern "C" {
DEBUG_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* funcName)
{
    return GetDeviceProcAddr(device, funcName);
}

DEBUG_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance instance, const char* funcName)
{
    return GetInstanceProcAddr(instance, funcName);
}

DEBUG_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties)
{
    return EnumerateInstanceExtensionProperties(pLayerName, pPropertyCount, pProperties);
}

DEBUG_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice,
    const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties)
{
    return EnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pPropertyCount, pProperties);
}

DEBUG_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(
    uint32_t* pPropertyCount, VkLayerProperties* pProperties)
{
    return EnumerateInstanceLayerProperties(pPropertyCount, pProperties);
}
}