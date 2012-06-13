/*
 * Copyright 2011 Piotr Caban for CodeWeavers
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
#include <stdio.h>
#include <limits.h>

#include "msvcp90.h"
#include "windef.h"
#include "winbase.h"
#include "wine/debug.h"
WINE_DEFAULT_DEBUG_CHANNEL(msvcp90);

typedef enum {
    IOSTATE_goodbit   = 0x00,
    IOSTATE_eofbit    = 0x01,
    IOSTATE_failbit   = 0x02,
    IOSTATE_badbit    = 0x04,
    IOSTATE__Hardfail = 0x10,
    IOSTATE_mask      = 0x17
} IOSB_iostate;

typedef enum {
    FMTFLAG_skipws      = 0x0001,
    FMTFLAG_unitbuf     = 0x0002,
    FMTFLAG_uppercase   = 0x0004,
    FMTFLAG_showbase    = 0x0008,
    FMTFLAG_showpoint   = 0x0010,
    FMTFLAG_showpos     = 0x0020,
    FMTFLAG_left        = 0x0040,
    FMTFLAG_right       = 0x0080,
    FMTFLAG_internal    = 0x0100,
    FMTFLAG_dec         = 0x0200,
    FMTFLAG_oct         = 0x0400,
    FMTFLAG_hex         = 0x0800,
    FMTFLAG_scientific  = 0x1000,
    FMTFLAG_fixed       = 0x2000,
    FMTFLAG_hexfloat    = 0x3000,
    FMTFLAG_boolalpha   = 0x4000,
    FMTFLAG_stdio       = 0x8000,
    FMTFLAG_adjustfield = FMTFLAG_left|FMTFLAG_right|FMTFLAG_internal,
    FMTFLAG_basefield   = FMTFLAG_dec|FMTFLAG_oct|FMTFLAG_hex,
    FMTFLAG_floadfield  = FMTFLAG_scientific|FMTFLAG_fixed,
    FMTFLAG_mask        = 0xffff
} IOSB_fmtflags;

typedef enum {
    OPENMODE_in         = 0x01,
    OPENMODE_out        = 0x02,
    OPENMODE_ate        = 0x04,
    OPENMODE_app        = 0x08,
    OPENMODE_trunc      = 0x10,
    OPENMODE__Nocreate  = 0x40,
    OPENMODE__Noreplace = 0x80,
    OPENMODE_binray     = 0x20,
    OPENMODE_mask       = 0xff
} IOSB_openmode;

typedef enum {
    SEEKDIR_beg  = 0x1,
    SEEKDIR_cur  = 0x2,
    SEEKDIR_end  = 0x3,
    SEEKDIR_mask = 0x3
} IOSB_seekdir;

typedef struct _iosarray {
    struct _iosarray *next;
    int index;
    int long_val;
    void *ptr_val;
} IOS_BASE_iosarray;

typedef enum {
    EVENT_erase_event,
    EVENT_imbue_event,
    EVENT_copyfmt_event
} IOS_BASE_event;

struct _ios_base;
typedef void (CDECL *IOS_BASE_event_callback)(IOS_BASE_event, struct _ios_base*, int);
typedef struct _fnarray {
    struct _fnarray *next;
    int index;
    IOS_BASE_event_callback event_handler;
} IOS_BASE_fnarray;

/* ?_Index@ios_base@std@@0HA */
int ios_base_Index = 0;
/* ?_Sync@ios_base@std@@0_NA */
MSVCP_bool ios_base_Sync = FALSE;

typedef struct _ios_base {
    const vtable_ptr *vtable;
    MSVCP_size_t stdstr;
    IOSB_iostate state;
    IOSB_iostate except;
    IOSB_fmtflags fmtfl;
    streamsize prec;
    streamsize wide;
    IOS_BASE_iosarray *arr;
    IOS_BASE_fnarray *calls;
    locale *loc;
} ios_base;

typedef struct {
    streamoff off;
    __int64 DECLSPEC_ALIGN(8) pos;
    int state;
} fpos_int;

static inline const char* debugstr_fpos_int(fpos_int *fpos)
{
    return wine_dbg_sprintf("fpos(%ld %s %d)", fpos->off, wine_dbgstr_longlong(fpos->pos), fpos->state);
}

typedef struct {
    const vtable_ptr *vtable;
    mutex lock;
    char *rbuf;
    char *wbuf;
    char **prbuf;
    char **pwbuf;
    char *rpos;
    char *wpos;
    char **prpos;
    char **pwpos;
    int rsize;
    int wsize;
    int *prsize;
    int *pwsize;
    locale *loc;
} basic_streambuf_char;

typedef struct {
    const vtable_ptr *vtable;
    mutex lock;
    wchar_t *rbuf;
    wchar_t *wbuf;
    wchar_t **prbuf;
    wchar_t **pwbuf;
    wchar_t *rpos;
    wchar_t *wpos;
    wchar_t **prpos;
    wchar_t **pwpos;
    int rsize;
    int wsize;
    int *prsize;
    int *pwsize;
    locale *loc;
} basic_streambuf_wchar;

typedef struct {
    ios_base base;
    basic_streambuf_char *strbuf;
    struct _basic_ostream_char *stream;
    char fillch;
} basic_ios_char;

typedef struct {
    ios_base base;
    basic_streambuf_wchar *strbuf;
    struct _basic_ostream_wchar *stream;
    wchar_t fillch;
} basic_ios_wchar;

typedef struct _basic_ostream_char {
    const int *vbtable;
    /* virtual inheritance
     * basic_ios_char base;
     */
} basic_ostream_char;

typedef struct _basic_ostream_wchar {
    const int *vbtable;
    /* virtual inheritance
     * basic_ios_char base;
     */
} basic_ostream_wchar;

typedef struct {
    const int *vbtable;
    streamsize count;
    /* virtual inheritance
     * basic_ios_char base;
     */
} basic_istream_char;

extern const vtable_ptr MSVCP_iosb_vtable;

/* ??_7ios_base@std@@6B@ */
extern const vtable_ptr MSVCP_ios_base_vtable;

/* ??_7?$basic_ios@DU?$char_traits@D@std@@@std@@6B@ */
extern const vtable_ptr MSVCP_basic_ios_char_vtable;

/* ??_7?$basic_ios@_WU?$char_traits@_W@std@@@std@@6B@ */
extern const vtable_ptr MSVCP_basic_ios_wchar_vtable;

/* ??_7?$basic_ios@GU?$char_traits@G@std@@@std@@6B@ */
extern const vtable_ptr MSVCP_basic_ios_short_vtable;

/* ??_7?$basic_streambuf@DU?$char_traits@D@std@@@std@@6B@ */
extern const vtable_ptr MSVCP_basic_streambuf_char_vtable;

/* ??_7?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@6B@ */
extern const vtable_ptr MSVCP_basic_streambuf_wchar_vtable;

/* ??_7?$basic_streambuf@GU?$char_traits@G@std@@@std@@6B@ */
extern const vtable_ptr MSVCP_basic_streambuf_short_vtable;

/* ??_8?$basic_ostream@DU?$char_traits@D@std@@@std@@7B@ */
const int basic_ostream_char_vbtable[] = {0, sizeof(basic_ostream_char)};
/* ??_7?$basic_ostream@DU?$char_traits@D@std@@@std@@6B@ */
extern const vtable_ptr MSVCP_basic_ostream_char_vtable;

/* ??_8?$basic_istream@DU?$char_traits@D@std@@@std@@7B@ */
const int basic_istream_char_vbtable[] = {0, sizeof(basic_istream_char)};
/* ??_7?$basic_istream@DU?$char_traits@D@std@@@std@@6B@ */
extern const vtable_ptr MSVCP_basic_istream_char_vtable;

DEFINE_RTTI_DATA(iosb, 0, 0, NULL, NULL, NULL, ".?AV?$_Iosb@H@std@@");
DEFINE_RTTI_DATA(ios_base, 0, 1, &iosb_rtti_base_descriptor, NULL, NULL, ".?AV?$_Iosb@H@std@@");
DEFINE_RTTI_DATA(basic_ios_char, 0, 2, &ios_base_rtti_base_descriptor, &iosb_rtti_base_descriptor,
        NULL, ".?AV?$basic_ios@DU?$char_traits@D@std@@@std@@");
DEFINE_RTTI_DATA(basic_ios_wchar, 0, 2, &ios_base_rtti_base_descriptor, &iosb_rtti_base_descriptor,
        NULL, ".?AV?$basic_ios@_WU?$char_traits@_W@std@@@std@@");
DEFINE_RTTI_DATA(basic_ios_short, 0, 2, &ios_base_rtti_base_descriptor, &iosb_rtti_base_descriptor,
        NULL, ".?AV?$basic_ios@GU?$char_traits@G@std@@@std@@");
DEFINE_RTTI_DATA(basic_streambuf_char, 0, 0, NULL, NULL, NULL,
        ".?AV?$basic_streambuf@DU?$char_traits@D@std@@@std@@");
DEFINE_RTTI_DATA(basic_streambuf_wchar, 0, 0, NULL, NULL, NULL,
        ".?AV?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@");
DEFINE_RTTI_DATA(basic_streambuf_short, 0, 0, NULL, NULL, NULL,
        ".?AV?$basic_streambuf@GU?$char_traits@G@std@@@std@@");
DEFINE_RTTI_DATA(basic_ostream_char, sizeof(basic_ostream_char), 3, &basic_ios_char_rtti_base_descriptor,
        &ios_base_rtti_base_descriptor, &iosb_rtti_base_descriptor,
        ".?AV?$basic_ostream@DU?$char_traits@D@std@@@std@@");
DEFINE_RTTI_DATA(basic_istream_char, sizeof(basic_istream_char), 3, &basic_ios_char_rtti_base_descriptor,
        &ios_base_rtti_base_descriptor, &iosb_rtti_base_descriptor,
        ".?AV?$basic_istream@DU?$char_traits@D@std@@@std@@");

#ifndef __GNUC__
void __asm_dummy_vtables(void) {
#endif
    __ASM_VTABLE(iosb, "");
    __ASM_VTABLE(ios_base, "");
    __ASM_VTABLE(basic_ios_char, "");
    __ASM_VTABLE(basic_ios_wchar, "");
    __ASM_VTABLE(basic_ios_short, "");
    __ASM_VTABLE(basic_streambuf_char,
            VTABLE_ADD_FUNC(basic_streambuf_char_overflow)
            VTABLE_ADD_FUNC(basic_streambuf_char_pbackfail)
            VTABLE_ADD_FUNC(basic_streambuf_char_showmanyc)
            VTABLE_ADD_FUNC(basic_streambuf_char_underflow)
            VTABLE_ADD_FUNC(basic_streambuf_char_uflow)
            VTABLE_ADD_FUNC(basic_streambuf_char_xsgetn)
            VTABLE_ADD_FUNC(basic_streambuf_char__Xsgetn_s)
            VTABLE_ADD_FUNC(basic_streambuf_char_xsputn)
            VTABLE_ADD_FUNC(basic_streambuf_char_seekoff)
            VTABLE_ADD_FUNC(basic_streambuf_char_seekpos)
            VTABLE_ADD_FUNC(basic_streambuf_char_setbuf)
            VTABLE_ADD_FUNC(basic_streambuf_char_sync)
            VTABLE_ADD_FUNC(basic_streambuf_char_imbue));
    __ASM_VTABLE(basic_streambuf_wchar,
            VTABLE_ADD_FUNC(basic_streambuf_wchar_overflow)
            VTABLE_ADD_FUNC(basic_streambuf_wchar_pbackfail)
            VTABLE_ADD_FUNC(basic_streambuf_wchar_showmanyc)
            VTABLE_ADD_FUNC(basic_streambuf_wchar_underflow)
            VTABLE_ADD_FUNC(basic_streambuf_wchar_uflow)
            VTABLE_ADD_FUNC(basic_streambuf_wchar_xsgetn)
            VTABLE_ADD_FUNC(basic_streambuf_wchar__Xsgetn_s)
            VTABLE_ADD_FUNC(basic_streambuf_wchar_xsputn)
            VTABLE_ADD_FUNC(basic_streambuf_wchar_seekoff)
            VTABLE_ADD_FUNC(basic_streambuf_wchar_seekpos)
            VTABLE_ADD_FUNC(basic_streambuf_wchar_setbuf)
            VTABLE_ADD_FUNC(basic_streambuf_wchar_sync)
            VTABLE_ADD_FUNC(basic_streambuf_wchar_imbue));
    __ASM_VTABLE(basic_streambuf_short,
            VTABLE_ADD_FUNC(basic_streambuf_wchar_overflow)
            VTABLE_ADD_FUNC(basic_streambuf_wchar_pbackfail)
            VTABLE_ADD_FUNC(basic_streambuf_wchar_showmanyc)
            VTABLE_ADD_FUNC(basic_streambuf_wchar_underflow)
            VTABLE_ADD_FUNC(basic_streambuf_wchar_uflow)
            VTABLE_ADD_FUNC(basic_streambuf_wchar_xsgetn)
            VTABLE_ADD_FUNC(basic_streambuf_wchar__Xsgetn_s)
            VTABLE_ADD_FUNC(basic_streambuf_wchar_xsputn)
            VTABLE_ADD_FUNC(basic_streambuf_wchar_seekoff)
            VTABLE_ADD_FUNC(basic_streambuf_wchar_seekpos)
            VTABLE_ADD_FUNC(basic_streambuf_wchar_setbuf)
            VTABLE_ADD_FUNC(basic_streambuf_wchar_sync)
            VTABLE_ADD_FUNC(basic_streambuf_wchar_imbue));
    __ASM_VTABLE(basic_ostream_char, "");
    __ASM_VTABLE(basic_istream_char, "");
#ifndef __GNUC__
}
#endif

/* ?setp@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IAEXPAD00@Z */
/* ?setp@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEAAXPEAD00@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_setp_next, 16)
void __thiscall basic_streambuf_char_setp_next(basic_streambuf_char *this, char *first, char *next, char *last)
{
    TRACE("(%p %p %p %p)\n", this, first, next, last);

    this->wbuf = first;
    this->wpos = next;
    this->wsize = last-next;
}

/* ?setp@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IAEXPAD0@Z */
/* ?setp@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEAAXPEAD0@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_setp, 12)
void __thiscall basic_streambuf_char_setp(basic_streambuf_char *this, char *first, char *last)
{
    basic_streambuf_char_setp_next(this, first, first, last);
}

/* ?setg@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IAEXPAD00@Z */
/* ?setg@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEAAXPEAD00@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_setg, 16)
void __thiscall basic_streambuf_char_setg(basic_streambuf_char *this, char *first, char *next, char *last)
{
    TRACE("(%p %p %p %p)\n", this, first, next, last);

    this->rbuf = first;
    this->rpos = next;
    this->rsize = last-next;
}

/* ?_Init@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IAEXXZ */
/* ?_Init@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEAAXXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char__Init_empty, 4)
void __thiscall basic_streambuf_char__Init_empty(basic_streambuf_char *this)
{
    TRACE("(%p)\n", this);

    this->prbuf = &this->rbuf;
    this->pwbuf = &this->wbuf;
    this->prpos = &this->rpos;
    this->pwpos = &this->wpos;
    this->prsize = &this->rsize;
    this->pwsize = &this->wsize;

    basic_streambuf_char_setp(this, NULL, NULL);
    basic_streambuf_char_setg(this, NULL, NULL, NULL);
}

/* ??0?$basic_streambuf@DU?$char_traits@D@std@@@std@@IAE@W4_Uninitialized@1@@Z */
/* ??0?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEAA@W4_Uninitialized@1@@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_ctor_uninitialized, 8)
basic_streambuf_char* __thiscall basic_streambuf_char_ctor_uninitialized(basic_streambuf_char *this, int uninitialized)
{
    TRACE("(%p %d)\n", this, uninitialized);
    this->vtable = &MSVCP_basic_streambuf_char_vtable;
    mutex_ctor(&this->lock);
    return this;
}

/* ??0?$basic_streambuf@DU?$char_traits@D@std@@@std@@IAE@XZ */
/* ??0?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEAA@XZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_ctor, 4)
basic_streambuf_char* __thiscall basic_streambuf_char_ctor(basic_streambuf_char *this)
{
    TRACE("(%p)\n", this);

    this->vtable = &MSVCP_basic_streambuf_char_vtable;
    mutex_ctor(&this->lock);
    this->loc = MSVCRT_operator_new(sizeof(locale));
    locale_ctor(this->loc);
    basic_streambuf_char__Init_empty(this);

    return this;
}

/* ??1?$basic_streambuf@DU?$char_traits@D@std@@@std@@UAE@XZ */
/* ??1?$basic_streambuf@DU?$char_traits@D@std@@@std@@UEAA@XZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_dtor, 4)
void __thiscall basic_streambuf_char_dtor(basic_streambuf_char *this)
{
    TRACE("(%p)\n", this);

    mutex_dtor(&this->lock);
    locale_dtor(this->loc);
    MSVCRT_operator_delete(this->loc);
}

DEFINE_THISCALL_WRAPPER(MSVCP_basic_streambuf_char_vector_dtor, 8)
basic_streambuf_char* __thiscall MSVCP_basic_streambuf_char_vector_dtor(basic_streambuf_char *this, unsigned int flags)
{
    TRACE("(%p %x)\n", this, flags);
    if(flags & 2) {
        /* we have an array, with the number of elements stored before the first object */
        int i, *ptr = (int *)this-1;

        for(i=*ptr-1; i>=0; i--)
            basic_streambuf_char_dtor(this+i);
        MSVCRT_operator_delete(ptr);
    } else {
        basic_streambuf_char_dtor(this);
        if(flags & 1)
            MSVCRT_operator_delete(this);
    }

    return this;
}

