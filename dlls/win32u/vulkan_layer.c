/*
 * Chaining a host Vulkan layer in front of the host loader
 *
 * Copyright 2026 SomeoneIsWorking
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

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

#include "ntstatus.h"
#include "win32u_private.h"
#include "ntuser_private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(vulkan);

/* The loader-layer interface of the Vulkan loader (vk_layer.h), version 2. */
typedef enum { VK_LAYER_LINK_INFO = 0 } VkLayerFunction;
enum { LAYER_NEGOTIATE_INTERFACE_STRUCT = 1 };

typedef struct VkLayerInstanceLink_
{
    struct VkLayerInstanceLink_ *pNext;
    PFN_vkGetInstanceProcAddr pfnNextGetInstanceProcAddr;
    PFN_vkVoidFunction pfnNextGetPhysicalDeviceProcAddr;
} VkLayerInstanceLink;

typedef struct
{
    VkStructureType sType;
    const void *pNext;
    VkLayerFunction function;
    union { VkLayerInstanceLink *pLayerInfo; void *other; } u;
} VkLayerInstanceCreateInfo;

typedef struct VkLayerDeviceLink_
{
    struct VkLayerDeviceLink_ *pNext;
    PFN_vkGetInstanceProcAddr pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr pfnNextGetDeviceProcAddr;
} VkLayerDeviceLink;

typedef struct
{
    VkStructureType sType;
    const void *pNext;
    VkLayerFunction function;
    union { VkLayerDeviceLink *pLayerInfo; void *other; } u;
} VkLayerDeviceCreateInfo;

typedef struct
{
    int sType;
    void *pNext;
    uint32_t loaderLayerInterfaceVersion;
    PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr pfnGetDeviceProcAddr;
    PFN_vkVoidFunction pfnGetPhysicalDeviceProcAddr;
} VkNegotiateLayerInterface;

typedef VkResult (VKAPI_PTR *PFN_vkNegotiateLoaderLayerInterfaceVersion)( VkNegotiateLayerInterface * );

static PFN_vkGetInstanceProcAddr host_gipa, layer_gipa;
static PFN_vkGetDeviceProcAddr host_gdpa, layer_gdpa;
static PFN_vkCreateDevice host_create_device;

/* The host loader is not a layer: the link infos are taken out of the chain it gets. */
static const void *skip_link_infos( const void *next )
{
    while (next)
    {
        const VkBaseInStructure *in = next;
        if (in->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO &&
            in->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO) break;
        next = in->pNext;
    }
    return next;
}

static VkResult VKAPI_CALL host_vkCreateInstance( const VkInstanceCreateInfo *create_info,
                                                  const VkAllocationCallbacks *allocator, VkInstance *instance )
{
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)host_gipa( NULL, "vkCreateInstance" );
    VkInstanceCreateInfo info = *create_info;
    VkResult res;

    info.pNext = skip_link_infos( info.pNext );
    if ((res = create( &info, allocator, instance ))) return res;
    if (!host_create_device) host_create_device = (PFN_vkCreateDevice)host_gipa( *instance, "vkCreateDevice" );
    return res;
}

static VkResult VKAPI_CALL host_vkCreateDevice( VkPhysicalDevice physical_device, const VkDeviceCreateInfo *create_info,
                                                const VkAllocationCallbacks *allocator, VkDevice *device )
{
    VkDeviceCreateInfo info = *create_info;

    info.pNext = skip_link_infos( info.pNext );
    return host_create_device( physical_device, &info, allocator, device );
}

/* What the layer sees as the next element of the chain. */
static PFN_vkVoidFunction VKAPI_CALL host_vkGetInstanceProcAddr( VkInstance instance, const char *name )
{
    if (!strcmp( name, "vkCreateInstance" )) return (PFN_vkVoidFunction)host_vkCreateInstance;
    if (!strcmp( name, "vkCreateDevice" )) return (PFN_vkVoidFunction)host_vkCreateDevice;
    return host_gipa( instance, name );
}

