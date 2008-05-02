/* Unit test suite for list boxes.
 *
 * Copyright 2003 Ferenc Wagner
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

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "winnls.h"

#include "wine/test.h"

#ifdef VISIBLE
#define WAIT Sleep (1000)
#define REDRAW RedrawWindow (handle, NULL, 0, RDW_UPDATENOW)
#else
#define WAIT
#define REDRAW
#endif

static const char * const strings[4] = {
  "First added",
  "Second added",
  "Third added",
  "Fourth added which is very long because at some time we only had a 256 byte character buffer and that was overflowing in one of those applications that had a common dialog file open box and tried to add a 300 characters long custom filter string which of course the code did not like and crashed. Just make sure this string is longer than 256 characters."
};

static HWND
create_listbox (DWORD add_style, HWND parent)
{
  HWND handle;
  int ctl_id=0;
  if (parent)
    ctl_id=1;
  handle=CreateWindow ("LISTBOX", "TestList",
                            (LBS_STANDARD & ~LBS_SORT) | add_style,
                            0, 0, 100, 100,
                            parent, (HMENU)ctl_id, NULL, 0);

  assert (handle);
  SendMessage (handle, LB_ADDSTRING, 0, (LPARAM) (LPCTSTR) strings[0]);
  SendMessage (handle, LB_ADDSTRING, 0, (LPARAM) (LPCTSTR) strings[1]);
  SendMessage (handle, LB_ADDSTRING, 0, (LPARAM) (LPCTSTR) strings[2]);
  SendMessage (handle, LB_ADDSTRING, 0, (LPARAM) (LPCTSTR) strings[3]);

#ifdef VISIBLE
  ShowWindow (handle, SW_SHOW);
#endif
  REDRAW;

  return handle;
}

struct listbox_prop {
  DWORD add_style;
};

struct listbox_stat {
  int selected, anchor, caret, selcount;
};

struct listbox_test {
  struct listbox_prop prop;
  struct listbox_stat  init,  init_todo;
  struct listbox_stat click, click_todo;
  struct listbox_stat  step,  step_todo;
  struct listbox_stat   sel,   sel_todo;
};

static void
listbox_query (HWND handle, struct listbox_stat *results)
{
  results->selected = SendMessage (handle, LB_GETCURSEL, 0, 0);
  results->anchor   = SendMessage (handle, LB_GETANCHORINDEX, 0, 0);
  results->caret    = SendMessage (handle, LB_GETCARETINDEX, 0, 0);
  results->selcount = SendMessage (handle, LB_GETSELCOUNT, 0, 0);
}

static void
buttonpress (HWND handle, WORD x, WORD y)
{
  LPARAM lp=x+(y<<16);

  WAIT;
  SendMessage (handle, WM_LBUTTONDOWN, (WPARAM) MK_LBUTTON, lp);
  SendMessage (handle, WM_LBUTTONUP  , (WPARAM) 0         , lp);
  REDRAW;
}

static void
keypress (HWND handle, WPARAM keycode, BYTE scancode, BOOL extended)
{
  LPARAM lp=1+(scancode<<16)+(extended?KEYEVENTF_EXTENDEDKEY:0);

  WAIT;
  SendMessage (handle, WM_KEYDOWN, keycode, lp);
  SendMessage (handle, WM_KEYUP  , keycode, lp | 0xc000000);
  REDRAW;
}

#define listbox_field_ok(t, s, f, got) \
  ok (t.s.f==got.f, "style %#x, step " #s ", field " #f \
      ": expected %d, got %d\n", (unsigned int)t.prop.add_style, \
      t.s.f, got.f)

#define listbox_todo_field_ok(t, s, f, got) \
  if (t.s##_todo.f) todo_wine { listbox_field_ok(t, s, f, got); } \
  else listbox_field_ok(t, s, f, got)

#define listbox_ok(t, s, got) \
  listbox_todo_field_ok(t, s, selected, got); \
  listbox_todo_field_ok(t, s, anchor, got); \
  listbox_todo_field_ok(t, s, caret, got); \
  listbox_todo_field_ok(t, s, selcount, got)

static void
check (const struct listbox_test test)
{
  struct listbox_stat answer;
  HWND hLB=create_listbox (test.prop.add_style, 0);
  RECT second_item;
  int i;
  int res;

  listbox_query (hLB, &answer);
  listbox_ok (test, init, answer);

  SendMessage (hLB, LB_GETITEMRECT, (WPARAM) 1, (LPARAM) &second_item);
  buttonpress(hLB, (WORD)second_item.left, (WORD)second_item.top);

  listbox_query (hLB, &answer);
  listbox_ok (test, click, answer);

  keypress (hLB, VK_DOWN, 0x50, TRUE);

  listbox_query (hLB, &answer);
  listbox_ok (test, step, answer);

  DestroyWindow (hLB);
  hLB=create_listbox (test.prop.add_style, 0);

  SendMessage (hLB, LB_SELITEMRANGE, TRUE, MAKELPARAM(1, 2));
  listbox_query (hLB, &answer);
  listbox_ok (test, sel, answer);

  for (i=0;i<4;i++) {
	DWORD size = SendMessage (hLB, LB_GETTEXTLEN, i, 0);
	CHAR *txt;
	WCHAR *txtw;
	int resA, resW;

	txt = HeapAlloc (GetProcessHeap(), 0, size+1);
	memset(txt, 0, size+1);
	resA=SendMessageA(hLB, LB_GETTEXT, i, (LPARAM)txt);
        ok(!strcmp (txt, strings[i]), "returned string for item %d does not match %s vs %s\n", i, txt, strings[i]);

	txtw = HeapAlloc (GetProcessHeap(), 0, 2*size+2);
	memset(txtw, 0, 2*size+2);
	resW=SendMessageW(hLB, LB_GETTEXT, i, (LPARAM)txtw);
	if (resA != resW) {
            trace("SendMessageW(LB_GETTEXT) not supported on this platform (resA=%d resW=%d), skipping...\n",
                resA, resW);
	} else {
	    WideCharToMultiByte( CP_ACP, 0, txtw, -1, txt, size, NULL, NULL );
            ok(!strcmp (txt, strings[i]), "returned string for item %d does not match %s vs %s\n", i, txt, strings[i]);
	}

	HeapFree (GetProcessHeap(), 0, txtw);
	HeapFree (GetProcessHeap(), 0, txt);
  }
  
  /* Confirm the count of items, and that an invalid delete does not remove anything */
  res = SendMessage (hLB, LB_GETCOUNT, 0, 0);
  ok((res==4), "Expected 4 items, got %d\n", res);
  res = SendMessage (hLB, LB_DELETESTRING, -1, 0);
  ok((res==LB_ERR), "Expected LB_ERR items, got %d\n", res);
  res = SendMessage (hLB, LB_DELETESTRING, 4, 0);
  ok((res==LB_ERR), "Expected LB_ERR items, got %d\n", res);
  res = SendMessage (hLB, LB_GETCOUNT, 0, 0);
  ok((res==4), "Expected 4 items, got %d\n", res);

  WAIT;
  DestroyWindow (hLB);
}