/* ?_Gnavail@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IBEHXZ */
/* ?_Gnavail@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEBA_JXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char__Gnavail, 4)
streamsize __thiscall basic_streambuf_char__Gnavail(const basic_streambuf_char *this)
{
    TRACE("(%p)\n", this);
    return *this->prpos ? *this->prsize : 0;
}

/* ?_Gndec@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IAEPADXZ */
/* ?_Gndec@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEAAPEADXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char__Gndec, 4)
char* __thiscall basic_streambuf_char__Gndec(basic_streambuf_char *this)
{
    TRACE("(%p)\n", this);
    (*this->prsize)++;
    (*this->prpos)--;
    return *this->prpos;
}

/* ?_Gninc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IAEPADXZ */
/* ?_Gninc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEAAPEADXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char__Gninc, 4)
char* __thiscall basic_streambuf_char__Gninc(basic_streambuf_char *this)
{
    TRACE("(%p)\n", this);
    (*this->prsize)--;
    return (*this->prpos)++;
}

/* ?_Gnpreinc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IAEPADXZ */
/* ?_Gnpreinc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEAAPEADXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char__Gnpreinc, 4)
char* __thiscall basic_streambuf_char__Gnpreinc(basic_streambuf_char *this)
{
    TRACE("(%p)\n", this);
    (*this->prsize)--;
    (*this->prpos)++;
    return *this->prpos;
}

/* ?_Init@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IAEXPAPAD0PAH001@Z */
/* ?_Init@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEAAXPEAPEAD0PEAH001@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char__Init, 28)
void __thiscall basic_streambuf_char__Init(basic_streambuf_char *this, char **gf, char **gn, int *gc, char **pf, char **pn, int *pc)
{
    TRACE("(%p %p %p %p %p %p %p)\n", this, gf, gn, gc, pf, pn, pc);

    this->prbuf = gf;
    this->pwbuf = pf;
    this->prpos = gn;
    this->pwpos = pn;
    this->prsize = gc;
    this->pwsize = pc;
}

/* ?_Lock@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QAEXXZ */
/* ?_Lock@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QEAAXXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char__Lock, 4)
void __thiscall basic_streambuf_char__Lock(basic_streambuf_char *this)
{
    TRACE("(%p)\n", this);
    mutex_lock(&this->lock);
}

/* ?_Pnavail@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IBEHXZ */
/* ?_Pnavail@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEBA_JXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char__Pnavail, 4)
streamsize __thiscall basic_streambuf_char__Pnavail(const basic_streambuf_char *this)
{
    TRACE("(%p)\n", this);
    return *this->pwpos ? *this->pwsize : 0;
}

/* ?_Pninc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IAEPADXZ */
/* ?_Pninc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEAAPEADXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char__Pninc, 4)
char* __thiscall basic_streambuf_char__Pninc(basic_streambuf_char *this)
{
    TRACE("(%p)\n", this);
    (*this->pwsize)--;
    return (*this->pwpos)++;
}

/* ?underflow@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAEHXZ */
/* ?underflow@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MEAAHXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_underflow, 4)
#define call_basic_streambuf_char_underflow(this) CALL_VTBL_FUNC(this, 16, \
        int, (basic_streambuf_char*), (this))
int __thiscall basic_streambuf_char_underflow(basic_streambuf_char *this)
{
    TRACE("(%p)\n", this);
    return EOF;
}

/* ?uflow@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAEHXZ */
/* ?uflow@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MEAAHXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_uflow, 4)
#define call_basic_streambuf_char_uflow(this) CALL_VTBL_FUNC(this, 20, \
        int, (basic_streambuf_char*), (this))
int __thiscall basic_streambuf_char_uflow(basic_streambuf_char *this)
{
    int ret;

    TRACE("(%p)\n", this);

    if(call_basic_streambuf_char_underflow(this)==EOF)
        return EOF;

    ret = **this->prpos;
    (*this->prsize)--;
    (*this->prpos)++;
    return ret;
}

/* ?_Xsgetn_s@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAEHPADIH@Z */
/* ?_Xsgetn_s@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MEAA_JPEAD_K_J@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char__Xsgetn_s, 16)
#define call_basic_streambuf_char__Xsgetn_s(this, ptr, size, count) CALL_VTBL_FUNC(this, 28, \
        streamsize, (basic_streambuf_char*, char*, MSVCP_size_t, streamsize), (this, ptr, size, count))
streamsize __thiscall basic_streambuf_char__Xsgetn_s(basic_streambuf_char *this, char *ptr, MSVCP_size_t size, streamsize count)
{
    streamsize copied, chunk;

    TRACE("(%p %p %lu %ld)\n", this, ptr, size, count);

    for(copied=0; copied<count && size;) {
        chunk = basic_streambuf_char__Gnavail(this);
        if(chunk > count-copied)
            chunk = count-copied;

        if(chunk) {
            memcpy_s(ptr+copied, size, *this->prpos, chunk);
            *this->prpos += chunk;
            *this->prsize -= chunk;
            copied += chunk;
            size -= chunk;
        }else if((ptr[copied] = call_basic_streambuf_char_uflow(this)) != EOF) {
            copied++;
            size--;
        }else {
            break;
        }
    }

    return copied;
}

/* ?_Sgetn_s@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QAEHPADIH@Z */
/* ?_Sgetn_s@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QEAA_JPEAD_K_J@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char__Sgetn_s, 16)
streamsize __thiscall basic_streambuf_char__Sgetn_s(basic_streambuf_char *this, char *ptr, MSVCP_size_t size, streamsize count)
{
    TRACE("(%p %p %lu %ld)\n", this, ptr, size, count);
    return call_basic_streambuf_char__Xsgetn_s(this, ptr, size, count);
}

/* ?_Unlock@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QAEXXZ */
/* ?_Unlock@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QEAAXXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char__Unlock, 4)
void __thiscall basic_streambuf_char__Unlock(basic_streambuf_char *this)
{
    TRACE("(%p)\n", this);
    mutex_unlock(&this->lock);
}

/* ?eback@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IBEPADXZ */
/* ?eback@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEBAPEADXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_eback, 4)
char* __thiscall basic_streambuf_char_eback(const basic_streambuf_char *this)
{
    TRACE("(%p)\n", this);
    return *this->prbuf;
}

/* ?gptr@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IBEPADXZ */
/* ?gptr@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEBAPEADXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_gptr, 4)
char* __thiscall basic_streambuf_char_gptr(const basic_streambuf_char *this)
{
    TRACE("(%p)\n", this);
    return *this->prpos;
}

/* ?egptr@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IBEPADXZ */
/* ?egptr@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEBAPEADXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_egptr, 4)
char* __thiscall basic_streambuf_char_egptr(const basic_streambuf_char *this)
{
    TRACE("(%p)\n", this);
    return *this->prpos+*this->prsize;
}

/* ?epptr@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IBEPADXZ */
/* ?epptr@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEBAPEADXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_epptr, 4)
char* __thiscall basic_streambuf_char_epptr(const basic_streambuf_char *this)
{
    TRACE("(%p)\n", this);
    return *this->pwpos+*this->pwsize;
}

/* ?gbump@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IAEXH@Z */
/* ?gbump@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEAAXH@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_gbump, 8)
void __thiscall basic_streambuf_char_gbump(basic_streambuf_char *this, int off)
{
    TRACE("(%p %d)\n", this, off);
    *this->prpos += off;
    *this->prsize -= off;
}

/* ?getloc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QBE?AVlocale@2@XZ */
/* ?getloc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QEBA?AVlocale@2@XZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_getloc, 8)
locale* __thiscall basic_streambuf_char_getloc(const basic_streambuf_char *this, locale *ret)
{
    TRACE("(%p)\n", this);
    return locale_copy_ctor(ret, this->loc);
}

/* ?imbue@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAEXABVlocale@2@@Z */
/* ?imbue@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MEAAXAEBVlocale@2@@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_imbue, 8)
#define call_basic_streambuf_char_imbue(this, loc) CALL_VTBL_FUNC(this, 52, \
        void, (basic_streambuf_char*, const locale*), (this, loc))
void __thiscall basic_streambuf_char_imbue(basic_streambuf_char *this, const locale *loc)
{
    TRACE("(%p %p)\n", this, loc);
}

/* ?overflow@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAEHH@Z */
/* ?overflow@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MEAAHH@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_overflow, 8)
#define call_basic_streambuf_char_overflow(this, ch) CALL_VTBL_FUNC(this, 4, \
        int, (basic_streambuf_char*, int), (this, ch))
int __thiscall basic_streambuf_char_overflow(basic_streambuf_char *this, int ch)
{
    TRACE("(%p %d)\n", this, ch);
    return EOF;
}

/* ?pbackfail@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAEHH@Z */
/* ?pbackfail@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MEAAHH@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_pbackfail, 8)
#define call_basic_streambuf_char_pbackfail(this, ch) CALL_VTBL_FUNC(this, 8, \
        int, (basic_streambuf_char*, int), (this, ch))
int __thiscall basic_streambuf_char_pbackfail(basic_streambuf_char *this, int ch)
{
    TRACE("(%p %d)\n", this, ch);
    return EOF;
}

/* ?pbase@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IBEPADXZ */
/* ?pbase@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEBAPEADXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_pbase, 4)
char* __thiscall basic_streambuf_char_pbase(const basic_streambuf_char *this)
{
    TRACE("(%p)\n", this);
    return *this->pwbuf;
}

/* ?pbump@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IAEXH@Z */
/* ?pbump@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEAAXH@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_pbump, 8)
void __thiscall basic_streambuf_char_pbump(basic_streambuf_char *this, int off)
{
    TRACE("(%p %d)\n", this, off);
    *this->pwpos += off;
    *this->pwsize -= off;
}

/* ?pptr@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IBEPADXZ */
/* ?pptr@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEBAPEADXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_pptr, 4)
char* __thiscall basic_streambuf_char_pptr(const basic_streambuf_char *this)
{
    TRACE("(%p)\n", this);
    return *this->pwpos;
}

/* ?pubimbue@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QAE?AVlocale@2@ABV32@@Z */
/* ?pubimbue@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QEAA?AVlocale@2@AEBV32@@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_pubimbue, 12)
locale* __thiscall basic_streambuf_char_pubimbue(basic_streambuf_char *this, locale *ret, const locale *loc)
{
    TRACE("(%p %p)\n", this, loc);
    memcpy(ret, this->loc, sizeof(locale));
    call_basic_streambuf_char_imbue(this, loc);
    locale_copy_ctor(this->loc, loc);
    return ret;
}

/* ?seekoff@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAE?AV?$fpos@H@2@JHH@Z */
/* ?seekoff@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MEAA?AV?$fpos@H@2@_JHH@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_seekoff, 20)
#define call_basic_streambuf_char_seekoff(this, ret, off, way, mode) CALL_VTBL_FUNC(this, 36, \
        fpos_int*, (basic_streambuf_char*, fpos_int*, streamoff, int, int), (this, ret, off, way, mode))
fpos_int* __thiscall basic_streambuf_char_seekoff(basic_streambuf_char *this,
        fpos_int *ret, streamoff off, int way, int mode)
{
    TRACE("(%p %ld %d %d)\n", this, off, way, mode);
    ret->off = 0;
    ret->pos = -1;
    ret->state = 0;
    return ret;
}

/* ?pubseekoff@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QAE?AV?$fpos@H@2@JHH@Z */
/* ?pubseekoff@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QEAA?AV?$fpos@H@2@_JHH@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_pubseekoff, 20)
fpos_int* __thiscall basic_streambuf_char_pubseekoff(basic_streambuf_char *this,
        fpos_int *ret, streamoff off, int way, int mode)
{
    TRACE("(%p %ld %d %d)\n", this, off, way, mode);
    return call_basic_streambuf_char_seekoff(this, ret, off, way, mode);
}

/* ?pubseekoff@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QAE?AV?$fpos@H@2@JII@Z */
/* ?pubseekoff@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QEAA?AV?$fpos@H@2@_JII@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_pubseekoff_old, 20)
fpos_int* __thiscall basic_streambuf_char_pubseekoff_old(basic_streambuf_char *this,
        fpos_int *ret, streamoff off, unsigned int way, unsigned int mode)
{
    TRACE("(%p %ld %d %d)\n", this, off, way, mode);
    return basic_streambuf_char_pubseekoff(this, ret, off, way, mode);
}

/* ?seekpos@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAE?AV?$fpos@H@2@V32@H@Z */
/* ?seekpos@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MEAA?AV?$fpos@H@2@V32@H@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_seekpos, 36)
#define call_basic_streambuf_char_seekpos(this, ret, pos, mode) CALL_VTBL_FUNC(this, 40, \
        fpos_int*, (basic_streambuf_char*, fpos_int*, fpos_int, int), (this, ret, pos, mode))
fpos_int* __thiscall basic_streambuf_char_seekpos(basic_streambuf_char *this,
        fpos_int *ret, fpos_int pos, int mode)
{
    TRACE("(%p %s %d)\n", this, debugstr_fpos_int(&pos), mode);
    ret->off = 0;
    ret->pos = -1;
    ret->state = 0;
    return ret;
}

/* ?pubseekpos@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QAE?AV?$fpos@H@2@V32@H@Z */
/* ?pubseekpos@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QEAA?AV?$fpos@H@2@V32@H@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_pubseekpos, 36)
fpos_int* __thiscall basic_streambuf_char_pubseekpos(basic_streambuf_char *this,
        fpos_int *ret, fpos_int pos, int mode)
{
    TRACE("(%p %s %d)\n", this, debugstr_fpos_int(&pos), mode);
    return call_basic_streambuf_char_seekpos(this, ret, pos, mode);
}

/* ?pubseekpos@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QAE?AV?$fpos@H@2@V32@I@Z */
/* ?pubseekpos@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QEAA?AV?$fpos@H@2@V32@I@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_pubseekpos_old, 36)
fpos_int* __thiscall basic_streambuf_char_pubseekpos_old(basic_streambuf_char *this,
        fpos_int *ret, fpos_int pos, unsigned int mode)
{
    TRACE("(%p %s %d)\n", this, debugstr_fpos_int(&pos), mode);
    return basic_streambuf_char_pubseekpos(this, ret, pos, mode);
}

/* ?setbuf@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAEPAV12@PADH@Z */
/* ?setbuf@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MEAAPEAV12@PEAD_J@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_setbuf, 12)
#define call_basic_streambuf_char_setbuf(this, buf, count) CALL_VTBL_FUNC(this, 44, \
        basic_streambuf_char*, (basic_streambuf_char*, char*, streamsize), (this, buf, count))
basic_streambuf_char* __thiscall basic_streambuf_char_setbuf(basic_streambuf_char *this, char *buf, streamsize count)
{
    TRACE("(%p %p %ld)\n", this, buf, count);
    return this;
}

/* ?pubsetbuf@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QAEPAV12@PADH@Z */
/* ?pubsetbuf@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QEAAPEAV12@PEAD_J@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_pubsetbuf, 12)
basic_streambuf_char* __thiscall basic_streambuf_char_pubsetbuf(basic_streambuf_char *this, char *buf, streamsize count)
{
    TRACE("(%p %p %ld)\n", this, buf, count);
    return call_basic_streambuf_char_setbuf(this, buf, count);
}

/* ?sync@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAEHXZ */
/* ?sync@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MEAAHXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_sync, 4)
#define call_basic_streambuf_char_sync(this) CALL_VTBL_FUNC(this, 48, \
        int, (basic_streambuf_char*), (this))
int __thiscall basic_streambuf_char_sync(basic_streambuf_char *this)
{
    TRACE("(%p)\n", this);
    return 0;
}

/* ?pubsync@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QAEHXZ */
/* ?pubsync@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QEAAHXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_pubsync, 4)
int __thiscall basic_streambuf_char_pubsync(basic_streambuf_char *this)
{
    TRACE("(%p)\n", this);
    return call_basic_streambuf_char_sync(this);
}

/* ?sgetn@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QAEHPADH@Z */
/* ?sgetn@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QEAA_JPEAD_J@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_sgetn, 12)
streamsize __thiscall basic_streambuf_char_sgetn(basic_streambuf_char *this, char *ptr, streamsize count)
{
    TRACE("(%p %p %ld)\n", this, ptr, count);
    return call_basic_streambuf_char__Xsgetn_s(this, ptr, -1, count);
}

/* ?showmanyc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAEHXZ */
/* ?showmanyc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MEAA_JXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_showmanyc, 4)
#define call_basic_streambuf_char_showmanyc(this) CALL_VTBL_FUNC(this, 12, \
        streamsize, (basic_streambuf_char*), (this))
streamsize __thiscall basic_streambuf_char_showmanyc(basic_streambuf_char *this)
{
    TRACE("(%p)\n", this);
    return 0;
}

/* ?in_avail@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QAEHXZ */
/* ?in_avail@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QEAA_JXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_in_avail, 4)
streamsize __thiscall basic_streambuf_char_in_avail(basic_streambuf_char *this)
{
    streamsize ret;

    TRACE("(%p)\n", this);

    ret = basic_streambuf_char__Gnavail(this);
    return ret ? ret : call_basic_streambuf_char_showmanyc(this);
}

/* ?sputbackc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QAEHD@Z */
/* ?sputbackc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QEAAHD@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_sputbackc, 8)
int __thiscall basic_streambuf_char_sputbackc(basic_streambuf_char *this, char ch)
{
    TRACE("(%p %d)\n", this, ch);
    if(*this->prpos && *this->prpos>*this->prbuf && (*this->prpos)[-1]==ch) {
        (*this->prsize)++;
        (*this->prpos)--;
        return ch;
    }

    return call_basic_streambuf_char_pbackfail(this, ch);
}

/* ?sputc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QAEHD@Z */
/* ?sputc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QEAAHD@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_sputc, 8)
int __thiscall basic_streambuf_char_sputc(basic_streambuf_char *this, char ch)
{
    TRACE("(%p %d)\n", this, ch);
    return basic_streambuf_char__Pnavail(this) ?
        (*basic_streambuf_char__Pninc(this) = ch) :
        call_basic_streambuf_char_overflow(this, ch);
}

/* ?sungetc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QAEHXZ */
/* ?sungetc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QEAAHXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_sungetc, 4)
int __thiscall basic_streambuf_char_sungetc(basic_streambuf_char *this)
{
    TRACE("(%p)\n", this);
    if(*this->prpos && *this->prpos>*this->prbuf) {
        (*this->prsize)++;
        (*this->prpos)--;
        return **this->prpos;
    }

    return call_basic_streambuf_char_pbackfail(this, EOF);
}

/* ?stossc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QAEXXZ */
/* ?stossc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QEAAXXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_stossc, 4)
void __thiscall basic_streambuf_char_stossc(basic_streambuf_char *this)
{
    TRACE("(%p)\n", this);
    if(basic_streambuf_char__Gnavail(this))
        basic_streambuf_char__Gninc(this);
    else
        call_basic_streambuf_char_uflow(this);
}

/* ?sbumpc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QAEHXZ */
/* ?sbumpc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QEAAHXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_sbumpc, 4)
int __thiscall basic_streambuf_char_sbumpc(basic_streambuf_char *this)
{
    TRACE("(%p)\n", this);
    return basic_streambuf_char__Gnavail(this) ?
        *basic_streambuf_char__Gninc(this) : call_basic_streambuf_char_uflow(this);
}

/* ?sgetc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QAEHXZ */
/* ?sgetc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QEAAHXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_sgetc, 4)
int __thiscall basic_streambuf_char_sgetc(basic_streambuf_char *this)
{
    TRACE("(%p)\n", this);
    return basic_streambuf_char__Gnavail(this) ?
        *basic_streambuf_char_gptr(this) : call_basic_streambuf_char_underflow(this);
}

/* ?snextc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QAEHXZ */
/* ?snextc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QEAAHXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_snextc, 4)
int __thiscall basic_streambuf_char_snextc(basic_streambuf_char *this)
{
    TRACE("(%p)\n", this);

    if(basic_streambuf_char__Gnavail(this) > 1)
        return *basic_streambuf_char__Gnpreinc(this);
    return basic_streambuf_char_sbumpc(this)==EOF ?
        EOF : basic_streambuf_char_sgetc(this);
}

/* ?xsgetn@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAEHPADH@Z */
/* ?xsgetn@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MEAA_JPEAD_J@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_xsgetn, 12)
#define call_basic_streambuf_char_xsgetn(this, ptr, count) CALL_VTBL_FUNC(this, 24, \
        streamsize, (basic_streambuf_char*, char*, streamsize), (this, ptr, count))
streamsize __thiscall basic_streambuf_char_xsgetn(basic_streambuf_char *this, char *ptr, streamsize count)
{
    TRACE("(%p %p %ld)\n", this, ptr, count);
    return call_basic_streambuf_char__Xsgetn_s(this, ptr, -1, count);
}

/* ?xsputn@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAEHPBDH@Z */
/* ?xsputn@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MEAA_JPEBD_J@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_xsputn, 12)
#define call_basic_streambuf_char_xsputn(this, ptr, count) CALL_VTBL_FUNC(this, 32, \
        streamsize, (basic_streambuf_char*, const char*, streamsize), (this, ptr, count))
streamsize __thiscall basic_streambuf_char_xsputn(basic_streambuf_char *this, const char *ptr, streamsize count)
{
    streamsize copied, chunk;

    TRACE("(%p %p %ld)\n", this, ptr, count);

    for(copied=0; copied<count;) {
        chunk = basic_streambuf_char__Pnavail(this);
        if(chunk > count-copied)
            chunk = count-copied;

        if(chunk) {
            memcpy(*this->pwpos, ptr+copied, chunk);
            *this->pwpos += chunk;
            *this->pwsize -= chunk;
            copied += chunk;
        }else if(call_basic_streambuf_char_overflow(this, ptr[copied]) != EOF) {
            copied++;
        }else {
            break;
        }
    }

    return copied;
}

/* ?sputn@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QAEHPBDH@Z */
/* ?sputn@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QEAA_JPEBD_J@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_char_sputn, 12)
streamsize __thiscall basic_streambuf_char_sputn(basic_streambuf_char *this, const char *ptr, streamsize count)
{
    TRACE("(%p %p %ld)\n", this, ptr, count);
    return call_basic_streambuf_char_xsputn(this, ptr, count);
}

/* ?setp@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IAEXPA_W00@Z */
/* ?setp@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IEAAXPEA_W00@Z */
/* ?setp@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IAEXPAG00@Z */
/* ?setp@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IEAAXPEAG00@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_setp_next, 16)
void __thiscall basic_streambuf_wchar_setp_next(basic_streambuf_wchar *this, wchar_t *first, wchar_t *next, wchar_t *last)
{
    TRACE("(%p %p %p %p)\n", this, first, next, last);

    this->wbuf = first;
    this->wpos = next;
    this->wsize = last-next;
}

/* ?setp@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IAEXPA_W0@Z */
/* ?setp@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IEAAXPEA_W0@Z */
/* ?setp@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IAEXPAG0@Z */
/* ?setp@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IEAAXPEAG0@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_setp, 12)
void __thiscall basic_streambuf_wchar_setp(basic_streambuf_wchar *this, wchar_t *first, wchar_t *last)
{
    basic_streambuf_wchar_setp_next(this, first, first, last);
}

/* ?setg@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IAEXPA_W00@Z */
/* ?setg@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IEAAXPEA_W00@Z */
/* ?setg@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IAEXPAG00@Z */
/* ?setg@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IEAAXPEAG00@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_setg, 16)
void __thiscall basic_streambuf_wchar_setg(basic_streambuf_wchar *this, wchar_t *first, wchar_t *next, wchar_t *last)
{
    TRACE("(%p %p %p %p)\n", this, first, next, last);

    this->rbuf = first;
    this->rpos = next;
    this->rsize = last-next;
}

/* ?_Init@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IAEXXZ */
/* ?_Init@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IEAAXXZ */
/* ?_Init@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IAEXXZ */
/* ?_Init@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IEAAXXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar__Init_empty, 4)
void __thiscall basic_streambuf_wchar__Init_empty(basic_streambuf_wchar *this)
{
    TRACE("(%p)\n", this);

    this->prbuf = &this->rbuf;
    this->pwbuf = &this->wbuf;
    this->prpos = &this->rpos;
    this->pwpos = &this->wpos;
    this->prsize = &this->rsize;
    this->pwsize = &this->wsize;

    basic_streambuf_wchar_setp(this, NULL, NULL);
    basic_streambuf_wchar_setg(this, NULL, NULL, NULL);
}

/* ??0?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IAE@W4_Uninitialized@1@@Z */
/* ??0?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IEAA@W4_Uninitialized@1@@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_ctor_uninitialized, 8)
basic_streambuf_wchar* __thiscall basic_streambuf_wchar_ctor_uninitialized(basic_streambuf_wchar *this, int uninitialized)
{
    TRACE("(%p %d)\n", this, uninitialized);
    this->vtable = &MSVCP_basic_streambuf_wchar_vtable;
    mutex_ctor(&this->lock);
    return this;
}

/* ??0?$basic_streambuf@GU?$char_traits@G@std@@@std@@IAE@W4_Uninitialized@1@@Z */
/* ??0?$basic_streambuf@GU?$char_traits@G@std@@@std@@IEAA@W4_Uninitialized@1@@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_short_ctor_uninitialized, 8)
basic_streambuf_wchar* __thiscall basic_streambuf_short_ctor_uninitialized(basic_streambuf_wchar *this, int uninitialized)
{
    TRACE("(%p %d)\n", this, uninitialized);
    basic_streambuf_wchar_ctor_uninitialized(this, uninitialized);
    this->vtable = &MSVCP_basic_streambuf_short_vtable;
    return this;
}

/* ??0?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IAE@XZ */
/* ??0?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IEAA@XZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_ctor, 4)
basic_streambuf_wchar* __thiscall basic_streambuf_wchar_ctor(basic_streambuf_wchar *this)
{
    TRACE("(%p)\n", this);

    this->vtable = &MSVCP_basic_streambuf_wchar_vtable;
    mutex_ctor(&this->lock);
    this->loc = MSVCRT_operator_new(sizeof(locale));
    locale_ctor(this->loc);
    basic_streambuf_wchar__Init_empty(this);

    return this;
}

/* ??0?$basic_streambuf@GU?$char_traits@G@std@@@std@@IAE@XZ */
/* ??0?$basic_streambuf@GU?$char_traits@G@std@@@std@@IEAA@XZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_short_ctor, 4)
basic_streambuf_wchar* __thiscall basic_streambuf_short_ctor(basic_streambuf_wchar *this)
{
    TRACE("(%p)\n", this);
    basic_streambuf_wchar_ctor(this);
    this->vtable = &MSVCP_basic_streambuf_short_vtable;
    return this;
}

/* ??1?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@UAE@XZ */
/* ??1?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@UEAA@XZ */
/* ??1?$basic_streambuf@GU?$char_traits@G@std@@@std@@UAE@XZ */
/* ??1?$basic_streambuf@GU?$char_traits@G@std@@@std@@UEAA@XZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_dtor, 4)
void __thiscall basic_streambuf_wchar_dtor(basic_streambuf_wchar *this)
{
    TRACE("(%p)\n", this);

    mutex_dtor(&this->lock);
    locale_dtor(this->loc);
    MSVCRT_operator_delete(this->loc);
}

DEFINE_THISCALL_WRAPPER(MSVCP_basic_streambuf_wchar_vector_dtor, 8)
basic_streambuf_wchar* __thiscall MSVCP_basic_streambuf_wchar_vector_dtor(basic_streambuf_wchar *this, unsigned int flags)
{
    TRACE("(%p %x)\n", this, flags);
    if(flags & 2) {
        /* we have an array, with the number of elements stored before the first object */
        int i, *ptr = (int *)this-1;

        for(i=*ptr-1; i>=0; i--)
            basic_streambuf_wchar_dtor(this+i);
        MSVCRT_operator_delete(ptr);
    } else {
        basic_streambuf_wchar_dtor(this);
        if(flags & 1)
            MSVCRT_operator_delete(this);
    }

    return this;
}

