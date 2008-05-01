/*
 * Miscellaneous Marshaling Routines
 *
 * Copyright 2005 Robert Shearman
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

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define COBJMACROS
#define NONAMELESSUNION
#define NONAMELESSSTRUCT

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "winerror.h"

#include "ole2.h"
#include "oleauto.h"
#include "rpcproxy.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(ole);

#define ALIGNED_LENGTH(_Len, _Align) (((_Len)+(_Align))&~(_Align))
#define ALIGNED_POINTER(_Ptr, _Align) ((LPVOID)ALIGNED_LENGTH((ULONG_PTR)(_Ptr), _Align))
#define ALIGN_LENGTH(_Len, _Align) _Len = ALIGNED_LENGTH(_Len, _Align)
#define ALIGN_POINTER(_Ptr, _Align) _Ptr = ALIGNED_POINTER(_Ptr, _Align)

static const char* debugstr_user_flags(ULONG *pFlags)
{
    char buf[12];
    const char* loword;
    switch (LOWORD(*pFlags))
    {
    case MSHCTX_LOCAL:
        loword="MSHCTX_LOCAL";
        break;
    case MSHCTX_NOSHAREDMEM:
        loword="MSHCTX_NOSHAREDMEM";
        break;
    case MSHCTX_DIFFERENTMACHINE:
        loword="MSHCTX_DIFFERENTMACHINE";
        break;
    case MSHCTX_INPROC:
        loword="MSHCTX_INPROC";
        break;
    default:
        sprintf(buf, "%d", LOWORD(*pFlags));
        loword=buf;
    }

    if (HIWORD(*pFlags) == NDR_LOCAL_DATA_REPRESENTATION)
        return wine_dbg_sprintf("MAKELONG(NDR_LOCAL_REPRESENTATION, %s)", loword);
    else
        return wine_dbg_sprintf("MAKELONG(0x%04x, %s)", HIWORD(*pFlags), loword);
}

/******************************************************************************
 *           CLIPFORMAT_UserSize [OLE32.@]
 *
 * Calculates the buffer size required to marshal a clip format.
 *
 * PARAMS
 *  pFlags       [I] Flags. See notes.
 *  StartingSize [I] Starting size of the buffer. This value is added on to
 *                   the buffer size required for the clip format.
 *  pCF          [I] Clip format to size.
 *
 * RETURNS
 *  The buffer size required to marshal a clip format plus the starting size.
 *
 * NOTES
 *  Even though the function is documented to take a pointer to an unsigned
 *  long in pFlags, it actually takes a pointer to a USER_MARSHAL_CB structure, of which
 *  the first parameter is an unsigned long.
 *  This function is only intended to be called by the RPC runtime.
 */
ULONG __RPC_USER CLIPFORMAT_UserSize(ULONG *pFlags, ULONG StartingSize, CLIPFORMAT *pCF)
{
    ULONG size = StartingSize;

    TRACE("(%s, %d, %p\n", debugstr_user_flags(pFlags), StartingSize, pCF);

    size += sizeof(userCLIPFORMAT);

    /* only need to marshal the name if it is not a pre-defined type and
     * we are going remote */
    if ((*pCF >= 0xc000) && (LOWORD(*pFlags) == MSHCTX_DIFFERENTMACHINE))
    {
        WCHAR format[255];
        INT ret;
        size += 3 * sizeof(INT);
        /* urg! this function is badly designed because it won't tell us how
         * much space is needed without doing a dummy run of storing the
         * name into a buffer */
        ret = GetClipboardFormatNameW(*pCF, format, sizeof(format)/sizeof(format[0])-1);
        if (!ret)
            RaiseException(DV_E_CLIPFORMAT, 0, 0, NULL);
        size += (ret + 1) * sizeof(WCHAR);
    }
    return size;
}

/******************************************************************************
 *           CLIPFORMAT_UserMarshal [OLE32.@]
 *
 * Marshals a clip format into a buffer.
 *
 * PARAMS
 *  pFlags  [I] Flags. See notes.
 *  pBuffer [I] Buffer to marshal the clip format into.
 *  pCF     [I] Clip format to marshal.
 *
 * RETURNS
 *  The end of the marshaled data in the buffer.
 *
 * NOTES
 *  Even though the function is documented to take a pointer to an unsigned
 *  long in pFlags, it actually takes a pointer to a USER_MARSHAL_CB structure, of which
 *  the first parameter is an unsigned long.
 *  This function is only intended to be called by the RPC runtime.
 */
