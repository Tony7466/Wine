/* 
 * Unit test suite for localspl API functions: local print monitor
 *
 * Copyright 2006 Detlef Riekenberg
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
 *
 */

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "wingdi.h"
#include "winreg.h"

#include "winspool.h"
#include "ddk/winsplp.h"

#include "wine/test.h"


/* ##### */

static HMODULE  hdll;
static HMODULE  hlocalmon;
static LPMONITOREX (WINAPI *pInitializePrintMonitor)(LPWSTR);

static LPMONITOREX pm;
static BOOL  (WINAPI *pEnumPorts)(LPWSTR, DWORD, LPBYTE, DWORD, LPDWORD, LPDWORD);
static BOOL  (WINAPI *pOpenPort)(LPWSTR, PHANDLE);
static BOOL  (WINAPI *pOpenPortEx)(LPWSTR, LPWSTR, PHANDLE, struct _MONITOR *);
static BOOL  (WINAPI *pStartDocPort)(HANDLE, LPWSTR, DWORD, DWORD, LPBYTE);
static BOOL  (WINAPI *pWritePort)(HANDLE hPort, LPBYTE, DWORD, LPDWORD);
static BOOL  (WINAPI *pReadPort)(HANDLE hPort, LPBYTE, DWORD, LPDWORD);
static BOOL  (WINAPI *pEndDocPort)(HANDLE);
static BOOL  (WINAPI *pClosePort)(HANDLE);
static BOOL  (WINAPI *pAddPort)(LPWSTR, HWND, LPWSTR);
static BOOL  (WINAPI *pAddPortEx)(LPWSTR, DWORD, LPBYTE, LPWSTR);
static BOOL  (WINAPI *pConfigurePort)(LPWSTR, HWND, LPWSTR);
static BOOL  (WINAPI *pDeletePort)(LPWSTR, HWND, LPWSTR);
static BOOL  (WINAPI *pGetPrinterDataFromPort)(HANDLE, DWORD, LPWSTR, LPWSTR, DWORD, LPWSTR, DWORD, LPDWORD);
static BOOL  (WINAPI *pSetPortTimeOuts)(HANDLE, LPCOMMTIMEOUTS, DWORD);
static BOOL  (WINAPI *pXcvOpenPort)(LPCWSTR, ACCESS_MASK, PHANDLE phXcv);
static DWORD (WINAPI *pXcvDataPort)(HANDLE, LPCWSTR, PBYTE, DWORD, PBYTE, DWORD, PDWORD);
static BOOL  (WINAPI *pXcvClosePort)(HANDLE);

static HMODULE  hlocalui;
static PMONITORUI (WINAPI *pInitializePrintMonitorUI)(VOID);
static PMONITORUI pui;
static BOOL  (WINAPI *pAddPortUI)(PCWSTR, HWND, PCWSTR, PWSTR *);
static BOOL  (WINAPI *pConfigurePortUI)(PCWSTR, HWND, PCWSTR);
static BOOL  (WINAPI *pDeletePortUI)(PCWSTR, HWND, PCWSTR);

static WCHAR cmd_MonitorUIW[] = {'M','o','n','i','t','o','r','U','I',0};
static WCHAR cmd_MonitorUI_lcaseW[] = {'m','o','n','i','t','o','r','u','i',0};
static WCHAR cmd_PortIsValidW[] = {'P','o','r','t','I','s','V','a','l','i','d',0};
static WCHAR does_not_existW[] = {'d','o','e','s','_','n','o','t','_','e','x','i','s','t',0};
static WCHAR emptyW[] = {0};
static WCHAR Monitors_LocalPortW[] = {
                                'S','y','s','t','e','m','\\',
                                'C','u','r','r','e','n','t','C','o','n','t','r','o','l','S','e','t','\\',
                                'C','o','n','t','r','o','l','\\',
                                'P','r','i','n','t','\\',
                                'M','o','n','i','t','o','r','s','\\',
                                'L','o','c','a','l',' ','P','o','r','t',0};

static WCHAR portname_com1W[] = {'C','O','M','1',':',0};
static WCHAR portname_com2W[] = {'C','O','M','2',':',0};
static WCHAR portname_fileW[] = {'F','I','L','E',':',0};
static WCHAR portname_lpt1W[] = {'L','P','T','1',':',0};
static WCHAR portname_lpt2W[] = {'L','P','T','2',':',0};
static WCHAR server_does_not_existW[] = {'\\','\\','d','o','e','s','_','n','o','t','_','e','x','i','s','t',0};
static WCHAR wineW[] = {'W','i','n','e',0};

