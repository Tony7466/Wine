/*
 * Wordpad implementation
 *
 * Copyright 2004 by Krzysztof Foltman
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

#define WIN32_LEAN_AND_MEAN
#define _WIN32_IE 0x0400

#define MAX_STRING_LEN 255

#include <stdarg.h>
#include <ctype.h>
#include <stdio.h>
#include <assert.h>

#include <windows.h>
#include <richedit.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>

#include "resource.h"

/* use LoadString */
static const WCHAR xszAppTitle[] = {'W','i','n','e',' ','W','o','r','d','p','a','d',0};
static const WCHAR xszMainMenu[] = {'M','A','I','N','M','E','N','U',0};

static const WCHAR wszRichEditClass[] = {'R','I','C','H','E','D','I','T','2','0','W',0};
static const WCHAR wszMainWndClass[] = {'W','O','R','D','P','A','D','T','O','P',0};
static const WCHAR wszAppTitle[] = {'W','i','n','e',' ','W','o','r','d','p','a','d',0};

static const WCHAR key_recentfiles[] = {'R','e','c','e','n','t',' ','f','i','l','e',
                                        ' ','l','i','s','t',0};
static const WCHAR key_options[] = {'O','p','t','i','o','n','s',0};

static const WCHAR var_file[] = {'F','i','l','e','%','d',0};
static const WCHAR var_framerect[] = {'F','r','a','m','e','R','e','c','t',0};


static HWND hMainWnd;
static HWND hEditorWnd;
static HWND hFindWnd;

static UINT ID_FINDMSGSTRING;

static WCHAR wszFilter[MAX_STRING_LEN*4+6*3+5];
static WCHAR wszDefaultFileName[MAX_STRING_LEN];
static WCHAR wszSaveChanges[MAX_STRING_LEN];

static LRESULT OnSize( HWND hWnd, WPARAM wParam, LPARAM lParam );

/* Load string resources */
static void DoLoadStrings(void)
{
    LPWSTR p = wszFilter;
    static const WCHAR files_rtf[] = {'*','.','r','t','f','\0'};
    static const WCHAR files_txt[] = {'*','.','t','x','t','\0'};
    static const WCHAR files_all[] = {'*','.','*','\0'};
    HINSTANCE hInstance = (HINSTANCE)GetWindowLongPtr(hMainWnd, GWLP_HINSTANCE);

    LoadStringW(hInstance, STRING_RICHTEXT_FILES_RTF, p, MAX_STRING_LEN);
    p += lstrlenW(p) + 1;
    lstrcpyW(p, files_rtf);
    p += lstrlenW(p) + 1;
    LoadStringW(hInstance, STRING_TEXT_FILES_TXT, p, MAX_STRING_LEN);
    p += lstrlenW(p) + 1;
    lstrcpyW(p, files_txt);
    p += lstrlenW(p) + 1;
    LoadStringW(hInstance, STRING_TEXT_FILES_UNICODE_TXT, p, MAX_STRING_LEN);
    p += lstrlenW(p) + 1;
    lstrcpyW(p, files_txt);
    p += lstrlenW(p) + 1;
    LoadStringW(hInstance, STRING_ALL_FILES, p, MAX_STRING_LEN);
    p += lstrlenW(p) + 1;
    lstrcpyW(p, files_all);
    p += lstrlenW(p) + 1;
    *p = '\0';

    p = wszDefaultFileName;
    LoadStringW(hInstance, STRING_DEFAULT_FILENAME, p, MAX_STRING_LEN);

    p = wszSaveChanges;
    LoadStringW(hInstance, STRING_PROMPT_SAVE_CHANGES, p, MAX_STRING_LEN);
}

static void AddButton(HWND hwndToolBar, int nImage, int nCommand)
{
    TBBUTTON button;

    ZeroMemory(&button, sizeof(button));
    button.iBitmap = nImage;
    button.idCommand = nCommand;
    button.fsState = TBSTATE_ENABLED;
    button.fsStyle = TBSTYLE_BUTTON;
    button.dwData = 0;
    button.iString = -1;
    SendMessageW(hwndToolBar, TB_ADDBUTTONSW, 1, (LPARAM)&button);
}

static void AddSeparator(HWND hwndToolBar)
{
    TBBUTTON button;

    ZeroMemory(&button, sizeof(button));
    button.iBitmap = -1;
    button.idCommand = 0;
    button.fsState = 0;
    button.fsStyle = TBSTYLE_SEP;
    button.dwData = 0;
    button.iString = -1;
    SendMessageW(hwndToolBar, TB_ADDBUTTONSW, 1, (LPARAM)&button);
}

static DWORD CALLBACK stream_in(DWORD_PTR cookie, LPBYTE buffer, LONG cb, LONG *pcb)
{
    HANDLE hFile = (HANDLE)cookie;
    DWORD read;

    if(!ReadFile(hFile, buffer, cb, &read, 0))
        return 1;

    *pcb = read;

    return 0;
}

static DWORD CALLBACK stream_out(DWORD_PTR cookie, LPBYTE buffer, LONG cb, LONG *pcb)
{
    DWORD written;
    int ret;
    HANDLE hFile = (HANDLE)cookie;

    ret = WriteFile(hFile, buffer, cb, &written, 0);

    if(!ret || (cb != written))
        return 1;

    *pcb = cb;

    return 0;
}


static LPWSTR file_basename(LPWSTR path)
{
    LPWSTR pos = path + lstrlenW(path);

    while(pos > path)
    {
        if(*pos == '\\' || *pos == '/')
        {
            pos++;
            break;
        }
        pos--;
    }
    return pos;
}

static WCHAR wszFileName[MAX_PATH];
static WPARAM fileFormat = SF_RTF;

static void set_caption(LPCWSTR wszNewFileName)
{
    static const WCHAR wszSeparator[] = {' ','-',' '};
    WCHAR *wszCaption;
    SIZE_T length = 0;

    if(!wszNewFileName)
        wszNewFileName = wszDefaultFileName;
    else
        wszNewFileName = file_basename((LPWSTR)wszNewFileName);

    wszCaption = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                lstrlenW(wszNewFileName)*sizeof(WCHAR)+sizeof(wszSeparator)+sizeof(wszAppTitle));

    if(!wszCaption)
        return;

    memcpy(wszCaption, wszNewFileName, lstrlenW(wszNewFileName)*sizeof(WCHAR));
    length += lstrlenW(wszNewFileName);
    memcpy(wszCaption + length, wszSeparator, sizeof(wszSeparator));
    length += sizeof(wszSeparator) / sizeof(WCHAR);
    memcpy(wszCaption + length, wszAppTitle, sizeof(wszAppTitle));

    SetWindowTextW(hMainWnd, wszCaption);

    HeapFree(GetProcessHeap(), 0, wszCaption);
}

static LRESULT registry_get_handle(HKEY *hKey, LPDWORD action, LPCWSTR subKey)
{
    LONG ret;
    static const WCHAR wszProgramKey[] = {'S','o','f','t','w','a','r','e','\\',
        'M','i','c','r','o','s','o','f','t','\\',
        'W','i','n','d','o','w','s','\\',
        'C','u','r','r','e','n','t','V','e','r','s','i','o','n','\\',
        'A','p','p','l','e','t','s','\\',
        'W','o','r','d','p','a','d',0};
    LPWSTR key = (LPWSTR)wszProgramKey;

    if(subKey)
    {
        WCHAR backslash[] = {'\\',0};
        key = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                        (lstrlenW(wszProgramKey)+lstrlenW(subKey)+lstrlenW(backslash)+1)
                        *sizeof(WCHAR));

        if(!key)
            return 1;

        lstrcpyW(key, wszProgramKey);
        lstrcatW(key, backslash);
        lstrcatW(key, subKey);
    }

    if(action)
    {
        ret = RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, NULL, REG_OPTION_NON_VOLATILE,
                              KEY_READ | KEY_WRITE, NULL, hKey, action);
    } else
    {
        ret = RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_READ | KEY_WRITE, hKey);
    }

    if(subKey)
        HeapFree(GetProcessHeap(), 0, key);

    return ret;
}