unsigned char * __RPC_USER CLIPFORMAT_UserMarshal(ULONG *pFlags, unsigned char *pBuffer, CLIPFORMAT *pCF)
{
    wireCLIPFORMAT wirecf = (wireCLIPFORMAT)pBuffer;

    TRACE("(%s, %p, &0x%04x\n", debugstr_user_flags(pFlags), pBuffer, *pCF);

    wirecf->u.dwValue = *pCF;
    pBuffer += sizeof(*wirecf);

    /* only need to marshal the name if it is not a pre-defined type and
     * we are going remote */
    if ((*pCF >= 0xc000) && (LOWORD(*pFlags) == MSHCTX_DIFFERENTMACHINE))
    {
        WCHAR format[255];
        INT len;
        wirecf->fContext = WDT_REMOTE_CALL;
        len = GetClipboardFormatNameW(*pCF, format, sizeof(format)/sizeof(format[0])-1);
        if (!len)
            RaiseException(DV_E_CLIPFORMAT, 0, 0, NULL);
        len += 1;
        *(INT *)pBuffer = len;
        pBuffer += sizeof(INT);
        *(INT *)pBuffer = 0;
        pBuffer += sizeof(INT);
        *(INT *)pBuffer = len;
        pBuffer += sizeof(INT);
        TRACE("marshaling format name %s\n", debugstr_wn(format, len-1));
        lstrcpynW((LPWSTR)pBuffer, format, len);
        pBuffer += len * sizeof(WCHAR);
        *(WCHAR *)pBuffer = '\0';
        pBuffer += sizeof(WCHAR);
    }
    else
        wirecf->fContext = WDT_INPROC_CALL;

    return pBuffer;
}

/******************************************************************************
 *           CLIPFORMAT_UserUnmarshal [OLE32.@]
 *
 * Unmarshals a clip format from a buffer.
 *
 * PARAMS
 *  pFlags  [I] Flags. See notes.
 *  pBuffer [I] Buffer to marshal the clip format from.
 *  pCF     [O] Address that receive the unmarshaled clip format.
 *
 * RETURNS
 *  The end of the marshaled data in the buffer.
 *
 * NOTES
 *  Even though the function is documented to take a pointer to an unsigned
 *  long in pFlags, it actually takes a pointer to a USER_MARSHAL_CB structure, of which
 *  the first parameter is an unsigned long.
 *  This function is only intended to be called by the RPC runtime.
 */
unsigned char * __RPC_USER CLIPFORMAT_UserUnmarshal(ULONG *pFlags, unsigned char *pBuffer, CLIPFORMAT *pCF)
{
    wireCLIPFORMAT wirecf = (wireCLIPFORMAT)pBuffer;

    TRACE("(%s, %p, %p\n", debugstr_user_flags(pFlags), pBuffer, pCF);

    pBuffer += sizeof(*wirecf);
    if (wirecf->fContext == WDT_INPROC_CALL)
        *pCF = (CLIPFORMAT)wirecf->u.dwValue;
    else if (wirecf->fContext == WDT_REMOTE_CALL)
    {
        CLIPFORMAT cf;
        INT len = *(INT *)pBuffer;
        pBuffer += sizeof(INT);
        if (*(INT *)pBuffer != 0)
            RaiseException(RPC_S_INVALID_BOUND, 0, 0, NULL);
        pBuffer += sizeof(INT);
        if (*(INT *)pBuffer != len)
            RaiseException(RPC_S_INVALID_BOUND, 0, 0, NULL);
        pBuffer += sizeof(INT);
        if (((WCHAR *)pBuffer)[len] != '\0')
            RaiseException(RPC_S_INVALID_BOUND, 0, 0, NULL);
        TRACE("unmarshaling clip format %s\n", debugstr_w((LPCWSTR)pBuffer));
        cf = RegisterClipboardFormatW((LPCWSTR)pBuffer);
        pBuffer += (len + 1) * sizeof(WCHAR);
        if (!cf)
            RaiseException(DV_E_CLIPFORMAT, 0, 0, NULL);
        *pCF = cf;
    }
    else
        /* code not really appropriate, but nearest I can find */
        RaiseException(RPC_S_INVALID_TAG, 0, 0, NULL);
    return pBuffer;
}