static WCHAR tempdirW[MAX_PATH];
static WCHAR tempfileW[MAX_PATH];

/* ########################### */

static void test_AddPort(void)
{
    DWORD   res;

    /* moved to localui.dll since w2k */
    if (!pAddPort) return;

    if (0)
    {
    /* NT4 crash on this test */
    res = pAddPort(NULL, 0, NULL);
    }

    /*  Testing-Results (localmon.dll from NT4.0):
        - The Servername is ignored
        - Case of MonitorName is ignored
    */

    SetLastError(0xdeadbeef);
    res = pAddPort(NULL, 0, emptyW);
    ok(!res, "returned %d with %u (expected '0')\n", res, GetLastError());

    SetLastError(0xdeadbeef);
    res = pAddPort(NULL, 0, does_not_existW);
    ok(!res, "returned %d with %u (expected '0')\n", res, GetLastError());

}

/* ########################### */
                                       
static void test_ConfigurePort(void)
{
    DWORD   res;

    /* moved to localui.dll since w2k */
    if (!pConfigurePort) return;

    if (0)
    {
    /* NT4 crash on this test */
    res = pConfigurePort(NULL, 0, NULL);
    }

    /*  Testing-Results (localmon.dll from NT4.0):
        - Case of Portname is ignored
        - "COM1:" and "COM01:" are the same (Compared by value)
        - Portname without ":" => Dialog "Nothing to configure" comes up; Success
        - "LPT1:", "LPT0:" and "LPT:" are the same (Numbers in "LPT:" are ignored)
        - Empty Servername (LPT1:) => Dialog comes up (Servername is ignored)
        - "FILE:" => Dialog "Nothing to configure" comes up; Success
        - Empty Portname =>  => Dialog "Nothing to configure" comes up; Success
        - Port "does_not_exist" => Dialog "Nothing to configure" comes up; Success
    */
    if (winetest_interactive > 0) {

        SetLastError(0xdeadbeef);
        res = pConfigurePort(NULL, 0, portname_com1W);
        trace("returned %d with %u\n", res, GetLastError());

        SetLastError(0xdeadbeef);
        res = pConfigurePort(NULL, 0, portname_lpt1W);
        trace("returned %d with %u\n", res, GetLastError());

        SetLastError(0xdeadbeef);
        res = pConfigurePort(NULL, 0, portname_fileW);
        trace("returned %d with %u\n", res, GetLastError());
    }
}

/* ########################### */

static void test_DeletePort(void)
{
    DWORD   res;

    /* moved to localui.dll since w2k */
    if (!pDeletePort) return;

    if (0)
    {
    /* NT4 crash on this test */
    res = pDeletePort(NULL, 0, NULL);
    }

    /*  Testing-Results (localmon.dll from NT4.0):
        - Case of Portname is ignored (returned '1' on Success)
        - "COM1:" and "COM01:" are different (Compared as string)
        - server_does_not_exist (LPT1:) => Port deleted, Success (Servername is ignored)
        - Empty Portname =>  => FALSE (LastError not changed)
        - Port "does_not_exist" => FALSE (LastError not changed)
    */

    SetLastError(0xdeadbeef);
    res = pDeletePort(NULL, 0, emptyW);
    ok(!res, "returned %d with %u (expected '0')\n", res, GetLastError());

    SetLastError(0xdeadbeef);
    res = pDeletePort(NULL, 0, does_not_existW);
    ok(!res, "returned %d with %u (expected '0')\n", res, GetLastError());

}

/* ########################### */

