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
#include <string>
#include <thread>
#include <unordered_map>

struct LayerData
{
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t instanceVersion = VK_API_VERSION_1_0;
    std::unique_ptr<VkLayerDispatchTable> deviceDispatchTable;
    std::unique_ptr<VkLayerInstanceDispatchTable> instanceDispatchTable;
    std::unordered_map<VkDebugUtilsMessengerEXT, VkDebugUtilsMessengerCreateInfoEXT> debugCallbacks;
    PFN_vkSetDeviceLoaderData fpSetDeviceLoaderData = nullptr;
    // std::bitset<Extension::EXTENSION_COUNT> enabledExtensions;
};

typedef uintptr_t DispatchKey;
static std::mutex g_layerDataMutex;
static std::unordered_map<DispatchKey, LayerData*> g_layerDataMap;

static const VkLayerProperties g_layerProperties = {DEBUG_LAYER_NAME, VK_LAYER_API_VERSION, 1, "Vulkan Debug Layer"};

template <typename T>
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

VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                                              const VkAllocationCallbacks* pAllocator, VkInstance* pInstance)
{
    RS_TRACE_NAME(__func__);
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
    layer_init_instance_dispatch_table(*pInstance, instanceLayerData->instanceDispatchTable.get(), getInstanceProc);
    return result;
}

VKAPI_ATTR void VKAPI_CALL DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator)
{
    RS_TRACE_NAME(__func__);
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
    RS_TRACE_NAME(__func__);
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
        (PFN_vkCreateDevice)fpGetInstanceProcAddr(physicalDeviceData->instance, "vkCreateDevice");
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
    RS_TRACE_NAME(__func__);
    DispatchKey deviceKey = GetDispatchKey(device);
    LayerData* deviceData = GetLayerData(deviceKey);
    deviceData->deviceDispatchTable->DestroyDevice(device, pAllocator);
    FreeLayerData(deviceKey);
}

VKAPI_ATTR VkResult VKAPI_CALL EnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pCount,
                                                                    VkExtensionProperties* pProperties)
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

VKAPI_ATTR VkResult VKAPI_CALL EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice,
                                                                  const char* pLayerName, uint32_t* pCount,
                                                                  VkExtensionProperties* pProperties)
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

VKAPI_ATTR VkResult VKAPI_CALL hook_vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo,
                                                     const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory)
{
    RS_TRACE_NAME("hook_vkAllocateMemory");
    DispatchKey deviceKey = GetDispatchKey(device);
    LayerData* deviceData = GetLayerData(deviceKey);
    return deviceData->deviceDispatchTable->AllocateMemory(device, pAllocateInfo, pAllocator, pMemory);
}

VKAPI_ATTR void VKAPI_CALL hook_vkFreeMemory(VkDevice device, VkDeviceMemory memory,
                                             const VkAllocationCallbacks* pAllocator)
{
    RS_TRACE_NAME("hook_vkFreeMemory");

    DispatchKey deviceKey = GetDispatchKey(device);
    LayerData* deviceData = GetLayerData(deviceKey);
    deviceData->deviceDispatchTable->FreeMemory(device, memory, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL hook_vkAllocateCommandBuffers(VkDevice device,
                                                             const VkCommandBufferAllocateInfo* pAllocateInfo,
                                                             VkCommandBuffer* pCommandBuffers)
{
    RS_TRACE_NAME("hook_vkAllocateCommandBuffers");
    DispatchKey deviceKey = GetDispatchKey(device);
    LayerData* deviceData = GetLayerData(deviceKey);
    return deviceData->deviceDispatchTable->AllocateCommandBuffers(device, pAllocateInfo, pCommandBuffers);
}

VKAPI_ATTR VkResult VKAPI_CALL hook_vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits,
                                                  VkFence fence)
{
    RS_TRACE_NAME("hook_vkQueueSubmit");
    DispatchKey deviceKey = GetDispatchKey(queue);
    LayerData* deviceData = GetLayerData(deviceKey);
    return deviceData->deviceDispatchTable->QueueSubmit(queue, submitCount, pSubmits, fence);
}

VKAPI_ATTR void VKAPI_CALL hook_vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool,
                                                     uint32_t commandBufferCount,
                                                     const VkCommandBuffer* pCommandBuffers)
{
    RS_TRACE_NAME("hook_vkFreeCommandBuffers");
    DispatchKey deviceKey = GetDispatchKey(device);
    LayerData* deviceData = GetLayerData(deviceKey);
    return deviceData->deviceDispatchTable->FreeCommandBuffers(device, commandPool, commandBufferCount,
                                                               pCommandBuffers);
}

