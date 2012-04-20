/*
 * Copyright 2008 Luis Busquets
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
#include "wine/debug.h"
#include "wine/unicode.h"
#include "windef.h"
#include "wingdi.h"
#include "d3dx9.h"
#include "d3dx9shader.h"
#include "d3dx9_36_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3dx);

LPCSTR WINAPI D3DXGetPixelShaderProfile(LPDIRECT3DDEVICE9 device)
{
    D3DCAPS9 caps;

    TRACE("device %p\n", device);

    if (!device) return NULL;

    IDirect3DDevice9_GetDeviceCaps(device,&caps);

    switch (caps.PixelShaderVersion)
    {
    case D3DPS_VERSION(1, 1):
        return "ps_1_1";

    case D3DPS_VERSION(1, 2):
        return "ps_1_2";

    case D3DPS_VERSION(1, 3):
        return "ps_1_3";

    case D3DPS_VERSION(1, 4):
        return "ps_1_4";

    case D3DPS_VERSION(2, 0):
        if ((caps.PS20Caps.NumTemps>=22)                          &&
            (caps.PS20Caps.Caps&D3DPS20CAPS_ARBITRARYSWIZZLE)     &&
            (caps.PS20Caps.Caps&D3DPS20CAPS_GRADIENTINSTRUCTIONS) &&
            (caps.PS20Caps.Caps&D3DPS20CAPS_PREDICATION)          &&
            (caps.PS20Caps.Caps&D3DPS20CAPS_NODEPENDENTREADLIMIT) &&
            (caps.PS20Caps.Caps&D3DPS20CAPS_NOTEXINSTRUCTIONLIMIT))
        {
            return "ps_2_a";
        }
        if ((caps.PS20Caps.NumTemps>=32)                          &&
            (caps.PS20Caps.Caps&D3DPS20CAPS_NOTEXINSTRUCTIONLIMIT))
        {
            return "ps_2_b";
        }
        return "ps_2_0";

    case D3DPS_VERSION(3, 0):
        return "ps_3_0";
    }

    return NULL;
}

UINT WINAPI D3DXGetShaderSize(const DWORD *byte_code)
{
    const DWORD *ptr = byte_code;

    TRACE("byte_code %p\n", byte_code);

    if (!ptr) return 0;

    /* Look for the END token, skipping the VERSION token */
    while (*++ptr != D3DSIO_END)
    {
        /* Skip comments */
        if ((*ptr & D3DSI_OPCODE_MASK) == D3DSIO_COMMENT)
        {
            ptr += ((*ptr & D3DSI_COMMENTSIZE_MASK) >> D3DSI_COMMENTSIZE_SHIFT);
        }
    }
    ++ptr;

    /* Return the shader size in bytes */
    return (ptr - byte_code) * sizeof(*ptr);
}

DWORD WINAPI D3DXGetShaderVersion(const DWORD *byte_code)
{
    TRACE("byte_code %p\n", byte_code);

    return byte_code ? *byte_code : 0;
}

LPCSTR WINAPI D3DXGetVertexShaderProfile(LPDIRECT3DDEVICE9 device)
{
    D3DCAPS9 caps;

    TRACE("device %p\n", device);

    if (!device) return NULL;

    IDirect3DDevice9_GetDeviceCaps(device,&caps);

    switch (caps.VertexShaderVersion)
    {
    case D3DVS_VERSION(1, 1):
        return "vs_1_1";
    case D3DVS_VERSION(2, 0):
        if ((caps.VS20Caps.NumTemps>=13) &&
            (caps.VS20Caps.DynamicFlowControlDepth==24) &&
            (caps.VS20Caps.Caps&D3DPS20CAPS_PREDICATION))
        {
            return "vs_2_a";
        }
        return "vs_2_0";
    case D3DVS_VERSION(3, 0):
        return "vs_3_0";
    }

    return NULL;
}