/******************************************************************************
 *           CLIPFORMAT_UserFree [OLE32.@]
 *
 * Frees an unmarshaled clip format.
 *
 * PARAMS
 *  pFlags  [I] Flags. See notes.
 *  pCF     [I] Clip format to free.
 *
 * RETURNS
 *  The end of the marshaled data in the buffer.
 *
 * NOTES
 *  Even though the function is documented to take a pointer to an unsigned
 *  long in pFlags, it actually takes a pointer to a USER_MARSHAL_CB
 *  structure, of which the first parameter is an unsigned long.
 *  This function is only intended to be called by the RPC runtime.
 */
void __RPC_USER CLIPFORMAT_UserFree(ULONG *pFlags, CLIPFORMAT *pCF)
{
    /* there is no inverse of the RegisterClipboardFormat function,
     * so nothing to do */
}

static ULONG __RPC_USER handle_UserSize(ULONG *pFlags, ULONG StartingSize, HANDLE *handle)
{
    if (LOWORD(*pFlags) == MSHCTX_DIFFERENTMACHINE)
    {
        ERR("can't remote a local handle\n");
        RaiseException(RPC_S_INVALID_TAG, 0, 0, NULL);
        return StartingSize;
    }
    return StartingSize + sizeof(RemotableHandle);
}

static unsigned char * __RPC_USER handle_UserMarshal(ULONG *pFlags, unsigned char *pBuffer, HANDLE *handle)
{
    RemotableHandle *remhandle = (RemotableHandle *)pBuffer;
    if (LOWORD(*pFlags) == MSHCTX_DIFFERENTMACHINE)
    {
        ERR("can't remote a local handle\n");
        RaiseException(RPC_S_INVALID_TAG, 0, 0, NULL);
        return pBuffer;
    }
    remhandle->fContext = WDT_INPROC_CALL;
    remhandle->u.hInproc = (LONG_PTR)*handle;
    return pBuffer + sizeof(RemotableHandle);
}

static unsigned char * __RPC_USER handle_UserUnmarshal(ULONG *pFlags, unsigned char *pBuffer, HANDLE *handle)
{
    RemotableHandle *remhandle = (RemotableHandle *)pBuffer;
    if (remhandle->fContext != WDT_INPROC_CALL)
        RaiseException(RPC_X_BAD_STUB_DATA, 0, 0, NULL);
    *handle = (HANDLE)remhandle->u.hInproc;
    return pBuffer + sizeof(RemotableHandle);
}

static void __RPC_USER handle_UserFree(ULONG *pFlags, HANDLE *phMenu)
{
    /* nothing to do */
}

#define IMPL_WIREM_HANDLE(type) \
    ULONG __RPC_USER type##_UserSize(ULONG *pFlags, ULONG StartingSize, type *handle) \
    { \
        TRACE("(%s, %d, %p\n", debugstr_user_flags(pFlags), StartingSize, handle); \
        return handle_UserSize(pFlags, StartingSize, (HANDLE *)handle); \
    } \
    \
    unsigned char * __RPC_USER type##_UserMarshal(ULONG *pFlags, unsigned char *pBuffer, type *handle) \
    { \
        TRACE("(%s, %p, &%p\n", debugstr_user_flags(pFlags), pBuffer, *handle); \
        return handle_UserMarshal(pFlags, pBuffer, (HANDLE *)handle); \
    } \
    \
    unsigned char * __RPC_USER type##_UserUnmarshal(ULONG *pFlags, unsigned char *pBuffer, type *handle) \
    { \
        TRACE("(%s, %p, %p\n", debugstr_user_flags(pFlags), pBuffer, handle); \
        return handle_UserUnmarshal(pFlags, pBuffer, (HANDLE *)handle); \
    } \
    \
    void __RPC_USER type##_UserFree(ULONG *pFlags, type *handle) \
    { \
        TRACE("(%s, &%p\n", debugstr_user_flags(pFlags), *handle); \
        return handle_UserFree(pFlags, (HANDLE *)handle); \
    }

