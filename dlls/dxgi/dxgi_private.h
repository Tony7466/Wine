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
#ifdef DXGI_INIT_GUID
#include "initguid.h"
#endif
#include "wine/wined3d.h"
#include "wine/winedxgi.h"

extern CRITICAL_SECTION dxgi_cs DECLSPEC_HIDDEN;

/* TRACE helper functions */
const char *debug_dxgi_format(DXGI_FORMAT format) DECLSPEC_HIDDEN;

WINED3DFORMAT wined3dformat_from_dxgi_format(DXGI_FORMAT format) DECLSPEC_HIDDEN;

/* IDXGIFactory */
extern const struct IWineDXGIFactoryVtbl dxgi_factory_vtbl DECLSPEC_HIDDEN;
struct dxgi_factory
{
    const struct IWineDXGIFactoryVtbl *vtbl;
    LONG refcount;
    IWineD3D *wined3d;
    UINT adapter_count;
    IDXGIAdapter **adapters;
};

/* IDXGIDevice */
extern const struct IWineDXGIDeviceVtbl dxgi_device_vtbl DECLSPEC_HIDDEN;
struct dxgi_device
{
    const struct IWineDXGIDeviceVtbl *vtbl;
    IUnknown *child_layer;
    LONG refcount;
    IWineD3DDevice *wined3d_device;
    IWineDXGIFactory *factory;
};

/* IDXGIOutput */
struct dxgi_output
{
    const struct IDXGIOutputVtbl *vtbl;
    LONG refcount;
};

void dxgi_output_init(struct dxgi_output *output) DECLSPEC_HIDDEN;

/* IDXGIAdapter */
struct dxgi_adapter
{
    const struct IWineDXGIAdapterVtbl *vtbl;
    IDXGIFactory *parent;
    LONG refcount;
    UINT ordinal;
    IDXGIOutput *output;
};

HRESULT dxgi_adapter_init(struct dxgi_adapter *adapter, IDXGIFactory *parent, UINT ordinal) DECLSPEC_HIDDEN;

/* IDXGISwapChain */
extern const struct IDXGISwapChainVtbl dxgi_swapchain_vtbl DECLSPEC_HIDDEN;
struct dxgi_swapchain
{
    const struct IDXGISwapChainVtbl *vtbl;
    LONG refcount;
    IWineD3DSwapChain *wined3d_swapchain;
};

/* IDXGISurface */
extern const struct IDXGISurfaceVtbl dxgi_surface_vtbl DECLSPEC_HIDDEN;
extern const struct IUnknownVtbl dxgi_surface_inner_unknown_vtbl DECLSPEC_HIDDEN;
struct dxgi_surface
{
    const struct IDXGISurfaceVtbl *vtbl;
    const struct IUnknownVtbl *inner_unknown_vtbl;
    IUnknown *outer_unknown;
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
