/*
 * Generic Implementation of strmbase Base Renderer classes
 *
 * Copyright 2012 Aric Stewart, CodeWeavers
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

#include "dshow.h"
#include "wine/debug.h"
#include "wine/unicode.h"
#include "wine/strmbase.h"
#include "uuids.h"
#include "vfwmsgs.h"

WINE_DEFAULT_DEBUG_CHANNEL(strmbase);

static const WCHAR wcsInputPinName[] = {'i','n','p','u','t',' ','p','i','n',0};
static const WCHAR wcsAltInputPinName[] = {'I','n',0};

static inline BaseInputPin *impl_BaseInputPin_from_IPin( IPin *iface )
{
    return CONTAINING_RECORD(iface, BaseInputPin, pin.IPin_iface);
}

static inline BaseRenderer *impl_from_IBaseFilter(IBaseFilter *iface)
{
    return CONTAINING_RECORD(iface, BaseRenderer, filter.IBaseFilter_iface);
}

static inline BaseRenderer *impl_from_BaseFilter(BaseFilter *iface)
{
    return CONTAINING_RECORD(iface, BaseRenderer, filter);
}

static HRESULT WINAPI BaseRenderer_InputPin_EndOfStream(IPin * iface)
{
    HRESULT hr = S_OK;
    BaseInputPin* This = impl_BaseInputPin_from_IPin(iface);
    BaseRenderer *pFilter = impl_from_IBaseFilter(This->pin.pinInfo.pFilter);

    TRACE("(%p/%p)->()\n", This, pFilter);

    EnterCriticalSection(&pFilter->filter.csFilter);
    EnterCriticalSection(&pFilter->csRenderLock);
    hr = BaseInputPinImpl_EndOfStream(iface);
    EnterCriticalSection(This->pin.pCritSec);
    if (SUCCEEDED(hr))
    {
        if (pFilter->pFuncsTable->pfnEndOfStream)
            hr = pFilter->pFuncsTable->pfnEndOfStream(pFilter);
        else
            hr = BaseRendererImpl_EndOfStream(pFilter);
    }
    LeaveCriticalSection(This->pin.pCritSec);
    LeaveCriticalSection(&pFilter->csRenderLock);
    LeaveCriticalSection(&pFilter->filter.csFilter);
    return hr;
}

static HRESULT WINAPI BaseRenderer_InputPin_EndFlush(IPin * iface)
{
    BaseInputPin* This = impl_BaseInputPin_from_IPin(iface);
    BaseRenderer *pFilter = impl_from_IBaseFilter(This->pin.pinInfo.pFilter);
    HRESULT hr = S_OK;

    TRACE("(%p/%p)->()\n", This, pFilter);

    EnterCriticalSection(&pFilter->filter.csFilter);
    EnterCriticalSection(&pFilter->csRenderLock);
    EnterCriticalSection(This->pin.pCritSec);
    hr = BaseInputPinImpl_EndFlush(iface);
    if (SUCCEEDED(hr))
    {
        if (pFilter->pFuncsTable->pfnEndFlush)
            hr = pFilter->pFuncsTable->pfnEndFlush(pFilter);
        else
            hr = BaseRendererImpl_EndFlush(pFilter);
    }
    LeaveCriticalSection(This->pin.pCritSec);
    LeaveCriticalSection(&pFilter->csRenderLock);
    LeaveCriticalSection(&pFilter->filter.csFilter);
    return hr;
}

static const IPinVtbl BaseRenderer_InputPin_Vtbl =
{
    BaseInputPinImpl_QueryInterface,
    BasePinImpl_AddRef,
    BaseInputPinImpl_Release,
    BaseInputPinImpl_Connect,
    BaseInputPinImpl_ReceiveConnection,
    BasePinImpl_Disconnect,
    BasePinImpl_ConnectedTo,
    BasePinImpl_ConnectionMediaType,
    BasePinImpl_QueryPinInfo,
    BasePinImpl_QueryDirection,
    BasePinImpl_QueryId,
    BaseInputPinImpl_QueryAccept,
    BasePinImpl_EnumMediaTypes,
    BasePinImpl_QueryInternalConnections,
    BaseRenderer_InputPin_EndOfStream,
    BaseInputPinImpl_BeginFlush,
    BaseRenderer_InputPin_EndFlush,
    BaseInputPinImpl_NewSegment
};

static IPin* WINAPI BaseRenderer_GetPin(BaseFilter *iface, int pos)
{
    BaseRenderer *This = impl_from_BaseFilter(iface);

    if (pos >= 1 || pos < 0)
        return NULL;

    IPin_AddRef(&This->pInputPin->pin.IPin_iface);
    return &This->pInputPin->pin.IPin_iface;
}

static LONG WINAPI BaseRenderer_GetPinCount(BaseFilter *iface)
{
    return 1;
}

static HRESULT WINAPI BaseRenderer_Input_CheckMediaType(BasePin *pin, const AM_MEDIA_TYPE * pmt)
{
    BaseRenderer *This = impl_from_IBaseFilter(pin->pinInfo.pFilter);
    return This->pFuncsTable->pfnCheckMediaType(This, pmt);
}

static HRESULT WINAPI BaseRenderer_Receive(BaseInputPin *pin, IMediaSample * pSample)
{
    BaseRenderer *This = impl_from_IBaseFilter(pin->pin.pinInfo.pFilter);
    return BaseRendererImpl_Receive(This, pSample);
}

static const BaseFilterFuncTable RendererBaseFilterFuncTable = {
    BaseRenderer_GetPin,
    BaseRenderer_GetPinCount
};

static const  BasePinFuncTable input_BaseFuncTable = {
    BaseRenderer_Input_CheckMediaType,
    NULL,
    BasePinImpl_GetMediaTypeVersion,
    BasePinImpl_GetMediaType
};

static const BaseInputPinFuncTable input_BaseInputFuncTable = {
   BaseRenderer_Receive
};


HRESULT WINAPI BaseRenderer_Init(BaseRenderer * This, const IBaseFilterVtbl *Vtbl, IUnknown *pUnkOuter, const CLSID *pClsid, DWORD_PTR DebugInfo, const BaseRendererFuncTable* pBaseFuncsTable)
{
    PIN_INFO piInput;
    HRESULT hr;

    BaseFilter_Init(&This->filter, Vtbl, pClsid, DebugInfo, &RendererBaseFilterFuncTable);

    This->pFuncsTable = pBaseFuncsTable;

    /* construct input pin */
    piInput.dir = PINDIR_INPUT;
    piInput.pFilter = &This->filter.IBaseFilter_iface;
    lstrcpynW(piInput.achName, wcsInputPinName, sizeof(piInput.achName) / sizeof(piInput.achName[0]));

    hr = BaseInputPin_Construct(&BaseRenderer_InputPin_Vtbl, &piInput, &input_BaseFuncTable, &input_BaseInputFuncTable, &This->filter.csFilter, NULL, (IPin **)&This->pInputPin);

    if (SUCCEEDED(hr))
    {
        hr = CreatePosPassThru(pUnkOuter ? pUnkOuter: (IUnknown*)This, TRUE, &This->pInputPin->pin.IPin_iface, &This->pPosition);
        if (FAILED(hr))
            return hr;

        InitializeCriticalSection(&This->csRenderLock);
        This->csRenderLock.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__": BaseRenderer.csRenderLock");
    }

    return hr;
}

