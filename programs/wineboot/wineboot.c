/*
 * Copyright (C) 2002 Andreas Mohr
 * Copyright (C) 2002 Shachar Shemesh
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
/* Wine "bootup" handler application
 *
 * This app handles the various "hooks" windows allows for applications to perform
 * as part of the bootstrap process. These are roughly divided into three types.
 * Knowledge base articles that explain this are 137367, 179365, 232487 and 232509.
 * Also, 119941 has some info on grpconv.exe
 * The operations performed are (by order of execution):
 *
 * Preboot (prior to fully loading the Windows kernel):
 * - wininit.exe (rename operations left in wininit.ini - Win 9x only)
 * - PendingRenameOperations (rename operations left in the registry - Win NT+ only)
 *
 * Startup (before the user logs in)
 * - Services (NT)
 * - HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\RunServicesOnce (9x, asynch)
 * - HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\RunServices (9x, asynch)
 * 
 * After log in
 * - HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\RunOnce (all, synch)
 * - HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\Run (all, asynch)
 * - HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run (all, asynch)
 * - Startup folders (all, ?asynch?)
 * - HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\RunOnce (all, asynch)
 *
 * Somewhere in there is processing the RunOnceEx entries (also no imp)
 * 
 * Bugs:
 * - If a pending rename registry does not start with \??\ the entry is
 *   processed anyways. I'm not sure that is the Windows behaviour.
 * - Need to check what is the windows behaviour when trying to delete files
 *   and directories that are read-only
 * - In the pending rename registry processing - there are no traces of the files
 *   processed (requires translations from Unicode to Ansi).
 */

#include "config.h"
#include "wine/port.h"

#define WIN32_LEAN_AND_MEAN

#include <stdio.h>
#ifdef HAVE_GETOPT_H
# include <getopt.h>
#endif
#include <windows.h>
#include <wine/unicode.h>
#include <wine/debug.h>

#define COBJMACROS
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <shellapi.h>

WINE_DEFAULT_DEBUG_CHANNEL(wineboot);

#define MAX_LINE_LENGTH (2*MAX_PATH+2)

extern BOOL shutdown_close_windows( BOOL force );
extern void kill_processes( BOOL kill_desktop );

static WCHAR windowsdir[MAX_PATH];

/* Performs the rename operations dictated in %SystemRoot%\Wininit.ini.
 * Returns FALSE if there was an error, or otherwise if all is ok.
 */
static BOOL wininit(void)
{
    static const WCHAR nulW[] = {'N','U','L',0};
    static const WCHAR renameW[] = {'r','e','n','a','m','e',0};
    static const WCHAR wininitW[] = {'w','i','n','i','n','i','t','.','i','n','i',0};
    static const WCHAR wininitbakW[] = {'w','i','n','i','n','i','t','.','b','a','k',0};
    WCHAR initial_buffer[1024];
    WCHAR *str, *buffer = initial_buffer;
    DWORD size = sizeof(initial_buffer)/sizeof(WCHAR);
    DWORD res;

    for (;;)
    {
        if (!(res = GetPrivateProfileSectionW( renameW, buffer, size, wininitW ))) return TRUE;
        if (res < size - 2) break;
        if (buffer != initial_buffer) HeapFree( GetProcessHeap(), 0, buffer );
        size *= 2;
        if (!(buffer = HeapAlloc( GetProcessHeap(), 0, size * sizeof(WCHAR) ))) return FALSE;
    }

    for (str = buffer; *str; str += strlenW(str) + 1)
    {
        WCHAR *value;

        if (*str == ';') continue;  /* comment */
        if (!(value = strchrW( str, '=' ))) continue;

        /* split the line into key and value */
        *value++ = 0;

        if (!lstrcmpiW( nulW, str ))
        {
            WINE_TRACE("Deleting file %s\n", wine_dbgstr_w(value) );
            if( !DeleteFileW( value ) )
                WINE_WARN("Error deleting file %s\n", wine_dbgstr_w(value) );
        }
        else
        {
            WINE_TRACE("Renaming file %s to %s\n", wine_dbgstr_w(value), wine_dbgstr_w(str) );

            if( !MoveFileExW(value, str, MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING) )
                WINE_WARN("Error renaming %s to %s\n", wine_dbgstr_w(value), wine_dbgstr_w(str) );
        }
        str = value;
    }

    if (buffer != initial_buffer) HeapFree( GetProcessHeap(), 0, buffer );

    if( !MoveFileExW( wininitW, wininitbakW, MOVEFILE_REPLACE_EXISTING) )
    {
        WINE_ERR("Couldn't rename wininit.ini, error %d\n", GetLastError() );

        return FALSE;
    }

    return TRUE;
}

