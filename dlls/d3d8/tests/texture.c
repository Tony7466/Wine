/*
 * Copyright (C) 2006 Henri Verbeet
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
#define COBJMACROS
#include <d3d8.h>
#include "wine/test.h"

static HWND create_window(void)
{
    WNDCLASS wc = {0};
    wc.lpfnWndProc = &DefWindowProc;
    wc.lpszClassName = "d3d8_test_wc";
    RegisterClass(&wc);

    return CreateWindow("d3d8_test_wc", "d3d8_test",
            0, 0, 0, 0, 0, 0, 0, 0, 0);
}

static IDirect3DDevice8 *init_d3d8(HMODULE d3d8_handle)
{
    IDirect3D8 * (__stdcall * d3d8_create)(UINT SDKVersion) = 0;
    IDirect3D8 *d3d8_ptr = 0;
    IDirect3DDevice8 *device_ptr = 0;
    D3DPRESENT_PARAMETERS present_parameters;
    D3DDISPLAYMODE               d3ddm;
    HRESULT hr;

    d3d8_create = (void *)GetProcAddress(d3d8_handle, "Direct3DCreate8");
    ok(d3d8_create != NULL, "Failed to get address of Direct3DCreate8\n");
    if (!d3d8_create) return NULL;

    d3d8_ptr = d3d8_create(D3D_SDK_VERSION);
    ok(d3d8_ptr != NULL, "Failed to create IDirect3D8 object\n");
    if (!d3d8_ptr) return NULL;

    IDirect3D8_GetAdapterDisplayMode(d3d8_ptr, D3DADAPTER_DEFAULT, &d3ddm );
    ZeroMemory(&present_parameters, sizeof(present_parameters));
    present_parameters.Windowed = TRUE;
    present_parameters.hDeviceWindow = create_window();
    present_parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    present_parameters.BackBufferFormat = d3ddm.Format;

    hr = IDirect3D8_CreateDevice(d3d8_ptr, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
            NULL, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &present_parameters, &device_ptr);

    if(FAILED(hr))
    {
        trace("could not create device, IDirect3D8_CreateDevice returned %#x\n", hr);
        return NULL;
    }

    return device_ptr;
}

static void test_texture_stage_state(IDirect3DDevice8 *device_ptr, DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD expected)
{
    DWORD value;

    HRESULT hr = IDirect3DDevice8_GetTextureStageState(device_ptr, stage, type, &value);
    ok(SUCCEEDED(hr) && value == expected, "GetTextureStageState (stage %#x, type %#x) returned: hr %#x, value %#x. "
        "Expected hr %#x, value %#x\n", stage, type, hr, value, D3D_OK, expected);
}

/* Test the default texture stage state values */
static void test_texture_stage_states(IDirect3DDevice8 *device_ptr, int num_stages)
{
    int i;
    for (i = 0; i < num_stages; ++i)
    {
        test_texture_stage_state(device_ptr, i, D3DTSS_COLOROP, i ? D3DTOP_DISABLE : D3DTOP_MODULATE);
        test_texture_stage_state(device_ptr, i, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        test_texture_stage_state(device_ptr, i, D3DTSS_COLORARG2, D3DTA_CURRENT);
        test_texture_stage_state(device_ptr, i, D3DTSS_ALPHAOP, i ? D3DTOP_DISABLE : D3DTOP_SELECTARG1);
        test_texture_stage_state(device_ptr, i, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        test_texture_stage_state(device_ptr, i, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
        test_texture_stage_state(device_ptr, i, D3DTSS_BUMPENVMAT00, 0);
        test_texture_stage_state(device_ptr, i, D3DTSS_BUMPENVMAT01, 0);
        test_texture_stage_state(device_ptr, i, D3DTSS_BUMPENVMAT10, 0);
        test_texture_stage_state(device_ptr, i, D3DTSS_BUMPENVMAT11, 0);
        test_texture_stage_state(device_ptr, i, D3DTSS_TEXCOORDINDEX, i);
        test_texture_stage_state(device_ptr, i, D3DTSS_BUMPENVLSCALE, 0);
        test_texture_stage_state(device_ptr, i, D3DTSS_BUMPENVLOFFSET, 0);
        test_texture_stage_state(device_ptr, i, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
        test_texture_stage_state(device_ptr, i, D3DTSS_COLORARG0, D3DTA_CURRENT);
        test_texture_stage_state(device_ptr, i, D3DTSS_ALPHAARG0, D3DTA_CURRENT);
        test_texture_stage_state(device_ptr, i, D3DTSS_RESULTARG, D3DTA_CURRENT);
    }
}

START_TEST(texture)
{
    D3DCAPS8 caps;
    HMODULE d3d8_handle;
    IDirect3DDevice8 *device_ptr;

    d3d8_handle = LoadLibraryA("d3d8.dll");
    if (!d3d8_handle)
    {
        skip("Could not load d3d8.dll\n");
        return;
    }

    device_ptr = init_d3d8(d3d8_handle);
    if (!device_ptr) return;

    IDirect3DDevice8_GetDeviceCaps(device_ptr, &caps);

    test_texture_stage_states(device_ptr, caps.MaxTextureBlendStages);
}
