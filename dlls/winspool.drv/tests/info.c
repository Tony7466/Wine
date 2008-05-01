/*
 * Copyright (C) 2003, 2004 Stefan Leichter
 * Copyright (C) 2005, 2006 Detlef Riekenberg
 * Copyright (C) 2006 Dmitry Timoshkov
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

#include "wine/test.h"
#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "wingdi.h"
#include "winuser.h"
#include "winreg.h"
#include "winspool.h"

#define MAGIC_DEAD  0xdeadbeef
#define DEFAULT_PRINTER_SIZE 1000

static CHAR does_not_exist_dll[]= "does_not_exist.dll";
static CHAR does_not_exist[]    = "does_not_exist";
static CHAR empty[]             = "";
static CHAR env_x86[]           = "Windows NT x86";
static CHAR env_win9x_case[]    = "windowS 4.0";
static CHAR illegal_name[]      = "illegal,name";
static CHAR invalid_env[]       = "invalid_env";
static CHAR portname_com1[]     = "COM1:";
static CHAR portname_file[]     = "FILE:";
static CHAR portname_lpt1[]     = "LPT1:";
static CHAR server_does_not_exist[] = "\\does_not_exist";
static CHAR version_dll[]       = "version.dll";
static CHAR winetest[]  = "winetest";

static HANDLE  hwinspool;
static FARPROC pGetDefaultPrinterA;
static FARPROC pSetDefaultPrinterA;

struct monitor_entry {
    LPSTR  env;
    CHAR  dllname[32];
};

static LPSTR   default_printer = NULL;
static LPSTR   local_server = NULL;

/* report common behavior only once */
static DWORD report_deactivated_spooler = 1;
#define RETURN_ON_DEACTIVATED_SPOOLER(res) \
    if((res == 0) && (GetLastError() == RPC_S_SERVER_UNAVAILABLE)) \
    { \
        if(report_deactivated_spooler > 0) { \
            report_deactivated_spooler--; \
            trace("The Service 'Spooler' is required for many test\n"); \
        } \
        return; \
    }


static void find_default_printer(VOID)
{
    static  char    buffer[DEFAULT_PRINTER_SIZE];
    DWORD   needed;
    DWORD   res;
    LPSTR   ptr;

    if ((default_printer == NULL) && (pGetDefaultPrinterA))
    {
        /* w2k and above */
        needed = sizeof(buffer);
        res = pGetDefaultPrinterA(buffer, &needed);
        if(res)  default_printer = buffer;
        trace("default_printer: '%s'\n", default_printer);
    }
    if (default_printer == NULL)
    {
        HKEY hwindows;
        DWORD   type;
        /* NT 3.x and above */
        if (RegOpenKeyEx(HKEY_CURRENT_USER, 
                        "Software\\Microsoft\\Windows NT\\CurrentVersion\\Windows",
                        0, KEY_QUERY_VALUE, &hwindows) == NO_ERROR) {

            needed = sizeof(buffer);
            if (RegQueryValueEx(hwindows, "device", NULL, 
                                &type, (LPBYTE)buffer, &needed) == NO_ERROR) {

                ptr = strchr(buffer, ',');
                if (ptr) {
                    ptr[0] = '\0';
                    default_printer = buffer;
                }
            }
            RegCloseKey(hwindows);
        }
        trace("default_printer: '%s'\n", default_printer);
    }
    if (default_printer == NULL)
    {
        /* win9x */
        needed = sizeof(buffer);
        res = GetProfileStringA("windows", "device", "*", buffer, needed);
        if(res) {
            ptr = strchr(buffer, ',');
            if (ptr) {
                ptr[0] = '\0';
                default_printer = buffer;
            }
        }
        trace("default_printer: '%s'\n", default_printer);
    }
}


static struct monitor_entry * find_installed_monitor(void)
{
    MONITOR_INFO_2A mi2a; 
    static struct  monitor_entry * entry = NULL;
    DWORD   num_tests;
    DWORD   i = 0;

    static struct monitor_entry  monitor_table[] = {
        {env_win9x_case, "localspl.dll"},
        {env_x86,        "localspl.dll"},
        {env_win9x_case, "localmon.dll"},
        {env_x86,        "localmon.dll"},
        {env_win9x_case, "tcpmon.dll"},
        {env_x86,        "tcpmon.dll"},
        {env_win9x_case, "usbmon.dll"},
        {env_x86,        "usbmon.dll"},
        {env_win9x_case, "mspp32.dll"},
        {env_x86,        "win32spl.dll"},
        {env_x86,        "redmonnt.dll"},
        {env_x86,        "redmon35.dll"},
        {env_win9x_case, "redmon95.dll"},
        {env_x86,        "pdfcmnnt.dll"},
        {env_win9x_case, "pdfcmn95.dll"},
    };

    if (entry) return entry;

    num_tests = (sizeof(monitor_table)/sizeof(struct monitor_entry));

    /* cleanup */
    DeleteMonitorA(NULL, env_x86, winetest);
    DeleteMonitorA(NULL, env_win9x_case, winetest);

    /* find a usable monitor from the table */
    mi2a.pName = winetest;
    while ((entry == NULL) && (i < num_tests)) {
        entry = &monitor_table[i];
        i++;
        mi2a.pEnvironment = entry->env;
        mi2a.pDLLName = entry->dllname;

        if (AddMonitorA(NULL, 2, (LPBYTE) &mi2a)) {
            /* we got one */
            trace("using '%s', '%s'\n", entry->env, entry->dllname);
            DeleteMonitorA(NULL, entry->env, winetest);
        }
        else
        {
            entry = NULL;
        }
    }
    return entry;
}


/* ########################### */

static void find_local_server(VOID)
{
    static  char    buffer[MAX_PATH];
    DWORD   res;
    DWORD   size;

    size = sizeof(buffer) - 3 ;
    buffer[0] = '\\';
    buffer[1] = '\\';
    buffer[2] = '\0';

    SetLastError(0xdeadbeef);
    res = GetComputerNameA(&buffer[2], &size);
    trace("returned %d with %d and %d: '%s'\n", res, GetLastError(), size, buffer);

    ok( res != 0, "returned %d with %d and %d: '%s' (expected '!= 0')\n",
        res, GetLastError(), size, buffer);

    if (res) local_server = buffer;
}

/* ########################### */