VKAPI_ATTR VkResult VKAPI_CALL hook_vkBeginCommandBuffer(VkCommandBuffer commandBuffer,
                                                         const VkCommandBufferBeginInfo* pBeginInfo)
{
    RS_TRACE_NAME("hook_vkBeginCommandBuffer");
    DispatchKey deviceKey = GetDispatchKey(commandBuffer);
    LayerData* deviceData = GetLayerData(deviceKey);
    return deviceData->deviceDispatchTable->BeginCommandBuffer(commandBuffer, pBeginInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL hook_vkEndCommandBuffer(VkCommandBuffer commandBuffer)
{
    RS_TRACE_NAME("hook_vkEndCommandBuffer");
    DispatchKey deviceKey = GetDispatchKey(commandBuffer);
    LayerData* deviceData = GetLayerData(deviceKey);
    return deviceData->deviceDispatchTable->EndCommandBuffer(commandBuffer);
}

VKAPI_ATTR void VKAPI_CALL hook_vkCmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
                                                  VkDeviceSize dstOffset, VkDeviceSize dataSize, const void* pData)
{
    RS_TRACE_NAME("hook_vkCmdUpdateBuffer");
    DispatchKey deviceKey = GetDispatchKey(commandBuffer);
    LayerData* deviceData = GetLayerData(deviceKey);
    return deviceData->deviceDispatchTable->CmdUpdateBuffer(commandBuffer, dstBuffer, dstOffset, dataSize, pData);
}

VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer,
                                                uint32_t regionCount, const VkBufferCopy* pRegions)
{
    RS_TRACE_NAME("hook_vkCmdCopyBuffer");
    DispatchKey deviceKey = GetDispatchKey(commandBuffer);
    LayerData* deviceData = GetLayerData(deviceKey);
    return deviceData->deviceDispatchTable->CmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, regionCount, pRegions);
}

VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage,
                                               VkImageLayout srcImageLayout, VkImage dstImage,
                                               VkImageLayout dstImageLayout, uint32_t regionCount,
                                               const VkImageCopy* pRegions)
{
    RS_TRACE_NAME("hook_vkCmdCopyImage");
    DispatchKey deviceKey = GetDispatchKey(commandBuffer);
    LayerData* deviceData = GetLayerData(deviceKey);
    deviceData->deviceDispatchTable->CmdCopyImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout,
                                                  regionCount, pRegions);
}

VKAPI_ATTR void VKAPI_CALL hook_vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage,
                                               VkImageLayout srcImageLayout, VkImage dstImage,
                                               VkImageLayout dstImageLayout, uint32_t regionCount,
                                               const VkImageBlit* pRegions, VkFilter filter)
{
    RS_TRACE_NAME("hook_vkCmdBlitImage");
    DispatchKey deviceKey = GetDispatchKey(commandBuffer);
    LayerData* deviceData = GetLayerData(deviceKey);
    deviceData->deviceDispatchTable->CmdBlitImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout,
                                                  regionCount, pRegions, filter);
}

VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer,
                                                       VkImage dstImage, VkImageLayout dstImageLayout,
                                                       uint32_t regionCount, const VkBufferImageCopy* pRegions)
{
    RS_TRACE_NAME("hook_vkCmdCopyBufferToImage");
    DispatchKey deviceKey = GetDispatchKey(commandBuffer);
    LayerData* deviceData = GetLayerData(deviceKey);
    deviceData->deviceDispatchTable->CmdCopyBufferToImage(commandBuffer, srcBuffer, dstImage, dstImageLayout,
                                                          regionCount, pRegions);
}

VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage,
                                                       VkImageLayout srcImageLayout, VkBuffer dstBuffer,
                                                       uint32_t regionCount, const VkBufferImageCopy* pRegions)
{
    RS_TRACE_NAME("hook_vkCmdCopyImageToBuffer");
    DispatchKey deviceKey = GetDispatchKey(commandBuffer);
    LayerData* deviceData = GetLayerData(deviceKey);
    deviceData->deviceDispatchTable->CmdCopyImageToBuffer(commandBuffer, srcImage, srcImageLayout, dstBuffer,
                                                          regionCount, pRegions);
}

VKAPI_ATTR void VKAPI_CALL hook_vkCmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
                                                VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data)
{
    RS_TRACE_NAME("hook_vkCmdFillBuffer");
    DispatchKey deviceKey = GetDispatchKey(commandBuffer);
    LayerData* deviceData = GetLayerData(deviceKey);
    deviceData->deviceDispatchTable->CmdFillBuffer(commandBuffer, dstBuffer, dstOffset, size, data);
}

VKAPI_ATTR void VKAPI_CALL hook_vkCmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image,
                                                     VkImageLayout imageLayout, const VkClearColorValue* pColor,
                                                     uint32_t rangeCount, const VkImageSubresourceRange* pRanges)
{
    RS_TRACE_NAME("hook_vkCmdClearColorImage");
    DispatchKey deviceKey = GetDispatchKey(commandBuffer);
    LayerData* deviceData = GetLayerData(deviceKey);
    deviceData->deviceDispatchTable->CmdClearColorImage(commandBuffer, image, imageLayout, pColor, rangeCount, pRanges);
}