static void check_item_height(void)
{
    HWND hLB;
    HDC hdc;
    HFONT font;
    TEXTMETRIC tm;
    INT itemHeight;

    hLB = create_listbox (0, 0);
    ok ((hdc = GetDCEx( hLB, 0, DCX_CACHE )) != 0, "Can't get hdc\n");
    ok ((font = GetCurrentObject(hdc, OBJ_FONT)) != 0, "Can't get the current font\n");
    ok (GetTextMetrics( hdc, &tm ), "Can't read font metrics\n");
    ReleaseDC( hLB, hdc);

    ok (SendMessage(hLB, WM_SETFONT, (WPARAM)font, 0) == 0, "Can't set font\n");

    itemHeight = SendMessage(hLB, LB_GETITEMHEIGHT, 0, 0);
    ok (itemHeight == tm.tmHeight, "Item height wrong, got %d, expecting %d\n", itemHeight, tm.tmHeight);

    DestroyWindow (hLB);

    hLB = CreateWindow ("LISTBOX", "TestList", LBS_OWNERDRAWVARIABLE,
                         0, 0, 100, 100, NULL, NULL, NULL, 0);
    itemHeight = SendMessage(hLB, LB_GETITEMHEIGHT, 0, 0);
    ok(itemHeight == tm.tmHeight, "itemHeight %d\n", itemHeight);
    itemHeight = SendMessage(hLB, LB_GETITEMHEIGHT, 5, 0);
    ok(itemHeight == tm.tmHeight, "itemHeight %d\n", itemHeight);
    itemHeight = SendMessage(hLB, LB_GETITEMHEIGHT, -5, 0);
    ok(itemHeight == tm.tmHeight, "itemHeight %d\n", itemHeight);
    DestroyWindow (hLB);
}