static void test_EnumPorts(void)
{
    DWORD   res;
    DWORD   level;
    LPBYTE  buffer;
    DWORD   cbBuf;
    DWORD   pcbNeeded;
    DWORD   pcReturned;

    if (!pEnumPorts) return;

    /* valid levels are 1 and 2 */
    for(level = 0; level < 4; level++) {

        cbBuf = 0xdeadbeef;
        pcReturned = 0xdeadbeef;
        SetLastError(0xdeadbeef);
        res = pEnumPorts(NULL, level, NULL, 0, &cbBuf, &pcReturned);

        /* use only a short test, when we test with an invalid level */
        if(!level || (level > 2)) {
            /* NT4 fails with ERROR_INVALID_LEVEL (as expected)
               XP succeeds with ERROR_SUCCESS () */
            ok( (cbBuf == 0) && (pcReturned == 0),
                "(%d) returned %d with %u and %d, %d (expected 0, 0)\n",
                level, res, GetLastError(), cbBuf, pcReturned);
            continue;
        }        

        ok( !res && (GetLastError() == ERROR_INSUFFICIENT_BUFFER),
            "(%d) returned %d with %u and %d, %d (expected '0' with "
            "ERROR_INSUFFICIENT_BUFFER)\n",
            level, res, GetLastError(), cbBuf, pcReturned);

        buffer = HeapAlloc(GetProcessHeap(), 0, cbBuf * 2);
        if (buffer == NULL) continue;

        pcbNeeded = 0xdeadbeef;
        pcReturned = 0xdeadbeef;
        SetLastError(0xdeadbeef);
        res = pEnumPorts(NULL, level, buffer, cbBuf, &pcbNeeded, &pcReturned);
        ok( res, "(%d) returned %d with %u and %d, %d (expected '!= 0')\n",
            level, res, GetLastError(), pcbNeeded, pcReturned);
        /* We can compare the returned Data with the Registry / "win.ini",[Ports] here */

        pcbNeeded = 0xdeadbeef;
        pcReturned = 0xdeadbeef;
        SetLastError(0xdeadbeef);
        res = pEnumPorts(NULL, level, buffer, cbBuf+1, &pcbNeeded, &pcReturned);
        ok( res, "(%d) returned %d with %u and %d, %d (expected '!= 0')\n",
            level, res, GetLastError(), pcbNeeded, pcReturned);

        pcbNeeded = 0xdeadbeef;
        pcReturned = 0xdeadbeef;
        SetLastError(0xdeadbeef);
        res = pEnumPorts(NULL, level, buffer, cbBuf-1, &pcbNeeded, &pcReturned);
        ok( !res && (GetLastError() == ERROR_INSUFFICIENT_BUFFER),
            "(%d) returned %d with %u and %d, %d (expected '0' with "
            "ERROR_INSUFFICIENT_BUFFER)\n",
            level, res, GetLastError(), pcbNeeded, pcReturned);

        if (0)
        {
        /* The following tests crash this app with native localmon/localspl */
        res = pEnumPorts(NULL, level, NULL, cbBuf, &pcbNeeded, &pcReturned);
        res = pEnumPorts(NULL, level, buffer, cbBuf, NULL, &pcReturned);
        res = pEnumPorts(NULL, level, buffer, cbBuf, &pcbNeeded, NULL);
        }

        /* The Servername is ignored */
        pcbNeeded = 0xdeadbeef;
        pcReturned = 0xdeadbeef;
        SetLastError(0xdeadbeef);
        res = pEnumPorts(emptyW, level, buffer, cbBuf+1, &pcbNeeded, &pcReturned);
        ok( res, "(%d) returned %d with %u and %d, %d (expected '!= 0')\n",
            level, res, GetLastError(), pcbNeeded, pcReturned);

        pcbNeeded = 0xdeadbeef;
        pcReturned = 0xdeadbeef;
        SetLastError(0xdeadbeef);
        res = pEnumPorts(server_does_not_existW, level, buffer, cbBuf+1, &pcbNeeded, &pcReturned);
        ok( res, "(%d) returned %d with %u and %d, %d (expected '!= 0')\n",
            level, res, GetLastError(), pcbNeeded, pcReturned);

        HeapFree(GetProcessHeap(), 0, buffer);
    }
}

/* ########################### */


static void test_InitializePrintMonitor(void)
{
    LPMONITOREX res;

    SetLastError(0xdeadbeef);
    res = pInitializePrintMonitor(NULL);
    /* The Parameter was unchecked before w2k */
    ok( res || (GetLastError() == ERROR_INVALID_PARAMETER),
        "returned %p with %u\n (expected '!= NULL' or: NULL with "
        "ERROR_INVALID_PARAMETER)\n", res, GetLastError());

    SetLastError(0xdeadbeef);
    res = pInitializePrintMonitor(emptyW);
    ok( res || (GetLastError() == ERROR_INVALID_PARAMETER),
        "returned %p with %u\n (expected '!= NULL' or: NULL with "
        "ERROR_INVALID_PARAMETER)\n", res, GetLastError());


    /* Every call with a non-empty string returns the same Pointer */
    SetLastError(0xdeadbeef);
    res = pInitializePrintMonitor(Monitors_LocalPortW);
    ok( res == pm,
        "returned %p with %u (expected %p)\n", res, GetLastError(), pm);
}

/* ########################### */