DEFINE_THISCALL_WRAPPER(MSVCP_basic_streambuf_short_vector_dtor, 8)
basic_streambuf_wchar* __thiscall MSVCP_basic_streambuf_short_vector_dtor(basic_streambuf_wchar *this, unsigned int flags)
{
    TRACE("(%p %x)\n", this, flags);
    return MSVCP_basic_streambuf_wchar_vector_dtor(this, flags);
}

/* ?_Gnavail@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IBEHXZ */
/* ?_Gnavail@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IEBA_JXZ */
/* ?_Gnavail@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IBEHXZ */
/* ?_Gnavail@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IEBA_JXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar__Gnavail, 4)
streamsize __thiscall basic_streambuf_wchar__Gnavail(const basic_streambuf_wchar *this)
{
    TRACE("(%p)\n", this);
    return *this->prpos ? *this->prsize : 0;
}

/* ?_Gndec@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IAEPA_WXZ */
/* ?_Gndec@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IEAAPEA_WXZ */
/* ?_Gndec@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IAEPAGXZ */
/* ?_Gndec@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IEAAPEAGXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar__Gndec, 4)
wchar_t* __thiscall basic_streambuf_wchar__Gndec(basic_streambuf_wchar *this)
{
    TRACE("(%p)\n", this);
    (*this->prsize)++;
    (*this->prpos)--;
    return *this->prpos;
}

/* ?_Gninc@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IAEPA_WXZ */
/* ?_Gninc@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IEAAPEA_WXZ */
/* ?_Gninc@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IAEPAGXZ */
/* ?_Gninc@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IEAAPEAGXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar__Gninc, 4)
wchar_t* __thiscall basic_streambuf_wchar__Gninc(basic_streambuf_wchar *this)
{
    TRACE("(%p)\n", this);
    (*this->prsize)--;
    return (*this->prpos)++;
}

/* ?_Gnpreinc@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IAEPA_WXZ */
/* ?_Gnpreinc@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IEAAPEA_WXZ */
/* ?_Gnpreinc@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IAEPAGXZ */
/* ?_Gnpreinc@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IEAAPEAGXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar__Gnpreinc, 4)
wchar_t* __thiscall basic_streambuf_wchar__Gnpreinc(basic_streambuf_wchar *this)
{
    TRACE("(%p)\n", this);
    (*this->prsize)--;
    (*this->prpos)++;
    return *this->prpos;
}

/* ?_Init@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IAEXPAPA_W0PAH001@Z */
/* ?_Init@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IEAAXPEAPEA_W0PEAH001@Z */
/* ?_Init@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IAEXPAPAG0PAH001@Z */
/* ?_Init@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IEAAXPEAPEAG0PEAH001@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar__Init, 28)
void __thiscall basic_streambuf_wchar__Init(basic_streambuf_wchar *this, wchar_t **gf, wchar_t **gn, int *gc, wchar_t **pf, wchar_t **pn, int *pc)
{
    TRACE("(%p %p %p %p %p %p %p)\n", this, gf, gn, gc, pf, pn, pc);

    this->prbuf = gf;
    this->pwbuf = pf;
    this->prpos = gn;
    this->pwpos = pn;
    this->prsize = gc;
    this->pwsize = pc;
}

/* ?_Lock@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QAEXXZ */
/* ?_Lock@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QEAAXXZ */
/* ?_Lock@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QAEXXZ */
/* ?_Lock@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QEAAXXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar__Lock, 4)
void __thiscall basic_streambuf_wchar__Lock(basic_streambuf_wchar *this)
{
    TRACE("(%p)\n", this);
    mutex_lock(&this->lock);
}

/* ?_Pnavail@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IBEHXZ */
/* ?_Pnavail@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IEBA_JXZ */
/* ?_Pnavail@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IBEHXZ */
/* ?_Pnavail@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IEBA_JXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar__Pnavail, 4)
streamsize __thiscall basic_streambuf_wchar__Pnavail(const basic_streambuf_wchar *this)
{
    TRACE("(%p)\n", this);
    return *this->pwpos ? *this->pwsize : 0;
}

/* ?_Pninc@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IAEPA_WXZ */
/* ?_Pninc@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IEAAPEA_WXZ */
/* ?_Pninc@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IAEPAGXZ */
/* ?_Pninc@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IEAAPEAGXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar__Pninc, 4)
wchar_t* __thiscall basic_streambuf_wchar__Pninc(basic_streambuf_wchar *this)
{
    TRACE("(%p)\n", this);
    (*this->pwsize)--;
    return (*this->pwpos)++;
}

/* ?underflow@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@MAEGXZ */
/* ?underflow@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@MEAAGXZ */
/* ?underflow@?$basic_streambuf@GU?$char_traits@G@std@@@std@@MAEGXZ */
/* ?underflow@?$basic_streambuf@GU?$char_traits@G@std@@@std@@MEAAGXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_underflow, 4)
#define call_basic_streambuf_wchar_underflow(this) CALL_VTBL_FUNC(this, 16, \
        unsigned short, (basic_streambuf_wchar*), (this))
unsigned short __thiscall basic_streambuf_wchar_underflow(basic_streambuf_wchar *this)
{
    TRACE("(%p)\n", this);
    return WEOF;
}

/* ?uflow@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@MAEGXZ */
/* ?uflow@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@MEAAGXZ */
/* ?uflow@?$basic_streambuf@GU?$char_traits@G@std@@@std@@MAEGXZ */
/* ?uflow@?$basic_streambuf@GU?$char_traits@G@std@@@std@@MEAAGXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_uflow, 4)
#define call_basic_streambuf_wchar_uflow(this) CALL_VTBL_FUNC(this, 20, \
        unsigned short, (basic_streambuf_wchar*), (this))
unsigned short __thiscall basic_streambuf_wchar_uflow(basic_streambuf_wchar *this)
{
    int ret;

    TRACE("(%p)\n", this);

    if(call_basic_streambuf_wchar_underflow(this)==WEOF)
        return WEOF;

    ret = **this->prpos;
    (*this->prsize)--;
    (*this->prpos)++;
    return ret;
}

/* ?_Xsgetn_s@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@MAEHPA_WIH@Z */
/* ?_Xsgetn_s@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@MEAA_JPEA_W_K_J@Z */
/* ?_Xsgetn_s@?$basic_streambuf@GU?$char_traits@G@std@@@std@@MAEHPAGIH@Z */
/* ?_Xsgetn_s@?$basic_streambuf@GU?$char_traits@G@std@@@std@@MEAA_JPEAG_K_J@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar__Xsgetn_s, 16)
#define call_basic_streambuf_wchar__Xsgetn_s(this, ptr, size, count) CALL_VTBL_FUNC(this, 28, \
        streamsize, (basic_streambuf_wchar*, wchar_t*, MSVCP_size_t, streamsize), (this, ptr, size, count))
streamsize __thiscall basic_streambuf_wchar__Xsgetn_s(basic_streambuf_wchar *this, wchar_t *ptr, MSVCP_size_t size, streamsize count)
{
    streamsize copied, chunk;

    TRACE("(%p %p %lu %ld)\n", this, ptr, size, count);

    for(copied=0; copied<count && size;) {
        chunk = basic_streambuf_wchar__Gnavail(this);
        if(chunk > count-copied)
            chunk = count-copied;

        if(chunk) {
            memcpy_s(ptr+copied, size, *this->prpos, chunk);
            *this->prpos += chunk;
            *this->prsize -= chunk;
            copied += chunk;
            size -= chunk;
        }else if((ptr[copied] = call_basic_streambuf_wchar_uflow(this)) != WEOF) {
            copied++;
            size--;
        }else {
            break;
        }
    }

    return copied;
}

/* ?_Sgetn_s@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QAEHPA_WIH@Z */
/* ?_Sgetn_s@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QEAA_JPEA_W_K_J@Z */
/* ?_Sgetn_s@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QAEHPAGIH@Z */
/* ?_Sgetn_s@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QEAA_JPEAG_K_J@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar__Sgetn_s, 16)
streamsize __thiscall basic_streambuf_wchar__Sgetn_s(basic_streambuf_wchar *this, wchar_t *ptr, MSVCP_size_t size, streamsize count)
{
    TRACE("(%p %p %lu %ld)\n", this, ptr, size, count);
    return call_basic_streambuf_wchar__Xsgetn_s(this, ptr, size, count);
}

/* ?_Unlock@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QAEXXZ */
/* ?_Unlock@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QEAAXXZ */
/* ?_Unlock@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QAEXXZ */
/* ?_Unlock@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QEAAXXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar__Unlock, 4)
void __thiscall basic_streambuf_wchar__Unlock(basic_streambuf_wchar *this)
{
    TRACE("(%p)\n", this);
    mutex_unlock(&this->lock);
}

/* ?eback@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IBEPA_WXZ */
/* ?eback@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IEBAPEA_WXZ */
/* ?eback@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IBEPAGXZ */
/* ?eback@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IEBAPEAGXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_eback, 4)
wchar_t* __thiscall basic_streambuf_wchar_eback(const basic_streambuf_wchar *this)
{
    TRACE("(%p)\n", this);
    return *this->prbuf;
}

/* ?gptr@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IBEPA_WXZ */
/* ?gptr@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IEBAPEA_WXZ */
/* ?gptr@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IBEPAGXZ */
/* ?gptr@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IEBAPEAGXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_gptr, 4)
wchar_t* __thiscall basic_streambuf_wchar_gptr(const basic_streambuf_wchar *this)
{
    TRACE("(%p)\n", this);
    return *this->prpos;
}

/* ?egptr@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IBEPA_WXZ */
/* ?egptr@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IEBAPEA_WXZ */
/* ?egptr@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IBEPAGXZ */
/* ?egptr@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IEBAPEAGXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_egptr, 4)
wchar_t* __thiscall basic_streambuf_wchar_egptr(const basic_streambuf_wchar *this)
{
    TRACE("(%p)\n", this);
    return *this->prpos+*this->prsize;
}

/* ?epptr@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IBEPA_WXZ */
/* ?epptr@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IEBAPEA_WXZ */
/* ?epptr@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IBEPAGXZ */
/* ?epptr@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IEBAPEAGXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_epptr, 4)
wchar_t* __thiscall basic_streambuf_wchar_epptr(const basic_streambuf_wchar *this)
{
    TRACE("(%p)\n", this);
    return *this->pwpos+*this->pwsize;
}

/* ?gbump@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IAEXH@Z */
/* ?gbump@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IEAAXH@Z */
/* ?gbump@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IAEXH@Z */
/* ?gbump@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IEAAXH@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_gbump, 8)
void __thiscall basic_streambuf_wchar_gbump(basic_streambuf_wchar *this, int off)
{
    TRACE("(%p %d)\n", this, off);
    *this->prpos += off;
    *this->prsize -= off;
}

/* ?getloc@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QBE?AVlocale@2@XZ */
/* ?getloc@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QEBA?AVlocale@2@XZ */
/* ?getloc@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QBE?AVlocale@2@XZ */
/* ?getloc@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QEBA?AVlocale@2@XZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_getloc, 8)
locale* __thiscall basic_streambuf_wchar_getloc(const basic_streambuf_wchar *this, locale *ret)
{
    TRACE("(%p)\n", this);
    return locale_copy_ctor(ret, this->loc);
}

/* ?imbue@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@MAEXABVlocale@2@@Z */
/* ?imbue@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@MEAAXAEBVlocale@2@@Z */
/* ?imbue@?$basic_streambuf@GU?$char_traits@G@std@@@std@@MAEXABVlocale@2@@Z */
/* ?imbue@?$basic_streambuf@GU?$char_traits@G@std@@@std@@MEAAXAEBVlocale@2@@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_imbue, 8)
#define call_basic_streambuf_wchar_imbue(this, loc) CALL_VTBL_FUNC(this, 52, \
        void, (basic_streambuf_wchar*, const locale*), (this, loc))
void __thiscall basic_streambuf_wchar_imbue(basic_streambuf_wchar *this, const locale *loc)
{
    TRACE("(%p %p)\n", this, loc);
}

/* ?overflow@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@MAEGG@Z */
/* ?overflow@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@MEAAGG@Z */
/* ?overflow@?$basic_streambuf@GU?$char_traits@G@std@@@std@@MAEGG@Z */
/* ?overflow@?$basic_streambuf@GU?$char_traits@G@std@@@std@@MEAAGG@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_overflow, 8)
#define call_basic_streambuf_wchar_overflow(this, ch) CALL_VTBL_FUNC(this, 4, \
        unsigned short, (basic_streambuf_wchar*, unsigned short), (this, ch))
unsigned short __thiscall basic_streambuf_wchar_overflow(basic_streambuf_wchar *this, unsigned short ch)
{
    TRACE("(%p %d)\n", this, ch);
    return WEOF;
}

/* ?pbackfail@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@MAEGG@Z */
/* ?pbackfail@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@MEAAGG@Z */
/* ?pbackfail@?$basic_streambuf@GU?$char_traits@G@std@@@std@@MAEGG@Z */
/* ?pbackfail@?$basic_streambuf@GU?$char_traits@G@std@@@std@@MEAAGG@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_pbackfail, 8)
#define call_basic_streambuf_wchar_pbackfail(this, ch) CALL_VTBL_FUNC(this, 8, \
        unsigned short, (basic_streambuf_wchar*, unsigned short), (this, ch))
unsigned short __thiscall basic_streambuf_wchar_pbackfail(basic_streambuf_wchar *this, unsigned short ch)
{
    TRACE("(%p %d)\n", this, ch);
    return WEOF;
}

/* ?pbase@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IBEPA_WXZ */
/* ?pbase@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IEBAPEA_WXZ */
/* ?pbase@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IBEPAGXZ */
/* ?pbase@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IEBAPEAGXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_pbase, 4)
wchar_t* __thiscall basic_streambuf_wchar_pbase(const basic_streambuf_wchar *this)
{
    TRACE("(%p)\n", this);
    return *this->pwbuf;
}

/* ?pbump@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IAEXH@Z */
/* ?pbump@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IEAAXH@Z */
/* ?pbump@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IAEXH@Z */
/* ?pbump@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IEAAXH@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_pbump, 8)
void __thiscall basic_streambuf_wchar_pbump(basic_streambuf_wchar *this, int off)
{
    TRACE("(%p %d)\n", this, off);
    *this->pwpos += off;
    *this->pwsize -= off;
}

/* ?pptr@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IBEPA_WXZ */
/* ?pptr@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@IEBAPEA_WXZ */
/* ?pptr@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IBEPAGXZ */
/* ?pptr@?$basic_streambuf@GU?$char_traits@G@std@@@std@@IEBAPEAGXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_pptr, 4)
wchar_t* __thiscall basic_streambuf_wchar_pptr(const basic_streambuf_wchar *this)
{
    TRACE("(%p)\n", this);
    return *this->pwpos;
}

/* ?pubimbue@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QAE?AVlocale@2@ABV32@@Z */
/* ?pubimbue@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QEAA?AVlocale@2@AEBV32@@Z */
/* ?pubimbue@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QAE?AVlocale@2@ABV32@@Z */
/* ?pubimbue@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QEAA?AVlocale@2@AEBV32@@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_pubimbue, 12)
locale* __thiscall basic_streambuf_wchar_pubimbue(basic_streambuf_wchar *this, locale *ret, const locale *loc)
{
    TRACE("(%p %p)\n", this, loc);
    memcpy(ret, this->loc, sizeof(locale));
    call_basic_streambuf_wchar_imbue(this, loc);
    locale_copy_ctor(this->loc, loc);
    return ret;
}

/* ?seekoff@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@MAE?AV?$fpos@H@2@JHH@Z */
/* ?seekoff@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@MEAA?AV?$fpos@H@2@_JHH@Z */
/* ?seekoff@?$basic_streambuf@GU?$char_traits@G@std@@@std@@MAE?AV?$fpos@H@2@JHH@Z */
/* ?seekoff@?$basic_streambuf@GU?$char_traits@G@std@@@std@@MEAA?AV?$fpos@H@2@_JHH@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_seekoff, 20)
#define call_basic_streambuf_wchar_seekoff(this, ret, off, way, mode) CALL_VTBL_FUNC(this, 36, \
        fpos_int*, (basic_streambuf_wchar*, fpos_int*, streamoff, int, int), (this, ret, off, way, mode))
fpos_int* __thiscall basic_streambuf_wchar_seekoff(basic_streambuf_wchar *this,
        fpos_int *ret, streamoff off, int way, int mode)
{
    TRACE("(%p %ld %d %d)\n", this, off, way, mode);
    ret->off = 0;
    ret->pos = -1;
    ret->state = 0;
    return ret;
}

/* ?pubseekoff@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QAE?AV?$fpos@H@2@JHH@Z */
/* ?pubseekoff@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QEAA?AV?$fpos@H@2@_JHH@Z */
/* ?pubseekoff@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QAE?AV?$fpos@H@2@JHH@Z */
/* ?pubseekoff@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QEAA?AV?$fpos@H@2@_JHH@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_pubseekoff, 20)
fpos_int* __thiscall basic_streambuf_wchar_pubseekoff(basic_streambuf_wchar *this,
        fpos_int *ret, streamoff off, int way, int mode)
{
    TRACE("(%p %ld %d %d)\n", this, off, way, mode);
    return call_basic_streambuf_wchar_seekoff(this, ret, off, way, mode);
}

/* ?pubseekoff@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QAE?AV?$fpos@H@2@JII@Z */
/* ?pubseekoff@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QEAA?AV?$fpos@H@2@_JII@Z */
/* ?pubseekoff@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QAE?AV?$fpos@H@2@JII@Z */
/* ?pubseekoff@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QEAA?AV?$fpos@H@2@_JII@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_pubseekoff_old, 20)
fpos_int* __thiscall basic_streambuf_wchar_pubseekoff_old(basic_streambuf_wchar *this,
        fpos_int *ret, streamoff off, unsigned int way, unsigned int mode)
{
    TRACE("(%p %ld %d %d)\n", this, off, way, mode);
    return basic_streambuf_wchar_pubseekoff(this, ret, off, way, mode);
}

/* ?seekpos@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@MAE?AV?$fpos@H@2@V32@H@Z */
/* ?seekpos@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@MEAA?AV?$fpos@H@2@V32@H@Z */
/* ?seekpos@?$basic_streambuf@GU?$char_traits@G@std@@@std@@MAE?AV?$fpos@H@2@V32@H@Z */
/* ?seekpos@?$basic_streambuf@GU?$char_traits@G@std@@@std@@MEAA?AV?$fpos@H@2@V32@H@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_seekpos, 36)
#define call_basic_streambuf_wchar_seekpos(this, ret, pos, mode) CALL_VTBL_FUNC(this, 40, \
        fpos_int*, (basic_streambuf_wchar*, fpos_int*, fpos_int, int), (this, ret, pos, mode))
fpos_int* __thiscall basic_streambuf_wchar_seekpos(basic_streambuf_wchar *this,
        fpos_int *ret, fpos_int pos, int mode)
{
    TRACE("(%p %s %d)\n", this, debugstr_fpos_int(&pos), mode);
    ret->off = 0;
    ret->pos = -1;
    ret->state = 0;
    return ret;
}

/* ?pubseekpos@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QAE?AV?$fpos@H@2@V32@H@Z */
/* ?pubseekpos@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QEAA?AV?$fpos@H@2@V32@H@Z */
/* ?pubseekpos@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QAE?AV?$fpos@H@2@V32@H@Z */
/* ?pubseekpos@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QEAA?AV?$fpos@H@2@V32@H@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_pubseekpos, 36)
fpos_int* __thiscall basic_streambuf_wchar_pubseekpos(basic_streambuf_wchar *this,
        fpos_int *ret, fpos_int pos, int mode)
{
    TRACE("(%p %s %d)\n", this, debugstr_fpos_int(&pos), mode);
    return call_basic_streambuf_wchar_seekpos(this, ret, pos, mode);
}

/* ?pubseekpos@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QAE?AV?$fpos@H@2@V32@I@Z */
/* ?pubseekpos@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QEAA?AV?$fpos@H@2@V32@I@Z */
/* ?pubseekpos@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QAE?AV?$fpos@H@2@V32@I@Z */
/* ?pubseekpos@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QEAA?AV?$fpos@H@2@V32@I@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_pubseekpos_old, 36)
fpos_int* __thiscall basic_streambuf_wchar_pubseekpos_old(basic_streambuf_wchar *this,
        fpos_int *ret, fpos_int pos, unsigned int mode)
{
    TRACE("(%p %s %d)\n", this, debugstr_fpos_int(&pos), mode);
    return basic_streambuf_wchar_pubseekpos(this, ret, pos, mode);
}

/* ?setbuf@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@MAEPAV12@PA_WH@Z */
/* ?setbuf@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@MEAAPEAV12@PEA_W_J@Z */
/* ?setbuf@?$basic_streambuf@GU?$char_traits@G@std@@@std@@MAEPAV12@PAGH@Z */
/* ?setbuf@?$basic_streambuf@GU?$char_traits@G@std@@@std@@MEAAPEAV12@PEAG_J@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_setbuf, 12)
#define call_basic_streambuf_wchar_setbuf(this, buf, count) CALL_VTBL_FUNC(this, 44, \
        basic_streambuf_wchar*, (basic_streambuf_wchar*, wchar_t*, streamsize), (this, buf, count))
basic_streambuf_wchar* __thiscall basic_streambuf_wchar_setbuf(basic_streambuf_wchar *this, wchar_t *buf, streamsize count)
{
    TRACE("(%p %p %ld)\n", this, buf, count);
    return this;
}

/* ?pubsetbuf@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QAEPAV12@PA_WH@Z */
/* ?pubsetbuf@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QEAAPEAV12@PEA_W_J@Z */
/* ?pubsetbuf@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QAEPAV12@PAGH@Z */
/* ?pubsetbuf@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QEAAPEAV12@PEAG_J@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_pubsetbuf, 12)
basic_streambuf_wchar* __thiscall basic_streambuf_wchar_pubsetbuf(basic_streambuf_wchar *this, wchar_t *buf, streamsize count)
{
    TRACE("(%p %p %ld)\n", this, buf, count);
    return call_basic_streambuf_wchar_setbuf(this, buf, count);
}

/* ?sync@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@MAEHXZ */
/* ?sync@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@MEAAHXZ */
/* ?sync@?$basic_streambuf@GU?$char_traits@G@std@@@std@@MAEHXZ */
/* ?sync@?$basic_streambuf@GU?$char_traits@G@std@@@std@@MEAAHXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_sync, 4)
#define call_basic_streambuf_wchar_sync(this) CALL_VTBL_FUNC(this, 48, \
        int, (basic_streambuf_wchar*), (this))
int __thiscall basic_streambuf_wchar_sync(basic_streambuf_wchar *this)
{
    TRACE("(%p)\n", this);
    return 0;
}

/* ?pubsync@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QAEHXZ */
/* ?pubsync@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QEAAHXZ */
/* ?pubsync@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QAEHXZ */
/* ?pubsync@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QEAAHXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_pubsync, 4)
int __thiscall basic_streambuf_wchar_pubsync(basic_streambuf_wchar *this)
{
    TRACE("(%p)\n", this);
    return call_basic_streambuf_wchar_sync(this);
}

/* ?sgetn@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QAEHPA_WH@Z */
/* ?sgetn@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QEAA_JPEA_W_J@Z */
/* ?sgetn@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QAEHPAGH@Z */
/* ?sgetn@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QEAA_JPEAG_J@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_sgetn, 12)
streamsize __thiscall basic_streambuf_wchar_sgetn(basic_streambuf_wchar *this, wchar_t *ptr, streamsize count)
{
    TRACE("(%p %p %ld)\n", this, ptr, count);
    return call_basic_streambuf_wchar__Xsgetn_s(this, ptr, -1, count);
}

/* ?showmanyc@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@MAEHXZ */
/* ?showmanyc@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@MEAA_JXZ */
/* ?showmanyc@?$basic_streambuf@GU?$char_traits@G@std@@@std@@MAEHXZ */
/* ?showmanyc@?$basic_streambuf@GU?$char_traits@G@std@@@std@@MEAA_JXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_showmanyc, 4)
#define call_basic_streambuf_wchar_showmanyc(this) CALL_VTBL_FUNC(this, 12, \
        streamsize, (basic_streambuf_wchar*), (this))
streamsize __thiscall basic_streambuf_wchar_showmanyc(basic_streambuf_wchar *this)
{
    TRACE("(%p)\n", this);
    return 0;
}

/* ?in_avail@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QAEHXZ */
/* ?in_avail@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QEAA_JXZ */
/* ?in_avail@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QAEHXZ */
/* ?in_avail@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QEAA_JXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_in_avail, 4)
streamsize __thiscall basic_streambuf_wchar_in_avail(basic_streambuf_wchar *this)
{
    streamsize ret;

    TRACE("(%p)\n", this);

    ret = basic_streambuf_wchar__Gnavail(this);
    return ret ? ret : call_basic_streambuf_wchar_showmanyc(this);
}

/* ?sputbackc@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QAEG_W@Z */
/* ?sputbackc@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QEAAG_W@Z */
/* ?sputbackc@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QAEGG@Z */
/* ?sputbackc@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QEAAGG@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_sputbackc, 8)
unsigned short __thiscall basic_streambuf_wchar_sputbackc(basic_streambuf_wchar *this, wchar_t ch)
{
    TRACE("(%p %d)\n", this, ch);
    if(*this->prpos && *this->prpos>*this->prbuf && (*this->prpos)[-1]==ch) {
        (*this->prsize)++;
        (*this->prpos)--;
        return ch;
    }

    return call_basic_streambuf_wchar_pbackfail(this, ch);
}

/* ?sputc@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QAEG_W@Z */
/* ?sputc@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QEAAG_W@Z */
/* ?sputc@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QAEGG@Z */
/* ?sputc@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QEAAHG@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_sputc, 8)
unsigned short __thiscall basic_streambuf_wchar_sputc(basic_streambuf_wchar *this, wchar_t ch)
{
    TRACE("(%p %d)\n", this, ch);
    return basic_streambuf_wchar__Pnavail(this) ?
        (*basic_streambuf_wchar__Pninc(this) = ch) :
        call_basic_streambuf_wchar_overflow(this, ch);
}

/* ?sungetc@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QAEGXZ */
/* ?sungetc@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QEAAGXZ */
/* ?sungetc@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QAEGXZ */
/* ?sungetc@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QEAAGXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_sungetc, 4)
unsigned short __thiscall basic_streambuf_wchar_sungetc(basic_streambuf_wchar *this)
{
    TRACE("(%p)\n", this);
    if(*this->prpos && *this->prpos>*this->prbuf) {
        (*this->prsize)++;
        (*this->prpos)--;
        return **this->prpos;
    }

    return call_basic_streambuf_wchar_pbackfail(this, WEOF);
}

/* ?stossc@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QAEXXZ */
/* ?stossc@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QEAAXXZ */
/* ?stossc@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QAEXXZ */
/* ?stossc@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QEAAXXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_stossc, 4)
void __thiscall basic_streambuf_wchar_stossc(basic_streambuf_wchar *this)
{
    TRACE("(%p)\n", this);
    if(basic_streambuf_wchar__Gnavail(this))
        basic_streambuf_wchar__Gninc(this);
    else
        call_basic_streambuf_wchar_uflow(this);
}

/* ?sbumpc@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QAEGXZ */
/* ?sbumpc@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QEAAGXZ */
/* ?sbumpc@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QAEGXZ */
/* ?sbumpc@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QEAAGXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_sbumpc, 4)
unsigned short __thiscall basic_streambuf_wchar_sbumpc(basic_streambuf_wchar *this)
{
    TRACE("(%p)\n", this);
    return basic_streambuf_wchar__Gnavail(this) ?
        *basic_streambuf_wchar__Gninc(this) : call_basic_streambuf_wchar_uflow(this);
}

/* ?sgetc@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QAEGXZ */
/* ?sgetc@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QEAAGXZ */
/* ?sgetc@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QAEGXZ */
/* ?sgetc@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QEAAGXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_sgetc, 4)
unsigned short __thiscall basic_streambuf_wchar_sgetc(basic_streambuf_wchar *this)
{
    TRACE("(%p)\n", this);
    return basic_streambuf_wchar__Gnavail(this) ?
        *basic_streambuf_wchar_gptr(this) : call_basic_streambuf_wchar_underflow(this);
}

/* ?snextc@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QAEGXZ */
/* ?snextc@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QEAAGXZ */
/* ?snextc@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QAEGXZ */
/* ?snextc@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QEAAGXZ */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_snextc, 4)
unsigned short __thiscall basic_streambuf_wchar_snextc(basic_streambuf_wchar *this)
{
    TRACE("(%p)\n", this);

    if(basic_streambuf_wchar__Gnavail(this) > 1)
        return *basic_streambuf_wchar__Gnpreinc(this);
    return basic_streambuf_wchar_sbumpc(this)==WEOF ?
        WEOF : basic_streambuf_wchar_sgetc(this);
}