IMPL_WIREM_HANDLE(HACCEL)
IMPL_WIREM_HANDLE(HMENU)
IMPL_WIREM_HANDLE(HWND)

/******************************************************************************
 *           HGLOBAL_UserSize [OLE32.@]
 */
ULONG __RPC_USER HGLOBAL_UserSize(ULONG *pFlags, ULONG StartingSize, HGLOBAL *phGlobal)
{
    ULONG size = StartingSize;

    TRACE("(%s, %d, %p\n", debugstr_user_flags(pFlags), StartingSize, phGlobal);

    ALIGN_LENGTH(size, 3);

    size += sizeof(ULONG);

    if (LOWORD(*pFlags == MSHCTX_INPROC))
        size += sizeof(HGLOBAL);
    else
    {
        size += sizeof(ULONG);
        if (*phGlobal)
        {
            SIZE_T ret;
            size += 3 * sizeof(ULONG);
            ret = GlobalSize(*phGlobal);
            size += (ULONG)ret;
        }
    }
    
    return size;
}

unsigned char * __RPC_USER HGLOBAL_UserMarshal(ULONG *pFlags, unsigned char *pBuffer, HGLOBAL *phGlobal)
{
    TRACE("(%s, %p, &%p\n", debugstr_user_flags(pFlags), pBuffer, *phGlobal);

    ALIGN_POINTER(pBuffer, 3);

    if (LOWORD(*pFlags == MSHCTX_INPROC))
    {
        if (sizeof(*phGlobal) == 8)
            *(ULONG *)pBuffer = WDT_INPROC64_CALL;
        else
            *(ULONG *)pBuffer = WDT_INPROC_CALL;
        pBuffer += sizeof(ULONG);
        *(HGLOBAL *)pBuffer = *phGlobal;
        pBuffer += sizeof(HGLOBAL);
    }
    else
    {
        *(ULONG *)pBuffer = WDT_REMOTE_CALL;
        pBuffer += sizeof(ULONG);
        *(ULONG *)pBuffer = (ULONG)*phGlobal;
        pBuffer += sizeof(ULONG);
        if (*phGlobal)
        {
            const unsigned char *memory;
            SIZE_T size = GlobalSize(*phGlobal);
            *(ULONG *)pBuffer = (ULONG)size;
            pBuffer += sizeof(ULONG);
            *(ULONG *)pBuffer = (ULONG)*phGlobal;
            pBuffer += sizeof(ULONG);
            *(ULONG *)pBuffer = (ULONG)size;
            pBuffer += sizeof(ULONG);

            memory = GlobalLock(*phGlobal);
            memcpy(pBuffer, memory, size);
            pBuffer += size;
            GlobalUnlock(*phGlobal);
        }
    }

    return pBuffer;
}

unsigned char * __RPC_USER HGLOBAL_UserUnmarshal(ULONG *pFlags, unsigned char *pBuffer, HGLOBAL *phGlobal)
{
    ULONG fContext;

    TRACE("(%s, %p, &%p\n", debugstr_user_flags(pFlags), pBuffer, *phGlobal);

    ALIGN_POINTER(pBuffer, 3);

    fContext = *(ULONG *)pBuffer;
    pBuffer += sizeof(ULONG);

    if (((fContext == WDT_INPROC_CALL) && (sizeof(*phGlobal) < 8)) ||
        ((fContext == WDT_INPROC64_CALL) && (sizeof(*phGlobal) == 8)))
    {
        *phGlobal = *(HGLOBAL *)pBuffer;
        pBuffer += sizeof(*phGlobal);
    }
    else if (fContext == WDT_REMOTE_CALL)
    {
        ULONG handle;

        handle = *(ULONG *)pBuffer;
        pBuffer += sizeof(ULONG);

        if (handle)
        {
            ULONG size;
            void *memory;

            size = *(ULONG *)pBuffer;
            pBuffer += sizeof(ULONG);
            /* redundancy is bad - it means you have to check consistency like
             * this: */
            if (*(ULONG *)pBuffer != handle)
            {
                RaiseException(RPC_X_BAD_STUB_DATA, 0, 0, NULL);
                return pBuffer;
            }
            pBuffer += sizeof(ULONG);
            /* redundancy is bad - it means you have to check consistency like
             * this: */
            if (*(ULONG *)pBuffer != size)
            {
                RaiseException(RPC_X_BAD_STUB_DATA, 0, 0, NULL);
                return pBuffer;
            }
            pBuffer += sizeof(ULONG);

            /* FIXME: check size is not too big */

            *phGlobal = GlobalAlloc(GMEM_MOVEABLE, size);
            memory = GlobalLock(*phGlobal);
            memcpy(memory, pBuffer, size);
            pBuffer += size;
            GlobalUnlock(*phGlobal);
        }
        else
            *phGlobal = NULL;
    }
    else
        RaiseException(RPC_S_INVALID_TAG, 0, 0, NULL);

    return pBuffer;
}

