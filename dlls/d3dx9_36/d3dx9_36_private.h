/*
 * Copyright (C) 2008 Tony Wasserka
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
 *
 */

#ifndef __WINE_D3DX9_36_PRIVATE_H
#define __WINE_D3DX9_36_PRIVATE_H

#include <stdarg.h>

#define COBJMACROS
#include "wingdi.h"
#include "d3dx9.h"


/* ID3DXFont */
typedef struct ID3DXFontImpl
{
    /* IUnknown fields */
    const ID3DXFontVtbl *lpVtbl;
    LONG ref;

    /* ID3DXFont fields */
} ID3DXFontImpl;


/*ID3DXSprite */
typedef struct _SPRITE {
    LPDIRECT3DTEXTURE9 texture;
    UINT texw, texh;
    RECT rect;
    D3DXVECTOR3 center;
    D3DXVECTOR3 pos;
    D3DCOLOR color;
} SPRITE;

typedef struct ID3DXSpriteImpl
{
    /* IUnknown fields */
    const ID3DXSpriteVtbl *lpVtbl;
    LONG ref;

    /* ID3DXSprite fields */
    IDirect3DDevice9 *device;
    IDirect3DVertexDeclaration9 *vdecl;
    IDirect3DStateBlock9 *stateblock;
    D3DXMATRIX transform;
    D3DXMATRIX view;
    DWORD flags;

    /* Store the relevant caps to prevent multiple GetDeviceCaps calls */
    DWORD texfilter_caps;
    DWORD maxanisotropy;
    DWORD alphacmp_caps;

    SPRITE *sprites;
    int sprite_count;      /* number of sprites to be drawn */
    int allocated_sprites; /* number of (pre-)allocated sprites */
} ID3DXSpriteImpl;


#endif /* __WINE_D3DX9_36_PRIVATE_H */