static void registry_set_winrect(void)
{
    HKEY hKey;
    DWORD action;

    if(registry_get_handle(&hKey, &action, (LPWSTR)key_options) == ERROR_SUCCESS)
    {
        RECT rc;

        GetWindowRect(hMainWnd, &rc);

        RegSetValueExW(hKey, var_framerect, 0, REG_BINARY, (LPBYTE)&rc, sizeof(RECT));
    }
}

static RECT registry_read_winrect(void)
{
    HKEY hKey;
    RECT rc;
    DWORD size = sizeof(RECT);

    ZeroMemory(&rc, sizeof(RECT));
    if(registry_get_handle(&hKey, 0, (LPWSTR)key_options) != ERROR_SUCCESS ||
       RegQueryValueExW(hKey, var_framerect, 0, NULL, (LPBYTE)&rc, &size) !=
       ERROR_SUCCESS || size != sizeof(RECT))
    {
        rc.top = 0;
        rc.left = 0;
        rc.bottom = 300;
        rc.right = 600;
    }

    RegCloseKey(hKey);
    return rc;
}

static void truncate_path(LPWSTR file, LPWSTR out, LPWSTR pos1, LPWSTR pos2)
{
    static const WCHAR dots[] = {'.','.','.',0};

    *++pos1 = 0;

    lstrcatW(out, file);
    lstrcatW(out, dots);
    lstrcatW(out, pos2);
}

static void format_filelist_filename(LPWSTR file, LPWSTR out)
{
    LPWSTR pos_basename;
    LPWSTR truncpos1, truncpos2;
    WCHAR myDocs[MAX_STRING_LEN];

    SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, (LPWSTR)&myDocs);
    pos_basename = file_basename(file);
    truncpos1 = NULL;
    truncpos2 = NULL;

    *(pos_basename-1) = 0;
    if(!lstrcmpiW(file, myDocs) || (lstrlenW(pos_basename) > FILELIST_ENTRY_LENGTH))
    {
        truncpos1 = pos_basename;
        *(pos_basename-1) = '\\';
    } else
    {
        LPWSTR pos;
        BOOL morespace = FALSE;

        *(pos_basename-1) = '\\';

        for(pos = file; pos < pos_basename; pos++)
        {
            if(*pos == '\\' || *pos == '/')
            {
                if(truncpos1)
                {
                    if((pos - file + lstrlenW(pos_basename)) > FILELIST_ENTRY_LENGTH)
                        break;

                    truncpos1 = pos;
                    morespace = TRUE;
                    break;
                }

                if((pos - file + lstrlenW(pos_basename)) > FILELIST_ENTRY_LENGTH)
                    break;

                truncpos1 = pos;
            }
        }

        if(morespace)
        {
            for(pos = pos_basename; pos >= truncpos1; pos--)
            {
                if(*pos == '\\' || *pos == '/')
                {
                    if((truncpos1 - file + lstrlenW(pos_basename) + pos_basename - pos) > FILELIST_ENTRY_LENGTH)
                        break;

                    truncpos2 = pos;
                }
            }
        }
    }

    if(truncpos1 == pos_basename)
        lstrcatW(out, pos_basename);
    else if(truncpos1 == truncpos2 || !truncpos2)
        lstrcatW(out, file);
    else
        truncate_path(file, out, truncpos1, truncpos2 ? truncpos2 : (pos_basename-1));
}

static void registry_read_filelist(HWND hMainWnd)
{
    HKEY hFileKey;

    if(registry_get_handle(&hFileKey, 0, key_recentfiles) == ERROR_SUCCESS)
    {
        WCHAR itemText[MAX_PATH+3], buffer[MAX_PATH];
        /* The menu item name is not the same as the file name, so we need to store
           the file name here */
        static WCHAR file1[MAX_PATH], file2[MAX_PATH], file3[MAX_PATH], file4[MAX_PATH];
        WCHAR numFormat[] = {'&','%','d',' ',0};
        LPWSTR pFile[] = {file1, file2, file3, file4};
        DWORD pathSize = MAX_PATH*sizeof(WCHAR);
        int i;
        WCHAR key[6];
        MENUITEMINFOW mi;
        HMENU hMenu = GetMenu(hMainWnd);

        mi.cbSize = sizeof(MENUITEMINFOW);
        mi.fMask = MIIM_ID | MIIM_DATA | MIIM_STRING | MIIM_FTYPE;
        mi.fType = MFT_STRING;
        mi.dwTypeData = itemText;
        mi.wID = ID_FILE_RECENT1;

        RemoveMenu(hMenu, ID_FILE_RECENT_SEPARATOR, MF_BYCOMMAND);
        for(i = 0; i < FILELIST_ENTRIES; i++)
        {
            wsprintfW(key, var_file, i+1);
            RemoveMenu(hMenu, ID_FILE_RECENT1+i, MF_BYCOMMAND);
            if(RegQueryValueExW(hFileKey, (LPWSTR)key, 0, NULL, (LPBYTE)pFile[i], &pathSize)
               != ERROR_SUCCESS)
                break;

            mi.dwItemData = (DWORD)pFile[i];
            wsprintfW(itemText, numFormat, i+1);

            lstrcpyW(buffer, pFile[i]);

            format_filelist_filename(buffer, itemText);

            InsertMenuItemW(hMenu, ID_FILE_EXIT, FALSE, &mi);
            mi.wID++;
            pathSize = MAX_PATH*sizeof(WCHAR);
        }
        mi.fType = MFT_SEPARATOR;
        mi.fMask = MIIM_FTYPE | MIIM_ID;
        InsertMenuItemW(hMenu, ID_FILE_EXIT, FALSE, &mi);

        RegCloseKey(hFileKey);
    }
}

static void registry_set_filelist(LPCWSTR newFile)
{
    HKEY hKey;
    DWORD action;

    if(registry_get_handle(&hKey, &action, key_recentfiles) == ERROR_SUCCESS)
    {
        LPCWSTR pFiles[FILELIST_ENTRIES];
        int i;
        HMENU hMenu = GetMenu(hMainWnd);
        MENUITEMINFOW mi;
        WCHAR buffer[6];

        mi.cbSize = sizeof(MENUITEMINFOW);
        mi.fMask = MIIM_DATA;

        for(i = 0; i < FILELIST_ENTRIES; i++)
            pFiles[i] = NULL;

        for(i = 0; GetMenuItemInfoW(hMenu, ID_FILE_RECENT1+i, FALSE, &mi); i++)
            pFiles[i] = (LPWSTR)mi.dwItemData;

        if(lstrcmpiW(newFile, pFiles[0]))
        {
            for(i = 0; pFiles[i] && i < FILELIST_ENTRIES; i++)
            {
                if(!lstrcmpiW(pFiles[i], newFile))
                {
                    int j;
                    for(j = 0; pFiles[j] && j < i; j++)
                    {
                        pFiles[i-j] = pFiles[i-j-1];
                    }
                    pFiles[0] = NULL;
                    break;
                }
            }

            if(!pFiles[0])
            {
                pFiles[0] = newFile;
            } else
            {
                for(i = 0; pFiles[i] && i < FILELIST_ENTRIES-1; i++)
                    pFiles[FILELIST_ENTRIES-1-i] = pFiles[FILELIST_ENTRIES-2-i];

                pFiles[0] = newFile;
            }

            for(i = 0; pFiles[i] && i < FILELIST_ENTRIES; i++)
            {
                wsprintfW(buffer, var_file, i+1);
                RegSetValueExW(hKey, (LPWSTR)&buffer, 0, REG_SZ, (LPBYTE)pFiles[i],
                               (lstrlenW(pFiles[i])+1)*sizeof(WCHAR));
            }
        }
    }
    RegCloseKey(hKey);
    registry_read_filelist(hMainWnd);
}