VKAPI_ATTR void VKAPI_CALL hook_vkCmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image,
                                                            VkImageLayout imageLayout,
                                                            const VkClearDepthStencilValue* pDepthStencil,
                                                            uint32_t rangeCount, const VkImageSubresourceRange* pRanges)
{
    RS_TRACE_NAME("hook_vkCmdClearDepthStencilImage");
    DispatchKey deviceKey = GetDispatchKey(commandBuffer);
    LayerData* deviceData = GetLayerData(deviceKey);
    deviceData->deviceDispatchTable->CmdClearDepthStencilImage(commandBuffer, image, imageLayout, pDepthStencil,
                                                               rangeCount, pRanges);
}

VKAPI_ATTR void VKAPI_CALL hook_vkCmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount,
                                                      const VkClearAttachment* pAttachments, uint32_t rectCount,
                                                      const VkClearRect* pRects)
{
    RS_TRACE_NAME("hook_vkCmdClearAttachments");
    DispatchKey deviceKey = GetDispatchKey(commandBuffer);
    LayerData* deviceData = GetLayerData(deviceKey);
    deviceData->deviceDispatchTable->CmdClearAttachments(commandBuffer, attachmentCount, pAttachments, rectCount,
                                                         pRects);
}

VKAPI_ATTR void VKAPI_CALL hook_vkCmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage,
                                                  VkImageLayout srcImageLayout, VkImage dstImage,
                                                  VkImageLayout dstImageLayout, uint32_t regionCount,
                                                  const VkImageResolve* pRegions)
{
    RS_TRACE_NAME("hook_vkCmdResolveImage");
    DispatchKey deviceKey = GetDispatchKey(commandBuffer);
    LayerData* deviceData = GetLayerData(deviceKey);
    deviceData->deviceDispatchTable->CmdResolveImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout,
                                                     regionCount, pRegions);
}

VKAPI_ATTR void VKAPI_CALL hook_vkCmdBeginRenderPass(VkCommandBuffer commandBuffer,
                                                     const VkRenderPassBeginInfo* pRenderPassBegin,
                                                     VkSubpassContents contents)
{
    RS_TRACE_NAME("hook_vkCmdBeginRenderPass");
    DispatchKey deviceKey = GetDispatchKey(commandBuffer);
    LayerData* deviceData = GetLayerData(deviceKey);
    deviceData->deviceDispatchTable->CmdBeginRenderPass(commandBuffer, pRenderPassBegin, contents);
}

VKAPI_ATTR void VKAPI_CALL hook_vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount,
                                          uint32_t firstVertex, uint32_t firstInstance)
{
    RS_TRACE_NAME("hook_vkCmdDraw");
    DispatchKey deviceKey = GetDispatchKey(commandBuffer);
    LayerData* deviceData = GetLayerData(deviceKey);
    deviceData->deviceDispatchTable->CmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount,
                                                 uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset,
                                                 uint32_t firstInstance)
{
    RS_TRACE_NAME("hook_vkCmdDrawIndexed");
    DispatchKey deviceKey = GetDispatchKey(commandBuffer);
    LayerData* deviceData = GetLayerData(deviceKey);
    deviceData->deviceDispatchTable->CmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset,
                                                    firstInstance);
}

