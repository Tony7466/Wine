/*
 * Copyright 2009 Henri Verbeet for CodeWeavers
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

#include "config.h"
#include "wine/port.h"

#include "d3d10_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d10);

#define MAKE_TAG(ch0, ch1, ch2, ch3) \
    ((DWORD)(ch0) | ((DWORD)(ch1) << 8) | \
    ((DWORD)(ch2) << 16) | ((DWORD)(ch3) << 24 ))
#define TAG_DXBC MAKE_TAG('D', 'X', 'B', 'C')
#define TAG_FX10 MAKE_TAG('F', 'X', '1', '0')
#define TAG_ISGN MAKE_TAG('I', 'S', 'G', 'N')

static const struct ID3D10EffectTechniqueVtbl d3d10_effect_technique_vtbl;
static const struct ID3D10EffectPassVtbl d3d10_effect_pass_vtbl;

static inline void read_dword(const char **ptr, DWORD *d)
{
    memcpy(d, *ptr, sizeof(*d));
    *ptr += sizeof(*d);
}

static inline void skip_dword_unknown(const char **ptr, unsigned int count)
{
    unsigned int i;
    DWORD d;

    FIXME("Skipping %u unknown DWORDs:\n", count);
    for (i = 0; i < count; ++i)
    {
        read_dword(ptr, &d);
        FIXME("\t0x%08x\n", d);
    }
}

static inline void write_dword(char **ptr, DWORD d)
{
    memcpy(*ptr, &d, sizeof(d));
    *ptr += sizeof(d);
}

static inline void write_dword_unknown(char **ptr, DWORD d)
{
    FIXME("Writing unknown DWORD 0x%08x\n", d);
    write_dword(ptr, d);
}

static inline void read_tag(const char **ptr, DWORD *t, char t_str[5])
{
    read_dword(ptr, t);
    memcpy(t_str, t, 4);
    t_str[4] = '\0';
}

static HRESULT parse_dxbc(const char *data, SIZE_T data_size,
        HRESULT (*chunk_handler)(const char *data, DWORD data_size, DWORD tag, void *ctx), void *ctx)
{
    const char *ptr = data;
    HRESULT hr = S_OK;
    DWORD chunk_count;
    DWORD total_size;
    char tag_str[5];
    unsigned int i;
    DWORD tag;

    read_tag(&ptr, &tag, tag_str);
    TRACE("tag: %s\n", tag_str);

    if (tag != TAG_DXBC)
    {
        WARN("Wrong tag.\n");
        return E_FAIL;
    }

    /* checksum? */
    skip_dword_unknown(&ptr, 4);

    skip_dword_unknown(&ptr, 1);

    read_dword(&ptr, &total_size);
    TRACE("total size: %#x\n", total_size);

    read_dword(&ptr, &chunk_count);
    TRACE("chunk count: %#x\n", chunk_count);

    for (i = 0; i < chunk_count; ++i)
    {
        DWORD chunk_tag, chunk_size;
        const char *chunk_ptr;
        DWORD chunk_offset;

        read_dword(&ptr, &chunk_offset);
        TRACE("chunk %u at offset %#x\n", i, chunk_offset);

        chunk_ptr = data + chunk_offset;

        read_dword(&chunk_ptr, &chunk_tag);
        read_dword(&chunk_ptr, &chunk_size);

        hr = chunk_handler(chunk_ptr, chunk_size, chunk_tag, ctx);
        if (FAILED(hr)) break;
    }

    return hr;
}

static HRESULT parse_fx10_pass_index(struct d3d10_effect_pass *p, const char **ptr)
{
    unsigned int i;

    read_dword(ptr, &p->start);
    TRACE("Pass starts at offset %#x\n", p->start);

    read_dword(ptr, &p->variable_count);
    TRACE("Pass has %u variables\n", p->variable_count);

    skip_dword_unknown(ptr, 1);

    p->variables = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, p->variable_count * sizeof(*p->variables));
    if (!p->variables)
    {
        ERR("Failed to allocate variables memory\n");
        return E_OUTOFMEMORY;
    }

    for (i = 0; i < p->variable_count; ++i)
    {
        struct d3d10_effect_variable *v = &p->variables[i];

        v->pass = p;

        read_dword(ptr, &v->type);
        TRACE("Variable %u is of type %#x\n", i, v->type);

        skip_dword_unknown(ptr, 2);

        read_dword(ptr, &v->idx_offset);
        TRACE("Variable %u idx is at offset %#x\n", i, v->idx_offset);
    }

    return S_OK;
}