static void clear_formatting(void)
{
    PARAFORMAT2 pf;

    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_ALIGNMENT;
    pf.wAlignment = PFA_LEFT;
    SendMessageW(hEditorWnd, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}

static int fileformat_number(WPARAM format)
{
    int number = 0;

    if(format == SF_TEXT)
    {
        number = 1;
    } else if (format == (SF_TEXT | SF_UNICODE))
    {
        number = 2;
    }
    return number;
}

static WPARAM fileformat_flags(int format)
{
    WPARAM flags[] = { SF_RTF , SF_TEXT , SF_TEXT | SF_UNICODE };

    return flags[format];
}

static void set_fileformat(WPARAM format)
{
    fileFormat = format;
}

static void DoOpenFile(LPCWSTR szOpenFileName)
{
    HANDLE hFile;
    EDITSTREAM es;
    char fileStart[5];
    DWORD readOut;
    WPARAM format = SF_TEXT;

    hFile = CreateFileW(szOpenFileName, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return;

    ReadFile(hFile, fileStart, 5, &readOut, NULL);
    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);

    if(readOut >= 2 && (BYTE)fileStart[0] == 0xff && (BYTE)fileStart[1] == 0xfe)
    {
        format = SF_TEXT | SF_UNICODE;
        SetFilePointer(hFile, 2, NULL, FILE_BEGIN);
    } else if(readOut >= 5)
    {
        static const char header[] = "{\\rtf";
        if(!memcmp(header, fileStart, 5))
            format = SF_RTF;
    }

    es.dwCookie = (DWORD_PTR)hFile;
    es.pfnCallback = stream_in;

    clear_formatting();
    SendMessageW(hEditorWnd, EM_STREAMIN, format, (LPARAM)&es);

    CloseHandle(hFile);

    SetFocus(hEditorWnd);

    set_caption(szOpenFileName);

    lstrcpyW(wszFileName, szOpenFileName);
    SendMessageW(hEditorWnd, EM_SETMODIFY, FALSE, 0);
    registry_set_filelist(szOpenFileName);
    set_fileformat(format);
}

static void DoSaveFile(LPCWSTR wszSaveFileName, WPARAM format)
{
    HANDLE hFile;
    EDITSTREAM stream;
    LRESULT ret;

    hFile = CreateFileW(wszSaveFileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL);

    if(hFile == INVALID_HANDLE_VALUE)
        return;

    if(format == (SF_TEXT | SF_UNICODE))
    {
        static const BYTE unicode[] = {0xff,0xfe};
        DWORD writeOut;
        WriteFile(hFile, &unicode, sizeof(unicode), &writeOut, 0);

        if(writeOut != sizeof(unicode))
            return;
    }

    stream.dwCookie = (DWORD_PTR)hFile;
    stream.pfnCallback = stream_out;

    ret = SendMessageW(hEditorWnd, EM_STREAMOUT, format, (LPARAM)&stream);

    CloseHandle(hFile);

    SetFocus(hEditorWnd);

    if(!ret)
    {
        GETTEXTLENGTHEX gt;
        gt.flags = GTL_DEFAULT;
        gt.codepage = 1200;

        if(SendMessageW(hEditorWnd, EM_GETTEXTLENGTHEX, (WPARAM)&gt, 0))
            return;
    }

    lstrcpyW(wszFileName, wszSaveFileName);
    set_caption(wszFileName);
    SendMessageW(hEditorWnd, EM_SETMODIFY, FALSE, 0);
    set_fileformat(format);
}

static void DialogSaveFile(void)
{
    OPENFILENAMEW sfn;

    WCHAR wszFile[MAX_PATH] = {'\0'};
    static const WCHAR wszDefExt[] = {'r','t','f','\0'};

    ZeroMemory(&sfn, sizeof(sfn));

    sfn.lStructSize = sizeof(sfn);
    sfn.Flags = OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    sfn.hwndOwner = hMainWnd;
    sfn.lpstrFilter = wszFilter;
    sfn.lpstrFile = wszFile;
    sfn.nMaxFile = MAX_PATH;
    sfn.lpstrDefExt = wszDefExt;
    sfn.nFilterIndex = fileformat_number(fileFormat)+1;

    while(GetSaveFileNameW(&sfn))
    {
        if(fileformat_flags(sfn.nFilterIndex-1) != SF_RTF)
        {
            if(MessageBoxW(hMainWnd, MAKEINTRESOURCEW(STRING_SAVE_LOSEFORMATTING),
                           wszAppTitle, MB_YESNO | MB_ICONEXCLAMATION) != IDYES)
            {
                continue;
            } else
            {
                DoSaveFile(sfn.lpstrFile, fileformat_flags(sfn.nFilterIndex-1));
                break;
            }
        } else
        {
            DoSaveFile(sfn.lpstrFile, fileformat_flags(sfn.nFilterIndex-1));
            break;
        }
    }
}

static BOOL prompt_save_changes(void)
{
    if(!wszFileName[0])
    {
        GETTEXTLENGTHEX gt;
        gt.flags = GTL_NUMCHARS;
        gt.codepage = 1200;
        if(!SendMessageW(hEditorWnd, EM_GETTEXTLENGTHEX, (WPARAM)&gt, 0))
            return TRUE;
    }

    if(!SendMessageW(hEditorWnd, EM_GETMODIFY, 0, 0))
    {
        return TRUE;
    } else
    {
        LPWSTR displayFileName;
        WCHAR *text;
        int ret;

        if(!wszFileName[0])
            displayFileName = wszDefaultFileName;
        else
            displayFileName = file_basename(wszFileName);

        text = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                         (lstrlenW(displayFileName)+lstrlenW(wszSaveChanges))*sizeof(WCHAR));

        if(!text)
            return FALSE;

        wsprintfW(text, wszSaveChanges, displayFileName);

        ret = MessageBoxW(hMainWnd, text, wszAppTitle, MB_YESNOCANCEL | MB_ICONEXCLAMATION);

        HeapFree(GetProcessHeap(), 0, text);

        switch(ret)
        {
            case IDNO:
                return TRUE;

            case IDYES:
                if(wszFileName[0])
                    DoSaveFile(wszFileName, fileFormat);
                else
                    DialogSaveFile();
                return TRUE;

            default:
                return FALSE;
        }
    }
}

static void DialogOpenFile(void)
{
    OPENFILENAMEW ofn;

    WCHAR wszFile[MAX_PATH] = {'\0'};
    static const WCHAR wszDefExt[] = {'r','t','f','\0'};

    ZeroMemory(&ofn, sizeof(ofn));

    ofn.lStructSize = sizeof(ofn);
    ofn.Flags = OFN_HIDEREADONLY | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.hwndOwner = hMainWnd;
    ofn.lpstrFilter = wszFilter;
    ofn.lpstrFile = wszFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = wszDefExt;
    ofn.nFilterIndex = fileformat_number(fileFormat)+1;

    if(GetOpenFileNameW(&ofn))
    {
        if(prompt_save_changes())
            DoOpenFile(ofn.lpstrFile);
    }
}

static void HandleCommandLine(LPWSTR cmdline)
{
    WCHAR delimiter;
    int opt_print = 0;

    /* skip white space */
    while (*cmdline == ' ') cmdline++;

    /* skip executable name */
    delimiter = (*cmdline == '"' ? '"' : ' ');

    if (*cmdline == delimiter) cmdline++;
    while (*cmdline && *cmdline != delimiter) cmdline++;
    if (*cmdline == delimiter) cmdline++;

    while (*cmdline == ' ' || *cmdline == '-' || *cmdline == '/')
    {
        WCHAR option;

        if (*cmdline++ == ' ') continue;

        option = *cmdline;
        if (option) cmdline++;
        while (*cmdline == ' ') cmdline++;

        switch (option)
        {
            case 'p':
            case 'P':
                opt_print = 1;
                break;
        }
    }

    if (*cmdline)
    {
        /* file name is passed on the command line */
        if (cmdline[0] == '"')
        {
            cmdline++;
            cmdline[lstrlenW(cmdline) - 1] = 0;
        }
        DoOpenFile(cmdline);
        InvalidateRect(hMainWnd, NULL, FALSE);
    }

    if (opt_print)
        MessageBox(hMainWnd, "Printing not implemented", "WordPad", MB_OK);
}