static void test_AddMonitor(void)
{
    MONITOR_INFO_2A mi2a; 
    struct  monitor_entry * entry = NULL;
    DWORD   res;

    entry = find_installed_monitor();

    SetLastError(MAGIC_DEAD);
    res = AddMonitorA(NULL, 1, NULL);
    ok(!res && (GetLastError() == ERROR_INVALID_LEVEL), 
        "returned %d with %d (expected '0' with ERROR_INVALID_LEVEL)\n",
        res, GetLastError());

    SetLastError(MAGIC_DEAD);
    res = AddMonitorA(NULL, 3, NULL);
    ok(!res && (GetLastError() == ERROR_INVALID_LEVEL), 
        "returned %d with %d (expected '0' with ERROR_INVALID_LEVEL)\n",
        res, GetLastError());

    if (0)
    {
    /* This test crash with win9x on vmware (works with win9x on qemu 0.8.1) */
    SetLastError(MAGIC_DEAD);
    res = AddMonitorA(NULL, 2, NULL);
    /* NT: unchanged,  9x: ERROR_PRIVILEGE_NOT_HELD */
    ok(!res &&
        ((GetLastError() == MAGIC_DEAD) ||
         (GetLastError() == ERROR_PRIVILEGE_NOT_HELD)), 
        "returned %d with %d (expected '0' with: MAGIC_DEAD or "
        "ERROR_PRIVILEGE_NOT_HELD)\n", res, GetLastError());
    }

    ZeroMemory(&mi2a, sizeof(MONITOR_INFO_2A));
    SetLastError(MAGIC_DEAD);
    res = AddMonitorA(NULL, 2, (LPBYTE) &mi2a);
    RETURN_ON_DEACTIVATED_SPOOLER(res)

    if (!res && (GetLastError() == ERROR_ACCESS_DENIED)) {
        trace("skip tests (ACCESS_DENIED)\n");
        return;
    }

    /* NT: ERROR_INVALID_PARAMETER,  9x: ERROR_INVALID_ENVIRONMENT */
    ok(!res && ((GetLastError() == ERROR_INVALID_PARAMETER) ||
                (GetLastError() == ERROR_INVALID_ENVIRONMENT)), 
        "returned %d with %d (expected '0' with: ERROR_INVALID_PARAMETER or "
        "ERROR_INVALID_ENVIRONMENT)\n", res, GetLastError());

    if (!entry) {
        trace("No usable Monitor found: Skip tests\n");
        return;
    }

    if (0)
    {
    /* The Test is deactivated, because when mi2a.pName is NULL, the subkey
       HKLM\System\CurrentControlSet\Control\Print\Monitors\C:\WINDOWS\SYSTEM
       or HKLM\System\CurrentControlSet\Control\Print\Monitors\�
       is created on win9x and we do not want to hit this bug here. */

    mi2a.pEnvironment = entry->env;
    SetLastError(MAGIC_DEAD);
    res = AddMonitorA(NULL, 2, (LPBYTE) &mi2a);
    /* NT: ERROR_INVALID_PARAMETER,  9x: ERROR_PRIVILEGE_NOT_HELD */
    }

    mi2a.pEnvironment = entry->env;
    mi2a.pName = empty;
    SetLastError(MAGIC_DEAD);
    res = AddMonitorA(NULL, 2, (LPBYTE) &mi2a);
    /* NT: ERROR_INVALID_PARAMETER,  9x: ERROR_PRIVILEGE_NOT_HELD */
    ok( !res &&
        ((GetLastError() == ERROR_INVALID_PARAMETER) ||
         (GetLastError() == ERROR_PRIVILEGE_NOT_HELD)), 
        "returned %d with %d (expected '0' with: ERROR_INVALID_PARAMETER or "
        "ERROR_PRIVILEGE_NOT_HELD)\n",
        res, GetLastError());

    mi2a.pName = winetest;
    SetLastError(MAGIC_DEAD);
    res = AddMonitorA(NULL, 2, (LPBYTE) &mi2a);
    /* NT: ERROR_INVALID_PARAMETER,  9x: ERROR_PRIVILEGE_NOT_HELD */
    ok( !res &&
        ((GetLastError() == ERROR_INVALID_PARAMETER) ||
         (GetLastError() == ERROR_PRIVILEGE_NOT_HELD)), 
        "returned %d with %d (expected '0' with: ERROR_INVALID_PARAMETER or "
        "ERROR_PRIVILEGE_NOT_HELD)\n",
        res, GetLastError());

    mi2a.pDLLName = empty;
    SetLastError(MAGIC_DEAD);
    res = AddMonitorA(NULL, 2, (LPBYTE) &mi2a);
    ok( !res && (GetLastError() == ERROR_INVALID_PARAMETER),
        "returned %d with %d (expected '0' with ERROR_INVALID_PARAMETER)\n",
        res, GetLastError());

    mi2a.pDLLName = does_not_exist_dll;
    SetLastError(MAGIC_DEAD);
    res = AddMonitorA(NULL, 2, (LPBYTE) &mi2a);
    /* NT: ERROR_MOD_NOT_FOUND,  9x: ERROR_INVALID_PARAMETER */
    ok( !res &&
        ((GetLastError() == ERROR_MOD_NOT_FOUND) ||
        (GetLastError() == ERROR_INVALID_PARAMETER)),
        "returned %d with %d (expected '0' with: ERROR_MOD_NOT_FOUND or "
        "ERROR_INVALID_PARAMETER)\n", res, GetLastError());

    mi2a.pDLLName = version_dll;
    SetLastError(MAGIC_DEAD);
    res = AddMonitorA(NULL, 2, (LPBYTE) &mi2a);
    /* NT: ERROR_PROC_NOT_FOUND,  9x: ERROR_INVALID_PARAMETER */
    ok( !res &&
        ((GetLastError() == ERROR_PROC_NOT_FOUND) ||
        (GetLastError() == ERROR_INVALID_PARAMETER)),
        "returned %d with %d (expected '0' with: ERROR_PROC_NOT_FOUND or "
        "ERROR_INVALID_PARAMETER)\n", res, GetLastError());
    if (res) DeleteMonitorA(NULL, entry->env, winetest);

   /* Test AddMonitor with real options */
    mi2a.pDLLName = entry->dllname;
    SetLastError(MAGIC_DEAD);
    res = AddMonitorA(NULL, 2, (LPBYTE) &mi2a);
    ok(res, "returned %d with %d (expected '!= 0')\n", res, GetLastError());

    /* add a monitor twice */
    SetLastError(MAGIC_DEAD);
    res = AddMonitorA(NULL, 2, (LPBYTE) &mi2a);
    /* NT: ERROR_PRINT_MONITOR_ALREADY_INSTALLED (3006), 9x: ERROR_ALREADY_EXISTS (183) */
    ok( !res &&
        ((GetLastError() == ERROR_PRINT_MONITOR_ALREADY_INSTALLED) ||
        (GetLastError() == ERROR_ALREADY_EXISTS)), 
        "returned %d with %d (expected '0' with: "
        "ERROR_PRINT_MONITOR_ALREADY_INSTALLED or ERROR_ALREADY_EXISTS)\n",
        res, GetLastError());

    DeleteMonitorA(NULL, entry->env, winetest);
    SetLastError(MAGIC_DEAD);
    res = AddMonitorA(empty, 2, (LPBYTE) &mi2a);
    ok(res, "returned %d with %d (expected '!= 0')\n", res, GetLastError());

    /* cleanup */
    DeleteMonitorA(NULL, entry->env, winetest);

}

/* ########################### */

static void test_AddPort(void)
{
    DWORD   res;

    SetLastError(0xdeadbeef);
    res = AddPortA(NULL, 0, NULL);
    RETURN_ON_DEACTIVATED_SPOOLER(res)
    /* NT: RPC_X_NULL_REF_POINTER, 9x: ERROR_INVALID_PARAMETER */
    ok( !res && ((GetLastError() == RPC_X_NULL_REF_POINTER) || 
                 (GetLastError() == ERROR_INVALID_PARAMETER)),
        "returned %d with %d (expected '0' with ERROR_NOT_SUPPORTED or "
        "ERROR_INVALID_PARAMETER)\n", res, GetLastError());


    SetLastError(0xdeadbeef);
    res = AddPortA(NULL, 0, empty);
    /* Allowed only for (Printer-)Administrators */
    if (!res && (GetLastError() == ERROR_ACCESS_DENIED)) {
        trace("skip tests (ACCESS_DENIED)\n");
        return;
    }
    /* XP: ERROR_NOT_SUPPORTED, NT351 and 9x: ERROR_INVALID_PARAMETER */
    ok( !res && ((GetLastError() == ERROR_NOT_SUPPORTED) || 
                 (GetLastError() == ERROR_INVALID_PARAMETER)),
        "returned %d with %d (expected '0' with ERROR_NOT_SUPPORTED or "
        "ERROR_INVALID_PARAMETER)\n", res, GetLastError());


    SetLastError(0xdeadbeef);
    res = AddPortA(NULL, 0, does_not_exist);
    /* XP: ERROR_NOT_SUPPORTED, NT351 and 9x: ERROR_INVALID_PARAMETER */
    ok( !res && ((GetLastError() == ERROR_NOT_SUPPORTED) || 
                 (GetLastError() == ERROR_INVALID_PARAMETER)),
        "returned %d with %d (expected '0' with ERROR_NOT_SUPPORTED or "
        "ERROR_INVALID_PARAMETER)\n", res, GetLastError());

}

/* ########################### */

static void test_ConfigurePort(void)
{
    DWORD   res;


    SetLastError(0xdeadbeef);
    res = ConfigurePortA(NULL, 0, NULL);
    RETURN_ON_DEACTIVATED_SPOOLER(res)
    /* NT: RPC_X_NULL_REF_POINTER, 9x: ERROR_INVALID_PARAMETER */
    ok( !res && ((GetLastError() == RPC_X_NULL_REF_POINTER) || 
                 (GetLastError() == ERROR_INVALID_PARAMETER)),
        "returned %d with %d (expected '0' with ERROR_NOT_SUPPORTED or "
        "ERROR_INVALID_PARAMETER)\n", res, GetLastError());

    SetLastError(0xdeadbeef);
    res = ConfigurePortA(NULL, 0, empty);
    /* Allowed only for (Printer-)Administrators */
    if (!res && (GetLastError() == ERROR_ACCESS_DENIED)) {
        trace(" skip tests (ACCESS_DENIED)\n");
        return;
    }
    /* XP: ERROR_NOT_SUPPORTED, NT351 and 9x: ERROR_INVALID_PARAMETER */
    ok( !res && ((GetLastError() == ERROR_NOT_SUPPORTED) || 
                 (GetLastError() == ERROR_INVALID_PARAMETER)),
        "returned %d with %d (expected '0' with ERROR_NOT_SUPPORTED or "
        "ERROR_INVALID_PARAMETER)\n", res, GetLastError());


    SetLastError(0xdeadbeef);
    res = ConfigurePortA(NULL, 0, does_not_exist);
    /* XP: ERROR_NOT_SUPPORTED, NT351 and 9x: ERROR_INVALID_PARAMETER */
    ok( !res && ((GetLastError() == ERROR_NOT_SUPPORTED) || 
                 (GetLastError() == ERROR_INVALID_PARAMETER)),
        "returned %d with %d (expected '0' with ERROR_NOT_SUPPORTED or "
        "ERROR_INVALID_PARAMETER)\n", res, GetLastError());


    /*  Testing-Results:
        - Case of Portnames is ignored 
        - Portname without ":" => NT: ERROR_NOT_SUPPORTED, 9x: Dialog comes up
        - Empty Servername (LPT1:) => NT: ERROR_NOT_SUPPORTED, 9x: Dialog comes up

        - Port not present =>  9x: ERROR_INVALID_PARAMETER, NT:ERROR_NOT_SUPPORTED
        - "FILE:" => 9x:Success, NT:ERROR_CANCELED
        - Cancel ("Local Port") => ERROR_CANCELED
        - Cancel ("Redirected Port") => Success
    */
    if (winetest_interactive > 0) {
        SetLastError(0xdeadbeef);
        res = ConfigurePortA(NULL, 0, portname_com1);
        trace("'%s' returned %d with %d\n", portname_com1, res, GetLastError());

        SetLastError(0xdeadbeef);
        res = ConfigurePortA(NULL, 0, portname_lpt1);
        trace("'%s' returned %d with %d\n", portname_lpt1, res, GetLastError());

        SetLastError(0xdeadbeef);
        res = ConfigurePortA(NULL, 0, portname_file);
        trace("'%s' returned %d with %d\n", portname_file, res, GetLastError());
    }
}

/* ########################### */

