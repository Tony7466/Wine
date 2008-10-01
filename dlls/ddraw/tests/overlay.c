/*
 * Unit tests for DirectDraw overlay functions
 *
 * Copyright (C) 2008 Stefan D�singer for CodeWeavers
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

#include <assert.h>
#include "wine/test.h"
#include "ddraw.h"
#include "unknwn.h"

static HRESULT (WINAPI *pDirectDrawCreateEx)(LPGUID,LPVOID*,REFIID,LPUNKNOWN);

static IDirectDraw7 *ddraw = NULL;
static IDirectDrawSurface7 *primary = NULL;

static IDirectDrawSurface7 *create_overlay(DWORD width, DWORD height, DWORD format) {
    DDSURFACEDESC2 ddsd;
    HRESULT hr;
    IDirectDrawSurface7 *ret;

    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
    ddsd.dwWidth = width;
    ddsd.dwHeight = height;
    ddsd.ddsCaps.dwCaps = DDSCAPS_OVERLAY;
    ddsd.ddpfPixelFormat.dwSize = sizeof(ddsd.ddpfPixelFormat);
    ddsd.ddpfPixelFormat.dwFlags = DDPF_FOURCC;
    ddsd.ddpfPixelFormat.dwFourCC = format;
    hr = IDirectDraw7_CreateSurface(ddraw, &ddsd, &ret, NULL);
    if(FAILED(hr)) return NULL;
    else return ret;
}

static BOOL CreateDirectDraw(void)
{
    HRESULT hr;
    DDSURFACEDESC2 ddsd;
    IDirectDrawSurface7 *overlay = NULL;
    HMODULE hmod = GetModuleHandleA("ddraw.dll");

    pDirectDrawCreateEx = (void*)GetProcAddress(hmod, "DirectDrawCreateEx");
    if (!pDirectDrawCreateEx) {
        win_skip("DirectDrawCreateEx is not available\n");
        return FALSE;
    }

    hr = pDirectDrawCreateEx(NULL, (void**)&ddraw, &IID_IDirectDraw7, NULL);
    ok(hr == DD_OK || hr == DDERR_NODIRECTDRAWSUPPORT, "DirectDrawCreateEx returned: %x\n", hr);
    if (!ddraw) {
        trace("DirectDrawCreateEx() failed with an error %x\n", hr);
        return FALSE;
    }

    hr = IDirectDraw_SetCooperativeLevel(ddraw, NULL, DDSCL_NORMAL);
    ok(hr == DD_OK, "SetCooperativeLevel returned: %x\n", hr );

    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS;
    ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    hr = IDirectDraw7_CreateSurface(ddraw, &ddsd, &primary, NULL);
    if (!SUCCEEDED(hr)) {
        IDirectDraw7_Release(ddraw);
        trace("IDirectDraw7_CreateSurface() failed with an error %x\n", hr);
        return FALSE;
    }

    overlay = create_overlay(64, 64, MAKEFOURCC('U','Y','V','Y'));
    if (!overlay) {
        IDirectDrawSurface7_Release(primary);
        IDirectDraw7_Release(ddraw);
        skip("Failed to create an overlay - assuming not supported\n");
        return FALSE;
    }
    IDirectDraw7_Release(overlay);

    return TRUE;
}

static void rectangle_settings(void) {
    IDirectDrawSurface7 *overlay = create_overlay(64, 64, MAKEFOURCC('U','Y','V','Y'));
    HRESULT hr;
    RECT rect = {0, 0, 64, 64};
    LONG posx, posy;

    /* The dx sdk sort of implies that rect must be set when DDOVER_SHOW is used. Show that this is wrong */
    hr = IDirectDrawSurface7_UpdateOverlay(overlay, NULL, primary, &rect, DDOVER_SHOW, NULL);
    ok(hr == DD_OK, "IDirectDrawSurface7_UpdateOverlay failed with hr=0x%08x\n", hr);
    hr = IDirectDrawSurface7_UpdateOverlay(overlay, NULL, primary, NULL, DDOVER_HIDE, NULL);
    ok(hr == DD_OK, "IDirectDrawSurface7_UpdateOverlay failed with hr=0x%08x\n", hr);
    hr = IDirectDrawSurface7_UpdateOverlay(overlay, NULL, primary, NULL, DDOVER_SHOW, NULL);
    ok(hr == DD_OK, "IDirectDrawSurface7_UpdateOverlay failed with hr=0x%08x\n", hr);

    /* Show that the overlay position is the (top, left) coordinate of the dest rectangle */
    rect.top += 16;
    rect.left += 32;
    rect.bottom += 16;
    rect.right += 32;
    hr = IDirectDrawSurface7_UpdateOverlay(overlay, NULL, primary, &rect, DDOVER_SHOW, NULL);
    ok(hr == DD_OK, "IDirectDrawSurface7_UpdateOverlay failed with hr=0x%08x\n", hr);
    posx = -1; posy = -1;
    hr = IDirectDrawSurface7_GetOverlayPosition(overlay, &posx, &posy);
    ok(hr == DD_OK, "IDirectDrawSurface7_GetOverlayPosition failed with hr=0x%08x\n", hr);
    ok(posx == rect.left && posy == rect.top, "Overlay position is (%d, %d), expected (%d, %d)\n",
       posx, posy, rect.left, rect.top);

    /* Passing a NULL dest rect sets the position to 0/0 . Visually it can be seen that the overlay overlays the whole primary(==screen)*/
    hr = IDirectDrawSurface7_UpdateOverlay(overlay, NULL, primary, NULL, 0, NULL);
    ok(hr == DD_OK, "IDirectDrawSurface7_UpdateOverlay failed with hr=0x%08x\n", hr);
    hr = IDirectDrawSurface7_GetOverlayPosition(overlay, &posx, &posy);
    ok(hr == DD_OK, "IDirectDrawSurface7_GetOverlayPosition failed with hr=0x%08x\n", hr);
    ok(posx == 0 && posy == 0, "Overlay position is (%d, %d), expected (%d, %d)\n",
       posx, posy, 0, 0);

    /* The position cannot be retrieved when the overlay is not shown */
    hr = IDirectDrawSurface7_UpdateOverlay(overlay, NULL, primary, &rect, DDOVER_HIDE, NULL);
    ok(hr == DD_OK, "IDirectDrawSurface7_UpdateOverlay failed with hr=0x%08x\n", hr);
    posx = -1; posy = -1;
    hr = IDirectDrawSurface7_GetOverlayPosition(overlay, &posx, &posy);
    ok(hr == DDERR_OVERLAYNOTVISIBLE, "IDirectDrawSurface7_GetOverlayPosition failed with hr=0x%08x\n", hr);
    ok(posx == 0 && posy == 0, "Overlay position is (%d, %d), expected (%d, %d)\n",
       posx, posy, 0, 0);

    IDirectDrawSurface7_Release(overlay);
}