static LRESULT handle_findmsg(LPFINDREPLACEW pFr)
{
    if(pFr->Flags & FR_DIALOGTERM)
    {
        hFindWnd = 0;
        pFr->Flags = FR_FINDNEXT;
        return 0;
    } else if(pFr->Flags & FR_FINDNEXT)
    {
        DWORD flags = FR_DOWN;
        FINDTEXTW ft;
        static CHARRANGE cr;
        LRESULT end, ret;
        GETTEXTLENGTHEX gt;
        LRESULT length;
        int startPos;
        HMENU hMenu = GetMenu(hMainWnd);
        MENUITEMINFOW mi;

        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_DATA;
        mi.dwItemData = 1;
        SetMenuItemInfoW(hMenu, ID_FIND_NEXT, FALSE, &mi);

        gt.flags = GTL_NUMCHARS;
        gt.codepage = 1200;

        length = SendMessageW(hEditorWnd, EM_GETTEXTLENGTHEX, (WPARAM)&gt, 0);

        if(pFr->lCustData == -1)
        {
            SendMessageW(hEditorWnd, EM_GETSEL, (WPARAM)&startPos, (LPARAM)&end);
            cr.cpMin = startPos;
            pFr->lCustData = startPos;
            cr.cpMax = length;
            if(cr.cpMin == length)
                cr.cpMin = 0;
        } else
        {
            startPos = pFr->lCustData;
        }

        if(cr.cpMax > length)
        {
            startPos = 0;
            cr.cpMin = 0;
            cr.cpMax = length;
        }

        ft.chrg = cr;
        ft.lpstrText = pFr->lpstrFindWhat;

        if(pFr->Flags & FR_MATCHCASE)
            flags |= FR_MATCHCASE;
        if(pFr->Flags & FR_WHOLEWORD)
            flags |= FR_WHOLEWORD;

        ret = SendMessageW(hEditorWnd, EM_FINDTEXTW, (WPARAM)flags, (LPARAM)&ft);

        if(ret == -1)
        {
            if(cr.cpMax == length && cr.cpMax != startPos)
            {
                ft.chrg.cpMin = cr.cpMin = 0;
                ft.chrg.cpMax = cr.cpMax = startPos;

                ret = SendMessageW(hEditorWnd, EM_FINDTEXTW, (WPARAM)flags, (LPARAM)&ft);
            }
        }

        if(ret == -1)
        {
            pFr->lCustData = -1;
            MessageBoxW(hMainWnd, MAKEINTRESOURCEW(STRING_SEARCH_FINISHED), wszAppTitle,
                        MB_OK | MB_ICONASTERISK);
        } else
        {
            end = ret + lstrlenW(pFr->lpstrFindWhat);
            cr.cpMin = end;
            SendMessageW(hEditorWnd, EM_SETSEL, (WPARAM)ret, (LPARAM)end);
            SendMessageW(hEditorWnd, EM_SCROLLCARET, 0, 0);
        }
    }

    return 0;
}

static void dialog_find(LPFINDREPLACEW fr)
{
    static WCHAR findBuffer[MAX_STRING_LEN];

    ZeroMemory(fr, sizeof(FINDREPLACEW));
    fr->lStructSize = sizeof(FINDREPLACEW);
    fr->hwndOwner = hMainWnd;
    fr->Flags = FR_HIDEUPDOWN;
    fr->lpstrFindWhat = findBuffer;
    fr->lCustData = -1;
    fr->wFindWhatLen = MAX_STRING_LEN*sizeof(WCHAR);

    hFindWnd = FindTextW(fr);
}


static void DoDefaultFont(void)
{
    static const WCHAR szFaceName[] = {'T','i','m','e','s',' ','N','e','w',' ','R','o','m','a','n',0};
    CHARFORMAT2W fmt;

    ZeroMemory(&fmt, sizeof(fmt));

    fmt.cbSize = sizeof(fmt);
    fmt.dwMask = CFM_FACE | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE;
    fmt.dwEffects = 0;

    lstrcpyW(fmt.szFaceName, szFaceName);

    SendMessageW(hEditorWnd, EM_SETCHARFORMAT,  SCF_DEFAULT, (LPARAM)&fmt);
}

static void update_window(void)
{
    RECT rect;

    GetWindowRect(hMainWnd, &rect);

    (void) OnSize(hMainWnd, SIZE_RESTORED, MAKELONG(rect.bottom, rect.right));
}

static void toggle_toolbar(int bandId)
{
    HWND hwndReBar = GetDlgItem(hMainWnd, IDC_REBAR);
    REBARBANDINFOW rbbinfo;
    BOOL hide = TRUE;

    if(!hwndReBar)
        return;

    rbbinfo.cbSize = sizeof(rbbinfo);
    rbbinfo.fMask = RBBIM_STYLE | RBBIM_SIZE;

    SendMessageW(hwndReBar, RB_GETBANDINFO, bandId, (LPARAM)&rbbinfo);

    if(rbbinfo.fStyle & RBBS_HIDDEN)
        hide = FALSE;

    SendMessageW(hwndReBar, RB_SHOWBAND, bandId, hide ? 0 : 1);

    if(bandId == BANDID_TOOLBAR)
    {
        rbbinfo.fMask ^= RBBIM_SIZE;

        SendMessageW(hwndReBar, RB_GETBANDINFO, BANDID_FORMATBAR, (LPARAM)&rbbinfo);

        if(hide)
            rbbinfo.fStyle ^= RBBS_BREAK;
        else
            rbbinfo.fStyle |= RBBS_BREAK;

        SendMessageW(hwndReBar, RB_SETBANDINFO, BANDID_FORMATBAR, (LPARAM)&rbbinfo);
    }
}

BOOL CALLBACK datetime_proc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch(message)
    {
        case WM_INITDIALOG:
            {
                WCHAR buffer[MAX_STRING_LEN];
                SYSTEMTIME st;
                HWND hListWnd = GetDlgItem(hWnd, IDC_DATETIME);
                GetLocalTime(&st);

                GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, 0, (LPWSTR)&buffer,
                               MAX_STRING_LEN);
                SendMessageW(hListWnd, LB_ADDSTRING, 0, (LPARAM)&buffer);
                GetDateFormatW(LOCALE_USER_DEFAULT, DATE_LONGDATE, &st, 0, (LPWSTR)&buffer,
                               MAX_STRING_LEN);
                SendMessageW(hListWnd, LB_ADDSTRING, 0, (LPARAM)&buffer);
                GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, 0, (LPWSTR)&buffer, MAX_STRING_LEN);
                SendMessageW(hListWnd, LB_ADDSTRING, 0, (LPARAM)&buffer);

                SendMessageW(hListWnd, LB_SETSEL, TRUE, 0);
            }
            break;

        case WM_COMMAND:
            switch(LOWORD(wParam))
            {
                case IDOK:
                    {
                        LRESULT index;
                        HWND hListWnd = GetDlgItem(hWnd, IDC_DATETIME);

                        index = SendMessageW(hListWnd, LB_GETCURSEL, 0, 0);

                        if(index != LB_ERR)
                        {
                            WCHAR buffer[MAX_STRING_LEN];
                            SendMessageW(hListWnd, LB_GETTEXT, index, (LPARAM)&buffer);
                            SendMessageW(hEditorWnd, EM_REPLACESEL, TRUE, (LPARAM)&buffer);
                        }
                    }
                    /* Fall through */

                case IDCANCEL:
                    EndDialog(hWnd, wParam);
                    return TRUE;
            }
    }
    return FALSE;
}