HRESULT WINAPI BaseRendererImpl_QueryInterface(IBaseFilter* iface, REFIID riid, LPVOID * ppv)
{
    BaseRenderer *This = impl_from_IBaseFilter(iface);

    if (IsEqualIID(riid, &IID_IMediaSeeking))
        return IUnknown_QueryInterface(This->pPosition, riid, ppv);
    else
        return BaseFilterImpl_QueryInterface(iface, riid, ppv);
}

ULONG WINAPI BaseRendererImpl_Release(IBaseFilter* iface)
{
    BaseRenderer *This = impl_from_IBaseFilter(iface);
    ULONG refCount = BaseFilterImpl_Release(iface);

    if (!refCount)
    {
        IPin *pConnectedTo;

        if (SUCCEEDED(IPin_ConnectedTo(&This->pInputPin->pin.IPin_iface, &pConnectedTo)))
        {
            IPin_Disconnect(pConnectedTo);
            IPin_Release(pConnectedTo);
        }
        IPin_Disconnect(&This->pInputPin->pin.IPin_iface);
        IPin_Release(&This->pInputPin->pin.IPin_iface);

        if (This->pPosition)
            IUnknown_Release(This->pPosition);

        This->csRenderLock.DebugInfo->Spare[0] = 0;
        DeleteCriticalSection(&This->csRenderLock);
    }
    return refCount;
}

HRESULT WINAPI BaseRendererImpl_Receive(BaseRenderer *This, IMediaSample * pSample)
{
    HRESULT hr = S_OK;
    REFERENCE_TIME start, stop;

    TRACE("(%p)->%p\n", This, pSample);

    if (This->pFuncsTable->pfnPrepareReceive)
        hr = This->pFuncsTable->pfnPrepareReceive(This, pSample);
    if (FAILED(hr))
    {
        if (hr == VFW_E_SAMPLE_REJECTED)
            return S_OK;
        else
            return hr;
    }

    if (This->pFuncsTable->pfnPrepareRender)
        This->pFuncsTable->pfnPrepareRender(This);

    EnterCriticalSection(&This->csRenderLock);
    if ( This->filter.state == State_Paused )
    {
        if (This->pFuncsTable->pfnOnReceiveFirstSample)
            This->pFuncsTable->pfnOnReceiveFirstSample(This, pSample);
    }

    /* Wait for render Time */
    if (SUCCEEDED(IMediaSample_GetMediaTime(pSample, &start, &stop)))
    {
        hr = S_FALSE;
        RendererPosPassThru_RegisterMediaTime(This->pPosition, start);
        if (This->pFuncsTable->pfnShouldDrawSampleNow)
            hr = This->pFuncsTable->pfnShouldDrawSampleNow(This, pSample, &start, &stop);

        if (hr == S_OK)
            ;/* Do not wait: drop through */
        else if (hr == S_FALSE)
        {
            if (This->pFuncsTable->pfnOnWaitStart)
                This->pFuncsTable->pfnOnWaitStart(This);
            /* TODO: Wait */
            if (This->pFuncsTable->pfnOnWaitEnd)
                This->pFuncsTable->pfnOnWaitEnd(This);
        }
        else
        {
            LeaveCriticalSection(&This->csRenderLock);
            /* Drop Sample */
            return S_OK;
        }
    }
    if (This->pInputPin->flushing || This->pInputPin->end_of_stream)
        hr = S_FALSE;

    if (SUCCEEDED(hr))
        hr = This->pFuncsTable->pfnDoRenderSample(This, pSample);
    LeaveCriticalSection(&This->csRenderLock);

    return hr;
}