/******************************************************************************
 *           HGLOBAL_UserFree [OLE32.@]
 */
void __RPC_USER HGLOBAL_UserFree(ULONG *pFlags, HGLOBAL *phGlobal)
{
    TRACE("(%s, &%p\n", debugstr_user_flags(pFlags), *phGlobal);

    if (LOWORD(*pFlags != MSHCTX_INPROC) && *phGlobal)
        GlobalFree(*phGlobal);
}

ULONG __RPC_USER HBITMAP_UserSize(ULONG *pFlags, ULONG StartingSize, HBITMAP *phBmp)
{
    FIXME(":stub\n");
    return StartingSize;
}

unsigned char * __RPC_USER HBITMAP_UserMarshal(ULONG *pFlags, unsigned char *pBuffer, HBITMAP *phBmp)
{
    FIXME(":stub\n");
    return pBuffer;
}

unsigned char * __RPC_USER HBITMAP_UserUnmarshal(ULONG *pFlags, unsigned char *pBuffer, HBITMAP *phBmp)
{
    FIXME(":stub\n");
    return pBuffer;
}

void __RPC_USER HBITMAP_UserFree(ULONG *pFlags, HBITMAP *phBmp)
{
    FIXME(":stub\n");
}

ULONG __RPC_USER HDC_UserSize(ULONG *pFlags, ULONG StartingSize, HDC *phdc)
{
    FIXME(":stub\n");
    return StartingSize;
}

unsigned char * __RPC_USER HDC_UserMarshal(ULONG *pFlags, unsigned char *pBuffer, HDC *phdc)
{
    FIXME(":stub\n");
    return pBuffer;
}

unsigned char * __RPC_USER HDC_UserUnmarshal(ULONG *pFlags, unsigned char *pBuffer, HDC *phdc)
{
    FIXME(":stub\n");
    return pBuffer;
}

void __RPC_USER HDC_UserFree(ULONG *pFlags, HDC *phdc)
{
    FIXME(":stub\n");
}

ULONG __RPC_USER HPALETTE_UserSize(ULONG *pFlags, ULONG StartingSize, HPALETTE *phPal)
{
    FIXME(":stub\n");
    return StartingSize;
}

unsigned char * __RPC_USER HPALETTE_UserMarshal(ULONG *pFlags, unsigned char *pBuffer, HPALETTE *phPal)
{
    FIXME(":stub\n");
    return pBuffer;
}

unsigned char * __RPC_USER HPALETTE_UserUnmarshal(ULONG *pFlags, unsigned char *pBuffer, HPALETTE *phPal)
{
    FIXME(":stub\n");
    return pBuffer;
}

void __RPC_USER HPALETTE_UserFree(ULONG *pFlags, HPALETTE *phPal)
{
    FIXME(":stub\n");
}


ULONG __RPC_USER HENHMETAFILE_UserSize(ULONG *pFlags, ULONG StartingSize, HENHMETAFILE *phEmf)
{
    ULONG size = StartingSize;

    TRACE("(%s, %d, %p\n", debugstr_user_flags(pFlags), StartingSize, *phEmf);

    size += sizeof(ULONG);
    if (LOWORD(*pFlags) == MSHCTX_INPROC)
        size += sizeof(ULONG_PTR);
    else
    {
        size += sizeof(ULONG);

        if (*phEmf)
        {
            UINT emfsize;
    
            size += 2 * sizeof(ULONG);
            emfsize = GetEnhMetaFileBits(*phEmf, 0, NULL);
            size += emfsize;
        }
    }

    return size;
}