/* ?xsgetn@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@MAEHPA_WH@Z */
/* ?xsgetn@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@MEAA_JPEA_W_J@Z */
/* ?xsgetn@?$basic_streambuf@GU?$char_traits@G@std@@@std@@MAEHPAGH@Z */
/* ?xsgetn@?$basic_streambuf@GU?$char_traits@G@std@@@std@@MEAA_JPEAG_J@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_xsgetn, 12)
#define call_basic_streambuf_wchar_xsgetn(this, ptr, count) CALL_VTBL_FUNC(this, 24, \
        streamsize, (basic_streambuf_wchar*, wchar_t*, streamsize), (this, ptr, count))
streamsize __thiscall basic_streambuf_wchar_xsgetn(basic_streambuf_wchar *this, wchar_t *ptr, streamsize count)
{
    TRACE("(%p %p %ld)\n", this, ptr, count);
    return call_basic_streambuf_wchar__Xsgetn_s(this, ptr, -1, count);
}

/* ?xsputn@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@MAEHPB_WH@Z */
/* ?xsputn@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@MEAA_JPEB_W_J@Z */
/* ?xsputn@?$basic_streambuf@GU?$char_traits@G@std@@@std@@MAEHPBGH@Z */
/* ?xsputn@?$basic_streambuf@GU?$char_traits@G@std@@@std@@MEAA_JPEBG_J@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_xsputn, 12)
#define call_basic_streambuf_wchar_xsputn(this, ptr, count) CALL_VTBL_FUNC(this, 32, \
        streamsize, (basic_streambuf_wchar*, const wchar_t*, streamsize), (this, ptr, count))
streamsize __thiscall basic_streambuf_wchar_xsputn(basic_streambuf_wchar *this, const wchar_t *ptr, streamsize count)
{
    streamsize copied, chunk;

    TRACE("(%p %p %ld)\n", this, ptr, count);

    for(copied=0; copied<count;) {
        chunk = basic_streambuf_wchar__Pnavail(this);
        if(chunk > count-copied)
            chunk = count-copied;

        if(chunk) {
            memcpy(*this->pwpos, ptr+copied, chunk);
            *this->pwpos += chunk;
            *this->pwsize -= chunk;
            copied += chunk;
        }else if(call_basic_streambuf_wchar_overflow(this, ptr[copied]) != WEOF) {
            copied++;
        }else {
            break;
        }
    }

    return copied;
}

/* ?sputn@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QAEHPB_WH@Z */
/* ?sputn@?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@QEAA_JPEB_W_J@Z */
/* ?sputn@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QAEHPBGH@Z */
/* ?sputn@?$basic_streambuf@GU?$char_traits@G@std@@@std@@QEAA_JPEBG_J@Z */
DEFINE_THISCALL_WRAPPER(basic_streambuf_wchar_sputn, 12)
streamsize __thiscall basic_streambuf_wchar_sputn(basic_streambuf_wchar *this, const wchar_t *ptr, streamsize count)
{
    TRACE("(%p %p %ld)\n", this, ptr, count);
    return call_basic_streambuf_wchar_xsputn(this, ptr, count);
}

/* ??0ios_base@std@@IAE@XZ */
/* ??0ios_base@std@@IEAA@XZ */
DEFINE_THISCALL_WRAPPER(ios_base_ctor, 4)
ios_base* __thiscall ios_base_ctor(ios_base *this)
{
    TRACE("(%p)\n", this);
    this->vtable = &MSVCP_ios_base_vtable;
    return this;
}

/* ??0ios_base@std@@QAE@ABV01@@Z */
/* ??0ios_base@std@@QEAA@AEBV01@@Z */
DEFINE_THISCALL_WRAPPER(ios_base_copy_ctor, 8)
ios_base* __thiscall ios_base_copy_ctor(ios_base *this, const ios_base *copy)
{
    TRACE("(%p %p)\n", this, copy);
    *this = *copy;
    this->vtable = &MSVCP_ios_base_vtable;
    return this;
}

/* ?_Callfns@ios_base@std@@AAEXW4event@12@@Z */
/* ?_Callfns@ios_base@std@@AEAAXW4event@12@@Z */
DEFINE_THISCALL_WRAPPER(ios_base_Callfns, 8)
void __thiscall ios_base_Callfns(ios_base *this, IOS_BASE_event event)
{
    IOS_BASE_fnarray *cur;

    TRACE("(%p %x)\n", this, event);

    for(cur=this->calls; cur; cur=cur->next)
        cur->event_handler(event, this, cur->index);
}

/* ?_Tidy@ios_base@std@@AAAXXZ */
/* ?_Tidy@ios_base@std@@AEAAXXZ */
void CDECL ios_base_Tidy(ios_base *this)
{
    IOS_BASE_iosarray *arr_cur, *arr_next;
    IOS_BASE_fnarray *event_cur, *event_next;

    TRACE("(%p)\n", this);

    ios_base_Callfns(this, EVENT_erase_event);

    for(arr_cur=this->arr; arr_cur; arr_cur=arr_next) {
        arr_next = arr_cur->next;
        MSVCRT_operator_delete(arr_cur);
    }
    this->arr = NULL;

    for(event_cur=this->calls; event_cur; event_cur=event_next) {
        event_next = event_cur->next;
        MSVCRT_operator_delete(event_cur);
    }
    this->calls = NULL;
}

/* ?_Ios_base_dtor@ios_base@std@@CAXPAV12@@Z */
/* ?_Ios_base_dtor@ios_base@std@@CAXPEAV12@@Z */
void CDECL ios_base_Ios_base_dtor(ios_base *obj)
{
    TRACE("(%p)\n", obj);
    if(obj->loc) {
        locale_dtor(obj->loc);
        MSVCRT_operator_delete(obj->loc);
    }
    ios_base_Tidy(obj);
}

/* ??1ios_base@std@@UAE@XZ */
/* ??1ios_base@std@@UEAA@XZ */
DEFINE_THISCALL_WRAPPER(ios_base_dtor, 4)
void __thiscall ios_base_dtor(ios_base *this)
{
    ios_base_Ios_base_dtor(this);
}

DEFINE_THISCALL_WRAPPER(MSVCP_ios_base_vector_dtor, 8)
ios_base* __thiscall MSVCP_ios_base_vector_dtor(ios_base *this, unsigned int flags)
{
    TRACE("(%p %x)\n", this, flags);
    if(flags & 2) {
        /* we have an array, with the number of elements stored before the first object */
        int i, *ptr = (int *)this-1;

        for(i=*ptr-1; i>=0; i--)
            ios_base_dtor(this+i);
        MSVCRT_operator_delete(ptr);
    } else {
        ios_base_dtor(this);
        if(flags & 1)
            MSVCRT_operator_delete(this);
    }

    return this;
}

DEFINE_THISCALL_WRAPPER(MSVCP_iosb_vector_dtor, 8)
void* __thiscall MSVCP_iosb_vector_dtor(void *this, unsigned int flags)
{
    TRACE("(%p %x)\n", this, flags);
    if(flags & 2) {
        int *ptr = (int *)this-1;
        MSVCRT_operator_delete(ptr);
    } else {
        if(flags & 1)
            MSVCRT_operator_delete(this);
    }

    return this;
}

/* ?_Findarr@ios_base@std@@AAEAAU_Iosarray@12@H@Z */
/* ?_Findarr@ios_base@std@@AEAAAEAU_Iosarray@12@H@Z */
DEFINE_THISCALL_WRAPPER(ios_base_Findarr, 8)
IOS_BASE_iosarray* __thiscall ios_base_Findarr(ios_base *this, int index)
{
    IOS_BASE_iosarray *p;

    TRACE("(%p %d)\n", this, index);

    for(p=this->arr; p; p=p->next) {
        if(p->index == index)
            return p;
    }

    for(p=this->arr; p; p=p->next) {
        if(!p->long_val && !p->ptr_val) {
            p->index = index;
            return p;
        }
    }

    p = MSVCRT_operator_new(sizeof(IOS_BASE_iosarray));
    p->next = this->arr;
    p->index = index;
    p->long_val = 0;
    p->ptr_val = NULL;
    this->arr = p;
    return p;
}

/* ?iword@ios_base@std@@QAEAAJH@Z */
/* ?iword@ios_base@std@@QEAAAEAJH@Z */
DEFINE_THISCALL_WRAPPER(ios_base_iword, 8)
LONG* __thiscall ios_base_iword(ios_base *this, int index)
{
    TRACE("(%p %d)\n", this, index);
    return &ios_base_Findarr(this, index)->long_val;
}

/* ?pword@ios_base@std@@QAEAAPAXH@Z */
/* ?pword@ios_base@std@@QEAAAEAPEAXH@Z */
DEFINE_THISCALL_WRAPPER(ios_base_pword, 8)
void** __thiscall ios_base_pword(ios_base *this, int index)
{
    TRACE("(%p %d)\n", this, index);
    return &ios_base_Findarr(this, index)->ptr_val;
}

/* ?register_callback@ios_base@std@@QAEXP6AXW4event@12@AAV12@H@ZH@Z */
/* ?register_callback@ios_base@std@@QEAAXP6AXW4event@12@AEAV12@H@ZH@Z */
DEFINE_THISCALL_WRAPPER(ios_base_register_callback, 12)
void __thiscall ios_base_register_callback(ios_base *this, IOS_BASE_event_callback callback, int index)
{
    IOS_BASE_fnarray *event;

    TRACE("(%p %p %d)\n", this, callback, index);

    event = MSVCRT_operator_new(sizeof(IOS_BASE_fnarray));
    event->next = this->calls;
    event->index = index;
    event->event_handler = callback;
    this->calls = event;
}

/* ?clear@ios_base@std@@QAEXH_N@Z */
/* ?clear@ios_base@std@@QEAAXH_N@Z */
DEFINE_THISCALL_WRAPPER(ios_base_clear_reraise, 12)
void __thiscall ios_base_clear_reraise(ios_base *this, IOSB_iostate state, MSVCP_bool reraise)
{
    TRACE("(%p %x %x)\n", this, state, reraise);

    this->state = state & IOSTATE_mask;
    if(!(this->state & this->except))
        return;

    if(reraise)
        throw_exception(EXCEPTION_RERAISE, NULL);
    else if(this->state & this->except & IOSTATE_eofbit)
        throw_exception(EXCEPTION_FAILURE, "eofbit is set");
    else if(this->state & this->except & IOSTATE_failbit)
        throw_exception(EXCEPTION_FAILURE, "failbit is set");
    else if(this->state & this->except & IOSTATE_badbit)
        throw_exception(EXCEPTION_FAILURE, "badbit is set");
    else if(this->state & this->except & IOSTATE__Hardfail)
        throw_exception(EXCEPTION_FAILURE, "_Hardfail is set");
}

/* ?clear@ios_base@std@@QAEXH@Z */
/* ?clear@ios_base@std@@QEAAXH@Z */
DEFINE_THISCALL_WRAPPER(ios_base_clear, 8)
void __thiscall ios_base_clear(ios_base *this, IOSB_iostate state)
{
    ios_base_clear_reraise(this, state, FALSE);
}

/* ?clear@ios_base@std@@QAEXI@Z */
/* ?clear@ios_base@std@@QEAAXI@Z */
DEFINE_THISCALL_WRAPPER(ios_base_clear_unsigned, 8)
void __thiscall ios_base_clear_unsigned(ios_base *this, unsigned int state)
{
    ios_base_clear_reraise(this, (IOSB_iostate)state, FALSE);
}

/* ?exceptions@ios_base@std@@QAEXH@Z */
/* ?exceptions@ios_base@std@@QEAAXH@Z */
DEFINE_THISCALL_WRAPPER(ios_base_exceptions_set, 8)
void __thiscall ios_base_exceptions_set(ios_base *this, IOSB_iostate state)
{
    TRACE("(%p %x)\n", this, state);
    this->except = state & IOSTATE_mask;
    ios_base_clear(this, this->state);
}

