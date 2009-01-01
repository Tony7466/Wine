/*
 * Implementation of the Microsoft Installer (msi.dll)
 *
 * Copyright 2002,2003,2004,2005 Mike McCormack for CodeWeavers
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

#include <stdarg.h>

#define COBJMACROS
#define NONAMELESSUNION

#include "windef.h"
#include "winbase.h"
#include "winreg.h"
#include "winnls.h"
#include "wine/debug.h"
#include "wine/unicode.h"
#include "msi.h"
#include "msiquery.h"
#include "msipriv.h"
#include "objidl.h"
#include "objbase.h"
#include "msiserver.h"
#include "query.h"

#include "initguid.h"

WINE_DEFAULT_DEBUG_CHANNEL(msi);

DEFINE_GUID( CLSID_MsiDatabase, 0x000c1084, 0x0000, 0x0000,
             0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46);
DEFINE_GUID( CLSID_MsiPatch, 0x000c1086, 0x0000, 0x0000,
             0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46);

/*
 *  .MSI  file format
 *
 *  An .msi file is a structured storage file.
 *  It contains a number of streams.
 *  A stream for each table in the database.
 *  Two streams for the string table in the database.
 *  Any binary data in a table is a reference to a stream.
 */

static VOID MSI_CloseDatabase( MSIOBJECTHDR *arg )
{
    MSIDATABASE *db = (MSIDATABASE *) arg;

    msi_free(db->path);
    free_cached_tables( db );
    msi_free_transforms( db );
    msi_destroy_stringtable( db->strings );
    IStorage_Release( db->storage );
    if (db->deletefile)
    {
        DeleteFileW( db->deletefile );
        msi_free( db->deletefile );
    }
}

UINT MSI_OpenDatabaseW(LPCWSTR szDBPath, LPCWSTR szPersist, MSIDATABASE **pdb)
{
    IStorage *stg = NULL;
    HRESULT r;
    MSIDATABASE *db = NULL;
    UINT ret = ERROR_FUNCTION_FAILED;
    LPCWSTR szMode, save_path;
    STATSTG stat;
    BOOL created = FALSE;
    WCHAR path[MAX_PATH];

    static const WCHAR backslash[] = {'\\',0};
    static const WCHAR szTables[]  = { '_','T','a','b','l','e','s',0 };

    TRACE("%s %s\n",debugstr_w(szDBPath),debugstr_w(szPersist) );

    if( !pdb )
        return ERROR_INVALID_PARAMETER;

    if (szPersist - MSIDBOPEN_PATCHFILE >= MSIDBOPEN_READONLY &&
        szPersist - MSIDBOPEN_PATCHFILE <= MSIDBOPEN_CREATEDIRECT)
    {
        TRACE("Database is a patch\n");
        szPersist -= MSIDBOPEN_PATCHFILE;
    }

    save_path = szDBPath;
    szMode = szPersist;
    if( HIWORD( szPersist ) )
    {
        if (!CopyFileW( szDBPath, szPersist, FALSE ))
            return ERROR_OPEN_FAILED;

        szDBPath = szPersist;
        szPersist = MSIDBOPEN_TRANSACT;
        created = TRUE;
    }

    if( szPersist == MSIDBOPEN_READONLY )
    {
        r = StgOpenStorage( szDBPath, NULL,
              STGM_DIRECT|STGM_READ|STGM_SHARE_DENY_WRITE, NULL, 0, &stg);
    }
    else if( szPersist == MSIDBOPEN_CREATE || szPersist == MSIDBOPEN_CREATEDIRECT )
    {
        /* FIXME: MSIDBOPEN_CREATE should case STGM_TRANSACTED flag to be
         * used here: */
        r = StgCreateDocfile( szDBPath,
              STGM_CREATE|STGM_DIRECT|STGM_READWRITE|STGM_SHARE_EXCLUSIVE, 0, &stg);
        if( r == ERROR_SUCCESS )
        {
            IStorage_SetClass( stg, &CLSID_MsiDatabase );
            /* create the _Tables stream */
            r = write_stream_data(stg, szTables, NULL, 0, TRUE);
            if (SUCCEEDED(r))
                r = msi_init_string_table( stg );
        }
        created = TRUE;
    }
    else if( szPersist == MSIDBOPEN_TRANSACT )
    {
        /* FIXME: MSIDBOPEN_TRANSACT should case STGM_TRANSACTED flag to be
         * used here: */
        r = StgOpenStorage( szDBPath, NULL,
              STGM_DIRECT|STGM_READWRITE|STGM_SHARE_EXCLUSIVE, NULL, 0, &stg);
    }
    else if( szPersist == MSIDBOPEN_DIRECT )
    {
        r = StgOpenStorage( szDBPath, NULL,
              STGM_DIRECT|STGM_READWRITE|STGM_SHARE_EXCLUSIVE, NULL, 0, &stg);
    }
    else
    {
        ERR("unknown flag %p\n",szPersist);
        return ERROR_INVALID_PARAMETER;
    }

    if( FAILED( r ) || !stg )
    {
        FIXME("open failed r = %08x for %s\n", r, debugstr_w(szDBPath));
        return ERROR_FUNCTION_FAILED;
    }

    r = IStorage_Stat( stg, &stat, STATFLAG_NONAME );
    if( FAILED( r ) )
    {
        FIXME("Failed to stat storage\n");
        goto end;
    }

    if ( !IsEqualGUID( &stat.clsid, &CLSID_MsiDatabase ) &&
         !IsEqualGUID( &stat.clsid, &CLSID_MsiPatch ) ) 
    {
        ERR("storage GUID is not a MSI database GUID %s\n",
             debugstr_guid(&stat.clsid) );
        goto end;
    }

    db = alloc_msiobject( MSIHANDLETYPE_DATABASE, sizeof (MSIDATABASE),
                              MSI_CloseDatabase );
    if( !db )
    {
        FIXME("Failed to allocate a handle\n");
        goto end;
    }

    if (!strchrW( save_path, '\\' ))
    {
        GetCurrentDirectoryW( MAX_PATH, path );
        lstrcatW( path, backslash );
        lstrcatW( path, save_path );
    }
    else
        lstrcpyW( path, save_path );

    db->path = strdupW( path );

    if( TRACE_ON( msi ) )
        enum_stream_names( stg );

    db->storage = stg;
    db->mode = szMode;
    if (created)
        db->deletefile = strdupW( szDBPath );
    else
        db->deletefile = NULL;
    list_init( &db->tables );
    list_init( &db->transforms );

    db->strings = msi_load_string_table( stg, &db->bytes_per_strref );
    if( !db->strings )
        goto end;

    ret = ERROR_SUCCESS;

    msiobj_addref( &db->hdr );
    IStorage_AddRef( stg );
    *pdb = db;

end:
    if( db )
        msiobj_release( &db->hdr );
    if( stg )
        IStorage_Release( stg );

    return ret;
}

