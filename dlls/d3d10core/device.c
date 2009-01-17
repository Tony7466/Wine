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
 *
 */

#include "config.h"
#include "wine/port.h"

#include "d3d10core_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d10core);

/* Inner IUnknown methods */

static inline struct d3d10_device *d3d10_device_from_inner_unknown(IUnknown *iface)
{
    return (struct d3d10_device *)((char*)iface - FIELD_OFFSET(struct d3d10_device, inner_unknown_vtbl));
}

static HRESULT STDMETHODCALLTYPE d3d10_device_inner_QueryInterface(IUnknown *iface, REFIID riid, void **object)
{
    struct d3d10_device *This = d3d10_device_from_inner_unknown(iface);

    TRACE("iface %p, riid %s, object %p\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_IUnknown)
            || IsEqualGUID(riid, &IID_ID3D10Device))
    {
        IUnknown_AddRef((IUnknown *)This);
        *object = This;
        return S_OK;
    }

    if (IsEqualGUID(riid, &IID_IWineD3DDeviceParent))
    {
        IUnknown_AddRef((IUnknown *)&This->device_parent_vtbl);
        *object = &This->device_parent_vtbl;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d10_device_inner_AddRef(IUnknown *iface)
{
    struct d3d10_device *This = d3d10_device_from_inner_unknown(iface);
    ULONG refcount = InterlockedIncrement(&This->refcount);

    TRACE("%p increasing refcount to %u\n", This, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d10_device_inner_Release(IUnknown *iface)
{
    struct d3d10_device *This = d3d10_device_from_inner_unknown(iface);
    ULONG refcount = InterlockedDecrement(&This->refcount);

    TRACE("%p decreasing refcount to %u\n", This, refcount);

    return refcount;
}

/* IUnknown methods */

static HRESULT STDMETHODCALLTYPE d3d10_device_QueryInterface(ID3D10Device *iface, REFIID riid, void **object)
{
    struct d3d10_device *This = (struct d3d10_device *)iface;
    TRACE("Forwarding to outer IUnknown\n");
    return IUnknown_QueryInterface(This->outer_unknown, riid, object);
}

static ULONG STDMETHODCALLTYPE d3d10_device_AddRef(ID3D10Device *iface)
{
    struct d3d10_device *This = (struct d3d10_device *)iface;
    TRACE("Forwarding to outer IUnknown\n");
    return IUnknown_AddRef(This->outer_unknown);
}

static ULONG STDMETHODCALLTYPE d3d10_device_Release(ID3D10Device *iface)
{
    struct d3d10_device *This = (struct d3d10_device *)iface;
    TRACE("Forwarding to outer IUnknown\n");
    return IUnknown_Release(This->outer_unknown);
}

/* ID3D10Device methods */

static void STDMETHODCALLTYPE d3d10_device_VSSetConstantBuffers(ID3D10Device *iface,
        UINT start_slot, UINT buffer_count, ID3D10Buffer *const *buffers)
{
    FIXME("iface %p, start_slot %u, buffer_count %u, buffers %p stub!\n",
            iface, start_slot, buffer_count, buffers);
}

static void STDMETHODCALLTYPE d3d10_device_PSSetShaderResources(ID3D10Device *iface,
        UINT start_slot, UINT view_count, ID3D10ShaderResourceView *const *views)
{
    FIXME("iface %p, start_slot %u, view_count %u, views %p stub!\n",
            iface, start_slot, view_count, views);
}

static void STDMETHODCALLTYPE d3d10_device_PSSetShader(ID3D10Device *iface, ID3D10PixelShader *shader)
{
    FIXME("iface %p, shader %p stub!\n", iface, shader);
}

static void STDMETHODCALLTYPE d3d10_device_PSSetSamplers(ID3D10Device *iface,
        UINT start_slot, UINT sampler_count, ID3D10SamplerState *const *samplers)
{
    FIXME("iface %p, start_slot %u, sampler_count %u, samplers %p stub!\n",
            iface, start_slot, sampler_count, samplers);
}

static void STDMETHODCALLTYPE d3d10_device_VSSetShader(ID3D10Device *iface, ID3D10VertexShader *shader)
{
    FIXME("iface %p, shader %p stub!\n", iface, shader);
}

static void STDMETHODCALLTYPE d3d10_device_DrawIndexed(ID3D10Device *iface,
        UINT index_count, UINT start_index_location, INT base_vertex_location)
{
    FIXME("iface %p, index_count %u, start_index_location %u, base_vertex_location %d stub!\n",
            iface, index_count, start_index_location, base_vertex_location);
}

static void STDMETHODCALLTYPE d3d10_device_Draw(ID3D10Device *iface,
        UINT vertex_count, UINT start_vertex_location)
{
    FIXME("iface %p, vertex_count %u, start_vertex_location %u stub!\n",
            iface, vertex_count, start_vertex_location);
}

static void STDMETHODCALLTYPE d3d10_device_PSSetConstantBuffers(ID3D10Device *iface,
        UINT start_slot, UINT buffer_count, ID3D10Buffer *const *buffers)
{
    FIXME("iface %p, start_slot %u, buffer_count %u, buffers %p stub!\n",
            iface, start_slot, buffer_count, buffers);
}

static void STDMETHODCALLTYPE d3d10_device_IASetInputLayout(ID3D10Device *iface, ID3D10InputLayout *input_layout)
{
    FIXME("iface %p, input_layout %p stub!\n", iface, input_layout);
}

static void STDMETHODCALLTYPE d3d10_device_IASetVertexBuffers(ID3D10Device *iface,
        UINT start_slot, UINT buffer_count, ID3D10Buffer *const *buffers,
        const UINT *strides, const UINT *offsets)
{
    FIXME("iface %p, start_slot %u, buffer_count %u, buffers %p, strides %p, offsets %p stub!\n",
            iface, start_slot, buffer_count, buffers, strides, offsets);
}

static void STDMETHODCALLTYPE d3d10_device_IASetIndexBuffer(ID3D10Device *iface,
        ID3D10Buffer *buffer, DXGI_FORMAT format, UINT offset)
{
    FIXME("iface %p, buffer %p, format %s, offset %u stub!\n",
            iface, buffer, debug_dxgi_format(format), offset);
}

static void STDMETHODCALLTYPE d3d10_device_DrawIndexedInstanced(ID3D10Device *iface,
        UINT instance_index_count, UINT instance_count, UINT start_index_location,
        INT base_vertex_location, UINT start_instance_location)
{
    FIXME("iface %p, instance_index_count %u, instance_count %u, start_index_location %u,\n"
            "\tbase_vertex_location %d, start_instance_location %u stub!\n",
            iface, instance_index_count, instance_count, start_index_location,
            base_vertex_location, start_instance_location);
}

static void STDMETHODCALLTYPE d3d10_device_DrawInstanced(ID3D10Device *iface,
        UINT instance_vertex_count, UINT instance_count,
        UINT start_vertex_location, UINT start_instance_location)
{
    FIXME("iface %p, instance_vertex_count %u, instance_count %u, start_vertex_location %u,\n"
            "\tstart_instance_location %u stub!\n", iface, instance_vertex_count, instance_count,
            start_vertex_location, start_instance_location);
}

static void STDMETHODCALLTYPE d3d10_device_GSSetConstantBuffers(ID3D10Device *iface,
        UINT start_slot, UINT buffer_count, ID3D10Buffer *const *buffers)
{
    FIXME("iface %p, start_slot %u, buffer_count %u, buffers %p stub!\n",
            iface, start_slot, buffer_count, buffers);
}

static void STDMETHODCALLTYPE d3d10_device_GSSetShader(ID3D10Device *iface, ID3D10GeometryShader *shader)
{
    FIXME("iface %p, shader %p stub!\n", iface, shader);
}

static void STDMETHODCALLTYPE d3d10_device_IASetPrimitiveTopology(ID3D10Device *iface, D3D10_PRIMITIVE_TOPOLOGY topology)
{
    FIXME("iface %p, topology %s stub!\n", iface, debug_d3d10_primitive_topology(topology));
}

static void STDMETHODCALLTYPE d3d10_device_VSSetShaderResources(ID3D10Device *iface,
        UINT start_slot, UINT view_count, ID3D10ShaderResourceView *const *views)
{
    FIXME("iface %p, start_slot %u, view_count %u, views %p stub!\n",
            iface, start_slot, view_count, views);
}

static void STDMETHODCALLTYPE d3d10_device_VSSetSamplers(ID3D10Device *iface,
        UINT start_slot, UINT sampler_count, ID3D10SamplerState *const *samplers)
{
    FIXME("iface %p, start_slot %u, sampler_count %u, samplers %p stub!\n",
            iface, start_slot, sampler_count, samplers);
}

static void STDMETHODCALLTYPE d3d10_device_SetPredication(ID3D10Device *iface, ID3D10Predicate *predicate, BOOL value)
{
    FIXME("iface %p, predicate %p, value %d stub!\n", iface, predicate, value);
}

static void STDMETHODCALLTYPE d3d10_device_GSSetShaderResources(ID3D10Device *iface,
        UINT start_slot, UINT view_count, ID3D10ShaderResourceView *const *views)
{
    FIXME("iface %p, start_slot %u, view_count %u, views %p stub!\n",
            iface, start_slot, view_count, views);
}

static void STDMETHODCALLTYPE d3d10_device_GSSetSamplers(ID3D10Device *iface,
        UINT start_slot, UINT sampler_count, ID3D10SamplerState *const *samplers)
{
    FIXME("iface %p, start_slot %u, sampler_count %u, samplers %p stub!\n",
            iface, start_slot, sampler_count, samplers);
}

static void STDMETHODCALLTYPE d3d10_device_OMSetRenderTargets(ID3D10Device *iface,
        UINT render_target_view_count, ID3D10RenderTargetView *const *render_target_views,
        ID3D10DepthStencilView *depth_stencil_view)
{
    FIXME("iface %p, render_target_view_count %u, render_target_views %p, depth_stencil_view %p\n",
            iface, render_target_view_count, render_target_views, depth_stencil_view);
}

static void STDMETHODCALLTYPE d3d10_device_OMSetBlendState(ID3D10Device *iface,
        ID3D10BlendState *blend_state, const FLOAT blend_factor[4], UINT sample_mask)
{
    FIXME("iface %p, blend_state %p, blend_factor [%f %f %f %f], sample_mask 0x%08x stub!\n",
            iface, blend_state, blend_factor[0], blend_factor[1], blend_factor[2], blend_factor[3], sample_mask);
}

static void STDMETHODCALLTYPE d3d10_device_OMSetDepthStencilState(ID3D10Device *iface,
        ID3D10DepthStencilState *depth_stencil_state, UINT stencil_ref)
{
    FIXME("iface %p, depth_stencil_state %p, stencil_ref %u stub!\n",
            iface, depth_stencil_state, stencil_ref);
}

static void STDMETHODCALLTYPE d3d10_device_SOSetTargets(ID3D10Device *iface,
        UINT target_count, ID3D10Buffer *const *targets, const UINT *offsets)
{
    FIXME("iface %p, target_count %u, targets %p, offsets %p stub!\n", iface, target_count, targets, offsets);
}

static void STDMETHODCALLTYPE d3d10_device_DrawAuto(ID3D10Device *iface)
{
    FIXME("iface %p stub!\n", iface);
}

static void STDMETHODCALLTYPE d3d10_device_RSSetState(ID3D10Device *iface, ID3D10RasterizerState *rasterizer_state)
{
    FIXME("iface %p, rasterizer_state %p stub!\n", iface, rasterizer_state);
}

static void STDMETHODCALLTYPE d3d10_device_RSSetViewports(ID3D10Device *iface,
        UINT viewport_count, const D3D10_VIEWPORT *viewports)
{
    FIXME("iface %p, viewport_count %u, viewports %p stub!\n", iface, viewport_count, viewports);
}

static void STDMETHODCALLTYPE d3d10_device_RSSetScissorRects(ID3D10Device *iface,
        UINT rect_count, const D3D10_RECT *rects)
{
    FIXME("iface %p, rect_count %u, rects %p\n", iface, rect_count, rects);
}

static void STDMETHODCALLTYPE d3d10_device_CopySubresourceRegion(ID3D10Device *iface,
        ID3D10Resource *dst_resource, UINT dst_subresource_idx, UINT dst_x, UINT dst_y, UINT dst_z,
        ID3D10Resource *src_resource, UINT src_subresource_idx, const D3D10_BOX *src_box)
{
    FIXME("iface %p, dst_resource %p, dst_subresource_idx %u, dst_x %u, dst_y %u, dst_z %u,\n"
            "\tsrc_resource %p, src_subresource_idx %u, src_box %p stub!\n",
            iface, dst_resource, dst_subresource_idx, dst_x, dst_y, dst_z,
            src_resource, src_subresource_idx, src_box);
}

static void STDMETHODCALLTYPE d3d10_device_CopyResource(ID3D10Device *iface,
        ID3D10Resource *dst_resource, ID3D10Resource *src_resource)
{
    FIXME("iface %p, dst_resource %p, src_resource %p stub!\n", iface, dst_resource, src_resource);
}

static void STDMETHODCALLTYPE d3d10_device_UpdateSubresource(ID3D10Device *iface,
        ID3D10Resource *resource, UINT subresource_idx, const D3D10_BOX *box,
        const void *data, UINT row_pitch, UINT depth_pitch)
{
    FIXME("iface %p, resource %p, subresource_idx %u, box %p, data %p, row_pitch %u, depth_pitch %u stub!\n",
            iface, resource, subresource_idx, box, data, row_pitch, depth_pitch);
}

static void STDMETHODCALLTYPE d3d10_device_ClearRenderTargetView(ID3D10Device *iface,
        ID3D10RenderTargetView *render_target_view, const FLOAT color_rgba[4])
{
    FIXME("iface %p, render_target_view %p, color_rgba [%f %f %f %f] stub!\n",
            iface, render_target_view, color_rgba[0], color_rgba[1], color_rgba[2], color_rgba[3]);
}

static void STDMETHODCALLTYPE d3d10_device_ClearDepthStencilView(ID3D10Device *iface,
        ID3D10DepthStencilView *depth_stencil_view, UINT flags, FLOAT depth, UINT8 stencil)
{
    FIXME("iface %p, depth_stencil_view %p, flags %#x, depth %f, stencil %u stub!\n",
            iface, depth_stencil_view, flags, depth, stencil);
}

static void STDMETHODCALLTYPE d3d10_device_GenerateMips(ID3D10Device *iface, ID3D10ShaderResourceView *shader_resource_view)
{
    FIXME("iface %p, shader_resource_view %p stub!\n", iface, shader_resource_view);
}

static void STDMETHODCALLTYPE d3d10_device_ResolveSubresource(ID3D10Device *iface,
        ID3D10Resource *dst_resource, UINT dst_subresource_idx,
        ID3D10Resource *src_resource, UINT src_subresource_idx, DXGI_FORMAT format)
{
    FIXME("iface %p, dst_resource %p, dst_subresource_idx %u,\n"
            "\tsrc_resource %p, src_subresource_idx %u, format %s stub!\n",
            iface, dst_resource, dst_subresource_idx,
            src_resource, src_subresource_idx, debug_dxgi_format(format));
}

static void STDMETHODCALLTYPE d3d10_device_VSGetConstantBuffers(ID3D10Device *iface,
        UINT start_slot, UINT buffer_count, ID3D10Buffer **buffers)
{
    FIXME("iface %p, start_slot %u, buffer_count %u, buffers %p stub!\n",
            iface, start_slot, buffer_count, buffers);
}

static void STDMETHODCALLTYPE d3d10_device_PSGetShaderResources(ID3D10Device *iface,
        UINT start_slot, UINT view_count, ID3D10ShaderResourceView **views)
{
    FIXME("iface %p, start_slot %u, view_count %u, views %p stub!\n",
            iface, start_slot, view_count, views);
}

static void STDMETHODCALLTYPE d3d10_device_PSGetShader(ID3D10Device *iface, ID3D10PixelShader **shader)
{
    FIXME("iface %p, shader %p stub!\n", iface, shader);
}

static void STDMETHODCALLTYPE d3d10_device_PSGetSamplers(ID3D10Device *iface,
        UINT start_slot, UINT sampler_count, ID3D10SamplerState **samplers)
{
    FIXME("iface %p, start_slot %u, sampler_count %u, samplers %p stub!\n",
            iface, start_slot, sampler_count, samplers);
}

static void STDMETHODCALLTYPE d3d10_device_VSGetShader(ID3D10Device *iface, ID3D10VertexShader **shader)
{
    FIXME("iface %p, shader %p stub!\n", iface, shader);
}

static void STDMETHODCALLTYPE d3d10_device_PSGetConstantBuffers(ID3D10Device *iface,
        UINT start_slot, UINT buffer_count, ID3D10Buffer **buffers)
{
    FIXME("iface %p, start_slot %u, buffer_count %u, buffer %p stub!\n",
            iface, start_slot, buffer_count, buffers);
}

static void STDMETHODCALLTYPE d3d10_device_IAGetInputLayout(ID3D10Device *iface, ID3D10InputLayout **input_layout)
{
    FIXME("iface %p, input_layout %p stub!\n", iface, input_layout);
}

static void STDMETHODCALLTYPE d3d10_device_IAGetVertexBuffers(ID3D10Device *iface,
        UINT start_slot, UINT buffer_count, ID3D10Buffer **buffers, UINT *strides, UINT *offsets)
{
    FIXME("iface %p, start_slot %u, buffer_count %u, buffers %p, strides %p, offsets %p stub!\n",
            iface, start_slot, buffer_count, buffers, strides, offsets);
}

static void STDMETHODCALLTYPE d3d10_device_IAGetIndexBuffer(ID3D10Device *iface,
        ID3D10Buffer **buffer, DXGI_FORMAT *format, UINT *offset)
{
    FIXME("iface %p, buffer %p, format %p, offset %p stub!\n", iface, buffer, format, offset);
}

static void STDMETHODCALLTYPE d3d10_device_GSGetConstantBuffers(ID3D10Device *iface,
        UINT start_slot, UINT buffer_count, ID3D10Buffer **buffers)
{
    FIXME("iface %p, start_slot %u, buffer_count %u, buffers %p stub!\n",
            iface, start_slot, buffer_count, buffers);
}

static void STDMETHODCALLTYPE d3d10_device_GSGetShader(ID3D10Device *iface, ID3D10GeometryShader **shader)
{
    FIXME("iface %p, shader %p stub!\n", iface, shader);
}

static void STDMETHODCALLTYPE d3d10_device_IAGetPrimitiveTopology(ID3D10Device *iface, D3D10_PRIMITIVE_TOPOLOGY *topology)
{
    FIXME("iface %p, topology %p stub!\n", iface, topology);
}

static void STDMETHODCALLTYPE d3d10_device_VSGetShaderResources(ID3D10Device *iface,
        UINT start_slot, UINT view_count, ID3D10ShaderResourceView **views)
{
    FIXME("iface %p, start_slot %u, view_count %u, views %p stub!\n",
            iface, start_slot, view_count, views);
}

static void STDMETHODCALLTYPE d3d10_device_VSGetSamplers(ID3D10Device *iface,
        UINT start_slot, UINT sampler_count, ID3D10SamplerState **samplers)
{
    FIXME("iface %p, start_slot %u, sampler_count %u, samplers %p stub!\n",
            iface, start_slot, sampler_count, samplers);
}

static void STDMETHODCALLTYPE d3d10_device_GetPredication(ID3D10Device *iface,
        ID3D10Predicate **predicate, BOOL *value)
{
    FIXME("iface %p, predicate %p, value %p stub!\n", iface, predicate, value);
}

static void STDMETHODCALLTYPE d3d10_device_GSGetShaderResources(ID3D10Device *iface,
        UINT start_slot, UINT view_count, ID3D10ShaderResourceView **views)
{
    FIXME("iface %p, start_slot %u, view_count %u, views %p stub!\n",
            iface, start_slot, view_count, views);
}

static void STDMETHODCALLTYPE d3d10_device_GSGetSamplers(ID3D10Device *iface,
        UINT start_slot, UINT sampler_count, ID3D10SamplerState **samplers)
{
    FIXME("iface %p, start_slot %u, sampler_count %u, samplers %p stub!\n",
            iface, start_slot, sampler_count, samplers);
}

static void STDMETHODCALLTYPE d3d10_device_OMGetRenderTargets(ID3D10Device *iface,
        UINT view_count, ID3D10RenderTargetView **render_target_views, ID3D10DepthStencilView **depth_stencil_view)
{
    FIXME("iface %p, view_count %u, render_target_views %p, depth_stencil_view %p stub!\n",
            iface, view_count, render_target_views, depth_stencil_view);
}

static void STDMETHODCALLTYPE d3d10_device_OMGetBlendState(ID3D10Device *iface,
        ID3D10BlendState **blend_state, FLOAT blend_factor[4], UINT *sample_mask)
{
    FIXME("iface %p, blend_state %p, blend_factor %p, sample_mask %p stub!\n",
            iface, blend_state, blend_factor, sample_mask);
}

static void STDMETHODCALLTYPE d3d10_device_OMGetDepthStencilState(ID3D10Device *iface,
        ID3D10DepthStencilState **depth_stencil_state, UINT *stencil_ref)
{
    FIXME("iface %p, depth_stencil_state %p, stencil_ref %p stub!\n",
            iface, depth_stencil_state, stencil_ref);
}

static void STDMETHODCALLTYPE d3d10_device_SOGetTargets(ID3D10Device *iface,
        UINT buffer_count, ID3D10Buffer **buffers, UINT *offsets)
{
    FIXME("iface %p, buffer_count %u, buffers %p, offsets %p stub!\n",
            iface, buffer_count, buffers, offsets);
}

static void STDMETHODCALLTYPE d3d10_device_RSGetState(ID3D10Device *iface, ID3D10RasterizerState **rasterizer_state)
{
    FIXME("iface %p, rasterizer_state %p stub!\n", iface, rasterizer_state);
}

static void STDMETHODCALLTYPE d3d10_device_RSGetViewports(ID3D10Device *iface,
        UINT *viewport_count, D3D10_VIEWPORT *viewports)
{
    FIXME("iface %p, viewport_count %p, viewports %p stub!\n", iface, viewport_count, viewports);
}

static void STDMETHODCALLTYPE d3d10_device_RSGetScissorRects(ID3D10Device *iface, UINT *rect_count, D3D10_RECT *rects)
{
    FIXME("iface %p, rect_count %p, rects %p stub!\n", iface, rect_count, rects);
}

static HRESULT STDMETHODCALLTYPE d3d10_device_GetDeviceRemovedReason(ID3D10Device *iface)
{
    FIXME("iface %p stub!\n", iface);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_SetExceptionMode(ID3D10Device *iface, UINT flags)
{
    FIXME("iface %p, flags %#x stub!\n", iface, flags);

    return E_NOTIMPL;
}

static UINT STDMETHODCALLTYPE d3d10_device_GetExceptionMode(ID3D10Device *iface)
{
    FIXME("iface %p stub!\n", iface);

    return 0;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_GetPrivateData(ID3D10Device *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    FIXME("iface %p, guid %s, data_size %p, data %p stub!\n",
            iface, debugstr_guid(guid), data_size, data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_SetPrivateData(ID3D10Device *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    FIXME("iface %p, guid %s, data_size %u, data %p stub!\n",
            iface, debugstr_guid(guid), data_size, data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_SetPrivateDataInterface(ID3D10Device *iface,
        REFGUID guid, const IUnknown *data)
{
    FIXME("iface %p, guid %s, data %p stub!\n", iface, debugstr_guid(guid), data);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d3d10_device_ClearState(ID3D10Device *iface)
{
    FIXME("iface %p stub!\n", iface);
}

static void STDMETHODCALLTYPE d3d10_device_Flush(ID3D10Device *iface)
{
    FIXME("iface %p stub!\n", iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateBuffer(ID3D10Device *iface,
        const D3D10_BUFFER_DESC *desc, const D3D10_SUBRESOURCE_DATA *data, ID3D10Buffer **buffer)
{
    FIXME("iface %p, desc %p, data %p, buffer %p stub!\n", iface, desc, data, buffer);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateTexture1D(ID3D10Device *iface,
        const D3D10_TEXTURE1D_DESC *desc, const D3D10_SUBRESOURCE_DATA *data, ID3D10Texture1D **texture)
{
    FIXME("iface %p, desc %p, data %p, texture %p stub!\n", iface, desc, data, texture);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateTexture2D(ID3D10Device *iface,
        const D3D10_TEXTURE2D_DESC *desc, const D3D10_SUBRESOURCE_DATA *data, ID3D10Texture2D **texture)
{
    struct d3d10_texture2d *object;

    FIXME("iface %p, desc %p, data %p, texture %p partial stub!\n", iface, desc, data, texture);

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (!object)
    {
        ERR("Failed to allocate D3D10 texture2d object memory\n");
        return E_OUTOFMEMORY;
    }

    object->vtbl = &d3d10_texture2d_vtbl;
    object->refcount = 1;
    *texture = (ID3D10Texture2D *)object;

    TRACE("Created ID3D10Texture2D %p\n", object);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateTexture3D(ID3D10Device *iface,
        const D3D10_TEXTURE3D_DESC *desc, const D3D10_SUBRESOURCE_DATA *data, ID3D10Texture3D **texture)
{
    FIXME("iface %p, desc %p, data %p, texture %p stub!\n", iface, desc, data, texture);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateShaderResourceView(ID3D10Device *iface,
        ID3D10Resource *resource, const D3D10_SHADER_RESOURCE_VIEW_DESC *desc, ID3D10ShaderResourceView **view)
{
    FIXME("iface %p, resource %p, desc %p, view %p stub!\n", iface, resource, desc, view);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateRenderTargetView(ID3D10Device *iface,
        ID3D10Resource *resource, const D3D10_RENDER_TARGET_VIEW_DESC *desc, ID3D10RenderTargetView **view)
{
    FIXME("iface %p, resource %p, desc %p, view %p stub!\n", iface, resource, desc, view);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateDepthStencilView(ID3D10Device *iface,
        ID3D10Resource *resource, const D3D10_DEPTH_STENCIL_VIEW_DESC *desc, ID3D10DepthStencilView **view)
{
    FIXME("iface %p, resource %p, desc %p, view %p stub!\n", iface, resource, desc, view);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateInputLayout(ID3D10Device *iface,
        const D3D10_INPUT_ELEMENT_DESC *element_descs, UINT element_count, const void *shader_byte_code,
        SIZE_T shader_byte_code_length, ID3D10InputLayout **input_layout)
{
    FIXME("iface %p, element_descs %p, element_count %u, shader_byte_code %p,"
            "\tshader_byte_code_length %lu, input_layout %p stub!\n",
            iface, element_descs, element_count, shader_byte_code,
            shader_byte_code_length, input_layout);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateVertexShader(ID3D10Device *iface,
        const void *byte_code, SIZE_T byte_code_length, ID3D10VertexShader **shader)
{
    FIXME("iface %p, byte_code %p, byte_code_length %lu, shader %p stub!\n",
            iface, byte_code, byte_code_length, shader);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateGeometryShader(ID3D10Device *iface,
        const void *byte_code, SIZE_T byte_code_length, ID3D10GeometryShader **shader)
{
    FIXME("iface %p, byte_code %p, byte_code_length %lu, shader %p stub!\n",
            iface, byte_code, byte_code_length, shader);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateGeometryShaderWithStreamOutput(ID3D10Device *iface,
        const void *byte_code, SIZE_T byte_code_length, const D3D10_SO_DECLARATION_ENTRY *output_stream_decls,
        UINT output_stream_decl_count, UINT output_stream_stride, ID3D10GeometryShader **shader)
{
    FIXME("iface %p, byte_code %p, byte_code_length %lu, output_stream_decls %p,\n"
            "\toutput_stream_decl_count %u, output_stream_stride %u, shader %p stub!\n",
            iface, byte_code, byte_code_length, output_stream_decls,
            output_stream_decl_count, output_stream_stride, shader);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreatePixelShader(ID3D10Device *iface,
        const void *byte_code, SIZE_T byte_code_length, ID3D10PixelShader **shader)
{
    FIXME("iface %p, byte_code %p, byte_code_length %lu, shader %p stub!\n",
            iface, byte_code, byte_code_length, shader);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateBlendState(ID3D10Device *iface,
        const D3D10_BLEND_DESC *desc, ID3D10BlendState **blend_state)
{
    FIXME("iface %p, desc %p, blend_state %p stub!\n", iface, desc, blend_state);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateDepthStencilState(ID3D10Device *iface,
        const D3D10_DEPTH_STENCIL_DESC *desc, ID3D10DepthStencilState **depth_stencil_state)
{
    FIXME("iface %p, desc %p, depth_stencil_state %p stub!\n", iface, desc, depth_stencil_state);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateRasterizerState(ID3D10Device *iface,
        const D3D10_RASTERIZER_DESC *desc, ID3D10RasterizerState **rasterizer_state)
{
    FIXME("iface %p, desc %p, rasterizer_state %p stub!\n", iface, desc, rasterizer_state);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateSamplerState(ID3D10Device *iface,
        const D3D10_SAMPLER_DESC *desc, ID3D10SamplerState **sampler_state)
{
    FIXME("iface %p, desc %p, sampler_state %p stub!\n", iface, desc, sampler_state);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateQuery(ID3D10Device *iface,
        const D3D10_QUERY_DESC *desc, ID3D10Query **query)
{
    FIXME("iface %p, desc %p, query %p stub!\n", iface, desc, query);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreatePredicate(ID3D10Device *iface,
        const D3D10_QUERY_DESC *desc, ID3D10Predicate **predicate)
{
    FIXME("iface %p, desc %p, predicate %p stub!\n", iface, desc, predicate);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateCounter(ID3D10Device *iface,
        const D3D10_COUNTER_DESC *desc, ID3D10Counter **counter)
{
    FIXME("iface %p, desc %p, counter %p stub!\n", iface, desc, counter);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CheckFormatSupport(ID3D10Device *iface,
        DXGI_FORMAT format, UINT *format_support)
{
    FIXME("iface %p, format %s, format_support %p stub!\n",
            iface, debug_dxgi_format(format), format_support);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CheckMultisampleQualityLevels(ID3D10Device *iface,
        DXGI_FORMAT format, UINT sample_count, UINT *quality_level_count)
{
    FIXME("iface %p, format %s, sample_count %u, quality_level_count %p stub!\n",
            iface, debug_dxgi_format(format), sample_count, quality_level_count);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d3d10_device_CheckCounterInfo(ID3D10Device *iface, D3D10_COUNTER_INFO *counter_info)
{
    FIXME("iface %p, counter_info %p stub!\n", iface, counter_info);
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CheckCounter(ID3D10Device *iface,
        const D3D10_COUNTER_DESC *desc, D3D10_COUNTER_TYPE *type, UINT *active_counters, LPSTR name,
        UINT *name_length, LPSTR units, UINT *units_length, LPSTR description, UINT *description_length)
{
    FIXME("iface %p, desc %p, type %p, active_counters %p, name %p, name_length %p,\n"
            "\tunits %p, units_length %p, description %p, description_length %p stub!\n",
            iface, desc, type, active_counters, name, name_length,
            units, units_length, description, description_length);

    return E_NOTIMPL;
}

static UINT STDMETHODCALLTYPE d3d10_device_GetCreationFlags(ID3D10Device *iface)
{
    FIXME("iface %p stub!\n", iface);

    return 0;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_OpenSharedResource(ID3D10Device *iface,
        HANDLE resource_handle, REFIID guid, void **resource)
{
    FIXME("iface %p, resource_handle %p, guid %s, resource %p stub!\n",
            iface, resource_handle, debugstr_guid(guid), resource);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d3d10_device_SetTextFilterSize(ID3D10Device *iface, UINT width, UINT height)
{
    FIXME("iface %p, width %u, height %u stub!\n", iface, width, height);
}

static void STDMETHODCALLTYPE d3d10_device_GetTextFilterSize(ID3D10Device *iface, UINT *width, UINT *height)
{
    FIXME("iface %p, width %p, height %p stub!\n", iface, width, height);
}

const struct ID3D10DeviceVtbl d3d10_device_vtbl =
{
    /* IUnknown methods */
    d3d10_device_QueryInterface,
    d3d10_device_AddRef,
    d3d10_device_Release,
    /* ID3D10Device methods */
    d3d10_device_VSSetConstantBuffers,
    d3d10_device_PSSetShaderResources,
    d3d10_device_PSSetShader,
    d3d10_device_PSSetSamplers,
    d3d10_device_VSSetShader,
    d3d10_device_DrawIndexed,
    d3d10_device_Draw,
    d3d10_device_PSSetConstantBuffers,
    d3d10_device_IASetInputLayout,
    d3d10_device_IASetVertexBuffers,
    d3d10_device_IASetIndexBuffer,
    d3d10_device_DrawIndexedInstanced,
    d3d10_device_DrawInstanced,
    d3d10_device_GSSetConstantBuffers,
    d3d10_device_GSSetShader,
    d3d10_device_IASetPrimitiveTopology,
    d3d10_device_VSSetShaderResources,
    d3d10_device_VSSetSamplers,
    d3d10_device_SetPredication,
    d3d10_device_GSSetShaderResources,
    d3d10_device_GSSetSamplers,
    d3d10_device_OMSetRenderTargets,
    d3d10_device_OMSetBlendState,
    d3d10_device_OMSetDepthStencilState,
    d3d10_device_SOSetTargets,
    d3d10_device_DrawAuto,
    d3d10_device_RSSetState,
    d3d10_device_RSSetViewports,
    d3d10_device_RSSetScissorRects,
    d3d10_device_CopySubresourceRegion,
    d3d10_device_CopyResource,
    d3d10_device_UpdateSubresource,
    d3d10_device_ClearRenderTargetView,
    d3d10_device_ClearDepthStencilView,
    d3d10_device_GenerateMips,
    d3d10_device_ResolveSubresource,
    d3d10_device_VSGetConstantBuffers,
    d3d10_device_PSGetShaderResources,
    d3d10_device_PSGetShader,
    d3d10_device_PSGetSamplers,
    d3d10_device_VSGetShader,
    d3d10_device_PSGetConstantBuffers,
    d3d10_device_IAGetInputLayout,
    d3d10_device_IAGetVertexBuffers,
    d3d10_device_IAGetIndexBuffer,
    d3d10_device_GSGetConstantBuffers,
    d3d10_device_GSGetShader,
    d3d10_device_IAGetPrimitiveTopology,
    d3d10_device_VSGetShaderResources,
    d3d10_device_VSGetSamplers,
    d3d10_device_GetPredication,
    d3d10_device_GSGetShaderResources,
    d3d10_device_GSGetSamplers,
    d3d10_device_OMGetRenderTargets,
    d3d10_device_OMGetBlendState,
    d3d10_device_OMGetDepthStencilState,
    d3d10_device_SOGetTargets,
    d3d10_device_RSGetState,
    d3d10_device_RSGetViewports,
    d3d10_device_RSGetScissorRects,
    d3d10_device_GetDeviceRemovedReason,
    d3d10_device_SetExceptionMode,
    d3d10_device_GetExceptionMode,
    d3d10_device_GetPrivateData,
    d3d10_device_SetPrivateData,
    d3d10_device_SetPrivateDataInterface,
    d3d10_device_ClearState,
    d3d10_device_Flush,
    d3d10_device_CreateBuffer,
    d3d10_device_CreateTexture1D,
    d3d10_device_CreateTexture2D,
    d3d10_device_CreateTexture3D,
    d3d10_device_CreateShaderResourceView,
    d3d10_device_CreateRenderTargetView,
    d3d10_device_CreateDepthStencilView,
    d3d10_device_CreateInputLayout,
    d3d10_device_CreateVertexShader,
    d3d10_device_CreateGeometryShader,
    d3d10_device_CreateGeometryShaderWithStreamOutput,
    d3d10_device_CreatePixelShader,
    d3d10_device_CreateBlendState,
    d3d10_device_CreateDepthStencilState,
    d3d10_device_CreateRasterizerState,
    d3d10_device_CreateSamplerState,
    d3d10_device_CreateQuery,
    d3d10_device_CreatePredicate,
    d3d10_device_CreateCounter,
    d3d10_device_CheckFormatSupport,
    d3d10_device_CheckMultisampleQualityLevels,
    d3d10_device_CheckCounterInfo,
    d3d10_device_CheckCounter,
    d3d10_device_GetCreationFlags,
    d3d10_device_OpenSharedResource,
    d3d10_device_SetTextFilterSize,
    d3d10_device_GetTextFilterSize,
};

const struct IUnknownVtbl d3d10_device_inner_unknown_vtbl =
{
    /* IUnknown methods */
    d3d10_device_inner_QueryInterface,
    d3d10_device_inner_AddRef,
    d3d10_device_inner_Release,
};

/* IWineD3DDeviceParent IUnknown methods */

static inline struct d3d10_device *device_from_device_parent(IWineD3DDeviceParent *iface)
{
    return (struct d3d10_device *)((char*)iface - FIELD_OFFSET(struct d3d10_device, device_parent_vtbl));
}

HRESULT STDMETHODCALLTYPE device_parent_QueryInterface(IWineD3DDeviceParent *iface, REFIID riid, void **object)
{
    struct d3d10_device *This = device_from_device_parent(iface);
    return d3d10_device_QueryInterface((ID3D10Device *)This, riid, object);
}

ULONG STDMETHODCALLTYPE device_parent_AddRef(IWineD3DDeviceParent *iface)
{
    struct d3d10_device *This = device_from_device_parent(iface);
    return d3d10_device_AddRef((ID3D10Device *)This);
}

ULONG STDMETHODCALLTYPE device_parent_Release(IWineD3DDeviceParent *iface)
{
    struct d3d10_device *This = device_from_device_parent(iface);
    return d3d10_device_Release((ID3D10Device *)This);
}

/* IWineD3DDeviceParent methods */

static HRESULT STDMETHODCALLTYPE device_parent_CreateSurface(IWineD3DDeviceParent *iface,
        IUnknown *superior, UINT width, UINT height, WINED3DFORMAT format, DWORD usage,
        WINED3DPOOL pool, UINT level, WINED3DCUBEMAP_FACES face, IWineD3DSurface **surface)
{
    FIXME("iface %p, superior %p, width %u, height %u, format %#x, usage %#x,\n"
            "\tpool %#x, level %u, face %u, surface %p stub!\n",
            iface, superior, width, height, format, usage, pool, level, face, surface);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE device_parent_CreateRenderTarget(IWineD3DDeviceParent *iface,
        IUnknown *superior, UINT width, UINT height, WINED3DFORMAT format, WINED3DMULTISAMPLE_TYPE multisample_type,
        DWORD multisample_quality, BOOL lockable, IWineD3DSurface **surface)
{
    FIXME("iface %p, superior %p, width %u, height %u, format %#x, multisample_type %#x,\n"
            "\tmultisample_quality %u, lockable %u, surface %p stub!\n",
            iface, superior, width, height, format, multisample_type, multisample_quality, lockable, surface);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE device_parent_CreateDepthStencilSurface(IWineD3DDeviceParent *iface,
        IUnknown *superior, UINT width, UINT height, WINED3DFORMAT format, WINED3DMULTISAMPLE_TYPE multisample_type,
        DWORD multisample_quality, BOOL discard, IWineD3DSurface **surface)
{
    FIXME("iface %p, superior %p, width %u, height %u, format %#x, multisample_type %#x,\n"
            "\tmultisample_quality %u, discard %u, surface %p stub!\n",
            iface, superior, width, height, format, multisample_type, multisample_quality, discard, surface);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE device_parent_CreateVolume(IWineD3DDeviceParent *iface,
        IUnknown *superior, UINT width, UINT height, UINT depth, WINED3DFORMAT format,
        WINED3DPOOL pool, DWORD usage, IWineD3DVolume **volume)
{
    FIXME("iface %p, superior %p, width %u, height %u, depth %u, format %#x, pool %#x, usage %#x, volume %p stub!\n",
            iface, superior, width, height, depth, format, pool, usage, volume);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE device_parent_CreateSwapChain(IWineD3DDeviceParent *iface,
        WINED3DPRESENT_PARAMETERS *present_parameters, IWineD3DSwapChain **swapchain)
{
    FIXME("iface %p, present_parameters %p, swapchain %p stub!\n", iface, present_parameters, swapchain);

    return E_NOTIMPL;
}


const struct IWineD3DDeviceParentVtbl d3d10_wined3d_device_parent_vtbl =
{
    /* IUnknown methods */
    device_parent_QueryInterface,
    device_parent_AddRef,
    device_parent_Release,
    /* IWineD3DDeviceParent methods */
    device_parent_CreateSurface,
    device_parent_CreateRenderTarget,
    device_parent_CreateDepthStencilSurface,
    device_parent_CreateVolume,
    device_parent_CreateSwapChain,
};