VKAPI_ATTR void VKAPI_CALL hook_vkCmdEndRenderPass(VkCommandBuffer commandBuffer)
{
    RS_TRACE_NAME("hook_vkCmdEndRenderPass");
    DispatchKey deviceKey = GetDispatchKey(commandBuffer);
    LayerData* deviceData = GetLayerData(deviceKey);
    deviceData->deviceDispatchTable->CmdEndRenderPass(commandBuffer);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device, const char* funcName)
{
    if (!funcName) return nullptr;

    if (std::strcmp(funcName, "vkGetDeviceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(GetDeviceProcAddr);
    }
    if (std::strcmp(funcName, "vkDestroyDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(DestroyDevice);
    }
    if (strcmp("vkAllocateMemory", funcName) == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkAllocateMemory);
    }

    if (!strcmp(funcName, "vkFreeMemory")) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkFreeMemory);
    if (!strcmp(funcName, "vkAllocateCommandBuffers"))
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkAllocateCommandBuffers);
    if (!strcmp(funcName, "vkFreeCommandBuffers"))
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkFreeCommandBuffers);
    if (!strcmp(funcName, "vkBeginCommandBuffer"))
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkBeginCommandBuffer);
    if (!strcmp(funcName, "vkEndCommandBuffer")) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkEndCommandBuffer);
    if (!strcmp(funcName, "vkQueueSubmit")) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkQueueSubmit);

    if (!strcmp(funcName, "vkCmdCopyBuffer")) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdCopyBuffer);
    if (!strcmp(funcName, "vkCmdCopyImage")) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdCopyImage);
    if (!strcmp(funcName, "vkCmdBlitImage")) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdBlitImage);
    if (!strcmp(funcName, "vkCmdCopyBufferToImage"))
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdCopyBufferToImage);
    if (!strcmp(funcName, "vkCmdCopyImageToBuffer"))
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdCopyImageToBuffer);
    if (!strcmp(funcName, "vkCmdUpdateBuffer")) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdUpdateBuffer);
    if (!strcmp(funcName, "vkCmdFillBuffer")) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdFillBuffer);
    if (!strcmp(funcName, "vkCmdClearColorImage"))
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdClearColorImage);
    if (!strcmp(funcName, "vkCmdClearDepthStencilImage"))
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdClearDepthStencilImage);
    if (!strcmp(funcName, "vkCmdClearAttachments"))
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdClearAttachments);
    if (!strcmp(funcName, "vkCmdResolveImage")) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdResolveImage);

    if (!strcmp(funcName, "vkCmdEndRenderPass")) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdEndRenderPass);
    if (!strcmp(funcName, "vkCmdDrawIndexed")) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdDrawIndexed);
    if (!strcmp(funcName, "vkCmdDraw")) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdDraw);
    if (!strcmp(funcName, "vkCmdBeginRenderPass"))
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdBeginRenderPass);

    LayerData* devData = GetLayerData(GetDispatchKey(device));
    VkLayerDispatchTable* pTable = devData->deviceDispatchTable.get();
    if (!pTable || pTable->GetDeviceProcAddr == nullptr) {
        return nullptr;
    }
    return pTable->GetDeviceProcAddr(device, funcName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance, const char* funcName)
{
    if (!funcName) return nullptr;

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

    if (strcmp("vkAllocateMemory", funcName) == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkAllocateMemory);
    }

    if (!strcmp(funcName, "vkFreeMemory")) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkFreeMemory);
    if (!strcmp(funcName, "vkAllocateCommandBuffers"))
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkAllocateCommandBuffers);
    if (!strcmp(funcName, "vkFreeCommandBuffers"))
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkFreeCommandBuffers);
    if (!strcmp(funcName, "vkBeginCommandBuffer"))
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkBeginCommandBuffer);
    if (!strcmp(funcName, "vkEndCommandBuffer")) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkEndCommandBuffer);
    if (!strcmp(funcName, "vkQueueSubmit")) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkQueueSubmit);

    if (!strcmp(funcName, "vkCmdCopyBuffer")) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdCopyBuffer);
    if (!strcmp(funcName, "vkCmdCopyImage")) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdCopyImage);
    if (!strcmp(funcName, "vkCmdBlitImage")) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdBlitImage);
    if (!strcmp(funcName, "vkCmdCopyBufferToImage"))
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdCopyBufferToImage);
    if (!strcmp(funcName, "vkCmdCopyImageToBuffer"))
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdCopyImageToBuffer);
    if (!strcmp(funcName, "vkCmdUpdateBuffer")) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdUpdateBuffer);
    if (!strcmp(funcName, "vkCmdFillBuffer")) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdFillBuffer);
    if (!strcmp(funcName, "vkCmdClearColorImage"))
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdClearColorImage);
    if (!strcmp(funcName, "vkCmdClearDepthStencilImage"))
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdClearDepthStencilImage);
    if (!strcmp(funcName, "vkCmdClearAttachments"))
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdClearAttachments);
    if (!strcmp(funcName, "vkCmdResolveImage")) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdResolveImage);

    if (!strcmp(funcName, "vkCmdEndRenderPass")) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdEndRenderPass);
    if (!strcmp(funcName, "vkCmdDrawIndexed")) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdDrawIndexed);
    if (!strcmp(funcName, "vkCmdDraw")) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdDraw);
    if (!strcmp(funcName, "vkCmdBeginRenderPass"))
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdBeginRenderPass);

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

DEBUG_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance,
                                                                                  const char* funcName)
{
    return GetInstanceProcAddr(instance, funcName);
}

DEBUG_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties)
{
    return EnumerateInstanceExtensionProperties(pLayerName, pPropertyCount, pProperties);
}

DEBUG_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount,
                                     VkExtensionProperties* pProperties)
{
    return EnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pPropertyCount, pProperties);
}

DEBUG_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(uint32_t* pPropertyCount,
                                                                                     VkLayerProperties* pProperties)
{
    return EnumerateInstanceLayerProperties(pPropertyCount, pProperties);
}
}