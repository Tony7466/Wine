/*
 * MIME OLE Interfaces
 *
 * Copyright 2006 Robert Shearman for CodeWeavers
 * Copyright 2007 Huw Davies for CodeWeavers
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
#define NONAMELESSUNION

#include <stdarg.h>
#include <stdio.h>

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "objbase.h"
#include "ole2.h"
#include "mimeole.h"

#include "wine/list.h"
#include "wine/debug.h"

#include "inetcomm_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(inetcomm);

typedef struct
{
    LPCSTR     name;
    DWORD      id;
    DWORD      flags; /* MIMEPROPFLAGS */
    VARTYPE    default_vt;
} property_t;

typedef struct
{
    struct list entry;
    property_t prop;
} property_list_entry_t;

static const property_t default_props[] =
{
    {"References",                   PID_HDR_REFS,       0,                               VT_LPSTR},
    {"Subject",                      PID_HDR_SUBJECT,    0,                               VT_LPSTR},
    {"From",                         PID_HDR_FROM,       MPF_ADDRESS,                     VT_LPSTR},
    {"Message-ID",                   PID_HDR_MESSAGEID,  0,                               VT_LPSTR},
    {"Return-Path",                  PID_HDR_RETURNPATH, MPF_ADDRESS,                     VT_LPSTR},
    {"Date",                         PID_HDR_DATE,       0,                               VT_LPSTR},
    {"Received",                     PID_HDR_RECEIVED,   0,                               VT_LPSTR},
    {"Reply-To",                     PID_HDR_REPLYTO,    MPF_ADDRESS,                     VT_LPSTR},
    {"X-Mailer",                     PID_HDR_XMAILER,    0,                               VT_LPSTR},
    {"Bcc",                          PID_HDR_BCC,        MPF_ADDRESS,                     VT_LPSTR},
    {"MIME-Version",                 PID_HDR_MIMEVER,    MPF_MIME,                        VT_LPSTR},
    {"Content-Type",                 PID_HDR_CNTTYPE,    MPF_MIME | MPF_HASPARAMS,        VT_LPSTR},
    {"Content-Transfer-Encoding",    PID_HDR_CNTXFER,    MPF_MIME,                        VT_LPSTR},
    {"Content-ID",                   PID_HDR_CNTID,      MPF_MIME,                        VT_LPSTR},
    {"Content-Disposition",          PID_HDR_CNTDISP,    MPF_MIME,                        VT_LPSTR},
    {"To",                           PID_HDR_TO,         MPF_ADDRESS,                     VT_LPSTR},
    {"Cc",                           PID_HDR_CC,         MPF_ADDRESS,                     VT_LPSTR},
    {"Sender",                       PID_HDR_SENDER,     MPF_ADDRESS,                     VT_LPSTR},
    {"In-Reply-To",                  PID_HDR_INREPLYTO,  0,                               VT_LPSTR},
    {NULL,                           0,                  0,                               0}
};

typedef struct
{
    struct list entry;
    char *name;
    char *value;
} param_t;

typedef struct
{
    struct list entry;
    const property_t *prop;
    PROPVARIANT value;
    struct list params;
} header_t;

typedef struct MimeBody
{
    const IMimeBodyVtbl *lpVtbl;
    LONG refs;

    HBODY handle;

    struct list headers;
    struct list new_props; /* FIXME: This should be in a PropertySchema */
    DWORD next_prop_id;
    char *content_pri_type;
    char *content_sub_type;
    ENCODINGTYPE encoding;
    void *data;
    IID data_iid;
    BODYOFFSETS body_offsets;
} MimeBody;

static inline MimeBody *impl_from_IMimeBody( IMimeBody *iface )
{
    return (MimeBody *)((char*)iface - FIELD_OFFSET(MimeBody, lpVtbl));
}

static LPSTR strdupA(LPCSTR str)
{
    char *ret;
    int len = strlen(str);
    ret = HeapAlloc(GetProcessHeap(), 0, len + 1);
    memcpy(ret, str, len + 1);
    return ret;
}

#define PARSER_BUF_SIZE 1024

/*****************************************************
 *        copy_headers_to_buf [internal]
 *
 * Copies the headers into a '\0' terminated memory block and leave
 * the stream's current position set to after the blank line.
 */
static HRESULT copy_headers_to_buf(IStream *stm, char **ptr)
{
    char *buf = NULL;
    DWORD size = PARSER_BUF_SIZE, offset = 0, last_end = 0;
    HRESULT hr;
    int done = 0;

    *ptr = NULL;

    do
    {
        char *end;
        DWORD read;

        if(!buf)
            buf = HeapAlloc(GetProcessHeap(), 0, size + 1);
        else
        {
            size *= 2;
            buf = HeapReAlloc(GetProcessHeap(), 0, buf, size + 1);
        }
        if(!buf)
        {
            hr = E_OUTOFMEMORY;
            goto fail;
        }

        hr = IStream_Read(stm, buf + offset, size - offset, &read);
        if(FAILED(hr)) goto fail;

        offset += read;
        buf[offset] = '\0';

        if(read == 0) done = 1;

        while(!done && (end = strstr(buf + last_end, "\r\n")))
        {
            DWORD new_end = end - buf + 2;
            if(new_end - last_end == 2)
            {
                LARGE_INTEGER off;
                off.QuadPart = new_end;
                IStream_Seek(stm, off, STREAM_SEEK_SET, NULL);
                buf[new_end] = '\0';
                done = 1;
            }
            else
                last_end = new_end;
        }
    } while(!done);

    *ptr = buf;
    return S_OK;

fail:
    HeapFree(GetProcessHeap(), 0, buf);
    return hr;
}

static header_t *read_prop(MimeBody *body, char **ptr)
{
    char *colon = strchr(*ptr, ':');
    const property_t *prop;
    header_t *ret;

    if(!colon) return NULL;

    *colon = '\0';

    for(prop = default_props; prop->name; prop++)
    {
        if(!strcasecmp(*ptr, prop->name))
        {
            TRACE("%s: found match with default property id %d\n", *ptr, prop->id);
            break;
        }
    }

    if(!prop->name)
    {
        property_list_entry_t *prop_entry;
        LIST_FOR_EACH_ENTRY(prop_entry, &body->new_props, property_list_entry_t, entry)
        {
            if(!strcasecmp(*ptr, prop_entry->prop.name))
            {
                TRACE("%s: found match with already added new property id %d\n", *ptr, prop_entry->prop.id);
                prop = &prop_entry->prop;
                break;
            }
        }
        if(!prop->name)
        {
            prop_entry = HeapAlloc(GetProcessHeap(), 0, sizeof(*prop_entry));
            prop_entry->prop.name = strdupA(*ptr);
            prop_entry->prop.id = body->next_prop_id++;
            prop_entry->prop.flags = 0;
            prop_entry->prop.default_vt = VT_LPSTR;
            list_add_tail(&body->new_props, &prop_entry->entry);
            prop = &prop_entry->prop;
            TRACE("%s: allocating new prop id %d\n", *ptr, prop_entry->prop.id);
        }
    }

    ret = HeapAlloc(GetProcessHeap(), 0, sizeof(*ret));
    ret->prop = prop;
    PropVariantInit(&ret->value);
    list_init(&ret->params);
    *ptr = colon + 1;

    return ret;
}