static BOOL pendingRename(void)
{
    static const WCHAR ValueName[] = {'P','e','n','d','i','n','g',
                                      'F','i','l','e','R','e','n','a','m','e',
                                      'O','p','e','r','a','t','i','o','n','s',0};
    static const WCHAR SessionW[] = { 'S','y','s','t','e','m','\\',
                                     'C','u','r','r','e','n','t','C','o','n','t','r','o','l','S','e','t','\\',
                                     'C','o','n','t','r','o','l','\\',
                                     'S','e','s','s','i','o','n',' ','M','a','n','a','g','e','r',0};
    WCHAR *buffer=NULL;
    const WCHAR *src=NULL, *dst=NULL;
    DWORD dataLength=0;
    HKEY hSession=NULL;
    DWORD res;

    WINE_TRACE("Entered\n");

    if( (res=RegOpenKeyExW( HKEY_LOCAL_MACHINE, SessionW, 0, KEY_ALL_ACCESS, &hSession ))
            !=ERROR_SUCCESS )
    {
        if( res==ERROR_FILE_NOT_FOUND )
        {
            WINE_TRACE("The key was not found - skipping\n");
            res=TRUE;
        }
        else
        {
            WINE_ERR("Couldn't open key, error %d\n", res );
            res=FALSE;
        }

        goto end;
    }

    res=RegQueryValueExW( hSession, ValueName, NULL, NULL /* The value type does not really interest us, as it is not
                                                             truly a REG_MULTI_SZ anyways */,
            NULL, &dataLength );
    if( res==ERROR_FILE_NOT_FOUND )
    {
        /* No value - nothing to do. Great! */
        WINE_TRACE("Value not present - nothing to rename\n");
        res=TRUE;
        goto end;
    }

    if( res!=ERROR_SUCCESS )
    {
        WINE_ERR("Couldn't query value's length (%d)\n", res );
        res=FALSE;
        goto end;
    }

    buffer=HeapAlloc( GetProcessHeap(),0,dataLength );
    if( buffer==NULL )
    {
        WINE_ERR("Couldn't allocate %u bytes for the value\n", dataLength );
        res=FALSE;
        goto end;
    }

    res=RegQueryValueExW( hSession, ValueName, NULL, NULL, (LPBYTE)buffer, &dataLength );
    if( res!=ERROR_SUCCESS )
    {
        WINE_ERR("Couldn't query value after successfully querying before (%u),\n"
                "please report to wine-devel@winehq.org\n", res);
        res=FALSE;
        goto end;
    }

    /* Make sure that the data is long enough and ends with two NULLs. This
     * simplifies the code later on.
     */
    if( dataLength<2*sizeof(buffer[0]) ||
            buffer[dataLength/sizeof(buffer[0])-1]!='\0' ||
            buffer[dataLength/sizeof(buffer[0])-2]!='\0' )
    {
        WINE_ERR("Improper value format - doesn't end with NULL\n");
        res=FALSE;
        goto end;
    }

    for( src=buffer; (src-buffer)*sizeof(src[0])<dataLength && *src!='\0';
            src=dst+lstrlenW(dst)+1 )
    {
        DWORD dwFlags=0;

        WINE_TRACE("processing next command\n");

        dst=src+lstrlenW(src)+1;

        /* We need to skip the \??\ header */
        if( src[0]=='\\' && src[1]=='?' && src[2]=='?' && src[3]=='\\' )
            src+=4;

        if( dst[0]=='!' )
        {
            dwFlags|=MOVEFILE_REPLACE_EXISTING;
            dst++;
        }

        if( dst[0]=='\\' && dst[1]=='?' && dst[2]=='?' && dst[3]=='\\' )
            dst+=4;

        if( *dst!='\0' )
        {
            /* Rename the file */
            MoveFileExW( src, dst, dwFlags );
        } else
        {
            /* Delete the file or directory */
            if( (res=GetFileAttributesW(src))!=INVALID_FILE_ATTRIBUTES )
            {
                if( (res&FILE_ATTRIBUTE_DIRECTORY)==0 )
                {
                    /* It's a file */
                    DeleteFileW(src);
                } else
                {
                    /* It's a directory */
                    RemoveDirectoryW(src);
                }
            } else
            {
                WINE_ERR("couldn't get file attributes (%d)\n", GetLastError() );
            }
        }
    }

    if((res=RegDeleteValueW(hSession, ValueName))!=ERROR_SUCCESS )
    {
        WINE_ERR("Error deleting the value (%u)\n", GetLastError() );
        res=FALSE;
    } else
        res=TRUE;
    
end:
    HeapFree(GetProcessHeap(), 0, buffer);

    if( hSession!=NULL )
        RegCloseKey( hSession );

    return res;
}

