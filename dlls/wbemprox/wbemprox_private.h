/*
 * Copyright 2009 Hans Leidekker for CodeWeavers
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

#include "wine/debug.h"
#include "wine/heap.h"
#include "wine/list.h"

enum wbm_namespace
{
    WBEMPROX_NAMESPACE_CIMV2,
    WBEMPROX_NAMESPACE_MS_WINDOWS_STORAGE,
    WBEMPROX_NAMESPACE_LAST,
};

extern IClientSecurity client_security DECLSPEC_HIDDEN;
extern struct list *table_list[WBEMPROX_NAMESPACE_LAST] DECLSPEC_HIDDEN;

enum param_direction
{
    PARAM_OUT   = -1,
    PARAM_INOUT = 0,
    PARAM_IN    = 1
};

#define CIM_TYPE_MASK    0x00000fff

#define COL_TYPE_MASK    0x0000ffff
#define COL_FLAG_DYNAMIC 0x00010000
#define COL_FLAG_KEY     0x00020000
#define COL_FLAG_METHOD  0x00040000

typedef HRESULT (class_method)(IWbemClassObject *object, IWbemContext *context, IWbemClassObject *in_params,
        IWbemClassObject **out_params);

enum operator
{
    OP_EQ      = 1,
    OP_AND     = 2,
    OP_OR      = 3,
    OP_GT      = 4,
    OP_LT      = 5,
    OP_LE      = 6,
    OP_GE      = 7,
    OP_NE      = 8,
    OP_ISNULL  = 9,
    OP_NOTNULL = 10,
    OP_LIKE    = 11,
    OP_NOT     = 12
};

struct expr;
struct complex_expr
{
    enum operator op;
    struct expr *left;
    struct expr *right;
};

enum expr_type
{
    EXPR_COMPLEX = 1,
    EXPR_UNARY   = 2,
    EXPR_PROPVAL = 3,
    EXPR_SVAL    = 4,
    EXPR_IVAL    = 5,
    EXPR_BVAL    = 6
};

struct expr
{
    enum expr_type type;
    union
    {
        struct complex_expr expr;
        const struct property *propval;
        const WCHAR *sval;
        int ival;
    } u;
};

struct column
{
    const WCHAR *name;
    UINT type;
};

enum fill_status
{
    FILL_STATUS_FAILED = -1,
    FILL_STATUS_UNFILTERED,
    FILL_STATUS_FILTERED
};

#define TABLE_FLAG_DYNAMIC 0x00000001

struct table
{
    const WCHAR *name;
    UINT num_cols;
    const struct column *columns;
    UINT num_rows;
    UINT num_rows_allocated;
    BYTE *data;
    enum fill_status (*fill)(struct table *, const struct expr *cond);
    UINT flags;
    struct list entry;
    LONG refs;
};

struct property
{
    const WCHAR *name;
    const WCHAR *class;
    const struct property *next;
};

struct array
{
    UINT elem_size;
    UINT count;
    void *ptr;
};

struct field
{
    UINT type;
    union
    {
        LONGLONG ival;
        WCHAR *sval;
        struct array *aval;
    } u;
};

struct record
{
    UINT count;
    struct field *fields;
    struct table *table;
};

struct keyword
{
    const WCHAR *name;
    const WCHAR *value;
    const struct keyword *next;
};

enum view_type
{
    VIEW_TYPE_SELECT,
    VIEW_TYPE_ASSOCIATORS,
};

struct view
{
    enum wbm_namespace ns;
    enum view_type type;
    const WCHAR *path;                      /* ASSOCIATORS OF query */
    const struct keyword *keywordlist;
    const struct property *proplist;        /* SELECT query */
    const struct expr *cond;
    UINT table_count;
    struct table **table;
    UINT result_count;
    UINT *result;
};

struct query
{
    LONG refs;
    enum wbm_namespace ns;
    struct view *view;
    struct list mem;
};

struct path
{
    WCHAR *class;
    UINT   class_len;
    WCHAR *filter;
    UINT   filter_len;
};

HRESULT parse_path( const WCHAR *, struct path ** ) DECLSPEC_HIDDEN;
void free_path( struct path * ) DECLSPEC_HIDDEN;
WCHAR *query_from_path( const struct path * ) DECLSPEC_HIDDEN;

struct query *create_query( enum wbm_namespace ) DECLSPEC_HIDDEN;
void free_query( struct query * ) DECLSPEC_HIDDEN;
struct query *addref_query( struct query * ) DECLSPEC_HIDDEN;
void release_query( struct query *query ) DECLSPEC_HIDDEN;
HRESULT exec_query( enum wbm_namespace, const WCHAR *, IEnumWbemClassObject ** ) DECLSPEC_HIDDEN;
HRESULT parse_query( enum wbm_namespace, const WCHAR *, struct view **, struct list * ) DECLSPEC_HIDDEN;
HRESULT create_view( enum view_type, enum wbm_namespace, const WCHAR *, const struct keyword *, const WCHAR *,
                     const struct property *, const struct expr *, struct view ** ) DECLSPEC_HIDDEN;