unsigned char * __RPC_USER HENHMETAFILE_UserMarshal(ULONG *pFlags, unsigned char *pBuffer, HENHMETAFILE *phEmf)
{
    TRACE("(%s, %p, &%p\n", debugstr_user_flags(pFlags), pBuffer, *phEmf);

    if (LOWORD(*pFlags) == MSHCTX_INPROC)
    {
        if (sizeof(*phEmf) == 8)
            *(ULONG *)pBuffer = WDT_INPROC64_CALL;
        else
            *(ULONG *)pBuffer = WDT_INPROC_CALL;
        pBuffer += sizeof(ULONG);
        *(HENHMETAFILE *)pBuffer = *phEmf;
        pBuffer += sizeof(HENHMETAFILE);
    }
    else
    {
        *(ULONG *)pBuffer = WDT_REMOTE_CALL;
        pBuffer += sizeof(ULONG);
        *(ULONG *)pBuffer = (ULONG)(ULONG_PTR)*phEmf;
        pBuffer += sizeof(ULONG);
    
        if (*phEmf)
        {
            UINT emfsize = GetEnhMetaFileBits(*phEmf, 0, NULL);
    
            *(ULONG *)pBuffer = emfsize;
            pBuffer += sizeof(ULONG);
            *(ULONG *)pBuffer = emfsize;
            pBuffer += sizeof(ULONG);
            GetEnhMetaFileBits(*phEmf, emfsize, pBuffer);
            pBuffer += emfsize;
        }
    }

    return pBuffer;
}

unsigned char * __RPC_USER HENHMETAFILE_UserUnmarshal(ULONG *pFlags, unsigned char *pBuffer, HENHMETAFILE *phEmf)
{
    ULONG fContext;

    TRACE("(%s, %p, %p\n", debugstr_user_flags(pFlags), pBuffer, phEmf);

    fContext = *(ULONG *)pBuffer;
    pBuffer += sizeof(ULONG);

    if (((fContext == WDT_INPROC_CALL) && (sizeof(*phEmf) < 8)) ||
        ((fContext == WDT_INPROC64_CALL) && (sizeof(*phEmf) == 8)))
    {
        *phEmf = *(HENHMETAFILE *)pBuffer;
        pBuffer += sizeof(*phEmf);
    }
    else if (fContext == WDT_REMOTE_CALL)
    {
        ULONG handle;

        handle = *(ULONG *)pBuffer;
        pBuffer += sizeof(ULONG);

        if (handle)
        {
            ULONG size;
            size = *(ULONG *)pBuffer;
            pBuffer += sizeof(ULONG);
            if (size != *(ULONG *)pBuffer)
            {
                RaiseException(RPC_X_BAD_STUB_DATA, 0, 0, NULL);
                return pBuffer;
            }
            pBuffer += sizeof(ULONG);
            *phEmf = SetEnhMetaFileBits(size, pBuffer);
            pBuffer += size;
        }
        else 
            *phEmf = NULL;
    }
    else
        RaiseException(RPC_S_INVALID_TAG, 0, 0, NULL);

    return pBuffer;
}

void __RPC_USER HENHMETAFILE_UserFree(ULONG *pFlags, HENHMETAFILE *phEmf)
{
    TRACE("(%s, &%p\n", debugstr_user_flags(pFlags), *phEmf);

    if (LOWORD(*pFlags) != MSHCTX_INPROC)
        DeleteEnhMetaFile(*phEmf);
}