static VkResult VKAPI_CALL chain_vkCreateInstance( const VkInstanceCreateInfo *create_info,
                                                   const VkAllocationCallbacks *allocator, VkInstance *instance )
{
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)layer_gipa( NULL, "vkCreateInstance" );
    VkLayerInstanceLink link = { .pfnNextGetInstanceProcAddr = host_vkGetInstanceProcAddr };
    VkLayerInstanceCreateInfo chain =
    {
        .sType = VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO,
        .pNext = create_info->pNext,
        .function = VK_LAYER_LINK_INFO,
        .u.pLayerInfo = &link,
    };
    VkInstanceCreateInfo info = *create_info;

    info.pNext = &chain;
    return create( &info, allocator, instance );
}

static VkResult VKAPI_CALL chain_vkCreateDevice( VkPhysicalDevice physical_device, const VkDeviceCreateInfo *create_info,
                                                 const VkAllocationCallbacks *allocator, VkDevice *device )
{
    PFN_vkCreateDevice create = (PFN_vkCreateDevice)layer_gipa( NULL, "vkCreateDevice" );
    VkLayerDeviceLink link =
    {
        .pfnNextGetInstanceProcAddr = host_vkGetInstanceProcAddr,
        .pfnNextGetDeviceProcAddr = host_gdpa,
    };
    VkLayerDeviceCreateInfo chain =
    {
        .sType = VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO,
        .pNext = create_info->pNext,
        .function = VK_LAYER_LINK_INFO,
        .u.pLayerInfo = &link,
    };
    VkDeviceCreateInfo info = *create_info;

    info.pNext = &chain;
    return create( physical_device, &info, allocator, device );
}

/* Global commands belong to the loader, the rest to the layer. */
static PFN_vkVoidFunction VKAPI_CALL chain_vkGetInstanceProcAddr( VkInstance instance, const char *name )
{
    if (!strcmp( name, "vkCreateInstance" )) return (PFN_vkVoidFunction)chain_vkCreateInstance;
    if (!strcmp( name, "vkCreateDevice" )) return (PFN_vkVoidFunction)chain_vkCreateDevice;
    if (!instance) return host_gipa( NULL, name );
    return layer_gipa( instance, name );
}

/***********************************************************************
 *      vulkan_layer_chain
 *
 * Puts the layer WINE_VULKAN_LAYER names between Wine and the host loader, as the loader would
 * an implicit layer.
 */
void vulkan_layer_chain( PFN_vkGetInstanceProcAddr *gipa, PFN_vkGetDeviceProcAddr *gdpa )
{
    PFN_vkNegotiateLoaderLayerInterfaceVersion negotiate;
    VkNegotiateLayerInterface version = { .sType = LAYER_NEGOTIATE_INTERFACE_STRUCT,
                                          .loaderLayerInterfaceVersion = 2 };
    const char *path = getenv( "WINE_VULKAN_LAYER" );
    void *handle;

    if (!path || !*path) return;
    if (!(handle = dlopen( path, RTLD_NOW | RTLD_LOCAL )))
    {
        ERR( "Failed to load Vulkan layer %s: %s\n", debugstr_a(path), dlerror() );
        return;
    }
    if (!(negotiate = dlsym( handle, "vkNegotiateLoaderLayerInterfaceVersion" )) ||
        negotiate( &version ) || version.loaderLayerInterfaceVersion < 2)
    {
        ERR( "Vulkan layer %s does not negotiate interface version 2\n", debugstr_a(path) );
        dlclose( handle );
        return;
    }

    host_gipa = *gipa;
    host_gdpa = *gdpa;
    layer_gipa = version.pfnGetInstanceProcAddr;
    layer_gdpa = version.pfnGetDeviceProcAddr;
    *gipa = chain_vkGetInstanceProcAddr;
    *gdpa = layer_gdpa;
    TRACE( "Chained Vulkan layer %s\n", debugstr_a(path) );
}
