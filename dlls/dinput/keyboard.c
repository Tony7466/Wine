/*		DirectInput Keyboard device
 *
 * Copyright 1998 Marcus Meissner
 * Copyright 1998,1999 Lionel Ulmer
 * Copyright 2000-2001 TransGaming Technologies Inc.
 * Copyright 2005 Raphael Junqueira
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
#include "wine/port.h"

#include <stdarg.h>
#include <string.h>
#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winerror.h"
#include "dinput.h"

#include "dinput_private.h"
#include "device_private.h"
#include "wine/debug.h"
#include "wine/unicode.h"

WINE_DEFAULT_DEBUG_CHANNEL(dinput);

#define WINE_DINPUT_KEYBOARD_MAX_KEYS 256

static const IDirectInputDevice8AVtbl SysKeyboardAvt;
static const IDirectInputDevice8WVtbl SysKeyboardWvt;

typedef struct SysKeyboardImpl SysKeyboardImpl;
struct SysKeyboardImpl
{
    struct IDirectInputDevice2AImpl base;

    IDirectInputImpl*           dinput;
};

static SysKeyboardImpl* current_lock = NULL; 
/* Today's acquired device
 * FIXME: currently this can be only one.
 * Maybe this should be a linked list or st.
 * I don't know what the rules are for multiple acquired keyboards,
 * but 'DI_LOSTFOCUS' and 'DI_UNACQUIRED' exist for a reason.
*/

static BYTE DInputKeyState[WINE_DINPUT_KEYBOARD_MAX_KEYS]; /* array for 'GetDeviceState' */

static LRESULT CALLBACK KeyboardCallback( int code, WPARAM wparam, LPARAM lparam )
{
    SysKeyboardImpl *This = (SysKeyboardImpl *)current_lock;
    int dik_code;
    KBDLLHOOKSTRUCT *hook = (KBDLLHOOKSTRUCT *)lparam;
    BYTE new_diks;

    TRACE("(%d,%ld,%ld)\n", code, wparam, lparam);

    /* returns now if not HC_ACTION */
    if (code != HC_ACTION) return CallNextHookEx(0, code, wparam, lparam);
  
    dik_code = hook->scanCode & 0xff;
    if (hook->flags & LLKHF_EXTENDED) dik_code |= 0x80;

    new_diks = hook->flags & LLKHF_UP ? 0 : 0x80;

    /* returns now if key event already known */
    if (new_diks == DInputKeyState[dik_code])
        return CallNextHookEx(0, code, wparam, lparam);

    DInputKeyState[dik_code] = new_diks;
    TRACE(" setting %02X to %02X\n", dik_code, DInputKeyState[dik_code]);
      
    dik_code = id_to_offset(&This->base.data_format, DIDFT_MAKEINSTANCE(dik_code) | DIDFT_PSHBUTTON);
    EnterCriticalSection(&This->base.crit);
    queue_event((LPDIRECTINPUTDEVICE8A)This, dik_code, new_diks, hook->time, This->dinput->evsequence++);
    LeaveCriticalSection(&This->base.crit);
    
    return CallNextHookEx(0, code, wparam, lparam);
}

static const GUID DInput_Wine_Keyboard_GUID = { /* 0ab8648a-7735-11d2-8c73-71df54a96441 */
  0x0ab8648a,
  0x7735,
  0x11d2,
  {0x8c, 0x73, 0x71, 0xdf, 0x54, 0xa9, 0x64, 0x41}
};

static void fill_keyboard_dideviceinstanceA(LPDIDEVICEINSTANCEA lpddi, DWORD version) {
    DWORD dwSize;
    DIDEVICEINSTANCEA ddi;
    
    dwSize = lpddi->dwSize;

    TRACE("%d %p\n", dwSize, lpddi);
    
    memset(lpddi, 0, dwSize);
    memset(&ddi, 0, sizeof(ddi));

    ddi.dwSize = dwSize;
    ddi.guidInstance = GUID_SysKeyboard;/* DInput's GUID */
    ddi.guidProduct = DInput_Wine_Keyboard_GUID; /* Vendor's GUID */
    if (version >= 0x0800)
        ddi.dwDevType = DI8DEVTYPE_KEYBOARD | (DI8DEVTYPEKEYBOARD_UNKNOWN << 8);
    else
        ddi.dwDevType = DIDEVTYPE_KEYBOARD | (DIDEVTYPEKEYBOARD_UNKNOWN << 8);
    strcpy(ddi.tszInstanceName, "Keyboard");
    strcpy(ddi.tszProductName, "Wine Keyboard");

    memcpy(lpddi, &ddi, (dwSize < sizeof(ddi) ? dwSize : sizeof(ddi)));
}