enum runkeys {
    RUNKEY_RUN, RUNKEY_RUNONCE, RUNKEY_RUNSERVICES, RUNKEY_RUNSERVICESONCE
};

const WCHAR runkeys_names[][30]=
{
    {'R','u','n',0},
    {'R','u','n','O','n','c','e',0},
    {'R','u','n','S','e','r','v','i','c','e','s',0},
    {'R','u','n','S','e','r','v','i','c','e','s','O','n','c','e',0}
};

#define INVALID_RUNCMD_RETURN -1
/*
 * This function runs the specified command in the specified dir.
 * [in,out] cmdline - the command line to run. The function may change the passed buffer.
 * [in] dir - the dir to run the command in. If it is NULL, then the current dir is used.
 * [in] wait - whether to wait for the run program to finish before returning.
 * [in] minimized - Whether to ask the program to run minimized.
 *
 * Returns:
 * If running the process failed, returns INVALID_RUNCMD_RETURN. Use GetLastError to get the error code.
 * If wait is FALSE - returns 0 if successful.
 * If wait is TRUE - returns the program's return value.
 */
static DWORD runCmd(LPWSTR cmdline, LPCWSTR dir, BOOL wait, BOOL minimized)
{
    STARTUPINFOW si;
    PROCESS_INFORMATION info;
    DWORD exit_code=0;

    memset(&si, 0, sizeof(si));
    si.cb=sizeof(si);
    if( minimized )
    {
        si.dwFlags=STARTF_USESHOWWINDOW;
        si.wShowWindow=SW_MINIMIZE;
    }
    memset(&info, 0, sizeof(info));

    if( !CreateProcessW(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, dir, &si, &info) )
    {
        WINE_ERR("Failed to run command %s (%d)\n", wine_dbgstr_w(cmdline),
                 GetLastError() );

        return INVALID_RUNCMD_RETURN;
    }

    WINE_TRACE("Successfully ran command %s - Created process handle %p\n",
               wine_dbgstr_w(cmdline), info.hProcess );

    if(wait)
    {   /* wait for the process to exit */
        WaitForSingleObject(info.hProcess, INFINITE);
        GetExitCodeProcess(info.hProcess, &exit_code);
    }

    CloseHandle( info.hProcess );

    return exit_code;
}

/*
 * Process a "Run" type registry key.
 * hkRoot is the HKEY from which "Software\Microsoft\Windows\CurrentVersion" is
 *      opened.
 * szKeyName is the key holding the actual entries.
 * bDelete tells whether we should delete each value right before executing it.
 * bSynchronous tells whether we should wait for the prog to complete before
 *      going on to the next prog.
 */