static void unfold_header(char *header, int len)
{
    char *start = header, *cp = header;

    do {
        while(*cp == ' ' || *cp == '\t')
        {
            cp++;
            len--;
        }
        if(cp != start)
            memmove(start, cp, len + 1);

        cp = strstr(start, "\r\n");
        len -= (cp - start);
        start = cp;
        *start = ' ';
        start++;
        len--;
        cp += 2;
    } while(*cp == ' ' || *cp == '\t');

    *(start - 1) = '\0';
}

static char *unquote_string(const char *str)
{
    int quoted = 0;
    char *ret, *cp;

    while(*str == ' ' || *str == '\t') str++;

    if(*str == '"')
    {
        quoted = 1;
        str++;
    }
    ret = strdupA(str);
    for(cp = ret; *cp; cp++)
    {
        if(*cp == '\\')
            memmove(cp, cp + 1, strlen(cp + 1) + 1);
        else if(*cp == '"')
        {
            if(!quoted)
            {
                WARN("quote in unquoted string\n");
            }
            else
            {
                *cp = '\0';
                break;
            }
        }
    }
    return ret;
}

static void add_param(header_t *header, const char *p)
{
    const char *key = p, *value, *cp = p;
    param_t *param;
    char *name;

    TRACE("got param %s\n", p);

    while (*key == ' ' || *key == '\t' ) key++;

    cp = strchr(key, '=');
    if(!cp)
    {
        WARN("malformed parameter - skipping\n");
        return;
    }

    name = HeapAlloc(GetProcessHeap(), 0, cp - key + 1);
    memcpy(name, key, cp - key);
    name[cp - key] = '\0';

    value = cp + 1;

    param = HeapAlloc(GetProcessHeap(), 0, sizeof(*param));
    param->name = name;
    param->value = unquote_string(value);
    list_add_tail(&header->params, &param->entry);
}

static void split_params(header_t *header, char *value)
{
    char *cp = value, *start = value;
    int in_quote = 0;
    int done_value = 0;

    while(*cp)
    {
        if(!in_quote && *cp == ';')
        {
            *cp = '\0';
            if(done_value) add_param(header, start);
            done_value = 1;
            start = cp + 1;
        }
        else if(*cp == '"')
            in_quote = !in_quote;
        cp++;
    }
    if(done_value) add_param(header, start);
}

static void read_value(header_t *header, char **cur)
{
    char *end = *cur, *value;
    DWORD len;

    do {
        end = strstr(end, "\r\n");
        end += 2;
    } while(*end == ' ' || *end == '\t');

    len = end - *cur;
    value = HeapAlloc(GetProcessHeap(), 0, len + 1);
    memcpy(value, *cur, len);
    value[len] = '\0';

    unfold_header(value, len);
    TRACE("value %s\n", debugstr_a(value));

    if(header->prop->flags & MPF_HASPARAMS)
    {
        split_params(header, value);
        TRACE("value w/o params %s\n", debugstr_a(value));
    }

    header->value.vt = VT_LPSTR;
    header->value.u.pszVal = value;

    *cur = end;
}

static void init_content_type(MimeBody *body, header_t *header)
{
    char *slash;
    DWORD len;

    if(header->prop->id != PID_HDR_CNTTYPE)
    {
        ERR("called with header %s\n", header->prop->name);
        return;
    }

    slash = strchr(header->value.u.pszVal, '/');
    if(!slash)
    {
        WARN("malformed context type value\n");
        return;
    }
    len = slash - header->value.u.pszVal;
    body->content_pri_type = HeapAlloc(GetProcessHeap(), 0, len + 1);
    memcpy(body->content_pri_type, header->value.u.pszVal, len);
    body->content_pri_type[len] = '\0';
    body->content_sub_type = strdupA(slash + 1);
}

static HRESULT parse_headers(MimeBody *body, IStream *stm)
{
    char *header_buf, *cur_header_ptr;
    HRESULT hr;
    header_t *header;

    hr = copy_headers_to_buf(stm, &header_buf);
    if(FAILED(hr)) return hr;

    cur_header_ptr = header_buf;
    while((header = read_prop(body, &cur_header_ptr)))
    {
        read_value(header, &cur_header_ptr);
        list_add_tail(&body->headers, &header->entry);

        if(header->prop->id == PID_HDR_CNTTYPE)
            init_content_type(body, header);
    }

    HeapFree(GetProcessHeap(), 0, header_buf);
    return hr;
}

static void empty_param_list(struct list *list)
{
    param_t *param, *cursor2;

    LIST_FOR_EACH_ENTRY_SAFE(param, cursor2, list, param_t, entry)
    {
        list_remove(&param->entry);
        HeapFree(GetProcessHeap(), 0, param->name);
        HeapFree(GetProcessHeap(), 0, param->value);
        HeapFree(GetProcessHeap(), 0, param);
    }
}

static void empty_header_list(struct list *list)
{
    header_t *header, *cursor2;

    LIST_FOR_EACH_ENTRY_SAFE(header, cursor2, list, header_t, entry)
    {
        list_remove(&header->entry);
        PropVariantClear(&header->value);
        empty_param_list(&header->params);
        HeapFree(GetProcessHeap(), 0, header);
    }
}

static void empty_new_prop_list(struct list *list)
{
    property_list_entry_t *prop, *cursor2;

    LIST_FOR_EACH_ENTRY_SAFE(prop, cursor2, list, property_list_entry_t, entry)
    {
        list_remove(&prop->entry);
        HeapFree(GetProcessHeap(), 0, (char *)prop->prop.name);
        HeapFree(GetProcessHeap(), 0, prop);
    }
}

static void release_data(REFIID riid, void *data)
{
    if(!data) return;

    if(IsEqualIID(riid, &IID_IStream))
        IStream_Release((IStream *)data);
    else
        FIXME("Unhandled data format %s\n", debugstr_guid(riid));
}

static HRESULT find_prop(MimeBody *body, const char *name, header_t **prop)
{
    header_t *header;

    *prop = NULL;

    LIST_FOR_EACH_ENTRY(header, &body->headers, header_t, entry)
    {
        if(!strcasecmp(name, header->prop->name))
        {
            *prop = header;
            return S_OK;
        }
    }

    return MIME_E_NOT_FOUND;
}