static LRESULT WINAPI main_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_DRAWITEM:
    {
        RECT rc_item, rc_client, rc_clip;
        DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lparam;

        trace("%p WM_DRAWITEM %08lx %08lx\n", hwnd, wparam, lparam);

        ok(wparam == dis->CtlID, "got wParam=%08lx instead of %08x\n",
			wparam, dis->CtlID);
        ok(dis->CtlType == ODT_LISTBOX, "wrong CtlType %04x\n", dis->CtlType);

        GetClientRect(dis->hwndItem, &rc_client);
        trace("hwndItem %p client rect (%d,%d-%d,%d)\n", dis->hwndItem,
               rc_client.left, rc_client.top, rc_client.right, rc_client.bottom);
        GetClipBox(dis->hDC, &rc_clip);
        trace("clip rect (%d,%d-%d,%d)\n", rc_clip.left, rc_clip.top, rc_clip.right, rc_clip.bottom);
        ok(EqualRect(&rc_client, &rc_clip), "client rect of the listbox should be equal to the clip box\n");

        trace("rcItem (%d,%d-%d,%d)\n", dis->rcItem.left, dis->rcItem.top,
               dis->rcItem.right, dis->rcItem.bottom);
        SendMessage(dis->hwndItem, LB_GETITEMRECT, dis->itemID, (LPARAM)&rc_item);
        trace("item rect (%d,%d-%d,%d)\n", rc_item.left, rc_item.top, rc_item.right, rc_item.bottom);
        ok(EqualRect(&dis->rcItem, &rc_item), "item rects are not equal\n");

        break;
    }

    default:
        break;
    }

    return DefWindowProc(hwnd, msg, wparam, lparam);
}

static void test_ownerdraw(void)
{
    WNDCLASS cls;
    HWND parent, hLB;
    INT ret;
    RECT rc;

    cls.style = 0;
    cls.lpfnWndProc = main_window_proc;
    cls.cbClsExtra = 0;
    cls.cbWndExtra = 0;
    cls.hInstance = GetModuleHandle(0);
    cls.hIcon = 0;
    cls.hCursor = LoadCursor(0, (LPSTR)IDC_ARROW);
    cls.hbrBackground = GetStockObject(WHITE_BRUSH);
    cls.lpszMenuName = NULL;
    cls.lpszClassName = "main_window_class";
    assert(RegisterClass(&cls));

    parent = CreateWindowEx(0, "main_window_class", NULL,
                            WS_POPUP | WS_VISIBLE,
                            100, 100, 400, 400,
                            GetDesktopWindow(), 0,
                            GetModuleHandle(0), NULL);
    assert(parent);

    hLB = create_listbox(LBS_OWNERDRAWFIXED | WS_CHILD | WS_VISIBLE, parent);
    assert(hLB);

    UpdateWindow(hLB);

    /* make height short enough */
    SendMessage(hLB, LB_GETITEMRECT, 0, (LPARAM)&rc);
    SetWindowPos(hLB, 0, 0, 0, 100, rc.bottom - rc.top + 1,
                 SWP_NOZORDER | SWP_NOMOVE);

    /* make 0 item invisible */
    SendMessage(hLB, LB_SETTOPINDEX, 1, 0);
    ret = SendMessage(hLB, LB_GETTOPINDEX, 0, 0);
    ok(ret == 1, "wrong top index %d\n", ret);

    SendMessage(hLB, LB_GETITEMRECT, 0, (LPARAM)&rc);
    trace("item 0 rect (%d,%d-%d,%d)\n", rc.left, rc.top, rc.right, rc.bottom);
    ok(!IsRectEmpty(&rc), "empty item rect\n");
    ok(rc.top < 0, "rc.top is not negative (%d)\n", rc.top);

    DestroyWindow(hLB);
    DestroyWindow(parent);
}