static void offscreen_test(void) {
    IDirectDrawSurface7 *overlay = create_overlay(64, 64, MAKEFOURCC('U','Y','V','Y')), *offscreen = NULL;
    HRESULT hr;
    DDSURFACEDESC2 ddsd;

    /* Try to overlay a NULL surface */
    hr = IDirectDrawSurface7_UpdateOverlay(overlay, NULL, NULL, NULL, DDOVER_SHOW, NULL);
    ok(hr == DDERR_INVALIDPARAMS, "IDirectDrawSurface7_UpdateOverlay failed with hr=0x%08x\n", hr);
    hr = IDirectDrawSurface7_UpdateOverlay(overlay, NULL, NULL, NULL, DDOVER_HIDE, NULL);
    ok(hr == DDERR_INVALIDPARAMS, "IDirectDrawSurface7_UpdateOverlay failed with hr=0x%08x\n", hr);

    /* Try to overlay an offscreen surface */
    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
    ddsd.dwWidth = 64;
    ddsd.dwHeight = 64;
    ddsd.ddpfPixelFormat.dwSize = sizeof(ddsd.ddpfPixelFormat);
    ddsd.ddpfPixelFormat.dwFlags = DDPF_RGB;
    ddsd.ddpfPixelFormat.dwFourCC = 0;
    ddsd.ddpfPixelFormat.dwRGBBitCount = 16;
    ddsd.ddpfPixelFormat.dwRBitMask = 0xF800;
    ddsd.ddpfPixelFormat.dwGBitMask = 0x07e0;
    ddsd.ddpfPixelFormat.dwBBitMask = 0x001F;
    ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    hr = IDirectDraw7_CreateSurface(ddraw, &ddsd, &offscreen, NULL);
    ok(hr == DD_OK, "IDirectDraw7_CreateSurface failed with hr=0x%08x\n", hr);

    hr = IDirectDrawSurface7_UpdateOverlay(overlay, NULL, offscreen, NULL, DDOVER_SHOW, NULL);
    ok(hr == DD_OK, "IDirectDrawSurface7_UpdateOverlay failed with hr=0x%08x\n", hr);

    /* Try to overlay the primary with a non-overlay surface */
    hr = IDirectDrawSurface7_UpdateOverlay(offscreen, NULL, primary, NULL, DDOVER_SHOW, NULL);
    ok(hr == DDERR_NOTAOVERLAYSURFACE, "IDirectDrawSurface7_UpdateOverlay failed with hr=0x%08x\n", hr);

    IDirectDrawSurface7_Release(offscreen);
    IDirectDrawSurface7_Release(overlay);
}

static void yv12_test(void)
{
    HRESULT hr;
    DDSURFACEDESC2 desc;
    IDirectDrawSurface7 *surface;

    surface = create_overlay(256, 256, MAKEFOURCC('Y','V','1','2'));
    if(!surface) {
        skip("YV12 surfaces not available\n");
        return;
    }

    memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(desc);
    hr = IDirectDrawSurface7_Lock(surface, NULL, &desc, 0, NULL);
    ok(hr == DD_OK, "IDirectDrawSurface7_Lock returned 0x%08x, expected DD_OK\n", hr);

    ok(desc.dwFlags == (DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_CAPS | DDSD_PITCH),
       "Unexpected desc.dwFlags 0x%08x\n", desc.dwFlags);
    ok(desc.ddsCaps.dwCaps == (DDSCAPS_OVERLAY | DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM),
       "Unexpected desc.ddsCaps.dwCaps 0x%08x\n", desc.ddsCaps.dwCaps);
    ok(desc.dwWidth == 256 && desc.dwHeight == 256, "Expected size 64x64, got %ux%u\n",
       desc.dwWidth, desc.dwHeight);
    /* The overlay pitch seems to have 256 byte alignment */
    ok(desc.lPitch == 256, "Unexpected pitch %u, expected 256\n", desc.lPitch);

    hr = IDirectDrawSurface7_Unlock(surface, NULL);
    ok(hr == DD_OK, "IDirectDrawSurface7_Unlock returned 0x%08x, expected DD_OK\n", hr);

    IDirectDrawSurface7_Release(surface);
}

START_TEST(overlay)
{
    if(CreateDirectDraw() == FALSE) {
        skip("Failed to initialize ddraw\n");
        return;
    }

    rectangle_settings();
    offscreen_test();
    yv12_test();

    if(primary) IDirectDrawSurface7_Release(primary);
    if(ddraw) IDirectDraw7_Release(ddraw);
}