HRESULT WINAPI BaseRendererImpl_FindPin(IBaseFilter * iface, LPCWSTR Id, IPin **ppPin)
{
    BaseRenderer *This = impl_from_IBaseFilter(iface);

    TRACE("(%p)->(%p,%p)\n", This, debugstr_w(Id), ppPin);

    if (!Id || !ppPin)
        return E_POINTER;

    if (!lstrcmpiW(Id,wcsInputPinName) || !lstrcmpiW(Id,wcsAltInputPinName))
    {
        *ppPin = &This->pInputPin->pin.IPin_iface;
        IPin_AddRef(*ppPin);
        return S_OK;
    }
    *ppPin = NULL;
    return VFW_E_NOT_FOUND;
}

HRESULT WINAPI BaseRendererImpl_Stop(IBaseFilter * iface)
{
    BaseRenderer *This = impl_from_IBaseFilter(iface);

    TRACE("(%p)->()\n", This);

    EnterCriticalSection(&This->csRenderLock);
    {
        RendererPosPassThru_ResetMediaTime(This->pPosition);
        if (This->pFuncsTable->pfnOnStopStreaming)
            This->pFuncsTable->pfnOnStopStreaming(This);
        This->filter.state = State_Stopped;
    }
    LeaveCriticalSection(&This->csRenderLock);

    return S_OK;
}

HRESULT WINAPI BaseRendererImpl_Run(IBaseFilter * iface, REFERENCE_TIME tStart)
{
    HRESULT hr = S_OK;
    BaseRenderer *This = impl_from_IBaseFilter(iface);
    TRACE("(%p)->(%s)\n", This, wine_dbgstr_longlong(tStart));

    EnterCriticalSection(&This->csRenderLock);
    This->filter.rtStreamStart = tStart;
    if (This->filter.state == State_Running)
        goto out;
    if (This->pInputPin->pin.pConnectedTo)
    {
        This->pInputPin->end_of_stream = 0;
    }
    else if (This->filter.filterInfo.pGraph)
    {
        IMediaEventSink *pEventSink;
        hr = IFilterGraph_QueryInterface(This->filter.filterInfo.pGraph, &IID_IMediaEventSink, (LPVOID*)&pEventSink);
        if (SUCCEEDED(hr))
        {
            hr = IMediaEventSink_Notify(pEventSink, EC_COMPLETE, S_OK, (LONG_PTR)This);
            IMediaEventSink_Release(pEventSink);
        }
        hr = S_OK;
    }
    if (SUCCEEDED(hr))
    {
        if (This->pFuncsTable->pfnOnStartStreaming)
            This->pFuncsTable->pfnOnStartStreaming(This);
        This->filter.state = State_Running;
    }
out:
    LeaveCriticalSection(&This->csRenderLock);

    return hr;
}

HRESULT WINAPI BaseRendererImpl_Pause(IBaseFilter * iface)
{
    BaseRenderer *This = impl_from_IBaseFilter(iface);

    TRACE("(%p)->()\n", This);

    EnterCriticalSection(&This->csRenderLock);
    {
     if (This->filter.state != State_Paused)
        {
            if (This->filter.state == State_Stopped)
                This->pInputPin->end_of_stream = 0;
            This->filter.state = State_Paused;
        }
    }
    LeaveCriticalSection(&This->csRenderLock);

    return S_OK;
}

HRESULT WINAPI BaseRendererImpl_EndOfStream(BaseRenderer* iface)
{
    IMediaEventSink* pEventSink;
    IFilterGraph *graph;
    HRESULT hr = S_OK;

    TRACE("(%p)\n", iface);

    graph = iface->filter.filterInfo.pGraph;
    if (graph)
    {        hr = IFilterGraph_QueryInterface(iface->filter.filterInfo.pGraph, &IID_IMediaEventSink, (LPVOID*)&pEventSink);
        if (SUCCEEDED(hr))
        {
            hr = IMediaEventSink_Notify(pEventSink, EC_COMPLETE, S_OK, (LONG_PTR)iface);
            IMediaEventSink_Release(pEventSink);
        }
    }
    RendererPosPassThru_EOS(iface->pPosition);

    return hr;
}

HRESULT WINAPI BaseRendererImpl_EndFlush(BaseRenderer* iface)
{
    TRACE("(%p)\n", iface);
    RendererPosPassThru_ResetMediaTime(iface->pPosition);
    return S_OK;
}