#define listbox_test_query(exp, got) \
  ok(exp.selected == got.selected, "expected selected %d, got %d\n", exp.selected, got.selected); \
  ok(exp.anchor == got.anchor, "expected anchor %d, got %d\n", exp.anchor, got.anchor); \
  ok(exp.caret == got.caret, "expected caret %d, got %d\n", exp.caret, got.caret); \
  ok(exp.selcount == got.selcount, "expected selcount %d, got %d\n", exp.selcount, got.selcount);

static void test_selection(void)
{
    static const struct listbox_stat test_nosel = { 0, LB_ERR, 0, 0 };
    static const struct listbox_stat test_1 = { 0, LB_ERR, 0, 2 };
    static const struct listbox_stat test_2 = { 0, LB_ERR, 0, 3 };
    static const struct listbox_stat test_3 = { 0, LB_ERR, 0, 4 };
    HWND hLB;
    struct listbox_stat answer;
    INT ret;

    trace("testing LB_SELITEMRANGE\n");

    hLB = create_listbox(LBS_EXTENDEDSEL, 0);
    assert(hLB);

    listbox_query(hLB, &answer);
    listbox_test_query(test_nosel, answer);

    ret = SendMessage(hLB, LB_SELITEMRANGE, TRUE, MAKELPARAM(1, 2));
    ok(ret == LB_OKAY, "LB_SELITEMRANGE returned %d instead of LB_OKAY\n", ret);
    listbox_query(hLB, &answer);
    listbox_test_query(test_1, answer);

    SendMessage(hLB, LB_SETSEL, FALSE, (LPARAM)-1);
    listbox_query(hLB, &answer);
    listbox_test_query(test_nosel, answer);

    ret = SendMessage(hLB, LB_SELITEMRANGE, TRUE, MAKELPARAM(0, 4));
    ok(ret == LB_OKAY, "LB_SELITEMRANGE returned %d instead of LB_OKAY\n", ret);
    listbox_query(hLB, &answer);
    listbox_test_query(test_3, answer);

    SendMessage(hLB, LB_SETSEL, FALSE, (LPARAM)-1);
    listbox_query(hLB, &answer);
    listbox_test_query(test_nosel, answer);

    ret = SendMessage(hLB, LB_SELITEMRANGE, TRUE, MAKELPARAM(-5, 5));
    ok(ret == LB_OKAY, "LB_SELITEMRANGE returned %d instead of LB_OKAY\n", ret);
    listbox_query(hLB, &answer);
    listbox_test_query(test_nosel, answer);

    SendMessage(hLB, LB_SETSEL, FALSE, (LPARAM)-1);
    listbox_query(hLB, &answer);
    listbox_test_query(test_nosel, answer);

    ret = SendMessage(hLB, LB_SELITEMRANGE, TRUE, MAKELPARAM(2, 10));
    ok(ret == LB_OKAY, "LB_SELITEMRANGE returned %d instead of LB_OKAY\n", ret);
    listbox_query(hLB, &answer);
    listbox_test_query(test_1, answer);

    SendMessage(hLB, LB_SETSEL, FALSE, (LPARAM)-1);
    listbox_query(hLB, &answer);
    listbox_test_query(test_nosel, answer);

    ret = SendMessage(hLB, LB_SELITEMRANGE, TRUE, MAKELPARAM(4, 10));
    ok(ret == LB_OKAY, "LB_SELITEMRANGE returned %d instead of LB_OKAY\n", ret);
    listbox_query(hLB, &answer);
    listbox_test_query(test_nosel, answer);

    SendMessage(hLB, LB_SETSEL, FALSE, (LPARAM)-1);
    listbox_query(hLB, &answer);
    listbox_test_query(test_nosel, answer);

    ret = SendMessage(hLB, LB_SELITEMRANGE, TRUE, MAKELPARAM(10, 1));
    ok(ret == LB_OKAY, "LB_SELITEMRANGE returned %d instead of LB_OKAY\n", ret);
    listbox_query(hLB, &answer);
    listbox_test_query(test_2, answer);

    SendMessage(hLB, LB_SETSEL, FALSE, (LPARAM)-1);
    listbox_query(hLB, &answer);
    listbox_test_query(test_nosel, answer);

    ret = SendMessage(hLB, LB_SELITEMRANGE, TRUE, MAKELPARAM(1, -1));
    ok(ret == LB_OKAY, "LB_SELITEMRANGE returned %d instead of LB_OKAY\n", ret);
    listbox_query(hLB, &answer);
    listbox_test_query(test_2, answer);

    DestroyWindow(hLB);
}