static HRESULT WINAPI MimeBody_QueryInterface(IMimeBody* iface,
                                     REFIID riid,
                                     void** ppvObject)
{
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppvObject);

    *ppvObject = NULL;

    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IPersist) ||
        IsEqualIID(riid, &IID_IPersistStreamInit) ||
        IsEqualIID(riid, &IID_IMimePropertySet) ||
        IsEqualIID(riid, &IID_IMimeBody))
    {
        *ppvObject = iface;
    }

    if(*ppvObject)
    {
        IUnknown_AddRef((IUnknown*)*ppvObject);
        return S_OK;
    }

    FIXME("no interface for %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI MimeBody_AddRef(IMimeBody* iface)
{
    MimeBody *This = impl_from_IMimeBody(iface);
    TRACE("(%p)->()\n", iface);
    return InterlockedIncrement(&This->refs);
}

static ULONG WINAPI MimeBody_Release(IMimeBody* iface)
{
    MimeBody *This = impl_from_IMimeBody(iface);
    ULONG refs;

    TRACE("(%p)->()\n", iface);

    refs = InterlockedDecrement(&This->refs);
    if (!refs)
    {
        empty_header_list(&This->headers);
        empty_new_prop_list(&This->new_props);

        HeapFree(GetProcessHeap(), 0, This->content_pri_type);
        HeapFree(GetProcessHeap(), 0, This->content_sub_type);

        release_data(&This->data_iid, This->data);

        HeapFree(GetProcessHeap(), 0, This);
    }

    return refs;
}

static HRESULT WINAPI MimeBody_GetClassID(
                                 IMimeBody* iface,
                                 CLSID* pClassID)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}


static HRESULT WINAPI MimeBody_IsDirty(
                              IMimeBody* iface)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_Load(
                           IMimeBody* iface,
                           LPSTREAM pStm)
{
    MimeBody *This = impl_from_IMimeBody(iface);
    TRACE("(%p)->(%p)\n", iface, pStm);
    return parse_headers(This, pStm);
}

static HRESULT WINAPI MimeBody_Save(
                           IMimeBody* iface,
                           LPSTREAM pStm,
                           BOOL fClearDirty)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_GetSizeMax(
                                 IMimeBody* iface,
                                 ULARGE_INTEGER* pcbSize)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_InitNew(
                              IMimeBody* iface)
{
    TRACE("%p->()\n", iface);
    return S_OK;
}

static HRESULT WINAPI MimeBody_GetPropInfo(
                                  IMimeBody* iface,
                                  LPCSTR pszName,
                                  LPMIMEPROPINFO pInfo)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_SetPropInfo(
                                  IMimeBody* iface,
                                  LPCSTR pszName,
                                  LPCMIMEPROPINFO pInfo)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_GetProp(
                              IMimeBody* iface,
                              LPCSTR pszName,
                              DWORD dwFlags,
                              LPPROPVARIANT pValue)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_SetProp(
                              IMimeBody* iface,
                              LPCSTR pszName,
                              DWORD dwFlags,
                              LPCPROPVARIANT pValue)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_AppendProp(
                                 IMimeBody* iface,
                                 LPCSTR pszName,
                                 DWORD dwFlags,
                                 LPPROPVARIANT pValue)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_DeleteProp(
                                 IMimeBody* iface,
                                 LPCSTR pszName)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_CopyProps(
                                IMimeBody* iface,
                                ULONG cNames,
                                LPCSTR* prgszName,
                                IMimePropertySet* pPropertySet)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_MoveProps(
                                IMimeBody* iface,
                                ULONG cNames,
                                LPCSTR* prgszName,
                                IMimePropertySet* pPropertySet)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_DeleteExcept(
                                   IMimeBody* iface,
                                   ULONG cNames,
                                   LPCSTR* prgszName)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_QueryProp(
                                IMimeBody* iface,
                                LPCSTR pszName,
                                LPCSTR pszCriteria,
                                boolean fSubString,
                                boolean fCaseSensitive)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_GetCharset(
                                 IMimeBody* iface,
                                 LPHCHARSET phCharset)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_SetCharset(
                                 IMimeBody* iface,
                                 HCHARSET hCharset,
                                 CSETAPPLYTYPE applytype)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_GetParameters(
                                    IMimeBody* iface,
                                    LPCSTR pszName,
                                    ULONG* pcParams,
                                    LPMIMEPARAMINFO* pprgParam)
{
    MimeBody *This = impl_from_IMimeBody(iface);
    HRESULT hr;
    header_t *header;

    TRACE("(%p)->(%s, %p, %p)\n", iface, debugstr_a(pszName), pcParams, pprgParam);

    *pprgParam = NULL;
    *pcParams = 0;

    hr = find_prop(This, pszName, &header);
    if(hr != S_OK) return hr;

    *pcParams = list_count(&header->params);
    if(*pcParams)
    {
        IMimeAllocator *alloc;
        param_t *param;
        MIMEPARAMINFO *info;

        MimeOleGetAllocator(&alloc);

        *pprgParam = info = IMimeAllocator_Alloc(alloc, *pcParams * sizeof(**pprgParam));
        LIST_FOR_EACH_ENTRY(param, &header->params, param_t, entry)
        {
            int len;

            len = strlen(param->name) + 1;
            info->pszName = IMimeAllocator_Alloc(alloc, len);
            memcpy(info->pszName, param->name, len);
            len = strlen(param->value) + 1;
            info->pszData = IMimeAllocator_Alloc(alloc, len);
            memcpy(info->pszData, param->value, len);
            info++;
        }
        IMimeAllocator_Release(alloc);
    }
    return S_OK;
}

static HRESULT WINAPI MimeBody_IsContentType(
                                    IMimeBody* iface,
                                    LPCSTR pszPriType,
                                    LPCSTR pszSubType)
{
    MimeBody *This = impl_from_IMimeBody(iface);

    TRACE("(%p)->(%s, %s)\n", This, debugstr_a(pszPriType), debugstr_a(pszSubType));
    if(pszPriType)
    {
        const char *pri = This->content_pri_type;
        if(!pri) pri = "text";
        if(strcasecmp(pri, pszPriType)) return S_FALSE;
    }

    if(pszSubType)
    {
        const char *sub = This->content_sub_type;
        if(!sub) sub = "plain";
        if(strcasecmp(sub, pszSubType)) return S_FALSE;
    }

    return S_OK;
}

static HRESULT WINAPI MimeBody_BindToObject(
                                   IMimeBody* iface,
                                   REFIID riid,
                                   void** ppvObject)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_Clone(
                            IMimeBody* iface,
                            IMimePropertySet** ppPropertySet)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_SetOption(
                                IMimeBody* iface,
                                const TYPEDID oid,
                                LPCPROPVARIANT pValue)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_GetOption(
                                IMimeBody* iface,
                                const TYPEDID oid,
                                LPPROPVARIANT pValue)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_EnumProps(
                                IMimeBody* iface,
                                DWORD dwFlags,
                                IMimeEnumProperties** ppEnum)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_IsType(
                             IMimeBody* iface,
                             IMSGBODYTYPE bodytype)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_SetDisplayName(
                                     IMimeBody* iface,
                                     LPCSTR pszDisplay)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_GetDisplayName(
                                     IMimeBody* iface,
                                     LPSTR* ppszDisplay)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_GetOffsets(
                                 IMimeBody* iface,
                                 LPBODYOFFSETS pOffsets)
{
    MimeBody *This = impl_from_IMimeBody(iface);
    TRACE("(%p)->(%p)\n", This, pOffsets);

    *pOffsets = This->body_offsets;

    if(This->body_offsets.cbBodyEnd == 0) return MIME_E_NO_DATA;
    return S_OK;
}

