/*
 * Copyright 2008 Henri Verbeet for CodeWeavers
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

#ifndef __WINE_DXGI_PRIVATE_H
#define __WINE_DXGI_PRIVATE_H

#include "wine/debug.h"

#define COBJMACROS
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "objbase.h"

#include "dxgi.h"
#include "wine/wined3d_interface.h"
#ifdef DXGI_INIT_GUID
#include "initguid.h"
#endif
#include "dxgi_private_interface.h"

extern CRITICAL_SECTION dxgi_cs;

/* TRACE helper functions */
const char *debug_dxgi_format(DXGI_FORMAT format);

/* IDXGIFactory */
extern const struct IWineDXGIFactoryVtbl dxgi_factory_vtbl;
struct dxgi_factory
{
    const struct IWineDXGIFactoryVtbl *vtbl;
    LONG refcount;
    IWineD3D *wined3d;
    UINT adapter_count;
    IDXGIAdapter **adapters;
};

/* IDXGIDevice */
extern const struct IDXGIDeviceVtbl dxgi_device_vtbl;
struct dxgi_device
{
    const struct IDXGIDeviceVtbl *vtbl;
    IUnknown *child_layer;
    LONG refcount;
    IWineD3DDevice *wined3d_device;
    IWineDXGIFactory *factory;
};

/* IDXGIAdapter */
extern const struct IWineDXGIAdapterVtbl dxgi_adapter_vtbl;
struct dxgi_adapter
{
    const struct IWineDXGIAdapterVtbl *vtbl;
    IDXGIFactory *parent;
    LONG refcount;
    UINT ordinal;
};

/* IDXGISwapChain */
extern const struct IDXGISwapChainVtbl dxgi_swapchain_vtbl;
struct dxgi_swapchain
{
    const struct IDXGISwapChainVtbl *vtbl;
    LONG refcount;
};

/* Layered device */
enum dxgi_device_layer_id
{
    DXGI_DEVICE_LAYER_DEBUG1        = 0x8,
    DXGI_DEVICE_LAYER_THREAD_SAFE   = 0x10,
    DXGI_DEVICE_LAYER_DEBUG2        = 0x20,
    DXGI_DEVICE_LAYER_SWITCH_TO_REF = 0x30,
    DXGI_DEVICE_LAYER_D3D10_DEVICE  = 0xffffffff,
};

struct layer_get_size_args
{
    DWORD unknown0;
    DWORD unknown1;
    DWORD *unknown2;
    DWORD *unknown3;
    IDXGIAdapter *adapter;
    WORD interface_major;
    WORD interface_minor;
    WORD version_build;
    WORD version_revision;
};

struct dxgi_device_layer
{
    enum dxgi_device_layer_id id;
    HRESULT (WINAPI *init)(enum dxgi_device_layer_id id, DWORD *count, DWORD *values);
    UINT (WINAPI *get_size)(enum dxgi_device_layer_id id, struct layer_get_size_args *args, DWORD unknown0);
    HRESULT (WINAPI *create)(enum dxgi_device_layer_id id, void **layer_base, DWORD unknown0,
            void *device_object, REFIID riid, void **device_layer);
};

#endif /* __WINE_DXGI_PRIVATE_H */