BOOL CALLBACK newfile_proc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch(message)
    {
        case WM_INITDIALOG:
            {
                HINSTANCE hInstance = (HINSTANCE)GetWindowLongPtr(hMainWnd, GWLP_HINSTANCE);
                WCHAR buffer[MAX_STRING_LEN];
                HWND hListWnd = GetDlgItem(hWnd, IDC_NEWFILE);

                LoadStringW(hInstance, STRING_NEWFILE_RICHTEXT, (LPWSTR)buffer, MAX_STRING_LEN);
                SendMessageW(hListWnd, LB_ADDSTRING, 0, (LPARAM)&buffer);
                LoadStringW(hInstance, STRING_NEWFILE_TXT, (LPWSTR)buffer, MAX_STRING_LEN);
                SendMessageW(hListWnd, LB_ADDSTRING, 0, (LPARAM)&buffer);
                LoadStringW(hInstance, STRING_NEWFILE_TXT_UNICODE, (LPWSTR)buffer, MAX_STRING_LEN);
                SendMessageW(hListWnd, LB_ADDSTRING, 0, (LPARAM)&buffer);

                SendMessageW(hListWnd, LB_SETSEL, TRUE, 0);
            }
            break;

        case WM_COMMAND:
            switch(LOWORD(wParam))
            {
                case IDOK:
                    {
                        LRESULT index;
                        HWND hListWnd = GetDlgItem(hWnd, IDC_NEWFILE);
                        index = SendMessageW(hListWnd, LB_GETCURSEL, 0, 0);

                        if(index != LB_ERR)
                            EndDialog(hWnd, MAKELONG(fileformat_flags(index),0));
                    }
                    return TRUE;

                case IDCANCEL:
                    EndDialog(hWnd, MAKELONG(ID_NEWFILE_ABORT,0));
                    return TRUE;
            }
    }
    return FALSE;
}

static LRESULT OnCreate( HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    HWND hToolBarWnd, hFormatBarWnd,  hReBarWnd;
    HINSTANCE hInstance = (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE);
    HANDLE hDLL;
    TBADDBITMAP ab;
    int nStdBitmaps = 0;
    REBARINFO rbi;
    REBARBANDINFOW rbb;
    static const WCHAR wszRichEditDll[] = {'R','I','C','H','E','D','2','0','.','D','L','L','\0'};
    static const WCHAR wszRichEditText[] = {'R','i','c','h','E','d','i','t',' ','t','e','x','t','\0'};

    CreateStatusWindowW(CCS_NODIVIDER|WS_CHILD|WS_VISIBLE, wszRichEditText, hWnd, IDC_STATUSBAR);

    hReBarWnd = CreateWindowExW(WS_EX_TOOLWINDOW, REBARCLASSNAMEW, NULL,
      CCS_NODIVIDER|WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|WS_CLIPCHILDREN|RBS_VARHEIGHT|CCS_TOP,
      CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, hWnd, (HMENU)IDC_REBAR, hInstance, NULL);

    rbi.cbSize = sizeof(rbi);
    rbi.fMask = 0;
    rbi.himl = NULL;
    if(!SendMessageW(hReBarWnd, RB_SETBARINFO, 0, (LPARAM)&rbi))
        return -1;

    hToolBarWnd = CreateToolbarEx(hReBarWnd, CCS_NOPARENTALIGN|CCS_NOMOVEY|WS_VISIBLE|WS_CHILD|TBSTYLE_TOOLTIPS|TBSTYLE_BUTTON,
      IDC_TOOLBAR,
      1, hInstance, IDB_TOOLBAR,
      NULL, 0,
      24, 24, 16, 16, sizeof(TBBUTTON));

    ab.hInst = HINST_COMMCTRL;
    ab.nID = IDB_STD_SMALL_COLOR;
    nStdBitmaps = SendMessageW(hToolBarWnd, TB_ADDBITMAP, 0, (LPARAM)&ab);

    AddButton(hToolBarWnd, nStdBitmaps+STD_FILENEW, ID_FILE_NEW);
    AddButton(hToolBarWnd, nStdBitmaps+STD_FILEOPEN, ID_FILE_OPEN);
    AddButton(hToolBarWnd, nStdBitmaps+STD_FILESAVE, ID_FILE_SAVE);
    AddSeparator(hToolBarWnd);
    AddButton(hToolBarWnd, nStdBitmaps+STD_PRINT, ID_PRINT);
    AddButton(hToolBarWnd, nStdBitmaps+STD_PRINTPRE, ID_PREVIEW);
    AddSeparator(hToolBarWnd);
    AddButton(hToolBarWnd, nStdBitmaps+STD_FIND, ID_FIND);
    AddSeparator(hToolBarWnd);
    AddButton(hToolBarWnd, nStdBitmaps+STD_CUT, ID_EDIT_CUT);
    AddButton(hToolBarWnd, nStdBitmaps+STD_COPY, ID_EDIT_COPY);
    AddButton(hToolBarWnd, nStdBitmaps+STD_PASTE, ID_EDIT_PASTE);
    AddButton(hToolBarWnd, nStdBitmaps+STD_UNDO, ID_EDIT_UNDO);
    AddButton(hToolBarWnd, nStdBitmaps+STD_REDOW, ID_EDIT_REDO);
    AddSeparator(hToolBarWnd);
    AddButton(hToolBarWnd, 0, ID_DATETIME);

    SendMessageW(hToolBarWnd, TB_AUTOSIZE, 0, 0);

    rbb.cbSize = sizeof(rbb);
    rbb.fMask = RBBIM_SIZE | RBBIM_CHILDSIZE | RBBIM_CHILD | RBBIM_STYLE;
    rbb.fStyle = RBBS_CHILDEDGE | RBBS_BREAK | RBBS_NOGRIPPER;
    rbb.cx = 0;
    rbb.hwndChild = hToolBarWnd;
    rbb.cxMinChild = 0;
    rbb.cyChild = rbb.cyMinChild = HIWORD(SendMessageW(hToolBarWnd, TB_GETBUTTONSIZE, 0, 0));

    SendMessageW(hReBarWnd, RB_INSERTBAND, BANDID_TOOLBAR, (LPARAM)&rbb);

    hFormatBarWnd = CreateToolbarEx(hReBarWnd,
         CCS_NOPARENTALIGN | CCS_NOMOVEY | WS_VISIBLE | TBSTYLE_TOOLTIPS | TBSTYLE_BUTTON,
         IDC_FORMATBAR, 7, hInstance, IDB_FORMATBAR, NULL, 0, 16, 16, 16, 16, sizeof(TBBUTTON));

    AddButton(hFormatBarWnd, 0, ID_FORMAT_BOLD);
    AddButton(hFormatBarWnd, 1, ID_FORMAT_ITALIC);
    AddButton(hFormatBarWnd, 2, ID_FORMAT_UNDERLINE);
    AddSeparator(hFormatBarWnd);
    AddButton(hFormatBarWnd, 3, ID_ALIGN_LEFT);
    AddButton(hFormatBarWnd, 4, ID_ALIGN_CENTER);
    AddButton(hFormatBarWnd, 5, ID_ALIGN_RIGHT);
    AddSeparator(hFormatBarWnd);
    AddButton(hFormatBarWnd, 6, ID_BULLET);

    SendMessageW(hFormatBarWnd, TB_AUTOSIZE, 0, 0);

    rbb.hwndChild = hFormatBarWnd;

    SendMessageW(hReBarWnd, RB_INSERTBAND, BANDID_FORMATBAR, (LPARAM)&rbb);

    hDLL = LoadLibraryW(wszRichEditDll);
    if(!hDLL)
    {
        MessageBoxW(hWnd, MAKEINTRESOURCEW(STRING_LOAD_RICHED_FAILED), wszAppTitle,
                    MB_OK | MB_ICONEXCLAMATION);
        PostQuitMessage(1);
    }

    hEditorWnd = CreateWindowExW(WS_EX_CLIENTEDGE, wszRichEditClass, NULL,
      WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|WS_VSCROLL,
      0, 0, 1000, 100, hWnd, (HMENU)IDC_EDITOR, hInstance, NULL);

    if (!hEditorWnd)
    {
        fprintf(stderr, "Error code %u\n", GetLastError());
        return -1;
    }
    assert(hEditorWnd);

    SetFocus(hEditorWnd);
    SendMessageW(hEditorWnd, EM_SETEVENTMASK, 0, ENM_SELCHANGE);

    DoDefaultFont();

    DoLoadStrings();
    SendMessageW(hEditorWnd, EM_SETMODIFY, FALSE, 0);

    ID_FINDMSGSTRING = RegisterWindowMessageW(FINDMSGSTRINGW);

    registry_read_filelist(hWnd);

    return 0;
}

