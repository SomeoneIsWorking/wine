/*
 * Android Vulkan surfaces
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

#include <stdarg.h>

#include "ntstatus.h"
#include "android.h"
#include "wine/debug.h"

#include "wine/vulkan.h"
#include "wine/vulkan_driver.h"

WINE_DEFAULT_DEBUG_CHANNEL(vulkan);

static VkResult android_vulkan_surface_create( struct client_surface *client, const struct vulkan_instance *instance,
                                               VkSurfaceKHR *handle )
{
    VkAndroidSurfaceCreateInfoKHR info = {.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
    VkResult res;

    TRACE( "%s %p %p\n", debugstr_client_surface( client ), instance, handle );

    if (!(info.window = client_surface_window( client ))) return VK_ERROR_SURFACE_LOST_KHR;
    if ((res = instance->p_vkCreateAndroidSurfaceKHR( instance->host.instance, &info, NULL, handle ))) return res;

    TRACE( "Created surface 0x%s on window %p\n", wine_dbgstr_longlong( *handle ), info.window );
    return VK_SUCCESS;
}

/* Every queue of an Android device can present to any of its surfaces. */
static VkBool32 android_get_physical_device_presentation_support( struct vulkan_physical_device *physical_device,
                                                                  uint32_t index )
{
    return VK_TRUE;
}

static void android_map_instance_extensions( struct vulkan_instance_extensions *extensions )
{
    if (extensions->has_VK_KHR_win32_surface) extensions->has_VK_KHR_android_surface = 1;
    if (extensions->has_VK_KHR_android_surface) extensions->has_VK_KHR_win32_surface = 1;
}

static void android_map_device_extensions( struct vulkan_device_extensions *extensions )
{
    if (extensions->has_VK_KHR_external_memory_win32) extensions->has_VK_KHR_external_memory_fd = 1;
    if (extensions->has_VK_KHR_external_memory_fd) extensions->has_VK_KHR_external_memory_win32 = 1;
    if (extensions->has_VK_KHR_external_semaphore_win32) extensions->has_VK_KHR_external_semaphore_fd = 1;
    if (extensions->has_VK_KHR_external_semaphore_fd) extensions->has_VK_KHR_external_semaphore_win32 = 1;
    if (extensions->has_VK_KHR_external_fence_win32) extensions->has_VK_KHR_external_fence_fd = 1;
    if (extensions->has_VK_KHR_external_fence_fd) extensions->has_VK_KHR_external_fence_win32 = 1;
}

static const struct vulkan_driver_funcs android_vulkan_driver_funcs =
{
    .p_vulkan_surface_create = android_vulkan_surface_create,
    .p_get_physical_device_presentation_support = android_get_physical_device_presentation_support,
    .p_map_instance_extensions = android_map_instance_extensions,
    .p_map_device_extensions = android_map_device_extensions,
};

/**********************************************************************
 *           ANDROID_VulkanInit
 */
UINT ANDROID_VulkanInit( UINT version, void *vulkan_handle, const struct vulkan_driver_funcs **driver_funcs )
{
    if (version != WINE_VULKAN_DRIVER_VERSION)
    {
        ERR( "version mismatch, win32u wants %u but driver has %u\n", version, WINE_VULKAN_DRIVER_VERSION );
        return STATUS_INVALID_PARAMETER;
    }

    *driver_funcs = &android_vulkan_driver_funcs;
    return STATUS_SUCCESS;
}