static void test_XcvClosePort(void)
{
    DWORD   res;
    HANDLE hXcv;

    if ((pXcvOpenPort == NULL) || (pXcvClosePort == NULL)) return;

    if (0)
    {
    /* crash with native localspl.dll (w2k+xp) */
    res = pXcvClosePort(NULL);
    res = pXcvClosePort(INVALID_HANDLE_VALUE);
    }


    SetLastError(0xdeadbeef);
    hXcv = (HANDLE) 0xdeadbeef;
    res = pXcvOpenPort(emptyW, SERVER_ACCESS_ADMINISTER, &hXcv);
    ok(res, "returned %d with %u and %p (expected '!= 0')\n", res, GetLastError(), hXcv);

    if (res) {
        SetLastError(0xdeadbeef);
        res = pXcvClosePort(hXcv);
        ok( res, "returned %d with %u (expected '!= 0')\n", res, GetLastError());

        if (0)
        {
        /* test for "Double Free": crash with native localspl.dll (w2k+xp) */
        res = pXcvClosePort(hXcv);
        }
    }
}

/* ########################### */

static void test_XcvDataPort_MonitorUI(void)
{
    DWORD   res;
    HANDLE  hXcv;
    BYTE    buffer[MAX_PATH + 2];
    DWORD   needed;
    DWORD   len;


    if ((pXcvOpenPort == NULL) || (pXcvDataPort == NULL) || (pXcvClosePort == NULL)) return;

    hXcv = (HANDLE) 0xdeadbeef;
    SetLastError(0xdeadbeef);
    res = pXcvOpenPort(emptyW, SERVER_ACCESS_ADMINISTER, &hXcv);
    ok(res, "returned %d with %u and %p (expected '!= 0')\n", res, GetLastError(), hXcv);
    if (!res) return;

    /* ask for needed size */
    needed = (DWORD) 0xdeadbeef;
    SetLastError(0xdeadbeef);
    res = pXcvDataPort(hXcv, cmd_MonitorUIW, NULL, 0, NULL, 0, &needed);
    if (res == ERROR_INVALID_PARAMETER) {
        pXcvClosePort(hXcv);
        skip("'MonitorUI' nor supported\n");
        return;
    }
    ok( (res == ERROR_INSUFFICIENT_BUFFER) && (needed <= MAX_PATH),
        "returned %d with %u and 0x%x (expected 'ERROR_INSUFFICIENT_BUFFER' "
        " and '<= MAX_PATH')\n", res, GetLastError(), needed);

    if (needed > MAX_PATH) {
        pXcvClosePort(hXcv);
        skip("buffer overflow (%u)\n", needed);
        return;
    }
    len = needed;

    /* the command is required */
    needed = (DWORD) 0xdeadbeef;
    SetLastError(0xdeadbeef);
    res = pXcvDataPort(hXcv, emptyW, NULL, 0, NULL, 0, &needed);
    ok( res == ERROR_INVALID_PARAMETER, "returned %d with %u and 0x%x "
        "(expected 'ERROR_INVALID_PARAMETER')\n", res, GetLastError(), needed);

    if (0) {
    /* crash with native localspl.dll (w2k+xp) */
    res = pXcvDataPort(hXcv, NULL, NULL, 0, buffer, MAX_PATH, &needed);
    res = pXcvDataPort(hXcv, cmd_MonitorUIW, NULL, 0, NULL, len, &needed);
    res = pXcvDataPort(hXcv, cmd_MonitorUIW, NULL, 0, buffer, len, NULL);
    }


    /* hXcv is ignored for the command "MonitorUI" */
    needed = (DWORD) 0xdeadbeef;
    SetLastError(0xdeadbeef);
    res = pXcvDataPort(NULL, cmd_MonitorUIW, NULL, 0, buffer, len, &needed);
    ok( res == ERROR_SUCCESS, "returned %d with %u and 0x%x "
        "(expected 'ERROR_SUCCESS')\n", res, GetLastError(), needed);


    /* pszDataName is case-sensitive */
    memset(buffer, 0, len);
    needed = (DWORD) 0xdeadbeef;
    SetLastError(0xdeadbeef);
    res = pXcvDataPort(hXcv, cmd_MonitorUI_lcaseW, NULL, 0, buffer, len, &needed);
    ok( res == ERROR_INVALID_PARAMETER, "returned %d with %u and 0x%x "
        "(expected 'ERROR_INVALID_PARAMETER')\n", res, GetLastError(), needed);

    /* off by one: larger  */
    needed = (DWORD) 0xdeadbeef;
    SetLastError(0xdeadbeef);
    res = pXcvDataPort(hXcv, cmd_MonitorUIW, NULL, 0, buffer, len+1, &needed);
    ok( res == ERROR_SUCCESS, "returned %d with %u and 0x%x "
        "(expected 'ERROR_SUCCESS')\n", res, GetLastError(), needed);


    /* off by one: smaller */
    /* the buffer is not modified for NT4, w2k, XP */
    needed = (DWORD) 0xdeadbeef;
    SetLastError(0xdeadbeef);
    res = pXcvDataPort(hXcv, cmd_MonitorUIW, NULL, 0, buffer, len-1, &needed);
    ok( res == ERROR_INSUFFICIENT_BUFFER, "returned %d with %u and 0x%x "
        "(expected 'ERROR_INSUFFICIENT_BUFFER')\n", res, GetLastError(), needed);

    /* Normal use. The DLL-Name without a Path is returned */
    memset(buffer, 0, len);
    needed = (DWORD) 0xdeadbeef;
    SetLastError(0xdeadbeef);
    res = pXcvDataPort(hXcv, cmd_MonitorUIW, NULL, 0, buffer, len, &needed);
    ok( res == ERROR_SUCCESS, "returned %d with %u and 0x%x "
        "(expected 'ERROR_SUCCESS')\n", res, GetLastError(), needed);


    pXcvClosePort(hXcv);


    /* small check without access-rights: */
    hXcv = (HANDLE) 0xdeadbeef;
    SetLastError(0xdeadbeef);
    res = pXcvOpenPort(emptyW, 0, &hXcv);
    ok(res, "returned %d with %u and %p (expected '!= 0')\n", res, GetLastError(), hXcv);
    if (!res) return;

    /* The ACCESS_MASK is ignored for "MonitorUI" */
    memset(buffer, 0, len);
    needed = (DWORD) 0xdeadbeef;
    SetLastError(0xdeadbeef);
    res = pXcvDataPort(hXcv, cmd_MonitorUIW, NULL, 0, buffer, sizeof(buffer), &needed);
    ok( res == ERROR_SUCCESS, "returned %d with %u and 0x%x "
        "(expected 'ERROR_SUCCESS')\n", res, GetLastError(), needed);

    pXcvClosePort(hXcv);

}