static void test_DeleteMonitor(void)
{
    MONITOR_INFO_2A         mi2a;
    struct monitor_entry  * entry = NULL;
    DWORD                   res;


    entry = find_installed_monitor();

    if (!entry) {
        trace("No usable Monitor found: Skip tests\n");
        return;
    }

    mi2a.pName = winetest;
    mi2a.pEnvironment = entry->env;
    mi2a.pDLLName = entry->dllname;

    /* Testing DeleteMonitor with real options */
    AddMonitorA(NULL, 2, (LPBYTE) &mi2a);

    SetLastError(MAGIC_DEAD);
    res = DeleteMonitorA(NULL, entry->env, winetest);
    ok(res, "returned %d with %d (expected '!= 0')\n", res, GetLastError());

    /* Delete the Monitor twice */
    SetLastError(MAGIC_DEAD);
    res = DeleteMonitorA(NULL, entry->env, winetest);
    /* NT: ERROR_UNKNOWN_PRINT_MONITOR (3000), 9x: ERROR_INVALID_PARAMETER (87) */
    ok( !res &&
        ((GetLastError() == ERROR_UNKNOWN_PRINT_MONITOR) ||
        (GetLastError() == ERROR_INVALID_PARAMETER)), 
        "returned %d with %d (expected '0' with: ERROR_UNKNOWN_PRINT_MONITOR"
        " or ERROR_INVALID_PARAMETER)\n", res, GetLastError());

    /* the environment */
    AddMonitorA(NULL, 2, (LPBYTE) &mi2a);
    SetLastError(MAGIC_DEAD);
    res = DeleteMonitorA(NULL, NULL, winetest);
    ok(res, "returned %d with %d (expected '!=0')\n", res, GetLastError());

    AddMonitorA(NULL, 2, (LPBYTE) &mi2a);
    SetLastError(MAGIC_DEAD);
    res = DeleteMonitorA(NULL, empty, winetest);
    ok(res, "returned %d with %d (expected '!=0')\n", res, GetLastError());

    AddMonitorA(NULL, 2, (LPBYTE) &mi2a);
    SetLastError(MAGIC_DEAD);
    res = DeleteMonitorA(NULL, invalid_env, winetest);
    ok(res, "returned %d with %d (expected '!=0')\n", res, GetLastError());

    /* the monitor-name */
    AddMonitorA(NULL, 2, (LPBYTE) &mi2a);
    SetLastError(MAGIC_DEAD);
    res = DeleteMonitorA(NULL, entry->env, NULL);
    /* NT: ERROR_INVALID_PARAMETER (87),  9x: ERROR_INVALID_NAME (123)*/
    ok( !res &&
        ((GetLastError() == ERROR_INVALID_PARAMETER) ||
        (GetLastError() == ERROR_INVALID_NAME)),
        "returned %d with %d (expected '0' with: ERROR_INVALID_PARAMETER or "
        "ERROR_INVALID_NAME)\n", res, GetLastError());

    AddMonitorA(NULL, 2, (LPBYTE) &mi2a);
    SetLastError(MAGIC_DEAD);
    res = DeleteMonitorA(NULL, entry->env, empty);
    /* NT: ERROR_INVALID_PARAMETER (87),  9x: ERROR_INVALID_NAME (123)*/
    ok( !res && 
        ((GetLastError() == ERROR_INVALID_PARAMETER) ||
        (GetLastError() == ERROR_INVALID_NAME)),
        "returned %d with %d (expected '0' with: ERROR_INVALID_PARAMETER or "
        "ERROR_INVALID_NAME)\n", res, GetLastError());

    AddMonitorA(NULL, 2, (LPBYTE) &mi2a);
    SetLastError(MAGIC_DEAD);
    res = DeleteMonitorA(empty, entry->env, winetest);
    ok(res, "returned %d with %d (expected '!=0')\n", res, GetLastError());

    /* cleanup */
    DeleteMonitorA(NULL, entry->env, winetest);
}

/* ########################### */

static void test_DeletePort(void)
{
    DWORD   res;

    SetLastError(0xdeadbeef);
    res = DeletePortA(NULL, 0, NULL);
    RETURN_ON_DEACTIVATED_SPOOLER(res)

    SetLastError(0xdeadbeef);
    res = DeletePortA(NULL, 0, empty);
    /* Allowed only for (Printer-)Administrators */
    if (!res && (GetLastError() == ERROR_ACCESS_DENIED)) {
        trace("skip tests (ACCESS_DENIED)\n");
        return;
    }
    /* XP: ERROR_NOT_SUPPORTED, NT351 and 9x: ERROR_INVALID_PARAMETER */
    ok( !res && ((GetLastError() == ERROR_NOT_SUPPORTED) || 
                 (GetLastError() == ERROR_INVALID_PARAMETER)),
        "returned %d with %d (expected '0' with ERROR_NOT_SUPPORTED or "
        "ERROR_INVALID_PARAMETER)\n", res, GetLastError());


    SetLastError(0xdeadbeef);
    res = DeletePortA(NULL, 0, does_not_exist);
    /* XP: ERROR_NOT_SUPPORTED, NT351 and 9x: ERROR_INVALID_PARAMETER */
    ok( !res && ((GetLastError() == ERROR_NOT_SUPPORTED) || 
                 (GetLastError() == ERROR_INVALID_PARAMETER)),
        "returned %d with %d (expected '0' with ERROR_NOT_SUPPORTED or "
        "ERROR_INVALID_PARAMETER)\n", res, GetLastError());

}

/* ########################### */

static void test_EnumForms(LPSTR pName)
{
    DWORD   res;
    HANDLE  hprinter = 0;
    LPBYTE  buffer;
    DWORD   cbBuf;
    DWORD   pcbNeeded;
    DWORD   pcReturned;
    DWORD   level;
  

    res = OpenPrinter(pName, &hprinter, NULL);
    RETURN_ON_DEACTIVATED_SPOOLER(res)
    if (!res || !hprinter)
    {
        /* Open the local Prinserver is not supported on win9x */
        if (pName) trace("Failed to open '%s', skiping the test\n", pName);
        return;
    }

    /* valid levels are 1 and 2 */
    for(level = 0; level < 4; level++) {
        cbBuf = 0xdeadbeef;
        pcReturned = 0xdeadbeef;
        SetLastError(0xdeadbeef);
        res = EnumFormsA(hprinter, level, NULL, 0, &cbBuf, &pcReturned);
       
        /* EnumForms is not implemented in win9x */
        if (!res && (GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)) continue;

        /* EnumForms for the Server not implemented on all NT-Versions */
        if (!res && (GetLastError() == ERROR_INVALID_HANDLE) && !pName) continue;

        /* Level 2 for EnumForms is not supported on all systems */
        if (!res && (GetLastError() == ERROR_INVALID_LEVEL) && (level == 2)) continue;

        /* use only a short test, when we test with an invalid level */
        if(!level || (level > 2)) {
            ok( (!res && (GetLastError() == ERROR_INVALID_LEVEL)) ||
                (res && (pcReturned == 0)),
                "(%d) returned %d with %d and 0x%08x (expected '0' with "
                "ERROR_INVALID_LEVEL or '!=0' and 0x0)\n",
                level, res, GetLastError(), pcReturned);
            continue;
        }        

        ok((!res) && (GetLastError() == ERROR_INSUFFICIENT_BUFFER),
            "(%d) returned %d with %d (expected '0' with "
            "ERROR_INSUFFICIENT_BUFFER)\n", level, res, GetLastError());

        buffer = HeapAlloc(GetProcessHeap(), 0, cbBuf *2);
        if (buffer == NULL) continue;

        SetLastError(0xdeadbeef);
        res = EnumFormsA(hprinter, level, buffer, cbBuf, &pcbNeeded, &pcReturned);
        ok(res, "(%d) returned %d with %d (expected '!=0')\n",
                level, res, GetLastError());
        /* We can dump the returned Data here */


        SetLastError(0xdeadbeef);
        res = EnumFormsA(hprinter, level, buffer, cbBuf+1, &pcbNeeded, &pcReturned);
        ok( res, "(%d) returned %d with %d (expected '!=0')\n",
            level, res, GetLastError());

        SetLastError(0xdeadbeef);
        res = EnumFormsA(hprinter, level, buffer, cbBuf-1, &pcbNeeded, &pcReturned);
        ok( !res && (GetLastError() == ERROR_INSUFFICIENT_BUFFER),
            "(%d) returned %d with %d (expected '0' with "
            "ERROR_INSUFFICIENT_BUFFER)\n", level, res, GetLastError());


        SetLastError(0xdeadbeef);
        res = EnumFormsA(hprinter, level, NULL, cbBuf, &pcbNeeded, &pcReturned);
        ok( !res && (GetLastError() == ERROR_INVALID_USER_BUFFER) ,
            "(%d) returned %d with %d (expected '0' with "
            "ERROR_INVALID_USER_BUFFER)\n", level, res, GetLastError());


        SetLastError(0xdeadbeef);
        res = EnumFormsA(hprinter, level, buffer, cbBuf, NULL, &pcReturned);
        ok( !res && (GetLastError() == RPC_X_NULL_REF_POINTER) ,
            "(%d) returned %d with %d (expected '0' with "
            "RPC_X_NULL_REF_POINTER)\n", level, res, GetLastError());

        SetLastError(0xdeadbeef);
        res = EnumFormsA(hprinter, level, buffer, cbBuf, &pcbNeeded, NULL);
        ok( !res && (GetLastError() == RPC_X_NULL_REF_POINTER) ,
            "(%d) returned %d with %d (expected '0' with "
            "RPC_X_NULL_REF_POINTER)\n", level, res, GetLastError());

        SetLastError(0xdeadbeef);
        res = EnumFormsA(0, level, buffer, cbBuf, &pcbNeeded, &pcReturned);
        ok( !res && (GetLastError() == ERROR_INVALID_HANDLE) ,
            "(%d) returned %d with %d (expected '0' with "
            "ERROR_INVALID_HANDLE)\n", level, res, GetLastError());

        HeapFree(GetProcessHeap(), 0, buffer);
    } /* for(level ... */

    ClosePrinter(hprinter);
}