static void test_listbox_height(void)
{
    HWND hList;
    int r, id;

    hList = CreateWindow( "ListBox", "list test", 0, 
                          1, 1, 600, 100, NULL, NULL, NULL, NULL );
    ok( hList != NULL, "failed to create listbox\n");

    id = SendMessage( hList, LB_ADDSTRING, 0, (LPARAM) "hi");
    ok( id == 0, "item id wrong\n");

    r = SendMessage( hList, LB_SETITEMHEIGHT, 0, MAKELPARAM( 20, 0 ));
    ok( r == 0, "send message failed\n");

    r = SendMessage(hList, LB_GETITEMHEIGHT, 0, 0 );
    ok( r == 20, "height wrong\n");

    r = SendMessage( hList, LB_SETITEMHEIGHT, 0, MAKELPARAM( 0, 30 ));
    ok( r == -1, "send message failed\n");

    r = SendMessage(hList, LB_GETITEMHEIGHT, 0, 0 );
    ok( r == 20, "height wrong\n");

    r = SendMessage( hList, LB_SETITEMHEIGHT, 0, MAKELPARAM( 0x100, 0 ));
    ok( r == -1, "send message failed\n");

    r = SendMessage(hList, LB_GETITEMHEIGHT, 0, 0 );
    ok( r == 20, "height wrong\n");

    r = SendMessage( hList, LB_SETITEMHEIGHT, 0, MAKELPARAM( 0xff, 0 ));
    ok( r == 0, "send message failed\n");

    r = SendMessage(hList, LB_GETITEMHEIGHT, 0, 0 );
    ok( r == 0xff, "height wrong\n");

    DestroyWindow( hList );
}

