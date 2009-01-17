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

#ifndef __WINE_D3D10CORE_PRIVATE_H
#define __WINE_D3D10CORE_PRIVATE_H

#include "wine/debug.h"

#define COBJMACROS
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "objbase.h"

#include "d3d10.h"
#ifdef D3D10CORE_INIT_GUID
#include "initguid.h"
#endif
#include "wine/wined3d.h"

/* TRACE helper functions */
const char *debug_d3d10_primitive_topology(D3D10_PRIMITIVE_TOPOLOGY topology);
const char *debug_dxgi_format(DXGI_FORMAT format);

/* IDirect3D10Device */
extern const struct ID3D10DeviceVtbl d3d10_device_vtbl;
extern const struct IUnknownVtbl d3d10_device_inner_unknown_vtbl;
extern const struct IWineD3DDeviceParentVtbl d3d10_wined3d_device_parent_vtbl;
struct d3d10_device
{
    const struct ID3D10DeviceVtbl *vtbl;
    const struct IUnknownVtbl *inner_unknown_vtbl;
    const struct IWineD3DDeviceParentVtbl *device_parent_vtbl;
    IUnknown *outer_unknown;
    LONG refcount;
};

/* ID3D10Texture2D */
extern const struct ID3D10Texture2DVtbl d3d10_texture2d_vtbl;
struct d3d10_texture2d
{
    const struct ID3D10Texture2DVtbl *vtbl;
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

HRESULT WINAPI DXGID3D10CreateDevice(HMODULE d3d10core, IDXGIFactory *factory, IDXGIAdapter *adapter,
        UINT flags, DWORD unknown0, void **device);
HRESULT WINAPI DXGID3D10RegisterLayers(const struct dxgi_device_layer *layers, UINT layer_count);

#endif /* __WINE_D3D10CORE_PRIVATE_H */