ULONG __RPC_USER STGMEDIUM_UserSize(ULONG *pFlags, ULONG StartingSize, STGMEDIUM *pStgMedium)
{
    ULONG size = StartingSize;

    TRACE("(%s, %d, %p\n", debugstr_user_flags(pFlags), StartingSize, pStgMedium);

    ALIGN_LENGTH(size, 3);

    size += 2 * sizeof(DWORD);
    if (pStgMedium->tymed != TYMED_NULL)
        size += sizeof(DWORD);

    switch (pStgMedium->tymed)
    {
    case TYMED_NULL:
        TRACE("TYMED_NULL\n");
        break;
    case TYMED_HGLOBAL:
        TRACE("TYMED_HGLOBAL\n");
        size = HGLOBAL_UserSize(pFlags, size, &pStgMedium->u.hGlobal);
        break;
    case TYMED_FILE:
        FIXME("TYMED_FILE\n");
        break;
    case TYMED_ISTREAM:
        FIXME("TYMED_ISTREAM\n");
        break;
    case TYMED_ISTORAGE:
        FIXME("TYMED_ISTORAGE\n");
        break;
    case TYMED_GDI:
        FIXME("TYMED_GDI\n");
        break;
    case TYMED_MFPICT:
        FIXME("TYMED_MFPICT\n");
        break;
    case TYMED_ENHMF:
        TRACE("TYMED_ENHMF\n");
        size = HENHMETAFILE_UserSize(pFlags, size, &pStgMedium->u.hEnhMetaFile);
        break;
    default:
        RaiseException(DV_E_TYMED, 0, 0, NULL);
    }

    if (pStgMedium->pUnkForRelease)
        FIXME("buffer size pUnkForRelease\n");

    return size;
}

unsigned char * __RPC_USER STGMEDIUM_UserMarshal(ULONG *pFlags, unsigned char *pBuffer, STGMEDIUM *pStgMedium)
{
    TRACE("(%s, %p, %p\n", debugstr_user_flags(pFlags), pBuffer, pStgMedium);

    ALIGN_POINTER(pBuffer, 3);

    *(DWORD *)pBuffer = pStgMedium->tymed;
    pBuffer += sizeof(DWORD);
    if (pStgMedium->tymed != TYMED_NULL)
    {
        *(DWORD *)pBuffer = (DWORD)(DWORD_PTR)pStgMedium->u.pstg;
        pBuffer += sizeof(DWORD);
    }
    *(DWORD *)pBuffer = (DWORD)(DWORD_PTR)pStgMedium->pUnkForRelease;
    pBuffer += sizeof(DWORD);

    switch (pStgMedium->tymed)
    {
    case TYMED_NULL:
        TRACE("TYMED_NULL\n");
        break;
    case TYMED_HGLOBAL:
        TRACE("TYMED_HGLOBAL\n");
        pBuffer = HGLOBAL_UserMarshal(pFlags, pBuffer, &pStgMedium->u.hGlobal);
        break;
    case TYMED_FILE:
        FIXME("TYMED_FILE\n");
        break;
    case TYMED_ISTREAM:
        FIXME("TYMED_ISTREAM\n");
        break;
    case TYMED_ISTORAGE:
        FIXME("TYMED_ISTORAGE\n");
        break;
    case TYMED_GDI:
        FIXME("TYMED_GDI\n");
        break;
    case TYMED_MFPICT:
        FIXME("TYMED_MFPICT\n");
        break;
    case TYMED_ENHMF:
        TRACE("TYMED_ENHMF\n");
        pBuffer = HENHMETAFILE_UserMarshal(pFlags, pBuffer, &pStgMedium->u.hEnhMetaFile);
        break;
    default:
        RaiseException(DV_E_TYMED, 0, 0, NULL);
    }

    if (pStgMedium->pUnkForRelease)
        FIXME("marshal pUnkForRelease\n");

    return pBuffer;
}