/* ########################### */

static void test_EnumMonitors(void)
{
    DWORD   res;
    LPBYTE  buffer;
    DWORD   cbBuf;
    DWORD   pcbNeeded;
    DWORD   pcReturned;
    DWORD   level;

    /* valid levels are 1 and 2 */
    for(level = 0; level < 4; level++) {
        cbBuf = MAGIC_DEAD;
        pcReturned = MAGIC_DEAD;
        SetLastError(MAGIC_DEAD);
        res = EnumMonitorsA(NULL, level, NULL, 0, &cbBuf, &pcReturned);

        RETURN_ON_DEACTIVATED_SPOOLER(res)

        /* not implemented yet in wine */
        if (!res && (GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)) continue;


        /* use only a short test, when we test with an invalid level */
        if(!level || (level > 2)) {
            ok( (!res && (GetLastError() == ERROR_INVALID_LEVEL)) ||
                (res && (pcReturned == 0)),
                "(%d) returned %d with %d and 0x%08x (expected '0' with "
                "ERROR_INVALID_LEVEL or '!=0' and 0x0)\n",
                level, res, GetLastError(), pcReturned);
            continue;
        }        

        /* Level 2 is not supported on win9x */
        if (!res && (GetLastError() == ERROR_INVALID_LEVEL)) {
            trace("Level %d not supported, skipping tests\n", level);
            continue;
        }

        ok((!res) && (GetLastError() == ERROR_INSUFFICIENT_BUFFER),
            "(%d) returned %d with %d (expected '0' with "
            "ERROR_INSUFFICIENT_BUFFER)\n", level, res, GetLastError());

        if (!cbBuf) {
            trace("no valid buffer size returned, skipping tests\n");
            continue;
        }

        buffer = HeapAlloc(GetProcessHeap(), 0, cbBuf *2);
        if (buffer == NULL) continue;

        SetLastError(MAGIC_DEAD);
        pcbNeeded = MAGIC_DEAD;
        res = EnumMonitorsA(NULL, level, buffer, cbBuf, &pcbNeeded, &pcReturned);
        ok(res, "(%d) returned %d with %d (expected '!=0')\n",
                level, res, GetLastError());
        ok(pcbNeeded == cbBuf, "(%d) returned %d (expected %d)\n",
                level, pcbNeeded, cbBuf);
        /* We can validate the returned Data with the Registry here */


        SetLastError(MAGIC_DEAD);
        pcReturned = MAGIC_DEAD;
        pcbNeeded = MAGIC_DEAD;
        res = EnumMonitorsA(NULL, level, buffer, cbBuf+1, &pcbNeeded, &pcReturned);
        ok(res, "(%d) returned %d with %d (expected '!=0')\n", level,
                res, GetLastError());
        ok(pcbNeeded == cbBuf, "(%d) returned %d (expected %d)\n", level,
                pcbNeeded, cbBuf);

        SetLastError(MAGIC_DEAD);
        pcbNeeded = MAGIC_DEAD;
        res = EnumMonitorsA(NULL, level, buffer, cbBuf-1, &pcbNeeded, &pcReturned);
        ok( !res && (GetLastError() == ERROR_INSUFFICIENT_BUFFER),
            "(%d) returned %d with %d (expected '0' with "
            "ERROR_INSUFFICIENT_BUFFER)\n", level, res, GetLastError());

        ok(pcbNeeded == cbBuf, "(%d) returned %d (expected %d)\n", level,
                pcbNeeded, cbBuf);

/*
      Do not add the next test:
      w2k+:  RPC_X_NULL_REF_POINTER 
      NT3.5: ERROR_INVALID_USER_BUFFER
      win9x: crash in winspool.drv

      res = EnumMonitorsA(NULL, level, NULL, cbBuf, &pcbNeeded, &pcReturned);
*/

        SetLastError(MAGIC_DEAD);
        pcbNeeded = MAGIC_DEAD;
        pcReturned = MAGIC_DEAD;
        res = EnumMonitorsA(NULL, level, buffer, cbBuf, NULL, &pcReturned);
        ok( res || (!res && (GetLastError() == RPC_X_NULL_REF_POINTER)) ,
            "(%d) returned %d with %d (expected '!=0' or '0' with "
            "RPC_X_NULL_REF_POINTER)\n", level, res, GetLastError());

        pcbNeeded = MAGIC_DEAD;
        pcReturned = MAGIC_DEAD;
        SetLastError(MAGIC_DEAD);
        res = EnumMonitorsA(NULL, level, buffer, cbBuf, &pcbNeeded, NULL);
        ok( res || (!res && (GetLastError() == RPC_X_NULL_REF_POINTER)) ,
            "(%d) returned %d with %d (expected '!=0' or '0' with "
            "RPC_X_NULL_REF_POINTER)\n", level, res, GetLastError());

        HeapFree(GetProcessHeap(), 0, buffer);
    } /* for(level ... */
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

    /* valid levels are 1 and 2 */
    for(level = 0; level < 4; level++) {

        cbBuf = 0xdeadbeef;
        pcReturned = 0xdeadbeef;
        SetLastError(0xdeadbeef);
        res = EnumPortsA(NULL, level, NULL, 0, &cbBuf, &pcReturned);
        RETURN_ON_DEACTIVATED_SPOOLER(res)

        /* use only a short test, when we test with an invalid level */
        if(!level || (level > 2)) {
            /* NT: ERROR_INVALID_LEVEL, 9x: success */
            ok( (!res && (GetLastError() == ERROR_INVALID_LEVEL)) ||
                (res && (pcReturned == 0)),
                "(%d) returned %d with %d and 0x%08x (expected '0' with "
                "ERROR_INVALID_LEVEL or '!=0' and 0x0)\n",
                level, res, GetLastError(), pcReturned);
            continue;
        }        

        
        /* Level 2 is not supported on NT 3.x */
        if (!res && (GetLastError() == ERROR_INVALID_LEVEL)) {
            trace("Level %d not supported, skipping tests\n", level);
            continue;
        }

        ok((!res) && (GetLastError() == ERROR_INSUFFICIENT_BUFFER),
            "(%d) returned %d with %d (expected '0' with "
            "ERROR_INSUFFICIENT_BUFFER)\n", level, res, GetLastError());

        buffer = HeapAlloc(GetProcessHeap(), 0, cbBuf *2);
        if (buffer == NULL) continue;

        pcbNeeded = 0xdeadbeef;
        SetLastError(0xdeadbeef);
        res = EnumPortsA(NULL, level, buffer, cbBuf, &pcbNeeded, &pcReturned);
        ok(res, "(%d) returned %d with %d (expected '!=0')\n", level, res, GetLastError());
        ok(pcbNeeded == cbBuf, "(%d) returned %d (expected %d)\n", level, pcbNeeded, cbBuf);
        /* ToDo: Compare the returned Data with the Registry / "win.ini",[Ports] here */

        pcbNeeded = 0xdeadbeef;
        pcReturned = 0xdeadbeef;
        SetLastError(0xdeadbeef);
        res = EnumPortsA(NULL, level, buffer, cbBuf+1, &pcbNeeded, &pcReturned);
        ok(res, "(%d) returned %d with %d (expected '!=0')\n", level, res, GetLastError());
        ok(pcbNeeded == cbBuf, "(%d) returned %d (expected %d)\n", level, pcbNeeded, cbBuf);

        pcbNeeded = 0xdeadbeef;
        SetLastError(0xdeadbeef);
        res = EnumPortsA(NULL, level, buffer, cbBuf-1, &pcbNeeded, &pcReturned);
        ok( !res && (GetLastError() == ERROR_INSUFFICIENT_BUFFER),
            "(%d) returned %d with %d (expected '0' with "
            "ERROR_INSUFFICIENT_BUFFER)\n", level, res, GetLastError());
        ok(pcbNeeded == cbBuf, "(%d) returned %d (expected %d)\n", level, pcbNeeded, cbBuf);

        /*
          Do not add this test:
          res = EnumPortsA(NULL, level, NULL, cbBuf, &pcbNeeded, &pcReturned);
          w2k+:  RPC_X_NULL_REF_POINTER 
          NT3.5: ERROR_INVALID_USER_BUFFER
          win9x: crash in winspool.drv
         */

        SetLastError(0xdeadbeef);
        res = EnumPorts(NULL, level, buffer, cbBuf, NULL, &pcReturned);
        /* NT: RPC_X_NULL_REF_POINTER (1780),  9x: success */
        ok( (!res && (GetLastError() == RPC_X_NULL_REF_POINTER) ) ||
            ( res && (GetLastError() == ERROR_SUCCESS) ),
            "(%d) returned %d with %d (expected '0' with "
            "RPC_X_NULL_REF_POINTER or '!=0' with NO_ERROR)\n",
            level, res, GetLastError());


        SetLastError(0xdeadbeef);
        res = EnumPorts(NULL, level, buffer, cbBuf, &pcbNeeded, NULL);
        /* NT: RPC_X_NULL_REF_POINTER (1780),  9x: success */
        ok( (!res && (GetLastError() == RPC_X_NULL_REF_POINTER) ) ||
            ( res && (GetLastError() == ERROR_SUCCESS) ),
            "(%d) returned %d with %d (expected '0' with "
            "RPC_X_NULL_REF_POINTER or '!=0' with NO_ERROR)\n",
            level, res, GetLastError());

        HeapFree(GetProcessHeap(), 0, buffer);
    }
}

