# 一、Vulkan介绍

Vulkan 是一个适用于高性能 3D 图形设备的低开销、跨平台 API。与 OpenGL ES (GLES) 一样，Vulkan 提供用于在应用中创建高品质实时图形的工具。与OpenGL ES相比使用 Vulkan 的优势明显，Vulkan可以大大降低 CPU 开销，另外Vulkan支持 SPIR-V 二进制Shader语言。


# 二、Vulkan-Loader

![High Level View of Loader](docs/images/high_level_loader.png)

Vulkan-Loader处于应用程序和GPU驱动之间，负责加载GPU驱动提供给应用开发者使用，Vulkan-Loader作为Vulkan技术的系统级支持，主要功能如下: 

1、系统上支持一个或多个支持 Vulkan 的驱动程序，而不会相互干扰。

2、支持 Vulkan Layers，Vulkan Layer作为可选模块，可以由应用程序、开发人员或标准系统设置启用。

## 1、Vulkan-Loader官方文档

[LoaderInterfaceArchitecture.md](docs/LoaderInterfaceArchitecture.md)

[LoaderApplicationInterface.md](docs/LoaderApplicationInterface.md)

[LoaderDriverInterface.md](docs/LoaderDriverInterface.md)

[LoaderLayerInterface.md](docs/LoaderLayerInterface.md)


## 2、驱动程序和Vulkan Layer的扫描与加载

Vulkan-Loader加载驱动程序和Vulkan Layer都是通过配置json清单文件的方式。

json清单文件应放在以下三个目录下，否则无法识别

`/system/etc/`

`/vendor/etc/`

`/data/`

### 1) 驱动程序

驱动的json清单文件需要放在上述三个目录下的`vulkan/icd.d/`目录下，否则无法识别。

驱动json清单文件示例

```json
{
   "file_format_version": "1.0.1",
   "ICD": {
      "library_path": "path to driver library",
      "api_version": "1.2.205",
      "library_arch" : "64",
      "is_portability_driver": false
   }
}
```

详见[Driver Manifest File Format](docs/LoaderDriverInterface.md#driver-manifest-file-format)

### 2) Vulkan Layer

Vulkan Layer的json清单文件需要放在上述三个目录下的`vulkan/implicit_layer.d/`和`vulkan/explicit_layer.d/`目录下，否则无法识别。

`vulkan/implicit_layer.d/`目录下的Layer会被Vulkan-Loader默认加载，`vulkan/explicit_layer.d/`目录下的Layer不会被默认加载，需要主动开启。

下面给出[swapchain_layer](#swapchain_layer)的json清单文件示例

```json
{
    "file_format_version" : "1.0.0",
    "layer" : {
        "name": "VK_LAYER_OpenHarmony_OHOS_surface",
        "type": "GLOBAL",
        "library_path": "libvulkan_swapchain.so",
        "api_version": "1.3.231",
        "implementation_version": "1",
        "description": "Vulkan Swapchain",
        "disable_environment": {
            "DISABLE_OPENHARMONY_SWAPCHAIN_LAYER": "1"
        },
        "instance_extensions": [
            { "name": "VK_KHR_surface", "spec_version": "25" },
            { "name": "VK_OpenHarmony_OHOS_surface", "spec_version": "1" }
        ],
        "device_extensions": [
            { "name": "VK_KHR_swapchain", "spec_version": "70" }
        ]
    }
}
```

Vulkan Layer的json清单文件格式的详情请见[Layer Manifest File Format](docs/LoaderLayerInterface.md#layer-manifest-file-format)

**建议：** 驱动的json清单文件放到`/vendor/etc/vulkan/icd.d/`目录下，Vulkan Layer的json清单文件放到`/system/etc/vulkan/implicit_layer.d/`和`/system/etc/vulkan/explicit_layer.d/`目录下。


# 三、Vulkan API

Vulkan-Loader当前支持的Vulkan API版本为v1.3.231，实际使用时支持的Vulkan API版本以GPU驱动的实现为准。

# 四、Window System Integration(WSI)

## swapchain_layer

swapchain_layer是实现Vulkan WSI(Window System Integration, 窗口系统集成) 与OpenHarmony平台本地窗口(OHNativeWindow)对接的模块，作为一个隐式加载的vulkan layer使用。

swapchain_layer实现了下面几个扩展：

**VK_OpenHarmony_OHOS_surface 扩展**

**VK_KHR_surface 扩展**

**VK_KHR_swapchain 扩展**

代码地址：[swapchain_layer](https://gitee.com/openharmony/graphic_graphic_2d/tree/master/frameworks/vulkan_layers/swapchain_layer)

WSI 实现依赖**VK_OHOS_native_buffer 扩展**；此扩展仅由驱动开发者实现，仅WSI使用，WSI不会提供给应用。


# 五、构建指导

参考[BUILD.md](BUILD.md)


# 六、License

见 [LICENSE](LICENSE.txt).