static BOOL ProcessRunKeys( HKEY hkRoot, LPCWSTR szKeyName, BOOL bDelete,
        BOOL bSynchronous )
{
    static const WCHAR WINKEY_NAME[]={'S','o','f','t','w','a','r','e','\\',
        'M','i','c','r','o','s','o','f','t','\\','W','i','n','d','o','w','s','\\',
        'C','u','r','r','e','n','t','V','e','r','s','i','o','n',0};
    HKEY hkWin=NULL, hkRun=NULL;
    DWORD res=ERROR_SUCCESS;
    DWORD i, nMaxCmdLine=0, nMaxValue=0;
    WCHAR *szCmdLine=NULL;
    WCHAR *szValue=NULL;

    if (hkRoot==HKEY_LOCAL_MACHINE)
        WINE_TRACE("processing %s entries under HKLM\n",wine_dbgstr_w(szKeyName) );
    else
        WINE_TRACE("processing %s entries under HKCU\n",wine_dbgstr_w(szKeyName) );

    if( (res=RegOpenKeyExW( hkRoot, WINKEY_NAME, 0, KEY_READ, &hkWin ))!=ERROR_SUCCESS )
    {
        WINE_ERR("RegOpenKey failed on Software\\Microsoft\\Windows\\CurrentVersion (%d)\n",
                res);

        goto end;
    }

    if( (res=RegOpenKeyExW( hkWin, szKeyName, 0, bDelete?KEY_ALL_ACCESS:KEY_READ, &hkRun ))!=
            ERROR_SUCCESS)
    {
        if( res==ERROR_FILE_NOT_FOUND )
        {
            WINE_TRACE("Key doesn't exist - nothing to be done\n");

            res=ERROR_SUCCESS;
        }
        else
            WINE_ERR("RegOpenKey failed on run key (%d)\n", res);

        goto end;
    }
    
    if( (res=RegQueryInfoKeyW( hkRun, NULL, NULL, NULL, NULL, NULL, NULL, &i, &nMaxValue,
                    &nMaxCmdLine, NULL, NULL ))!=ERROR_SUCCESS )
    {
        WINE_ERR("Couldn't query key info (%d)\n", res );

        goto end;
    }

    if( i==0 )
    {
        WINE_TRACE("No commands to execute.\n");

        res=ERROR_SUCCESS;
        goto end;
    }
    
    if( (szCmdLine=HeapAlloc(GetProcessHeap(),0,nMaxCmdLine))==NULL )
    {
        WINE_ERR("Couldn't allocate memory for the commands to be executed\n");

        res=ERROR_NOT_ENOUGH_MEMORY;
        goto end;
    }

    if( (szValue=HeapAlloc(GetProcessHeap(),0,(++nMaxValue)*sizeof(*szValue)))==NULL )
    {
        WINE_ERR("Couldn't allocate memory for the value names\n");

        res=ERROR_NOT_ENOUGH_MEMORY;
        goto end;
    }
    
    while( i>0 )
    {
        DWORD nValLength=nMaxValue, nDataLength=nMaxCmdLine;
        DWORD type;

        --i;

        if( (res=RegEnumValueW( hkRun, i, szValue, &nValLength, 0, &type,
                        (LPBYTE)szCmdLine, &nDataLength ))!=ERROR_SUCCESS )
        {
            WINE_ERR("Couldn't read in value %d - %d\n", i, res );

            continue;
        }

        if( bDelete && (res=RegDeleteValueW( hkRun, szValue ))!=ERROR_SUCCESS )
        {
            WINE_ERR("Couldn't delete value - %d, %d. Running command anyways.\n", i, res );
        }
        
        if( type!=REG_SZ )
        {
            WINE_ERR("Incorrect type of value #%d (%d)\n", i, type );

            continue;
        }

        if( (res=runCmd(szCmdLine, NULL, bSynchronous, FALSE ))==INVALID_RUNCMD_RETURN )
        {
            WINE_ERR("Error running cmd #%d (%d)\n", i, GetLastError() );
        }

        WINE_TRACE("Done processing cmd #%d\n", i);
    }

    res=ERROR_SUCCESS;

end:
    HeapFree( GetProcessHeap(), 0, szValue );
    HeapFree( GetProcessHeap(), 0, szCmdLine );

    if( hkRun!=NULL )
        RegCloseKey( hkRun );
    if( hkWin!=NULL )
        RegCloseKey( hkWin );

    WINE_TRACE("done\n");

    return res==ERROR_SUCCESS?TRUE:FALSE;
}