UINT WINAPI MsiOpenDatabaseW(LPCWSTR szDBPath, LPCWSTR szPersist, MSIHANDLE *phDB)
{
    MSIDATABASE *db;
    UINT ret;

    TRACE("%s %s %p\n",debugstr_w(szDBPath),debugstr_w(szPersist), phDB);

    ret = MSI_OpenDatabaseW( szDBPath, szPersist, &db );
    if( ret == ERROR_SUCCESS )
    {
        *phDB = alloc_msihandle( &db->hdr );
        if (! *phDB)
            ret = ERROR_NOT_ENOUGH_MEMORY;
        msiobj_release( &db->hdr );
    }

    return ret;
}

UINT WINAPI MsiOpenDatabaseA(LPCSTR szDBPath, LPCSTR szPersist, MSIHANDLE *phDB)
{
    HRESULT r = ERROR_FUNCTION_FAILED;
    LPWSTR szwDBPath = NULL, szwPersist = NULL;

    TRACE("%s %s %p\n", debugstr_a(szDBPath), debugstr_a(szPersist), phDB);

    if( szDBPath )
    {
        szwDBPath = strdupAtoW( szDBPath );
        if( !szwDBPath )
            goto end;
    }

    if( HIWORD(szPersist) )
    {
        szwPersist = strdupAtoW( szPersist );
        if( !szwPersist )
            goto end;
    }
    else
        szwPersist = (LPWSTR)(DWORD_PTR)szPersist;

    r = MsiOpenDatabaseW( szwDBPath, szwPersist, phDB );

end:
    if( HIWORD(szPersist) )
        msi_free( szwPersist );
    msi_free( szwDBPath );

    return r;
}

static LPWSTR msi_read_text_archive(LPCWSTR path)
{
    HANDLE file;
    LPSTR data = NULL;
    LPWSTR wdata = NULL;
    DWORD read, size = 0;

    file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
    if (file == INVALID_HANDLE_VALUE)
        return NULL;

    size = GetFileSize( file, NULL );
    data = msi_alloc( size + 1 );
    if (!data)
        goto done;

    if (!ReadFile( file, data, size, &read, NULL ))
        goto done;

    data[size] = '\0';
    wdata = strdupAtoW( data );

done:
    CloseHandle( file );
    msi_free( data );
    return wdata;
}

static void msi_parse_line(LPWSTR *line, LPWSTR **entries, DWORD *num_entries)
{
    LPWSTR ptr = *line, save;
    DWORD i, count = 1;

    *entries = NULL;

    /* stay on this line */
    while (*ptr && *ptr != '\n')
    {
        /* entries are separated by tabs */
        if (*ptr == '\t')
            count++;

        ptr++;
    }

    *entries = msi_alloc(count * sizeof(LPWSTR));
    if (!*entries)
        return;

    /* store pointers into the data */
    for (i = 0, ptr = *line; i < count; i++)
    {
        while (*ptr && *ptr == '\r') ptr++;
        save = ptr;

        while (*ptr && *ptr != '\t' && *ptr != '\n' && *ptr != '\r') ptr++;

        /* NULL-separate the data */
        if (*ptr == '\n' || *ptr == '\r')
        {
            while (*ptr == '\n' || *ptr == '\r')
                *(ptr++) = '\0';
        }
        else if (*ptr)
            *ptr++ = '\0';

        (*entries)[i] = save;
    }

    /* move to the next line if there's more, else EOF */
    *line = ptr;

    if (num_entries)
        *num_entries = count;
}

static LPWSTR msi_build_createsql_prelude(LPWSTR table)
{
    LPWSTR prelude;
    DWORD size;

    static const WCHAR create_fmt[] = {'C','R','E','A','T','E',' ','T','A','B','L','E',' ','`','%','s','`',' ','(',' ',0};

    size = sizeof(create_fmt)/sizeof(create_fmt[0]) + lstrlenW(table) - 2;
    prelude = msi_alloc(size * sizeof(WCHAR));
    if (!prelude)
        return NULL;

    sprintfW(prelude, create_fmt, table);
    return prelude;
}

static LPWSTR msi_build_createsql_columns(LPWSTR *columns_data, LPWSTR *types, DWORD num_columns)
{
    LPWSTR columns, p;
    LPCWSTR type;
    DWORD sql_size = 1, i, len;
    WCHAR expanded[128], *ptr;
    WCHAR size[10], comma[2], extra[30];

    static const WCHAR column_fmt[] = {'`','%','s','`',' ','%','s','%','s','%','s','%','s',' ',0};
    static const WCHAR size_fmt[] = {'(','%','s',')',0};
    static const WCHAR type_char[] = {'C','H','A','R',0};
    static const WCHAR type_int[] = {'I','N','T',0};
    static const WCHAR type_long[] = {'L','O','N','G',0};
    static const WCHAR type_notnull[] = {' ','N','O','T',' ','N','U','L','L',0};
    static const WCHAR localizable[] = {' ','L','O','C','A','L','I','Z','A','B','L','E',0};

    columns = msi_alloc_zero(sql_size * sizeof(WCHAR));
    if (!columns)
        return NULL;

    for (i = 0; i < num_columns; i++)
    {
        type = NULL;
        comma[1] = size[0] = extra[0] = '\0';

        if (i == num_columns - 1)
            comma[0] = '\0';
        else
            comma[0] = ',';

        ptr = &types[i][1];
        len = atolW(ptr);
        extra[0] = '\0';

        switch (types[i][0])
        {
            case 'l':
                lstrcpyW(extra, type_notnull);
            case 'L':
                lstrcatW(extra, localizable);
                type = type_char;
                sprintfW(size, size_fmt, ptr);
                break;
            case 's':
                lstrcpyW(extra, type_notnull);
            case 'S':
                type = type_char;
                sprintfW(size, size_fmt, ptr);
                break;
            case 'i':
                lstrcpyW(extra, type_notnull);
            case 'I':
                if (len == 2)
                    type = type_int;
                else
                    type = type_long;
                break;
            default:
                ERR("Unknown type: %c\n", types[i][0]);
                msi_free(columns);
                return NULL;
        }

        sprintfW(expanded, column_fmt, columns_data[i], type, size, extra, comma);
        sql_size += lstrlenW(expanded);

        p = msi_realloc(columns, sql_size * sizeof(WCHAR));
        if (!p)
        {
            msi_free(columns);
            return NULL;
        }
        columns = p;

        lstrcatW(columns, expanded);
    }

    return columns;
}