static HRESULT WINAPI MimeBody_GetCurrentEncoding(
                                         IMimeBody* iface,
                                         ENCODINGTYPE* pietEncoding)
{
    MimeBody *This = impl_from_IMimeBody(iface);

    TRACE("(%p)->(%p)\n", This, pietEncoding);

    *pietEncoding = This->encoding;
    return S_OK;
}

static HRESULT WINAPI MimeBody_SetCurrentEncoding(
                                         IMimeBody* iface,
                                         ENCODINGTYPE ietEncoding)
{
    MimeBody *This = impl_from_IMimeBody(iface);

    TRACE("(%p)->(%d)\n", This, ietEncoding);

    This->encoding = ietEncoding;
    return S_OK;
}

static HRESULT WINAPI MimeBody_GetEstimatedSize(
                                       IMimeBody* iface,
                                       ENCODINGTYPE ietEncoding,
                                       ULONG* pcbSize)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_GetDataHere(
                                  IMimeBody* iface,
                                  ENCODINGTYPE ietEncoding,
                                  IStream* pStream)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_GetData(
                              IMimeBody* iface,
                              ENCODINGTYPE ietEncoding,
                              IStream** ppStream)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_SetData(
                              IMimeBody* iface,
                              ENCODINGTYPE ietEncoding,
                              LPCSTR pszPriType,
                              LPCSTR pszSubType,
                              REFIID riid,
                              LPVOID pvObject)
{
    MimeBody *This = impl_from_IMimeBody(iface);
    TRACE("(%p)->(%d, %s, %s, %s %p)\n", This, ietEncoding, debugstr_a(pszPriType), debugstr_a(pszSubType),
          debugstr_guid(riid), pvObject);

    if(IsEqualIID(riid, &IID_IStream))
        IStream_AddRef((IStream *)pvObject);
    else
    {
        FIXME("Unhandled object type %s\n", debugstr_guid(riid));
        return E_INVALIDARG;
    }

    if(This->data)
        FIXME("release old data\n");

    This->data_iid = *riid;
    This->data = pvObject;

    IMimeBody_SetCurrentEncoding(iface, ietEncoding);

    /* FIXME: Update the content type.
       If pszPriType == NULL use 'application'
       If pszSubType == NULL use 'octet-stream' */

    return S_OK;
}

static HRESULT WINAPI MimeBody_EmptyData(
                                IMimeBody* iface)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_CopyTo(
                             IMimeBody* iface,
                             IMimeBody* pBody)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_GetTransmitInfo(
                                      IMimeBody* iface,
                                      LPTRANSMITINFO pTransmitInfo)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_SaveToFile(
                                 IMimeBody* iface,
                                 ENCODINGTYPE ietEncoding,
                                 LPCSTR pszFilePath)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeBody_GetHandle(
                                IMimeBody* iface,
                                LPHBODY phBody)
{
    MimeBody *This = impl_from_IMimeBody(iface);
    TRACE("(%p)->(%p)\n", iface, phBody);

    *phBody = This->handle;
    return This->handle ? S_OK : MIME_E_NO_DATA;
}

static IMimeBodyVtbl body_vtbl =
{
    MimeBody_QueryInterface,
    MimeBody_AddRef,
    MimeBody_Release,
    MimeBody_GetClassID,
    MimeBody_IsDirty,
    MimeBody_Load,
    MimeBody_Save,
    MimeBody_GetSizeMax,
    MimeBody_InitNew,
    MimeBody_GetPropInfo,
    MimeBody_SetPropInfo,
    MimeBody_GetProp,
    MimeBody_SetProp,
    MimeBody_AppendProp,
    MimeBody_DeleteProp,
    MimeBody_CopyProps,
    MimeBody_MoveProps,
    MimeBody_DeleteExcept,
    MimeBody_QueryProp,
    MimeBody_GetCharset,
    MimeBody_SetCharset,
    MimeBody_GetParameters,
    MimeBody_IsContentType,
    MimeBody_BindToObject,
    MimeBody_Clone,
    MimeBody_SetOption,
    MimeBody_GetOption,
    MimeBody_EnumProps,
    MimeBody_IsType,
    MimeBody_SetDisplayName,
    MimeBody_GetDisplayName,
    MimeBody_GetOffsets,
    MimeBody_GetCurrentEncoding,
    MimeBody_SetCurrentEncoding,
    MimeBody_GetEstimatedSize,
    MimeBody_GetDataHere,
    MimeBody_GetData,
    MimeBody_SetData,
    MimeBody_EmptyData,
    MimeBody_CopyTo,
    MimeBody_GetTransmitInfo,
    MimeBody_SaveToFile,
    MimeBody_GetHandle
};

static HRESULT MimeBody_set_offsets(MimeBody *body, const BODYOFFSETS *offsets)
{
    TRACE("setting offsets to %d, %d, %d, %d\n", offsets->cbBoundaryStart,
          offsets->cbHeaderStart, offsets->cbBodyStart, offsets->cbBodyEnd);

    body->body_offsets = *offsets;
    return S_OK;
}

#define FIRST_CUSTOM_PROP_ID 0x100

HRESULT MimeBody_create(IUnknown *outer, void **obj)
{
    MimeBody *This;
    BODYOFFSETS body_offsets;

    *obj = NULL;

    if(outer) return CLASS_E_NOAGGREGATION;

    This = HeapAlloc(GetProcessHeap(), 0, sizeof(*This));
    if (!This) return E_OUTOFMEMORY;

    This->lpVtbl = &body_vtbl;
    This->refs = 1;
    This->handle = NULL;
    list_init(&This->headers);
    list_init(&This->new_props);
    This->next_prop_id = FIRST_CUSTOM_PROP_ID;
    This->content_pri_type = NULL;
    This->content_sub_type = NULL;
    This->encoding = IET_7BIT;
    This->data = NULL;
    This->data_iid = IID_NULL;

    body_offsets.cbBoundaryStart = body_offsets.cbHeaderStart = 0;
    body_offsets.cbBodyStart     = body_offsets.cbBodyEnd     = 0;
    MimeBody_set_offsets(This, &body_offsets);

    *obj = (IMimeBody *)&This->lpVtbl;
    return S_OK;
}

typedef struct MimeMessage
{
    const IMimeMessageVtbl *lpVtbl;

    LONG refs;
} MimeMessage;