static void fill_keyboard_dideviceinstanceW(LPDIDEVICEINSTANCEW lpddi, DWORD version) {
    DWORD dwSize;
    DIDEVICEINSTANCEW ddi;
    
    dwSize = lpddi->dwSize;

    TRACE("%d %p\n", dwSize, lpddi);
    
    memset(lpddi, 0, dwSize);
    memset(&ddi, 0, sizeof(ddi));
 
    ddi.dwSize = dwSize;
    ddi.guidInstance = GUID_SysKeyboard;/* DInput's GUID */
    ddi.guidProduct = DInput_Wine_Keyboard_GUID; /* Vendor's GUID */
    if (version >= 0x0800)
        ddi.dwDevType = DI8DEVTYPE_KEYBOARD | (DI8DEVTYPEKEYBOARD_UNKNOWN << 8);
    else
        ddi.dwDevType = DIDEVTYPE_KEYBOARD | (DIDEVTYPEKEYBOARD_UNKNOWN << 8);
    MultiByteToWideChar(CP_ACP, 0, "Keyboard", -1, ddi.tszInstanceName, MAX_PATH);
    MultiByteToWideChar(CP_ACP, 0, "Wine Keyboard", -1, ddi.tszProductName, MAX_PATH);

    memcpy(lpddi, &ddi, (dwSize < sizeof(ddi) ? dwSize : sizeof(ddi)));
}
 
static BOOL keyboarddev_enum_deviceA(DWORD dwDevType, DWORD dwFlags, LPDIDEVICEINSTANCEA lpddi, DWORD version, int id)
{
  if (id != 0)
    return FALSE;

  if ((dwDevType == 0) ||
      ((dwDevType == DIDEVTYPE_KEYBOARD) && (version < 0x0800)) ||
      (((dwDevType == DI8DEVCLASS_KEYBOARD) || (dwDevType == DI8DEVTYPE_KEYBOARD)) && (version >= 0x0800))) {
    TRACE("Enumerating the Keyboard device\n");
 
    fill_keyboard_dideviceinstanceA(lpddi, version);
    
    return TRUE;
  }

  return FALSE;
}

static BOOL keyboarddev_enum_deviceW(DWORD dwDevType, DWORD dwFlags, LPDIDEVICEINSTANCEW lpddi, DWORD version, int id)
{
  if (id != 0)
    return FALSE;

  if ((dwDevType == 0) ||
      ((dwDevType == DIDEVTYPE_KEYBOARD) && (version < 0x0800)) ||
      (((dwDevType == DI8DEVCLASS_KEYBOARD) || (dwDevType == DI8DEVTYPE_KEYBOARD)) && (version >= 0x0800))) {
    TRACE("Enumerating the Keyboard device\n");

    fill_keyboard_dideviceinstanceW(lpddi, version);
    
    return TRUE;
  }

  return FALSE;
}

static SysKeyboardImpl *alloc_device(REFGUID rguid, const void *kvt, IDirectInputImpl *dinput)
{
    SysKeyboardImpl* newDevice;
    LPDIDATAFORMAT df = NULL;
    int i, idx = 0;

    newDevice = HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(SysKeyboardImpl));
    newDevice->base.lpVtbl = kvt;
    newDevice->base.ref = 1;
    memcpy(&newDevice->base.guid, rguid, sizeof(*rguid));
    newDevice->dinput = dinput;
    InitializeCriticalSection(&newDevice->base.crit);
    newDevice->base.crit.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": SysKeyboardImpl*->base.crit");

    /* Create copy of default data format */
    if (!(df = HeapAlloc(GetProcessHeap(), 0, c_dfDIKeyboard.dwSize))) goto failed;
    memcpy(df, &c_dfDIKeyboard, c_dfDIKeyboard.dwSize);
    if (!(df->rgodf = HeapAlloc(GetProcessHeap(), 0, df->dwNumObjs * df->dwObjSize))) goto failed;

    for (i = 0; i < df->dwNumObjs; i++)
    {
        char buf[MAX_PATH];

        if (!GetKeyNameTextA(((i & 0x7f) << 16) | ((i & 0x80) << 17), buf, sizeof(buf)))
            continue;

        memcpy(&df->rgodf[idx], &c_dfDIKeyboard.rgodf[i], df->dwObjSize);
        df->rgodf[idx++].dwType = DIDFT_MAKEINSTANCE(i) | DIDFT_PSHBUTTON;
    }
    df->dwNumObjs = idx;

    newDevice->base.data_format.wine_df = df;
    IDirectInput_AddRef((LPDIRECTINPUTDEVICE8A)newDevice->dinput);
    return newDevice;

