/*
 * Copyright 2018 Henri Verbeet for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"
#include "wine/port.h"
#include "wined3d_private.h"

#include "wine/vulkan_driver.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d);

static inline const struct wined3d_adapter_vk *wined3d_adapter_vk_const(const struct wined3d_adapter *adapter)
{
    return CONTAINING_RECORD(adapter, struct wined3d_adapter_vk, a);
}

static const char *debug_vk_version(uint32_t version)
{
    return wine_dbg_sprintf("%u.%u.%u",
            VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), VK_VERSION_PATCH(version));
}

#ifdef USE_WIN32_VULKAN
static BOOL wined3d_load_vulkan(struct wined3d_vk_info *vk_info)
{
    struct vulkan_ops *vk_ops = &vk_info->vk_ops;

    if (!(vk_info->vulkan_lib = LoadLibraryA("vulkan-1.dll")))
    {
        WARN("Failed to load vulkan-1.dll.\n");
        return FALSE;
    }

    vk_ops->vkGetInstanceProcAddr = (void *)GetProcAddress(vk_info->vulkan_lib, "vkGetInstanceProcAddr");
    if (!vk_ops->vkGetInstanceProcAddr)
    {
        FreeLibrary(vk_info->vulkan_lib);
        return FALSE;
    }

    return TRUE;
}

static void wined3d_unload_vulkan(struct wined3d_vk_info *vk_info)
{
    if (vk_info->vulkan_lib)
    {
        FreeLibrary(vk_info->vulkan_lib);
        vk_info->vulkan_lib = NULL;
    }
}
#else
static BOOL wined3d_load_vulkan(struct wined3d_vk_info *vk_info)
{
    struct vulkan_ops *vk_ops = &vk_info->vk_ops;
    const struct vulkan_funcs *vk_funcs;
    HDC dc;

    dc = GetDC(0);
    vk_funcs = __wine_get_vulkan_driver(dc, WINE_VULKAN_DRIVER_VERSION);
    ReleaseDC(0, dc);

    if (!vk_funcs)
        return FALSE;

    vk_ops->vkGetInstanceProcAddr = (void *)vk_funcs->p_vkGetInstanceProcAddr;
    return TRUE;
}

static void wined3d_unload_vulkan(struct wined3d_vk_info *vk_info) {}
#endif

static void adapter_vk_destroy(struct wined3d_adapter *adapter)
{
    struct wined3d_adapter_vk *adapter_vk = wined3d_adapter_vk(adapter);
    struct wined3d_vk_info *vk_info = &adapter_vk->vk_info;

    VK_CALL(vkDestroyInstance(vk_info->instance, NULL));
    wined3d_unload_vulkan(vk_info);
    wined3d_adapter_cleanup(&adapter_vk->a);
    heap_free(adapter_vk);
}

static BOOL adapter_vk_create_context(struct wined3d_context *context,
        struct wined3d_texture *target, const struct wined3d_format *ds_format)
{
    return TRUE;
}

static void adapter_vk_get_wined3d_caps(const struct wined3d_adapter *adapter, struct wined3d_caps *caps)
{
    const struct wined3d_adapter_vk *adapter_vk = wined3d_adapter_vk_const(adapter);
    const VkPhysicalDeviceLimits *limits = &adapter_vk->device_limits;
    BOOL sampler_anisotropy = limits->maxSamplerAnisotropy > 1.0f;

    caps->ddraw_caps.dds_caps |= WINEDDSCAPS_3DDEVICE
            | WINEDDSCAPS_MIPMAP
            | WINEDDSCAPS_TEXTURE
            | WINEDDSCAPS_VIDEOMEMORY
            | WINEDDSCAPS_ZBUFFER;
    caps->ddraw_caps.caps |= WINEDDCAPS_3D;

    caps->Caps2 |= WINED3DCAPS2_CANGENMIPMAP;

    caps->PrimitiveMiscCaps |= WINED3DPMISCCAPS_BLENDOP
            | WINED3DPMISCCAPS_SEPARATEALPHABLEND
            | WINED3DPMISCCAPS_INDEPENDENTWRITEMASKS
            | WINED3DPMISCCAPS_POSTBLENDSRGBCONVERT;

    if (sampler_anisotropy)
    {
        caps->RasterCaps |= WINED3DPRASTERCAPS_ANISOTROPY
                | WINED3DPRASTERCAPS_ZBIAS
                | WINED3DPRASTERCAPS_MIPMAPLODBIAS;

        caps->TextureFilterCaps |= WINED3DPTFILTERCAPS_MAGFANISOTROPIC
                | WINED3DPTFILTERCAPS_MINFANISOTROPIC;

        caps->MaxAnisotropy = limits->maxSamplerAnisotropy;
    }

    caps->SrcBlendCaps |= WINED3DPBLENDCAPS_BLENDFACTOR;
    caps->DestBlendCaps |= WINED3DPBLENDCAPS_BLENDFACTOR
            | WINED3DPBLENDCAPS_SRCALPHASAT;

    caps->TextureCaps |= WINED3DPTEXTURECAPS_VOLUMEMAP
            | WINED3DPTEXTURECAPS_MIPVOLUMEMAP
            | WINED3DPTEXTURECAPS_VOLUMEMAP_POW2;
    caps->VolumeTextureFilterCaps |= WINED3DPTFILTERCAPS_MAGFLINEAR
            | WINED3DPTFILTERCAPS_MAGFPOINT
            | WINED3DPTFILTERCAPS_MINFLINEAR
            | WINED3DPTFILTERCAPS_MINFPOINT
            | WINED3DPTFILTERCAPS_MIPFLINEAR
            | WINED3DPTFILTERCAPS_MIPFPOINT
            | WINED3DPTFILTERCAPS_LINEAR
            | WINED3DPTFILTERCAPS_LINEARMIPLINEAR
            | WINED3DPTFILTERCAPS_LINEARMIPNEAREST
            | WINED3DPTFILTERCAPS_MIPLINEAR
            | WINED3DPTFILTERCAPS_MIPNEAREST
            | WINED3DPTFILTERCAPS_NEAREST;
    caps->VolumeTextureAddressCaps |= WINED3DPTADDRESSCAPS_INDEPENDENTUV
            | WINED3DPTADDRESSCAPS_CLAMP
            | WINED3DPTADDRESSCAPS_WRAP;
    caps->VolumeTextureAddressCaps |= WINED3DPTADDRESSCAPS_BORDER
            | WINED3DPTADDRESSCAPS_MIRROR
            | WINED3DPTADDRESSCAPS_MIRRORONCE;

    caps->MaxVolumeExtent = limits->maxImageDimension3D;

    caps->TextureCaps |= WINED3DPTEXTURECAPS_CUBEMAP
            | WINED3DPTEXTURECAPS_MIPCUBEMAP
            | WINED3DPTEXTURECAPS_CUBEMAP_POW2;
    caps->CubeTextureFilterCaps |= WINED3DPTFILTERCAPS_MAGFLINEAR
            | WINED3DPTFILTERCAPS_MAGFPOINT
            | WINED3DPTFILTERCAPS_MINFLINEAR
            | WINED3DPTFILTERCAPS_MINFPOINT
            | WINED3DPTFILTERCAPS_MIPFLINEAR
            | WINED3DPTFILTERCAPS_MIPFPOINT
            | WINED3DPTFILTERCAPS_LINEAR
            | WINED3DPTFILTERCAPS_LINEARMIPLINEAR
            | WINED3DPTFILTERCAPS_LINEARMIPNEAREST
            | WINED3DPTFILTERCAPS_MIPLINEAR
            | WINED3DPTFILTERCAPS_MIPNEAREST
            | WINED3DPTFILTERCAPS_NEAREST;

    if (sampler_anisotropy)
    {
        caps->CubeTextureFilterCaps |= WINED3DPTFILTERCAPS_MAGFANISOTROPIC
                | WINED3DPTFILTERCAPS_MINFANISOTROPIC;
    }

    caps->TextureAddressCaps |= WINED3DPTADDRESSCAPS_BORDER
            | WINED3DPTADDRESSCAPS_MIRROR
            | WINED3DPTADDRESSCAPS_MIRRORONCE;

    caps->StencilCaps |= WINED3DSTENCILCAPS_DECR
            | WINED3DSTENCILCAPS_INCR
            | WINED3DSTENCILCAPS_TWOSIDED;

    caps->DeclTypes |= WINED3DDTCAPS_FLOAT16_2 | WINED3DDTCAPS_FLOAT16_4;

    caps->MaxPixelShader30InstructionSlots = WINED3DMAX30SHADERINSTRUCTIONS;
    caps->MaxVertexShader30InstructionSlots = WINED3DMAX30SHADERINSTRUCTIONS;
    caps->PS20Caps.temp_count = WINED3DPS20_MAX_NUMTEMPS;
    caps->VS20Caps.temp_count = WINED3DVS20_MAX_NUMTEMPS;
}

static BOOL adapter_vk_check_format(const struct wined3d_adapter *adapter,
        const struct wined3d_format *adapter_format, const struct wined3d_format *rt_format,
        const struct wined3d_format *ds_format)
{
    return TRUE;
}

static const struct wined3d_adapter_ops wined3d_adapter_vk_ops =
{
    adapter_vk_destroy,
    adapter_vk_create_context,
    adapter_vk_get_wined3d_caps,
    adapter_vk_check_format,
};

static unsigned int wined3d_get_wine_vk_version(void)
{
    const char *ptr = PACKAGE_VERSION;
    int major, minor;

    major = atoi(ptr);

    while (isdigit(*ptr) || *ptr == '.')
        ++ptr;

    minor = atoi(ptr);

    return VK_MAKE_VERSION(major, minor, 0);
}

static const struct
{
    const char *name;
    unsigned int core_since_version;
    BOOL required;
}
vulkan_instance_extensions[] =
{
    {VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, VK_API_VERSION_1_1, FALSE},
};

static BOOL enable_vulkan_instance_extensions(uint32_t *extension_count,
        const char *enabled_extensions[], const struct wined3d_vk_info *vk_info)
{
    PFN_vkEnumerateInstanceExtensionProperties pfn_vkEnumerateInstanceExtensionProperties;
    VkExtensionProperties *extensions = NULL;
    BOOL success = FALSE, found;
    unsigned int i, j, count;
    VkResult vr;

    *extension_count = 0;

    if (!(pfn_vkEnumerateInstanceExtensionProperties
            = (void *)VK_CALL(vkGetInstanceProcAddr(NULL, "vkEnumerateInstanceExtensionProperties"))))
    {
        WARN("Failed to get 'vkEnumerateInstanceExtensionProperties'.\n");
        goto done;
    }

    if ((vr = pfn_vkEnumerateInstanceExtensionProperties(NULL, &count, NULL)) < 0)
    {
        WARN("Failed to count instance extensions, vr %d.\n", vr);
        goto done;
    }
    if (!(extensions = heap_calloc(count, sizeof(*extensions))))
    {
        WARN("Out of memory.\n");
        goto done;
    }
    if ((vr = pfn_vkEnumerateInstanceExtensionProperties(NULL, &count, extensions)) < 0)
    {
        WARN("Failed to enumerate extensions, vr %d.\n", vr);
        goto done;
    }

    for (i = 0; i < ARRAY_SIZE(vulkan_instance_extensions); ++i)
    {
        if (vulkan_instance_extensions[i].core_since_version <= vk_info->api_version)
            continue;

        for (j = 0, found = FALSE; j < count; ++j)
        {
            if (!strcmp(extensions[j].extensionName, vulkan_instance_extensions[i].name))
            {
                found = TRUE;
                break;
            }
        }
        if (found)
        {
            TRACE("Enabling instance extension '%s'.\n", vulkan_instance_extensions[i].name);
            enabled_extensions[(*extension_count)++] = vulkan_instance_extensions[i].name;
        }
        else if (!found && vulkan_instance_extensions[i].required)
        {
            WARN("Required extension '%s' is not available.\n", vulkan_instance_extensions[i].name);
            goto done;
        }
    }
    success = TRUE;

done:
    heap_free(extensions);
    return success;
}

static BOOL wined3d_init_vulkan(struct wined3d_vk_info *vk_info)
{
    const char *enabled_instance_extensions[ARRAY_SIZE(vulkan_instance_extensions)];
    PFN_vkEnumerateInstanceVersion pfn_vkEnumerateInstanceVersion;
    struct vulkan_ops *vk_ops = &vk_info->vk_ops;
    VkInstance instance = VK_NULL_HANDLE;
    VkInstanceCreateInfo instance_info;
    VkApplicationInfo app_info;
    uint32_t api_version = 0;
    char app_name[MAX_PATH];
    VkResult vr;

    if (!wined3d_load_vulkan(vk_info))
        return FALSE;

    if (!(vk_ops->vkCreateInstance = (void *)VK_CALL(vkGetInstanceProcAddr(NULL, "vkCreateInstance"))))
    {
        ERR("Failed to get 'vkCreateInstance'.\n");
        goto fail;
    }

    vk_info->api_version = VK_API_VERSION_1_0;
    if ((pfn_vkEnumerateInstanceVersion = (void *)VK_CALL(vkGetInstanceProcAddr(NULL, "vkEnumerateInstanceVersion")))
            && pfn_vkEnumerateInstanceVersion(&api_version) == VK_SUCCESS)
    {
        TRACE("Vulkan instance API version %s.\n", debug_vk_version(api_version));

        if (api_version >= VK_API_VERSION_1_1)
            vk_info->api_version = VK_API_VERSION_1_1;
    }

    memset(&app_info, 0, sizeof(app_info));
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    if (wined3d_get_app_name(app_name, ARRAY_SIZE(app_name)))
        app_info.pApplicationName = app_name;
    app_info.pEngineName = "Damavand";
    app_info.engineVersion = wined3d_get_wine_vk_version();
    app_info.apiVersion = vk_info->api_version;

    memset(&instance_info, 0, sizeof(instance_info));
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app_info;
    instance_info.ppEnabledExtensionNames = enabled_instance_extensions;
    if (!enable_vulkan_instance_extensions(&instance_info.enabledExtensionCount, enabled_instance_extensions, vk_info))
        goto fail;

    if ((vr = VK_CALL(vkCreateInstance(&instance_info, NULL, &instance))) < 0)
    {
        WARN("Failed to create Vulkan instance, vr %d.\n", vr);
        goto fail;
    }

    TRACE("Created Vulkan instance %p.\n", instance);

#define LOAD_INSTANCE_PFN(name) \
    if (!(vk_ops->name = (void *)VK_CALL(vkGetInstanceProcAddr(instance, #name)))) \
    { \
        WARN("Could not get instance proc addr for '" #name "'.\n"); \
        goto fail; \
    }
#define LOAD_INSTANCE_OPT_PFN(name) \
    vk_ops->name = (void *)VK_CALL(vkGetInstanceProcAddr(instance, #name));
#define VK_INSTANCE_PFN     LOAD_INSTANCE_PFN
#define VK_INSTANCE_EXT_PFN LOAD_INSTANCE_OPT_PFN
#define VK_DEVICE_PFN       LOAD_INSTANCE_PFN
    VK_INSTANCE_FUNCS()
    VK_DEVICE_FUNCS()
#undef VK_INSTANCE_PFN
#undef VK_INSTANCE_EXT_PFN
#undef VK_DEVICE_PFN

#define MAP_INSTANCE_FUNCTION(core_pfn, ext_pfn) \
    if (!vk_ops->core_pfn) \
        vk_ops->core_pfn = (void *)VK_CALL(vkGetInstanceProcAddr(instance, #ext_pfn));
    MAP_INSTANCE_FUNCTION(vkGetPhysicalDeviceProperties2, vkGetPhysicalDeviceProperties2KHR)
#undef MAP_INSTANCE_FUNCTION

    vk_info->instance = instance;

    return TRUE;

fail:
    if (vk_ops->vkDestroyInstance)
        VK_CALL(vkDestroyInstance(instance, NULL));
    wined3d_unload_vulkan(vk_info);
    return FALSE;
}

static VkPhysicalDevice get_vulkan_physical_device(struct wined3d_vk_info *vk_info)
{
    VkPhysicalDevice physical_devices[1];
    uint32_t count;
    VkResult vr;

    if ((vr = VK_CALL(vkEnumeratePhysicalDevices(vk_info->instance, &count, NULL))) < 0)
    {
        WARN("Failed to enumerate physical devices, vr %d.\n", vr);
        return VK_NULL_HANDLE;
    }
    if (!count)
    {
        WARN("No physical device.\n");
        return VK_NULL_HANDLE;
    }
    if (count > 1)
    {
        /* TODO: Create wined3d_adapter for each device. */
        FIXME("Multiple physical devices available.\n");
        count = 1;
    }

    if ((vr = VK_CALL(vkEnumeratePhysicalDevices(vk_info->instance, &count, physical_devices))) < 0)
    {
        WARN("Failed to get physical devices, vr %d.\n", vr);
        return VK_NULL_HANDLE;
    }

    return physical_devices[0];
}