/* ?exceptions@ios_base@std@@QAEXI@Z */
/* ?exceptions@ios_base@std@@QEAAXI@Z */
DEFINE_THISCALL_WRAPPER(ios_base_exceptions_set_unsigned, 8)
void __thiscall ios_base_exceptions_set_unsigned(ios_base *this, unsigned int state)
{
    TRACE("(%p %x)\n", this, state);
    ios_base_exceptions_set(this, state);
}

/* ?exceptions@ios_base@std@@QBEHXZ */
/* ?exceptions@ios_base@std@@QEBAHXZ */
DEFINE_THISCALL_WRAPPER(ios_base_exceptions_get, 4)
IOSB_iostate __thiscall ios_base_exceptions_get(ios_base *this)
{
    TRACE("(%p)\n", this);
    return this->except;
}

/* ?copyfmt@ios_base@std@@QAEAAV12@ABV12@@Z */
/* ?copyfmt@ios_base@std@@QEAAAEAV12@AEBV12@@Z */
DEFINE_THISCALL_WRAPPER(ios_base_copyfmt, 8)
ios_base* __thiscall ios_base_copyfmt(ios_base *this, const ios_base *rhs)
{
    TRACE("(%p %p)\n", this, rhs);

    if(this != rhs) {
        IOS_BASE_iosarray *arr_cur;
        IOS_BASE_fnarray *event_cur;

        ios_base_Tidy(this);

        for(arr_cur=rhs->arr; arr_cur; arr_cur=arr_cur->next) {
            if(arr_cur->long_val)
                *ios_base_iword(this, arr_cur->index) = arr_cur->long_val;
            if(arr_cur->ptr_val)
                *ios_base_pword(this, arr_cur->index) = arr_cur->ptr_val;
        }
        this->stdstr = rhs->stdstr;
        this->fmtfl = rhs->fmtfl;
        this->prec = rhs->prec;
        this->wide = rhs->wide;
        locale_operator_assign(this->loc, rhs->loc);

        for(event_cur=rhs->calls; event_cur; event_cur=event_cur->next)
            ios_base_register_callback(this, event_cur->event_handler, event_cur->index);

        ios_base_Callfns(this, EVENT_copyfmt_event);
        ios_base_exceptions_set(this, rhs->except);
    }

    return this;
}

/* ??4ios_base@std@@QAEAAV01@ABV01@@Z */
/* ??4ios_base@std@@QEAAAEAV01@AEBV01@@Z */
DEFINE_THISCALL_WRAPPER(ios_base_assign, 8)
ios_base* __thiscall ios_base_assign(ios_base *this, const ios_base *right)
{
    TRACE("(%p %p)\n", this, right);

    if(this != right) {
        this->state = right->state;
        ios_base_copyfmt(this, right);
    }

    return this;
}

/* ?fail@ios_base@std@@QBE_NXZ */
/* ?fail@ios_base@std@@QEBA_NXZ */
DEFINE_THISCALL_WRAPPER(ios_base_fail, 4)
MSVCP_bool __thiscall ios_base_fail(const ios_base *this)
{
    TRACE("(%p)\n", this);
    return (this->state & (IOSTATE_failbit|IOSTATE_badbit)) != 0;
}

/* ??7ios_base@std@@QBE_NXZ */
/* ??7ios_base@std@@QEBA_NXZ */
DEFINE_THISCALL_WRAPPER(ios_base_op_succ, 4)
MSVCP_bool __thiscall ios_base_op_succ(const ios_base *this)
{
    TRACE("(%p)\n", this);
    return ios_base_fail(this);
}

/* ??Bios_base@std@@QBEPAXXZ */
/* ??Bios_base@std@@QEBAPEAXXZ */
DEFINE_THISCALL_WRAPPER(ios_base_op_fail, 4)
void* __thiscall ios_base_op_fail(const ios_base *this)
{
    TRACE("(%p)\n", this);
    return ios_base_fail(this) ? NULL : (void*)this;
}

/* ?_Addstd@ios_base@std@@SAXPAV12@@Z */
/* ?_Addstd@ios_base@std@@SAXPEAV12@@Z */
void CDECL ios_base_Addstd(ios_base *add)
{
    FIXME("(%p) stub\n", add);
}

/* ?_Index_func@ios_base@std@@CAAAHXZ */
/* ?_Index_func@ios_base@std@@CAAEAHXZ */
int* CDECL ios_base_Index_func(void)
{
    TRACE("\n");
    return &ios_base_Index;
}

/* ?_Init@ios_base@std@@IAEXXZ */
/* ?_Init@ios_base@std@@IEAAXXZ */
DEFINE_THISCALL_WRAPPER(ios_base_Init, 4)
void __thiscall ios_base_Init(ios_base *this)
{
    TRACE("(%p)\n", this);

    this->stdstr = 0;
    this->state = this->except = IOSTATE_goodbit;
    this->fmtfl = FMTFLAG_skipws | FMTFLAG_dec;
    this->prec = 6;
    this->wide = 0;
    this->arr = NULL;
    this->calls = NULL;
    this->loc = MSVCRT_operator_new(sizeof(locale));
    locale_ctor(this->loc);
}

/* ?_Sync_func@ios_base@std@@CAAA_NXZ */
/* ?_Sync_func@ios_base@std@@CAAEA_NXZ */
MSVCP_bool* CDECL ios_base_Sync_func(void)
{
    TRACE("\n");
    return &ios_base_Sync;
}

/* ?bad@ios_base@std@@QBE_NXZ */
/* ?bad@ios_base@std@@QEBA_NXZ */
DEFINE_THISCALL_WRAPPER(ios_base_bad, 4)
MSVCP_bool __thiscall ios_base_bad(const ios_base *this)
{
    TRACE("(%p)\n", this);
    return (this->state & IOSTATE_badbit) != 0;
}

/* ?eof@ios_base@std@@QBE_NXZ */
/* ?eof@ios_base@std@@QEBA_NXZ */
DEFINE_THISCALL_WRAPPER(ios_base_eof, 4)
MSVCP_bool __thiscall ios_base_eof(const ios_base *this)
{
    TRACE("(%p)\n", this);
    return (this->state & IOSTATE_eofbit) != 0;
}

/* ?flags@ios_base@std@@QAEHH@Z */
/* ?flags@ios_base@std@@QEAAHH@Z */
DEFINE_THISCALL_WRAPPER(ios_base_flags_set, 8)
IOSB_fmtflags __thiscall ios_base_flags_set(ios_base *this, IOSB_fmtflags flags)
{
    IOSB_fmtflags ret = this->fmtfl;

    TRACE("(%p %x)\n", this, flags);

    this->fmtfl = flags & FMTFLAG_mask;
    return ret;
}

/* ?flags@ios_base@std@@QBEHXZ */
/* ?flags@ios_base@std@@QEBAHXZ */
DEFINE_THISCALL_WRAPPER(ios_base_flags_get, 4)
IOSB_fmtflags __thiscall ios_base_flags_get(const ios_base *this)
{
    TRACE("(%p)\n", this);
    return this->fmtfl;
}

/* ?getloc@ios_base@std@@QBE?AVlocale@2@XZ */
/* ?getloc@ios_base@std@@QEBA?AVlocale@2@XZ */
DEFINE_THISCALL_WRAPPER(ios_base_getloc, 8)
locale* __thiscall ios_base_getloc(const ios_base *this, locale *ret)
{
    TRACE("(%p)\n", this);
    return locale_copy_ctor(ret, this->loc);
}

/* ?good@ios_base@std@@QBE_NXZ */
/* ?good@ios_base@std@@QEBA_NXZ */
DEFINE_THISCALL_WRAPPER(ios_base_good, 4)
MSVCP_bool __thiscall ios_base_good(const ios_base *this)
{
    TRACE("(%p)\n", this);
    return this->state == IOSTATE_goodbit;
}

/* ?imbue@ios_base@std@@QAE?AVlocale@2@ABV32@@Z */
/* ?imbue@ios_base@std@@QEAA?AVlocale@2@AEBV32@@Z */
DEFINE_THISCALL_WRAPPER(ios_base_imbue, 12)
locale* __thiscall ios_base_imbue(ios_base *this, locale *ret, const locale *loc)
{
    TRACE("(%p %p)\n", this, loc);
    *ret = *this->loc;
    locale_copy_ctor(this->loc, loc);
    return ret;
}

/* ?precision@ios_base@std@@QAEHH@Z */
/* ?precision@ios_base@std@@QEAA_J_J@Z */
DEFINE_THISCALL_WRAPPER(ios_base_precision_set, 8)
streamsize __thiscall ios_base_precision_set(ios_base *this, streamsize precision)
{
    streamsize ret = this->prec;

    TRACE("(%p %ld)\n", this, precision);

    this->prec = precision;
    return ret;
}

/* ?precision@ios_base@std@@QBEHXZ */
/* ?precision@ios_base@std@@QEBA_JXZ */
DEFINE_THISCALL_WRAPPER(ios_base_precision_get, 4)
streamsize __thiscall ios_base_precision_get(const ios_base *this)
{
    TRACE("(%p)\n", this);
    return this->prec;
}

/* ?rdstate@ios_base@std@@QBEHXZ */
/* ?rdstate@ios_base@std@@QEBAHXZ */
DEFINE_THISCALL_WRAPPER(ios_base_rdstate, 4)
IOSB_iostate __thiscall ios_base_rdstate(const ios_base *this)
{
    TRACE("(%p)\n", this);
    return this->state;
}

/* ?setf@ios_base@std@@QAEHHH@Z */
/* ?setf@ios_base@std@@QEAAHHH@Z */
DEFINE_THISCALL_WRAPPER(ios_base_setf_mask, 12)
IOSB_fmtflags __thiscall ios_base_setf_mask(ios_base *this, IOSB_fmtflags flags, IOSB_fmtflags mask)
{
    IOSB_fmtflags ret = this->fmtfl;

    TRACE("(%p %x %x)\n", this, flags, mask);

    this->fmtfl = (this->fmtfl & (~mask)) | (flags & mask & FMTFLAG_mask);
    return ret;
}

/* ?setf@ios_base@std@@QAEHH@Z */
/* ?setf@ios_base@std@@QEAAHH@Z */
DEFINE_THISCALL_WRAPPER(ios_base_setf, 8)
IOSB_fmtflags __thiscall ios_base_setf(ios_base *this, IOSB_fmtflags flags)
{
    return ios_base_setf_mask(this, flags, ~0);
}

/* ?setstate@ios_base@std@@QAEXH_N@Z */
/* ?setstate@ios_base@std@@QEAAXH_N@Z */
DEFINE_THISCALL_WRAPPER(ios_base_setstate_reraise, 12)
void __thiscall ios_base_setstate_reraise(ios_base *this, IOSB_iostate state, MSVCP_bool reraise)
{
    TRACE("(%p %x %x)\n", this, state, reraise);

    if(state != IOSTATE_goodbit)
        ios_base_clear_reraise(this, this->state | state, reraise);
}

/* ?setstate@ios_base@std@@QAEXH@Z */
/* ?setstate@ios_base@std@@QEAAXH@Z */
DEFINE_THISCALL_WRAPPER(ios_base_setstate, 8)
void __thiscall ios_base_setstate(ios_base *this, IOSB_iostate state)
{
    ios_base_setstate_reraise(this, state, FALSE);
}

/* ?setstate@ios_base@std@@QAEXI@Z */
/* ?setstate@ios_base@std@@QEAAXI@Z */
DEFINE_THISCALL_WRAPPER(ios_base_setstate_unsigned, 8)
void __thiscall ios_base_setstate_unsigned(ios_base *this, unsigned int state)
{
    ios_base_setstate_reraise(this, (IOSB_iostate)state, FALSE);
}

/* ?sync_with_stdio@ios_base@std@@SA_N_N@Z */
MSVCP_bool CDECL ios_base_sync_with_stdio(MSVCP_bool sync)
{
    _Lockit lock;
    MSVCP_bool ret;

    TRACE("(%x)\n", sync);

    _Lockit_ctor_locktype(&lock, _LOCK_STREAM);
    ret = ios_base_Sync;
    ios_base_Sync = sync;
    _Lockit_dtor(&lock);
    return ret;
}

/* ?unsetf@ios_base@std@@QAEXH@Z */
/* ?unsetf@ios_base@std@@QEAAXH@Z */
DEFINE_THISCALL_WRAPPER(ios_base_unsetf, 8)
void __thiscall ios_base_unsetf(ios_base *this, IOSB_fmtflags flags)
{
    TRACE("(%p %x)\n", this, flags);
    this->fmtfl &= ~flags;
}

/* ?width@ios_base@std@@QAEHH@Z */
/* ?width@ios_base@std@@QEAA_J_J@Z */
DEFINE_THISCALL_WRAPPER(ios_base_width_set, 8)
streamsize __thiscall ios_base_width_set(ios_base *this, streamsize width)
{
    streamsize ret = this->wide;

    TRACE("(%p %ld)\n", this, width);

    this->wide = width;
    return ret;
}

/* ?width@ios_base@std@@QBEHXZ */
/* ?width@ios_base@std@@QEBA_JXZ */
DEFINE_THISCALL_WRAPPER(ios_base_width_get, 4)
streamsize __thiscall ios_base_width_get(ios_base *this)
{
    TRACE("(%p)\n", this);
    return this->wide;
}

/* ?xalloc@ios_base@std@@SAHXZ */
int CDECL ios_base_xalloc(void)
{
    _Lockit lock;
    int ret;

    TRACE("\n");

    _Lockit_ctor_locktype(&lock, _LOCK_STREAM);
    ret = ios_base_Index++;
    _Lockit_dtor(&lock);
    return ret;
}

/* ??0?$basic_ios@DU?$char_traits@D@std@@@std@@IAE@XZ */
/* ??0?$basic_ios@DU?$char_traits@D@std@@@std@@IEAA@XZ */
DEFINE_THISCALL_WRAPPER(basic_ios_char_ctor, 4)
basic_ios_char* __thiscall basic_ios_char_ctor(basic_ios_char *this)
{
    TRACE("(%p)\n", this);

    ios_base_ctor(&this->base);
    this->base.vtable = &MSVCP_basic_ios_char_vtable;
    return this;
}

/* ?init@?$basic_ios@DU?$char_traits@D@std@@@std@@IAEXPAV?$basic_streambuf@DU?$char_traits@D@std@@@2@_N@Z */
/* ?init@?$basic_ios@DU?$char_traits@D@std@@@std@@IEAAXPEAV?$basic_streambuf@DU?$char_traits@D@std@@@2@_N@Z */
DEFINE_THISCALL_WRAPPER(basic_ios_char_init, 12)
void __thiscall basic_ios_char_init(basic_ios_char *this, basic_streambuf_char *streambuf, MSVCP_bool isstd)
{
    TRACE("(%p %p %x)\n", this, streambuf, isstd);
    ios_base_Init(&this->base);
    this->strbuf = streambuf;
    this->stream = NULL;
    this->fillch = ' ';

    if(!streambuf)
        ios_base_setstate(&this->base, IOSTATE_badbit);

    if(isstd)
        FIXME("standard streams not handled yet\n");
}

/* ??0?$basic_ios@DU?$char_traits@D@std@@@std@@QAE@PAV?$basic_streambuf@DU?$char_traits@D@std@@@1@@Z */
/* ??0?$basic_ios@DU?$char_traits@D@std@@@std@@QEAA@PEAV?$basic_streambuf@DU?$char_traits@D@std@@@1@@Z */
DEFINE_THISCALL_WRAPPER(basic_ios_char_ctor_streambuf, 8)
basic_ios_char* __thiscall basic_ios_char_ctor_streambuf(basic_ios_char *this, basic_streambuf_char *strbuf)
{
    TRACE("(%p %p)\n", this, strbuf);

    basic_ios_char_ctor(this);
    basic_ios_char_init(this, strbuf, FALSE);
    return this;
}

/* ??1?$basic_ios@DU?$char_traits@D@std@@@std@@UAE@XZ */
/* ??1?$basic_ios@DU?$char_traits@D@std@@@std@@UEAA@XZ */
DEFINE_THISCALL_WRAPPER(basic_ios_char_dtor, 4)
void __thiscall basic_ios_char_dtor(basic_ios_char *this)
{
    TRACE("(%p)\n", this);
    ios_base_dtor(&this->base);
}

DEFINE_THISCALL_WRAPPER(MSVCP_basic_ios_char_vector_dtor, 8)
basic_ios_char* __thiscall MSVCP_basic_ios_char_vector_dtor(basic_ios_char *this, unsigned int flags)
{
    TRACE("(%p %x)\n", this, flags);
    if(flags & 2) {
        /* we have an array, with the number of elements stored before the first object */
        int i, *ptr = (int *)this-1;

        for(i=*ptr-1; i>=0; i--)
            basic_ios_char_dtor(this+i);
        MSVCRT_operator_delete(ptr);
    } else {
        basic_ios_char_dtor(this);
        if(flags & 1)
            MSVCRT_operator_delete(this);
    }

    return this;
}

/* ?clear@?$basic_ios@DU?$char_traits@D@std@@@std@@QAEXH_N@Z */
/* ?clear@?$basic_ios@DU?$char_traits@D@std@@@std@@QEAAXH_N@Z */
DEFINE_THISCALL_WRAPPER(basic_ios_char_clear_reraise, 12)
void __thiscall basic_ios_char_clear_reraise(basic_ios_char *this, IOSB_iostate state, MSVCP_bool reraise)
{
    TRACE("(%p %x %x)\n", this, state, reraise);
    ios_base_clear_reraise(&this->base, state | (this->strbuf ? IOSTATE_goodbit : IOSTATE_badbit), reraise);
}

/* ?clear@?$basic_ios@DU?$char_traits@D@std@@@std@@QAEXI@Z */
/* ?clear@?$basic_ios@DU?$char_traits@D@std@@@std@@QEAAXI@Z */
DEFINE_THISCALL_WRAPPER(basic_ios_char_clear, 8)
void __thiscall basic_ios_char_clear(basic_ios_char *this, unsigned int state)
{
    basic_ios_char_clear_reraise(this, (IOSB_iostate)state, FALSE);
}

/* ?copyfmt@?$basic_ios@DU?$char_traits@D@std@@@std@@QAEAAV12@ABV12@@Z */
/* ?copyfmt@?$basic_ios@DU?$char_traits@D@std@@@std@@QEAAAEAV12@AEBV12@@Z */
DEFINE_THISCALL_WRAPPER(basic_ios_char_copyfmt, 8)
basic_ios_char* __thiscall basic_ios_char_copyfmt(basic_ios_char *this, basic_ios_char *copy)
{
    TRACE("(%p %p)\n", this, copy);
    if(this == copy)
        return this;

    this->stream = copy->stream;
    this->fillch = copy->fillch;
    ios_base_copyfmt(&this->base, &copy->base);
    return this;
}

/* ?fill@?$basic_ios@DU?$char_traits@D@std@@@std@@QAEDD@Z */
/* ?fill@?$basic_ios@DU?$char_traits@D@std@@@std@@QEAADD@Z */
DEFINE_THISCALL_WRAPPER(basic_ios_char_fill_set, 8)
char __thiscall basic_ios_char_fill_set(basic_ios_char *this, char fill)
{
    char ret = this->fillch;

    TRACE("(%p %c)\n", this, fill);

    this->fillch = fill;
    return ret;
}

/* ?fill@?$basic_ios@DU?$char_traits@D@std@@@std@@QBEDXZ */
/* ?fill@?$basic_ios@DU?$char_traits@D@std@@@std@@QEBADXZ */
DEFINE_THISCALL_WRAPPER(basic_ios_char_fill_get, 4)
char __thiscall basic_ios_char_fill_get(basic_ios_char *this)
{
    TRACE("(%p)\n", this);
    return this->fillch;
}

/* ?imbue@?$basic_ios@DU?$char_traits@D@std@@@std@@QAE?AVlocale@2@ABV32@@Z */
/* ?imbue@?$basic_ios@DU?$char_traits@D@std@@@std@@QEAA?AVlocale@2@AEBV32@@Z */
DEFINE_THISCALL_WRAPPER(basic_ios_char_imbue, 12)
locale *__thiscall basic_ios_char_imbue(basic_ios_char *this, locale *ret, const locale *loc)
{
    TRACE("(%p %p %p)\n", this, ret, loc);

    if(this->strbuf)
        return basic_streambuf_char_pubimbue(this->strbuf, ret, loc);

    locale_copy_ctor(ret, loc);
    return ret;
}

/* ?narrow@?$basic_ios@DU?$char_traits@D@std@@@std@@QBEDDD@Z */
/* ?narrow@?$basic_ios@DU?$char_traits@D@std@@@std@@QEBADDD@Z */
DEFINE_THISCALL_WRAPPER(basic_ios_char_narrow, 12)
char __thiscall basic_ios_char_narrow(basic_ios_char *this, char ch, char def)
{
    FIXME("(%p %c %c) stub\n", this, ch, def);
    return def;
}

/* ?rdbuf@?$basic_ios@DU?$char_traits@D@std@@@std@@QAEPAV?$basic_streambuf@DU?$char_traits@D@std@@@2@PAV32@@Z */
/* ?rdbuf@?$basic_ios@DU?$char_traits@D@std@@@std@@QEAAPEAV?$basic_streambuf@DU?$char_traits@D@std@@@2@PEAV32@@Z */
DEFINE_THISCALL_WRAPPER(basic_ios_char_rdbuf_set, 8)
basic_streambuf_char* __thiscall basic_ios_char_rdbuf_set(basic_ios_char *this, basic_streambuf_char *streambuf)
{
    basic_streambuf_char *ret = this->strbuf;

    TRACE("(%p %p)\n", this, streambuf);

    this->strbuf = streambuf;
    basic_ios_char_clear(this, IOSTATE_goodbit);
    return ret;
}