failed:
    if (df) HeapFree(GetProcessHeap(), 0, df->rgodf);
    HeapFree(GetProcessHeap(), 0, df);
    HeapFree(GetProcessHeap(), 0, newDevice);
    return NULL;
}


static HRESULT keyboarddev_create_deviceA(IDirectInputImpl *dinput, REFGUID rguid, REFIID riid, LPDIRECTINPUTDEVICEA* pdev)
{
  if ((IsEqualGUID(&GUID_SysKeyboard,rguid)) ||          /* Generic Keyboard */
      (IsEqualGUID(&DInput_Wine_Keyboard_GUID,rguid))) { /* Wine Keyboard */
    if ((riid == NULL) ||
	IsEqualGUID(&IID_IDirectInputDeviceA,riid) ||
	IsEqualGUID(&IID_IDirectInputDevice2A,riid) ||
	IsEqualGUID(&IID_IDirectInputDevice7A,riid) ||
	IsEqualGUID(&IID_IDirectInputDevice8A,riid)) {
      *pdev = (IDirectInputDeviceA*) alloc_device(rguid, &SysKeyboardAvt, dinput);
      TRACE("Creating a Keyboard device (%p)\n", *pdev);
      if (!*pdev) return DIERR_OUTOFMEMORY;
      return DI_OK;
    } else
      return DIERR_NOINTERFACE;
  }
  return DIERR_DEVICENOTREG;
}

static HRESULT keyboarddev_create_deviceW(IDirectInputImpl *dinput, REFGUID rguid, REFIID riid, LPDIRECTINPUTDEVICEW* pdev)
{
  if ((IsEqualGUID(&GUID_SysKeyboard,rguid)) ||          /* Generic Keyboard */
      (IsEqualGUID(&DInput_Wine_Keyboard_GUID,rguid))) { /* Wine Keyboard */
    if ((riid == NULL) ||
	IsEqualGUID(&IID_IDirectInputDeviceW,riid) ||
	IsEqualGUID(&IID_IDirectInputDevice2W,riid) ||
	IsEqualGUID(&IID_IDirectInputDevice7W,riid) ||
	IsEqualGUID(&IID_IDirectInputDevice8W,riid)) {
      *pdev = (IDirectInputDeviceW*) alloc_device(rguid, &SysKeyboardWvt, dinput);
      TRACE("Creating a Keyboard device (%p)\n", *pdev);
      if (!*pdev) return DIERR_OUTOFMEMORY;
      return DI_OK;
    } else
      return DIERR_NOINTERFACE;
  }
  return DIERR_DEVICENOTREG;
}

const struct dinput_device keyboard_device = {
  "Wine keyboard driver",
  keyboarddev_enum_deviceA,
  keyboarddev_enum_deviceW,
  keyboarddev_create_deviceA,
  keyboarddev_create_deviceW
};

static ULONG WINAPI SysKeyboardAImpl_Release(LPDIRECTINPUTDEVICE8A iface)
{
    SysKeyboardImpl *This = (SysKeyboardImpl *)iface;
    ULONG ref;

    ref = InterlockedDecrement(&This->base.ref);
    if (ref) return ref;

    IDirectInputDevice_Unacquire(iface);

    HeapFree(GetProcessHeap(), 0, This->base.data_queue);

    /* Free data format */
    HeapFree(GetProcessHeap(), 0, This->base.data_format.wine_df->rgodf);
    HeapFree(GetProcessHeap(), 0, This->base.data_format.wine_df);
    release_DataFormat(&This->base.data_format);

    IDirectInput_Release((LPDIRECTINPUTDEVICE8A)This->dinput);
    This->base.crit.DebugInfo->Spare[0] = 0;
    DeleteCriticalSection(&This->base.crit);
    HeapFree(GetProcessHeap(), 0, This);

    return DI_OK;
}