/* ########################### */

static void test_XcvDataPort_PortIsValid(void)
{
    DWORD   res;
    HANDLE  hXcv;
    DWORD   needed;


    if ((pXcvOpenPort == NULL) || (pXcvDataPort == NULL) || (pXcvClosePort == NULL)) return;

    hXcv = (HANDLE) 0xdeadbeef;
    SetLastError(0xdeadbeef);
    res = pXcvOpenPort(emptyW, SERVER_ACCESS_ADMINISTER, &hXcv);
    ok(res, "hXcv: %d with %u and %p (expected '!= 0')\n", res, GetLastError(), hXcv);
    if (!res) return;

    /* normal use: "LPT1:" */
    needed = (DWORD) 0xdeadbeef;
    SetLastError(0xdeadbeef);
    res = pXcvDataPort(hXcv, cmd_PortIsValidW, (PBYTE) portname_lpt1W, sizeof(portname_lpt1W), NULL, 0, &needed);
    if (res == ERROR_INVALID_PARAMETER) {
        pXcvClosePort(hXcv);
        skip("'PostIsValid' not supported\n");
        return;
    }
    ok( res == ERROR_SUCCESS, "returned %d with %u (expected ERROR_SUCCESS)\n", res, GetLastError());


    if (0) {
    /* crash with native localspl.dll (w2k+xp) */
    res = pXcvDataPort(hXcv, cmd_PortIsValidW, NULL, 0, NULL, 0, &needed);
    }


    /* hXcv is ignored for the command "PortIsValid" */
    needed = (DWORD) 0xdeadbeef;
    SetLastError(0xdeadbeef);
    res = pXcvDataPort(NULL, cmd_PortIsValidW, (PBYTE) portname_lpt1W, sizeof(portname_lpt1W), NULL, 0, NULL);
    ok( res == ERROR_SUCCESS, "returned %d with %u (expected ERROR_SUCCESS)\n", res, GetLastError());

    /* needed is ignored */
    needed = (DWORD) 0xdeadbeef;
    SetLastError(0xdeadbeef);
    res = pXcvDataPort(hXcv, cmd_PortIsValidW, (PBYTE) portname_lpt1W, sizeof(portname_lpt1W), NULL, 0, NULL);
    ok( res == ERROR_SUCCESS, "returned %d with %u (expected ERROR_SUCCESS)\n", res, GetLastError());


    /* cbInputData is ignored */
    needed = (DWORD) 0xdeadbeef;
    SetLastError(0xdeadbeef);
    res = pXcvDataPort(hXcv, cmd_PortIsValidW, (PBYTE) portname_lpt1W, 0, NULL, 0, &needed);
    ok( res == ERROR_SUCCESS,
        "returned %d with %u and 0x%x (expected ERROR_SUCCESS)\n",
        res, GetLastError(), needed);

    needed = (DWORD) 0xdeadbeef;
    SetLastError(0xdeadbeef);
    res = pXcvDataPort(hXcv, cmd_PortIsValidW, (PBYTE) portname_lpt1W, 1, NULL, 0, &needed);
    ok( res == ERROR_SUCCESS,
        "returned %d with %u and 0x%x (expected ERROR_SUCCESS)\n",
        res, GetLastError(), needed);

    needed = (DWORD) 0xdeadbeef;
    SetLastError(0xdeadbeef);
    res = pXcvDataPort(hXcv, cmd_PortIsValidW, (PBYTE) portname_lpt1W, sizeof(portname_lpt1W) -1, NULL, 0, &needed);
    ok( res == ERROR_SUCCESS,
        "returned %d with %u and 0x%x (expected ERROR_SUCCESS)\n",
        res, GetLastError(), needed);

    needed = (DWORD) 0xdeadbeef;
    SetLastError(0xdeadbeef);
    res = pXcvDataPort(hXcv, cmd_PortIsValidW, (PBYTE) portname_lpt1W, sizeof(portname_lpt1W) -2, NULL, 0, &needed);
    ok( res == ERROR_SUCCESS,
        "returned %d with %u and 0x%x (expected ERROR_SUCCESS)\n",
        res, GetLastError(), needed);


    /* an empty name is not allowed */
    needed = (DWORD) 0xdeadbeef;
    SetLastError(0xdeadbeef);
    res = pXcvDataPort(hXcv, cmd_PortIsValidW, (PBYTE) emptyW, sizeof(emptyW), NULL, 0, &needed);
    ok( res == ERROR_PATH_NOT_FOUND,
        "returned %d with %u and 0x%x (expected ERROR_PATH_NOT_FOUND)\n",
        res, GetLastError(), needed);


    /* a directory is not allowed */
    needed = (DWORD) 0xdeadbeef;
    SetLastError(0xdeadbeef);
    res = pXcvDataPort(hXcv, cmd_PortIsValidW, (PBYTE) tempdirW, (lstrlenW(tempdirW) + 1) * sizeof(WCHAR), NULL, 0, &needed);
    /* XP(admin): ERROR_INVALID_NAME, XP(user): ERROR_PATH_NOT_FOUND, w2k ERROR_ACCESS_DENIED */
    ok( (res == ERROR_INVALID_NAME) || (res == ERROR_PATH_NOT_FOUND) ||
        (res == ERROR_ACCESS_DENIED), "returned %d with %u and 0x%x "
        "(expected ERROR_INVALID_NAME, ERROR_PATH_NOT_FOUND or ERROR_ACCESS_DENIED)\n",
        res, GetLastError(), needed);


    /* test more valid well known Ports: */
    needed = (DWORD) 0xdeadbeef;
    SetLastError(0xdeadbeef);
    res = pXcvDataPort(hXcv, cmd_PortIsValidW, (PBYTE) portname_lpt2W, sizeof(portname_lpt2W), NULL, 0, &needed);
    ok( res == ERROR_SUCCESS,
        "returned %d with %u and 0x%x (expected ERROR_SUCCESS)\n",
        res, GetLastError(), needed);


    needed = (DWORD) 0xdeadbeef;
    SetLastError(0xdeadbeef);
    res = pXcvDataPort(hXcv, cmd_PortIsValidW, (PBYTE) portname_com1W, sizeof(portname_com1W), NULL, 0, &needed);
    ok( res == ERROR_SUCCESS,
        "returned %d with %u and 0x%x (expected ERROR_SUCCESS)\n",
        res, GetLastError(), needed);


    needed = (DWORD) 0xdeadbeef;
    SetLastError(0xdeadbeef);
    res = pXcvDataPort(hXcv, cmd_PortIsValidW, (PBYTE) portname_com2W, sizeof(portname_com2W), NULL, 0, &needed);
    ok( res == ERROR_SUCCESS,
        "returned %d with %u and 0x%x (expected ERROR_SUCCESS)\n",
        res, GetLastError(), needed);


    needed = (DWORD) 0xdeadbeef;
    SetLastError(0xdeadbeef);
    res = pXcvDataPort(hXcv, cmd_PortIsValidW, (PBYTE) portname_fileW, sizeof(portname_fileW), NULL, 0, &needed);
    ok( res == ERROR_SUCCESS,
        "returned %d with %u and 0x%x (expected ERROR_SUCCESS)\n",
        res, GetLastError(), needed);


    /* a normal, writable file is allowed */
    needed = (DWORD) 0xdeadbeef;
    SetLastError(0xdeadbeef);
    res = pXcvDataPort(hXcv, cmd_PortIsValidW, (PBYTE) tempfileW, (lstrlenW(tempfileW) + 1) * sizeof(WCHAR), NULL, 0, &needed);
    ok( res == ERROR_SUCCESS,
        "returned %d with %u and 0x%x (expected ERROR_SUCCESS)\n",
        res, GetLastError(), needed);

    pXcvClosePort(hXcv);


    /* small check without access-rights: */
    hXcv = (HANDLE) 0xdeadbeef;
    SetLastError(0xdeadbeef);
    res = pXcvOpenPort(emptyW, 0, &hXcv);
    ok(res, "returned %d with %u and %p (expected '!= 0')\n", res, GetLastError(), hXcv);
    if (!res) return;

    /* The ACCESS_MASK from XcvOpenPort is ignored in "PortIsValid" */
    needed = (DWORD) 0xdeadbeef;
    SetLastError(0xdeadbeef);
    res = pXcvDataPort(hXcv, cmd_PortIsValidW, (PBYTE) portname_lpt1W, sizeof(portname_lpt1W), NULL, 0, &needed);
    ok( res == ERROR_SUCCESS, "returned %d with %u (expected ERROR_SUCCESS)\n", res, GetLastError());

    pXcvClosePort(hXcv);

}