static HRESULT parse_fx10_technique_index(struct d3d10_effect_technique *t, const char **ptr)
{
    HRESULT hr = S_OK;
    unsigned int i;

    read_dword(ptr, &t->start);
    TRACE("Technique starts at offset %#x\n", t->start);

    read_dword(ptr, &t->pass_count);
    TRACE("Technique has %u passes\n", t->pass_count);

    skip_dword_unknown(ptr, 1);

    t->passes = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, t->pass_count * sizeof(*t->passes));
    if (!t->passes)
    {
        ERR("Failed to allocate passes memory\n");
        return E_OUTOFMEMORY;
    }

    for (i = 0; i < t->pass_count; ++i)
    {
        struct d3d10_effect_pass *p = &t->passes[i];

        p->vtbl = &d3d10_effect_pass_vtbl;
        p->technique = t;

        hr = parse_fx10_pass_index(p, ptr);
        if (FAILED(hr)) break;
    }

    return hr;
}

static HRESULT shader_chunk_handler(const char *data, DWORD data_size, DWORD tag, void *ctx)
{
    struct d3d10_effect_shader_variable *s = ctx;
    char tag_str[5];

    memcpy(tag_str, &tag, 4);
    tag_str[4] = '\0';
    TRACE("tag: %s\n", tag_str);

    TRACE("chunk size: %#x\n", data_size);

    switch(tag)
    {
        case TAG_ISGN:
        {
            /* 32 (DXBC header) + 1 * 4 (chunk index) + 2 * 4 (chunk header) + data_size (chunk data) */
            UINT size = 44 + data_size;
            char *ptr;

            s->input_signature = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
            if (!s->input_signature)
            {
                ERR("Failed to allocate input signature data\n");
                return E_OUTOFMEMORY;
            }
            s->input_signature_size = size;

            ptr = s->input_signature;

            write_dword(&ptr, TAG_DXBC);

            /* signature(?) */
            write_dword_unknown(&ptr, 0);
            write_dword_unknown(&ptr, 0);
            write_dword_unknown(&ptr, 0);
            write_dword_unknown(&ptr, 0);

            /* seems to be always 1 */
            write_dword_unknown(&ptr, 1);

            /* DXBC size */
            write_dword(&ptr, size);

            /* chunk count */
            write_dword(&ptr, 1);

            /* chunk index */
            write_dword(&ptr, (ptr - s->input_signature) + 4);

            /* chunk */
            write_dword(&ptr, TAG_ISGN);
            write_dword(&ptr, data_size);
            memcpy(ptr, data, data_size);
            break;
        }

        default:
            FIXME("Unhandled chunk %s\n", tag_str);
            break;
    }

    return S_OK;
}

static HRESULT parse_shader(struct d3d10_effect_variable *v, const char *data)
{
    ID3D10Device *device = v->pass->technique->effect->device;
    struct d3d10_effect_shader_variable *s;
    const char *ptr = data;
    DWORD dxbc_size;
    HRESULT hr;

    v->data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(struct d3d10_effect_shader_variable));
    if (!v->data)
    {
        ERR("Failed to allocate shader variable memory\n");
        return E_OUTOFMEMORY;
    }

    if (!ptr) return S_OK;

    s = v->data;

    read_dword(&ptr, &dxbc_size);
    TRACE("dxbc size: %#x\n", dxbc_size);

    switch (v->type)
    {
        case D3D10_EVT_VERTEXSHADER:
            hr = ID3D10Device_CreateVertexShader(device, ptr, dxbc_size, &s->shader.vs);
            if (FAILED(hr)) return hr;
            break;

        case D3D10_EVT_PIXELSHADER:
            hr = ID3D10Device_CreatePixelShader(device, ptr, dxbc_size, &s->shader.ps);
            if (FAILED(hr)) return hr;
            break;
        case D3D10_EVT_GEOMETRYSHADER:
            hr = ID3D10Device_CreateGeometryShader(device, ptr, dxbc_size, &s->shader.gs);
            if (FAILED(hr)) return hr;
            break;
    }

    return parse_dxbc(ptr, dxbc_size, shader_chunk_handler, s);
}

