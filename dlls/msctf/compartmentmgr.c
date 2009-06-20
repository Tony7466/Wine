/*
 *  ITfCompartmentMgr implementation
 *
 *  Copyright 2009 Aric Stewart, CodeWeavers
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

#include <stdarg.h>

#define COBJMACROS

#include "wine/debug.h"
#include "windef.h"
#include "winbase.h"
#include "winreg.h"
#include "winuser.h"
#include "shlwapi.h"
#include "winerror.h"
#include "objbase.h"
#include "oleauto.h"
#include "olectl.h"

#include "wine/unicode.h"
#include "wine/list.h"

#include "msctf.h"
#include "msctf_internal.h"

WINE_DEFAULT_DEBUG_CHANNEL(msctf);

typedef struct tagCompartmentValue {
    struct list entry;
    GUID guid;
    TfClientId owner;
    ITfCompartment *compartment;
} CompartmentValue;

typedef struct tagCompartmentMgr {
    const ITfCompartmentMgrVtbl *CompartmentMgrVtbl;
    LONG refCount;

    IUnknown *pUnkOuter;

    struct list values;
} CompartmentMgr;

typedef struct tagCompartmentEnumGuid {
    const IEnumGUIDVtbl *Vtbl;
    LONG refCount;

    struct list *values;
    struct list *cursor;
} CompartmentEnumGuid;

static HRESULT CompartmentEnumGuid_Constructor(struct list* values, IEnumGUID **ppOut);

HRESULT CompartmentMgr_Destructor(ITfCompartmentMgr *iface)
{
    CompartmentMgr *This = (CompartmentMgr *)iface;
    struct list *cursor, *cursor2;

    LIST_FOR_EACH_SAFE(cursor, cursor2, &This->values)
    {
        CompartmentValue* value = LIST_ENTRY(cursor,CompartmentValue,entry);
        list_remove(cursor);
        ITfCompartment_Release(value->compartment);
        HeapFree(GetProcessHeap(),0,value);
    }

    HeapFree(GetProcessHeap(),0,This);
    return S_OK;
}

/*****************************************************
 * ITfCompartmentMgr functions
 *****************************************************/
static HRESULT WINAPI CompartmentMgr_QueryInterface(ITfCompartmentMgr *iface, REFIID iid, LPVOID *ppvOut)
{
    CompartmentMgr *This = (CompartmentMgr *)iface;
    if (This->pUnkOuter)
        return IUnknown_QueryInterface(This->pUnkOuter, iid, *ppvOut);
    else
    {
        *ppvOut = NULL;

        if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_ITfCompartmentMgr))
        {
            *ppvOut = This;
        }

        if (*ppvOut)
        {
            IUnknown_AddRef(iface);
            return S_OK;
        }

        WARN("unsupported interface: %s\n", debugstr_guid(iid));
        return E_NOINTERFACE;
    }
}

static ULONG WINAPI CompartmentMgr_AddRef(ITfCompartmentMgr *iface)
{
    CompartmentMgr *This = (CompartmentMgr *)iface;
    if (This->pUnkOuter)
        return IUnknown_AddRef(This->pUnkOuter);
    else
        return InterlockedIncrement(&This->refCount);
}

static ULONG WINAPI CompartmentMgr_Release(ITfCompartmentMgr *iface)
{
    CompartmentMgr *This = (CompartmentMgr *)iface;
    if (This->pUnkOuter)
        return IUnknown_Release(This->pUnkOuter);
    else
    {
        ULONG ret;

        ret = InterlockedDecrement(&This->refCount);
        if (ret == 0)
            CompartmentMgr_Destructor(iface);
        return ret;
    }
}

static HRESULT WINAPI CompartmentMgr_GetCompartment(ITfCompartmentMgr *iface,
        REFGUID rguid, ITfCompartment **ppcomp)
{
    CompartmentMgr *This = (CompartmentMgr *)iface;
    FIXME("STUB:(%p)\n",This);
    return E_NOTIMPL;
}

static HRESULT WINAPI CompartmentMgr_ClearCompartment(ITfCompartmentMgr *iface,
    TfClientId tid, REFGUID rguid)
{
    struct list *cursor;
    CompartmentMgr *This = (CompartmentMgr *)iface;
    TRACE("(%p) %i %s\n",This,tid,debugstr_guid(rguid));

    LIST_FOR_EACH(cursor, &This->values)
    {
        CompartmentValue* value = LIST_ENTRY(cursor,CompartmentValue,entry);
        if (IsEqualGUID(rguid,&value->guid))
        {
            if (value->owner && tid != value->owner)
                return E_UNEXPECTED;
            list_remove(cursor);
            ITfCompartment_Release(value->compartment);
            HeapFree(GetProcessHeap(),0,value);
            return S_OK;
        }
    }

    return CONNECT_E_NOCONNECTION;
}

static HRESULT WINAPI CompartmentMgr_EnumCompartments(ITfCompartmentMgr *iface,
 IEnumGUID **ppEnum)
{
    CompartmentMgr *This = (CompartmentMgr *)iface;
    TRACE("(%p) %p\n",This,ppEnum);
    if (!ppEnum)
        return E_INVALIDARG;
    return CompartmentEnumGuid_Constructor(&This->values, ppEnum);
}

static const ITfCompartmentMgrVtbl CompartmentMgr_CompartmentMgrVtbl =
{
    CompartmentMgr_QueryInterface,
    CompartmentMgr_AddRef,
    CompartmentMgr_Release,

    CompartmentMgr_GetCompartment,
    CompartmentMgr_ClearCompartment,
    CompartmentMgr_EnumCompartments
};