/*
 * WFP is Windows File Protection, in NT5 and Windows 2000 it maintains a cache
 * of known good dlls and scans through and replaces corrupted DLLs with these
 * known good versions. The only programs that should install into this dll
 * cache are Windows Updates and IE (which is treated like a Windows Update)
 *
 * Implementing this allows installing ie in win2k mode to actually install the
 * system dlls that we expect and need
 */
static int ProcessWindowsFileProtection(void)
{
    static const WCHAR winlogonW[] = {'S','o','f','t','w','a','r','e','\\',
                                      'M','i','c','r','o','s','o','f','t','\\',
                                      'W','i','n','d','o','w','s',' ','N','T','\\',
                                      'C','u','r','r','e','n','t','V','e','r','s','i','o','n','\\',
                                      'W','i','n','l','o','g','o','n',0};
    static const WCHAR cachedirW[] = {'S','F','C','D','l','l','C','a','c','h','e','D','i','r',0};
    static const WCHAR dllcacheW[] = {'\\','d','l','l','c','a','c','h','e','\\','*',0};
    static const WCHAR wildcardW[] = {'\\','*',0};
    WIN32_FIND_DATAW finddata;
    HANDLE find_handle;
    BOOL find_rc;
    DWORD rc;
    HKEY hkey;
    LPWSTR dllcache = NULL;

    if (!RegOpenKeyW( HKEY_LOCAL_MACHINE, winlogonW, &hkey ))
    {
        DWORD sz = 0;
        if (!RegQueryValueExW( hkey, cachedirW, 0, NULL, NULL, &sz))
        {
            sz += sizeof(WCHAR);
            dllcache = HeapAlloc(GetProcessHeap(),0,sz + sizeof(wildcardW));
            RegQueryValueExW( hkey, cachedirW, 0, NULL, (LPBYTE)dllcache, &sz);
            strcatW( dllcache, wildcardW );
        }
    }
    RegCloseKey(hkey);

    if (!dllcache)
    {
        DWORD sz = GetSystemDirectoryW( NULL, 0 );
        dllcache = HeapAlloc( GetProcessHeap(), 0, sz * sizeof(WCHAR) + sizeof(dllcacheW));
        GetSystemDirectoryW( dllcache, sz );
        strcatW( dllcache, dllcacheW );
    }

    find_handle = FindFirstFileW(dllcache,&finddata);
    find_rc = find_handle != INVALID_HANDLE_VALUE;
    while (find_rc)
    {
        static const WCHAR dotW[] = {'.',0};
        static const WCHAR dotdotW[] = {'.','.',0};
        WCHAR targetpath[MAX_PATH];
        WCHAR currentpath[MAX_PATH];
        UINT sz;
        UINT sz2;
        WCHAR tempfile[MAX_PATH];

        if (strcmpW(finddata.cFileName,dotW) == 0 || strcmpW(finddata.cFileName,dotdotW) == 0)
        {
            find_rc = FindNextFileW(find_handle,&finddata);
            continue;
        }

        sz = MAX_PATH;
        sz2 = MAX_PATH;
        VerFindFileW(VFFF_ISSHAREDFILE, finddata.cFileName, windowsdir,
                     windowsdir, currentpath, &sz, targetpath, &sz2);
        sz = MAX_PATH;
        rc = VerInstallFileW(0, finddata.cFileName, finddata.cFileName,
                             dllcache, targetpath, currentpath, tempfile, &sz);
        if (rc != ERROR_SUCCESS)
        {
            WINE_ERR("WFP: %s error 0x%x\n",wine_dbgstr_w(finddata.cFileName),rc);
            DeleteFileW(tempfile);
        }
        find_rc = FindNextFileW(find_handle,&finddata);
    }
    FindClose(find_handle);
    HeapFree(GetProcessHeap(),0,dllcache);
    return 1;
}