static HRESULT parse_fx10_variable(struct d3d10_effect_variable *v, const char *data)
{
    const char *ptr;
    DWORD offset;
    HRESULT hr;

    ptr = data + v->idx_offset;
    read_dword(&ptr, &offset);

    TRACE("Variable of type %#x starts at offset %#x\n", v->type, offset);

    /* FIXME: This probably isn't completely correct. */
    if (offset == 1)
    {
        WARN("Skipping variable\n");
        ptr = NULL;
    }
    else
    {
        ptr = data + offset;
    }

    switch (v->type)
    {
        case D3D10_EVT_VERTEXSHADER:
            TRACE("Vertex shader\n");
            hr = parse_shader(v, ptr);
            break;

        case D3D10_EVT_PIXELSHADER:
            TRACE("Pixel shader\n");
            hr = parse_shader(v, ptr);
            break;

        case D3D10_EVT_GEOMETRYSHADER:
            TRACE("Geometry shader\n");
            hr = parse_shader(v, ptr);
            break;

        default:
            FIXME("Unhandled variable type %#x\n", v->type);
            hr = E_FAIL;
            break;
    }

    return hr;
}

static HRESULT parse_fx10_pass(struct d3d10_effect_pass *p, const char *data)
{
    HRESULT hr = S_OK;
    const char *ptr;
    size_t name_len;
    unsigned int i;

    ptr = data + p->start;

    name_len = strlen(ptr) + 1;
    p->name = HeapAlloc(GetProcessHeap(), 0, name_len);
    if (!p->name)
    {
        ERR("Failed to allocate name memory\n");
        return E_OUTOFMEMORY;
    }

    memcpy(p->name, ptr, name_len);
    ptr += name_len;

    TRACE("pass name: %s\n", p->name);

    for (i = 0; i < p->variable_count; ++i)
    {
        hr = parse_fx10_variable(&p->variables[i], data);
        if (FAILED(hr)) break;
    }

    return hr;
}

static HRESULT parse_fx10_technique(struct d3d10_effect_technique *t, const char *data)
{
    HRESULT hr = S_OK;
    const char *ptr;
    size_t name_len;
    unsigned int i;

    ptr = data + t->start;

    name_len = strlen(ptr) + 1;
    t->name = HeapAlloc(GetProcessHeap(), 0, name_len);
    if (!t->name)
    {
        ERR("Failed to allocate name memory\n");
        return E_OUTOFMEMORY;
    }

    memcpy(t->name, ptr, name_len);
    ptr += name_len;

    TRACE("technique name: %s\n", t->name);

    for (i = 0; i < t->pass_count; ++i)
    {
        hr = parse_fx10_pass(&t->passes[i], data);
        if (FAILED(hr)) break;
    }

    return hr;
}

static HRESULT parse_fx10_body(struct d3d10_effect *e, const char *data, DWORD data_size)
{
    const char *ptr = data + e->index_offset;
    HRESULT hr = S_OK;
    unsigned int i;

    skip_dword_unknown(&ptr, 6);

    e->techniques = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, e->technique_count * sizeof(*e->techniques));
    if (!e->techniques)
    {
        ERR("Failed to allocate techniques memory\n");
        return E_OUTOFMEMORY;
    }

    for (i = 0; i < e->technique_count; ++i)
    {
        struct d3d10_effect_technique *t = &e->techniques[i];

        t->vtbl = &d3d10_effect_technique_vtbl;
        t->effect = e;

        hr = parse_fx10_technique_index(t, &ptr);
        if (FAILED(hr)) break;

        hr = parse_fx10_technique(t, data);
        if (FAILED(hr)) break;
    }

    return hr;
}