/* ?rdbuf@?$basic_ios@DU?$char_traits@D@std@@@std@@QBEPAV?$basic_streambuf@DU?$char_traits@D@std@@@2@XZ */
/* ?rdbuf@?$basic_ios@DU?$char_traits@D@std@@@std@@QEBAPEAV?$basic_streambuf@DU?$char_traits@D@std@@@2@XZ */
DEFINE_THISCALL_WRAPPER(basic_ios_char_rdbuf_get, 4)
basic_streambuf_char* __thiscall basic_ios_char_rdbuf_get(const basic_ios_char *this)
{
    TRACE("(%p)\n", this);
    return this->strbuf;
}

/* ?setstate@?$basic_ios@DU?$char_traits@D@std@@@std@@QAEXH_N@Z */
/* ?setstate@?$basic_ios@DU?$char_traits@D@std@@@std@@QEAAXH_N@Z */
DEFINE_THISCALL_WRAPPER(basic_ios_char_setstate_reraise, 12)
void __thiscall basic_ios_char_setstate_reraise(basic_ios_char *this, IOSB_iostate state, MSVCP_bool reraise)
{
    TRACE("(%p %x %x)\n", this, state, reraise);

    if(state != IOSTATE_goodbit)
        basic_ios_char_clear_reraise(this, this->base.state | state, reraise);
}

/* ?setstate@?$basic_ios@DU?$char_traits@D@std@@@std@@QAEXI@Z */
/* ?setstate@?$basic_ios@DU?$char_traits@D@std@@@std@@QEAAXI@Z */
DEFINE_THISCALL_WRAPPER(basic_ios_char_setstate, 8)
void __thiscall basic_ios_char_setstate(basic_ios_char *this, unsigned int state)
{
    basic_ios_char_setstate_reraise(this, (IOSB_iostate)state, FALSE);
}

/* ?tie@?$basic_ios@DU?$char_traits@D@std@@@std@@QAEPAV?$basic_ostream@DU?$char_traits@D@std@@@2@PAV32@@Z */
/* ?tie@?$basic_ios@DU?$char_traits@D@std@@@std@@QEAAPEAV?$basic_ostream@DU?$char_traits@D@std@@@2@PEAV32@@Z */
DEFINE_THISCALL_WRAPPER(basic_ios_char_tie_set, 8)
basic_ostream_char* __thiscall basic_ios_char_tie_set(basic_ios_char *this, basic_ostream_char *ostream)
{
    basic_ostream_char *ret = this->stream;

    TRACE("(%p %p)\n", this, ostream);

    this->stream = ostream;
    return ret;
}

/* ?tie@?$basic_ios@DU?$char_traits@D@std@@@std@@QBEPAV?$basic_ostream@DU?$char_traits@D@std@@@2@XZ */
/* ?tie@?$basic_ios@DU?$char_traits@D@std@@@std@@QEBAPEAV?$basic_ostream@DU?$char_traits@D@std@@@2@XZ */
DEFINE_THISCALL_WRAPPER(basic_ios_char_tie_get, 4)
basic_ostream_char* __thiscall basic_ios_char_tie_get(const basic_ios_char *this)
{
    TRACE("(%p)\n", this);
    return this->stream;
}

/* ?widen@?$basic_ios@DU?$char_traits@D@std@@@std@@QBEDD@Z */
/* ?widen@?$basic_ios@DU?$char_traits@D@std@@@std@@QEBADD@Z */
DEFINE_THISCALL_WRAPPER(basic_ios_char_widen, 8)
char __thiscall basic_ios_char_widen(basic_ios_char *this, char ch)
{
    FIXME("(%p %c) stub\n", this, ch);
    return 0;
}


/* ??0?$basic_ios@_WU?$char_traits@_W@std@@@std@@IAE@XZ */
/* ??0?$basic_ios@_WU?$char_traits@_W@std@@@std@@IEAA@XZ */
DEFINE_THISCALL_WRAPPER(basic_ios_wchar_ctor, 4)
basic_ios_wchar* __thiscall basic_ios_wchar_ctor(basic_ios_wchar *this)
{
    TRACE("(%p)\n", this);

    ios_base_ctor(&this->base);
    this->base.vtable = &MSVCP_basic_ios_wchar_vtable;
    return this;
}

/* ??0?$basic_ios@GU?$char_traits@G@std@@@std@@IAE@XZ */
/* ??0?$basic_ios@GU?$char_traits@G@std@@@std@@IEAA@XZ */
DEFINE_THISCALL_WRAPPER(basic_ios_short_ctor, 4)
basic_ios_wchar* __thiscall basic_ios_short_ctor(basic_ios_wchar *this)
{
    basic_ios_wchar_ctor(this);
    this->base.vtable = &MSVCP_basic_ios_short_vtable;
    return this;
}

/* ?init@?$basic_ios@_WU?$char_traits@_W@std@@@std@@IAEXPAV?$basic_streambuf@_WU?$char_traits@_W@std@@@2@_N@Z */
/* ?init@?$basic_ios@_WU?$char_traits@_W@std@@@std@@IEAAXPEAV?$basic_streambuf@_WU?$char_traits@_W@std@@@2@_N@Z */
/* ?init@?$basic_ios@GU?$char_traits@G@std@@@std@@IAEXPAV?$basic_streambuf@GU?$char_traits@G@std@@@2@_N@Z */
/* ?init@?$basic_ios@GU?$char_traits@G@std@@@std@@IEAAXPEAV?$basic_streambuf@GU?$char_traits@G@std@@@2@_N@Z */
DEFINE_THISCALL_WRAPPER(basic_ios_wchar_init, 12)
void __thiscall basic_ios_wchar_init(basic_ios_wchar *this, basic_streambuf_wchar *streambuf, MSVCP_bool isstd)
{
    TRACE("(%p %p %x)\n", this, streambuf, isstd);
    ios_base_Init(&this->base);
    this->strbuf = streambuf;
    this->stream = NULL;
    this->fillch = ' ';

    if(!streambuf)
        ios_base_setstate(&this->base, IOSTATE_badbit);

    if(isstd)
        FIXME("standard streams not handled yet\n");
}

/* ??0?$basic_ios@_WU?$char_traits@_W@std@@@std@@QAE@PAV?$basic_streambuf@_WU?$char_traits@_W@std@@@1@@Z */
/* ??0?$basic_ios@_WU?$char_traits@_W@std@@@std@@QEAA@PEAV?$basic_streambuf@_WU?$char_traits@_W@std@@@1@@Z */
DEFINE_THISCALL_WRAPPER(basic_ios_wchar_ctor_streambuf, 8)
basic_ios_wchar* __thiscall basic_ios_wchar_ctor_streambuf(basic_ios_wchar *this, basic_streambuf_wchar *strbuf)
{
    TRACE("(%p %p)\n", this, strbuf);

    basic_ios_wchar_ctor(this);
    basic_ios_wchar_init(this, strbuf, FALSE);
    return this;
}

/* ??0?$basic_ios@GU?$char_traits@G@std@@@std@@QAE@PAV?$basic_streambuf@GU?$char_traits@G@std@@@1@@Z */
/* ??0?$basic_ios@GU?$char_traits@G@std@@@std@@QEAA@PEAV?$basic_streambuf@GU?$char_traits@G@std@@@1@@Z */
DEFINE_THISCALL_WRAPPER(basic_ios_short_ctor_streambuf, 8)
basic_ios_wchar* __thiscall basic_ios_short_ctor_streambuf(basic_ios_wchar *this, basic_streambuf_wchar *strbuf)
{
    basic_ios_wchar_ctor_streambuf(this, strbuf);
    this->base.vtable = &MSVCP_basic_ios_short_vtable;
    return this;
}

/* ??1?$basic_ios@_WU?$char_traits@_W@std@@@std@@UAE@XZ */
/* ??1?$basic_ios@_WU?$char_traits@_W@std@@@std@@UEAA@XZ */
/* ??1?$basic_ios@GU?$char_traits@G@std@@@std@@UAE@XZ */
/* ??1?$basic_ios@GU?$char_traits@G@std@@@std@@UEAA@XZ */
DEFINE_THISCALL_WRAPPER(basic_ios_wchar_dtor, 4)
void __thiscall basic_ios_wchar_dtor(basic_ios_wchar *this)
{
    TRACE("(%p)\n", this);
    ios_base_dtor(&this->base);
}

DEFINE_THISCALL_WRAPPER(MSVCP_basic_ios_wchar_vector_dtor, 8)
basic_ios_wchar* __thiscall MSVCP_basic_ios_wchar_vector_dtor(basic_ios_wchar *this, unsigned int flags)
{
    TRACE("(%p %x)\n", this, flags);
    if(flags & 2) {
        /* we have an array, with the number of elements stored before the first object */
        int i, *ptr = (int *)this-1;

        for(i=*ptr-1; i>=0; i--)
            basic_ios_wchar_dtor(this+i);
        MSVCRT_operator_delete(ptr);
    } else {
        basic_ios_wchar_dtor(this);
        if(flags & 1)
            MSVCRT_operator_delete(this);
    }

    return this;
}

DEFINE_THISCALL_WRAPPER(MSVCP_basic_ios_short_vector_dtor, 8)
basic_ios_wchar* __thiscall MSVCP_basic_ios_short_vector_dtor(basic_ios_wchar *this, unsigned int flags)
{
    return MSVCP_basic_ios_wchar_vector_dtor(this, flags);
}

/* ?clear@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QAEXH_N@Z */
/* ?clear@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QEAAXH_N@Z */
/* ?clear@?$basic_ios@GU?$char_traits@G@std@@@std@@QAEXH_N@Z */
/* ?clear@?$basic_ios@GU?$char_traits@G@std@@@std@@QEAAXH_N@Z */
DEFINE_THISCALL_WRAPPER(basic_ios_wchar_clear_reraise, 12)
void __thiscall basic_ios_wchar_clear_reraise(basic_ios_wchar *this, IOSB_iostate state, MSVCP_bool reraise)
{
    TRACE("(%p %x %x)\n", this, state, reraise);
    ios_base_clear_reraise(&this->base, state | (this->strbuf ? IOSTATE_goodbit : IOSTATE_badbit), reraise);
}

/* ?clear@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QAEXI@Z */
/* ?clear@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QEAAXI@Z */
/* ?clear@?$basic_ios@GU?$char_traits@G@std@@@std@@QAEXI@Z */
/* ?clear@?$basic_ios@GU?$char_traits@G@std@@@std@@QEAAXI@Z */
DEFINE_THISCALL_WRAPPER(basic_ios_wchar_clear, 8)
void __thiscall basic_ios_wchar_clear(basic_ios_wchar *this, unsigned int state)
{
    basic_ios_wchar_clear_reraise(this, (IOSB_iostate)state, FALSE);
}

/* ?copyfmt@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QAEAAV12@ABV12@@Z */
/* ?copyfmt@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QEAAAEAV12@AEBV12@@Z */
/* ?copyfmt@?$basic_ios@GU?$char_traits@G@std@@@std@@QAEAAV12@ABV12@@Z */
/* ?copyfmt@?$basic_ios@GU?$char_traits@G@std@@@std@@QEAAAEAV12@AEBV12@@Z */
DEFINE_THISCALL_WRAPPER(basic_ios_wchar_copyfmt, 8)
basic_ios_wchar* __thiscall basic_ios_wchar_copyfmt(basic_ios_wchar *this, basic_ios_wchar *copy)
{
    TRACE("(%p %p)\n", this, copy);
    if(this == copy)
        return this;

    this->stream = copy->stream;
    this->fillch = copy->fillch;
    ios_base_copyfmt(&this->base, &copy->base);
    return this;
}

/* ?fill@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QAE_W_W@Z */
/* ?fill@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QEAA_W_W@Z */
/* ?fill@?$basic_ios@GU?$char_traits@G@std@@@std@@QAEGG@Z */
/* ?fill@?$basic_ios@GU?$char_traits@G@std@@@std@@QEAAGG@Z */
DEFINE_THISCALL_WRAPPER(basic_ios_wchar_fill_set, 8)
wchar_t __thiscall basic_ios_wchar_fill_set(basic_ios_wchar *this, wchar_t fill)
{
    wchar_t ret = this->fillch;

    TRACE("(%p %c)\n", this, fill);

    this->fillch = fill;
    return ret;
}

/* ?fill@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QBE_WXZ */
/* ?fill@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QEBA_WXZ */
/* ?fill@?$basic_ios@GU?$char_traits@G@std@@@std@@QBEGXZ */
/* ?fill@?$basic_ios@GU?$char_traits@G@std@@@std@@QEBAGXZ */
DEFINE_THISCALL_WRAPPER(basic_ios_wchar_fill_get, 4)
wchar_t __thiscall basic_ios_wchar_fill_get(basic_ios_wchar *this)
{
    TRACE("(%p)\n", this);
    return this->fillch;
}

/* ?imbue@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QAE?AVlocale@2@ABV32@@Z */
/* ?imbue@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QEAA?AVlocale@2@AEBV32@@Z */
/* ?imbue@?$basic_ios@GU?$char_traits@G@std@@@std@@QAE?AVlocale@2@ABV32@@Z */
/* ?imbue@?$basic_ios@GU?$char_traits@G@std@@@std@@QEAA?AVlocale@2@AEBV32@@Z */
DEFINE_THISCALL_WRAPPER(basic_ios_wchar_imbue, 12)
locale *__thiscall basic_ios_wchar_imbue(basic_ios_wchar *this, locale *ret, const locale *loc)
{
    TRACE("(%p %p %p)\n", this, ret, loc);

    if(this->strbuf)
        return basic_streambuf_wchar_pubimbue(this->strbuf, ret, loc);

    locale_copy_ctor(ret, loc);
    return ret;
}

/* ?narrow@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QBED_WD@Z */
/* ?narrow@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QEBAD_WD@Z */
/* ?narrow@?$basic_ios@GU?$char_traits@G@std@@@std@@QBEDGD@Z */
/* ?narrow@?$basic_ios@GU?$char_traits@G@std@@@std@@QEBADGD@Z */
DEFINE_THISCALL_WRAPPER(basic_ios_wchar_narrow, 12)
char __thiscall basic_ios_wchar_narrow(basic_ios_wchar *this, wchar_t ch, char def)
{
    FIXME("(%p %c %c) stub\n", this, ch, def);
    return def;
}

/* ?rdbuf@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QAEPAV?$basic_streambuf@_WU?$char_traits@_W@std@@@2@PAV32@@Z */
/* ?rdbuf@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QEAAPEAV?$basic_streambuf@_WU?$char_traits@_W@std@@@2@PEAV32@@Z */
/* ?rdbuf@?$basic_ios@GU?$char_traits@G@std@@@std@@QAEPAV?$basic_streambuf@GU?$char_traits@G@std@@@2@PAV32@@Z */
/* ?rdbuf@?$basic_ios@GU?$char_traits@G@std@@@std@@QEAAPEAV?$basic_streambuf@GU?$char_traits@G@std@@@2@PEAV32@@Z */
DEFINE_THISCALL_WRAPPER(basic_ios_wchar_rdbuf_set, 8)
basic_streambuf_wchar* __thiscall basic_ios_wchar_rdbuf_set(basic_ios_wchar *this, basic_streambuf_wchar *streambuf)
{
    basic_streambuf_wchar *ret = this->strbuf;

    TRACE("(%p %p)\n", this, streambuf);

    this->strbuf = streambuf;
    basic_ios_wchar_clear(this, IOSTATE_goodbit);
    return ret;
}

/* ?rdbuf@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QBEPAV?$basic_streambuf@_WU?$char_traits@_W@std@@@2@XZ */
/* ?rdbuf@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QEBAPEAV?$basic_streambuf@_WU?$char_traits@_W@std@@@2@XZ */
/* ?rdbuf@?$basic_ios@GU?$char_traits@G@std@@@std@@QBEPAV?$basic_streambuf@GU?$char_traits@G@std@@@2@XZ */
/* ?rdbuf@?$basic_ios@GU?$char_traits@G@std@@@std@@QEBAPEAV?$basic_streambuf@GU?$char_traits@G@std@@@2@XZ */
DEFINE_THISCALL_WRAPPER(basic_ios_wchar_rdbuf_get, 4)
basic_streambuf_wchar* __thiscall basic_ios_wchar_rdbuf_get(const basic_ios_wchar *this)
{
    TRACE("(%p)\n", this);
    return this->strbuf;
}

/* ?setstate@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QAEXH_N@Z */
/* ?setstate@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QEAAXH_N@Z */
/* ?setstate@?$basic_ios@GU?$char_traits@G@std@@@std@@QAEXH_N@Z */
/* ?setstate@?$basic_ios@GU?$char_traits@G@std@@@std@@QEAAXH_N@Z */
DEFINE_THISCALL_WRAPPER(basic_ios_wchar_setstate_reraise, 12)
void __thiscall basic_ios_wchar_setstate_reraise(basic_ios_wchar *this, IOSB_iostate state, MSVCP_bool reraise)
{
    TRACE("(%p %x %x)\n", this, state, reraise);

    if(state != IOSTATE_goodbit)
        basic_ios_wchar_clear_reraise(this, this->base.state | state, reraise);
}

/* ?setstate@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QAEXI@Z */
/* ?setstate@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QEAAXI@Z */
/* ?setstate@?$basic_ios@GU?$char_traits@G@std@@@std@@QAEXI@Z */
/* ?setstate@?$basic_ios@GU?$char_traits@G@std@@@std@@QEAAXI@Z */
DEFINE_THISCALL_WRAPPER(basic_ios_wchar_setstate, 8)
void __thiscall basic_ios_wchar_setstate(basic_ios_wchar *this, IOSB_iostate state)
{
    basic_ios_wchar_setstate_reraise(this, state, FALSE);
}

/* ?tie@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QAEPAV?$basic_ostream@_WU?$char_traits@_W@std@@@2@PAV32@@Z */
/* ?tie@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QEAAPEAV?$basic_ostream@_WU?$char_traits@_W@std@@@2@PEAV32@@Z */
/* ?tie@?$basic_ios@GU?$char_traits@G@std@@@std@@QAEPAV?$basic_ostream@GU?$char_traits@G@std@@@2@PAV32@@Z */
/* ?tie@?$basic_ios@GU?$char_traits@G@std@@@std@@QEAAPEAV?$basic_ostream@GU?$char_traits@G@std@@@2@PEAV32@@Z */
DEFINE_THISCALL_WRAPPER(basic_ios_wchar_tie_set, 8)
basic_ostream_wchar* __thiscall basic_ios_wchar_tie_set(basic_ios_wchar *this, basic_ostream_wchar *ostream)
{
    basic_ostream_wchar *ret = this->stream;

    TRACE("(%p %p)\n", this, ostream);

    this->stream = ostream;
    return ret;
}

/* ?tie@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QBEPAV?$basic_ostream@_WU?$char_traits@_W@std@@@2@XZ */
/* ?tie@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QEBAPEAV?$basic_ostream@_WU?$char_traits@_W@std@@@2@XZ */
/* ?tie@?$basic_ios@GU?$char_traits@G@std@@@std@@QBEPAV?$basic_ostream@GU?$char_traits@G@std@@@2@XZ */
/* ?tie@?$basic_ios@GU?$char_traits@G@std@@@std@@QEBAPEAV?$basic_ostream@GU?$char_traits@G@std@@@2@XZ */
DEFINE_THISCALL_WRAPPER(basic_ios_wchar_tie_get, 4)
basic_ostream_wchar* __thiscall basic_ios_wchar_tie_get(const basic_ios_wchar *this)
{
    TRACE("(%p)\n", this);
    return this->stream;
}

/* ?widen@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QBE_WD@Z */
/* ?widen@?$basic_ios@_WU?$char_traits@_W@std@@@std@@QEBA_WD@Z */
/* ?widen@?$basic_ios@GU?$char_traits@G@std@@@std@@QBEGD@Z */
/* ?widen@?$basic_ios@GU?$char_traits@G@std@@@std@@QEBAGD@Z */
DEFINE_THISCALL_WRAPPER(basic_ios_wchar_widen, 8)
wchar_t __thiscall basic_ios_wchar_widen(basic_ios_wchar *this, char ch)
{
    FIXME("(%p %c)\n", this, ch);
    return 0;
}

/* Caution: basic_ostream uses virtual inheritance.
 * All constructors have additional parameter that says if base class should be initialized.
 * Base class needs to be accessed using vbtable.
 */
static inline basic_ios_char* basic_ostream_char_get_basic_ios(basic_ostream_char *this)
{
    return (basic_ios_char*)((char*)this+this->vbtable[1]);
}

/* ??0?$basic_ostream@DU?$char_traits@D@std@@@std@@QAE@PAV?$basic_streambuf@DU?$char_traits@D@std@@@1@_N@Z */
/* ??0?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAA@PEAV?$basic_streambuf@DU?$char_traits@D@std@@@1@_N@Z */
DEFINE_THISCALL_WRAPPER(basic_ostream_char_ctor, 16)
basic_ostream_char* __thiscall basic_ostream_char_ctor(basic_ostream_char *this,
        basic_streambuf_char *strbuf, MSVCP_bool isstd, MSVCP_bool virt_init)
{
    basic_ios_char *base;

    TRACE("(%p %p %d %d)\n", this, strbuf, isstd, virt_init);

    if(virt_init) {
        this->vbtable = basic_ostream_char_vbtable;
        base = basic_ostream_char_get_basic_ios(this);
        basic_ios_char_ctor(base);
    }else {
        base = basic_ostream_char_get_basic_ios(this);
    }

    base->base.vtable = &MSVCP_basic_ostream_char_vtable;
    basic_ios_char_init(base, strbuf, isstd);
    return this;
}