static HRESULT WINAPI SysKeyboardAImpl_GetDeviceState(
	LPDIRECTINPUTDEVICE8A iface,DWORD len,LPVOID ptr
)
{
    SysKeyboardImpl *This = (SysKeyboardImpl *)iface;
    TRACE("(%p)->(%d,%p)\n", This, len, ptr);

    if (!This->base.acquired) return DIERR_NOTACQUIRED;

    if (len != WINE_DINPUT_KEYBOARD_MAX_KEYS)
        return DIERR_INVALIDPARAM;

    EnterCriticalSection(&This->base.crit);

    if (TRACE_ON(dinput)) {
	int i;
	for (i = 0; i < WINE_DINPUT_KEYBOARD_MAX_KEYS; i++) {
	    if (DInputKeyState[i] != 0x00) {
		TRACE(" - %02X: %02x\n", i, DInputKeyState[i]);
	    }
	}
    }
    
    memcpy(ptr, DInputKeyState, WINE_DINPUT_KEYBOARD_MAX_KEYS);
    LeaveCriticalSection(&This->base.crit);

    return DI_OK;
}

static HRESULT WINAPI SysKeyboardAImpl_Unacquire(LPDIRECTINPUTDEVICE8A iface);

static HRESULT WINAPI SysKeyboardAImpl_Acquire(LPDIRECTINPUTDEVICE8A iface)
{
    SysKeyboardImpl *This = (SysKeyboardImpl *)iface;
    HRESULT res;

    TRACE("(%p)\n",This);

    if ((res = IDirectInputDevice2AImpl_Acquire(iface)) != DI_OK) return res;

    if (current_lock != NULL) {
        FIXME("Not more than one keyboard can be acquired at the same time.\n");
        SysKeyboardAImpl_Unacquire((LPDIRECTINPUTDEVICE8A)current_lock);
    }
    current_lock = This;

    set_dinput_hook(WH_KEYBOARD_LL, KeyboardCallback);

    return DI_OK;
}

static HRESULT WINAPI SysKeyboardAImpl_Unacquire(LPDIRECTINPUTDEVICE8A iface)
{
    SysKeyboardImpl *This = (SysKeyboardImpl *)iface;
    HRESULT res;

    TRACE("(this=%p)\n",This);

    if ((res = IDirectInputDevice2AImpl_Unacquire(iface)) != DI_OK) return res;

    set_dinput_hook(WH_KEYBOARD_LL, NULL);

    /* No more locks */
    if (current_lock == This)
        current_lock = NULL;
    else
        ERR("this != current_lock\n");

    return DI_OK;
}

/******************************************************************************
  *     GetCapabilities : get the device capablitites
  */
static HRESULT WINAPI SysKeyboardAImpl_GetCapabilities(
	LPDIRECTINPUTDEVICE8A iface,
	LPDIDEVCAPS lpDIDevCaps)
{
    SysKeyboardImpl *This = (SysKeyboardImpl *)iface;
    DIDEVCAPS devcaps;

    TRACE("(this=%p,%p)\n",This,lpDIDevCaps);

    if ((lpDIDevCaps->dwSize != sizeof(DIDEVCAPS)) && (lpDIDevCaps->dwSize != sizeof(DIDEVCAPS_DX3))) {
        WARN("invalid parameter\n");
        return DIERR_INVALIDPARAM;
    }
    
    devcaps.dwSize = lpDIDevCaps->dwSize;
    devcaps.dwFlags = DIDC_ATTACHED;
    if (This->dinput->dwVersion >= 0x0800)
	devcaps.dwDevType = DI8DEVTYPE_KEYBOARD | (DI8DEVTYPEKEYBOARD_UNKNOWN << 8);
    else
	devcaps.dwDevType = DIDEVTYPE_KEYBOARD | (DIDEVTYPEKEYBOARD_UNKNOWN << 8);
    devcaps.dwAxes = 0;
    devcaps.dwButtons = This->base.data_format.wine_df->dwNumObjs;
    devcaps.dwPOVs = 0;
    devcaps.dwFFSamplePeriod = 0;
    devcaps.dwFFMinTimeResolution = 0;
    devcaps.dwFirmwareRevision = 100;
    devcaps.dwHardwareRevision = 100;
    devcaps.dwFFDriverVersion = 0;

    memcpy(lpDIDevCaps, &devcaps, lpDIDevCaps->dwSize);
    
    return DI_OK;
}