static LRESULT OnUser( HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    HWND hwndEditor = GetDlgItem(hWnd, IDC_EDITOR);
    HWND hwndReBar = GetDlgItem(hWnd, IDC_REBAR);
    HWND hwndToolBar = GetDlgItem(hwndReBar, IDC_TOOLBAR);
    HWND hwndFormatBar = GetDlgItem(hwndReBar, IDC_FORMATBAR);
    int from, to;
    CHARFORMAT2W fmt;
    PARAFORMAT2 pf;
    GETTEXTLENGTHEX gt;

    ZeroMemory(&fmt, sizeof(fmt));
    fmt.cbSize = sizeof(fmt);

    ZeroMemory(&pf, sizeof(pf));
    pf.cbSize = sizeof(pf);

    gt.flags = GTL_NUMCHARS;
    gt.codepage = 1200;

    SendMessageW(hwndToolBar, TB_ENABLEBUTTON, ID_FIND,
                 SendMessageW(hwndEditor, EM_GETTEXTLENGTHEX, (WPARAM)&gt, 0) ? 1 : 0);

    SendMessageW(hwndEditor, EM_GETCHARFORMAT, TRUE, (LPARAM)&fmt);

    SendMessageW(hwndEditor, EM_GETSEL, (WPARAM)&from, (LPARAM)&to);
    SendMessageW(hwndToolBar, TB_ENABLEBUTTON, ID_EDIT_UNDO,
      SendMessageW(hwndEditor, EM_CANUNDO, 0, 0));
    SendMessageW(hwndToolBar, TB_ENABLEBUTTON, ID_EDIT_REDO,
      SendMessageW(hwndEditor, EM_CANREDO, 0, 0));
    SendMessageW(hwndToolBar, TB_ENABLEBUTTON, ID_EDIT_CUT, from == to ? 0 : 1);
    SendMessageW(hwndToolBar, TB_ENABLEBUTTON, ID_EDIT_COPY, from == to ? 0 : 1);

    SendMessageW(hwndFormatBar, TB_CHECKBUTTON, ID_FORMAT_BOLD, (fmt.dwMask & CFM_BOLD) &&
            (fmt.dwEffects & CFE_BOLD));
    SendMessageW(hwndFormatBar, TB_INDETERMINATE, ID_FORMAT_BOLD, !(fmt.dwMask & CFM_BOLD));
    SendMessageW(hwndFormatBar, TB_CHECKBUTTON, ID_FORMAT_ITALIC, (fmt.dwMask & CFM_ITALIC) &&
            (fmt.dwEffects & CFE_ITALIC));
    SendMessageW(hwndFormatBar, TB_INDETERMINATE, ID_FORMAT_ITALIC, !(fmt.dwMask & CFM_ITALIC));
    SendMessageW(hwndFormatBar, TB_CHECKBUTTON, ID_FORMAT_UNDERLINE, (fmt.dwMask & CFM_UNDERLINE) &&
            (fmt.dwEffects & CFE_UNDERLINE));
    SendMessageW(hwndFormatBar, TB_INDETERMINATE, ID_FORMAT_UNDERLINE, !(fmt.dwMask & CFM_UNDERLINE));

    SendMessageW(hwndEditor, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
    SendMessageW(hwndFormatBar, TB_CHECKBUTTON, ID_ALIGN_LEFT, (pf.wAlignment == PFA_LEFT));
    SendMessageW(hwndFormatBar, TB_CHECKBUTTON, ID_ALIGN_CENTER, (pf.wAlignment == PFA_CENTER));
    SendMessageW(hwndFormatBar, TB_CHECKBUTTON, ID_ALIGN_RIGHT, (pf.wAlignment == PFA_RIGHT));

    SendMessageW(hwndFormatBar, TB_CHECKBUTTON, ID_BULLET, (pf.wNumbering & PFN_BULLET));
    return 0;
}

static LRESULT OnNotify( HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    HWND hwndEditor = GetDlgItem(hWnd, IDC_EDITOR);
    NMHDR *pHdr = (NMHDR *)lParam;

    if (pHdr->hwndFrom != hwndEditor)
        return 0;

    if (pHdr->code == EN_SELCHANGE)
    {
        SELCHANGE *pSC = (SELCHANGE *)lParam;
        char buf[128];

        sprintf( buf,"selection = %d..%d, line count=%ld",
                 pSC->chrg.cpMin, pSC->chrg.cpMax,
        SendMessage(hwndEditor, EM_GETLINECOUNT, 0, 0));
        SetWindowText(GetDlgItem(hWnd, IDC_STATUSBAR), buf);
        SendMessage(hWnd, WM_USER, 0, 0);
        return 1;
    }
    return 0;
}

static LRESULT OnCommand( HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    HWND hwndEditor = GetDlgItem(hWnd, IDC_EDITOR);
    HWND hwndStatus = GetDlgItem(hWnd, IDC_STATUSBAR);
    static FINDREPLACEW findreplace;

    if ((HWND)lParam == hwndEditor)
        return 0;

    switch(LOWORD(wParam))
    {

    case ID_FILE_EXIT:
        PostMessageW(hWnd, WM_CLOSE, 0, 0);
        break;

    case ID_FILE_NEW:
        {
            HINSTANCE hInstance = (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE);
            int ret = DialogBox(hInstance, MAKEINTRESOURCE(IDD_NEWFILE), hWnd,
                                (DLGPROC)newfile_proc);

            if(ret != ID_NEWFILE_ABORT)
            {
                if(prompt_save_changes())
                {
                    SETTEXTEX st;

                    set_caption(NULL);
                    wszFileName[0] = '\0';

                    st.flags = ST_DEFAULT;
                    st.codepage = 1200;
                    SendMessageW(hEditorWnd, EM_SETTEXTEX, (WPARAM)&st, 0);

                    clear_formatting();

                    SendMessageW(hEditorWnd, EM_SETMODIFY, FALSE, 0);
                    set_fileformat(ret);
                }
            }
        }
        break;

    case ID_FILE_OPEN:
        DialogOpenFile();
        break;

    case ID_FILE_SAVE:
        if(wszFileName[0])
        {
            DoSaveFile(wszFileName, fileFormat);
            break;
        }
        /* Fall through */

    case ID_FILE_SAVEAS:
        DialogSaveFile();
        break;

    case ID_FILE_RECENT1:
    case ID_FILE_RECENT2:
    case ID_FILE_RECENT3:
    case ID_FILE_RECENT4:
        {
            HMENU hMenu = GetMenu(hWnd);
            MENUITEMINFOW mi;

            mi.cbSize = sizeof(MENUITEMINFOW);
            mi.fMask = MIIM_DATA;
            if(GetMenuItemInfoW(hMenu, LOWORD(wParam), FALSE, &mi))
                DoOpenFile((LPWSTR)mi.dwItemData);
        }
        break;

    case ID_FIND:
        dialog_find(&findreplace);
        break;

    case ID_FIND_NEXT:
        handle_findmsg(&findreplace);
        break;

    case ID_PRINT:
    case ID_PREVIEW:
        {
            static const WCHAR wszNotImplemented[] = {'N','o','t',' ',
                                                      'i','m','p','l','e','m','e','n','t','e','d','\0'};
            MessageBoxW(hWnd, wszNotImplemented, wszAppTitle, MB_OK);
        }
        break;

    case ID_FORMAT_BOLD:
    case ID_FORMAT_ITALIC:
    case ID_FORMAT_UNDERLINE:
        {
        CHARFORMAT2W fmt;
        int mask = CFM_BOLD;
        if (LOWORD(wParam) == ID_FORMAT_ITALIC) mask = CFM_ITALIC;
        if (LOWORD(wParam) == ID_FORMAT_UNDERLINE) mask = CFM_UNDERLINE;

        ZeroMemory(&fmt, sizeof(fmt));
        fmt.cbSize = sizeof(fmt);
        SendMessageW(hwndEditor, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&fmt);
        if (!(fmt.dwMask&mask))
            fmt.dwEffects |= mask;
        else
            fmt.dwEffects ^= mask;
        fmt.dwMask = mask;
        SendMessageW(hwndEditor, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&fmt);
        break;
        }

    case ID_EDIT_CUT:
        PostMessageW(hwndEditor, WM_CUT, 0, 0);
        break;

    case ID_EDIT_COPY:
        PostMessageW(hwndEditor, WM_COPY, 0, 0);
        break;

    case ID_EDIT_PASTE:
        PostMessageW(hwndEditor, WM_PASTE, 0, 0);
        break;

    case ID_EDIT_CLEAR:
        PostMessageW(hwndEditor, WM_CLEAR, 0, 0);
        break;

    case ID_EDIT_SELECTALL:
        {
        CHARRANGE range = {0, -1};
        SendMessageW(hwndEditor, EM_EXSETSEL, 0, (LPARAM)&range);
        /* SendMessage(hwndEditor, EM_SETSEL, 0, -1); */
        return 0;
        }

    case ID_EDIT_GETTEXT:
        {
        int nLen = GetWindowTextLengthW(hwndEditor);
        LPWSTR data = HeapAlloc( GetProcessHeap(), 0, (nLen+1)*sizeof(WCHAR) );
        TEXTRANGEW tr;

        GetWindowTextW(hwndEditor, data, nLen+1);
        MessageBoxW(NULL, data, xszAppTitle, MB_OK);

        HeapFree( GetProcessHeap(), 0, data);
        data = HeapAlloc(GetProcessHeap(), 0, (nLen+1)*sizeof(WCHAR));
        tr.chrg.cpMin = 0;
        tr.chrg.cpMax = nLen;
        tr.lpstrText = data;
        SendMessage (hwndEditor, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
        MessageBoxW(NULL, data, xszAppTitle, MB_OK);
        HeapFree( GetProcessHeap(), 0, data );

        /* SendMessage(hwndEditor, EM_SETSEL, 0, -1); */
        return 0;
        }

    case ID_EDIT_CHARFORMAT:
    case ID_EDIT_DEFCHARFORMAT:
        {
        CHARFORMAT2W cf;
        LRESULT i;
        ZeroMemory(&cf, sizeof(cf));
        cf.cbSize = sizeof(cf);
        cf.dwMask = 0;
        i = SendMessageW(hwndEditor, EM_GETCHARFORMAT,
                        LOWORD(wParam) == ID_EDIT_CHARFORMAT, (LPARAM)&cf);
        return 0;
        }

    case ID_EDIT_PARAFORMAT:
        {
        PARAFORMAT2 pf;
        ZeroMemory(&pf, sizeof(pf));
        pf.cbSize = sizeof(pf);
        SendMessageW(hwndEditor, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
        return 0;
        }

    case ID_EDIT_SELECTIONINFO:
        {
        CHARRANGE range = {0, -1};
        char buf[128];
        WCHAR *data = NULL;

        SendMessage(hwndEditor, EM_EXGETSEL, 0, (LPARAM)&range);
        data = HeapAlloc(GetProcessHeap(), 0, sizeof(*data) * (range.cpMax-range.cpMin+1));
        SendMessage(hwndEditor, EM_GETSELTEXT, 0, (LPARAM)data);
        sprintf(buf, "Start = %d, End = %d", range.cpMin, range.cpMax);
        MessageBoxA(hWnd, buf, "Editor", MB_OK);
        MessageBoxW(hWnd, data, xszAppTitle, MB_OK);
        HeapFree( GetProcessHeap(), 0, data);
        /* SendMessage(hwndEditor, EM_SETSEL, 0, -1); */
        return 0;
        }

    case ID_EDIT_READONLY:
        {
        long nStyle = GetWindowLong(hwndEditor, GWL_STYLE);
        if (nStyle & ES_READONLY)
            SendMessageW(hwndEditor, EM_SETREADONLY, 0, 0);
        else
            SendMessageW(hwndEditor, EM_SETREADONLY, 1, 0);
        return 0;
        }

    case ID_EDIT_MODIFIED:
        if (SendMessageW(hwndEditor, EM_GETMODIFY, 0, 0))
            SendMessageW(hwndEditor, EM_SETMODIFY, 0, 0);
        else
            SendMessageW(hwndEditor, EM_SETMODIFY, 1, 0);
        return 0;

    case ID_EDIT_UNDO:
        SendMessageW(hwndEditor, EM_UNDO, 0, 0);
        return 0;

    case ID_EDIT_REDO:
        SendMessageW(hwndEditor, EM_REDO, 0, 0);
        return 0;

    case ID_BULLET:
        {
            PARAFORMAT pf;

            pf.cbSize = sizeof(pf);
            pf.dwMask = PFM_NUMBERING;
            SendMessageW(hwndEditor, EM_GETPARAFORMAT, 0, (LPARAM)&pf);

            pf.dwMask |=  PFM_OFFSET;

            if(pf.wNumbering == PFN_BULLET)
            {
                pf.wNumbering = 0;
                pf.dxOffset = 0;
            } else
            {
                pf.wNumbering = PFN_BULLET;
                pf.dxOffset = 720;
            }

            SendMessageW(hwndEditor, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
        }
        break;

    case ID_ALIGN_LEFT:
    case ID_ALIGN_CENTER:
    case ID_ALIGN_RIGHT:
        {
        PARAFORMAT2 pf;

        pf.cbSize = sizeof(pf);
        pf.dwMask = PFM_ALIGNMENT;
        switch(LOWORD(wParam)) {
        case ID_ALIGN_LEFT: pf.wAlignment = PFA_LEFT; break;
        case ID_ALIGN_CENTER: pf.wAlignment = PFA_CENTER; break;
        case ID_ALIGN_RIGHT: pf.wAlignment = PFA_RIGHT; break;
        }
        SendMessageW(hwndEditor, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
        break;
        }

    case ID_BACK_1:
        SendMessageW(hwndEditor, EM_SETBKGNDCOLOR, 1, 0);
        break;

    case ID_BACK_2:
        SendMessageW(hwndEditor, EM_SETBKGNDCOLOR, 0, RGB(255,255,192));
        break;

    case ID_TOGGLE_TOOLBAR:
        toggle_toolbar(BANDID_TOOLBAR);
        update_window();
        break;

    case ID_TOGGLE_FORMATBAR:
        toggle_toolbar(BANDID_FORMATBAR);
        update_window();
        break;

    case ID_TOGGLE_STATUSBAR:
        ShowWindow(hwndStatus, IsWindowVisible(hwndStatus) ? SW_HIDE : SW_SHOW);
        update_window();
        break;

    case ID_DATETIME:
        {
        HINSTANCE hInstance = (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE);
        DialogBoxW(hInstance, MAKEINTRESOURCEW(IDD_DATETIME), hWnd, (DLGPROC)datetime_proc);
        break;
        }

    default:
        SendMessageW(hwndEditor, WM_COMMAND, wParam, lParam);
        break;
    }
    return 0;
}

static LRESULT OnInitPopupMenu( HWND hWnd, WPARAM wParam, LPARAM lParam )
{
    HMENU hMenu = (HMENU)wParam;
    HWND hwndEditor = GetDlgItem(hWnd, IDC_EDITOR);
    HWND hwndReBar = GetDlgItem(hWnd, IDC_REBAR);
    HWND hwndStatus = GetDlgItem(hWnd, IDC_STATUSBAR);
    PARAFORMAT pf;
    int nAlignment = -1;
    REBARBANDINFOW rbbinfo;
    int selFrom, selTo;
    GETTEXTLENGTHEX gt;
    LRESULT textLength;
    MENUITEMINFOW mi;

    SendMessageW(hEditorWnd, EM_GETSEL, (WPARAM)&selFrom, (LPARAM)&selTo);
    EnableMenuItem(hMenu, ID_EDIT_COPY, MF_BYCOMMAND|(selFrom == selTo) ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(hMenu, ID_EDIT_CUT, MF_BYCOMMAND|(selFrom == selTo) ? MF_GRAYED : MF_ENABLED);

    pf.cbSize = sizeof(PARAFORMAT);
    SendMessageW(hwndEditor, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
    CheckMenuItem(hMenu, ID_EDIT_READONLY,
      MF_BYCOMMAND|(GetWindowLong(hwndEditor, GWL_STYLE)&ES_READONLY ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, ID_EDIT_MODIFIED,
      MF_BYCOMMAND|(SendMessage(hwndEditor, EM_GETMODIFY, 0, 0) ? MF_CHECKED : MF_UNCHECKED));
    if (pf.dwMask & PFM_ALIGNMENT)
        nAlignment = pf.wAlignment;
    CheckMenuItem(hMenu, ID_ALIGN_LEFT, MF_BYCOMMAND|(nAlignment == PFA_LEFT) ?
            MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hMenu, ID_ALIGN_CENTER, MF_BYCOMMAND|(nAlignment == PFA_CENTER) ?
            MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hMenu, ID_ALIGN_RIGHT, MF_BYCOMMAND|(nAlignment == PFA_RIGHT) ?
            MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hMenu, ID_BULLET, MF_BYCOMMAND | ((pf.wNumbering == PFN_BULLET) ?
            MF_CHECKED : MF_UNCHECKED));
    EnableMenuItem(hMenu, ID_EDIT_UNDO, MF_BYCOMMAND|(SendMessageW(hwndEditor, EM_CANUNDO, 0, 0)) ?
            MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, ID_EDIT_REDO, MF_BYCOMMAND|(SendMessageW(hwndEditor, EM_CANREDO, 0, 0)) ?
            MF_ENABLED : MF_GRAYED);

    rbbinfo.cbSize = sizeof(rbbinfo);
    rbbinfo.fMask = RBBIM_STYLE;
    SendMessageW(hwndReBar, RB_GETBANDINFO, BANDID_TOOLBAR, (LPARAM)&rbbinfo);

    CheckMenuItem(hMenu, ID_TOGGLE_TOOLBAR, MF_BYCOMMAND|(rbbinfo.fStyle & RBBS_HIDDEN) ?
            MF_UNCHECKED : MF_CHECKED);

    SendMessageW(hwndReBar, RB_GETBANDINFO, BANDID_FORMATBAR, (LPARAM)&rbbinfo);

    CheckMenuItem(hMenu, ID_TOGGLE_FORMATBAR, MF_BYCOMMAND|(rbbinfo.fStyle & RBBS_HIDDEN) ?
            MF_UNCHECKED : MF_CHECKED);

    CheckMenuItem(hMenu, ID_TOGGLE_STATUSBAR, MF_BYCOMMAND|IsWindowVisible(hwndStatus) ?
            MF_CHECKED : MF_UNCHECKED);

    gt.flags = GTL_NUMCHARS;
    gt.codepage = 1200;
    textLength = SendMessageW(hEditorWnd, EM_GETTEXTLENGTHEX, (WPARAM)&gt, 0);
    EnableMenuItem(hMenu, ID_FIND, MF_BYCOMMAND|(textLength ? MF_ENABLED : MF_GRAYED));

    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_DATA;

    GetMenuItemInfoW(hMenu, ID_FIND_NEXT, FALSE, &mi);

    EnableMenuItem(hMenu, ID_FIND_NEXT, MF_BYCOMMAND|((textLength && mi.dwItemData) ?
                   MF_ENABLED : MF_GRAYED));
    return 0;
}

static LRESULT OnSize( HWND hWnd, WPARAM wParam, LPARAM lParam )
{
    int nStatusSize = 0;
    RECT rc;
    HWND hwndEditor = GetDlgItem(hWnd, IDC_EDITOR);
    HWND hwndStatusBar = GetDlgItem(hWnd, IDC_STATUSBAR);
    HWND hwndReBar = GetDlgItem(hWnd, IDC_REBAR);
    int rebarHeight = 0;
    REBARBANDINFOW rbbinfo;
    int rebarRows = 2;

    if (hwndStatusBar)
    {
        SendMessageW(hwndStatusBar, WM_SIZE, 0, 0);
        if (IsWindowVisible(hwndStatusBar))
        {
            GetClientRect(hwndStatusBar, &rc);
            nStatusSize = rc.bottom - rc.top;
        } else
        {
            nStatusSize = 0;
        }
    }
    if (hwndReBar)
    {
        rbbinfo.cbSize = sizeof(rbbinfo);
        rbbinfo.fMask = RBBIM_STYLE;

        SendMessageW(hwndReBar, RB_GETBANDINFO, BANDID_TOOLBAR, (LPARAM)&rbbinfo);
        if(rbbinfo.fStyle & RBBS_HIDDEN)
            rebarRows--;

        SendMessageW(hwndReBar, RB_GETBANDINFO, BANDID_FORMATBAR, (LPARAM)&rbbinfo);
        if(rbbinfo.fStyle & RBBS_HIDDEN)
            rebarRows--;

        rebarHeight = rebarRows ? SendMessageW(hwndReBar, RB_GETBARHEIGHT, 0, 0) : 0;

        rc.top = rc.left = 0;
        rc.bottom = rebarHeight;
        rc.right = LOWORD(lParam);
        SendMessageW(hwndReBar, RB_SIZETORECT, 0, (LPARAM)&rc);
    }
    if (hwndEditor)
    {
        GetClientRect(hWnd, &rc);
        MoveWindow(hwndEditor, 0, rebarHeight, rc.right, rc.bottom-nStatusSize-rebarHeight, TRUE);
    }

    return DefWindowProcW(hWnd, WM_SIZE, wParam, lParam);
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if(msg == ID_FINDMSGSTRING)
        return handle_findmsg((LPFINDREPLACEW)lParam);

    switch(msg)
    {
    case WM_CREATE:
        return OnCreate( hWnd, wParam, lParam );

    case WM_USER:
        return OnUser( hWnd, wParam, lParam );

    case WM_NOTIFY:
        return OnNotify( hWnd, wParam, lParam );

    case WM_COMMAND:
        return OnCommand( hWnd, wParam, lParam );

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    case WM_CLOSE:
        if(prompt_save_changes())
        {
            registry_set_winrect();
            PostQuitMessage(0);
        }
        break;

    case WM_ACTIVATE:
        if (LOWORD(wParam))
            SetFocus(GetDlgItem(hWnd, IDC_EDITOR));
        return 0;

    case WM_INITMENUPOPUP:
        return OnInitPopupMenu( hWnd, wParam, lParam );

    case WM_SIZE:
        return OnSize( hWnd, wParam, lParam );

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    return 0;
}

int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hOldInstance, LPSTR szCmdParagraph, int res)
{
    INITCOMMONCONTROLSEX classes = {8, ICC_BAR_CLASSES|ICC_COOL_CLASSES};
    HACCEL hAccel;
    WNDCLASSW wc;
    MSG msg;
    RECT rc;
    static const WCHAR wszAccelTable[] = {'M','A','I','N','A','C','C','E','L',
                                          'T','A','B','L','E','\0'};

    InitCommonControlsEx(&classes);

    hAccel = LoadAcceleratorsW(hInstance, wszAccelTable);

    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 4;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_WORDPAD));
    wc.hCursor = LoadCursor(NULL, IDC_IBEAM);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.lpszMenuName = xszMainMenu;
    wc.lpszClassName = wszMainWndClass;
    RegisterClassW(&wc);

    rc = registry_read_winrect();
    hMainWnd = CreateWindowExW(0, wszMainWndClass, wszAppTitle, WS_OVERLAPPEDWINDOW,
      rc.left, rc.top, rc.right-rc.left, rc.bottom-rc.top, NULL, NULL, hInstance, NULL);
    ShowWindow(hMainWnd, SW_SHOWDEFAULT);

    set_caption(NULL);

    HandleCommandLine(GetCommandLineW());

    while(GetMessageW(&msg,0,0,0))
    {
        if (IsDialogMessage(hFindWnd, &msg))
            continue;

        if (TranslateAcceleratorW(hMainWnd, hAccel, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (!PeekMessageW(&msg, 0, 0, 0, PM_NOREMOVE))
            SendMessageW(hMainWnd, WM_USER, 0, 0);
    }

    return 0;
}