/* ########################### */

static void test_GetDefaultPrinter(void)
{
    BOOL    retval;
    DWORD   exact = DEFAULT_PRINTER_SIZE;
    DWORD   size;
    char    buffer[DEFAULT_PRINTER_SIZE];

    if (!pGetDefaultPrinterA)  return;
	/* only supported on NT like OSes starting with win2k */

    SetLastError(ERROR_SUCCESS);
    retval = pGetDefaultPrinterA(buffer, &exact);
    if (!retval || !exact || !strlen(buffer) ||
	(ERROR_SUCCESS != GetLastError())) {
	if ((ERROR_FILE_NOT_FOUND == GetLastError()) ||
	    (ERROR_INVALID_NAME == GetLastError()))
	    trace("this test requires a default printer to be set\n");
	else {
		ok( 0, "function call GetDefaultPrinterA failed unexpected!\n"
		"function returned %s\n"
		"last error 0x%08x\n"
		"returned buffer size 0x%08x\n"
		"returned buffer content %s\n",
		retval ? "true" : "false", GetLastError(), exact, buffer);
	}
	return;
    }
    SetLastError(ERROR_SUCCESS);
    retval = pGetDefaultPrinterA(NULL, NULL); 
    ok( !retval, "function result wrong! False expected\n");
    ok( ERROR_INVALID_PARAMETER == GetLastError(),
	"Last error wrong! ERROR_INVALID_PARAMETER expected, got 0x%08x\n",
	GetLastError());

    SetLastError(ERROR_SUCCESS);
    retval = pGetDefaultPrinterA(buffer, NULL); 
    ok( !retval, "function result wrong! False expected\n");
    ok( ERROR_INVALID_PARAMETER == GetLastError(),
	"Last error wrong! ERROR_INVALID_PARAMETER expected, got 0x%08x\n",
	GetLastError());

    SetLastError(ERROR_SUCCESS);
    size = 0;
    retval = pGetDefaultPrinterA(NULL, &size); 
    ok( !retval, "function result wrong! False expected\n");
    ok( ERROR_INSUFFICIENT_BUFFER == GetLastError(),
	"Last error wrong! ERROR_INSUFFICIENT_BUFFER expected, got 0x%08x\n",
	GetLastError());
    ok( size == exact, "Parameter size wrong! %d expected got %d\n",
	exact, size);

    SetLastError(ERROR_SUCCESS);
    size = DEFAULT_PRINTER_SIZE;
    retval = pGetDefaultPrinterA(NULL, &size); 
    ok( !retval, "function result wrong! False expected\n");
    ok( ERROR_INSUFFICIENT_BUFFER == GetLastError(),
	"Last error wrong! ERROR_INSUFFICIENT_BUFFER expected, got 0x%08x\n",
	GetLastError());
    ok( size == exact, "Parameter size wrong! %d expected got %d\n",
	exact, size);

    size = 0;
    retval = pGetDefaultPrinterA(buffer, &size); 
    ok( !retval, "function result wrong! False expected\n");
    ok( ERROR_INSUFFICIENT_BUFFER == GetLastError(),
	"Last error wrong! ERROR_INSUFFICIENT_BUFFER expected, got 0x%08x\n",
	GetLastError());
    ok( size == exact, "Parameter size wrong! %d expected got %d\n",
	exact, size);

    size = exact;
    retval = pGetDefaultPrinterA(buffer, &size); 
    ok( retval, "function result wrong! True expected\n");
    ok( size == exact, "Parameter size wrong! %d expected got %d\n",
	exact, size);
}

static void test_GetPrinterDriverDirectory(void)
{
    LPBYTE      buffer = NULL;
    DWORD       cbBuf = 0, pcbNeeded = 0;
    BOOL        res;


    SetLastError(MAGIC_DEAD);
    res = GetPrinterDriverDirectoryA( NULL, NULL, 1, NULL, 0, &cbBuf);
    trace("first call returned 0x%04x, with %d: buffer size 0x%08x\n",
    res, GetLastError(), cbBuf);

    RETURN_ON_DEACTIVATED_SPOOLER(res)
    ok((res == 0) && (GetLastError() == ERROR_INSUFFICIENT_BUFFER),
        "returned %d with lasterror=%d (expected '0' with "
        "ERROR_INSUFFICIENT_BUFFER)\n", res, GetLastError());

    if (!cbBuf) {
        trace("no valid buffer size returned, skipping tests\n");
        return;
    }

    buffer = HeapAlloc( GetProcessHeap(), 0, cbBuf*2);
    if (buffer == NULL)  return ;

    res = GetPrinterDriverDirectoryA(NULL, NULL, 1, buffer, cbBuf, &pcbNeeded);
    ok( res, "expected result != 0, got %d\n", res);
    ok( cbBuf == pcbNeeded, "pcbNeeded set to %d instead of %d\n",
                            pcbNeeded, cbBuf);

    res = GetPrinterDriverDirectoryA(NULL, NULL, 1, buffer, cbBuf*2, &pcbNeeded);
    ok( res, "expected result != 0, got %d\n", res);
    ok( cbBuf == pcbNeeded, "pcbNeeded set to %d instead of %d\n",
                            pcbNeeded, cbBuf);
 
    SetLastError(MAGIC_DEAD);
    res = GetPrinterDriverDirectoryA( NULL, NULL, 1, buffer, cbBuf-1, &pcbNeeded);
    ok( !res , "expected result == 0, got %d\n", res);
    ok( cbBuf == pcbNeeded, "pcbNeeded set to %d instead of %d\n",
                            pcbNeeded, cbBuf);
    
    ok( ERROR_INSUFFICIENT_BUFFER == GetLastError(),
        "last error set to %d instead of ERROR_INSUFFICIENT_BUFFER\n",
        GetLastError());

/*
    Do not add the next test:
    XPsp2: crash in this app, when the spooler is not running 
    NT3.5: ERROR_INVALID_USER_BUFFER
    win9x: ERROR_INVALID_PARAMETER

    pcbNeeded = MAGIC_DEAD;
    SetLastError(MAGIC_DEAD);
    res = GetPrinterDriverDirectoryA( NULL, NULL, 1, NULL, cbBuf, &pcbNeeded);
*/

    SetLastError(MAGIC_DEAD);
    res = GetPrinterDriverDirectoryA( NULL, NULL, 1, buffer, cbBuf, NULL);
    ok( (!res && RPC_X_NULL_REF_POINTER == GetLastError()) || res,
         "expected either result == 0 and "
         "last error == RPC_X_NULL_REF_POINTER or result != 0 "
         "got result %d and last error == %d\n", res, GetLastError());

    SetLastError(MAGIC_DEAD);
    res = GetPrinterDriverDirectoryA( NULL, NULL, 1, NULL, cbBuf, NULL);
    ok(res || (GetLastError() == RPC_X_NULL_REF_POINTER),
        "returned %d with %d (expected '!=0' or '0' with "
        "RPC_X_NULL_REF_POINTER)\n", res, GetLastError());
 
 
    /* with a valid buffer, but level is too large */
    buffer[0] = '\0';
    SetLastError(MAGIC_DEAD);
    res = GetPrinterDriverDirectoryA(NULL, NULL, 2, buffer, cbBuf, &pcbNeeded);

    /* Level not checked in win9x and wine:*/
    if((res != FALSE) && buffer[0])
    {
        trace("Level '2' not checked '%s'\n", buffer);
    }
    else
    {
        ok( !res && (GetLastError() == ERROR_INVALID_LEVEL),
        "returned %d with lasterror=%d (expected '0' with "
        "ERROR_INVALID_LEVEL)\n", res, GetLastError());
    }

    /* printing environments are case insensitive */
    /* "Windows 4.0" is valid for win9x and NT */
    buffer[0] = '\0';
    SetLastError(MAGIC_DEAD);
    res = GetPrinterDriverDirectoryA(NULL, env_win9x_case, 1, 
                                        buffer, cbBuf*2, &pcbNeeded);

    if(!res && (GetLastError() == ERROR_INSUFFICIENT_BUFFER)) {
        cbBuf = pcbNeeded;
        buffer = HeapReAlloc(GetProcessHeap(), 0, buffer, cbBuf*2);
        if (buffer == NULL)  return ;

        SetLastError(MAGIC_DEAD);
        res = GetPrinterDriverDirectoryA(NULL, env_win9x_case, 1, 
                                        buffer, cbBuf*2, &pcbNeeded);
    }

    ok(res && buffer[0], "returned %d with "
        "lasterror=%d and len=%d (expected '1' with 'len > 0')\n", 
        res, GetLastError(), lstrlenA((char *)buffer));

    buffer[0] = '\0';
    SetLastError(MAGIC_DEAD);
    res = GetPrinterDriverDirectoryA(NULL, env_x86, 1, 
                                        buffer, cbBuf*2, &pcbNeeded);

    if(!res && (GetLastError() == ERROR_INSUFFICIENT_BUFFER)) {
        cbBuf = pcbNeeded;
        buffer = HeapReAlloc(GetProcessHeap(), 0, buffer, cbBuf*2);
        if (buffer == NULL)  return ;

        buffer[0] = '\0';
        SetLastError(MAGIC_DEAD);
        res = GetPrinterDriverDirectoryA(NULL, env_x86, 1, 
                                        buffer, cbBuf*2, &pcbNeeded);
    }

    /* "Windows NT x86" is invalid for win9x */
    ok( (res && buffer[0]) ||
        (!res && (GetLastError() == ERROR_INVALID_ENVIRONMENT)), 
        "returned %d with lasterror=%d and len=%d (expected '!= 0' with "
        "'len > 0' or '0' with ERROR_INVALID_ENVIRONMENT)\n",
        res, GetLastError(), lstrlenA((char *)buffer));

    /* A setup program (PDFCreator_0.8.0) use empty strings */
    SetLastError(MAGIC_DEAD);
    res = GetPrinterDriverDirectoryA(empty, empty, 1, buffer, cbBuf*2, &pcbNeeded);
    ok(res, "returned %d with %d (expected '!=0')\n", res, GetLastError() );

    SetLastError(MAGIC_DEAD);
    res = GetPrinterDriverDirectoryA(NULL, empty, 1, buffer, cbBuf*2, &pcbNeeded);
    ok(res, "returned %d with %d (expected '!=0')\n", res, GetLastError() );

    SetLastError(MAGIC_DEAD);
    res = GetPrinterDriverDirectoryA(empty, NULL, 1, buffer, cbBuf*2, &pcbNeeded);
    ok(res, "returned %d with %d (expected '!=0')\n", res, GetLastError() );

    HeapFree( GetProcessHeap(), 0, buffer);
}