/******************************************************************************
  *     GetObjectInfo : get information about a device object such as a button
  *                     or axis
  */
static HRESULT WINAPI
SysKeyboardAImpl_GetObjectInfo(
	LPDIRECTINPUTDEVICE8A iface,
	LPDIDEVICEOBJECTINSTANCEA pdidoi,
	DWORD dwObj,
	DWORD dwHow)
{
    HRESULT res;

    res = IDirectInputDevice2AImpl_GetObjectInfo(iface, pdidoi, dwObj, dwHow);
    if (res != DI_OK) return res;

    if (!GetKeyNameTextA((DIDFT_GETINSTANCE(pdidoi->dwType) & 0x80) << 17 |
                         (DIDFT_GETINSTANCE(pdidoi->dwType) & 0x7f) << 16,
                         pdidoi->tszName, sizeof(pdidoi->tszName)))
        return DIERR_OBJECTNOTFOUND;

    _dump_OBJECTINSTANCEA(pdidoi);
    return res;
}

static HRESULT WINAPI SysKeyboardWImpl_GetObjectInfo(LPDIRECTINPUTDEVICE8W iface,
						     LPDIDEVICEOBJECTINSTANCEW pdidoi,
						     DWORD dwObj,
						     DWORD dwHow)
{
    HRESULT res;

    res = IDirectInputDevice2WImpl_GetObjectInfo(iface, pdidoi, dwObj, dwHow);
    if (res != DI_OK) return res;

    if (!GetKeyNameTextW((DIDFT_GETINSTANCE(pdidoi->dwType) & 0x80) << 17 |
                         (DIDFT_GETINSTANCE(pdidoi->dwType) & 0x7f) << 16,
                         pdidoi->tszName, sizeof(pdidoi->tszName)))
        return DIERR_OBJECTNOTFOUND;

    _dump_OBJECTINSTANCEW(pdidoi);
    return res;
}

/******************************************************************************
  *     GetDeviceInfo : get information about a device's identity
  */
static HRESULT WINAPI SysKeyboardAImpl_GetDeviceInfo(
	LPDIRECTINPUTDEVICE8A iface,
	LPDIDEVICEINSTANCEA pdidi)
{
    SysKeyboardImpl *This = (SysKeyboardImpl *)iface;
    TRACE("(this=%p,%p)\n", This, pdidi);

    if (pdidi->dwSize != sizeof(DIDEVICEINSTANCEA)) {
        WARN(" dinput3 not supported yet...\n");
	return DI_OK;
    }

    fill_keyboard_dideviceinstanceA(pdidi, This->dinput->dwVersion);
    
    return DI_OK;
}

static HRESULT WINAPI SysKeyboardWImpl_GetDeviceInfo(LPDIRECTINPUTDEVICE8W iface, LPDIDEVICEINSTANCEW pdidi) 
{
    SysKeyboardImpl *This = (SysKeyboardImpl *)iface;
    TRACE("(this=%p,%p)\n", This, pdidi);

    if (pdidi->dwSize != sizeof(DIDEVICEINSTANCEW)) {
        WARN(" dinput3 not supported yet...\n");
	return DI_OK;
    }

    fill_keyboard_dideviceinstanceW(pdidi, This->dinput->dwVersion);
    
    return DI_OK;
}