/* start services */
static void start_services(void)
{
    static const WCHAR servicesW[] = {'S','y','s','t','e','m','\\',
                                      'C','u','r','r','e','n','t','C','o','n','t','r','o','l','S','e','t','\\',
                                      'S','e','r','v','i','c','e','s',0};
    static const WCHAR startW[] = {'S','t','a','r','t',0};
    HKEY hkey, skey;
    DWORD type, size, start, index = 0;
    WCHAR name[MAX_PATH];
    SC_HANDLE manager;

    if (RegOpenKeyW( HKEY_LOCAL_MACHINE, servicesW, &hkey )) return;

    if (!(manager = OpenSCManagerW( NULL, NULL, SC_MANAGER_ALL_ACCESS )))
    {
        RegCloseKey( hkey );
        return;
    }

    while (!RegEnumKeyW( hkey, index++, name, sizeof(name) ))
    {
        if (RegOpenKeyW( hkey, name, &skey )) continue;
        size = sizeof(start);
        if (!RegQueryValueExW( skey, startW, NULL, &type, (LPBYTE)&start, &size ) && type == REG_DWORD)
        {
            if (start == SERVICE_BOOT_START ||
                start == SERVICE_SYSTEM_START ||
                start == SERVICE_AUTO_START)
            {
                SC_HANDLE handle = OpenServiceW( manager, name, SERVICE_ALL_ACCESS );
                if (handle)
                {
                    WINE_TRACE( "starting service %s start %u\n", wine_dbgstr_w(name), start );
                    StartServiceW( handle, 0, NULL );
                    CloseServiceHandle( handle );
                }
            }
        }
        RegCloseKey( skey );
    }
    CloseServiceHandle( manager );
    RegCloseKey( hkey );
}

/* Process items in the StartUp group of the user's Programs under the Start Menu. Some installers put
 * shell links here to restart themselves after boot. */
static BOOL ProcessStartupItems(void)
{
    BOOL ret = FALSE;
    HRESULT hr;
    int iRet;
    IMalloc *ppM = NULL;
    IShellFolder *psfDesktop = NULL, *psfStartup = NULL;
    LPITEMIDLIST pidlStartup = NULL, pidlItem;
    ULONG NumPIDLs;
    IEnumIDList *iEnumList = NULL;
    STRRET strret;
    WCHAR wszCommand[MAX_PATH];

    WINE_TRACE("Processing items in the StartUp folder.\n");

    hr = SHGetMalloc(&ppM);
    if (FAILED(hr))
    {
	WINE_ERR("Couldn't get IMalloc object.\n");
	goto done;
    }

    hr = SHGetDesktopFolder(&psfDesktop);
    if (FAILED(hr))
    {
	WINE_ERR("Couldn't get desktop folder.\n");
	goto done;
    }

    hr = SHGetSpecialFolderLocation(NULL, CSIDL_STARTUP, &pidlStartup);
    if (FAILED(hr))
    {
	WINE_TRACE("Couldn't get StartUp folder location.\n");
	goto done;
    }

    hr = IShellFolder_BindToObject(psfDesktop, pidlStartup, NULL, &IID_IShellFolder, (LPVOID*)&psfStartup);
    if (FAILED(hr))
    {
	WINE_TRACE("Couldn't bind IShellFolder to StartUp folder.\n");
	goto done;
    }

    hr = IShellFolder_EnumObjects(psfStartup, NULL, SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN, &iEnumList);
    if (FAILED(hr))
    {
	WINE_TRACE("Unable to enumerate StartUp objects.\n");
	goto done;
    }

    while (IEnumIDList_Next(iEnumList, 1, &pidlItem, &NumPIDLs) == S_OK &&
	   (NumPIDLs) == 1)
    {
	hr = IShellFolder_GetDisplayNameOf(psfStartup, pidlItem, SHGDN_FORPARSING, &strret);
	if (FAILED(hr))
	    WINE_TRACE("Unable to get display name of enumeration item.\n");
	else
	{
	    hr = StrRetToBufW(&strret, pidlItem, wszCommand, MAX_PATH);
	    if (FAILED(hr))
		WINE_TRACE("Unable to parse display name.\n");
	    else
		if ((iRet = (int)ShellExecuteW(NULL, NULL, wszCommand, NULL, NULL, SW_SHOWNORMAL)) <= 32)
		    WINE_ERR("Error %d executing command %s.\n", iRet, wine_dbgstr_w(wszCommand));
	}

	IMalloc_Free(ppM, pidlItem);
    }

    /* Return success */
    ret = TRUE;

done:
    if (iEnumList) IEnumIDList_Release(iEnumList);
    if (psfStartup) IShellFolder_Release(psfStartup);
    if (pidlStartup) IMalloc_Free(ppM, pidlStartup);

    return ret;
}