/* ########################### */

static void test_XcvOpenPort(void)
{
    DWORD   res;
    HANDLE  hXcv;

    if ((pXcvOpenPort == NULL) || (pXcvClosePort == NULL)) return;

    if (0)
    {
    /* crash with native localspl.dll (w2k+xp) */
    res = pXcvOpenPort(NULL, SERVER_ACCESS_ADMINISTER, &hXcv);
    res = pXcvOpenPort(emptyW, SERVER_ACCESS_ADMINISTER, NULL);
    }


    /* The returned handle is the result from a previous "spoolss.dll,DllAllocSplMem" */
    SetLastError(0xdeadbeef);
    hXcv = (HANDLE) 0xdeadbeef;
    res = pXcvOpenPort(emptyW, SERVER_ACCESS_ADMINISTER, &hXcv);
    ok(res, "returned %d with %u and %p (expected '!= 0')\n", res, GetLastError(), hXcv);
    if (res) pXcvClosePort(hXcv);


    /* The ACCESS_MASK is not checked in XcvOpenPort */
    SetLastError(0xdeadbeef);
    hXcv = (HANDLE) 0xdeadbeef;
    res = pXcvOpenPort(emptyW, 0, &hXcv);
    ok(res, "returned %d with %u and %p (expected '!= 0')\n", res, GetLastError(), hXcv);
    if (res) pXcvClosePort(hXcv);


    /* A copy of pszObject is saved in the Memory-Block */
    SetLastError(0xdeadbeef);
    hXcv = (HANDLE) 0xdeadbeef;
    res = pXcvOpenPort(portname_lpt1W, SERVER_ALL_ACCESS, &hXcv);
    ok(res, "returned %d with %u and %p (expected '!= 0')\n", res, GetLastError(), hXcv);
    if (res) pXcvClosePort(hXcv);

    SetLastError(0xdeadbeef);
    hXcv = (HANDLE) 0xdeadbeef;
    res = pXcvOpenPort(portname_fileW, SERVER_ALL_ACCESS, &hXcv);
    ok(res, "returned %d with %u and %p (expected '!= 0')\n", res, GetLastError(), hXcv);
    if (res) pXcvClosePort(hXcv);

}