static void test_itemfrompoint(void)
{
    /* WS_POPUP is required in order to have a more accurate size calculation (
       without caption). LBS_NOINTEGRALHEIGHT is required in order to test
       behavior of partially-displayed item.
     */
    HWND hList = CreateWindow( "ListBox", "list test",
                               WS_VISIBLE|WS_POPUP|LBS_NOINTEGRALHEIGHT,
                               1, 1, 600, 100, NULL, NULL, NULL, NULL );
    LONG r, id;
    RECT rc;

    /* For an empty listbox win2k returns 0x1ffff, win98 returns 0x10000 */
    r = SendMessage(hList, LB_ITEMFROMPOINT, 0, MAKELPARAM( /* x */ 30, /* y */ 30 ));
    ok( r == 0x1ffff || r == 0x10000, "ret %x\n", r );

    r = SendMessage(hList, LB_ITEMFROMPOINT, 0, MAKELPARAM( 700, 30 ));
    ok( r == 0x1ffff || r == 0x10000, "ret %x\n", r );

    r = SendMessage(hList, LB_ITEMFROMPOINT, 0, MAKELPARAM( 30, 300 ));
    ok( r == 0x1ffff || r == 0x10000, "ret %x\n", r );

    id = SendMessage( hList, LB_ADDSTRING, 0, (LPARAM) "hi");
    ok( id == 0, "item id wrong\n");
    id = SendMessage( hList, LB_ADDSTRING, 0, (LPARAM) "hi1");
    ok( id == 1, "item id wrong\n");

    r = SendMessage(hList, LB_ITEMFROMPOINT, 0, MAKELPARAM( /* x */ 30, /* y */ 30 ));
    ok( r == 0x1, "ret %x\n", r );

    r = SendMessage(hList, LB_ITEMFROMPOINT, 0, MAKELPARAM( /* x */ 30, /* y */ 601 ));
    ok( r == 0x10001, "ret %x\n", r );

    /* Resize control so that below assertions about sizes are valid */
    r = SendMessage( hList, LB_GETITEMRECT, 0, (LPARAM)&rc);
    ok( r == 1, "ret %x\n", r);
    r = MoveWindow(hList, 1, 1, 600, (rc.bottom - rc.top + 1) * 9 / 2, TRUE);
    ok( r != 0, "ret %x\n", r);

    id = SendMessage( hList, LB_ADDSTRING, 0, (LPARAM) "hi2");
    ok( id == 2, "item id wrong\n");
    id = SendMessage( hList, LB_ADDSTRING, 0, (LPARAM) "hi3");
    ok( id == 3, "item id wrong\n");
    id = SendMessage( hList, LB_ADDSTRING, 0, (LPARAM) "hi4");
    ok( id == 4, "item id wrong\n");
    id = SendMessage( hList, LB_ADDSTRING, 0, (LPARAM) "hi5");
    ok( id == 5, "item id wrong\n");
    id = SendMessage( hList, LB_ADDSTRING, 0, (LPARAM) "hi6");
    ok( id == 6, "item id wrong\n");
    id = SendMessage( hList, LB_ADDSTRING, 0, (LPARAM) "hi7");
    ok( id == 7, "item id wrong\n");

    /* Set the listbox up so that id 1 is at the top, this leaves 5
       partially visible at the bottom and 6, 7 are invisible */

    SendMessage( hList, LB_SETTOPINDEX, 1, 0);
    r = SendMessage( hList, LB_GETTOPINDEX, 0, 0);
    ok( r == 1, "top %d\n", r);

    r = SendMessage( hList, LB_GETITEMRECT, 5, (LPARAM)&rc);
    ok( r == 1, "ret %x\n", r);
    r = SendMessage( hList, LB_GETITEMRECT, 6, (LPARAM)&rc);
    ok( r == 0, "ret %x\n", r);

    r = SendMessage( hList, LB_ITEMFROMPOINT, 0, MAKELPARAM(/* x */ 10, /* y */ 10) );
    ok( r == 1, "ret %x\n", r);

    r = SendMessage( hList, LB_ITEMFROMPOINT, 0, MAKELPARAM(1000, 10) );
    ok( r == 0x10001, "ret %x\n", r );

    r = SendMessage( hList, LB_ITEMFROMPOINT, 0, MAKELPARAM(10, -10) );
    ok( r == 0x10001, "ret %x\n", r );

    r = SendMessage( hList, LB_ITEMFROMPOINT, 0, MAKELPARAM(10, 100) );
    ok( r == 0x10005, "item %x\n", r );

    r = SendMessage( hList, LB_ITEMFROMPOINT, 0, MAKELPARAM(10, 200) );
    ok( r == 0x10005, "item %x\n", r );

    DestroyWindow( hList );
}

static void test_listbox_item_data(void)
{
    HWND hList;
    int r, id;

    hList = CreateWindow( "ListBox", "list test", 0,
                          1, 1, 600, 100, NULL, NULL, NULL, NULL );
    ok( hList != NULL, "failed to create listbox\n");

    id = SendMessage( hList, LB_ADDSTRING, 0, (LPARAM) "hi");
    ok( id == 0, "item id wrong\n");

    r = SendMessage( hList, LB_SETITEMDATA, 0, MAKELPARAM( 20, 0 ));
    ok(r == TRUE, "LB_SETITEMDATA returned %d instead of TRUE\n", r);

    r = SendMessage( hList, LB_GETITEMDATA, 0, 0);
    ok( r == 20, "get item data failed\n");

    DestroyWindow( hList );
}