/* ??0?$basic_ostream@DU?$char_traits@D@std@@@std@@QAE@W4_Uninitialized@1@_N@Z */
/* ??0?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAA@W4_Uninitialized@1@_N@Z */
DEFINE_THISCALL_WRAPPER(basic_ostream_char_uninitialized, 16)
basic_ostream_char* __thiscall basic_ostream_char_uninitialized(basic_ostream_char *this,
        int uninitialized, MSVCP_bool addstd, MSVCP_bool virt_init)
{
    basic_ios_char *base;

    TRACE("(%p %d %x)\n", this, uninitialized, addstd);

    if(virt_init) {
        this->vbtable = basic_ostream_char_vbtable;
        base = basic_ostream_char_get_basic_ios(this);
        basic_ios_char_ctor(base);
    }else {
        base = basic_ostream_char_get_basic_ios(this);
    }

    base->base.vtable = &MSVCP_basic_ostream_char_vtable;
    if(addstd)
        ios_base_Addstd(&base->base);
    return this;
}

/* ??1?$basic_ostream@DU?$char_traits@D@std@@@std@@UAE@XZ */
/* ??1?$basic_ostream@DU?$char_traits@D@std@@@std@@UEAA@XZ */
DEFINE_THISCALL_WRAPPER(basic_ostream_char_dtor, 4)
void __thiscall basic_ostream_char_dtor(basic_ostream_char *this)
{
    /* don't destroy virtual base here */
    TRACE("(%p)\n", this);
}

/* ??_D?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEXXZ */
/* ??_D?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAXXZ */
DEFINE_THISCALL_WRAPPER(basic_ostream_char_vbase_dtor, 4)
void __thiscall basic_ostream_char_vbase_dtor(basic_ostream_char *this)
{
    TRACE("(%p)\n", this);
    basic_ostream_char_dtor(this);
    basic_ios_char_dtor(basic_ostream_char_get_basic_ios(this));
}

DEFINE_THISCALL_WRAPPER(MSVCP_basic_ostream_char_vector_dtor, 8)
basic_ostream_char* __thiscall MSVCP_basic_ostream_char_vector_dtor(basic_ios_char *base, unsigned int flags)
{
    basic_ostream_char *this = (basic_ostream_char *)((char*)base - basic_ostream_char_vbtable[1] + basic_ostream_char_vbtable[0]);

    TRACE("(%p %x)\n", this, flags);

    if(flags & 2) {
        /* we have an array, with the number of elements stored before the first object */
        int i, *ptr = (int *)this-1;

        for(i=*ptr-1; i>=0; i--)
            basic_ostream_char_vbase_dtor(this+i);
        MSVCRT_operator_delete(ptr);
    } else {
        basic_ostream_char_vbase_dtor(this);
        if(flags & 1)
            MSVCRT_operator_delete(this);
    }

    return this;
}

/* ?flush@?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV12@XZ */
/* ?flush@?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV12@XZ */
DEFINE_THISCALL_WRAPPER(basic_ostream_char_flush, 4)
basic_ostream_char* __thiscall basic_ostream_char_flush(basic_ostream_char *this)
{
    /* this function is not matching C++ specification */
    basic_ios_char *base = basic_ostream_char_get_basic_ios(this);

    TRACE("(%p)\n", this);

    if(basic_ios_char_rdbuf_get(base) && ios_base_good(&base->base)
            && basic_streambuf_char_pubsync(basic_ios_char_rdbuf_get(base))==-1)
        basic_ios_char_setstate(base, IOSTATE_badbit);
    return this;
}

/* ?_Osfx@?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEXXZ */
/* ?_Osfx@?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAXXZ */
DEFINE_THISCALL_WRAPPER(basic_ostream_char__Osfx, 4)
void __thiscall basic_ostream_char__Osfx(basic_ostream_char *this)
{
    basic_ios_char *base = basic_ostream_char_get_basic_ios(this);

    TRACE("(%p)\n", this);

    if(base->base.fmtfl & FMTFLAG_unitbuf)
        basic_ostream_char_flush(this);
}

/* ?osfx@?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEXXZ */
/* ?osfx@?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAXXZ */
DEFINE_THISCALL_WRAPPER(basic_ostream_char_osfx, 4)
void __thiscall basic_ostream_char_osfx(basic_ostream_char *this)
{
    TRACE("(%p)\n", this);
    basic_ostream_char__Osfx(this);
}

static BOOL basic_ostream_char_sentry_create(basic_ostream_char *ostr)
{
    basic_ios_char *base = basic_ostream_char_get_basic_ios(ostr);

    if(basic_ios_char_rdbuf_get(base))
        basic_streambuf_char__Lock(base->strbuf);

    if(ios_base_good(&base->base) && base->stream)
        basic_ostream_char_flush(base->stream);

    return ios_base_good(&base->base);
}

static void basic_ostream_char_sentry_destroy(basic_ostream_char *ostr)
{
    basic_ios_char *base = basic_ostream_char_get_basic_ios(ostr);

    if(ios_base_good(&base->base) && !__uncaught_exception())
        basic_ostream_char_osfx(ostr);

    if(basic_ios_char_rdbuf_get(base))
        basic_streambuf_char__Unlock(base->strbuf);
}

/* ?opfx@?$basic_ostream@DU?$char_traits@D@std@@@std@@QAE_NXZ */
/* ?opfx@?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAA_NXZ */
DEFINE_THISCALL_WRAPPER(basic_ostream_char_opfx, 4)
MSVCP_bool __thiscall basic_ostream_char_opfx(basic_ostream_char *this)
{
    basic_ios_char *base = basic_ostream_char_get_basic_ios(this);

    TRACE("(%p)\n", this);

    if(ios_base_good(&base->base) && base->stream)
        basic_ostream_char_flush(base->stream);
    return ios_base_good(&base->base);
}

/* ?put@?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV12@D@Z */
/* ?put@?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV12@D@Z */
DEFINE_THISCALL_WRAPPER(basic_ostream_char_put, 8)
basic_ostream_char* __thiscall basic_ostream_char_put(basic_ostream_char *this, char ch)
{
    basic_ios_char *base = basic_ostream_char_get_basic_ios(this);

    TRACE("(%p %c)\n", this, ch);

    if(!basic_ostream_char_sentry_create(this)
            || basic_streambuf_char_sputc(base->strbuf, ch)==EOF) {
        basic_ostream_char_sentry_destroy(this);
        basic_ios_char_setstate(base, IOSTATE_badbit);
        return this;
    }

    basic_ostream_char_sentry_destroy(this);
    return this;
}

/* ?seekp@?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV12@JH@Z */
/* ?seekp@?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV12@_JH@Z */
DEFINE_THISCALL_WRAPPER(basic_ostream_char_seekp, 12)
basic_ostream_char* __thiscall basic_ostream_char_seekp(basic_ostream_char *this, streamoff off, int way)
{
    basic_ios_char *base = basic_ostream_char_get_basic_ios(this);

    TRACE("(%p %ld %d)\n", this, off, way);

    if(!ios_base_fail(&base->base)) {
        fpos_int seek;

        basic_streambuf_char_pubseekoff(basic_ios_char_rdbuf_get(base),
                &seek, off, way, OPENMODE_out);
        if(seek.off==0 && seek.pos==-1 && seek.state==0)
            basic_ios_char_setstate(base, IOSTATE_failbit);
    }
    return this;
}

/* ?seekp@?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV12@V?$fpos@H@2@@Z */
/* ?seekp@?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV12@V?$fpos@H@2@@Z */
DEFINE_THISCALL_WRAPPER(basic_ostream_char_seekp_fpos, 28)
basic_ostream_char* __thiscall basic_ostream_char_seekp_fpos(basic_ostream_char *this, fpos_int pos)
{
    basic_ios_char *base = basic_ostream_char_get_basic_ios(this);

    TRACE("(%p %s)\n", this, debugstr_fpos_int(&pos));

    if(!ios_base_fail(&base->base)) {
        fpos_int seek;

        basic_streambuf_char_pubseekpos(basic_ios_char_rdbuf_get(base),
                &seek, pos, OPENMODE_out);
        if(seek.off==0 && seek.pos==-1 && seek.state==0)
            basic_ios_char_setstate(base, IOSTATE_failbit);
    }
    return this;
}

/* ?tellp@?$basic_ostream@DU?$char_traits@D@std@@@std@@QAE?AV?$fpos@H@2@XZ */
/* ?tellp@?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAA?AV?$fpos@H@2@XZ */
DEFINE_THISCALL_WRAPPER(basic_ostream_char_tellp, 8)
fpos_int* __thiscall basic_ostream_char_tellp(basic_ostream_char *this, fpos_int *ret)
{
    basic_ios_char *base = basic_ostream_char_get_basic_ios(this);

    TRACE("(%p)\n", this);

    if(!ios_base_fail(&base->base)) {
        basic_streambuf_char_pubseekoff(basic_ios_char_rdbuf_get(base),
                ret, 0, SEEKDIR_cur, OPENMODE_out);
    }else {
        ret->off = 0;
        ret->pos = -1;
        ret->state = 0;
    }
    return ret;
}

/* ?write@?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV12@PBDH@Z */
/* ?write@?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV12@PEBD_J@Z */
DEFINE_THISCALL_WRAPPER(basic_ostream_char_write, 12)
basic_ostream_char* __thiscall basic_ostream_char_write(basic_ostream_char *this, const char *str, streamsize count)
{
    basic_ios_char *base = basic_ostream_char_get_basic_ios(this);

    TRACE("(%p %s %ld)\n", this, debugstr_a(str), count);

    if(!basic_ostream_char_sentry_create(this)
            || basic_streambuf_char_sputn(base->strbuf, str, count)!=count) {
        basic_ostream_char_sentry_destroy(this);
        basic_ios_char_setstate(base, IOSTATE_badbit);
        return this;
    }

    basic_ostream_char_sentry_destroy(this);
    return this;
}

/* ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV01@F@Z */
/* ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@F@Z */
DEFINE_THISCALL_WRAPPER(basic_ostream_char_print_short, 8)
basic_ostream_char* __thiscall basic_ostream_char_print_short(basic_ostream_char *this, short val)
{
    FIXME("(%p %d) stub\n", this, val);
    return NULL;
}

/* ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV01@G@Z */
/* ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@G@Z */
DEFINE_THISCALL_WRAPPER(basic_ostream_char_print_ushort, 8)
basic_ostream_char* __thiscall basic_ostream_char_print_ushort(basic_ostream_char *this, unsigned short val)
{
    FIXME("(%p %d) stub\n", this, val);
    return NULL;
}

/* ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV01@H@Z */
/* ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@H@Z */
DEFINE_THISCALL_WRAPPER(basic_ostream_char_print_int, 8)
basic_ostream_char* __thiscall basic_ostream_char_print_int(basic_ostream_char *this, int val)
{
    FIXME("(%p %d) stub\n", this, val);
    return NULL;
}

/* ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV01@I@Z */
/* ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@I@Z */
DEFINE_THISCALL_WRAPPER(basic_ostream_char_print_uint, 8)
basic_ostream_char* __thiscall basic_ostream_char_print_uint(basic_ostream_char *this, unsigned int val)
{
    FIXME("(%p %d) stub\n", this, val);
    return NULL;
}

/* ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV01@J@Z */
/* ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@J@Z */
DEFINE_THISCALL_WRAPPER(basic_ostream_char_print_long, 8)
basic_ostream_char* __thiscall basic_ostream_char_print_long(basic_ostream_char *this, LONG val)
{
    FIXME("(%p %d) stub\n", this, val);
    return NULL;
}

/* ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV01@K@Z */
/* ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@K@Z */
DEFINE_THISCALL_WRAPPER(basic_ostream_char_print_ulong, 8)
basic_ostream_char* __thiscall basic_ostream_char_print_ulong(basic_ostream_char *this, ULONG val)
{
    FIXME("(%p %d) stub\n", this, val);
    return NULL;
}

/* ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV01@M@Z */
/* ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@M@Z */
DEFINE_THISCALL_WRAPPER(basic_ostream_char_print_float, 8)
basic_ostream_char* __thiscall basic_ostream_char_print_float(basic_ostream_char *this, float val)
{
    FIXME("(%p %f) stub\n", this, val);
    return NULL;
}

/* ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV01@N@Z */
/* ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@N@Z */
DEFINE_THISCALL_WRAPPER(basic_ostream_char_print_double, 12)
basic_ostream_char* __thiscall basic_ostream_char_print_double(basic_ostream_char *this, double val)
{
    FIXME("(%p %lf) stub\n", this, val);
    return NULL;
}

/* ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV01@PAV?$basic_streambuf@DU?$char_traits@D@std@@@1@@Z */
/* ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@PEAV?$basic_streambuf@DU?$char_traits@D@std@@@1@@Z */
DEFINE_THISCALL_WRAPPER(basic_ostream_char_print_streambuf, 8)
basic_ostream_char* __thiscall basic_ostream_char_print_streambuf(basic_ostream_char *this, basic_streambuf_char *val)
{
    FIXME("(%p %p) stub\n", this, val);
    return NULL;
}

/* ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV01@PBX@Z */
/* ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@PEBX@Z */
DEFINE_THISCALL_WRAPPER(basic_ostream_char_print_ptr, 8)
basic_ostream_char* __thiscall basic_ostream_char_print_ptr(basic_ostream_char *this, const void *val)
{
    FIXME("(%p %p) stub\n", this, val);
    return NULL;
}

/* ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV01@_J@Z */
/* ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@_J@Z */
DEFINE_THISCALL_WRAPPER(basic_ostream_char_print_int64, 12)
basic_ostream_char* __thiscall basic_ostream_char_print_int64(basic_ostream_char *this, __int64 val)
{
    FIXME("(%p) stub\n", this);
    return NULL;
}

/* ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV01@_K@Z */
/* ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@_K@Z */
DEFINE_THISCALL_WRAPPER(basic_ostream_char_print_uint64, 12)
basic_ostream_char* __thiscall basic_ostream_char_print_uint64(basic_ostream_char *this, unsigned __int64 val)
{
    FIXME("(%p) stub\n", this);
    return NULL;
}

/* ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV01@_N@Z */
/* ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@_N@Z */
DEFINE_THISCALL_WRAPPER(basic_ostream_char_print_bool, 8)
basic_ostream_char* __thiscall basic_ostream_char_print_bool(basic_ostream_char *this, MSVCP_bool val)
{
    FIXME("(%p %x) stub\n", this, val);
    return NULL;
}

/* Caution: basic_istream uses virtual inheritance. */
static inline basic_ios_char* basic_istream_char_get_basic_ios(basic_istream_char *this)
{
    return (basic_ios_char*)((char*)this+this->vbtable[1]);
}

/* ??0?$basic_istream@DU?$char_traits@D@std@@@std@@QAE@PAV?$basic_streambuf@DU?$char_traits@D@std@@@1@_N1@Z */
/* ??0?$basic_istream@DU?$char_traits@D@std@@@std@@QEAA@PEAV?$basic_streambuf@DU?$char_traits@D@std@@@1@_N1@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_ctor_init, 20)
basic_istream_char* __thiscall basic_istream_char_ctor_init(basic_istream_char *this, basic_streambuf_char *strbuf, MSVCP_bool isstd, MSVCP_bool noinit, MSVCP_bool virt_init)
{
    basic_ios_char *base;

    TRACE("(%p %p %d %d %d)\n", this, strbuf, isstd, noinit, virt_init);

    if(virt_init) {
        this->vbtable = basic_istream_char_vbtable;
        base = basic_istream_char_get_basic_ios(this);
        basic_ios_char_ctor(base);
    }else {
        base = basic_istream_char_get_basic_ios(this);
    }

    base->base.vtable = &MSVCP_basic_istream_char_vtable;
    this->count = 0;
    if(!noinit)
        basic_ios_char_init(base, strbuf, isstd);
    return this;
}

/* ??0?$basic_istream@DU?$char_traits@D@std@@@std@@QAE@PAV?$basic_streambuf@DU?$char_traits@D@std@@@1@_N@Z */
/* ??0?$basic_istream@DU?$char_traits@D@std@@@std@@QEAA@PEAV?$basic_streambuf@DU?$char_traits@D@std@@@1@_N@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_ctor, 16)
basic_istream_char* __thiscall basic_istream_char_ctor(basic_istream_char *this, basic_streambuf_char *strbuf, MSVCP_bool isstd, MSVCP_bool virt_init)
{
    return basic_istream_char_ctor_init(this, strbuf, isstd, FALSE, virt_init);
}

/* ??0?$basic_istream@DU?$char_traits@D@std@@@std@@QAE@W4_Uninitialized@1@@Z */
/* ??0?$basic_istream@DU?$char_traits@D@std@@@std@@QEAA@W4_Uninitialized@1@@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_ctor_uninitialized, 12)
basic_istream_char* __thiscall basic_istream_char_ctor_uninitialized(basic_istream_char *this, int uninitialized, MSVCP_bool virt_init)
{
    basic_ios_char *base;

    TRACE("(%p %d %d)\n", this, uninitialized, virt_init);

    if(virt_init) {
        this->vbtable = basic_istream_char_vbtable;
        base = basic_istream_char_get_basic_ios(this);
        basic_ios_char_ctor(base);
    }else {
        base = basic_istream_char_get_basic_ios(this);
    }

    base->base.vtable = &MSVCP_basic_istream_char_vtable;
    ios_base_Addstd(&base->base);
    return this;
}

/* ??1?$basic_istream@DU?$char_traits@D@std@@@std@@UAE@XZ */
/* ??1?$basic_istream@DU?$char_traits@D@std@@@std@@UEAA@XZ */
DEFINE_THISCALL_WRAPPER(basic_istream_char_dtor, 4)
void __thiscall basic_istream_char_dtor(basic_istream_char *this)
{
    /* don't destroy virtual base here */
    TRACE("(%p)\n", this);
}

/* ??_D?$basic_istream@DU?$char_traits@D@std@@@std@@QAEXXZ */
/* ??_D?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAXXZ */
DEFINE_THISCALL_WRAPPER(basic_istream_char_vbase_dtor, 4)
void __thiscall basic_istream_char_vbase_dtor(basic_istream_char *this)
{
    TRACE("(%p)\n", this);
    basic_istream_char_dtor(this);
    basic_ios_char_dtor(basic_istream_char_get_basic_ios(this));
}

DEFINE_THISCALL_WRAPPER(MSVCP_basic_istream_char_vector_dtor, 8)
basic_istream_char* __thiscall MSVCP_basic_istream_char_vector_dtor(basic_ios_char *base, unsigned int flags)
{
    basic_istream_char *this = (basic_istream_char *)((char*)base - basic_istream_char_vbtable[1] + basic_istream_char_vbtable[0]);

    TRACE("(%p %x)\n", this, flags);

    if(flags & 2) {
        /* we have an array, with the number of elements stored before the first object */
        int i, *ptr = (int *)this-1;

        for(i=*ptr-1; i>=0; i--)
            basic_istream_char_vbase_dtor(this+i);
        MSVCRT_operator_delete(ptr);
    } else {
        basic_istream_char_vbase_dtor(this);
        if(flags & 1)
            MSVCRT_operator_delete(this);
    }

    return this;
}

/* ?_Ipfx@?$basic_istream@DU?$char_traits@D@std@@@std@@QAE_N_N@Z */
/* ?_Ipfx@?$basic_istream@DU?$char_traits@D@std@@@std@@QEAA_N_N@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char__Ipfx, 8)
MSVCP_bool __thiscall basic_istream_char__Ipfx(basic_istream_char *this, MSVCP_bool noskip)
{
    basic_ios_char *base = basic_istream_char_get_basic_ios(this);

    TRACE("(%p %d)\n", this, noskip);

    if(!ios_base_good(&base->base)) {
        basic_ios_char_setstate(base, IOSTATE_failbit);
        return FALSE;
    }

    if(basic_ios_char_tie_get(base))
        basic_ostream_char_flush(basic_ios_char_tie_get(base));

    if(!noskip && (ios_base_flags_get(&base->base) & FMTFLAG_skipws)) {
        basic_streambuf_char *strbuf = basic_ios_char_rdbuf_get(base);
        int ch;

        for(ch = basic_streambuf_char_sgetc(strbuf); ;
                ch = basic_streambuf_char_snextc(strbuf)) {
            if(ch == EOF) {
                basic_ios_char_setstate(base, IOSTATE_eofbit);
                return FALSE;
            }

            /* TODO: use locale class instead of isspace */
            if(!isspace(ch))
                break;
        }
    }

    return TRUE;
}

/* ?ipfx@?$basic_istream@DU?$char_traits@D@std@@@std@@QAE_N_N@Z */
/* ?ipfx@?$basic_istream@DU?$char_traits@D@std@@@std@@QEAA_N_N@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_ipfx, 8)
MSVCP_bool __thiscall basic_istream_char_ipfx(basic_istream_char *this, MSVCP_bool noskip)
{
    return basic_istream_char__Ipfx(this, noskip);
}

/* ?isfx@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEXXZ */
/* ?isfx@?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAXXZ */
DEFINE_THISCALL_WRAPPER(basic_istream_char_isfx, 4)
void __thiscall basic_istream_char_isfx(basic_istream_char *this)
{
    TRACE("(%p)\n", this);
}