unsigned char * __RPC_USER STGMEDIUM_UserUnmarshal(ULONG *pFlags, unsigned char *pBuffer, STGMEDIUM *pStgMedium)
{
    DWORD content;
    DWORD releaseunk;

    ALIGN_POINTER(pBuffer, 3);

    TRACE("(%s, %p, %p\n", debugstr_user_flags(pFlags), pBuffer, pStgMedium);

    pStgMedium->tymed = *(DWORD *)pBuffer;
    pBuffer += sizeof(DWORD);
    if (pStgMedium->tymed != TYMED_NULL)
    {
        content = *(DWORD *)pBuffer;
        pBuffer += sizeof(DWORD);
    }
    releaseunk = *(DWORD *)pBuffer;
    pBuffer += sizeof(DWORD);

    switch (pStgMedium->tymed)
    {
    case TYMED_NULL:
        TRACE("TYMED_NULL\n");
        break;
    case TYMED_HGLOBAL:
        TRACE("TYMED_HGLOBAL\n");
        pBuffer = HGLOBAL_UserUnmarshal(pFlags, pBuffer, &pStgMedium->u.hGlobal);
        break;
    case TYMED_FILE:
        FIXME("TYMED_FILE\n");
        break;
    case TYMED_ISTREAM:
        FIXME("TYMED_ISTREAM\n");
        break;
    case TYMED_ISTORAGE:
        FIXME("TYMED_ISTORAGE\n");
        break;
    case TYMED_GDI:
        FIXME("TYMED_GDI\n");
        break;
    case TYMED_MFPICT:
        FIXME("TYMED_MFPICT\n");
        break;
    case TYMED_ENHMF:
        TRACE("TYMED_ENHMF\n");
        pBuffer = HENHMETAFILE_UserUnmarshal(pFlags, pBuffer, &pStgMedium->u.hEnhMetaFile);
        break;
    default:
        RaiseException(DV_E_TYMED, 0, 0, NULL);
    }

    pStgMedium->pUnkForRelease = NULL;
    if (releaseunk)
        FIXME("unmarshal pUnkForRelease\n");

    return pBuffer;
}

void __RPC_USER STGMEDIUM_UserFree(ULONG *pFlags, STGMEDIUM *pStgMedium)
{
    TRACE("(%s, %p\n", debugstr_user_flags(pFlags), pStgMedium);

    ReleaseStgMedium(pStgMedium);
}

ULONG __RPC_USER ASYNC_STGMEDIUM_UserSize(ULONG *pFlags, ULONG StartingSize, ASYNC_STGMEDIUM *pStgMedium)
{
    FIXME(":stub\n");
    return StartingSize;
}

unsigned char * __RPC_USER ASYNC_STGMEDIUM_UserMarshal(  ULONG *pFlags, unsigned char *pBuffer, ASYNC_STGMEDIUM *pStgMedium)
{
    FIXME(":stub\n");
    return pBuffer;
}

unsigned char * __RPC_USER ASYNC_STGMEDIUM_UserUnmarshal(ULONG *pFlags, unsigned char *pBuffer, ASYNC_STGMEDIUM *pStgMedium)
{
    FIXME(":stub\n");
    return pBuffer;
}

void __RPC_USER ASYNC_STGMEDIUM_UserFree(ULONG *pFlags, ASYNC_STGMEDIUM *pStgMedium)
{
    FIXME(":stub\n");
}

ULONG __RPC_USER FLAG_STGMEDIUM_UserSize(ULONG *pFlags, ULONG StartingSize, FLAG_STGMEDIUM *pStgMedium)
{
    FIXME(":stub\n");
    return StartingSize;
}

unsigned char * __RPC_USER FLAG_STGMEDIUM_UserMarshal(  ULONG *pFlags, unsigned char *pBuffer, FLAG_STGMEDIUM *pStgMedium)
{
    FIXME(":stub\n");
    return pBuffer;
}

unsigned char * __RPC_USER FLAG_STGMEDIUM_UserUnmarshal(ULONG *pFlags, unsigned char *pBuffer, FLAG_STGMEDIUM *pStgMedium)
{
    FIXME(":stub\n");
    return pBuffer;
}

void __RPC_USER FLAG_STGMEDIUM_UserFree(ULONG *pFlags, FLAG_STGMEDIUM *pStgMedium)
{
    FIXME(":stub\n");
}

ULONG __RPC_USER SNB_UserSize(ULONG *pFlags, ULONG StartingSize, SNB *pSnb)
{
    FIXME(":stub\n");
    return StartingSize;
}

unsigned char * __RPC_USER SNB_UserMarshal(  ULONG *pFlags, unsigned char *pBuffer, SNB *pSnb)
{
    FIXME(":stub\n");
    return pBuffer;
}

unsigned char * __RPC_USER SNB_UserUnmarshal(ULONG *pFlags, unsigned char *pBuffer, SNB *pSnb)
{
    FIXME(":stub\n");
    return pBuffer;
}

void __RPC_USER SNB_UserFree(ULONG *pFlags, SNB *pSnb)
{
    FIXME(":stub\n");
}