HRESULT CompartmentMgr_Constructor(IUnknown *pUnkOuter, REFIID riid, IUnknown **ppOut)
{
    CompartmentMgr *This;

    if (!ppOut)
        return E_POINTER;

    if (pUnkOuter && !IsEqualIID (riid, &IID_IUnknown))
        return CLASS_E_NOAGGREGATION;

    This = HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(CompartmentMgr));
    if (This == NULL)
        return E_OUTOFMEMORY;

    This->CompartmentMgrVtbl = &CompartmentMgr_CompartmentMgrVtbl;
    This->pUnkOuter = pUnkOuter;
    list_init(&This->values);

    if (pUnkOuter)
    {
        TRACE("returning %p\n", This);
        *ppOut = (IUnknown*)This;
        return S_OK;
    }
    else
    {
        HRESULT hr;
        hr = IUnknown_QueryInterface((IUnknown*)This, riid, (LPVOID*)ppOut);
        if (FAILED(hr))
            HeapFree(GetProcessHeap(),0,This);
        return hr;
    }
}

/**************************************************
 * IEnumGUID implementaion for ITfCompartmentMgr::EnumCompartments
 **************************************************/
static void CompartmentEnumGuid_Destructor(CompartmentEnumGuid *This)
{
    TRACE("destroying %p\n", This);
    HeapFree(GetProcessHeap(),0,This);
}

static HRESULT WINAPI CompartmentEnumGuid_QueryInterface(IEnumGUID *iface, REFIID iid, LPVOID *ppvOut)
{
    CompartmentEnumGuid *This = (CompartmentEnumGuid *)iface;
    *ppvOut = NULL;

    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IEnumGUID))
    {
        *ppvOut = This;
    }

    if (*ppvOut)
    {
        IUnknown_AddRef(iface);
        return S_OK;
    }

    WARN("unsupported interface: %s\n", debugstr_guid(iid));
    return E_NOINTERFACE;
}

static ULONG WINAPI CompartmentEnumGuid_AddRef(IEnumGUID *iface)
{
    CompartmentEnumGuid *This = (CompartmentEnumGuid*)iface;
    return InterlockedIncrement(&This->refCount);
}

static ULONG WINAPI CompartmentEnumGuid_Release(IEnumGUID *iface)
{
    CompartmentEnumGuid *This = (CompartmentEnumGuid *)iface;
    ULONG ret;

    ret = InterlockedDecrement(&This->refCount);
    if (ret == 0)
        CompartmentEnumGuid_Destructor(This);
    return ret;
}

/*****************************************************
 * IEnumGuid functions
 *****************************************************/
static HRESULT WINAPI CompartmentEnumGuid_Next( LPENUMGUID iface,
    ULONG celt, GUID *rgelt, ULONG *pceltFetched)
{
    CompartmentEnumGuid *This = (CompartmentEnumGuid *)iface;
    ULONG fetched = 0;

    TRACE("(%p)\n",This);

    if (rgelt == NULL) return E_POINTER;

    while (fetched < celt && This->cursor)
    {
        CompartmentValue* value = LIST_ENTRY(This->cursor,CompartmentValue,entry);
        if (!value)
            break;

        This->cursor = list_next(This->values,This->cursor);
        *rgelt = value->guid;

        ++fetched;
        ++rgelt;
    }

    if (pceltFetched) *pceltFetched = fetched;
    return fetched == celt ? S_OK : S_FALSE;
}

static HRESULT WINAPI CompartmentEnumGuid_Skip( LPENUMGUID iface, ULONG celt)
{
    CompartmentEnumGuid *This = (CompartmentEnumGuid *)iface;
    TRACE("(%p)\n",This);

    This->cursor = list_next(This->values,This->cursor);
    return S_OK;
}

static HRESULT WINAPI CompartmentEnumGuid_Reset( LPENUMGUID iface)
{
    CompartmentEnumGuid *This = (CompartmentEnumGuid *)iface;
    TRACE("(%p)\n",This);
    This->cursor = list_head(This->values);
    return S_OK;
}

static HRESULT WINAPI CompartmentEnumGuid_Clone( LPENUMGUID iface,
    IEnumGUID **ppenum)
{
    CompartmentEnumGuid *This = (CompartmentEnumGuid *)iface;
    HRESULT res;

    TRACE("(%p)\n",This);

    if (ppenum == NULL) return E_POINTER;

    res = CompartmentEnumGuid_Constructor(This->values, ppenum);
    if (SUCCEEDED(res))
    {
        CompartmentEnumGuid *new_This = (CompartmentEnumGuid *)*ppenum;
        new_This->cursor = This->cursor;
    }
    return res;
}

static const IEnumGUIDVtbl IEnumGUID_Vtbl ={
    CompartmentEnumGuid_QueryInterface,
    CompartmentEnumGuid_AddRef,
    CompartmentEnumGuid_Release,

    CompartmentEnumGuid_Next,
    CompartmentEnumGuid_Skip,
    CompartmentEnumGuid_Reset,
    CompartmentEnumGuid_Clone
};

static HRESULT CompartmentEnumGuid_Constructor(struct list *values, IEnumGUID **ppOut)
{
    CompartmentEnumGuid *This;

    This = HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(CompartmentEnumGuid));
    if (This == NULL)
        return E_OUTOFMEMORY;

    This->Vtbl= &IEnumGUID_Vtbl;
    This->refCount = 1;

    This->values = values;
    This->cursor = list_head(values);

    TRACE("returning %p\n", This);
    *ppOut = (IEnumGUID*)This;
    return S_OK;
}