HRESULT WINAPI D3DXFindShaderComment(CONST DWORD* byte_code, DWORD fourcc, LPCVOID* data, UINT* size)
{
    CONST DWORD *ptr = byte_code;

    TRACE("(%p, %x, %p, %p)", byte_code, fourcc, data, size);

    if (!byte_code)
        return D3DERR_INVALIDCALL;

    while (*++ptr != D3DSIO_END)
    {
        /* Check if it is a comment */
        if ((*ptr & D3DSI_OPCODE_MASK) == D3DSIO_COMMENT)
        {
            DWORD comment_size = (*ptr & D3DSI_COMMENTSIZE_MASK) >> D3DSI_COMMENTSIZE_SHIFT;

            /* Check if this is the comment we are looking for */
            if (*(ptr + 1) == fourcc)
            {
                UINT ctab_size = (comment_size - 1) * sizeof(DWORD);
                LPCVOID ctab_data = ptr + 2;
                if (size)
                    *size = ctab_size;
                if (data)
                    *data = ctab_data;
                TRACE("Returning comment data at %p with size %d\n", ctab_data, ctab_size);
                return D3D_OK;
            }
            ptr += comment_size;
        }
    }

    return S_FALSE;
}

HRESULT WINAPI D3DXAssembleShader(LPCSTR data,
                                  UINT data_len,
                                  CONST D3DXMACRO* defines,
                                  LPD3DXINCLUDE include,
                                  DWORD flags,
                                  LPD3DXBUFFER* shader,
                                  LPD3DXBUFFER* error_messages)
{
    FIXME("(%p, %d, %p, %p, %x, %p, %p): stub\n", data, data_len, defines, include, flags, shader, error_messages);
    return D3DERR_INVALIDCALL;
}

HRESULT WINAPI D3DXAssembleShaderFromFileA(LPCSTR filename,
                                           CONST D3DXMACRO* defines,
                                           LPD3DXINCLUDE include,
                                           DWORD flags,
                                           LPD3DXBUFFER* shader,
                                           LPD3DXBUFFER* error_messages)
{
    LPWSTR filename_w = NULL;
    DWORD len;
    HRESULT ret;

    if (!filename) return D3DXERR_INVALIDDATA;

    len = MultiByteToWideChar(CP_ACP, 0, filename, -1, NULL, 0);
    filename_w = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
    if (!filename_w) return E_OUTOFMEMORY;
    MultiByteToWideChar(CP_ACP, 0, filename, -1, filename_w, len);

    ret = D3DXAssembleShaderFromFileW(filename_w, defines, include, flags, shader, error_messages);

    HeapFree(GetProcessHeap(), 0, filename_w);
    return ret;
}

HRESULT WINAPI D3DXAssembleShaderFromFileW(LPCWSTR filename,
                                           CONST D3DXMACRO* defines,
                                           LPD3DXINCLUDE include,
                                           DWORD flags,
                                           LPD3DXBUFFER* shader,
                                           LPD3DXBUFFER* error_messages)
{
    FIXME("(%s, %p, %p, %x, %p, %p): stub\n", debugstr_w(filename), defines, include, flags, shader, error_messages);
    return D3DERR_INVALIDCALL;
}

HRESULT WINAPI D3DXAssembleShaderFromResourceA(HMODULE module,
                                               LPCSTR resource,
                                               CONST D3DXMACRO* defines,
                                               LPD3DXINCLUDE include,
                                               DWORD flags,
                                               LPD3DXBUFFER* shader,
                                               LPD3DXBUFFER* error_messages)
{
    HRSRC res;
    LPCSTR buffer;
    DWORD len;

    if (!(res = FindResourceA(module, resource, (LPCSTR)RT_RCDATA)))
        return D3DXERR_INVALIDDATA;
    if (FAILED(load_resource_into_memory(module, res, (LPVOID *)&buffer, &len)))
        return D3DXERR_INVALIDDATA;
    return D3DXAssembleShader(buffer, len, defines, include, flags,
                              shader, error_messages);
}

HRESULT WINAPI D3DXAssembleShaderFromResourceW(HMODULE module,
                                               LPCWSTR resource,
                                               CONST D3DXMACRO* defines,
                                               LPD3DXINCLUDE include,
                                               DWORD flags,
                                               LPD3DXBUFFER* shader,
                                               LPD3DXBUFFER* error_messages)
{
    HRSRC res;
    LPCSTR buffer;
    DWORD len;

    if (!(res = FindResourceW(module, resource, (LPCWSTR)RT_RCDATA)))
        return D3DXERR_INVALIDDATA;
    if (FAILED(load_resource_into_memory(module, res, (LPVOID *)&buffer, &len)))
        return D3DXERR_INVALIDDATA;
    return D3DXAssembleShader(buffer, len, defines, include, flags,
                              shader, error_messages);
}