static HRESULT WINAPI MimeMessage_QueryInterface(IMimeMessage *iface, REFIID riid, void **ppv)
{
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IPersist) ||
        IsEqualIID(riid, &IID_IPersistStreamInit) ||
        IsEqualIID(riid, &IID_IMimeMessageTree) ||
        IsEqualIID(riid, &IID_IMimeMessage))
    {
        *ppv = iface;
        IUnknown_AddRef(iface);
        return S_OK;
    }

    FIXME("no interface for %s\n", debugstr_guid(riid));
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI MimeMessage_AddRef(IMimeMessage *iface)
{
    MimeMessage *This = (MimeMessage *)iface;
    TRACE("(%p)->()\n", iface);
    return InterlockedIncrement(&This->refs);
}

static ULONG WINAPI MimeMessage_Release(IMimeMessage *iface)
{
    MimeMessage *This = (MimeMessage *)iface;
    ULONG refs;

    TRACE("(%p)->()\n", iface);

    refs = InterlockedDecrement(&This->refs);
    if (!refs)
    {
        HeapFree(GetProcessHeap(), 0, This);
    }

    return refs;
}

/*** IPersist methods ***/
static HRESULT WINAPI MimeMessage_GetClassID(
    IMimeMessage *iface,
    CLSID *pClassID)
{
    FIXME("(%p)->(%p)\n", iface, pClassID);
    return E_NOTIMPL;
}

