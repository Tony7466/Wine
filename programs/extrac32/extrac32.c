/*
 * Extract - Wine-compatible program for extract *.cab files.
 *
 * Copyright 2007 Etersoft (Lyutin Anatoly)
 * Copyright 2009 Ilya Shpigor
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

#include <windows.h>
#include <shellapi.h>
#include <setupapi.h>

#include "wine/unicode.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(extrac32);

static UINT WINAPI ExtCabCallback(PVOID Context, UINT Notification, UINT_PTR Param1, UINT_PTR Param2)
{
    FILE_IN_CABINET_INFO_W *pInfo;
    FILEPATHS_W *pFilePaths;

    switch(Notification)
    {
        case SPFILENOTIFY_FILEINCABINET:
            pInfo = (FILE_IN_CABINET_INFO_W*)Param1;
            lstrcpyW(pInfo->FullTargetName, (LPCWSTR)Context);
            lstrcatW(pInfo->FullTargetName, pInfo->NameInCabinet);
            return FILEOP_DOIT;
        case SPFILENOTIFY_FILEEXTRACTED:
            pFilePaths = (FILEPATHS_W*)Param1;
            WINE_TRACE("Extracted %s\n", wine_dbgstr_w(pFilePaths->Target));
            return NO_ERROR;
    }
    return NO_ERROR;
}

static void extract(LPCWSTR cabfile, LPWSTR destdir)
{
    if (!SetupIterateCabinetW(cabfile, 0, ExtCabCallback, destdir))
        WINE_ERR("Could not extract cab file %s\n", wine_dbgstr_w(cabfile));
}

int PASCAL wWinMain(HINSTANCE hInstance, HINSTANCE prev, LPWSTR cmdline, int show)
{
    LPWSTR *argv;
    int argc;
    int i;
    WCHAR check, cmd = 0;
    WCHAR path[MAX_PATH];
    WCHAR backslash[] = {'\\',0};
    LPCWSTR cabfile = NULL;

    path[0] = 0;
    argv = CommandLineToArgvW(cmdline, &argc);

    if(!argv)
    {
        WINE_ERR("Bad command line arguments\n");
        return 0;
    }

    /* Parse arguments */
    for(i = 0; i < argc; i++)
    {
        /* Get cabfile */
        if ((argv[i][0] != '/') && !cabfile)
        {
            cabfile = argv[i];
            continue;
        }
        /* Get parameters for commands */
        check = toupperW( argv[i][1] );
        switch(check)
        {
            case 'A':
                WINE_FIXME("/A not implemented\n");
                break;
            case 'Y':
                WINE_FIXME("/Y not implemented\n");
                break;
            case 'L':
                if ((i + 1) >= argc) return 0;
                if (!GetFullPathNameW(argv[++i], MAX_PATH, path, NULL))
                    return 0;
                break;
            case 'C':
                if (cmd) return 0;
                if ((i + 2) >= argc) return 0;
                cmd = check;
                cabfile = argv[++i];
                if (!GetFullPathNameW(argv[++i], MAX_PATH, path, NULL))
                    return 0;
                break;
            case 'E':
            case 'D':
                if (cmd) return 0;
                cmd = check;
                break;
            default:
                return 0;
        }
    }

    if (!cabfile)
        return 0;

    if (!path[0])
        GetCurrentDirectoryW(MAX_PATH, path);

    lstrcatW(path, backslash);

    /* Execute the specified command */
    switch(cmd)
    {
        case 'C':
            /* Copy file */
            WINE_FIXME("/C not implemented\n");
            break;
        case 'E':
            /* Extract CAB archive */
            extract(cabfile, path);
            break;
        case 0:
        case 'D':
            /* Display CAB archive */
            WINE_FIXME("/D not implemented\n");
            break;
    }
    return 0;
}