void destroy_view( struct view * ) DECLSPEC_HIDDEN;
HRESULT execute_view( struct view * ) DECLSPEC_HIDDEN;
struct table *get_view_table( const struct view *, UINT ) DECLSPEC_HIDDEN;
void init_table_list( void ) DECLSPEC_HIDDEN;
enum wbm_namespace get_namespace_from_string( const WCHAR *namespace ) DECLSPEC_HIDDEN;
struct table *grab_table( enum wbm_namespace, const WCHAR * ) DECLSPEC_HIDDEN;
struct table *addref_table( struct table * ) DECLSPEC_HIDDEN;
void release_table( struct table * ) DECLSPEC_HIDDEN;
struct table *create_table( const WCHAR *, UINT, const struct column *, UINT, UINT, BYTE *,
                            enum fill_status (*)(struct table *, const struct expr *) ) DECLSPEC_HIDDEN;
BOOL add_table( enum wbm_namespace, struct table * ) DECLSPEC_HIDDEN;
void free_columns( struct column *, UINT ) DECLSPEC_HIDDEN;
void free_row_values( const struct table *, UINT ) DECLSPEC_HIDDEN;
void clear_table( struct table * ) DECLSPEC_HIDDEN;
void free_table( struct table * ) DECLSPEC_HIDDEN;
UINT get_type_size( CIMTYPE ) DECLSPEC_HIDDEN;
HRESULT eval_cond( const struct table *, UINT, const struct expr *, LONGLONG *, UINT * ) DECLSPEC_HIDDEN;
HRESULT get_column_index( const struct table *, const WCHAR *, UINT * ) DECLSPEC_HIDDEN;
HRESULT get_value( const struct table *, UINT, UINT, LONGLONG * ) DECLSPEC_HIDDEN;
BSTR get_value_bstr( const struct table *, UINT, UINT ) DECLSPEC_HIDDEN;
HRESULT set_value( const struct table *, UINT, UINT, LONGLONG, CIMTYPE ) DECLSPEC_HIDDEN;
BOOL is_method( const struct table *, UINT ) DECLSPEC_HIDDEN;
HRESULT get_method( const struct table *, const WCHAR *, class_method ** ) DECLSPEC_HIDDEN;
HRESULT get_propval( const struct view *, UINT, const WCHAR *, VARIANT *, CIMTYPE *, LONG * ) DECLSPEC_HIDDEN;
HRESULT put_propval( const struct view *, UINT, const WCHAR *, VARIANT *, CIMTYPE ) DECLSPEC_HIDDEN;
HRESULT to_longlong( VARIANT *, LONGLONG *, CIMTYPE * ) DECLSPEC_HIDDEN;
SAFEARRAY *to_safearray( const struct array *, CIMTYPE ) DECLSPEC_HIDDEN;
VARTYPE to_vartype( CIMTYPE ) DECLSPEC_HIDDEN;
void destroy_array( struct array *, CIMTYPE ) DECLSPEC_HIDDEN;
BOOL is_result_prop( const struct view *, const WCHAR * ) DECLSPEC_HIDDEN;
HRESULT get_properties( const struct view *, UINT, LONG, SAFEARRAY ** ) DECLSPEC_HIDDEN;
HRESULT get_object( enum wbm_namespace ns, const WCHAR *, IWbemClassObject ** ) DECLSPEC_HIDDEN;
BSTR get_method_name( enum wbm_namespace ns, const WCHAR *, UINT ) DECLSPEC_HIDDEN;
void set_variant( VARTYPE, LONGLONG, void *, VARIANT * ) DECLSPEC_HIDDEN;
HRESULT create_signature( enum wbm_namespace ns, const WCHAR *, const WCHAR *, enum param_direction,
                          IWbemClassObject ** ) DECLSPEC_HIDDEN;

HRESULT WbemLocator_create(LPVOID *) DECLSPEC_HIDDEN;
HRESULT WbemServices_create(const WCHAR *, IWbemContext *, LPVOID *) DECLSPEC_HIDDEN;
HRESULT WbemContext_create(void **) DECLSPEC_HIDDEN;
HRESULT create_class_object(enum wbm_namespace ns, const WCHAR *, IEnumWbemClassObject *, UINT,
                            struct record *, IWbemClassObject **) DECLSPEC_HIDDEN;