static BOOL basic_istream_char_sentry_create(basic_istream_char *istr, MSVCP_bool noskip)
{
    basic_ios_char *base = basic_istream_char_get_basic_ios(istr);

    if(basic_ios_char_rdbuf_get(base))
        basic_streambuf_char__Lock(base->strbuf);

    return basic_istream_char_ipfx(istr, noskip);
}

static void basic_istream_char_sentry_destroy(basic_istream_char *istr)
{
    basic_ios_char *base = basic_istream_char_get_basic_ios(istr);

    if(basic_ios_char_rdbuf_get(base))
        basic_streambuf_char__Unlock(base->strbuf);
}

/* ?gcount@?$basic_istream@DU?$char_traits@D@std@@@std@@QBEHXZ */
/* ?gcount@?$basic_istream@DU?$char_traits@D@std@@@std@@QEBA_JXZ */
DEFINE_THISCALL_WRAPPER(basic_istream_char_gcount, 4)
int __thiscall basic_istream_char_gcount(const basic_istream_char *this)
{
    TRACE("(%p)\n", this);
    return this->count;
}

/* ?get@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEHXZ */
/* ?get@?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAHXZ */
DEFINE_THISCALL_WRAPPER(basic_istream_char_get, 4)
int __thiscall basic_istream_char_get(basic_istream_char *this)
{
    basic_ios_char *base = basic_istream_char_get_basic_ios(this);
    int ret;

    TRACE("(%p)\n", this);

    this->count = 0;

    if(!basic_istream_char_sentry_create(this, TRUE)) {
        basic_istream_char_sentry_destroy(this);
        return EOF;
    }

    ret = basic_streambuf_char_sbumpc(basic_ios_char_rdbuf_get(base));
    basic_istream_char_sentry_destroy(this);
    if(ret == EOF)
        basic_ios_char_setstate(base, IOSTATE_eofbit|IOSTATE_failbit);
    else
        this->count++;

    return ret;
}

/* ?get@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV12@AAD@Z */
/* ?get@?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV12@AEAD@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_get_ch, 8)
basic_istream_char* __thiscall basic_istream_char_get_ch(basic_istream_char *this, char *ch)
{
    int ret;

    TRACE("(%p %p)\n", this, ch);

    ret = basic_istream_char_get(this);
    if(ret != EOF)
        *ch = (char)ret;
    return this;
}

/* ?get@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV12@PADHD@Z */
/* ?get@?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV12@PEAD_JD@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_get_str_delim, 16)
basic_istream_char* __thiscall basic_istream_char_get_str_delim(basic_istream_char *this, char *str, streamsize count, char delim)
{
    basic_ios_char *base = basic_istream_char_get_basic_ios(this);
    int ch = delim;

    TRACE("(%p %p %ld %c)\n", this, str, count, delim);

    this->count = 0;

    if(basic_istream_char_sentry_create(this, TRUE)) {
        basic_streambuf_char *strbuf = basic_ios_char_rdbuf_get(base);

        for(ch = basic_streambuf_char_sgetc(strbuf); count>1;
                ch = basic_streambuf_char_snextc(strbuf)) {
            if(ch==EOF || ch==delim)
                break;

            *str++ = ch;
            this->count++;
            count--;
        }
    }
    basic_istream_char_sentry_destroy(this);

    basic_ios_char_setstate(base, (!this->count ? IOSTATE_failbit : IOSTATE_goodbit) |
            (ch==EOF ? IOSTATE_eofbit : IOSTATE_goodbit));
    if(count > 0)
        *str = 0;
    return this;
}

/* ?get@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV12@PADH@Z */
/* ?get@?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV12@PEAD_J@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_get_str, 12)
basic_istream_char* __thiscall basic_istream_char_get_str(basic_istream_char *this, char *str, streamsize count)
{
    return basic_istream_char_get_str_delim(this, str, count, '\n');
}

/* ?get@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV12@AAV?$basic_streambuf@DU?$char_traits@D@std@@@2@D@Z */
/* ?get@?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV12@AEAV?$basic_streambuf@DU?$char_traits@D@std@@@2@D@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_get_streambuf_delim, 12)
basic_istream_char* __thiscall basic_istream_char_get_streambuf_delim(basic_istream_char *this, basic_streambuf_char *strbuf, char delim)
{
    basic_ios_char *base = basic_istream_char_get_basic_ios(this);
    int ch = delim;

    TRACE("(%p %p %c)\n", this, strbuf, delim);

    this->count = 0;

    if(basic_istream_char_sentry_create(this, TRUE)) {
        basic_streambuf_char *strbuf_read = basic_ios_char_rdbuf_get(base);

        for(ch = basic_streambuf_char_sgetc(strbuf_read); ;
                ch = basic_streambuf_char_snextc(strbuf_read)) {
            if(ch==EOF || ch==delim)
                break;

            if(basic_streambuf_char_sputc(strbuf, ch) == EOF)
                break;
            this->count++;
        }
    }
    basic_istream_char_sentry_destroy(this);

    basic_ios_char_setstate(base, (!this->count ? IOSTATE_failbit : IOSTATE_goodbit) |
            (ch==EOF ? IOSTATE_eofbit : IOSTATE_goodbit));
    return this;
}

/* ?get@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV12@AAV?$basic_streambuf@DU?$char_traits@D@std@@@2@@Z */
/* ?get@?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV12@AEAV?$basic_streambuf@DU?$char_traits@D@std@@@2@@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_get_streambuf, 8)
basic_istream_char* __thiscall basic_istream_char_get_streambuf(basic_istream_char *this, basic_streambuf_char *strbuf)
{
    return basic_istream_char_get_streambuf_delim(this, strbuf, '\n');
}

/* ?getline@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV12@PADHD@Z */
/* ?getline@?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV12@PEAD_JD@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_getline_delim, 16)
basic_istream_char* __thiscall basic_istream_char_getline_delim(basic_istream_char *this, char *str, streamsize count, char delim)
{
    basic_ios_char *base = basic_istream_char_get_basic_ios(this);
    int ch = delim;

    TRACE("(%p %p %ld %c)\n", this, str, count, delim);

    this->count = 0;

    if(basic_istream_char_sentry_create(this, TRUE) && count>0) {
        basic_streambuf_char *strbuf = basic_ios_char_rdbuf_get(base);

        while(count > 1) {
            ch = basic_streambuf_char_sbumpc(strbuf);

            if(ch==EOF || ch==delim)
                break;

            *str++ = ch;
            this->count++;
            count--;
        }

        if(ch == delim)
            this->count++;
        else if(ch != EOF) {
            ch = basic_streambuf_char_sgetc(strbuf);

            if(ch == delim) {
                basic_streambuf_char__Gninc(strbuf);
                this->count++;
            }
        }
    }
    basic_istream_char_sentry_destroy(this);

    basic_ios_char_setstate(base, (ch==EOF ? IOSTATE_eofbit : IOSTATE_goodbit) |
            (!this->count || (ch!=delim && ch!=EOF) ? IOSTATE_failbit : IOSTATE_goodbit));
    if(count > 0)
        *str = 0;
    return this;
}

/* ?getline@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV12@PADH@Z */
/* ?getline@?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV12@PEAD_J@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_getline, 12)
basic_istream_char* __thiscall basic_istream_char_getline(basic_istream_char *this, char *str, streamsize count)
{
    return basic_istream_char_getline_delim(this, str, count, '\n');
}

/* ?ignore@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV12@HH@Z */
/* ?ignore@?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV12@_JH@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_ignore, 12)
basic_istream_char* __thiscall basic_istream_char_ignore(basic_istream_char *this, streamsize count, int delim)
{
    basic_ios_char *base = basic_istream_char_get_basic_ios(this);
    int ch = delim;

    TRACE("(%p %ld %d)\n", this, count, delim);

    this->count = 0;

    if(basic_istream_char_sentry_create(this, TRUE)) {
        basic_streambuf_char *strbuf = basic_ios_char_rdbuf_get(base);

        while(count > 0) {
            ch = basic_streambuf_char_sbumpc(strbuf);

            if(ch==EOF || ch==delim)
                break;

            this->count++;
            if(count != INT_MAX)
                count--;
        }
    }
    basic_istream_char_sentry_destroy(this);

    if(ch == EOF)
        basic_ios_char_setstate(base, IOSTATE_eofbit);
    return this;
}

/* ?peek@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEHXZ */
/* ?peek@?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAHXZ */
DEFINE_THISCALL_WRAPPER(basic_istream_char_peek, 4)
int __thiscall basic_istream_char_peek(basic_istream_char *this)
{
    basic_ios_char *base = basic_istream_char_get_basic_ios(this);
    int ret = EOF;

    TRACE("(%p)\n", this);

    this->count = 0;

    if(basic_istream_char_sentry_create(this, TRUE))
        ret = basic_streambuf_char_sgetc(basic_ios_char_rdbuf_get(base));
    basic_istream_char_sentry_destroy(this);
    return ret;
}

/* ?_Read_s@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV12@PADIH@Z */
/* ?_Read_s@?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV12@PEAD_K_J@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char__Read_s, 16)
basic_istream_char* __thiscall basic_istream_char__Read_s(basic_istream_char *this, char *str, MSVCP_size_t size, streamsize count)
{
    basic_ios_char *base = basic_istream_char_get_basic_ios(this);
    IOSB_iostate state = IOSTATE_goodbit;

    TRACE("(%p %p %lu %ld)\n", this, str, size, count);

    if(basic_istream_char_sentry_create(this, TRUE)) {
        basic_streambuf_char *strbuf = basic_ios_char_rdbuf_get(base);

        this->count = basic_streambuf_char__Sgetn_s(strbuf, str, size, count);
        if(this->count != count)
            state |= IOSTATE_failbit | IOSTATE_eofbit;
    }else {
        this->count = 0;
    }
    basic_istream_char_sentry_destroy(this);

    basic_ios_char_setstate(base, state);
    return this;
}

/* ?read@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV12@PADH@Z */
/* ?read@?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV12@PEAD_J@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_read, 12)
basic_istream_char* __thiscall basic_istream_char_read(basic_istream_char *this, char *str, streamsize count)
{
    return basic_istream_char__Read_s(this, str, count, count);
}

/* ?_Readsome_s@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEHPADIH@Z */
/* ?_Readsome_s@?$basic_istream@DU?$char_traits@D@std@@@std@@QEAA_JPEAD_K_J@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char__Readsome_s, 16)
streamsize __thiscall basic_istream_char__Readsome_s(basic_istream_char *this, char *str, MSVCP_size_t size, streamsize count)
{
    basic_ios_char *base = basic_istream_char_get_basic_ios(this);
    IOSB_iostate state = IOSTATE_goodbit;

    TRACE("(%p %p %lu %ld)\n", this, str, size, count);

    this->count = 0;

    if(basic_istream_char_sentry_create(this, TRUE)) {
        streamsize avail = basic_streambuf_char_in_avail(basic_ios_char_rdbuf_get(base));
        if(avail > count)
            avail = count;

        if(avail == -1)
            state |= IOSTATE_eofbit;
        else if(avail > 0)
            basic_istream_char__Read_s(this, str, size, avail);
    }else {
        state |= IOSTATE_failbit;
    }
    basic_istream_char_sentry_destroy(this);

    basic_ios_char_setstate(base, state);
    return this->count;
}

/* ?readsome@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEHPADH@Z */
/* ?readsome@?$basic_istream@DU?$char_traits@D@std@@@std@@QEAA_JPEAD_J@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_readsome, 12)
streamsize __thiscall basic_istream_char_readsome(basic_istream_char *this, char *str, streamsize count)
{
    return basic_istream_char__Readsome_s(this, str, count, count);
}

/* ?putback@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV12@D@Z */
/* ?putback@?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV12@D@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_putback, 8)
basic_istream_char* __thiscall basic_istream_char_putback(basic_istream_char *this, char ch)
{
    basic_ios_char *base = basic_istream_char_get_basic_ios(this);
    IOSB_iostate state = IOSTATE_goodbit;

    TRACE("(%p %c)\n", this, ch);

    this->count = 0;

    if(basic_istream_char_sentry_create(this, TRUE)) {
        basic_streambuf_char *strbuf = basic_ios_char_rdbuf_get(base);

        if(!ios_base_good(&base->base))
            state |= IOSTATE_failbit;
        else if(!strbuf || basic_streambuf_char_sputbackc(strbuf, ch)==EOF)
            state |= IOSTATE_badbit;
    }
    basic_istream_char_sentry_destroy(this);

    basic_ios_char_setstate(base, state);
    return this;
}

/* ?unget@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV12@XZ */
/* ?unget@?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV12@XZ */
DEFINE_THISCALL_WRAPPER(basic_istream_char_unget, 4)
basic_istream_char* __thiscall basic_istream_char_unget(basic_istream_char *this)
{
    basic_ios_char *base = basic_istream_char_get_basic_ios(this);
    IOSB_iostate state = IOSTATE_goodbit;

    TRACE("(%p)\n", this);

    this->count = 0;

    if(basic_istream_char_sentry_create(this, TRUE)) {
        basic_streambuf_char *strbuf = basic_ios_char_rdbuf_get(base);

        if(!ios_base_good(&base->base))
            state |= IOSTATE_failbit;
        else if(!strbuf || basic_streambuf_char_sungetc(strbuf)==EOF)
            state |= IOSTATE_badbit;
    }
    basic_istream_char_sentry_destroy(this);

    basic_ios_char_setstate(base, state);
    return this;
}

/* ?sync@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEHXZ */
/* ?sync@?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAHXZ */
DEFINE_THISCALL_WRAPPER(basic_istream_char_sync, 4)
int __thiscall basic_istream_char_sync(basic_istream_char *this)
{
    basic_ios_char *base = basic_istream_char_get_basic_ios(this);
    basic_streambuf_char *strbuf = basic_ios_char_rdbuf_get(base);

    TRACE("(%p)\n", this);

    if(!strbuf)
        return -1;

    if(basic_istream_char_sentry_create(this, TRUE)) {
        if(basic_streambuf_char_pubsync(strbuf) != -1) {
            basic_istream_char_sentry_destroy(this);
            return 0;
        }
    }
    basic_istream_char_sentry_destroy(this);

    basic_ios_char_setstate(base, IOSTATE_badbit);
    return -1;
}

/* ?tellg@?$basic_istream@DU?$char_traits@D@std@@@std@@QAE?AV?$fpos@H@2@XZ */
/* ?tellg@?$basic_istream@DU?$char_traits@D@std@@@std@@QEAA?AV?$fpos@H@2@XZ */
DEFINE_THISCALL_WRAPPER(basic_istream_char_tellg, 8)
fpos_int* __thiscall basic_istream_char_tellg(basic_istream_char *this, fpos_int *ret)
{
    TRACE("(%p %p)\n", this, ret);

    if(basic_istream_char_sentry_create(this, TRUE)) {
        basic_ios_char *base = basic_istream_char_get_basic_ios(this);
        if(!ios_base_fail(&base->base)) {
            basic_streambuf_char_pubseekoff(basic_ios_char_rdbuf_get(base),
                    ret, 0, SEEKDIR_cur, OPENMODE_in);
            basic_istream_char_sentry_destroy(this);

            if(ret->off==0 && ret->pos==-1 && ret->state==0)
                basic_ios_char_setstate(base, IOSTATE_failbit);
            return ret;
        }
    }
    basic_istream_char_sentry_destroy(this);

    ret->off = 0;
    ret->pos = -1;
    ret->state = 0;
    return ret;
}

/* ?seekg@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV12@JH@Z */
/* ?seekg@?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV12@_JH@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_seekg, 12)
basic_istream_char* __thiscall basic_istream_char_seekg(basic_istream_char *this, streamoff off, int dir)
{
    basic_ios_char *base = basic_istream_char_get_basic_ios(this);

    TRACE("(%p %ld %d)\n", this, off, dir);

    if(basic_istream_char_sentry_create(this, TRUE)) {
        if(!ios_base_fail(&base->base)) {
            basic_streambuf_char *strbuf = basic_ios_char_rdbuf_get(base);
            fpos_int ret;

            basic_streambuf_char_pubseekoff(strbuf, &ret, off, dir, OPENMODE_in);
            basic_istream_char_sentry_destroy(this);

            if(ret.off==0 && ret.pos==-1 && ret.state==0)
                basic_ios_char_setstate(base, IOSTATE_failbit);
            else
                basic_ios_char_clear(base, IOSTATE_goodbit);
            return this;
        }
    }
    basic_istream_char_sentry_destroy(this);

    basic_ios_char_setstate(base, IOSTATE_failbit);
    return this;
}

/* ?seekg@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV12@V?$fpos@H@2@@Z */
/* ?seekg@?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV12@V?$fpos@H@2@@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_seekg_fpos, 28)
basic_istream_char* __thiscall basic_istream_char_seekg_fpos(basic_istream_char *this, fpos_int pos)
{
    basic_ios_char *base = basic_istream_char_get_basic_ios(this);

    TRACE("(%p %s)\n", this, debugstr_fpos_int(&pos));

    if(basic_istream_char_sentry_create(this, TRUE)) {
        if(!ios_base_fail(&base->base)) {
            basic_streambuf_char *strbuf = basic_ios_char_rdbuf_get(base);
            fpos_int ret;

            basic_streambuf_char_pubseekpos(strbuf, &ret, pos, OPENMODE_in);
            basic_istream_char_sentry_destroy(this);

            if(ret.off==0 && ret.pos==-1 && ret.state==0)
                basic_ios_char_setstate(base, IOSTATE_failbit);
            else
                basic_ios_char_clear(base, IOSTATE_goodbit);
            return this;
        }
    }
    basic_istream_char_sentry_destroy(this);

    basic_ios_char_setstate(base, IOSTATE_failbit);
    return this;
}

/* ??5?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV01@AAF@Z */
/* ??5?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@AEAF@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_read_short, 8)
basic_istream_char* __thiscall basic_istream_char_read_short(basic_istream_char *this, short *arg1)
{
    FIXME("(%p %p) stub\n", this, arg1);
    return NULL;
}

/* ??5?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV01@AAG@Z */
/* ??5?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@AEAG@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_read_ushort, 8)
basic_istream_char* __thiscall basic_istream_char_read_ushort(basic_istream_char *this, unsigned short *arg1)
{
    FIXME("(%p %p) stub\n", this, arg1);
    return NULL;
}

/* ??5?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV01@AAH@Z */
/* ??5?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@AEAH@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_read_int, 8)
basic_istream_char* __thiscall basic_istream_char_read_int(basic_istream_char *this, int *arg1)
{
    FIXME("(%p %p) stub\n", this, arg1);
    return NULL;
}

/* ??5?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV01@AAI@Z */
/* ??5?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@AEAI@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_read_uint, 8)
basic_istream_char* __thiscall basic_istream_char_read_uint(basic_istream_char *this, unsigned int *arg1)
{
    FIXME("(%p %p) stub\n", this, arg1);
    return NULL;
}

/* ??5?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV01@AAJ@Z */
/* ??5?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@AEAJ@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_read_long, 8)
basic_istream_char* __thiscall basic_istream_char_read_long(basic_istream_char *this, LONG *arg1)
{
    FIXME("(%p %p) stub\n", this, arg1);
    return NULL;
}

/* ??5?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV01@AAK@Z */
/* ??5?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@AEAK@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_read_ulong, 8)
basic_istream_char* __thiscall basic_istream_char_read_ulong(basic_istream_char *this, ULONG *arg1)
{
    FIXME("(%p %p) stub\n", this, arg1);
    return NULL;
}

/* ??5?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV01@AAM@Z */
/* ??5?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@AEAM@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_read_float, 8)
basic_istream_char* __thiscall basic_istream_char_read_float(basic_istream_char *this, float *arg1)
{
    FIXME("(%p %p) stub\n", this, arg1);
    return NULL;
}

/* ??5?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV01@AAN@Z */
/* ??5?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@AEAN@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_read_double, 8)
basic_istream_char* __thiscall basic_istream_char_read_double(basic_istream_char *this, double *arg1)
{
    FIXME("(%p %p) stub\n", this, arg1);
    return NULL;
}

/* ??5?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV01@AAO@Z */
/* ??5?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@AEAO@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_read_ldouble, 8)
basic_istream_char* __thiscall basic_istream_char_read_ldouble(basic_istream_char *this, long double *arg1)
{
    FIXME("(%p %p) stub\n", this, arg1);
    return NULL;
}

/* ??5?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV01@AAPAX@Z */
/* ??5?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@AEAPEAX@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_read_ptr, 8)
basic_istream_char* __thiscall basic_istream_char_read_ptr(basic_istream_char *this, void **arg1)
{
    FIXME("(%p %p) stub\n", this, arg1);
    return NULL;
}

/* ??5?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV01@AA_J@Z */
/* ??5?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@AEA_J@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_read_int64, 8)
basic_istream_char* __thiscall basic_istream_char_read_int64(basic_istream_char *this, __int64 *arg1)
{
    FIXME("(%p %p) stub\n", this, arg1);
    return NULL;
}

/* ??5?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV01@AA_K@Z */
/* ??5?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@AEA_K@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_read_uint64, 8)
basic_istream_char* __thiscall basic_istream_char_read_uint64(basic_istream_char *this, unsigned __int64 *arg1)
{
    FIXME("(%p %p) stub\n", this, arg1);
    return NULL;
}

/* ??5?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV01@AA_N@Z */
/* ??5?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@AEA_N@Z */
DEFINE_THISCALL_WRAPPER(basic_istream_char_read_bool, 8)
basic_istream_char* __thiscall basic_istream_char_read_bool(basic_istream_char *this, MSVCP_bool *arg1)
{
    FIXME("(%p %p) stub\n", this, arg1);
    return NULL;
}