HRESULT WINAPI D3DXCompileShader(LPCSTR pSrcData,
                                 UINT srcDataLen,
                                 CONST D3DXMACRO* pDefines,
                                 LPD3DXINCLUDE pInclude,
                                 LPCSTR pFunctionName,
                                 LPCSTR pProfile,
                                 DWORD Flags,
                                 LPD3DXBUFFER* ppShader,
                                 LPD3DXBUFFER* ppErrorMsgs,
                                 LPD3DXCONSTANTTABLE * ppConstantTable)
{
    FIXME("(%p, %d, %p, %p, %p, %p, %d, %p, %p, %p): stub\n",
          pSrcData, srcDataLen, pDefines, pInclude, pFunctionName,
          pProfile, Flags, ppShader, ppErrorMsgs, ppConstantTable);
    return D3DERR_INVALIDCALL;
}

static const struct ID3DXConstantTableVtbl ID3DXConstantTable_Vtbl;

typedef struct ID3DXConstantTableImpl {
    const ID3DXConstantTableVtbl *lpVtbl;
    LONG ref;
    LPVOID ctab;
    DWORD size;
    D3DXCONSTANTTABLE_DESC desc;
} ID3DXConstantTableImpl;

/*** IUnknown methods ***/
static HRESULT WINAPI ID3DXConstantTableImpl_QueryInterface(ID3DXConstantTable* iface, REFIID riid, void** ppvObject)
{
    ID3DXConstantTableImpl *This = (ID3DXConstantTableImpl *)iface;

    TRACE("(%p)->(%s, %p)\n", This, debugstr_guid(riid), ppvObject);

    if (IsEqualGUID(riid, &IID_IUnknown) ||
        IsEqualGUID(riid, &IID_ID3DXBuffer) ||
        IsEqualGUID(riid, &IID_ID3DXConstantTable))
    {
        ID3DXConstantTable_AddRef(iface);
        *ppvObject = This;
        return S_OK;
    }

    WARN("Interface %s not found.\n", debugstr_guid(riid));

    return E_NOINTERFACE;
}

static ULONG WINAPI ID3DXConstantTableImpl_AddRef(ID3DXConstantTable* iface)
{
    ID3DXConstantTableImpl *This = (ID3DXConstantTableImpl *)iface;

    TRACE("(%p)->(): AddRef from %d\n", This, This->ref);

    return InterlockedIncrement(&This->ref);
}

static ULONG WINAPI ID3DXConstantTableImpl_Release(ID3DXConstantTable* iface)
{
    ID3DXConstantTableImpl *This = (ID3DXConstantTableImpl *)iface;
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p)->(): Release from %d\n", This, ref + 1);

    if (!ref)
    {
        HeapFree(GetProcessHeap(), 0, This->ctab);
        HeapFree(GetProcessHeap(), 0, This);
    }

    return ref;
}

/*** ID3DXBuffer methods ***/
static LPVOID WINAPI ID3DXConstantTableImpl_GetBufferPointer(ID3DXConstantTable* iface)
{
    ID3DXConstantTableImpl *This = (ID3DXConstantTableImpl *)iface;

    TRACE("(%p)->()\n", This);

    return This->ctab;
}

static DWORD WINAPI ID3DXConstantTableImpl_GetBufferSize(ID3DXConstantTable* iface)
{
    ID3DXConstantTableImpl *This = (ID3DXConstantTableImpl *)iface;

    TRACE("(%p)->()\n", This);

    return This->size;
}

/*** ID3DXConstantTable methods ***/
static HRESULT WINAPI ID3DXConstantTableImpl_GetDesc(ID3DXConstantTable* iface, D3DXCONSTANTTABLE_DESC *desc)
{
    ID3DXConstantTableImpl *This = (ID3DXConstantTableImpl *)iface;

    TRACE("(%p)->(%p)\n", This, desc);

    if (!desc)
        return D3DERR_INVALIDCALL;

    memcpy(desc, &This->desc, sizeof(This->desc));

    return D3D_OK;
}

static HRESULT WINAPI ID3DXConstantTableImpl_GetConstantDesc(ID3DXConstantTable* iface, D3DXHANDLE constant,
                                                             D3DXCONSTANT_DESC *desc, UINT *count)
{
    ID3DXConstantTableImpl *This = (ID3DXConstantTableImpl *)iface;

    FIXME("(%p)->(%p, %p, %p): stub\n", This, constant, desc, count);

    return E_NOTIMPL;
}