static LPWSTR msi_build_createsql_postlude(LPWSTR *primary_keys, DWORD num_keys)
{
    LPWSTR postlude, keys, ptr;
    DWORD size, key_size, i;

    static const WCHAR key_fmt[] = {'`','%','s','`',',',' ',0};
    static const WCHAR postlude_fmt[] = {'P','R','I','M','A','R','Y',' ','K','E','Y',' ','%','s',')',0};

    for (i = 0, size = 1; i < num_keys; i++)
        size += lstrlenW(key_fmt) + lstrlenW(primary_keys[i]) - 2;

    keys = msi_alloc(size * sizeof(WCHAR));
    if (!keys)
        return NULL;

    for (i = 0, ptr = keys; i < num_keys; i++)
    {
        key_size = lstrlenW(key_fmt) + lstrlenW(primary_keys[i]) -2;
        sprintfW(ptr, key_fmt, primary_keys[i]);
        ptr += key_size;
    }

    /* remove final ', ' */
    *(ptr - 2) = '\0';

    size = lstrlenW(postlude_fmt) + size - 1;
    postlude = msi_alloc(size * sizeof(WCHAR));
    if (!postlude)
        goto done;

    sprintfW(postlude, postlude_fmt, keys);

done:
    msi_free(keys);
    return postlude;
}

static UINT msi_add_table_to_db(MSIDATABASE *db, LPWSTR *columns, LPWSTR *types, LPWSTR *labels, DWORD num_labels, DWORD num_columns)
{
    UINT r;
    DWORD size;
    MSIQUERY *view;
    LPWSTR create_sql;
    LPWSTR prelude, columns_sql, postlude;

    prelude = msi_build_createsql_prelude(labels[0]);
    columns_sql = msi_build_createsql_columns(columns, types, num_columns);
    postlude = msi_build_createsql_postlude(labels + 1, num_labels - 1); /* skip over table name */

    if (!prelude || !columns_sql || !postlude)
        return ERROR_OUTOFMEMORY;

    size = lstrlenW(prelude) + lstrlenW(columns_sql) + lstrlenW(postlude) + 1;
    create_sql = msi_alloc(size * sizeof(WCHAR));
    if (!create_sql)
        return ERROR_OUTOFMEMORY;

    lstrcpyW(create_sql, prelude);
    lstrcatW(create_sql, columns_sql);
    lstrcatW(create_sql, postlude);

    msi_free(prelude);
    msi_free(columns_sql);
    msi_free(postlude);

    r = MSI_DatabaseOpenViewW( db, create_sql, &view );
    msi_free(create_sql);

    if (r != ERROR_SUCCESS)
        return r;

    r = MSI_ViewExecute(view, NULL);
    MSI_ViewClose(view);
    msiobj_release(&view->hdr);

    return r;
}

static UINT construct_record(DWORD num_columns, LPWSTR *types,
                             LPWSTR *data, MSIRECORD **rec)
{
    UINT i;

    *rec = MSI_CreateRecord(num_columns);
    if (!*rec)
        return ERROR_OUTOFMEMORY;

    for (i = 0; i < num_columns; i++)
    {
        switch (types[i][0])
        {
            case 'L': case 'l': case 'S': case 's':
                MSI_RecordSetStringW(*rec, i + 1, data[i]);
                break;
            case 'I': case 'i':
                if (*data[i])
                    MSI_RecordSetInteger(*rec, i + 1, atoiW(data[i]));
                break;
            default:
                ERR("Unhandled column type: %c\n", types[i][0]);
                msiobj_release(&(*rec)->hdr);
                return ERROR_FUNCTION_FAILED;
        }
    }

    return ERROR_SUCCESS;
}

static UINT msi_add_records_to_table(MSIDATABASE *db, LPWSTR *columns, LPWSTR *types,
                                     LPWSTR *labels, LPWSTR **records,
                                     int num_columns, int num_records)
{
    UINT r;
    DWORD i;
    MSIQUERY *view;
    MSIRECORD *rec;

    static const WCHAR select[] = {
        'S','E','L','E','C','T',' ','*',' ',
        'F','R','O','M',' ','`','%','s','`',0
    };

    r = MSI_OpenQuery(db, &view, select, labels[0]);
    if (r != ERROR_SUCCESS)
        return r;

    while (MSI_ViewFetch(view, &rec) != ERROR_NO_MORE_ITEMS)
    {
        r = MSI_ViewModify(view, MSIMODIFY_DELETE, rec);
        if (r != ERROR_SUCCESS)
            goto done;
    }

    for (i = 0; i < num_records; i++)
    {
        r = construct_record(num_columns, types, records[i], &rec);
        if (r != ERROR_SUCCESS)
            goto done;

        r = MSI_ViewModify(view, MSIMODIFY_INSERT, rec);
        if (r != ERROR_SUCCESS)
        {
            msiobj_release(&rec->hdr);
            goto done;
        }

        msiobj_release(&rec->hdr);
    }

done:
    msiobj_release(&view->hdr);
    return r;
}