START_TEST(listbox)
{
  const struct listbox_test SS =
/*   {add_style} */
    {{0},
     {LB_ERR, LB_ERR,      0, LB_ERR}, {0,0,0,0},
     {     1,      1,      1, LB_ERR}, {0,0,0,0},
     {     2,      2,      2, LB_ERR}, {0,0,0,0},
     {LB_ERR, LB_ERR,      0, LB_ERR}, {0,0,0,0}};
/* {selected, anchor,  caret, selcount}{TODO fields} */
  const struct listbox_test SS_NS =
    {{LBS_NOSEL},
     {LB_ERR, LB_ERR,      0, LB_ERR}, {0,0,0,0},
     {     1,      1,      1, LB_ERR}, {0,0,0,0},
     {     2,      2,      2, LB_ERR}, {0,0,0,0},
     {LB_ERR, LB_ERR,      0, LB_ERR}, {0,0,0,0}};
  const struct listbox_test MS =
    {{LBS_MULTIPLESEL},
     {     0, LB_ERR,      0,      0}, {0,0,0,0},
     {     1,      1,      1,      1}, {0,0,0,0},
     {     2,      1,      2,      1}, {0,0,0,0},
     {     0, LB_ERR,      0,      2}, {0,0,0,0}};
  const struct listbox_test MS_NS =
    {{LBS_MULTIPLESEL | LBS_NOSEL},
     {LB_ERR, LB_ERR,      0, LB_ERR}, {0,0,0,0},
     {     1,      1,      1, LB_ERR}, {0,0,0,0},
     {     2,      2,      2, LB_ERR}, {0,0,0,0},
     {LB_ERR, LB_ERR,      0, LB_ERR}, {0,0,0,0}};
  const struct listbox_test ES =
    {{LBS_EXTENDEDSEL},
     {     0, LB_ERR,      0,      0}, {0,0,0,0},
     {     1,      1,      1,      1}, {0,0,0,0},
     {     2,      2,      2,      1}, {0,0,0,0},
     {     0, LB_ERR,      0,      2}, {0,0,0,0}};
  const struct listbox_test ES_NS =
    {{LBS_EXTENDEDSEL | LBS_NOSEL},
     {LB_ERR, LB_ERR,      0, LB_ERR}, {0,0,0,0},
     {     1,      1,      1, LB_ERR}, {0,0,0,0},
     {     2,      2,      2, LB_ERR}, {0,0,0,0},
     {LB_ERR, LB_ERR,      0, LB_ERR}, {0,0,0,0}};
  const struct listbox_test EMS =
    {{LBS_EXTENDEDSEL | LBS_MULTIPLESEL},
     {     0, LB_ERR,      0,      0}, {0,0,0,0},
     {     1,      1,      1,      1}, {0,0,0,0},
     {     2,      2,      2,      1}, {0,0,0,0},
     {     0, LB_ERR,      0,      2}, {0,0,0,0}};
  const struct listbox_test EMS_NS =
    {{LBS_EXTENDEDSEL | LBS_MULTIPLESEL | LBS_NOSEL},
     {LB_ERR, LB_ERR,      0, LB_ERR}, {0,0,0,0},
     {     1,      1,      1, LB_ERR}, {0,0,0,0},
     {     2,      2,      2, LB_ERR}, {0,0,0,0},
     {LB_ERR, LB_ERR,      0, LB_ERR}, {0,0,0,0}};

  trace (" Testing single selection...\n");
  check (SS);
  trace (" ... with NOSEL\n");
  check (SS_NS);
  trace (" Testing multiple selection...\n");
  check (MS);
  trace (" ... with NOSEL\n");
  check (MS_NS);
  trace (" Testing extended selection...\n");
  check (ES);
  trace (" ... with NOSEL\n");
  check (ES_NS);
  trace (" Testing extended and multiple selection...\n");
  check (EMS);
  trace (" ... with NOSEL\n");
  check (EMS_NS);

  check_item_height();
  test_ownerdraw();
  test_selection();
  test_listbox_height();
  test_itemfrompoint();
  test_listbox_item_data();
}