static UINT WINAPI ID3DXConstantTableImpl_GetSamplerIndex(LPD3DXCONSTANTTABLE iface, D3DXHANDLE constant)
{
    ID3DXConstantTableImpl *This = (ID3DXConstantTableImpl *)iface;

    FIXME("(%p)->(%p): stub\n", This, constant);

    return (UINT)-1;
}

static D3DXHANDLE WINAPI ID3DXConstantTableImpl_GetConstant(ID3DXConstantTable* iface, D3DXHANDLE constant, UINT index)
{
    ID3DXConstantTableImpl *This = (ID3DXConstantTableImpl *)iface;

    FIXME("(%p)->(%p, %d): stub\n", This, constant, index);

    return NULL;
}

static D3DXHANDLE WINAPI ID3DXConstantTableImpl_GetConstantByName(ID3DXConstantTable* iface, D3DXHANDLE constant, LPCSTR name)
{
    ID3DXConstantTableImpl *This = (ID3DXConstantTableImpl *)iface;

    FIXME("(%p)->(%p, %s): stub\n", This, constant, name);

    return NULL;
}

static D3DXHANDLE WINAPI ID3DXConstantTableImpl_GetConstantElement(ID3DXConstantTable* iface, D3DXHANDLE constant, UINT index)
{
    ID3DXConstantTableImpl *This = (ID3DXConstantTableImpl *)iface;

    FIXME("(%p)->(%p, %d): stub\n", This, constant, index);

    return NULL;
}

static HRESULT WINAPI ID3DXConstantTableImpl_SetDefaults(ID3DXConstantTable* iface, LPDIRECT3DDEVICE9 device)
{
    ID3DXConstantTableImpl *This = (ID3DXConstantTableImpl *)iface;

    FIXME("(%p)->(%p): stub\n", This, device);

    return E_NOTIMPL;
}

static HRESULT WINAPI ID3DXConstantTableImpl_SetValue(ID3DXConstantTable* iface, LPDIRECT3DDEVICE9 device,
                                                      D3DXHANDLE constant, LPCVOID data, UINT bytes)
{
    ID3DXConstantTableImpl *This = (ID3DXConstantTableImpl *)iface;

    FIXME("(%p)->(%p, %p, %p, %d): stub\n", This, device, constant, data, bytes);

    return E_NOTIMPL;
}

static HRESULT WINAPI ID3DXConstantTableImpl_SetBool(ID3DXConstantTable* iface, LPDIRECT3DDEVICE9 device,
                                                     D3DXHANDLE constant, BOOL b)
{
    ID3DXConstantTableImpl *This = (ID3DXConstantTableImpl *)iface;

    FIXME("(%p)->(%p, %p, %d): stub\n", This, device, constant, b);

    return E_NOTIMPL;
}

static HRESULT WINAPI ID3DXConstantTableImpl_SetBoolArray(ID3DXConstantTable* iface, LPDIRECT3DDEVICE9 device,
                                                          D3DXHANDLE constant, CONST BOOL* b, UINT count)
{
    ID3DXConstantTableImpl *This = (ID3DXConstantTableImpl *)iface;

    FIXME("(%p)->(%p, %p, %p, %d): stub\n", This, device, constant, b, count);

    return E_NOTIMPL;
}

static HRESULT WINAPI ID3DXConstantTableImpl_SetInt(ID3DXConstantTable* iface, LPDIRECT3DDEVICE9 device, D3DXHANDLE constant, INT n)
{
    ID3DXConstantTableImpl *This = (ID3DXConstantTableImpl *)iface;

    FIXME("(%p)->(%p, %p, %d): stub\n", This, device, constant, n);

    return E_NOTIMPL;
}

static HRESULT WINAPI ID3DXConstantTableImpl_SetIntArray(ID3DXConstantTable* iface, LPDIRECT3DDEVICE9 device,
                                                         D3DXHANDLE constant, CONST INT* n, UINT count)
{
    ID3DXConstantTableImpl *This = (ID3DXConstantTableImpl *)iface;

    FIXME("(%p)->(%p, %p, %p, %d): stub\n", This, device, constant, n, count);

    return E_NOTIMPL;
}

static HRESULT WINAPI ID3DXConstantTableImpl_SetFloat(ID3DXConstantTable* iface, LPDIRECT3DDEVICE9 device,
                                                      D3DXHANDLE constant, FLOAT f)
{
    ID3DXConstantTableImpl *This = (ID3DXConstantTableImpl *)iface;

    FIXME("(%p)->(%p, %p, %f): stub\n", This, device, constant, f);

    return E_NOTIMPL;
}