UINT MSI_DatabaseImport(MSIDATABASE *db, LPCWSTR folder, LPCWSTR file)
{
    UINT r;
    DWORD len, i;
    DWORD num_labels, num_types;
    DWORD num_columns, num_records = 0;
    LPWSTR *columns, *types, *labels;
    LPWSTR path, ptr, data;
    LPWSTR **records = NULL;
    LPWSTR **temp_records;

    static const WCHAR backslash[] = {'\\',0};

    TRACE("%p %s %s\n", db, debugstr_w(folder), debugstr_w(file) );

    if( folder == NULL || file == NULL )
        return ERROR_INVALID_PARAMETER;

    len = lstrlenW(folder) + lstrlenW(backslash) + lstrlenW(file) + 1;
    path = msi_alloc( len * sizeof(WCHAR) );
    if (!path)
        return ERROR_OUTOFMEMORY;

    lstrcpyW( path, folder );
    lstrcatW( path, backslash );
    lstrcatW( path, file );

    data = msi_read_text_archive( path );

    ptr = data;
    msi_parse_line( &ptr, &columns, &num_columns );
    msi_parse_line( &ptr, &types, &num_types );
    msi_parse_line( &ptr, &labels, &num_labels );

    if (num_columns != num_types)
    {
        r = ERROR_FUNCTION_FAILED;
        goto done;
    }

    records = msi_alloc(sizeof(LPWSTR *));
    if (!records)
    {
        r = ERROR_OUTOFMEMORY;
        goto done;
    }

    /* read in the table records */
    while (*ptr)
    {
        msi_parse_line( &ptr, &records[num_records], NULL );

        num_records++;
        temp_records = msi_realloc(records, (num_records + 1) * sizeof(LPWSTR *));
        if (!temp_records)
        {
            r = ERROR_OUTOFMEMORY;
            goto done;
        }
        records = temp_records;
    }

    if (!TABLE_Exists(db, labels[0]))
    {
        r = msi_add_table_to_db( db, columns, types, labels, num_labels, num_columns );
        if (r != ERROR_SUCCESS)
        {
            r = ERROR_FUNCTION_FAILED;
            goto done;
        }
    }

    r = msi_add_records_to_table( db, columns, types, labels, records, num_columns, num_records );

done:
    msi_free(path);
    msi_free(data);
    msi_free(columns);
    msi_free(types);
    msi_free(labels);

    for (i = 0; i < num_records; i++)
        msi_free(records[i]);

    msi_free(records);

    return r;
}

UINT WINAPI MsiDatabaseImportW(MSIHANDLE handle, LPCWSTR szFolder, LPCWSTR szFilename)
{
    MSIDATABASE *db;
    UINT r;

    TRACE("%lx %s %s\n",handle,debugstr_w(szFolder), debugstr_w(szFilename));

    db = msihandle2msiinfo( handle, MSIHANDLETYPE_DATABASE );
    if( !db )
    {
        IWineMsiRemoteDatabase *remote_database;

        remote_database = (IWineMsiRemoteDatabase *)msi_get_remote( handle );
        if ( !remote_database )
            return ERROR_INVALID_HANDLE;

        IWineMsiRemoteDatabase_Release( remote_database );
        WARN("MsiDatabaseImport not allowed during a custom action!\n");

        return ERROR_SUCCESS;
    }

    r = MSI_DatabaseImport( db, szFolder, szFilename );
    msiobj_release( &db->hdr );
    return r;
}

UINT WINAPI MsiDatabaseImportA( MSIHANDLE handle,
               LPCSTR szFolder, LPCSTR szFilename )
{
    LPWSTR path = NULL, file = NULL;
    UINT r = ERROR_OUTOFMEMORY;

    TRACE("%lx %s %s\n", handle, debugstr_a(szFolder), debugstr_a(szFilename));

    if( szFolder )
    {
        path = strdupAtoW( szFolder );
        if( !path )
            goto end;
    }

    if( szFilename )
    {
        file = strdupAtoW( szFilename );
        if( !file )
            goto end;
    }

    r = MsiDatabaseImportW( handle, path, file );

end:
    msi_free( path );
    msi_free( file );

    return r;
}

static UINT msi_export_record( HANDLE handle, MSIRECORD *row, UINT start )
{
    UINT i, count, len, r = ERROR_SUCCESS;
    const char *sep;
    char *buffer;
    DWORD sz;

    len = 0x100;
    buffer = msi_alloc( len );
    if ( !buffer )
        return ERROR_OUTOFMEMORY;

    count = MSI_RecordGetFieldCount( row );
    for ( i=start; i<=count; i++ )
    {
        sz = len;
        r = MSI_RecordGetStringA( row, i, buffer, &sz );
        if (r == ERROR_MORE_DATA)
        {
            char *p = msi_realloc( buffer, sz + 1 );
            if (!p)
                break;
            len = sz + 1;
            buffer = p;
        }
        sz = len;
        r = MSI_RecordGetStringA( row, i, buffer, &sz );
        if (r != ERROR_SUCCESS)
            break;

        if (!WriteFile( handle, buffer, sz, &sz, NULL ))
        {
            r = ERROR_FUNCTION_FAILED;
            break;
        }

        sep = (i < count) ? "\t" : "\r\n";
        if (!WriteFile( handle, sep, strlen(sep), &sz, NULL ))
        {
            r = ERROR_FUNCTION_FAILED;
            break;
        }
    }
    msi_free( buffer );
    return r;
}

static UINT msi_export_row( MSIRECORD *row, void *arg )
{
    return msi_export_record( arg, row, 1 );
}

static UINT msi_export_forcecodepage( HANDLE handle )
{
    DWORD sz;

    static const char data[] = "\r\n\r\n0\t_ForceCodepage\r\n";

    FIXME("Read the codepage from the strings table!\n");

    sz = lstrlenA(data) + 1;
    if (!WriteFile(handle, data, sz, &sz, NULL))
        return ERROR_FUNCTION_FAILED;

    return ERROR_SUCCESS;
}