/*** IPersistStreamInit methods ***/
static HRESULT WINAPI MimeMessage_IsDirty(
    IMimeMessage *iface)
{
    FIXME("(%p)->()\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_Load(
    IMimeMessage *iface,
    LPSTREAM pStm){
    FIXME("(%p)->(%p)\n", iface, pStm);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_Save(
    IMimeMessage *iface,
    LPSTREAM pStm,
    BOOL fClearDirty)
{
    FIXME("(%p)->(%p, %s)\n", iface, pStm, fClearDirty ? "TRUE" : "FALSE");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_GetSizeMax(
    IMimeMessage *iface,
    ULARGE_INTEGER *pcbSize)
{
    FIXME("(%p)->(%p)\n", iface, pcbSize);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_InitNew(
    IMimeMessage *iface)
{
    FIXME("(%p)->()\n", iface);
    return E_NOTIMPL;
}

/*** IMimeMessageTree methods ***/
static HRESULT WINAPI MimeMessage_GetMessageSource(
    IMimeMessage *iface,
    IStream **ppStream,
    DWORD dwFlags)
{
    FIXME("(%p)->(%p, 0x%x)\n", iface, ppStream, dwFlags);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_GetMessageSize(
    IMimeMessage *iface,
    ULONG *pcbSize,
    DWORD dwFlags)
{
    FIXME("(%p)->(%p, 0x%x)\n", iface, pcbSize, dwFlags);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_LoadOffsetTable(
    IMimeMessage *iface,
    IStream *pStream)
{
    FIXME("(%p)->(%p)\n", iface, pStream);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_SaveOffsetTable(
    IMimeMessage *iface,
    IStream *pStream,
    DWORD dwFlags)
{
    FIXME("(%p)->(%p, 0x%x)\n", iface, pStream, dwFlags);
    return E_NOTIMPL;
}


static HRESULT WINAPI MimeMessage_GetFlags(
    IMimeMessage *iface,
    DWORD *pdwFlags)
{
    FIXME("(%p)->(%p)\n", iface, pdwFlags);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_Commit(
    IMimeMessage *iface,
    DWORD dwFlags)
{
    FIXME("(%p)->(0x%x)\n", iface, dwFlags);
    return E_NOTIMPL;
}


static HRESULT WINAPI MimeMessage_HandsOffStorage(
    IMimeMessage *iface)
{
    FIXME("(%p)->()\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_BindToObject(
    IMimeMessage *iface,
    const HBODY hBody,
    REFIID riid,
    void **ppvObject)
{
    FIXME("(%p)->(%p, %s, %p)\n", iface, hBody, debugstr_guid(riid), ppvObject);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_SaveBody(
    IMimeMessage *iface,
    HBODY hBody,
    DWORD dwFlags,
    IStream *pStream)
{
    FIXME("(%p)->(%p, 0x%x, %p)\n", iface, hBody, dwFlags, pStream);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_InsertBody(
    IMimeMessage *iface,
    BODYLOCATION location,
    HBODY hPivot,
    LPHBODY phBody)
{
    FIXME("(%p)->(%d, %p, %p)\n", iface, location, hPivot, phBody);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_GetBody(
    IMimeMessage *iface,
    BODYLOCATION location,
    HBODY hPivot,
    LPHBODY phBody)
{
    FIXME("(%p)->(%d, %p, %p)\n", iface, location, hPivot, phBody);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_DeleteBody(
    IMimeMessage *iface,
    HBODY hBody,
    DWORD dwFlags)
{
    FIXME("(%p)->(%p, %08x)\n", iface, hBody, dwFlags);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_MoveBody(
    IMimeMessage *iface,
    HBODY hBody,
    BODYLOCATION location)
{
    FIXME("(%p)->(%d)\n", iface, location);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_CountBodies(
    IMimeMessage *iface,
    HBODY hParent,
    boolean fRecurse,
    ULONG *pcBodies)
{
    FIXME("(%p)->(%p, %s, %p)\n", iface, hParent, fRecurse ? "TRUE" : "FALSE", pcBodies);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_FindFirst(
    IMimeMessage *iface,
    LPFINDBODY pFindBody,
    LPHBODY phBody)
{
    FIXME("(%p)->(%p, %p)\n", iface, pFindBody, phBody);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_FindNext(
    IMimeMessage *iface,
    LPFINDBODY pFindBody,
    LPHBODY phBody)
{
    FIXME("(%p)->(%p, %p)\n", iface, pFindBody, phBody);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_ResolveURL(
    IMimeMessage *iface,
    HBODY hRelated,
    LPCSTR pszBase,
    LPCSTR pszURL,
    DWORD dwFlags,
    LPHBODY phBody)
{
    FIXME("(%p)->(%p, %s, %s, 0x%x, %p)\n", iface, hRelated, pszBase, pszURL, dwFlags, phBody);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_ToMultipart(
    IMimeMessage *iface,
    HBODY hBody,
    LPCSTR pszSubType,
    LPHBODY phMultipart)
{
    FIXME("(%p)->(%p, %s, %p)\n", iface, hBody, pszSubType, phMultipart);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_GetBodyOffsets(
    IMimeMessage *iface,
    HBODY hBody,
    LPBODYOFFSETS pOffsets)
{
    FIXME("(%p)->(%p, %p)\n", iface, hBody, pOffsets);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_GetCharset(
    IMimeMessage *iface,
    LPHCHARSET phCharset)
{
    FIXME("(%p)->(%p)\n", iface, phCharset);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_SetCharset(
    IMimeMessage *iface,
    HCHARSET hCharset,
    CSETAPPLYTYPE applytype)
{
    FIXME("(%p)->(%p, %d)\n", iface, hCharset, applytype);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_IsBodyType(
    IMimeMessage *iface,
    HBODY hBody,
    IMSGBODYTYPE bodytype)
{
    FIXME("(%p)->(%p, %d)\n", iface, hBody, bodytype);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_IsContentType(
    IMimeMessage *iface,
    HBODY hBody,
    LPCSTR pszPriType,
    LPCSTR pszSubType)
{
    FIXME("(%p)->(%p, %s, %s)\n", iface, hBody, pszPriType, pszSubType);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_QueryBodyProp(
    IMimeMessage *iface,
    HBODY hBody,
    LPCSTR pszName,
    LPCSTR pszCriteria,
    boolean fSubString,
    boolean fCaseSensitive)
{
    FIXME("(%p)->(%p, %s, %s, %s, %s)\n", iface, hBody, pszName, pszCriteria, fSubString ? "TRUE" : "FALSE", fCaseSensitive ? "TRUE" : "FALSE");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_GetBodyProp(
    IMimeMessage *iface,
    HBODY hBody,
    LPCSTR pszName,
    DWORD dwFlags,
    LPPROPVARIANT pValue)
{
    FIXME("(%p)->(%p, %s, 0x%x, %p)\n", iface, hBody, pszName, dwFlags, pValue);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_SetBodyProp(
    IMimeMessage *iface,
    HBODY hBody,
    LPCSTR pszName,
    DWORD dwFlags,
    LPCPROPVARIANT pValue)
{
    FIXME("(%p)->(%p, %s, 0x%x, %p)\n", iface, hBody, pszName, dwFlags, pValue);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_DeleteBodyProp(
    IMimeMessage *iface,
    HBODY hBody,
    LPCSTR pszName)
{
    FIXME("(%p)->(%p, %s)\n", iface, hBody, pszName);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_SetOption(
    IMimeMessage *iface,
    const TYPEDID oid,
    LPCPROPVARIANT pValue)
{
    FIXME("(%p)->(%d, %p)\n", iface, oid, pValue);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_GetOption(
    IMimeMessage *iface,
    const TYPEDID oid,
    LPPROPVARIANT pValue)
{
    FIXME("(%p)->(%d, %p)\n", iface, oid, pValue);
    return E_NOTIMPL;
}

/*** IMimeMessage methods ***/
static HRESULT WINAPI MimeMessage_CreateWebPage(
    IMimeMessage *iface,
    IStream *pRootStm,
    LPWEBPAGEOPTIONS pOptions,
    IMimeMessageCallback *pCallback,
    IMoniker **ppMoniker)
{
    FIXME("(%p)->(%p, %p, %p, %p)\n", iface, pRootStm, pOptions, pCallback, ppMoniker);
    *ppMoniker = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_GetProp(
    IMimeMessage *iface,
    LPCSTR pszName,
    DWORD dwFlags,
    LPPROPVARIANT pValue)
{
    FIXME("(%p)->(%s, 0x%x, %p)\n", iface, pszName, dwFlags, pValue);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_SetProp(
    IMimeMessage *iface,
    LPCSTR pszName,
    DWORD dwFlags,
    LPCPROPVARIANT pValue)
{
    FIXME("(%p)->(%s, 0x%x, %p)\n", iface, pszName, dwFlags, pValue);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_DeleteProp(
    IMimeMessage *iface,
    LPCSTR pszName)
{
    FIXME("(%p)->(%s)\n", iface, pszName);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_QueryProp(
    IMimeMessage *iface,
    LPCSTR pszName,
    LPCSTR pszCriteria,
    boolean fSubString,
    boolean fCaseSensitive)
{
    FIXME("(%p)->(%s, %s, %s, %s)\n", iface, pszName, pszCriteria, fSubString ? "TRUE" : "FALSE", fCaseSensitive ? "TRUE" : "FALSE");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_GetTextBody(
    IMimeMessage *iface,
    DWORD dwTxtType,
    ENCODINGTYPE ietEncoding,
    IStream **pStream,
    LPHBODY phBody)
{
    FIXME("(%p)->(%d, %d, %p, %p)\n", iface, dwTxtType, ietEncoding, pStream, phBody);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_SetTextBody(
    IMimeMessage *iface,
    DWORD dwTxtType,
    ENCODINGTYPE ietEncoding,
    HBODY hAlternative,
    IStream *pStream,
    LPHBODY phBody)
{
    FIXME("(%p)->(%d, %d, %p, %p, %p)\n", iface, dwTxtType, ietEncoding, hAlternative, pStream, phBody);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_AttachObject(
    IMimeMessage *iface,
    REFIID riid,
    void *pvObject,
    LPHBODY phBody)
{
    FIXME("(%p)->(%s, %p, %p)\n", iface, debugstr_guid(riid), pvObject, phBody);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_AttachFile(
    IMimeMessage *iface,
    LPCSTR pszFilePath,
    IStream *pstmFile,
    LPHBODY phBody)
{
    FIXME("(%p)->(%s, %p, %p)\n", iface, pszFilePath, pstmFile, phBody);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_AttachURL(
    IMimeMessage *iface,
    LPCSTR pszBase,
    LPCSTR pszURL,
    DWORD dwFlags,
    IStream *pstmURL,
    LPSTR *ppszCIDURL,
    LPHBODY phBody)
{
    FIXME("(%p)->(%s, %s, 0x%x, %p, %p, %p)\n", iface, pszBase, pszURL, dwFlags, pstmURL, ppszCIDURL, phBody);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_GetAttachments(
    IMimeMessage *iface,
    ULONG *pcAttach,
    LPHBODY *pprghAttach)
{
    FIXME("(%p)->(%p, %p)\n", iface, pcAttach, pprghAttach);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_GetAddressTable(
    IMimeMessage *iface,
    IMimeAddressTable **ppTable)
{
    FIXME("(%p)->(%p)\n", iface, ppTable);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_GetSender(
    IMimeMessage *iface,
    LPADDRESSPROPS pAddress)
{
    FIXME("(%p)->(%p)\n", iface, pAddress);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_GetAddressTypes(
    IMimeMessage *iface,
    DWORD dwAdrTypes,
    DWORD dwProps,
    LPADDRESSLIST pList)
{
    FIXME("(%p)->(%d, %d, %p)\n", iface, dwAdrTypes, dwProps, pList);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_GetAddressFormat(
    IMimeMessage *iface,
    DWORD dwAdrTypes,
    ADDRESSFORMAT format,
    LPSTR *ppszFormat)
{
    FIXME("(%p)->(%d, %d, %p)\n", iface, dwAdrTypes, format, ppszFormat);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_EnumAddressTypes(
    IMimeMessage *iface,
    DWORD dwAdrTypes,
    DWORD dwProps,
    IMimeEnumAddressTypes **ppEnum)
{
    FIXME("(%p)->(%d, %d, %p)\n", iface, dwAdrTypes, dwProps, ppEnum);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_SplitMessage(
    IMimeMessage *iface,
    ULONG cbMaxPart,
    IMimeMessageParts **ppParts)
{
    FIXME("(%p)->(%d, %p)\n", iface, cbMaxPart, ppParts);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeMessage_GetRootMoniker(
    IMimeMessage *iface,
    IMoniker **ppMoniker)
{
    FIXME("(%p)->(%p)\n", iface, ppMoniker);
    return E_NOTIMPL;
}

static const IMimeMessageVtbl MimeMessageVtbl =
{
    MimeMessage_QueryInterface,
    MimeMessage_AddRef,
    MimeMessage_Release,
    MimeMessage_GetClassID,
    MimeMessage_IsDirty,
    MimeMessage_Load,
    MimeMessage_Save,
    MimeMessage_GetSizeMax,
    MimeMessage_InitNew,
    MimeMessage_GetMessageSource,
    MimeMessage_GetMessageSize,
    MimeMessage_LoadOffsetTable,
    MimeMessage_SaveOffsetTable,
    MimeMessage_GetFlags,
    MimeMessage_Commit,
    MimeMessage_HandsOffStorage,
    MimeMessage_BindToObject,
    MimeMessage_SaveBody,
    MimeMessage_InsertBody,
    MimeMessage_GetBody,
    MimeMessage_DeleteBody,
    MimeMessage_MoveBody,
    MimeMessage_CountBodies,
    MimeMessage_FindFirst,
    MimeMessage_FindNext,
    MimeMessage_ResolveURL,
    MimeMessage_ToMultipart,
    MimeMessage_GetBodyOffsets,
    MimeMessage_GetCharset,
    MimeMessage_SetCharset,
    MimeMessage_IsBodyType,
    MimeMessage_IsContentType,
    MimeMessage_QueryBodyProp,
    MimeMessage_GetBodyProp,
    MimeMessage_SetBodyProp,
    MimeMessage_DeleteBodyProp,
    MimeMessage_SetOption,
    MimeMessage_GetOption,
    MimeMessage_CreateWebPage,
    MimeMessage_GetProp,
    MimeMessage_SetProp,
    MimeMessage_DeleteProp,
    MimeMessage_QueryProp,
    MimeMessage_GetTextBody,
    MimeMessage_SetTextBody,
    MimeMessage_AttachObject,
    MimeMessage_AttachFile,
    MimeMessage_AttachURL,
    MimeMessage_GetAttachments,
    MimeMessage_GetAddressTable,
    MimeMessage_GetSender,
    MimeMessage_GetAddressTypes,
    MimeMessage_GetAddressFormat,
    MimeMessage_EnumAddressTypes,
    MimeMessage_SplitMessage,
    MimeMessage_GetRootMoniker,
};

/***********************************************************************
 *              MimeOleCreateMessage (INETCOMM.@)
 */
HRESULT WINAPI MimeOleCreateMessage(IUnknown *pUnkOuter, IMimeMessage **ppMessage)
{
    MimeMessage *This;

    TRACE("(%p, %p)\n", pUnkOuter, ppMessage);

    if (pUnkOuter)
    {
        FIXME("outer unknown not supported yet\n");
        return E_NOTIMPL;
    }

    *ppMessage = NULL;

    This = HeapAlloc(GetProcessHeap(), 0, sizeof(*This));
    if (!This) return E_OUTOFMEMORY;

    This->lpVtbl = &MimeMessageVtbl;
    This->refs = 1;

    *ppMessage = (IMimeMessage *)&This->lpVtbl;
    return S_OK;
}

/***********************************************************************
 *              MimeOleSetCompatMode (INETCOMM.@)
 */
HRESULT WINAPI MimeOleSetCompatMode(DWORD dwMode)
{
    FIXME("(0x%x)\n", dwMode);
    return S_OK;
}

/***********************************************************************
 *              MimeOleCreateVirtualStream (INETCOMM.@)
 */
HRESULT WINAPI MimeOleCreateVirtualStream(IStream **ppStream)
{
    HRESULT hr;
    FIXME("(%p)\n", ppStream);

    hr = CreateStreamOnHGlobal(NULL, TRUE, ppStream);
    return hr;
}

typedef struct MimeSecurity
{
    const IMimeSecurityVtbl *lpVtbl;

    LONG refs;
} MimeSecurity;

static HRESULT WINAPI MimeSecurity_QueryInterface(
        IMimeSecurity* iface,
        REFIID riid,
        void** obj)
{
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), obj);

    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IMimeSecurity))
    {
        *obj = iface;
        IUnknown_AddRef(iface);
        return S_OK;
    }

    FIXME("no interface for %s\n", debugstr_guid(riid));
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI MimeSecurity_AddRef(
        IMimeSecurity* iface)
{
    MimeSecurity *This = (MimeSecurity *)iface;
    TRACE("(%p)->()\n", iface);
    return InterlockedIncrement(&This->refs);
}

static ULONG WINAPI MimeSecurity_Release(
        IMimeSecurity* iface)
{
    MimeSecurity *This = (MimeSecurity *)iface;
    ULONG refs;

    TRACE("(%p)->()\n", iface);

    refs = InterlockedDecrement(&This->refs);
    if (!refs)
    {
        HeapFree(GetProcessHeap(), 0, This);
    }

    return refs;
}

static HRESULT WINAPI MimeSecurity_InitNew(
        IMimeSecurity* iface)
{
    FIXME("(%p)->(): stub\n", iface);
    return S_OK;
}

static HRESULT WINAPI MimeSecurity_CheckInit(
        IMimeSecurity* iface)
{
    FIXME("(%p)->(): stub\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeSecurity_EncodeMessage(
        IMimeSecurity* iface,
        IMimeMessageTree* pTree,
        DWORD dwFlags)
{
    FIXME("(%p)->(%p, %08x): stub\n", iface, pTree, dwFlags);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeSecurity_EncodeBody(
        IMimeSecurity* iface,
        IMimeMessageTree* pTree,
        HBODY hEncodeRoot,
        DWORD dwFlags)
{
    FIXME("(%p)->(%p, %p, %08x): stub\n", iface, pTree, hEncodeRoot, dwFlags);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeSecurity_DecodeMessage(
        IMimeSecurity* iface,
        IMimeMessageTree* pTree,
        DWORD dwFlags)
{
    FIXME("(%p)->(%p, %08x): stub\n", iface, pTree, dwFlags);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeSecurity_DecodeBody(
        IMimeSecurity* iface,
        IMimeMessageTree* pTree,
        HBODY hDecodeRoot,
        DWORD dwFlags)
{
    FIXME("(%p)->(%p, %p, %08x): stub\n", iface, pTree, hDecodeRoot, dwFlags);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeSecurity_EnumCertificates(
        IMimeSecurity* iface,
        HCAPICERTSTORE hc,
        DWORD dwUsage,
        PCX509CERT pPrev,
        PCX509CERT* ppCert)
{
    FIXME("(%p)->(%p, %08x, %p, %p): stub\n", iface, hc, dwUsage, pPrev, ppCert);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeSecurity_GetCertificateName(
        IMimeSecurity* iface,
        const PCX509CERT pX509Cert,
        const CERTNAMETYPE cn,
        LPSTR* ppszName)
{
    FIXME("(%p)->(%p, %08x, %p): stub\n", iface, pX509Cert, cn, ppszName);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeSecurity_GetMessageType(
        IMimeSecurity* iface,
        const HWND hwndParent,
        IMimeBody* pBody,
        DWORD* pdwSecType)
{
    FIXME("(%p)->(%p, %p, %p): stub\n", iface, hwndParent, pBody, pdwSecType);
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeSecurity_GetCertData(
        IMimeSecurity* iface,
        const PCX509CERT pX509Cert,
        const CERTDATAID dataid,
        LPPROPVARIANT pValue)
{
    FIXME("(%p)->(%p, %x, %p): stub\n", iface, pX509Cert, dataid, pValue);
    return E_NOTIMPL;
}


static const IMimeSecurityVtbl MimeSecurityVtbl =
{
    MimeSecurity_QueryInterface,
    MimeSecurity_AddRef,
    MimeSecurity_Release,
    MimeSecurity_InitNew,
    MimeSecurity_CheckInit,
    MimeSecurity_EncodeMessage,
    MimeSecurity_EncodeBody,
    MimeSecurity_DecodeMessage,
    MimeSecurity_DecodeBody,
    MimeSecurity_EnumCertificates,
    MimeSecurity_GetCertificateName,
    MimeSecurity_GetMessageType,
    MimeSecurity_GetCertData
};

/***********************************************************************
 *              MimeOleCreateSecurity (INETCOMM.@)
 */
HRESULT WINAPI MimeOleCreateSecurity(IMimeSecurity **ppSecurity)
{
    MimeSecurity *This;

    TRACE("(%p)\n", ppSecurity);

    *ppSecurity = NULL;

    This = HeapAlloc(GetProcessHeap(), 0, sizeof(*This));
    if (!This) return E_OUTOFMEMORY;

    This->lpVtbl = &MimeSecurityVtbl;
    This->refs = 1;

    *ppSecurity = (IMimeSecurity *)&This->lpVtbl;
    return S_OK;
}


typedef struct
{
    IMimeAllocatorVtbl *lpVtbl;
} MimeAllocator;

static HRESULT WINAPI MimeAlloc_QueryInterface(
        IMimeAllocator* iface,
        REFIID riid,
        void **obj)
{
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), obj);

    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IMalloc) ||
        IsEqualIID(riid, &IID_IMimeAllocator))
    {
        *obj = iface;
        IUnknown_AddRef(iface);
        return S_OK;
    }

    FIXME("no interface for %s\n", debugstr_guid(riid));
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI MimeAlloc_AddRef(
        IMimeAllocator* iface)
{
    return 2;
}

static ULONG WINAPI MimeAlloc_Release(
        IMimeAllocator* iface)
{
    return 1;
}

static LPVOID WINAPI MimeAlloc_Alloc(
        IMimeAllocator* iface,
        ULONG cb)
{
    return CoTaskMemAlloc(cb);
}

static LPVOID WINAPI MimeAlloc_Realloc(
        IMimeAllocator* iface,
        LPVOID pv,
        ULONG cb)
{
    return CoTaskMemRealloc(pv, cb);
}

static void WINAPI MimeAlloc_Free(
        IMimeAllocator* iface,
        LPVOID pv)
{
    return CoTaskMemFree(pv);
}

static ULONG WINAPI MimeAlloc_GetSize(
        IMimeAllocator* iface,
        LPVOID pv)
{
    FIXME("stub\n");
    return 0;
}

static int WINAPI MimeAlloc_DidAlloc(
        IMimeAllocator* iface,
        LPVOID pv)
{
    FIXME("stub\n");
    return 0;
}

static void WINAPI MimeAlloc_HeapMinimize(
        IMimeAllocator* iface)
{
    FIXME("stub\n");
    return;
}

static HRESULT WINAPI MimeAlloc_FreeParamInfoArray(
        IMimeAllocator* iface,
        ULONG cParams,
        LPMIMEPARAMINFO prgParam,
        boolean fFreeArray)
{
    ULONG i;
    TRACE("(%p)->(%d, %p, %d)\n", iface, cParams, prgParam, fFreeArray);

    for(i = 0; i < cParams; i++)
    {
        IMimeAllocator_Free(iface, prgParam[i].pszName);
        IMimeAllocator_Free(iface, prgParam[i].pszData);
    }
    if(fFreeArray) IMimeAllocator_Free(iface, prgParam);
    return S_OK;
}

static HRESULT WINAPI MimeAlloc_FreeAddressList(
        IMimeAllocator* iface,
        LPADDRESSLIST pList)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeAlloc_FreeAddressProps(
        IMimeAllocator* iface,
        LPADDRESSPROPS pAddress)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeAlloc_ReleaseObjects(
        IMimeAllocator* iface,
        ULONG cObjects,
        IUnknown **prgpUnknown,
        boolean fFreeArray)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}


static HRESULT WINAPI MimeAlloc_FreeEnumHeaderRowArray(
        IMimeAllocator* iface,
        ULONG cRows,
        LPENUMHEADERROW prgRow,
        boolean fFreeArray)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeAlloc_FreeEnumPropertyArray(
        IMimeAllocator* iface,
        ULONG cProps,
        LPENUMPROPERTY prgProp,
        boolean fFreeArray)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI MimeAlloc_FreeThumbprint(
        IMimeAllocator* iface,
        THUMBBLOB *pthumbprint)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}


static HRESULT WINAPI MimeAlloc_PropVariantClear(
        IMimeAllocator* iface,
        LPPROPVARIANT pProp)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static IMimeAllocatorVtbl mime_alloc_vtbl =
{
    MimeAlloc_QueryInterface,
    MimeAlloc_AddRef,
    MimeAlloc_Release,
    MimeAlloc_Alloc,
    MimeAlloc_Realloc,
    MimeAlloc_Free,
    MimeAlloc_GetSize,
    MimeAlloc_DidAlloc,
    MimeAlloc_HeapMinimize,
    MimeAlloc_FreeParamInfoArray,
    MimeAlloc_FreeAddressList,
    MimeAlloc_FreeAddressProps,
    MimeAlloc_ReleaseObjects,
    MimeAlloc_FreeEnumHeaderRowArray,
    MimeAlloc_FreeEnumPropertyArray,
    MimeAlloc_FreeThumbprint,
    MimeAlloc_PropVariantClear
};

static MimeAllocator mime_allocator =
{
    &mime_alloc_vtbl
};

HRESULT MimeAllocator_create(IUnknown *outer, void **obj)
{
    if(outer) return CLASS_E_NOAGGREGATION;

    *obj = &mime_allocator;
    return S_OK;
}

HRESULT WINAPI MimeOleGetAllocator(IMimeAllocator **alloc)
{
    return MimeAllocator_create(NULL, (void**)alloc);
}