static HRESULT WINAPI ID3DXConstantTableImpl_SetFloatArray(ID3DXConstantTable* iface, LPDIRECT3DDEVICE9 device,
                                                           D3DXHANDLE constant, CONST FLOAT* f, UINT count)
{
    ID3DXConstantTableImpl *This = (ID3DXConstantTableImpl *)iface;

    FIXME("(%p)->(%p, %p, %p, %d): stub\n", This, device, constant, f, count);

    return E_NOTIMPL;
}

static HRESULT WINAPI ID3DXConstantTableImpl_SetVector(ID3DXConstantTable* iface, LPDIRECT3DDEVICE9 device,
                                                       D3DXHANDLE constant, CONST D3DXVECTOR4* vector)
{
    ID3DXConstantTableImpl *This = (ID3DXConstantTableImpl *)iface;

    FIXME("(%p)->(%p, %p, %p): stub\n", This, device, constant, vector);

    return E_NOTIMPL;
}

static HRESULT WINAPI ID3DXConstantTableImpl_SetVectorArray(ID3DXConstantTable* iface, LPDIRECT3DDEVICE9 device,
                                                            D3DXHANDLE constant, CONST D3DXVECTOR4* vector, UINT count)
{
    ID3DXConstantTableImpl *This = (ID3DXConstantTableImpl *)iface;

    FIXME("(%p)->(%p, %p, %p, %d): stub\n", This, device, constant, vector, count);

    return E_NOTIMPL;
}

static HRESULT WINAPI ID3DXConstantTableImpl_SetMatrix(ID3DXConstantTable* iface, LPDIRECT3DDEVICE9 device,
                                                       D3DXHANDLE constant, CONST D3DXMATRIX* matrix)
{
    ID3DXConstantTableImpl *This = (ID3DXConstantTableImpl *)iface;

    FIXME("(%p)->(%p, %p, %p): stub\n", This, device, constant, matrix);

    return E_NOTIMPL;
}

static HRESULT WINAPI ID3DXConstantTableImpl_SetMatrixArray(ID3DXConstantTable* iface, LPDIRECT3DDEVICE9 device,
                                                            D3DXHANDLE constant, CONST D3DXMATRIX* matrix, UINT count)
{
    ID3DXConstantTableImpl *This = (ID3DXConstantTableImpl *)iface;

    FIXME("(%p)->(%p, %p, %p, %d): stub\n", This, device, constant, matrix, count);

    return E_NOTIMPL;
}

static HRESULT WINAPI ID3DXConstantTableImpl_SetMatrixPointerArray(ID3DXConstantTable* iface, LPDIRECT3DDEVICE9 device,
                                                                   D3DXHANDLE constant, CONST D3DXMATRIX** matrix, UINT count)
{
    ID3DXConstantTableImpl *This = (ID3DXConstantTableImpl *)iface;

    FIXME("(%p)->(%p, %p, %p, %d): stub\n", This, device, constant, matrix, count);

    return E_NOTIMPL;
}

static HRESULT WINAPI ID3DXConstantTableImpl_SetMatrixTranspose(ID3DXConstantTable* iface, LPDIRECT3DDEVICE9 device,
                                                                D3DXHANDLE constant, CONST D3DXMATRIX* matrix)
{
    ID3DXConstantTableImpl *This = (ID3DXConstantTableImpl *)iface;

    FIXME("(%p)->(%p, %p, %p): stub\n", This, device, constant, matrix);

    return E_NOTIMPL;
}

static HRESULT WINAPI ID3DXConstantTableImpl_SetMatrixTransposeArray(ID3DXConstantTable* iface, LPDIRECT3DDEVICE9 device,
                                                                     D3DXHANDLE constant, CONST D3DXMATRIX* matrix, UINT count)
{
    ID3DXConstantTableImpl *This = (ID3DXConstantTableImpl *)iface;

    FIXME("(%p)->(%p, %p, %p, %d): stub\n", This, device, constant, matrix, count);

    return E_NOTIMPL;
}

static HRESULT WINAPI ID3DXConstantTableImpl_SetMatrixTransposePointerArray(ID3DXConstantTable* iface, LPDIRECT3DDEVICE9 device,
                                                                            D3DXHANDLE constant, CONST D3DXMATRIX** matrix, UINT count)
{
    ID3DXConstantTableImpl *This = (ID3DXConstantTableImpl *)iface;

    FIXME("(%p)->(%p, %p, %p, %d): stub\n", This, device, constant, matrix, count);

    return E_NOTIMPL;
}