static void usage(void)
{
    WINE_MESSAGE( "Usage: wineboot [options]\n" );
    WINE_MESSAGE( "Options;\n" );
    WINE_MESSAGE( "    -h,--help         Display this help message\n" );
    WINE_MESSAGE( "    -e,--end-session  End the current session cleanly\n" );
    WINE_MESSAGE( "    -f,--force        Force exit for processes that don't exit cleanly\n" );
    WINE_MESSAGE( "    -k,--kill         Kill running processes without any cleanup\n" );
    WINE_MESSAGE( "    -r,--restart      Restart only, don't do normal startup operations\n" );
    WINE_MESSAGE( "    -s,--shutdown     Shutdown only, don't reboot\n" );
}

static const char short_options[] = "efhkrs";

static const struct option long_options[] =
{
    { "help",        0, 0, 'h' },
    { "end-session", 0, 0, 'e' },
    { "force",       0, 0, 'f' },
    { "kill",        0, 0, 'k' },
    { "restart",     0, 0, 'r' },
    { "shutdown",    0, 0, 's' },
    { NULL,          0, 0, 0 }
};

int main( int argc, char *argv[] )
{
    /* First, set the current directory to SystemRoot */
    int optc;
    int end_session = 0, force = 0, kill = 0, restart = 0, shutdown = 0;

    GetWindowsDirectoryW( windowsdir, MAX_PATH );
    if( !SetCurrentDirectoryW( windowsdir ) )
    {
        WINE_ERR("Cannot set the dir to %s (%d)\n", wine_dbgstr_w(windowsdir), GetLastError() );
        return 100;
    }

    while ((optc = getopt_long(argc, argv, short_options, long_options, NULL )) != -1)
    {
        switch(optc)
        {
        case 'e': end_session = 1; break;
        case 'f': force = 1; break;
        case 'k': kill = 1; break;
        case 'r': restart = 1; break;
        case 's': shutdown = 1; break;
        case 'h': usage(); return 0;
        case '?': usage(); return 1;
        }
    }

    if (end_session)
    {
        if (!shutdown_close_windows( force )) return 1;
    }

    if (end_session || kill) kill_processes( shutdown );

    if (shutdown) return 0;

    wininit();
    pendingRename();

    ProcessWindowsFileProtection();
    ProcessRunKeys( HKEY_LOCAL_MACHINE, runkeys_names[RUNKEY_RUNSERVICESONCE], TRUE, FALSE );
    if (!restart)
    {
        ProcessRunKeys( HKEY_LOCAL_MACHINE, runkeys_names[RUNKEY_RUNSERVICES], FALSE, FALSE );
        start_services();
    }
    ProcessRunKeys( HKEY_LOCAL_MACHINE, runkeys_names[RUNKEY_RUNONCE], TRUE, TRUE );
    if (!restart)
    {
        ProcessRunKeys( HKEY_LOCAL_MACHINE, runkeys_names[RUNKEY_RUN], FALSE, FALSE );
        ProcessRunKeys( HKEY_CURRENT_USER, runkeys_names[RUNKEY_RUN], FALSE, FALSE );
        ProcessStartupItems();
    }

    WINE_TRACE("Operation done\n");
    return 0;
}