static HRESULT parse_fx10(struct d3d10_effect *e, const char *data, DWORD data_size)
{
    const char *ptr = data;
    DWORD unknown;

    /* version info? */
    skip_dword_unknown(&ptr, 2);

    read_dword(&ptr, &unknown);
    FIXME("Unknown 0: %u\n", unknown);
    read_dword(&ptr, &unknown);
    FIXME("Unknown 1: %u\n", unknown);
    read_dword(&ptr, &unknown);
    FIXME("Unknown 2: %u\n", unknown);
    read_dword(&ptr, &unknown);
    FIXME("Unknown 3: %u\n", unknown);
    read_dword(&ptr, &unknown);
    FIXME("Unknown 4: %u\n", unknown);

    read_dword(&ptr, &e->technique_count);
    TRACE("Technique count: %u\n", e->technique_count);

    read_dword(&ptr, &e->index_offset);
    TRACE("Index offset: %#x\n", e->index_offset);

    read_dword(&ptr, &unknown);
    FIXME("Unknown 5: %u\n", unknown);
    read_dword(&ptr, &unknown);
    FIXME("Unknown 6: %u\n", unknown);
    read_dword(&ptr, &unknown);
    FIXME("Unknown 7: %u\n", unknown);

    read_dword(&ptr, &e->blendstate_count);
    TRACE("Blendstate count: %u\n", e->blendstate_count);

    read_dword(&ptr, &unknown);
    FIXME("Unknown 8: %u\n", unknown);
    read_dword(&ptr, &unknown);
    FIXME("Unknown 9: %u\n", unknown);
    read_dword(&ptr, &unknown);
    FIXME("Unknown 10: %u\n", unknown);
    read_dword(&ptr, &unknown);
    FIXME("Unknown 11: %u\n", unknown);
    read_dword(&ptr, &unknown);
    FIXME("Unknown 12: %u\n", unknown);
    read_dword(&ptr, &unknown);
    FIXME("Unknown 13: %u\n", unknown);

    return parse_fx10_body(e, ptr, data_size - (ptr - data));
}

static HRESULT fx10_chunk_handler(const char *data, DWORD data_size, DWORD tag, void *ctx)
{
    struct d3d10_effect *e = ctx;
    char tag_str[5];

    memcpy(tag_str, &tag, 4);
    tag_str[4] = '\0';
    TRACE("tag: %s\n", tag_str);

    TRACE("chunk size: %#x\n", data_size);

    switch(tag)
    {
        case TAG_FX10:
            return parse_fx10(e, data, data_size);

        default:
            FIXME("Unhandled chunk %s\n", tag_str);
            return S_OK;
    }
}

HRESULT d3d10_effect_parse(struct d3d10_effect *This, const void *data, SIZE_T data_size)
{
    return parse_dxbc(data, data_size, fx10_chunk_handler, This);
}

static void d3d10_effect_variable_destroy(struct d3d10_effect_variable *v)
{
    TRACE("variable %p\n", v);

    switch(v->type)
    {
        case D3D10_EVT_VERTEXSHADER:
        case D3D10_EVT_PIXELSHADER:
        case D3D10_EVT_GEOMETRYSHADER:
            HeapFree(GetProcessHeap(), 0, ((struct d3d10_effect_shader_variable *)v->data)->input_signature);
            break;

        default:
            break;
    }
    HeapFree(GetProcessHeap(), 0, v->data);
}

static HRESULT d3d10_effect_variable_apply(struct d3d10_effect_variable *v)
{
    ID3D10Device *device = v->pass->technique->effect->device;

    TRACE("variable %p, type %#x\n", v, v->type);

    switch(v->type)
    {
        case D3D10_EVT_VERTEXSHADER:
            ID3D10Device_VSSetShader(device, ((struct d3d10_effect_shader_variable *)v->data)->shader.vs);
            return S_OK;

        case D3D10_EVT_PIXELSHADER:
            ID3D10Device_PSSetShader(device, ((struct d3d10_effect_shader_variable *)v->data)->shader.ps);
            return S_OK;

        case D3D10_EVT_GEOMETRYSHADER:
            ID3D10Device_GSSetShader(device, ((struct d3d10_effect_shader_variable *)v->data)->shader.gs);
            return S_OK;

        default:
            FIXME("Unhandled variable type %#x\n", v->type);
            return E_FAIL;
    }
}

static void d3d10_effect_pass_destroy(struct d3d10_effect_pass *p)
{
    TRACE("pass %p\n", p);

    HeapFree(GetProcessHeap(), 0, p->name);
    if (p->variables)
    {
        unsigned int i;
        for (i = 0; i < p->variable_count; ++i)
        {
            d3d10_effect_variable_destroy(&p->variables[i]);
        }
        HeapFree(GetProcessHeap(), 0, p->variables);
    }
}