/* ##### */

static void test_GetPrintProcessorDirectory(void)
{
    LPBYTE      buffer = NULL;
    DWORD       cbBuf = 0;
    DWORD       pcbNeeded = 0;
    BOOL        res;


    SetLastError(0xdeadbeef);
    res = GetPrintProcessorDirectoryA(NULL, NULL, 1, NULL, 0, &cbBuf);
    /* The deactivated Spooler is caught here on NT3.51 */
    RETURN_ON_DEACTIVATED_SPOOLER(res)
    ok( !res && (GetLastError() == ERROR_INSUFFICIENT_BUFFER),
        "returned %d with %d (expected '0' with ERROR_INSUFFICIENT_BUFFER)\n",
        res, GetLastError());

    buffer = HeapAlloc(GetProcessHeap(), 0, cbBuf*2);
    if(buffer == NULL)  return;

    buffer[0] = '\0';
    SetLastError(0xdeadbeef);
    res = GetPrintProcessorDirectoryA(NULL, NULL, 1, buffer, cbBuf, &pcbNeeded);
    ok(res, "returned %d with %d (expected '!= 0')\n", res, GetLastError());

    SetLastError(0xdeadbeef);
    buffer[0] = '\0';
    res = GetPrintProcessorDirectoryA(NULL, NULL, 1, buffer, cbBuf*2, &pcbNeeded);
    ok(res, "returned %d with %d (expected '!= 0')\n", res, GetLastError());
 
    /* Buffer to small */
    buffer[0] = '\0';
    SetLastError(0xdeadbeef);
    res = GetPrintProcessorDirectoryA( NULL, NULL, 1, buffer, cbBuf-1, &pcbNeeded);
    ok( !res && (GetLastError() == ERROR_INSUFFICIENT_BUFFER),
        "returned %d with %d (expected '0' with ERROR_INSUFFICIENT_BUFFER)\n",
        res, GetLastError());

    if (0)
    {
    /* XPsp2: the program will crash here, when the spooler is not running  */
    /*        GetPrinterDriverDirectory has the same bug */
    pcbNeeded = 0;
    SetLastError(0xdeadbeef);
    res = GetPrintProcessorDirectoryA( NULL, NULL, 1, NULL, cbBuf, &pcbNeeded);
    }

    buffer[0] = '\0';
    SetLastError(0xdeadbeef);
    res = GetPrintProcessorDirectoryA( NULL, NULL, 1, buffer, cbBuf, NULL);
    /* NT: RPC_X_NULL_REF_POINTER, 9x: res != 0  */
    ok( res || (GetLastError() == RPC_X_NULL_REF_POINTER),
        "returned %d with %d (expected '!= 0' or '0' with "
        "RPC_X_NULL_REF_POINTER)\n", res, GetLastError());


    buffer[0] = '\0';
    SetLastError(0xdeadbeef);
    res = GetPrintProcessorDirectoryA( NULL, NULL, 1, NULL, cbBuf, NULL);
    /* NT: RPC_X_NULL_REF_POINTER, 9x: res != 0  */
    ok( res || (GetLastError() == RPC_X_NULL_REF_POINTER),
        "returned %d with %d (expected '!= 0' or '0' with "
        "RPC_X_NULL_REF_POINTER)\n", res, GetLastError());

 
    /* with a valid buffer, but level is invalid */
    buffer[0] = '\0';
    SetLastError(0xdeadbeef);
    res = GetPrintProcessorDirectoryA(NULL, NULL, 2, buffer, cbBuf, &pcbNeeded);
    if (res && buffer[0])
    {
        /* Level is ignored in win9x*/
        trace("invalid level (2) was ignored\n");
    }
    else
    {
        ok( !res && (GetLastError() == ERROR_INVALID_LEVEL),
            "returned %d with %d (expected '0' with ERROR_INVALID_LEVEL)\n",
            res, GetLastError());
    }

    /* Empty environment is the same as the default environment */
    buffer[0] = '\0';
    SetLastError(0xdeadbeef);
    res = GetPrintProcessorDirectoryA(NULL, empty, 1, buffer, cbBuf*2, &pcbNeeded);
    ok(res, "returned %d with %d (expected '!= 0')\n", res, GetLastError());

    /* "Windows 4.0" is valid for win9x and NT */
    buffer[0] = '\0';
    SetLastError(0xdeadbeef);
    res = GetPrintProcessorDirectoryA(NULL, env_win9x_case, 1, buffer, cbBuf*2, &pcbNeeded);
    ok(res, "returned %d with %d (expected '!= 0')\n", res, GetLastError());


    /* "Windows NT x86" is invalid for win9x */
    buffer[0] = '\0';
    SetLastError(0xdeadbeef);
    res = GetPrintProcessorDirectoryA(NULL, env_x86, 1, buffer, cbBuf*2, &pcbNeeded);
    ok( res || (GetLastError() == ERROR_INVALID_ENVIRONMENT), 
        "returned %d with %d (expected '!= 0' or '0' with "
        "ERROR_INVALID_ENVIRONMENT)\n", res, GetLastError());

    /* invalid on all Systems */
    buffer[0] = '\0';
    SetLastError(0xdeadbeef);
    res = GetPrintProcessorDirectoryA(NULL, invalid_env, 1, buffer, cbBuf*2, &pcbNeeded);
    ok( !res && (GetLastError() == ERROR_INVALID_ENVIRONMENT), 
        "returned %d with %d (expected '0' with ERROR_INVALID_ENVIRONMENT)\n",
        res, GetLastError());

    /* Empty servername is the same as the local computer */
    buffer[0] = '\0';
    SetLastError(0xdeadbeef);
    res = GetPrintProcessorDirectoryA(empty, NULL, 1, buffer, cbBuf*2, &pcbNeeded);
    ok(res, "returned %d with %d (expected '!= 0')\n", res, GetLastError());

    /* invalid on all Systems */
    buffer[0] = '\0';
    SetLastError(0xdeadbeef);
    res = GetPrintProcessorDirectoryA(server_does_not_exist, NULL, 1, buffer, cbBuf*2, &pcbNeeded);
    ok( !res && (GetLastError() == ERROR_INVALID_PARAMETER), 
        "returned %d with %d (expected '0' with ERROR_INVALID_PARAMETER)\n",
        res, GetLastError());

    HeapFree(GetProcessHeap(), 0, buffer);
}

/* ##### */