HRESULT EnumWbemClassObject_create(struct query *, LPVOID *) DECLSPEC_HIDDEN;
HRESULT WbemQualifierSet_create(enum wbm_namespace, const WCHAR *, const WCHAR *, LPVOID *) DECLSPEC_HIDDEN;

HRESULT process_get_owner(IWbemClassObject *obj, IWbemContext *context, IWbemClassObject *in, IWbemClassObject **out) DECLSPEC_HIDDEN;
HRESULT process_create(IWbemClassObject *obj, IWbemContext *context, IWbemClassObject *in, IWbemClassObject **out) DECLSPEC_HIDDEN;
HRESULT reg_create_key(IWbemClassObject *obj, IWbemContext *context, IWbemClassObject *in, IWbemClassObject **out) DECLSPEC_HIDDEN;
HRESULT reg_enum_key(IWbemClassObject *obj, IWbemContext *context, IWbemClassObject *in, IWbemClassObject **out) DECLSPEC_HIDDEN;
HRESULT reg_enum_values(IWbemClassObject *obj, IWbemContext *context, IWbemClassObject *in, IWbemClassObject **out) DECLSPEC_HIDDEN;
HRESULT reg_get_stringvalue(IWbemClassObject *obj, IWbemContext *context, IWbemClassObject *in, IWbemClassObject **out) DECLSPEC_HIDDEN;
HRESULT reg_set_stringvalue(IWbemClassObject *obj, IWbemContext *context, IWbemClassObject *in, IWbemClassObject **out) DECLSPEC_HIDDEN;
HRESULT reg_set_dwordvalue(IWbemClassObject *obj, IWbemContext *context, IWbemClassObject *in, IWbemClassObject **out) DECLSPEC_HIDDEN;
HRESULT reg_delete_key(IWbemClassObject *obj, IWbemContext *context, IWbemClassObject *in, IWbemClassObject **out) DECLSPEC_HIDDEN;
HRESULT service_pause_service(IWbemClassObject *obj, IWbemContext *context, IWbemClassObject *in, IWbemClassObject **out) DECLSPEC_HIDDEN;
HRESULT service_resume_service(IWbemClassObject *obj, IWbemContext *context, IWbemClassObject *in, IWbemClassObject **out) DECLSPEC_HIDDEN;
HRESULT service_start_service(IWbemClassObject *obj, IWbemContext *context, IWbemClassObject *in, IWbemClassObject **out) DECLSPEC_HIDDEN;
HRESULT service_stop_service(IWbemClassObject *obj, IWbemContext *context, IWbemClassObject *in, IWbemClassObject **out) DECLSPEC_HIDDEN;
HRESULT security_get_sd(IWbemClassObject *obj, IWbemContext *context, IWbemClassObject *in, IWbemClassObject **out) DECLSPEC_HIDDEN;
HRESULT security_set_sd(IWbemClassObject *obj, IWbemContext *context, IWbemClassObject *in, IWbemClassObject **out) DECLSPEC_HIDDEN;
HRESULT sysrestore_create(IWbemClassObject *obj, IWbemContext *context, IWbemClassObject *in, IWbemClassObject **out) DECLSPEC_HIDDEN;
HRESULT sysrestore_disable(IWbemClassObject *obj, IWbemContext *context, IWbemClassObject *in, IWbemClassObject **out) DECLSPEC_HIDDEN;
HRESULT sysrestore_enable(IWbemClassObject *obj, IWbemContext *context, IWbemClassObject *in, IWbemClassObject **out) DECLSPEC_HIDDEN;
HRESULT sysrestore_get_last_status(IWbemClassObject *obj, IWbemContext *context, IWbemClassObject *in, IWbemClassObject **out) DECLSPEC_HIDDEN;
HRESULT sysrestore_restore(IWbemClassObject *obj, IWbemContext *context, IWbemClassObject *in, IWbemClassObject **out) DECLSPEC_HIDDEN;

static inline WCHAR *heap_strdupW( const WCHAR *src )
{
    WCHAR *dst;
    if (!src) return NULL;
    if ((dst = heap_alloc( (lstrlenW( src ) + 1) * sizeof(WCHAR) ))) lstrcpyW( dst, src );
    return dst;
}

static inline WCHAR *heap_strdupAW( const char *src )
{
    int len;
    WCHAR *dst;
    if (!src) return NULL;
    len = MultiByteToWideChar( CP_ACP, 0, src, -1, NULL, 0 );
    if ((dst = heap_alloc( len * sizeof(*dst) ))) MultiByteToWideChar( CP_ACP, 0, src, -1, dst, len );
    return dst;
}

static inline BOOL is_digit(WCHAR c)
{
    return '0' <= c && c <= '9';
}