static void d3d10_effect_technique_destroy(struct d3d10_effect_technique *t)
{
    TRACE("technique %p\n", t);

    HeapFree(GetProcessHeap(), 0, t->name);
    if (t->passes)
    {
        unsigned int i;
        for (i = 0; i < t->pass_count; ++i)
        {
            d3d10_effect_pass_destroy(&t->passes[i]);
        }
        HeapFree(GetProcessHeap(), 0, t->passes);
    }
}

/* IUnknown methods */

static HRESULT STDMETHODCALLTYPE d3d10_effect_QueryInterface(ID3D10Effect *iface, REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D10Effect)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        IUnknown_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d10_effect_AddRef(ID3D10Effect *iface)
{
    struct d3d10_effect *This = (struct d3d10_effect *)iface;
    ULONG refcount = InterlockedIncrement(&This->refcount);

    TRACE("%p increasing refcount to %u\n", This, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d10_effect_Release(ID3D10Effect *iface)
{
    struct d3d10_effect *This = (struct d3d10_effect *)iface;
    ULONG refcount = InterlockedDecrement(&This->refcount);

    TRACE("%p decreasing refcount to %u\n", This, refcount);

    if (!refcount)
    {
        if (This->techniques)
        {
            unsigned int i;
            for (i = 0; i < This->technique_count; ++i)
            {
                d3d10_effect_technique_destroy(&This->techniques[i]);
            }
            HeapFree(GetProcessHeap(), 0, This->techniques);
        }
        ID3D10Device_Release(This->device);
        HeapFree(GetProcessHeap(), 0, This);
    }

    return refcount;
}

/* ID3D10Effect methods */

static BOOL STDMETHODCALLTYPE d3d10_effect_IsValid(ID3D10Effect *iface)
{
    FIXME("iface %p stub!\n", iface);

    return FALSE;
}

static BOOL STDMETHODCALLTYPE d3d10_effect_IsPool(ID3D10Effect *iface)
{
    FIXME("iface %p stub!\n", iface);

    return FALSE;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_GetDevice(ID3D10Effect *iface, ID3D10Device **device)
{
    struct d3d10_effect *This = (struct d3d10_effect *)iface;

    TRACE("iface %p, device %p\n", iface, device);

    ID3D10Device_AddRef(This->device);
    *device = This->device;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_GetDesc(ID3D10Effect *iface, D3D10_EFFECT_DESC *desc)
{
    FIXME("iface %p, desc %p stub!\n", iface, desc);

    return E_NOTIMPL;
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_GetConstantBufferByIndex(ID3D10Effect *iface,
        UINT index)
{
    FIXME("iface %p, index %u stub!\n", iface, index);

    return NULL;
}

static struct ID3D10EffectConstantBuffer * STDMETHODCALLTYPE d3d10_effect_GetConstantBufferByName(ID3D10Effect *iface,
        LPCSTR name)
{
    FIXME("iface %p, name \"%s\" stub!\n", iface, name);

    return NULL;
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_GetVariableByIndex(ID3D10Effect *iface, UINT index)
{
    FIXME("iface %p, index %u stub!\n", iface, index);

    return NULL;
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_GetVariableByName(ID3D10Effect *iface, LPCSTR name)
{
    FIXME("iface %p, name \"%s\" stub!\n", iface, name);

    return NULL;
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_GetVariableBySemantic(ID3D10Effect *iface,
        LPCSTR semantic)
{
    FIXME("iface %p, semantic \"%s\" stub!\n", iface, semantic);

    return NULL;
}

static struct ID3D10EffectTechnique * STDMETHODCALLTYPE d3d10_effect_GetTechniqueByIndex(ID3D10Effect *iface,
        UINT index)
{
    struct d3d10_effect *This = (struct d3d10_effect *)iface;
    struct d3d10_effect_technique *t;

    TRACE("iface %p, index %u\n", iface, index);

    if (index >= This->technique_count)
    {
        WARN("Invalid index specified\n");
        return NULL;
    }

    t = &This->techniques[index];

    TRACE("Returning technique %p, \"%s\"\n", t, t->name);

    return (ID3D10EffectTechnique *)t;
}

static struct ID3D10EffectTechnique * STDMETHODCALLTYPE d3d10_effect_GetTechniqueByName(ID3D10Effect *iface,
        LPCSTR name)
{
    struct d3d10_effect *This = (struct d3d10_effect *)iface;
    unsigned int i;

    TRACE("iface %p, name \"%s\"\n", iface, name);

    for (i = 0; i < This->technique_count; ++i)
    {
        struct d3d10_effect_technique *t = &This->techniques[i];
        if (!strcmp(t->name, name))
        {
            TRACE("Returning technique %p\n", t);
            return (ID3D10EffectTechnique *)t;
        }
    }

    return NULL;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_Optimize(ID3D10Effect *iface)
{
    FIXME("iface %p stub!\n", iface);

    return E_NOTIMPL;
}

static BOOL STDMETHODCALLTYPE d3d10_effect_IsOptimized(ID3D10Effect *iface)
{
    FIXME("iface %p stub!\n", iface);

    return FALSE;
}

const struct ID3D10EffectVtbl d3d10_effect_vtbl =
{
    /* IUnknown methods */
    d3d10_effect_QueryInterface,
    d3d10_effect_AddRef,
    d3d10_effect_Release,
    /* ID3D10Effect methods */
    d3d10_effect_IsValid,
    d3d10_effect_IsPool,
    d3d10_effect_GetDevice,
    d3d10_effect_GetDesc,
    d3d10_effect_GetConstantBufferByIndex,
    d3d10_effect_GetConstantBufferByName,
    d3d10_effect_GetVariableByIndex,
    d3d10_effect_GetVariableByName,
    d3d10_effect_GetVariableBySemantic,
    d3d10_effect_GetTechniqueByIndex,
    d3d10_effect_GetTechniqueByName,
    d3d10_effect_Optimize,
    d3d10_effect_IsOptimized,
};

/* ID3D10EffectTechnique methods */

static BOOL STDMETHODCALLTYPE d3d10_effect_technique_IsValid(ID3D10EffectTechnique *iface)
{
    FIXME("iface %p stub!\n", iface);

    return FALSE;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_technique_GetDesc(ID3D10EffectTechnique *iface,
        D3D10_TECHNIQUE_DESC *desc)
{
    struct d3d10_effect_technique *This = (struct d3d10_effect_technique *)iface;

    TRACE("iface %p, desc %p\n", iface, desc);

    desc->Name = This->name;
    desc->Passes = This->pass_count;
    WARN("Annotations not implemented\n");
    desc->Annotations = 0;

    return S_OK;
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_technique_GetAnnotationByIndex(
        ID3D10EffectTechnique *iface, UINT index)
{
    FIXME("iface %p, index %u stub!\n", iface, index);

    return NULL;
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_technique_GetAnnotationByName(
        ID3D10EffectTechnique *iface, LPCSTR name)
{
    FIXME("iface %p, name \"%s\" stub!\n", iface, name);

    return NULL;
}

static struct ID3D10EffectPass * STDMETHODCALLTYPE d3d10_effect_technique_GetPassByIndex(ID3D10EffectTechnique *iface,
        UINT index)
{
    struct d3d10_effect_technique *This = (struct d3d10_effect_technique *)iface;
    struct d3d10_effect_pass *p;

    TRACE("iface %p, index %u\n", iface, index);

    if (index >= This->pass_count)
    {
        WARN("Invalid index specified\n");
        return NULL;
    }

    p = &This->passes[index];

    TRACE("Returning pass %p, \"%s\"\n", p, p->name);

    return (ID3D10EffectPass *)p;
}

static struct ID3D10EffectPass * STDMETHODCALLTYPE d3d10_effect_technique_GetPassByName(ID3D10EffectTechnique *iface,
        LPCSTR name)
{
    struct d3d10_effect_technique *This = (struct d3d10_effect_technique *)iface;
    unsigned int i;

    TRACE("iface %p, name \"%s\"\n", iface, name);

    for (i = 0; i < This->pass_count; ++i)
    {
        struct d3d10_effect_pass *p = &This->passes[i];
        if (!strcmp(p->name, name))
        {
            TRACE("Returning pass %p\n", p);
            return (ID3D10EffectPass *)p;
        }
    }

    return NULL;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_technique_ComputeStateBlockMask(ID3D10EffectTechnique *iface,
        D3D10_STATE_BLOCK_MASK *mask)
{
    FIXME("iface %p,mask %p stub!\n", iface, mask);

    return E_NOTIMPL;
}

static const struct ID3D10EffectTechniqueVtbl d3d10_effect_technique_vtbl =
{
    /* ID3D10EffectTechnique methods */
    d3d10_effect_technique_IsValid,
    d3d10_effect_technique_GetDesc,
    d3d10_effect_technique_GetAnnotationByIndex,
    d3d10_effect_technique_GetAnnotationByName,
    d3d10_effect_technique_GetPassByIndex,
    d3d10_effect_technique_GetPassByName,
    d3d10_effect_technique_ComputeStateBlockMask,
};

/* ID3D10EffectPass methods */

static BOOL STDMETHODCALLTYPE d3d10_effect_pass_IsValid(ID3D10EffectPass *iface)
{
    FIXME("iface %p stub!\n", iface);

    return FALSE;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_pass_GetDesc(ID3D10EffectPass *iface, D3D10_PASS_DESC *desc)
{
    struct d3d10_effect_pass *This = (struct d3d10_effect_pass *)iface;
    unsigned int i;

    FIXME("iface %p, desc %p partial stub!\n", iface, desc);

    memset(desc, 0, sizeof(*desc));
    desc->Name = This->name;
    for (i = 0; i < This->variable_count; ++i)
    {
        struct d3d10_effect_variable *v = &This->variables[i];
        if (v->type == D3D10_EVT_VERTEXSHADER)
        {
            struct d3d10_effect_shader_variable *s = v->data;
            desc->pIAInputSignature = (BYTE *)s->input_signature;
            desc->IAInputSignatureSize = s->input_signature_size;
            break;
        }
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_pass_GetVertexShaderDesc(ID3D10EffectPass *iface,
        D3D10_PASS_SHADER_DESC *desc)
{
    FIXME("iface %p, desc %p stub!\n", iface, desc);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_pass_GetGeometryShaderDesc(ID3D10EffectPass *iface,
        D3D10_PASS_SHADER_DESC *desc)
{
    FIXME("iface %p, desc %p stub!\n", iface, desc);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_pass_GetPixelShaderDesc(ID3D10EffectPass *iface,
        D3D10_PASS_SHADER_DESC *desc)
{
    FIXME("iface %p, desc %p stub!\n", iface, desc);

    return E_NOTIMPL;
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_pass_GetAnnotationByIndex(ID3D10EffectPass *iface,
        UINT index)
{
    FIXME("iface %p, index %u stub!\n", iface, index);

    return NULL;
}

static struct ID3D10EffectVariable * STDMETHODCALLTYPE d3d10_effect_pass_GetAnnotationByName(ID3D10EffectPass *iface,
        LPCSTR name)
{
    FIXME("iface %p, name \"%s\" stub!\n", iface, name);

    return NULL;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_pass_Apply(ID3D10EffectPass *iface, UINT flags)
{
    struct d3d10_effect_pass *This = (struct d3d10_effect_pass *)iface;
    HRESULT hr = S_OK;
    unsigned int i;

    TRACE("iface %p, flags %#x\n", iface, flags);

    if (flags) FIXME("Ignoring flags (%#x)\n", flags);

    for (i = 0; i < This->variable_count; ++i)
    {
        hr = d3d10_effect_variable_apply(&This->variables[i]);
        if (FAILED(hr)) break;
    }

    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d10_effect_pass_ComputeStateBlockMask(ID3D10EffectPass *iface,
        D3D10_STATE_BLOCK_MASK *mask)
{
    FIXME("iface %p, mask %p stub!\n", iface, mask);

    return E_NOTIMPL;
}

static const struct ID3D10EffectPassVtbl d3d10_effect_pass_vtbl =
{
    /* ID3D10EffectPass methods */
    d3d10_effect_pass_IsValid,
    d3d10_effect_pass_GetDesc,
    d3d10_effect_pass_GetVertexShaderDesc,
    d3d10_effect_pass_GetGeometryShaderDesc,
    d3d10_effect_pass_GetPixelShaderDesc,
    d3d10_effect_pass_GetAnnotationByIndex,
    d3d10_effect_pass_GetAnnotationByName,
    d3d10_effect_pass_Apply,
    d3d10_effect_pass_ComputeStateBlockMask,
};