static void test_OpenPrinter(void)
{
    PRINTER_DEFAULTSA   defaults;
    HANDLE              hprinter;
    DWORD               res;

    SetLastError(MAGIC_DEAD);
    res = OpenPrinter(NULL, NULL, NULL);    
    /* The deactivated Spooler is caught here on NT3.51 */
    RETURN_ON_DEACTIVATED_SPOOLER(res)
    ok(!res && (GetLastError() == ERROR_INVALID_PARAMETER),
        "returned %d with %d (expected '0' with ERROR_INVALID_PARAMETER)\n",
        res, GetLastError());


    /* Get Handle for the local Printserver (NT only)*/
    hprinter = (HANDLE) MAGIC_DEAD;
    SetLastError(MAGIC_DEAD);
    res = OpenPrinter(NULL, &hprinter, NULL);
    /* The deactivated Spooler is caught here on XPsp2 */
    RETURN_ON_DEACTIVATED_SPOOLER(res)
    ok(res || (!res && GetLastError() == ERROR_INVALID_PARAMETER),
        "returned %d with %d (expected '!=0' or '0' with ERROR_INVALID_PARAMETER)\n",
        res, GetLastError());
    if(res) {
        ClosePrinter(hprinter);

        defaults.pDatatype=NULL;
        defaults.pDevMode=NULL;

        defaults.DesiredAccess=0;
        hprinter = (HANDLE) MAGIC_DEAD;
        SetLastError(MAGIC_DEAD);
        res = OpenPrinter(NULL, &hprinter, &defaults);
        ok(res, "returned %d with %d (expected '!=0')\n", res, GetLastError());
        if (res) ClosePrinter(hprinter);

        defaults.DesiredAccess=-1;
        hprinter = (HANDLE) MAGIC_DEAD;
        SetLastError(MAGIC_DEAD);
        res = OpenPrinter(NULL, &hprinter, &defaults);
        todo_wine {
        ok(!res && GetLastError() == ERROR_ACCESS_DENIED,
            "returned %d with %d (expected '0' with ERROR_ACCESS_DENIED)\n", 
            res, GetLastError());
        }
        if (res) ClosePrinter(hprinter);

    }


    if (local_server != NULL) {
        hprinter = (HANDLE) 0xdeadbeef;
        SetLastError(0xdeadbeef);
        res = OpenPrinter(local_server, &hprinter, NULL);
        ok(res || (!res && GetLastError() == ERROR_INVALID_PARAMETER),
            "returned %d with %d (expected '!=0' or '0' with ERROR_INVALID_PARAMETER)\n",
            res, GetLastError());
        if(res) ClosePrinter(hprinter);
    }

    /* Invalid Printername */
    hprinter = (HANDLE) MAGIC_DEAD;
    SetLastError(MAGIC_DEAD);
    res = OpenPrinter(illegal_name, &hprinter, NULL);
    ok(!res && ((GetLastError() == ERROR_INVALID_PRINTER_NAME) || 
                (GetLastError() == ERROR_INVALID_PARAMETER) ),
       "returned %d with %d (expected '0' with: ERROR_INVALID_PARAMETER or"
       "ERROR_INVALID_PRINTER_NAME)\n", res, GetLastError());
    if(res) ClosePrinter(hprinter);

    hprinter = (HANDLE) MAGIC_DEAD;
    SetLastError(MAGIC_DEAD);
    res = OpenPrinter(empty, &hprinter, NULL);
    /* NT: ERROR_INVALID_PRINTER_NAME,  9x: ERROR_INVALID_PARAMETER */
    ok( !res &&
        ((GetLastError() == ERROR_INVALID_PRINTER_NAME) || 
        (GetLastError() == ERROR_INVALID_PARAMETER) ),
        "returned %d with %d (expected '0' with: ERROR_INVALID_PRINTER_NAME"
        " or ERROR_INVALID_PARAMETER)\n", res, GetLastError());
    if(res) ClosePrinter(hprinter);


    /* Get Handle for the default Printer */
    if (default_printer)
    {
        hprinter = (HANDLE) MAGIC_DEAD;
        SetLastError(MAGIC_DEAD);
        res = OpenPrinter(default_printer, &hprinter, NULL);
        if((!res) && (GetLastError() == RPC_S_SERVER_UNAVAILABLE))
        {
            trace("The Service 'Spooler' is required for '%s'\n", default_printer);
            return;
        }
        ok(res, "returned %d with %d (expected '!=0')\n", res, GetLastError());
        if(res) ClosePrinter(hprinter);

        SetLastError(MAGIC_DEAD);
        res = OpenPrinter(default_printer, NULL, NULL);
        /* NT: FALSE with ERROR_INVALID_PARAMETER, 9x: TRUE */
        ok(res || (GetLastError() == ERROR_INVALID_PARAMETER),
            "returned %d with %d (expected '!=0' or '0' with "
            "ERROR_INVALID_PARAMETER)\n", res, GetLastError());

        defaults.pDatatype=NULL;
        defaults.pDevMode=NULL;
        defaults.DesiredAccess=0;

        hprinter = (HANDLE) MAGIC_DEAD;
        SetLastError(MAGIC_DEAD);
        res = OpenPrinter(default_printer, &hprinter, &defaults);
        ok(res || GetLastError() == ERROR_ACCESS_DENIED,
            "returned %d with %d (expected '!=0' or '0' with "
            "ERROR_ACCESS_DENIED)\n", res, GetLastError());
        if(res) ClosePrinter(hprinter);

        defaults.pDatatype = empty;

        hprinter = (HANDLE) MAGIC_DEAD;
        SetLastError(MAGIC_DEAD);
        res = OpenPrinter(default_printer, &hprinter, &defaults);
        /* stop here, when a remote Printserver has no RPC-Service running */
        RETURN_ON_DEACTIVATED_SPOOLER(res)
        ok(res || ((GetLastError() == ERROR_INVALID_DATATYPE) ||
                   (GetLastError() == ERROR_ACCESS_DENIED)),
            "returned %d with %d (expected '!=0' or '0' with: "
            "ERROR_INVALID_DATATYPE or ERROR_ACCESS_DENIED)\n",
            res, GetLastError());
        if(res) ClosePrinter(hprinter);


        defaults.pDatatype=NULL;
        defaults.DesiredAccess=PRINTER_ACCESS_USE;

        hprinter = (HANDLE) MAGIC_DEAD;
        SetLastError(MAGIC_DEAD);
        res = OpenPrinter(default_printer, &hprinter, &defaults);
        ok(res || GetLastError() == ERROR_ACCESS_DENIED,
            "returned %d with %d (expected '!=0' or '0' with "
            "ERROR_ACCESS_DENIED)\n", res, GetLastError());
        if(res) ClosePrinter(hprinter);


        defaults.DesiredAccess=PRINTER_ALL_ACCESS;
        hprinter = (HANDLE) MAGIC_DEAD;
        SetLastError(MAGIC_DEAD);
        res = OpenPrinter(default_printer, &hprinter, &defaults);
        ok(res || GetLastError() == ERROR_ACCESS_DENIED,
            "returned %d with %d (expected '!=0' or '0' with "
            "ERROR_ACCESS_DENIED)\n", res, GetLastError());
        if(res) ClosePrinter(hprinter);
    }

}


static void test_SetDefaultPrinter(void)
{
    DWORD   res;
    DWORD   size = DEFAULT_PRINTER_SIZE;
    CHAR    buffer[DEFAULT_PRINTER_SIZE];
    CHAR    org_value[DEFAULT_PRINTER_SIZE];


    if (!pSetDefaultPrinterA)  return;
	/* only supported on win2k and above */

    /* backup the original value */
    org_value[0] = '\0';
    SetLastError(MAGIC_DEAD);
    res = GetProfileStringA("windows", "device", NULL, org_value, size);

    /* first part: with the default Printer */
    SetLastError(MAGIC_DEAD);
    res = pSetDefaultPrinterA("no_printer_with_this_name");

    RETURN_ON_DEACTIVATED_SPOOLER(res)
    /* spooler is running or we have no spooler here*/

    /* Not implemented in wine */
    if (!res && (GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)) {
        trace("SetDefaultPrinterA() not implemented yet.\n");
        return;
    }

    ok(!res && (GetLastError() == ERROR_INVALID_PRINTER_NAME),
        "returned %d with %d (expected '0' with "
        "ERROR_INVALID_PRINTER_NAME)\n", res, GetLastError());

    WriteProfileStringA("windows", "device", org_value);
    SetLastError(MAGIC_DEAD);
    res = pSetDefaultPrinterA("");
    ok(res || (!res && (GetLastError() == ERROR_INVALID_PRINTER_NAME)),
        "returned %d with %d (expected '!=0' or '0' with "
        "ERROR_INVALID_PRINTER_NAME)\n", res, GetLastError());

    WriteProfileStringA("windows", "device", org_value);
    SetLastError(MAGIC_DEAD);
    res = pSetDefaultPrinterA(NULL);
    ok(res || (!res && (GetLastError() == ERROR_INVALID_PRINTER_NAME)),
        "returned %d with %d (expected '!=0' or '0' with "
        "ERROR_INVALID_PRINTER_NAME)\n", res, GetLastError());

    WriteProfileStringA("windows", "device", org_value);
    SetLastError(MAGIC_DEAD);
    res = pSetDefaultPrinterA(default_printer);
    ok(res || (!res && (GetLastError() == ERROR_INVALID_PRINTER_NAME)),
        "returned %d with %d (expected '!=0' or '0' with "
        "ERROR_INVALID_PRINTER_NAME)\n", res, GetLastError());


    /* second part: always without a default Printer */
    WriteProfileStringA("windows", "device", NULL);    
    SetLastError(MAGIC_DEAD);
    res = pSetDefaultPrinterA("no_printer_with_this_name");

    ok(!res && (GetLastError() == ERROR_INVALID_PRINTER_NAME),
        "returned %d with %d (expected '0' with "
        "ERROR_INVALID_PRINTER_NAME)\n", res, GetLastError());

    WriteProfileStringA("windows", "device", NULL);    
    SetLastError(MAGIC_DEAD);
    res = pSetDefaultPrinterA("");
    /* we get ERROR_INVALID_PRINTER_NAME when no printer is installed */
    ok(res || (!res && (GetLastError() == ERROR_INVALID_PRINTER_NAME)),
         "returned %d with %d (expected '!=0' or '0' with "
         "ERROR_INVALID_PRINTER_NAME)\n", res, GetLastError());

    WriteProfileStringA("windows", "device", NULL);    
    SetLastError(MAGIC_DEAD);
    res = pSetDefaultPrinterA(NULL);
    /* we get ERROR_INVALID_PRINTER_NAME when no printer is installed */
    ok(res || (!res && (GetLastError() == ERROR_INVALID_PRINTER_NAME)),
        "returned %d with %d (expected '!=0' or '0' with "
        "ERROR_INVALID_PRINTER_NAME)\n", res, GetLastError());

    WriteProfileStringA("windows", "device", NULL);    
    SetLastError(MAGIC_DEAD);
    res = pSetDefaultPrinterA(default_printer);
    ok(res || (!res && (GetLastError() == ERROR_INVALID_PRINTER_NAME)),
        "returned %d with %d (expected '!=0' or '0' with "
        "ERROR_INVALID_PRINTER_NAME)\n", res, GetLastError());

    /* restore the original value */
    res = pSetDefaultPrinterA(default_printer);          /* the nice way */
    WriteProfileStringA("windows", "device", org_value); /* the old way */

    buffer[0] = '\0';
    SetLastError(MAGIC_DEAD);
    res = GetProfileStringA("windows", "device", NULL, buffer, size);
    ok(!lstrcmpA(org_value, buffer), "'%s' (expected '%s')\n", buffer, org_value);

}