static const IDirectInputDevice8AVtbl SysKeyboardAvt =
{
	IDirectInputDevice2AImpl_QueryInterface,
	IDirectInputDevice2AImpl_AddRef,
	SysKeyboardAImpl_Release,
	SysKeyboardAImpl_GetCapabilities,
        IDirectInputDevice2AImpl_EnumObjects,
	IDirectInputDevice2AImpl_GetProperty,
	IDirectInputDevice2AImpl_SetProperty,
	SysKeyboardAImpl_Acquire,
	SysKeyboardAImpl_Unacquire,
	SysKeyboardAImpl_GetDeviceState,
	IDirectInputDevice2AImpl_GetDeviceData,
	IDirectInputDevice2AImpl_SetDataFormat,
	IDirectInputDevice2AImpl_SetEventNotification,
	IDirectInputDevice2AImpl_SetCooperativeLevel,
	SysKeyboardAImpl_GetObjectInfo,
	SysKeyboardAImpl_GetDeviceInfo,
	IDirectInputDevice2AImpl_RunControlPanel,
	IDirectInputDevice2AImpl_Initialize,
	IDirectInputDevice2AImpl_CreateEffect,
	IDirectInputDevice2AImpl_EnumEffects,
	IDirectInputDevice2AImpl_GetEffectInfo,
	IDirectInputDevice2AImpl_GetForceFeedbackState,
	IDirectInputDevice2AImpl_SendForceFeedbackCommand,
	IDirectInputDevice2AImpl_EnumCreatedEffectObjects,
	IDirectInputDevice2AImpl_Escape,
        IDirectInputDevice2AImpl_Poll,
        IDirectInputDevice2AImpl_SendDeviceData,
        IDirectInputDevice7AImpl_EnumEffectsInFile,
        IDirectInputDevice7AImpl_WriteEffectToFile,
        IDirectInputDevice8AImpl_BuildActionMap,
        IDirectInputDevice8AImpl_SetActionMap,
        IDirectInputDevice8AImpl_GetImageInfo
};

#if !defined(__STRICT_ANSI__) && defined(__GNUC__)
# define XCAST(fun)	(typeof(SysKeyboardWvt.fun))
#else
# define XCAST(fun)	(void*)
#endif

static const IDirectInputDevice8WVtbl SysKeyboardWvt =
{
	IDirectInputDevice2WImpl_QueryInterface,
	XCAST(AddRef)IDirectInputDevice2AImpl_AddRef,
	XCAST(Release)SysKeyboardAImpl_Release,
	XCAST(GetCapabilities)SysKeyboardAImpl_GetCapabilities,
        IDirectInputDevice2WImpl_EnumObjects,
	XCAST(GetProperty)IDirectInputDevice2AImpl_GetProperty,
	XCAST(SetProperty)IDirectInputDevice2AImpl_SetProperty,
	XCAST(Acquire)SysKeyboardAImpl_Acquire,
	XCAST(Unacquire)SysKeyboardAImpl_Unacquire,
	XCAST(GetDeviceState)SysKeyboardAImpl_GetDeviceState,
	XCAST(GetDeviceData)IDirectInputDevice2AImpl_GetDeviceData,
	XCAST(SetDataFormat)IDirectInputDevice2AImpl_SetDataFormat,
	XCAST(SetEventNotification)IDirectInputDevice2AImpl_SetEventNotification,
	XCAST(SetCooperativeLevel)IDirectInputDevice2AImpl_SetCooperativeLevel,
	SysKeyboardWImpl_GetObjectInfo,
	SysKeyboardWImpl_GetDeviceInfo,
	XCAST(RunControlPanel)IDirectInputDevice2AImpl_RunControlPanel,
	XCAST(Initialize)IDirectInputDevice2AImpl_Initialize,
	XCAST(CreateEffect)IDirectInputDevice2AImpl_CreateEffect,
	IDirectInputDevice2WImpl_EnumEffects,
	IDirectInputDevice2WImpl_GetEffectInfo,
	XCAST(GetForceFeedbackState)IDirectInputDevice2AImpl_GetForceFeedbackState,
	XCAST(SendForceFeedbackCommand)IDirectInputDevice2AImpl_SendForceFeedbackCommand,
	XCAST(EnumCreatedEffectObjects)IDirectInputDevice2AImpl_EnumCreatedEffectObjects,
	XCAST(Escape)IDirectInputDevice2AImpl_Escape,
        XCAST(Poll)IDirectInputDevice2AImpl_Poll,
        XCAST(SendDeviceData)IDirectInputDevice2AImpl_SendDeviceData,
        IDirectInputDevice7WImpl_EnumEffectsInFile,
        IDirectInputDevice7WImpl_WriteEffectToFile,
        IDirectInputDevice8WImpl_BuildActionMap,
        IDirectInputDevice8WImpl_SetActionMap,
        IDirectInputDevice8WImpl_GetImageInfo
};
#undef XCAST