/* ########################### */

#define GET_MONITOR_FUNC(name) \
            if(numentries > 0) { \
                numentries--; \
                p##name = (void *) pm->Monitor.pfn##name ;  \
            }


START_TEST(localmon)
{
    DWORD   numentries;
    DWORD   res;

    /* This DLL does not exist on Win9x */
    hdll = LoadLibraryA("localspl.dll");
    if (!hdll) return;

    tempdirW[0] = '\0';
    tempfileW[0] = '\0';
    res = GetTempPathW(MAX_PATH, tempdirW);
    ok(res != 0, "with %u\n", GetLastError());
    res = GetTempFileNameW(tempdirW, wineW, 0, tempfileW);
    ok(res != 0, "with %u\n", GetLastError());

    pInitializePrintMonitor = (void *) GetProcAddress(hdll, "InitializePrintMonitor");
    pInitializePrintMonitorUI = (void *) GetProcAddress(hdll, "InitializePrintMonitorUI");

    if (!pInitializePrintMonitor) {
        /* The Monitor for "Local Ports" was in a seperate dll before w2k */
        hlocalmon = LoadLibraryA("localmon.dll");
        if (hlocalmon) {
            pInitializePrintMonitor = (void *) GetProcAddress(hlocalmon, "InitializePrintMonitor");
        }
    }
    if (!pInitializePrintMonitor) return;

    /* Native localmon.dll / localspl.dll need a vaild Port-Entry in:
       a) since xp: HKLM\Software\Microsoft\Windows NT\CurrentVersion\Ports 
       b) up to w2k: Section "Ports" in win.ini
       or InitializePrintMonitor fails. */
    pm = pInitializePrintMonitor(Monitors_LocalPortW);
    if (pm) {
        numentries = (pm->dwMonitorSize ) / sizeof(VOID *);
        /* NT4: 14, since w2k: 17 */
        ok( numentries == 14 || numentries == 17, 
            "dwMonitorSize (%d) => %d Functions\n", pm->dwMonitorSize, numentries);

        GET_MONITOR_FUNC(EnumPorts);
        GET_MONITOR_FUNC(OpenPort);
        GET_MONITOR_FUNC(OpenPortEx);
        GET_MONITOR_FUNC(StartDocPort);
        GET_MONITOR_FUNC(WritePort);
        GET_MONITOR_FUNC(ReadPort);
        GET_MONITOR_FUNC(EndDocPort);
        GET_MONITOR_FUNC(ClosePort);
        GET_MONITOR_FUNC(AddPort);
        GET_MONITOR_FUNC(AddPortEx);
        GET_MONITOR_FUNC(ConfigurePort);
        GET_MONITOR_FUNC(DeletePort);
        GET_MONITOR_FUNC(GetPrinterDataFromPort);
        GET_MONITOR_FUNC(SetPortTimeOuts);
        GET_MONITOR_FUNC(XcvOpenPort);
        GET_MONITOR_FUNC(XcvDataPort);
        GET_MONITOR_FUNC(XcvClosePort);
    }

    if ((!pInitializePrintMonitorUI) && (pXcvOpenPort) && (pXcvDataPort) && (pXcvClosePort)) {
        /* The user interface for "Local Ports" is in a separate dll since w2k */
        BYTE    buffer[MAX_PATH];
        DWORD   res;
        DWORD   len;
        HANDLE  hXcv;

        res = pXcvOpenPort(emptyW, SERVER_ACCESS_ADMINISTER, &hXcv);
        if (res) {
            res = pXcvDataPort(hXcv, cmd_MonitorUIW, NULL, 0, buffer, MAX_PATH, &len);
            if (res == ERROR_SUCCESS) hlocalui = LoadLibraryW( (LPWSTR) buffer);
            if (hlocalui) pInitializePrintMonitorUI = (void *) GetProcAddress(hlocalui, "InitializePrintMonitorUI");
            pXcvClosePort(hXcv);
        }
    }

    if (pInitializePrintMonitorUI) {
        pui = pInitializePrintMonitorUI();
        if (pui) {
            numentries = (pui->dwMonitorUISize - sizeof(DWORD)) / sizeof(VOID *);
            ok( numentries == 3,
                "dwMonitorUISize (%d) => %d Functions\n", pui->dwMonitorUISize, numentries);

            if (numentries > 2) {
                pAddPortUI = pui->pfnAddPortUI;
                pConfigurePortUI = pui->pfnConfigurePortUI;
                pDeletePortUI = pui->pfnDeletePortUI;
            }
        }
    }

    test_InitializePrintMonitor();

    test_AddPort();
    test_ConfigurePort();
    test_DeletePort();
    test_EnumPorts();
    test_XcvClosePort();
    test_XcvDataPort_MonitorUI();
    test_XcvDataPort_PortIsValid();
    test_XcvOpenPort();
}