UINT MSI_DatabaseExport( MSIDATABASE *db, LPCWSTR table,
               LPCWSTR folder, LPCWSTR file )
{
    static const WCHAR query[] = {
        's','e','l','e','c','t',' ','*',' ','f','r','o','m',' ','%','s',0 };
    static const WCHAR szbs[] = { '\\', 0 };
    static const WCHAR forcecodepage[] = {
        '_','F','o','r','c','e','C','o','d','e','p','a','g','e',0 };
    MSIRECORD *rec = NULL;
    MSIQUERY *view = NULL;
    LPWSTR filename;
    HANDLE handle;
    UINT len, r;

    TRACE("%p %s %s %s\n", db, debugstr_w(table),
          debugstr_w(folder), debugstr_w(file) );

    if( folder == NULL || file == NULL )
        return ERROR_INVALID_PARAMETER;

    len = lstrlenW(folder) + lstrlenW(file) + 2;
    filename = msi_alloc(len * sizeof (WCHAR));
    if (!filename)
        return ERROR_OUTOFMEMORY;

    lstrcpyW( filename, folder );
    lstrcatW( filename, szbs );
    lstrcatW( filename, file );

    handle = CreateFileW( filename, GENERIC_READ | GENERIC_WRITE, 0,
                          NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
    msi_free( filename );
    if (handle == INVALID_HANDLE_VALUE)
        return ERROR_FUNCTION_FAILED;

    if (!lstrcmpW( table, forcecodepage ))
    {
        r = msi_export_forcecodepage( handle );
        goto done;
    }

    r = MSI_OpenQuery( db, &view, query, table );
    if (r == ERROR_SUCCESS)
    {
        /* write out row 1, the column names */
        r = MSI_ViewGetColumnInfo(view, MSICOLINFO_NAMES, &rec);
        if (r == ERROR_SUCCESS)
        {
            msi_export_record( handle, rec, 1 );
            msiobj_release( &rec->hdr );
        }

        /* write out row 2, the column types */
        r = MSI_ViewGetColumnInfo(view, MSICOLINFO_TYPES, &rec);
        if (r == ERROR_SUCCESS)
        {
            msi_export_record( handle, rec, 1 );
            msiobj_release( &rec->hdr );
        }

        /* write out row 3, the table name + keys */
        r = MSI_DatabaseGetPrimaryKeys( db, table, &rec );
        if (r == ERROR_SUCCESS)
        {
            MSI_RecordSetStringW( rec, 0, table );
            msi_export_record( handle, rec, 0 );
            msiobj_release( &rec->hdr );
        }

        /* write out row 4 onwards, the data */
        r = MSI_IterateRecords( view, 0, msi_export_row, handle );
        msiobj_release( &view->hdr );
    }

done:
    CloseHandle( handle );
    return r;
}

/***********************************************************************
 * MsiExportDatabaseW        [MSI.@]
 *
 * Writes a file containing the table data as tab separated ASCII.
 *
 * The format is as follows:
 *
 * row1 : colname1 <tab> colname2 <tab> .... colnameN <cr> <lf>
 * row2 : coltype1 <tab> coltype2 <tab> .... coltypeN <cr> <lf>
 * row3 : tablename <tab> key1 <tab> key2 <tab> ... keyM <cr> <lf>
 *
 * Followed by the data, starting at row 1 with one row per line
 *
 * row4 : data <tab> data <tab> data <tab> ... data <cr> <lf>
 */
UINT WINAPI MsiDatabaseExportW( MSIHANDLE handle, LPCWSTR szTable,
               LPCWSTR szFolder, LPCWSTR szFilename )
{
    MSIDATABASE *db;
    UINT r;

    TRACE("%lx %s %s %s\n", handle, debugstr_w(szTable),
          debugstr_w(szFolder), debugstr_w(szFilename));

    db = msihandle2msiinfo( handle, MSIHANDLETYPE_DATABASE );
    if( !db )
    {
        IWineMsiRemoteDatabase *remote_database;

        remote_database = (IWineMsiRemoteDatabase *)msi_get_remote( handle );
        if ( !remote_database )
            return ERROR_INVALID_HANDLE;

        IWineMsiRemoteDatabase_Release( remote_database );
        WARN("MsiDatabaseExport not allowed during a custom action!\n");

        return ERROR_SUCCESS;
    }

    r = MSI_DatabaseExport( db, szTable, szFolder, szFilename );
    msiobj_release( &db->hdr );
    return r;
}

UINT WINAPI MsiDatabaseExportA( MSIHANDLE handle, LPCSTR szTable,
               LPCSTR szFolder, LPCSTR szFilename )
{
    LPWSTR path = NULL, file = NULL, table = NULL;
    UINT r = ERROR_OUTOFMEMORY;

    TRACE("%lx %s %s %s\n", handle, debugstr_a(szTable),
          debugstr_a(szFolder), debugstr_a(szFilename));

    if( szTable )
    {
        table = strdupAtoW( szTable );
        if( !table )
            goto end;
    }

    if( szFolder )
    {
        path = strdupAtoW( szFolder );
        if( !path )
            goto end;
    }

    if( szFilename )
    {
        file = strdupAtoW( szFilename );
        if( !file )
            goto end;
    }

    r = MsiDatabaseExportW( handle, table, path, file );

end:
    msi_free( table );
    msi_free( path );
    msi_free( file );

    return r;
}

UINT WINAPI MsiDatabaseMergeA(MSIHANDLE hDatabase, MSIHANDLE hDatabaseMerge,
                              LPCSTR szTableName)
{
    UINT r;
    LPWSTR table;

    TRACE("(%ld, %ld, %s)\n", hDatabase, hDatabaseMerge,
          debugstr_a(szTableName));

    table = strdupAtoW(szTableName);
    r = MsiDatabaseMergeW(hDatabase, hDatabaseMerge, table);

    msi_free(table);
    return r;
}

typedef struct _tagMERGETABLE
{
    struct list entry;
    struct list rows;
    LPWSTR name;
    DWORD numconflicts;
} MERGETABLE;

typedef struct _tagMERGEROW
{
    struct list entry;
    MSIRECORD *data;
} MERGEROW;

typedef struct _tagMERGEDATA
{
    MSIDATABASE *db;
    MSIDATABASE *merge;
    MERGETABLE *curtable;
    MSIQUERY *curview;
    struct list *tabledata;
} MERGEDATA;

static UINT merge_verify_colnames(MSIQUERY *dbview, MSIQUERY *mergeview)
{
    MSIRECORD *dbrec, *mergerec;
    UINT r, i, count;

    r = MSI_ViewGetColumnInfo(dbview, MSICOLINFO_NAMES, &dbrec);
    if (r != ERROR_SUCCESS)
        return r;

    r = MSI_ViewGetColumnInfo(mergeview, MSICOLINFO_NAMES, &mergerec);
    if (r != ERROR_SUCCESS)
        return r;

    count = MSI_RecordGetFieldCount(dbrec);
    for (i = 1; i <= count; i++)
    {
        if (!MSI_RecordGetString(mergerec, i))
            break;

        if (lstrcmpW(MSI_RecordGetString(dbrec, i),
                     MSI_RecordGetString(mergerec, i)))
        {
            r = ERROR_DATATYPE_MISMATCH;
            goto done;
        }
    }

    msiobj_release(&dbrec->hdr);
    msiobj_release(&mergerec->hdr);
    dbrec = mergerec = NULL;

    r = MSI_ViewGetColumnInfo(dbview, MSICOLINFO_TYPES, &dbrec);
    if (r != ERROR_SUCCESS)
        return r;

    r = MSI_ViewGetColumnInfo(mergeview, MSICOLINFO_TYPES, &mergerec);
    if (r != ERROR_SUCCESS)
        return r;

    count = MSI_RecordGetFieldCount(dbrec);
    for (i = 1; i <= count; i++)
    {
        if (!MSI_RecordGetString(mergerec, i))
            break;

        if (lstrcmpW(MSI_RecordGetString(dbrec, i),
                     MSI_RecordGetString(mergerec, i)))
        {
            r = ERROR_DATATYPE_MISMATCH;
            break;
        }
    }

done:
    msiobj_release(&dbrec->hdr);
    msiobj_release(&mergerec->hdr);

    return r;
}

static UINT merge_verify_primary_keys(MSIDATABASE *db, MSIDATABASE *mergedb,
                                      LPCWSTR table)
{
    MSIRECORD *dbrec, *mergerec = NULL;
    UINT r, i, count;

    r = MSI_DatabaseGetPrimaryKeys(db, table, &dbrec);
    if (r != ERROR_SUCCESS)
        return r;

    r = MSI_DatabaseGetPrimaryKeys(mergedb, table, &mergerec);
    if (r != ERROR_SUCCESS)
        goto done;

    count = MSI_RecordGetFieldCount(dbrec);
    if (count != MSI_RecordGetFieldCount(mergerec))
    {
        r = ERROR_DATATYPE_MISMATCH;
        goto done;
    }

    for (i = 1; i <= count; i++)
    {
        if (lstrcmpW(MSI_RecordGetString(dbrec, i),
                     MSI_RecordGetString(mergerec, i)))
        {
            r = ERROR_DATATYPE_MISMATCH;
            goto done;
        }
    }

done:
    msiobj_release(&dbrec->hdr);
    msiobj_release(&mergerec->hdr);

    return r;
}

static LPWSTR get_key_value(MSIQUERY *view, LPCWSTR key, MSIRECORD *rec)
{
    MSIRECORD *colnames;
    LPWSTR str;
    UINT r, i = 0;
    int cmp;

    r = MSI_ViewGetColumnInfo(view, MSICOLINFO_NAMES, &colnames);
    if (r != ERROR_SUCCESS)
        return NULL;

    do
    {
        str = msi_dup_record_field(colnames, ++i);
        cmp = lstrcmpW(key, str);
        msi_free(str);
    } while (cmp);

    msiobj_release(&colnames->hdr);
    return msi_dup_record_field(rec, i);
}

static LPWSTR create_diff_row_query(MSIDATABASE *merge, MSIQUERY *view,
                                    LPWSTR table, MSIRECORD *rec)
{
    LPWSTR query = NULL, clause = NULL;
    LPWSTR ptr = NULL, val;
    LPCWSTR setptr;
    DWORD size = 1, oldsize;
    LPCWSTR key;
    MSIRECORD *keys;
    UINT r, i, count;

    static const WCHAR keyset[] = {
        '`','%','s','`',' ','=',' ','%','s',' ','A','N','D',' ',0};
    static const WCHAR lastkeyset[] = {
        '`','%','s','`',' ','=',' ','%','s',' ',0};
    static const WCHAR fmt[] = {'S','E','L','E','C','T',' ','*',' ',
        'F','R','O','M',' ','`','%','s','`',' ',
        'W','H','E','R','E',' ','%','s',0};

    r = MSI_DatabaseGetPrimaryKeys(merge, table, &keys);
    if (r != ERROR_SUCCESS)
        return NULL;

    clause = msi_alloc_zero(size * sizeof(WCHAR));
    if (!clause)
        goto done;

    ptr = clause;
    count = MSI_RecordGetFieldCount(keys);
    for (i = 1; i <= count; i++)
    {
        key = MSI_RecordGetString(keys, i);
        val = get_key_value(view, key, rec);

        if (i == count)
            setptr = lastkeyset;
        else
            setptr = keyset;

        oldsize = size;
        size += lstrlenW(setptr) + lstrlenW(key) + lstrlenW(val) - 4;
        clause = msi_realloc(clause, size * sizeof (WCHAR));
        if (!clause)
        {
            msi_free(val);
            goto done;
        }

        ptr = clause + oldsize - 1;
        sprintfW(ptr, setptr, key, val);
        msi_free(val);
    }

    size = lstrlenW(fmt) + lstrlenW(table) + lstrlenW(clause) + 1;
    query = msi_alloc(size * sizeof(WCHAR));
    if (!query)
        goto done;

    sprintfW(query, fmt, table, clause);

done:
    msi_free(clause);
    msiobj_release(&keys->hdr);
    return query;
}

static UINT merge_diff_row(MSIRECORD *rec, LPVOID param)
{
    MERGEDATA *data = (MERGEDATA *)param;
    MERGETABLE *table = data->curtable;
    MERGEROW *mergerow;
    MSIQUERY *dbview;
    MSIRECORD *row;
    LPWSTR query;
    UINT r;

    query = create_diff_row_query(data->merge, data->curview, table->name, rec);
    if (!query)
        return ERROR_OUTOFMEMORY;

    r = MSI_DatabaseOpenViewW(data->db, query, &dbview);
    if (r != ERROR_SUCCESS)
        goto done;

    r = MSI_ViewExecute(dbview, NULL);
    if (r != ERROR_SUCCESS)
        goto done;

    r = MSI_ViewFetch(dbview, &row);
    if (r == ERROR_SUCCESS && !MSI_RecordsAreEqual(rec, row))
    {
        table->numconflicts++;
        goto done;
    }
    else if (r != ERROR_NO_MORE_ITEMS)
        goto done;

    mergerow = msi_alloc(sizeof(MERGEROW));
    if (!mergerow)
    {
        r = ERROR_OUTOFMEMORY;
        goto done;
    }

    mergerow->data = MSI_CloneRecord(rec);
    if (!mergerow->data)
    {
        r = ERROR_OUTOFMEMORY;
        msi_free(mergerow);
        goto done;
    }

    list_add_tail(&table->rows, &mergerow->entry);

done:
    msi_free(query);
    msiobj_release(&row->hdr);
    msiobj_release(&dbview->hdr);
    return r;
}

static UINT merge_diff_tables(MSIRECORD *rec, LPVOID param)
{
    MERGEDATA *data = (MERGEDATA *)param;
    MERGETABLE *table;
    MSIQUERY *dbview;
    MSIQUERY *mergeview = NULL;
    LPCWSTR name;
    UINT r;

    static const WCHAR query[] = {'S','E','L','E','C','T',' ','*',' ',
        'F','R','O','M',' ','`','%','s','`',0};

    name = MSI_RecordGetString(rec, 1);

    r = MSI_OpenQuery(data->db, &dbview, query, name);
    if (r != ERROR_SUCCESS)
        return r;

    r = MSI_OpenQuery(data->merge, &mergeview, query, name);
    if (r != ERROR_SUCCESS)
        goto done;

    r = merge_verify_colnames(dbview, mergeview);
    if (r != ERROR_SUCCESS)
        goto done;

    r = merge_verify_primary_keys(data->db, data->merge, name);
    if (r != ERROR_SUCCESS)
        goto done;

    table = msi_alloc(sizeof(MERGETABLE));
    if (!table)
    {
        r = ERROR_OUTOFMEMORY;
        goto done;
    }

    list_init(&table->rows);
    table->name = strdupW(name);
    table->numconflicts = 0;
    data->curtable = table;
    data->curview = mergeview;

    r = MSI_IterateRecords(mergeview, NULL, merge_diff_row, data);
    if (r != ERROR_SUCCESS)
    {
        msi_free(table->name);
        msi_free(table);
        goto done;
    }

    list_add_tail(data->tabledata, &table->entry);

done:
    msiobj_release(&dbview->hdr);
    msiobj_release(&mergeview->hdr);
    return r;
}

static UINT gather_merge_data(MSIDATABASE *db, MSIDATABASE *merge,
                              struct list *tabledata)
{
    UINT r;
    MSIQUERY *view;
    MERGEDATA data;

    static const WCHAR query[] = {'S','E','L','E','C','T',' ','*',' ',
        'F','R','O','M',' ','`','_','T','a','b','l','e','s','`',0};

    r = MSI_DatabaseOpenViewW(merge, query, &view);
    if (r != ERROR_SUCCESS)
        return r;

    data.db = db;
    data.merge = merge;
    data.tabledata = tabledata;
    r = MSI_IterateRecords(view, NULL, merge_diff_tables, &data);

    msiobj_release(&view->hdr);
    return r;
}

static UINT merge_table(MSIDATABASE *db, MERGETABLE *table)
{
    UINT r;
    MERGEROW *row;
    MSIVIEW *tv;

    LIST_FOR_EACH_ENTRY(row, &table->rows, MERGEROW, entry)
    {
        r = TABLE_CreateView(db, table->name, &tv);
        if (r != ERROR_SUCCESS)
            return r;

        r = tv->ops->insert_row(tv, row->data, FALSE);
        tv->ops->delete(tv);

        if (r != ERROR_SUCCESS)
            return r;
    }

    return ERROR_SUCCESS;
}

static UINT update_merge_errors(MSIDATABASE *db, LPCWSTR error,
                                LPWSTR table, DWORD numconflicts)
{
    UINT r;
    MSIQUERY *view;

    static const WCHAR create[] = {
        'C','R','E','A','T','E',' ','T','A','B','L','E',' ',
        '`','%','s','`',' ','(','`','T','a','b','l','e','`',' ',
        'C','H','A','R','(','2','5','5',')',' ','N','O','T',' ',
        'N','U','L','L',',',' ','`','N','u','m','R','o','w','M','e','r','g','e',
        'C','o','n','f','l','i','c','t','s','`',' ','S','H','O','R','T',' ',
        'N','O','T',' ','N','U','L','L',' ','P','R','I','M','A','R','Y',' ',
        'K','E','Y',' ','`','T','a','b','l','e','`',')',0};
    static const WCHAR insert[] = {
        'I','N','S','E','R','T',' ','I','N','T','O',' ',
        '`','%','s','`',' ','(','`','T','a','b','l','e','`',',',' ',
        '`','N','u','m','R','o','w','M','e','r','g','e',
        'C','o','n','f','l','i','c','t','s','`',')',' ','V','A','L','U','E','S',
        ' ','(','\'','%','s','\'',',',' ','%','d',')',0};

    if (!TABLE_Exists(db, error))
    {
        r = MSI_OpenQuery(db, &view, create, error);
        if (r != ERROR_SUCCESS)
            return r;

        r = MSI_ViewExecute(view, NULL);
        msiobj_release(&view->hdr);
        if (r != ERROR_SUCCESS)
            return r;
    }

    r = MSI_OpenQuery(db, &view, insert, error, table, numconflicts);
    if (r != ERROR_SUCCESS)
        return r;

    r = MSI_ViewExecute(view, NULL);
    msiobj_release(&view->hdr);
    return r;
}

static void merge_free_rows(MERGETABLE *table)
{
    struct list *item, *cursor;

    LIST_FOR_EACH_SAFE(item, cursor, &table->rows)
    {
        MERGEROW *row = LIST_ENTRY(item, MERGEROW, entry);

        list_remove(&row->entry);
        merge_free_rows(table);
        msiobj_release(&row->data->hdr);
        msi_free(row);
    }
}

UINT WINAPI MsiDatabaseMergeW(MSIHANDLE hDatabase, MSIHANDLE hDatabaseMerge,
                              LPCWSTR szTableName)
{
    struct list tabledata = LIST_INIT(tabledata);
    struct list *item, *cursor;
    MSIDATABASE *db, *merge;
    MERGETABLE *table;
    BOOL conflicts;
    UINT r;

    TRACE("(%ld, %ld, %s)\n", hDatabase, hDatabaseMerge,
          debugstr_w(szTableName));

    if (szTableName && !*szTableName)
        return ERROR_INVALID_TABLE;

    db = msihandle2msiinfo(hDatabase, MSIHANDLETYPE_DATABASE);
    merge = msihandle2msiinfo(hDatabaseMerge, MSIHANDLETYPE_DATABASE);
    if (!db || !merge)
    {
        r = ERROR_INVALID_HANDLE;
        goto done;
    }

    r = gather_merge_data(db, merge, &tabledata);
    if (r != ERROR_SUCCESS)
        goto done;

    conflicts = FALSE;
    LIST_FOR_EACH_ENTRY(table, &tabledata, MERGETABLE, entry)
    {
        if (table->numconflicts)
        {
            conflicts = TRUE;

            r = update_merge_errors(db, szTableName, table->name,
                                    table->numconflicts);
            if (r != ERROR_SUCCESS)
                break;
        }
        else
        {
            r = merge_table(db, table);
            if (r != ERROR_SUCCESS)
                break;
        }
    }

    LIST_FOR_EACH_SAFE(item, cursor, &tabledata)
    {
        MERGETABLE *table = LIST_ENTRY(item, MERGETABLE, entry);

        list_remove(&table->entry);
        merge_free_rows(table);
        msi_free(table->name);
        msi_free(table);
    }

    if (conflicts)
        r = ERROR_FUNCTION_FAILED;

done:
    msiobj_release(&db->hdr);
    msiobj_release(&merge->hdr);
    return r;
}

MSIDBSTATE WINAPI MsiGetDatabaseState( MSIHANDLE handle )
{
    MSIDBSTATE ret = MSIDBSTATE_READ;
    MSIDATABASE *db;

    TRACE("%ld\n", handle);

    db = msihandle2msiinfo( handle, MSIHANDLETYPE_DATABASE );
    if( !db )
    {
        IWineMsiRemoteDatabase *remote_database;

        remote_database = (IWineMsiRemoteDatabase *)msi_get_remote( handle );
        if ( !remote_database )
            return MSIDBSTATE_ERROR;

        IWineMsiRemoteDatabase_Release( remote_database );
        WARN("MsiGetDatabaseState not allowed during a custom action!\n");

        return MSIDBSTATE_READ;
    }

    if (db->mode != MSIDBOPEN_READONLY )
        ret = MSIDBSTATE_WRITE;
    msiobj_release( &db->hdr );

    return ret;
}

typedef struct _msi_remote_database_impl {
    const IWineMsiRemoteDatabaseVtbl *lpVtbl;
    MSIHANDLE database;
    LONG refs;
} msi_remote_database_impl;

static inline msi_remote_database_impl* mrd_from_IWineMsiRemoteDatabase( IWineMsiRemoteDatabase* iface )
{
    return (msi_remote_database_impl *)iface;
}

static HRESULT WINAPI mrd_QueryInterface( IWineMsiRemoteDatabase *iface,
                                          REFIID riid,LPVOID *ppobj)
{
    if( IsEqualCLSID( riid, &IID_IUnknown ) ||
        IsEqualCLSID( riid, &IID_IWineMsiRemoteDatabase ) )
    {
        IUnknown_AddRef( iface );
        *ppobj = iface;
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG WINAPI mrd_AddRef( IWineMsiRemoteDatabase *iface )
{
    msi_remote_database_impl* This = mrd_from_IWineMsiRemoteDatabase( iface );

    return InterlockedIncrement( &This->refs );
}

static ULONG WINAPI mrd_Release( IWineMsiRemoteDatabase *iface )
{
    msi_remote_database_impl* This = mrd_from_IWineMsiRemoteDatabase( iface );
    ULONG r;

    r = InterlockedDecrement( &This->refs );
    if (r == 0)
    {
        MsiCloseHandle( This->database );
        msi_free( This );
    }
    return r;
}

static HRESULT WINAPI mrd_IsTablePersistent( IWineMsiRemoteDatabase *iface,
                                             BSTR table, MSICONDITION *persistent )
{
    msi_remote_database_impl *This = mrd_from_IWineMsiRemoteDatabase( iface );
    *persistent = MsiDatabaseIsTablePersistentW(This->database, (LPWSTR)table);
    return S_OK;
}

static HRESULT WINAPI mrd_GetPrimaryKeys( IWineMsiRemoteDatabase *iface,
                                          BSTR table, MSIHANDLE *keys )
{
    msi_remote_database_impl *This = mrd_from_IWineMsiRemoteDatabase( iface );
    UINT r = MsiDatabaseGetPrimaryKeysW(This->database, (LPWSTR)table, keys);
    return HRESULT_FROM_WIN32(r);
}

static HRESULT WINAPI mrd_GetSummaryInformation( IWineMsiRemoteDatabase *iface,
                                                UINT updatecount, MSIHANDLE *suminfo )
{
    msi_remote_database_impl *This = mrd_from_IWineMsiRemoteDatabase( iface );
    UINT r = MsiGetSummaryInformationW(This->database, NULL, updatecount, suminfo);
    return HRESULT_FROM_WIN32(r);
}

static HRESULT WINAPI mrd_OpenView( IWineMsiRemoteDatabase *iface,
                                    BSTR query, MSIHANDLE *view )
{
    msi_remote_database_impl *This = mrd_from_IWineMsiRemoteDatabase( iface );
    UINT r = MsiDatabaseOpenViewW(This->database, (LPWSTR)query, view);
    return HRESULT_FROM_WIN32(r);
}

static HRESULT WINAPI mrd_SetMsiHandle( IWineMsiRemoteDatabase *iface, MSIHANDLE handle )
{
    msi_remote_database_impl* This = mrd_from_IWineMsiRemoteDatabase( iface );
    This->database = handle;
    return S_OK;
}

static const IWineMsiRemoteDatabaseVtbl msi_remote_database_vtbl =
{
    mrd_QueryInterface,
    mrd_AddRef,
    mrd_Release,
    mrd_IsTablePersistent,
    mrd_GetPrimaryKeys,
    mrd_GetSummaryInformation,
    mrd_OpenView,
    mrd_SetMsiHandle,
};

HRESULT create_msi_remote_database( IUnknown *pOuter, LPVOID *ppObj )
{
    msi_remote_database_impl *This;

    This = msi_alloc( sizeof *This );
    if (!This)
        return E_OUTOFMEMORY;

    This->lpVtbl = &msi_remote_database_vtbl;
    This->database = 0;
    This->refs = 1;

    *ppObj = This;

    return S_OK;
}