const struct wined3d_gpu_description *get_vulkan_gpu_description(const VkPhysicalDeviceProperties *properties)
{
    const struct wined3d_gpu_description *description;

    TRACE("Device name: %s.\n", debugstr_a(properties->deviceName));
    TRACE("Vendor ID: 0x%04x, Device ID: 0x%04x.\n", properties->vendorID, properties->deviceID);
    TRACE("Driver version: %#x.\n", properties->driverVersion);
    TRACE("API version: %s.\n", debug_vk_version(properties->apiVersion));

    if ((description = wined3d_get_gpu_description(properties->vendorID, properties->deviceID)))
        return description;

    FIXME("Failed to retrieve GPU description for device %s %04x:%04x.\n",
            debugstr_a(properties->deviceName), properties->vendorID, properties->deviceID);

    return wined3d_get_gpu_description(HW_VENDOR_AMD, CARD_AMD_RADEON_RX_VEGA);
}

static BOOL wined3d_adapter_vk_init(struct wined3d_adapter_vk *adapter_vk,
        unsigned int ordinal, unsigned int wined3d_creation_flags)
{
    struct wined3d_vk_info *vk_info = &adapter_vk->vk_info;
    const struct wined3d_gpu_description *gpu_description;
    struct wined3d_adapter *adapter = &adapter_vk->a;
    VkPhysicalDeviceIDProperties id_properties;
    VkPhysicalDeviceProperties2 properties2;

    TRACE("adapter_vk %p, ordinal %u, wined3d_creation_flags %#x.\n",
            adapter_vk, ordinal, wined3d_creation_flags);

    if (!wined3d_adapter_init(adapter, ordinal))
        return FALSE;

    if (!wined3d_init_vulkan(vk_info))
    {
        WARN("Failed to initialize Vulkan.\n");
        goto fail;
    }

    if (!(adapter_vk->physical_device = get_vulkan_physical_device(vk_info)))
        goto fail_vulkan;

    memset(&id_properties, 0, sizeof(id_properties));
    id_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties2.pNext = &id_properties;

    if (vk_info->vk_ops.vkGetPhysicalDeviceProperties2)
        VK_CALL(vkGetPhysicalDeviceProperties2(adapter_vk->physical_device, &properties2));
    else
        VK_CALL(vkGetPhysicalDeviceProperties(adapter_vk->physical_device, &properties2.properties));
    adapter_vk->device_limits = properties2.properties.limits;

    if (!(gpu_description = get_vulkan_gpu_description(&properties2.properties)))
    {
        ERR("Failed to get GPU description.\n");
        goto fail_vulkan;
    }
    wined3d_driver_info_init(&adapter->driver_info, gpu_description, wined3d_settings.emulated_textureram);

    memcpy(&adapter->driver_uuid, id_properties.driverUUID, sizeof(adapter->driver_uuid));
    memcpy(&adapter->device_uuid, id_properties.deviceUUID, sizeof(adapter->device_uuid));

    if (!wined3d_adapter_vk_init_format_info(adapter_vk, vk_info))
        goto fail_vulkan;

    adapter->vertex_pipe = &none_vertex_pipe;
    adapter->fragment_pipe = &none_fragment_pipe;
    adapter->shader_backend = &none_shader_backend;
    adapter->adapter_ops = &wined3d_adapter_vk_ops;

    adapter->d3d_info.wined3d_creation_flags = wined3d_creation_flags;

    return TRUE;

fail_vulkan:
    VK_CALL(vkDestroyInstance(vk_info->instance, NULL));
    wined3d_unload_vulkan(vk_info);
fail:
    wined3d_adapter_cleanup(adapter);
    return FALSE;
}

struct wined3d_adapter *wined3d_adapter_vk_create(unsigned int ordinal,
        unsigned int wined3d_creation_flags)
{
    struct wined3d_adapter_vk *adapter_vk;

    if (!(adapter_vk = heap_alloc_zero(sizeof(*adapter_vk))))
        return NULL;

    if (!wined3d_adapter_vk_init(adapter_vk, ordinal, wined3d_creation_flags))
    {
        heap_free(adapter_vk);
        return NULL;
    }

    TRACE("Created adapter %p.\n", adapter_vk);

    return &adapter_vk->a;
}