static const struct ID3DXConstantTableVtbl ID3DXConstantTable_Vtbl =
{
    /*** IUnknown methods ***/
    ID3DXConstantTableImpl_QueryInterface,
    ID3DXConstantTableImpl_AddRef,
    ID3DXConstantTableImpl_Release,
    /*** ID3DXBuffer methods ***/
    ID3DXConstantTableImpl_GetBufferPointer,
    ID3DXConstantTableImpl_GetBufferSize,
    /*** ID3DXConstantTable methods ***/
    ID3DXConstantTableImpl_GetDesc,
    ID3DXConstantTableImpl_GetConstantDesc,
    ID3DXConstantTableImpl_GetSamplerIndex,
    ID3DXConstantTableImpl_GetConstant,
    ID3DXConstantTableImpl_GetConstantByName,
    ID3DXConstantTableImpl_GetConstantElement,
    ID3DXConstantTableImpl_SetDefaults,
    ID3DXConstantTableImpl_SetValue,
    ID3DXConstantTableImpl_SetBool,
    ID3DXConstantTableImpl_SetBoolArray,
    ID3DXConstantTableImpl_SetInt,
    ID3DXConstantTableImpl_SetIntArray,
    ID3DXConstantTableImpl_SetFloat,
    ID3DXConstantTableImpl_SetFloatArray,
    ID3DXConstantTableImpl_SetVector,
    ID3DXConstantTableImpl_SetVectorArray,
    ID3DXConstantTableImpl_SetMatrix,
    ID3DXConstantTableImpl_SetMatrixArray,
    ID3DXConstantTableImpl_SetMatrixPointerArray,
    ID3DXConstantTableImpl_SetMatrixTranspose,
    ID3DXConstantTableImpl_SetMatrixTransposeArray,
    ID3DXConstantTableImpl_SetMatrixTransposePointerArray
};

HRESULT WINAPI D3DXGetShaderConstantTableEx(CONST DWORD* byte_code,
                                            DWORD flags,
                                            LPD3DXCONSTANTTABLE* constant_table)
{
    ID3DXConstantTableImpl* object = NULL;
    HRESULT hr;
    LPCVOID data;
    UINT size;
    D3DXSHADER_CONSTANTTABLE* ctab_header;

    FIXME("(%p, %x, %p): semi-stub\n", byte_code, flags, constant_table);

    if (!byte_code || !constant_table)
        return D3DERR_INVALIDCALL;

    hr = D3DXFindShaderComment(byte_code, MAKEFOURCC('C','T','A','B'), &data, &size);
    if (hr != D3D_OK)
        return D3DXERR_INVALIDDATA;

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ID3DXConstantTableImpl));
    if (!object)
    {
        ERR("Out of memory\n");
        return E_OUTOFMEMORY;
    }

    object->lpVtbl = &ID3DXConstantTable_Vtbl;
    object->ref = 1;

    if (size < sizeof(D3DXSHADER_CONSTANTTABLE))
        goto error;

    object->ctab = HeapAlloc(GetProcessHeap(), 0, size);
    if (!object->ctab)
    {
        HeapFree(GetProcessHeap(), 0, object);
        ERR("Out of memory\n");
        return E_OUTOFMEMORY;
    }
    object->size = size;
    memcpy(object->ctab, data, object->size);

    ctab_header = (D3DXSHADER_CONSTANTTABLE*)data;
    if (ctab_header->Size != sizeof(D3DXSHADER_CONSTANTTABLE))
        goto error;
    object->desc.Creator = ctab_header->Creator ? (LPCSTR)object->ctab + ctab_header->Creator : NULL;
    object->desc.Version = ctab_header->Version;
    object->desc.Constants = ctab_header->Constants;

    *constant_table = (LPD3DXCONSTANTTABLE)object;

    return D3D_OK;

error:

    HeapFree(GetProcessHeap(), 0, object->ctab);
    HeapFree(GetProcessHeap(), 0, object);

    return D3DXERR_INVALIDDATA;
}

HRESULT WINAPI D3DXGetShaderConstantTable(CONST DWORD* byte_code,
                                          LPD3DXCONSTANTTABLE* constant_table)
{
    TRACE("(%p, %p): Forwarded to D3DXGetShaderConstantTableEx\n", byte_code, constant_table);

    return D3DXGetShaderConstantTableEx(byte_code, 0, constant_table);
}