static void test_GetPrinterDriver(void)
{
    HANDLE hprn;
    BOOL ret;
    BYTE *buf;
    INT level;
    DWORD needed, filled;

    if (!default_printer)
    {
        trace("There is no default printer installed, skiping the test\n");
        return;
    }

    hprn = 0;
    ret = OpenPrinter(default_printer, &hprn, NULL);
    if (!ret)
    {
        trace("There is no printers installed, skiping the test\n");
        return;
    }
    ok(hprn != 0, "wrong hprn %p\n", hprn);

    for (level = -1; level <= 7; level++)
    {
        SetLastError(0xdeadbeef);
        needed = (DWORD)-1;
        ret = GetPrinterDriver(hprn, NULL, level, NULL, 0, &needed);
        ok(!ret, "level %d: GetPrinterDriver should fail\n", level);
        if (level >= 1 && level <= 6)
        {
            /* Not all levels are supported on all Windows-Versions */
            if(GetLastError() == ERROR_INVALID_LEVEL) continue;
            ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "wrong error %d\n", GetLastError());
            ok(needed > 0,"not expected needed buffer size %d\n", needed);
        }
        else
        {
            /* ERROR_OUTOFMEMORY found on win9x */
            ok( ((GetLastError() == ERROR_INVALID_LEVEL) ||
                 (GetLastError() == ERROR_OUTOFMEMORY)),
                "%d: returned %d with %d (expected '0' with: "
                "ERROR_INVALID_LEVEL or ERROR_OUTOFMEMORY)\n",
                level, ret, GetLastError());
            /* needed is modified in win9x. The modified Value depends on the
               default Printer. testing for "needed == (DWORD)-1" will fail */
            continue;
        }

        buf = HeapAlloc(GetProcessHeap(), 0, needed);

        SetLastError(0xdeadbeef);
        filled = -1;
        ret = GetPrinterDriver(hprn, NULL, level, buf, needed, &filled);
        ok(ret, "level %d: GetPrinterDriver error %d\n", level, GetLastError());
        ok(needed == filled, "needed %d != filled %d\n", needed, filled);

        if (level == 2)
        {
            DRIVER_INFO_2 *di_2 = (DRIVER_INFO_2 *)buf;
            DWORD calculated = sizeof(*di_2);

            /* MSDN is wrong: The Drivers on the win9x-CD's have cVersion=0x0400
               NT351: 1, NT4.0+w2k(Kernelmode): 2, w2k and above(Usermode): 3  */
            ok((di_2->cVersion >= 0 && di_2->cVersion <= 3) ||
                (di_2->cVersion == 0x0400), "di_2->cVersion = %d\n", di_2->cVersion);
            ok(di_2->pName != NULL, "not expected NULL ptr\n");
            ok(di_2->pEnvironment != NULL, "not expected NULL ptr\n");
            ok(di_2->pDriverPath != NULL, "not expected NULL ptr\n");
            ok(di_2->pDataFile != NULL, "not expected NULL ptr\n");
            ok(di_2->pConfigFile != NULL, "not expected NULL ptr\n");

            trace("cVersion %d\n", di_2->cVersion);
            trace("pName %s\n", di_2->pName);
            calculated += strlen(di_2->pName) + 1;
            trace("pEnvironment %s\n", di_2->pEnvironment);
            calculated += strlen(di_2->pEnvironment) + 1;
            trace("pDriverPath %s\n", di_2->pDriverPath);
            calculated += strlen(di_2->pDriverPath) + 1;
            trace("pDataFile %s\n", di_2->pDataFile);
            calculated += strlen(di_2->pDataFile) + 1;
            trace("pConfigFile %s\n", di_2->pConfigFile);
            calculated += strlen(di_2->pConfigFile) + 1;

            /* XP allocates memory for both ANSI and unicode names */
            ok(filled >= calculated,"calculated %d != filled %d\n", calculated, filled);
        }

        HeapFree(GetProcessHeap(), 0, buf);
    }

    SetLastError(0xdeadbeef);
    ret = ClosePrinter(hprn);
    ok(ret, "ClosePrinter error %d\n", GetLastError());
}

static void test_DEVMODE(const DEVMODE *dm, LONG dmSize, LPCSTR exp_prn_name)
{
    /* On NT3.51, some fields in DEVMODE are empty/zero
      (dmDeviceName, dmSpecVersion, dmDriverVersion and dmDriverExtra)
       We skip the Tests on this Platform */
    if (dm->dmSpecVersion || dm->dmDriverVersion || dm->dmDriverExtra) {
    /* The 0-terminated Printername can be larger (MAX_PATH) than CCHDEVICENAME */
        ok(!strncmp(exp_prn_name, (LPCSTR)dm->dmDeviceName, CCHDEVICENAME -1),
            "expected '%s', got '%s'\n", exp_prn_name, dm->dmDeviceName);
        ok(dm->dmSize + dm->dmDriverExtra == dmSize,
            "%u != %d\n", dm->dmSize + dm->dmDriverExtra, dmSize);
    }
    trace("dmFields %08x\n", dm->dmFields);
}

static void test_DocumentProperties(void)
{
    HANDLE hprn;
    LONG dm_size, ret;
    DEVMODE *dm;

    if (!default_printer)
    {
        trace("There is no default printer installed, skiping the test\n");
        return;
    }

    hprn = 0;
    ret = OpenPrinter(default_printer, &hprn, NULL);
    if (!ret)
    {
        trace("There is no printers installed, skiping the test\n");
        return;
    }
    ok(hprn != 0, "wrong hprn %p\n", hprn);

    dm_size = DocumentProperties(0, hprn, NULL, NULL, NULL, 0);
    trace("DEVMODE required size %d\n", dm_size);
    ok(dm_size >= sizeof(DEVMODE), "unexpected DocumentProperties ret value %d\n", dm_size);

    dm = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dm_size);

    ret = DocumentProperties(0, hprn, NULL, dm, dm, DM_OUT_BUFFER);
    ok(ret == IDOK, "DocumentProperties ret value %d != expected IDOK\n", ret);

    test_DEVMODE(dm, dm_size, default_printer);

    HeapFree(GetProcessHeap(), 0, dm);

    SetLastError(0xdeadbeef);
    ret = ClosePrinter(hprn);
    ok(ret, "ClosePrinter error %d\n", GetLastError());
}

static void test_EnumPrinters(void)
{
    DWORD neededA, neededW, num;
    DWORD ret;

    SetLastError(0xdeadbeef);
    neededA = -1;
    ret = EnumPrintersA(PRINTER_ENUM_LOCAL, NULL, 2, NULL, 0, &neededA, &num);
    RETURN_ON_DEACTIVATED_SPOOLER(ret)
    if (!ret)
    {
        /* We have 1 or more printers */
        ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "gle %d\n", GetLastError());
        ok(neededA > 0, "Expected neededA to show the number of needed bytes\n");
    }
    else
    {
        /* We don't have any printers defined */
        ok(GetLastError() == S_OK, "gle %d\n", GetLastError());
        ok(neededA == 0, "Expected neededA to be zero\n");
    }
    ok(num == 0, "num %d\n", num);

    SetLastError(0xdeadbeef);
    neededW = -1;
    ret = EnumPrintersW(PRINTER_ENUM_LOCAL, NULL, 2, NULL, 0, &neededW, &num);
    /* EnumPrintersW is not supported on all platforms */
    if (!ret && (GetLastError() == ERROR_CALL_NOT_IMPLEMENTED))
    {
        trace("EnumPrintersW is not implemented, skipping some tests\n");
        return;
    }

    if (!ret)
    {
        /* We have 1 or more printers */
        ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "gle %d\n", GetLastError());
        ok(neededW > 0, "Expected neededW to show the number of needed bytes\n");
    }
    else
    {
        /* We don't have any printers defined */
        ok(GetLastError() == S_OK, "gle %d\n", GetLastError());
        ok(neededW == 0, "Expected neededW to be zero\n");
    }
    ok(num == 0, "num %d\n", num);

    /* Outlook2003 relies on the buffer size returned by EnumPrintersA being big enough
       to hold the buffer returned by EnumPrintersW */
    ok(neededA == neededW, "neededA %d neededW %d\n", neededA, neededW);
}

START_TEST(info)
{
    hwinspool = GetModuleHandleA("winspool.drv");
    pGetDefaultPrinterA = (void *) GetProcAddress(hwinspool, "GetDefaultPrinterA");
    pSetDefaultPrinterA = (void *) GetProcAddress(hwinspool, "SetDefaultPrinterA");

    find_default_printer();
    find_local_server();

    test_AddMonitor();
    test_AddPort();
    test_ConfigurePort();
    test_DeleteMonitor();
    test_DeletePort();
    test_DocumentProperties();
    test_EnumForms(NULL);
    if (default_printer) test_EnumForms(default_printer);
    test_EnumMonitors(); 
    test_EnumPorts();
    test_GetDefaultPrinter();
    test_GetPrinterDriverDirectory();
    test_GetPrintProcessorDirectory();
    test_OpenPrinter();
    test_GetPrinterDriver();
    test_SetDefaultPrinter();

    test_EnumPrinters();
}
