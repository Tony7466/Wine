/*
* Unit test suite for rich edit control
*
* Copyright 2006 Google (Thomas Kho)
* Copyright 2007 Matt Finnicum
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

#include <wine/test.h>
#include <windows.h>
#include <richedit.h>
#include <time.h>
#include <stdio.h>

static HMODULE hmoduleRichEdit;

static HWND new_window(LPCTSTR lpClassName, DWORD dwStyle, HWND parent) {
  HWND hwnd;
  hwnd = CreateWindow(lpClassName, NULL, dwStyle|WS_POPUP|WS_HSCROLL|WS_VSCROLL
                      |WS_VISIBLE, 0, 0, 200, 60, parent, NULL,
                      hmoduleRichEdit, NULL);
  ok(hwnd != NULL, "class: %s, error: %d\n", lpClassName, (int) GetLastError());
  return hwnd;
}

static HWND new_richedit(HWND parent) {
  return new_window(RICHEDIT_CLASS, ES_MULTILINE, parent);
}

static const char haystack[] = "WINEWine wineWine wine WineWine";
                             /* ^0        ^10       ^20       ^30 */

struct find_s {
  int start;
  int end;
  const char *needle;
  int flags;
  int expected_loc;
  int _todo_wine;
};


struct find_s find_tests[] = {
  /* Find in empty text */
  {0, -1, "foo", FR_DOWN, -1, 0},
  {0, -1, "foo", 0, -1, 0},
  {0, -1, "", FR_DOWN, -1, 0},
  {20, 5, "foo", FR_DOWN, -1, 0},
  {5, 20, "foo", FR_DOWN, -1, 0}
};

struct find_s find_tests2[] = {
  /* No-result find */
  {0, -1, "foo", FR_DOWN | FR_MATCHCASE, -1, 0},
  {5, 20, "WINE", FR_DOWN | FR_MATCHCASE, -1, 0},

  /* Subsequent finds */
  {0, -1, "Wine", FR_DOWN | FR_MATCHCASE, 4, 0},
  {5, 31, "Wine", FR_DOWN | FR_MATCHCASE, 13, 0},
  {14, 31, "Wine", FR_DOWN | FR_MATCHCASE, 23, 0},
  {24, 31, "Wine", FR_DOWN | FR_MATCHCASE, 27, 0},

  /* Find backwards */
  {19, 20, "Wine", FR_MATCHCASE, 13, 0},
  {10, 20, "Wine", FR_MATCHCASE, 4, 0},
  {20, 10, "Wine", FR_MATCHCASE, 13, 0},

  /* Case-insensitive */
  {1, 31, "wInE", FR_DOWN, 4, 0},
  {1, 31, "Wine", FR_DOWN, 4, 0},

  /* High-to-low ranges */
  {20, 5, "Wine", FR_DOWN, -1, 0},
  {2, 1, "Wine", FR_DOWN, -1, 0},
  {30, 29, "Wine", FR_DOWN, -1, 0},
  {20, 5, "Wine", 0, 13, 0},

  /* Find nothing */
  {5, 10, "", FR_DOWN, -1, 0},
  {10, 5, "", FR_DOWN, -1, 0},
  {0, -1, "", FR_DOWN, -1, 0},
  {10, 5, "", 0, -1, 0},

  /* Whole-word search */
  {0, -1, "wine", FR_DOWN | FR_WHOLEWORD, 18, 0},
  {0, -1, "win", FR_DOWN | FR_WHOLEWORD, -1, 0},
  {13, -1, "wine", FR_DOWN | FR_WHOLEWORD, 18, 0},
  {0, -1, "winewine", FR_DOWN | FR_WHOLEWORD, 0, 0},
  {10, -1, "winewine", FR_DOWN | FR_WHOLEWORD, 23, 0},
  {11, -1, "winewine", FR_WHOLEWORD, 0, 0},
  {31, -1, "winewine", FR_WHOLEWORD, 23, 0},
  
  /* Bad ranges */
  {5, 200, "XXX", FR_DOWN, -1, 0},
  {-20, 20, "Wine", FR_DOWN, -1, 0},
  {-20, 20, "Wine", FR_DOWN, -1, 0},
  {-15, -20, "Wine", FR_DOWN, -1, 0},
  {1<<12, 1<<13, "Wine", FR_DOWN, -1, 0},

  /* Check the case noted in bug 4479 where matches at end aren't recognized */
  {23, 31, "Wine", FR_DOWN | FR_MATCHCASE, 23, 0},
  {27, 31, "Wine", FR_DOWN | FR_MATCHCASE, 27, 0},
  {27, 32, "Wine", FR_DOWN | FR_MATCHCASE, 27, 0},
  {13, 31, "WineWine", FR_DOWN | FR_MATCHCASE, 23, 0},
  {13, 32, "WineWine", FR_DOWN | FR_MATCHCASE, 23, 0},

  /* The backwards case of bug 4479; bounds look right
   * Fails because backward find is wrong */
  {19, 20, "WINE", FR_MATCHCASE, 0, 0},
  {0, 20, "WINE", FR_MATCHCASE, -1, 0}
};

static void check_EM_FINDTEXT(HWND hwnd, const char *name, struct find_s *f, int id) {
  int findloc;
  FINDTEXT ft;
  memset(&ft, 0, sizeof(ft));
  ft.chrg.cpMin = f->start;
  ft.chrg.cpMax = f->end;
  ft.lpstrText = f->needle;
  findloc = SendMessage(hwnd, EM_FINDTEXT, f->flags, (LPARAM) &ft);
  ok(findloc == f->expected_loc,
     "EM_FINDTEXT(%s,%d) '%s' in range(%d,%d), flags %08x, got start at %d\n",
     name, id, f->needle, f->start, f->end, f->flags, findloc);
}

static void check_EM_FINDTEXTEX(HWND hwnd, const char *name, struct find_s *f,
    int id) {
  int findloc;
  FINDTEXTEX ft;
  memset(&ft, 0, sizeof(ft));
  ft.chrg.cpMin = f->start;
  ft.chrg.cpMax = f->end;
  ft.lpstrText = f->needle;
  findloc = SendMessage(hwnd, EM_FINDTEXTEX, f->flags, (LPARAM) &ft);
  ok(findloc == f->expected_loc,
      "EM_FINDTEXTEX(%s,%d) '%s' in range(%d,%d), flags %08x, start at %d\n",
      name, id, f->needle, f->start, f->end, f->flags, findloc);
  ok(ft.chrgText.cpMin == f->expected_loc,
      "EM_FINDTEXTEX(%s,%d) '%s' in range(%d,%d), flags %08x, start at %d\n",
      name, id, f->needle, f->start, f->end, f->flags, ft.chrgText.cpMin);
  ok(ft.chrgText.cpMax == ((f->expected_loc == -1) ? -1
        : f->expected_loc + strlen(f->needle)),
      "EM_FINDTEXTEX(%s,%d) '%s' in range(%d,%d), flags %08x, end at %d\n",
      name, id, f->needle, f->start, f->end, f->flags, ft.chrgText.cpMax);
}

static void run_tests_EM_FINDTEXT(HWND hwnd, const char *name, struct find_s *find,
    int num_tests)
{
  int i;

  for (i = 0; i < num_tests; i++) {
    if (find[i]._todo_wine) {
      todo_wine {
        check_EM_FINDTEXT(hwnd, name, &find[i], i);
        check_EM_FINDTEXTEX(hwnd, name, &find[i], i);
      }
    } else {
        check_EM_FINDTEXT(hwnd, name, &find[i], i);
        check_EM_FINDTEXTEX(hwnd, name, &find[i], i);
    }
  }
}

static void test_EM_FINDTEXT(void)
{
  HWND hwndRichEdit = new_richedit(NULL);

  /* Empty rich edit control */
  run_tests_EM_FINDTEXT(hwndRichEdit, "1", find_tests,
      sizeof(find_tests)/sizeof(struct find_s));

  SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) haystack);

  /* Haystack text */
  run_tests_EM_FINDTEXT(hwndRichEdit, "2", find_tests2,
      sizeof(find_tests2)/sizeof(struct find_s));

  DestroyWindow(hwndRichEdit);
}

static const struct getline_s {
  int line;
  size_t buffer_len;
  const char *text;
} gl[] = {
  {0, 10, "foo bar\r"},
  {1, 10, "\r"},
  {2, 10, "bar\r"},
  {3, 10, "\r"},

  /* Buffer smaller than line length */
  {0, 2, "foo bar\r"},
  {0, 1, "foo bar\r"},
  {0, 0, "foo bar\r"}
};

static void test_EM_GETLINE(void)
{
  int i;
  HWND hwndRichEdit = new_richedit(NULL);
  static const int nBuf = 1024;
  char dest[1024], origdest[1024];
  const char text[] = "foo bar\n"
      "\n"
      "bar\n";

  SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) text);

  memset(origdest, 0xBB, nBuf);
  for (i = 0; i < sizeof(gl)/sizeof(struct getline_s); i++)
  {
    int nCopied;
    int expected_nCopied = min(gl[i].buffer_len, strlen(gl[i].text));
    int expected_bytes_written = min(gl[i].buffer_len, strlen(gl[i].text) + 1);
    memset(dest, 0xBB, nBuf);
    *(WORD *) dest = gl[i].buffer_len;

    /* EM_GETLINE appends a "\r\0" to the end of the line
     * nCopied counts up to and including the '\r' */
    nCopied = SendMessage(hwndRichEdit, EM_GETLINE, gl[i].line, (LPARAM) dest);
    ok(nCopied == expected_nCopied, "%d: %d!=%d\n", i, nCopied,
       expected_nCopied);
    /* two special cases since a parameter is passed via dest */
    if (gl[i].buffer_len == 0)
      ok(!dest[0] && !dest[1] && !strncmp(dest+2, origdest+2, nBuf-2),
         "buffer_len=0\n");
    else if (gl[i].buffer_len == 1)
      ok(dest[0] == gl[i].text[0] && !dest[1] &&
         !strncmp(dest+2, origdest+2, nBuf-2), "buffer_len=1\n");
    else
    {
      ok(!strncmp(dest, gl[i].text, expected_bytes_written),
         "%d: expected_bytes_written=%d\n", i, expected_bytes_written);
      ok(!strncmp(dest + expected_bytes_written, origdest
                  + expected_bytes_written, nBuf - expected_bytes_written),
         "%d: expected_bytes_written=%d\n", i, expected_bytes_written);
    }
  }

  DestroyWindow(hwndRichEdit);
}

static int get_scroll_pos_y(HWND hwnd)
{
  POINT p = {-1, -1};
  SendMessage(hwnd, EM_GETSCROLLPOS, 0, (LPARAM) &p);
  ok(p.x != -1 && p.y != -1, "p.x:%d p.y:%d\n", p.x, p.y);
  return p.y;
}

static void move_cursor(HWND hwnd, long charindex)
{
  CHARRANGE cr;
  cr.cpMax = charindex;
  cr.cpMin = charindex;
  SendMessage(hwnd, EM_EXSETSEL, 0, (LPARAM) &cr);
}

static void line_scroll(HWND hwnd, int amount)
{
  SendMessage(hwnd, EM_LINESCROLL, 0, amount);
}

static void test_EM_SCROLLCARET(void)
{
  int prevY, curY;
  HWND hwndRichEdit = new_richedit(NULL);
  const char text[] = "aa\n"
      "this is a long line of text that should be longer than the "
      "control's width\n"
      "cc\n"
      "dd\n"
      "ee\n"
      "ff\n"
      "gg\n"
      "hh\n";

  /* Can't verify this */
  SendMessage(hwndRichEdit, EM_SCROLLCARET, 0, 0);

  SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) text);

  /* Caret above visible window */
  line_scroll(hwndRichEdit, 3);
  prevY = get_scroll_pos_y(hwndRichEdit);
  SendMessage(hwndRichEdit, EM_SCROLLCARET, 0, 0);
  curY = get_scroll_pos_y(hwndRichEdit);
  ok(prevY != curY, "%d == %d\n", prevY, curY);

  /* Caret below visible window */
  move_cursor(hwndRichEdit, sizeof(text) - 1);
  line_scroll(hwndRichEdit, -3);
  prevY = get_scroll_pos_y(hwndRichEdit);
  SendMessage(hwndRichEdit, EM_SCROLLCARET, 0, 0);
  curY = get_scroll_pos_y(hwndRichEdit);
  ok(prevY != curY, "%d == %d\n", prevY, curY);

  /* Caret in visible window */
  move_cursor(hwndRichEdit, sizeof(text) - 2);
  prevY = get_scroll_pos_y(hwndRichEdit);
  SendMessage(hwndRichEdit, EM_SCROLLCARET, 0, 0);
  curY = get_scroll_pos_y(hwndRichEdit);
  ok(prevY == curY, "%d != %d\n", prevY, curY);

  /* Caret still in visible window */
  line_scroll(hwndRichEdit, -1);
  prevY = get_scroll_pos_y(hwndRichEdit);
  SendMessage(hwndRichEdit, EM_SCROLLCARET, 0, 0);
  curY = get_scroll_pos_y(hwndRichEdit);
  ok(prevY == curY, "%d != %d\n", prevY, curY);

  DestroyWindow(hwndRichEdit);
}

static void test_EM_SETTEXTMODE(void)
{
  HWND hwndRichEdit = new_richedit(NULL);
  CHARFORMAT2 cf2, cf2test;
  CHARRANGE cr;
  int rc = 0;

  /*Test that EM_SETTEXTMODE fails if text exists within the control*/
  /*Insert text into the control*/

  SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) "wine");

  /*Attempt to change the control to plain text mode*/
  rc = SendMessage(hwndRichEdit, EM_SETTEXTMODE, (WPARAM) TM_PLAINTEXT, 0);
  ok(rc != 0, "EM_SETTEXTMODE: changed text mode in control containing text - returned: %d\n", rc);

  /*Test that EM_SETTEXTMODE does not allow rich edit text to be pasted.
  If rich text is pasted, it should have the same formatting as the rest
  of the text in the control*/

  /*Italicize the text
  *NOTE: If the default text was already italicized, the test will simply
  reverse; in other words, it will copy a regular "wine" into a plain
  text window that uses an italicized format*/
  cf2.cbSize = sizeof(CHARFORMAT2);
  SendMessage(hwndRichEdit, EM_GETCHARFORMAT, (WPARAM) SCF_DEFAULT,
             (LPARAM) &cf2);

  cf2.dwMask = CFM_ITALIC | cf2.dwMask;
  cf2.dwEffects = CFE_ITALIC ^ cf2.dwEffects;

  /*EM_SETCHARFORMAT is not yet fully implemented for all WPARAMs in wine;
  however, SCF_ALL has been implemented*/
  SendMessage(hwndRichEdit, EM_SETCHARFORMAT, (WPARAM) SCF_ALL, (LPARAM) &cf2);
  SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) "wine");

  /*Select the string "wine"*/
  cr.cpMin = 0;
  cr.cpMax = 4;
  SendMessage(hwndRichEdit, EM_EXSETSEL, 0, (LPARAM) &cr);

  /*Copy the italicized "wine" to the clipboard*/
  SendMessage(hwndRichEdit, WM_COPY, 0, 0);

  /*Reset the formatting to default*/
  cf2.dwEffects = CFE_ITALIC^cf2.dwEffects;
  SendMessage(hwndRichEdit, EM_SETCHARFORMAT, (WPARAM) SCF_ALL, (LPARAM) &cf2);

  /*Clear the text in the control*/
  SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) "");

  /*Switch to Plain Text Mode*/
  rc = SendMessage(hwndRichEdit, EM_SETTEXTMODE, (WPARAM) TM_PLAINTEXT, 0);
  ok(rc == 0, "EM_SETTEXTMODE: unable to switch to plain text mode with empty control:  returned: %d\n", rc);

  /*Input "wine" again in normal format*/
  SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) "wine");

  /*Paste the italicized "wine" into the control*/
  SendMessage(hwndRichEdit, WM_PASTE, 0, 0);

  /*Select a character from the first "wine" string*/
  cr.cpMin = 2;
  cr.cpMax = 3;
  SendMessage(hwndRichEdit, EM_EXSETSEL, 0, (LPARAM) &cr);

  /*Retrieve its formatting*/
  SendMessage(hwndRichEdit, EM_GETCHARFORMAT, (WPARAM) SCF_SELECTION,
              (LPARAM) &cf2);

  /*Select a character from the second "wine" string*/
  cr.cpMin = 5;
  cr.cpMax = 6;
  SendMessage(hwndRichEdit, EM_EXSETSEL, 0, (LPARAM) &cr);

  /*Retrieve its formatting*/
  cf2test.cbSize = sizeof(CHARFORMAT2);
  SendMessage(hwndRichEdit, EM_GETCHARFORMAT, (WPARAM) SCF_SELECTION,
               (LPARAM) &cf2test);

  /*Compare the two formattings*/
    ok((cf2.dwMask == cf2test.dwMask) && (cf2.dwEffects == cf2test.dwEffects),
      "two formats found in plain text mode - cf2.dwEffects: %x cf2test.dwEffects: %x\n",
       cf2.dwEffects, cf2test.dwEffects);
  /*Test TM_RICHTEXT by: switching back to Rich Text mode
                         printing "wine" in the current format(normal)
                         pasting "wine" from the clipboard(italicized)
                         comparing the two formats(should differ)*/

  /*Attempt to switch with text in control*/
  rc = SendMessage(hwndRichEdit, EM_SETTEXTMODE, (WPARAM) TM_RICHTEXT, 0);
  ok(rc != 0, "EM_SETTEXTMODE: changed from plain text to rich text with text in control - returned: %d\n", rc);

  /*Clear control*/
  SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) "");

  /*Switch into Rich Text mode*/
  rc = SendMessage(hwndRichEdit, EM_SETTEXTMODE, (WPARAM) TM_RICHTEXT, 0);
  ok(rc == 0, "EM_SETTEXTMODE: unable to change to rich text with empty control - returned: %d\n", rc);

  /*Print "wine" in normal formatting into the control*/
  SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) "wine");

  /*Paste italicized "wine" into the control*/
  SendMessage(hwndRichEdit, WM_PASTE, 0, 0);

  /*Select text from the first "wine" string*/
  cr.cpMin = 1;
  cr.cpMax = 3;
  SendMessage(hwndRichEdit, EM_EXSETSEL, 0, (LPARAM) &cr);

  /*Retrieve its formatting*/
  SendMessage(hwndRichEdit, EM_GETCHARFORMAT, (WPARAM) SCF_SELECTION,
                (LPARAM) &cf2);

  /*Select text from the second "wine" string*/
  cr.cpMin = 6;
  cr.cpMax = 7;
  SendMessage(hwndRichEdit, EM_EXSETSEL, 0, (LPARAM) &cr);

  /*Retrieve its formatting*/
  SendMessage(hwndRichEdit, EM_GETCHARFORMAT, (WPARAM) SCF_SELECTION,
                (LPARAM) &cf2test);

  /*Test that the two formattings are not the same*/
  todo_wine ok((cf2.dwMask == cf2test.dwMask) && (cf2.dwEffects != cf2test.dwEffects),
      "expected different formats - cf2.dwMask: %x, cf2test.dwMask: %x, cf2.dwEffects: %x, cf2test.dwEffects: %x\n",
      cf2.dwMask, cf2test.dwMask, cf2.dwEffects, cf2test.dwEffects);

  DestroyWindow(hwndRichEdit);
}

static void test_TM_PLAINTEXT(void)
{
  /*Tests plain text properties*/

  HWND hwndRichEdit = new_richedit(NULL);
  CHARFORMAT2 cf2, cf2test;
  CHARRANGE cr;

  /*Switch to plain text mode*/

  SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) "");
  SendMessage(hwndRichEdit, EM_SETTEXTMODE, TM_PLAINTEXT, 0);

  /*Fill control with text*/

  SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) "Is Wine an emulator? No it's not");

  /*Select some text and bold it*/

  cr.cpMin = 10;
  cr.cpMax = 20;
  SendMessage(hwndRichEdit, EM_EXSETSEL, 0, (LPARAM) &cr);
  cf2.cbSize = sizeof(CHARFORMAT2);
  SendMessage(hwndRichEdit, EM_GETCHARFORMAT, (WPARAM) SCF_DEFAULT,
	      (LPARAM) &cf2);

  cf2.dwMask = CFM_BOLD | cf2.dwMask;
  cf2.dwEffects = CFE_BOLD ^ cf2.dwEffects;

  SendMessage(hwndRichEdit, EM_SETCHARFORMAT, (WPARAM) SCF_SELECTION, (LPARAM) &cf2);

  /*Get the formatting of those characters*/

  SendMessage(hwndRichEdit, EM_GETCHARFORMAT, (WPARAM) SCF_SELECTION, (LPARAM) &cf2);

  /*Get the formatting of some other characters*/
  cf2test.cbSize = sizeof(CHARFORMAT2);
  cr.cpMin = 21;
  cr.cpMax = 30;
  SendMessage(hwndRichEdit, EM_EXSETSEL, 0, (LPARAM) &cr);
  SendMessage(hwndRichEdit, EM_GETCHARFORMAT, (WPARAM) SCF_SELECTION, (LPARAM) &cf2test);

  /*Test that they are the same as plain text allows only one formatting*/

  ok((cf2.dwMask == cf2test.dwMask) && (cf2.dwEffects == cf2test.dwEffects),
     "two selections' formats differ - cf2.dwMask: %x, cf2test.dwMask %x, cf2.dwEffects: %x, cf2test.dwEffects: %x\n",
     cf2.dwMask, cf2test.dwMask, cf2.dwEffects, cf2test.dwEffects);
  
  /*Fill the control with a "wine" string, which when inserted will be bold*/

  SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) "wine");

  /*Copy the bolded "wine" string*/

  cr.cpMin = 0;
  cr.cpMax = 4;
  SendMessage(hwndRichEdit, EM_EXSETSEL, 0, (LPARAM) &cr);
  SendMessage(hwndRichEdit, WM_COPY, 0, 0);

  /*Swap back to rich text*/

  SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) "");
  SendMessage(hwndRichEdit, EM_SETTEXTMODE, (WPARAM) TM_RICHTEXT, 0);

  /*Set the default formatting to bold italics*/

  SendMessage(hwndRichEdit, EM_GETCHARFORMAT, (WPARAM) SCF_DEFAULT, (LPARAM) &cf2);
  cf2.dwMask |= CFM_ITALIC;
  cf2.dwEffects ^= CFE_ITALIC;
  SendMessage(hwndRichEdit, EM_SETCHARFORMAT, (WPARAM) SCF_ALL, (LPARAM) &cf2);

  /*Set the text in the control to "wine", which will be bold and italicized*/

  SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) "wine");

  /*Paste the plain text "wine" string, which should take the insert
   formatting, which at the moment is bold italics*/

  SendMessage(hwndRichEdit, WM_PASTE, 0, 0);

  /*Select the first "wine" string and retrieve its formatting*/

  cr.cpMin = 1;
  cr.cpMax = 3;
  SendMessage(hwndRichEdit, EM_EXSETSEL, 0, (LPARAM) &cr);
  SendMessage(hwndRichEdit, EM_GETCHARFORMAT, (WPARAM) SCF_SELECTION, (LPARAM) &cf2);

  /*Select the second "wine" string and retrieve its formatting*/

  cr.cpMin = 5;
  cr.cpMax = 7;
  SendMessage(hwndRichEdit, EM_EXSETSEL, 0, (LPARAM) &cr);
  SendMessage(hwndRichEdit, EM_GETCHARFORMAT, (WPARAM) SCF_SELECTION, (LPARAM) &cf2test);

  /*Compare the two formattings. They should be the same.*/

  ok((cf2.dwMask == cf2test.dwMask) && (cf2.dwEffects == cf2test.dwEffects),
     "Copied text retained formatting - cf2.dwMask: %x, cf2test.dwMask: %x, cf2.dwEffects: %x, cf2test.dwEffects: %x\n",
     cf2.dwMask, cf2test.dwMask, cf2.dwEffects, cf2test.dwEffects);
  DestroyWindow(hwndRichEdit);
}

static void test_WM_GETTEXT(void)
{
    HWND hwndRichEdit = new_richedit(NULL);
    static const char text[] = "Hello. My name is RichEdit!";
    char buffer[1024] = {0};
    int result;

    SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) text);
    SendMessage(hwndRichEdit, WM_GETTEXT, 1024, (LPARAM) buffer);
    result = strcmp(buffer,text);
    ok(result == 0, 
        "WM_GETTEXT: settext and gettext differ. strcmp: %d\n", result);
    DestroyWindow(hwndRichEdit);
}

/* FIXME: need to test unimplemented options and robustly test wparam */
static void test_EM_SETOPTIONS(void)
{
    HWND hwndRichEdit = new_richedit(NULL);
    static const char text[] = "Hello. My name is RichEdit!";
    char buffer[1024] = {0};

    /* NEGATIVE TESTING - NO OPTIONS SET */
    SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) text);
    SendMessage(hwndRichEdit, EM_SETOPTIONS, ECOOP_SET, 0);

    /* testing no readonly by sending 'a' to the control*/
    SetFocus(hwndRichEdit);
    SendMessage(hwndRichEdit, WM_CHAR, 'a', 0x1E0001);
    SendMessage(hwndRichEdit, WM_GETTEXT, 1024, (LPARAM) buffer);
    ok(buffer[0]=='a', 
       "EM_SETOPTIONS: Text not changed! s1:%s s2:%s\n", text, buffer);
    SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) text);

    /* READONLY - sending 'a' to the control */
    SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) text);
    SendMessage(hwndRichEdit, EM_SETOPTIONS, ECOOP_SET, ECO_READONLY);
    SetFocus(hwndRichEdit);
    SendMessage(hwndRichEdit, WM_CHAR, 'a', 0x1E0001);
    SendMessage(hwndRichEdit, WM_GETTEXT, 1024, (LPARAM) buffer);
    ok(buffer[0]==text[0], 
       "EM_SETOPTIONS: Text changed! s1:%s s2:%s\n", text, buffer); 

    DestroyWindow(hwndRichEdit);
}

static void check_CFE_LINK_rcvd(HWND hwnd, int is_url)
{
  CHARFORMAT2W text_format;
  int link_present = 0;
  text_format.cbSize = sizeof(text_format);
  SendMessage(hwnd, EM_SETSEL, 0, 0);
  SendMessage(hwnd, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM) &text_format);
  link_present = text_format.dwEffects & CFE_LINK;
  if (is_url) 
  { /* control text is url; should get CFE_LINK */
	ok(0 != link_present, "URL Case: CFE_LINK not set.\n");
  }
  else 
  {
    ok(0 == link_present, "Non-URL Case: CFE_LINK set.\n");
  }
}

static HWND new_static_wnd(HWND parent) {
  return new_window("Static", 0, parent);
}

static void test_EM_AUTOURLDETECT(void)
{
  struct urls_s {
    const char *text;
    int is_url;
  } urls[12] = {
    {"winehq.org", 0},
    {"http://www.winehq.org", 1},
    {"http//winehq.org", 0},
    {"ww.winehq.org", 0},
    {"www.winehq.org", 1},
    {"ftp://192.168.1.1", 1},
    {"ftp//192.168.1.1", 0},
    {"mailto:your@email.com", 1},    
    {"prospero:prosperoserver", 1},
    {"telnet:test", 1},
    {"news:newserver", 1},
    {"wais:waisserver", 1}  
  };

  int i;
  int urlRet=-1;
  HWND hwndRichEdit, parent;

  parent = new_static_wnd(NULL);
  hwndRichEdit = new_richedit(parent);
  /* Try and pass EM_AUTOURLDETECT some test wParam values */
  urlRet=SendMessage(hwndRichEdit, EM_AUTOURLDETECT, FALSE, 0);
  ok(urlRet==0, "Good wParam: urlRet is: %d\n", urlRet);
  urlRet=SendMessage(hwndRichEdit, EM_AUTOURLDETECT, 1, 0);
  ok(urlRet==0, "Good wParam2: urlRet is: %d\n", urlRet);
  /* Windows returns -2147024809 (0x80070057) on bad wParam values */
  urlRet=SendMessage(hwndRichEdit, EM_AUTOURLDETECT, 8, 0);
  ok(urlRet==E_INVALIDARG, "Bad wParam: urlRet is: %d\n", urlRet);
  urlRet=SendMessage(hwndRichEdit, EM_AUTOURLDETECT, (WPARAM)"h", (LPARAM)"h");
  ok(urlRet==E_INVALIDARG, "Bad wParam2: urlRet is: %d\n", urlRet);
  /* for each url, check the text to see if CFE_LINK effect is present */
  for (i = 0; i < sizeof(urls)/sizeof(struct urls_s); i++) {
    SendMessage(hwndRichEdit, EM_AUTOURLDETECT, FALSE, 0);
    SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) urls[i].text);
    SendMessage(hwndRichEdit, WM_CHAR, 0, 0);
    check_CFE_LINK_rcvd(hwndRichEdit, 0);
    SendMessage(hwndRichEdit, EM_AUTOURLDETECT, TRUE, 0);
    SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) urls[i].text);
    SendMessage(hwndRichEdit, WM_CHAR, 0, 0);
    check_CFE_LINK_rcvd(hwndRichEdit, urls[i].is_url);
  }
  DestroyWindow(hwndRichEdit);
  DestroyWindow(parent);
}

static void test_EM_SCROLL(void)
{
  int i, j;
  int r; /* return value */
  int expr; /* expected return value */
  HWND hwndRichEdit = new_richedit(NULL);
  int y_before, y_after; /* units of lines of text */

  /* test a richedit box containing a single line of text */
  SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) "a");/* one line of text */
  expr = 0x00010000;
  for (i = 0; i < 4; i++) {
    static const int cmd[4] = { SB_PAGEDOWN, SB_PAGEUP, SB_LINEDOWN, SB_LINEUP };

    r = SendMessage(hwndRichEdit, EM_SCROLL, cmd[i], 0);
    y_after = SendMessage(hwndRichEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
    ok(expr == r, "EM_SCROLL improper return value returned (i == %d). "
       "Got 0x%08x, expected 0x%08x\n", i, r, expr);
    ok(y_after == 0, "EM_SCROLL improper scroll. scrolled to line %d, not 1 "
       "(i == %d)\n", y_after, i);
  }

  /*
   * test a richedit box that will scroll. There are two general
   * cases: the case without any long lines and the case with a long
   * line.
   */
  for (i = 0; i < 2; i++) { /* iterate through different bodies of text */
    if (i == 0)
      SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) "a\nb\nc\nd\ne");
    else
      SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM)
                  "a LONG LINE LONG LINE LONG LINE LONG LINE LONG LINE "
                  "LONG LINE LONG LINE LONG LINE LONG LINE LONG LINE "
                  "LONG LINE \nb\nc\nd\ne");
    for (j = 0; j < 12; j++) /* reset scrol position to top */
      SendMessage(hwndRichEdit, EM_SCROLL, SB_PAGEUP, 0);

    /* get first visible line */
    y_before = SendMessage(hwndRichEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
    r = SendMessage(hwndRichEdit, EM_SCROLL, SB_PAGEDOWN, 0); /* page down */

    /* get new current first visible line */
    y_after = SendMessage(hwndRichEdit, EM_GETFIRSTVISIBLELINE, 0, 0);

    ok(((r & 0xffffff00) == 0x00010000) &&
       ((r & 0x000000ff) != 0x00000000),
       "EM_SCROLL page down didn't scroll by a small positive number of "
       "lines (r == 0x%08x)\n", r);
    ok(y_after > y_before, "EM_SCROLL page down not functioning "
       "(line %d scrolled to line %d\n", y_before, y_after);

    y_before = y_after;
    
    r = SendMessage(hwndRichEdit, EM_SCROLL, SB_PAGEUP, 0); /* page up */
    y_after = SendMessage(hwndRichEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
    ok(((r & 0xffffff00) == 0x0001ff00),
       "EM_SCROLL page up didn't scroll by a small negative number of lines "
       "(r == 0x%08x)\n", r);
    ok(y_after < y_before, "EM_SCROLL page up not functioning (line "
       "%d scrolled to line %d\n", y_before, y_after);
    
    y_before = y_after;

    r = SendMessage(hwndRichEdit, EM_SCROLL, SB_LINEDOWN, 0); /* line down */

    y_after = SendMessage(hwndRichEdit, EM_GETFIRSTVISIBLELINE, 0, 0);

    ok(r == 0x00010001, "EM_SCROLL line down didn't scroll by one line "
       "(r == 0x%08x)\n", r);
    ok(y_after -1 == y_before, "EM_SCROLL line down didn't go down by "
       "1 line (%d scrolled to %d)\n", y_before, y_after);

    y_before = y_after;

    r = SendMessage(hwndRichEdit, EM_SCROLL, SB_LINEUP, 0); /* line up */

    y_after = SendMessage(hwndRichEdit, EM_GETFIRSTVISIBLELINE, 0, 0);

    ok(r == 0x0001ffff, "EM_SCROLL line up didn't scroll by one line "
       "(r == 0x%08x)\n", r);
    ok(y_after +1 == y_before, "EM_SCROLL line up didn't go up by 1 "
       "line (%d scrolled to %d)\n", y_before, y_after);

    y_before = y_after;

    r = SendMessage(hwndRichEdit, EM_SCROLL,
                    SB_LINEUP, 0); /* lineup beyond top */

    y_after = SendMessage(hwndRichEdit, EM_GETFIRSTVISIBLELINE, 0, 0);

    ok(r == 0x00010000,
       "EM_SCROLL line up returned indicating movement (0x%08x)\n", r);
    ok(y_before == y_after,
       "EM_SCROLL line up beyond top worked (%d)\n", y_after);

    y_before = y_after;

    r = SendMessage(hwndRichEdit, EM_SCROLL,
                    SB_PAGEUP, 0);/*page up beyond top */

    y_after = SendMessage(hwndRichEdit, EM_GETFIRSTVISIBLELINE, 0, 0);

    ok(r == 0x00010000,
       "EM_SCROLL page up returned indicating movement (0x%08x)\n", r);
    ok(y_before == y_after,
       "EM_SCROLL page up beyond top worked (%d)\n", y_after);

    for (j = 0; j < 12; j++) /* page down all the way to the bottom */
      SendMessage(hwndRichEdit, EM_SCROLL, SB_PAGEDOWN, 0);
    y_before = SendMessage(hwndRichEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
    r = SendMessage(hwndRichEdit, EM_SCROLL,
                    SB_PAGEDOWN, 0); /* page down beyond bot */
    y_after = SendMessage(hwndRichEdit, EM_GETFIRSTVISIBLELINE, 0, 0);

    ok(r == 0x00010000,
       "EM_SCROLL page down returned indicating movement (0x%08x)\n", r);
    ok(y_before == y_after,
       "EM_SCROLL page down beyond bottom worked (%d -> %d)\n",
       y_before, y_after);

    y_before = SendMessage(hwndRichEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
    SendMessage(hwndRichEdit, EM_SCROLL,
                SB_LINEDOWN, 0); /* line down beyond bot */
    y_after = SendMessage(hwndRichEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
    
    ok(r == 0x00010000,
       "EM_SCROLL line down returned indicating movement (0x%08x)\n", r);
    ok(y_before == y_after,
       "EM_SCROLL line down beyond bottom worked (%d -> %d)\n",
       y_before, y_after);
  }
  DestroyWindow(hwndRichEdit);
}

static void test_EM_SETUNDOLIMIT(void)
{
  /* cases we test for:
   * default behaviour - limiting at 100 undo's 
   * undo disabled - setting a limit of 0
   * undo limited -  undo limit set to some to some number, like 2
   * bad input - sending a negative number should default to 100 undo's */
 
  HWND hwndRichEdit = new_richedit(NULL);
  CHARRANGE cr;
  int i;
  int result;
  
  SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) "x");
  cr.cpMin = 0;
  cr.cpMax = 1;
  SendMessage(hwndRichEdit, WM_COPY, 0, 0);
    /*Load "x" into the clipboard. Paste is an easy, undo'able operation.
      also, multiple pastes don't combine like WM_CHAR would */
  SendMessage(hwndRichEdit, EM_EXSETSEL, 0, (LPARAM) &cr);

  /* first case - check the default */
  SendMessage(hwndRichEdit,EM_EMPTYUNDOBUFFER, 0,0); 
  for (i=0; i<101; i++) /* Put 101 undo's on the stack */
    SendMessage(hwndRichEdit, WM_PASTE, 0, 0); 
  for (i=0; i<100; i++) /* Undo 100 of them */
    SendMessage(hwndRichEdit, WM_UNDO, 0, 0); 
  ok(!SendMessage(hwndRichEdit, EM_CANUNDO, 0, 0),
     "EM_SETUNDOLIMIT allowed more than a hundred undo's by default.\n");

  /* second case - cannot undo */
  SendMessage(hwndRichEdit,EM_EMPTYUNDOBUFFER, 0, 0); 
  SendMessage(hwndRichEdit, EM_SETUNDOLIMIT, 0, 0); 
  SendMessage(hwndRichEdit,
              WM_PASTE, 0, 0); /* Try to put something in the undo stack */
  ok(!SendMessage(hwndRichEdit, EM_CANUNDO, 0, 0),
     "EM_SETUNDOLIMIT allowed undo with UNDOLIMIT set to 0\n");

  /* third case - set it to an arbitrary number */
  SendMessage(hwndRichEdit,EM_EMPTYUNDOBUFFER, 0, 0); 
  SendMessage(hwndRichEdit, EM_SETUNDOLIMIT, 2, 0); 
  SendMessage(hwndRichEdit, WM_PASTE, 0, 0);
  SendMessage(hwndRichEdit, WM_PASTE, 0, 0);
  SendMessage(hwndRichEdit, WM_PASTE, 0, 0); 
  /* If SETUNDOLIMIT is working, there should only be two undo's after this */
  ok(SendMessage(hwndRichEdit, EM_CANUNDO, 0,0),
     "EM_SETUNDOLIMIT didn't allow the first undo with UNDOLIMIT set to 2\n");
  SendMessage(hwndRichEdit, WM_UNDO, 0, 0);
  ok(SendMessage(hwndRichEdit, EM_CANUNDO, 0, 0),
     "EM_SETUNDOLIMIT didn't allow a second undo with UNDOLIMIT set to 2\n");
  SendMessage(hwndRichEdit, WM_UNDO, 0, 0); 
  ok(!SendMessage(hwndRichEdit, EM_CANUNDO, 0, 0),
     "EM_SETUNDOLIMIT allowed a third undo with UNDOLIMIT set to 2\n");
  
  /* fourth case - setting negative numbers should default to 100 undos */
  SendMessage(hwndRichEdit,EM_EMPTYUNDOBUFFER, 0,0); 
  result = SendMessage(hwndRichEdit, EM_SETUNDOLIMIT, -1, 0);
  ok (result == 100, 
      "EM_SETUNDOLIMIT returned %d when set to -1, instead of 100\n",result);
      
  DestroyWindow(hwndRichEdit);
}

static void test_ES_PASSWORD(void)
{
  /* This isn't hugely testable, so we're just going to run it through it's paces. */

  HWND hwndRichEdit = new_richedit(NULL);
  WCHAR result;

  /* First, check the default of a regular control */
  result = SendMessage(hwndRichEdit, EM_GETPASSWORDCHAR, 0, 0);
  ok (result == 0,
	"EM_GETPASSWORDCHAR returned %c by default, instead of NULL\n",result);

  /* Now, set it to something normal */
  SendMessage(hwndRichEdit, EM_SETPASSWORDCHAR, 'x', 0);
  result = SendMessage(hwndRichEdit, EM_GETPASSWORDCHAR, 0, 0);
  ok (result == 120,
	"EM_GETPASSWORDCHAR returned %c (%d) when set to 'x', instead of x (120)\n",result,result);

  /* Now, set it to something odd */
  SendMessage(hwndRichEdit, EM_SETPASSWORDCHAR, (WCHAR)1234, 0);
  result = SendMessage(hwndRichEdit, EM_GETPASSWORDCHAR, 0, 0);
  ok (result == 1234,
	"EM_GETPASSWORDCHAR returned %c (%d) when set to 'x', instead of x (120)\n",result,result);
  DestroyWindow(hwndRichEdit);
}

static void test_EM_SETTEXTEX(void)
{
  HWND hwndRichEdit = new_richedit(NULL);
  SETTEXTEX setText;
  GETTEXTEX getText;
  WCHAR TestItem1[] = {'T', 'e', 's', 't', 
                       'S', 'o', 'm', 'e', 
                       'T', 'e', 'x', 't', 0}; 
#define MAX_BUF_LEN 1024
  WCHAR buf[MAX_BUF_LEN];
  int result;
  CHARRANGE cr;

  setText.codepage = 1200;  /* no constant for unicode */
  getText.codepage = 1200;  /* no constant for unicode */
  getText.cb = MAX_BUF_LEN;
  getText.flags = GT_DEFAULT;

  setText.flags = 0;
  SendMessage(hwndRichEdit, EM_SETTEXTEX, (WPARAM)&setText, (LPARAM) TestItem1);
  SendMessage(hwndRichEdit, EM_GETTEXTEX, (WPARAM)&getText, (LPARAM) buf);
  ok(lstrcmpW(buf, TestItem1) == 0,
      "EM_GETTEXTEX results not what was set by EM_SETTEXTEX\n");

  result = SendMessage(hwndRichEdit, EM_SETTEXTEX, 
                       (WPARAM)&setText, (LPARAM) NULL);
  SendMessage(hwndRichEdit, EM_GETTEXTEX, (WPARAM)&getText, (LPARAM) buf);
  
  ok (result == 1, 
      "EM_SETTEXTEX returned %d, instead of 1\n",result);
  ok(lstrlenW(buf) == 0,
      "EM_SETTEXTEX with NULL lParam should clear rich edit.\n");
  
  /* put some text back */
  setText.flags = 0;
  SendMessage(hwndRichEdit, EM_SETTEXTEX, (WPARAM)&setText, (LPARAM) TestItem1);
  /* select some text */
  cr.cpMax = 1;
  cr.cpMin = 3;
  SendMessage(hwndRichEdit, EM_EXSETSEL, 0, (LPARAM) &cr);
  /* replace current selection */
  setText.flags = ST_SELECTION;
  result = SendMessage(hwndRichEdit, EM_SETTEXTEX, 
                       (WPARAM)&setText, (LPARAM) NULL);
  ok(result == 0,
      "EM_SETTEXTEX with NULL lParam to replace selection"
      " with no text should return 0. Got %i\n",
      result);
  
  /* put some text back */
  setText.flags = 0;
  SendMessage(hwndRichEdit, EM_SETTEXTEX, (WPARAM)&setText, (LPARAM) TestItem1);
  /* select some text */
  cr.cpMax = 1;
  cr.cpMin = 3;
  SendMessage(hwndRichEdit, EM_EXSETSEL, 0, (LPARAM) &cr);
  /* replace current selection */
  setText.flags = ST_SELECTION;
  result = SendMessage(hwndRichEdit, EM_SETTEXTEX,
                       (WPARAM)&setText, (LPARAM) TestItem1);
  /* get text */
  SendMessage(hwndRichEdit, EM_GETTEXTEX, (WPARAM)&getText, (LPARAM) buf);
  ok(result == lstrlenW(TestItem1),
      "EM_SETTEXTEX with NULL lParam to replace selection"
      " with no text should return 0. Got %i\n",
      result);
  ok(lstrlenW(buf) == 22,
      "EM_SETTEXTEX to replace selection with more text failed: %i.\n",
      lstrlenW(buf) );

  DestroyWindow(hwndRichEdit);
}

static void test_EM_LIMITTEXT(void)
{
  int ret;

  HWND hwndRichEdit = new_richedit(NULL);

  /* The main purpose of this test is to demonstrate that the nonsense in MSDN
   * about setting the length to -1 for multiline edit controls doesn't happen.
   */

  /* Don't check default gettextlimit case. That's done in other tests */

  /* Set textlimit to 100 */
  SendMessage (hwndRichEdit, EM_LIMITTEXT, 100, 0);
  ret = SendMessage (hwndRichEdit, EM_GETLIMITTEXT, 0, 0);
  ok (ret == 100,
      "EM_LIMITTEXT: set to 100, returned: %d, expected: 100\n", ret);

  /* Set textlimit to 0 */
  SendMessage (hwndRichEdit, EM_LIMITTEXT, 0, 0);
  ret = SendMessage (hwndRichEdit, EM_GETLIMITTEXT, 0, 0);
  ok (ret == 65536,
      "EM_LIMITTEXT: set to 0, returned: %d, expected: 65536\n", ret);

  /* Set textlimit to -1 */
  SendMessage (hwndRichEdit, EM_LIMITTEXT, -1, 0);
  ret = SendMessage (hwndRichEdit, EM_GETLIMITTEXT, 0, 0);
  ok (ret == -1,
      "EM_LIMITTEXT: set to -1, returned: %d, expected: -1\n", ret);

  /* Set textlimit to -2 */
  SendMessage (hwndRichEdit, EM_LIMITTEXT, -2, 0);
  ret = SendMessage (hwndRichEdit, EM_GETLIMITTEXT, 0, 0);
  ok (ret == -2,
      "EM_LIMITTEXT: set to -2, returned: %d, expected: -2\n", ret);

  DestroyWindow (hwndRichEdit);
}


static void test_EM_EXLIMITTEXT(void)
{
  int i, selBegin, selEnd, len1, len2;
  char text[1024 + 1];
  int textlimit = 0; /* multiple of 100 */
  HWND hwndRichEdit = new_richedit(NULL);
  
  i = SendMessage(hwndRichEdit, EM_GETLIMITTEXT, 0, 0);
  ok(32767 == i, "EM_EXLIMITTEXT: expected: %d, actual: %d\n", 32767, i); /* default */
  
  textlimit = 256000;
  SendMessage(hwndRichEdit, EM_EXLIMITTEXT, 0, textlimit);
  i = SendMessage(hwndRichEdit, EM_GETLIMITTEXT, 0, 0);
  /* set higher */
  ok(textlimit == i, "EM_EXLIMITTEXT: expected: %d, actual: %d\n", textlimit, i);
  
  textlimit = 1000;
  SendMessage(hwndRichEdit, EM_EXLIMITTEXT, 0, textlimit);
  i = SendMessage(hwndRichEdit, EM_GETLIMITTEXT, 0, 0);
  /* set lower */
  ok(textlimit == i, "EM_EXLIMITTEXT: expected: %d, actual: %d\n", textlimit, i);
 
  SendMessage(hwndRichEdit, EM_EXLIMITTEXT, 0, 0);
  i = SendMessage(hwndRichEdit, EM_GETLIMITTEXT, 0, 0);
  /* default for WParam = 0 */
  ok(65536 == i, "EM_EXLIMITTEXT: expected: %d, actual: %d\n", 65536, i);
 
  textlimit = sizeof(text)-1;
  memset(text, 'W', textlimit);
  text[sizeof(text)-1] = 0;
  SendMessage(hwndRichEdit, EM_EXLIMITTEXT, 0, textlimit);
  /* maxed out text */
  SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) text);
  
  SendMessage(hwndRichEdit, EM_SETSEL, 0, -1);  /* select everything */
  SendMessage(hwndRichEdit, EM_GETSEL, (WPARAM)&selBegin, (LPARAM)&selEnd);
  len1 = selEnd - selBegin;
  
  SendMessage(hwndRichEdit, WM_KEYDOWN, VK_BACK, 1);
  SendMessage(hwndRichEdit, WM_CHAR, VK_BACK, 1);
  SendMessage(hwndRichEdit, WM_KEYUP, VK_BACK, 1);
  SendMessage(hwndRichEdit, EM_SETSEL, 0, -1);
  SendMessage(hwndRichEdit, EM_GETSEL, (WPARAM)&selBegin, (LPARAM)&selEnd);
  len2 = selEnd - selBegin;
  
  ok(len1 != len2,
    "EM_EXLIMITTEXT: Change Expected\nOld Length: %d, New Length: %d, Limit: %d\n",
    len1,len2,i);
  
  SendMessage(hwndRichEdit, WM_KEYDOWN, 'A', 1);
  SendMessage(hwndRichEdit, WM_CHAR, 'A', 1);
  SendMessage(hwndRichEdit, WM_KEYUP, 'A', 1);
  SendMessage(hwndRichEdit, EM_SETSEL, 0, -1);
  SendMessage(hwndRichEdit, EM_GETSEL, (WPARAM)&selBegin, (LPARAM)&selEnd);
  len1 = selEnd - selBegin;
  
  ok(len1 != len2,
    "EM_EXLIMITTEXT: Change Expected\nOld Length: %d, New Length: %d, Limit: %d\n",
    len1,len2,i);
  
  SendMessage(hwndRichEdit, WM_KEYDOWN, 'A', 1);
  SendMessage(hwndRichEdit, WM_CHAR, 'A', 1);
  SendMessage(hwndRichEdit, WM_KEYUP, 'A', 1);  /* full; should be no effect */
  SendMessage(hwndRichEdit, EM_SETSEL, 0, -1);
  SendMessage(hwndRichEdit, EM_GETSEL, (WPARAM)&selBegin, (LPARAM)&selEnd);
  len2 = selEnd - selBegin;
  
  ok(len1 == len2, 
    "EM_EXLIMITTEXT: No Change Expected\nOld Length: %d, New Length: %d, Limit: %d\n",
    len1,len2,i);

  DestroyWindow(hwndRichEdit);
}

static void test_EM_GETLIMITTEXT(void)
{
  int i;
  HWND hwndRichEdit = new_richedit(NULL);

  i = SendMessage(hwndRichEdit, EM_GETLIMITTEXT, 0, 0);
  ok(32767 == i, "expected: %d, actual: %d\n", 32767, i); /* default value */

  SendMessage(hwndRichEdit, EM_EXLIMITTEXT, 0, 50000);
  i = SendMessage(hwndRichEdit, EM_GETLIMITTEXT, 0, 0);
  ok(50000 == i, "expected: %d, actual: %d\n", 50000, i);

  DestroyWindow(hwndRichEdit);
}

static void test_WM_SETFONT(void)
{
  /* There is no invalid input or error conditions for this function.
   * NULL wParam and lParam just fall back to their default values 
   * It should be noted that even if you use a gibberish name for your fonts
   * here, it will still work because the name is stored. They will display as
   * System, but will report their name to be whatever they were created as */
  
  HWND hwndRichEdit = new_richedit(NULL);
  HFONT testFont1 = CreateFontA (0,0,0,0,FW_LIGHT, 0, 0, 0, ANSI_CHARSET, 
    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | 
    FF_DONTCARE, "Marlett");
  HFONT testFont2 = CreateFontA (0,0,0,0,FW_LIGHT, 0, 0, 0, ANSI_CHARSET, 
    OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | 
    FF_DONTCARE, "MS Sans Serif");
  HFONT testFont3 = CreateFontA (0,0,0,0,FW_LIGHT, 0, 0, 0, ANSI_CHARSET, 
    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | 
    FF_DONTCARE, "Courier");
  LOGFONTA sentLogFont;
  CHARFORMAT2A returnedCF2A;
  
  returnedCF2A.cbSize = sizeof(returnedCF2A);
  
  SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) "x");
  SendMessage(hwndRichEdit, WM_SETFONT, (WPARAM)testFont1,(LPARAM) MAKELONG((WORD) TRUE, 0));
  SendMessage(hwndRichEdit, EM_GETCHARFORMAT,   SCF_DEFAULT,  (LPARAM) &returnedCF2A);

  GetObjectA(testFont1, sizeof(LOGFONTA), &sentLogFont);
  ok (!strcmp(sentLogFont.lfFaceName,returnedCF2A.szFaceName),
    "EM_GETCHARFOMAT: Returned wrong font on test 1. Sent: %s, Returned: %s\n",
    sentLogFont.lfFaceName,returnedCF2A.szFaceName);

  SendMessage(hwndRichEdit, WM_SETFONT, (WPARAM)testFont2,(LPARAM) MAKELONG((WORD) TRUE, 0));
  SendMessage(hwndRichEdit, EM_GETCHARFORMAT,   SCF_DEFAULT,  (LPARAM) &returnedCF2A);
  GetObjectA(testFont2, sizeof(LOGFONTA), &sentLogFont);
  ok (!strcmp(sentLogFont.lfFaceName,returnedCF2A.szFaceName),
    "EM_GETCHARFOMAT: Returned wrong font on test 2. Sent: %s, Returned: %s\n",
    sentLogFont.lfFaceName,returnedCF2A.szFaceName);
    
  SendMessage(hwndRichEdit, WM_SETFONT, (WPARAM)testFont3,(LPARAM) MAKELONG((WORD) TRUE, 0));
  SendMessage(hwndRichEdit, EM_GETCHARFORMAT,   SCF_DEFAULT,  (LPARAM) &returnedCF2A);
  GetObjectA(testFont3, sizeof(LOGFONTA), &sentLogFont);
  ok (!strcmp(sentLogFont.lfFaceName,returnedCF2A.szFaceName),
    "EM_GETCHARFOMAT: Returned wrong font on test 3. Sent: %s, Returned: %s\n",
    sentLogFont.lfFaceName,returnedCF2A.szFaceName);
   
  /* This last test is special since we send in NULL. We clear the variables
   * and just compare to "System" instead of the sent in font name. */
  ZeroMemory(&returnedCF2A,sizeof(returnedCF2A));
  ZeroMemory(&sentLogFont,sizeof(sentLogFont));
  returnedCF2A.cbSize = sizeof(returnedCF2A);
  
  SendMessage(hwndRichEdit, WM_SETFONT, (WPARAM)NULL,(LPARAM) MAKELONG((WORD) TRUE, 0));
  SendMessage(hwndRichEdit, EM_GETCHARFORMAT,   SCF_DEFAULT,  (LPARAM) &returnedCF2A);
  GetObjectA(NULL, sizeof(LOGFONTA), &sentLogFont);
  ok (!strcmp("System",returnedCF2A.szFaceName),
    "EM_GETCHARFOMAT: Returned wrong font on test 4. Sent: NULL, Returned: %s. Expected \"System\".\n",returnedCF2A.szFaceName);
  
  DestroyWindow(hwndRichEdit);
}


static DWORD CALLBACK test_EM_GETMODIFY_esCallback(DWORD_PTR dwCookie,
                                         LPBYTE pbBuff,
                                         LONG cb,
                                         LONG *pcb)
{
  const char** str = (const char**)dwCookie;
  int size = strlen(*str);
  if(size > 3)  /* let's make it peice-meal for fun */
    size = 3;
  *pcb = cb;
  if (*pcb > size) {
    *pcb = size;
  }
  if (*pcb > 0) {
    memcpy(pbBuff, *str, *pcb);
    *str += *pcb;
  }
  return 0;
}

static void test_EM_GETMODIFY(void)
{
  HWND hwndRichEdit = new_richedit(NULL);
  LRESULT result;
  SETTEXTEX setText;
  WCHAR TestItem1[] = {'T', 'e', 's', 't', 
                       'S', 'o', 'm', 'e', 
                       'T', 'e', 'x', 't', 0}; 
  WCHAR TestItem2[] = {'T', 'e', 's', 't', 
                       'S', 'o', 'm', 'e', 
                       'O', 't', 'h', 'e', 'r',
                       'T', 'e', 'x', 't', 0}; 
  const char* streamText = "hello world";
  CHARFORMAT2 cf2;
  PARAFORMAT2 pf2;
  EDITSTREAM es;
  
  HFONT testFont = CreateFontA (0,0,0,0,FW_LIGHT, 0, 0, 0, ANSI_CHARSET, 
    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | 
    FF_DONTCARE, "Courier");
  
  setText.codepage = 1200;  /* no constant for unicode */
  setText.flags = ST_KEEPUNDO;
  

  /* modify flag shouldn't be set when richedit is first created */
  result = SendMessage(hwndRichEdit, EM_GETMODIFY, 0, 0);
  ok (result == 0, 
      "EM_GETMODIFY returned non-zero, instead of zero on create\n");
  
  /* setting modify flag should actually set it */
  SendMessage(hwndRichEdit, EM_SETMODIFY, TRUE, 0);
  result = SendMessage(hwndRichEdit, EM_GETMODIFY, 0, 0);
  ok (result != 0, 
      "EM_GETMODIFY returned zero, instead of non-zero on EM_SETMODIFY\n");
  
  /* clearing modify flag should actually clear it */
  SendMessage(hwndRichEdit, EM_SETMODIFY, FALSE, 0);
  result = SendMessage(hwndRichEdit, EM_GETMODIFY, 0, 0);
  ok (result == 0, 
      "EM_GETMODIFY returned non-zero, instead of zero on EM_SETMODIFY\n");
 
  /* setting font doesn't change modify flag */
  SendMessage(hwndRichEdit, EM_SETMODIFY, FALSE, 0);
  SendMessage(hwndRichEdit, WM_SETFONT, (WPARAM)testFont,(LPARAM) MAKELONG((WORD) TRUE, 0));
  result = SendMessage(hwndRichEdit, EM_GETMODIFY, 0, 0);
  ok (result == 0,
      "EM_GETMODIFY returned non-zero, instead of zero on setting font\n");

  /* setting text should set modify flag */
  SendMessage(hwndRichEdit, EM_SETMODIFY, FALSE, 0);
  SendMessage(hwndRichEdit, EM_SETTEXTEX, (WPARAM)&setText, (LPARAM)TestItem1);
  result = SendMessage(hwndRichEdit, EM_GETMODIFY, 0, 0);
  ok (result != 0,
      "EM_GETMODIFY returned zero, instead of non-zero on setting text\n");
  
  /* undo previous text doesn't reset modify flag */
  SendMessage(hwndRichEdit, WM_UNDO, 0, 0);
  result = SendMessage(hwndRichEdit, EM_GETMODIFY, 0, 0);
  ok (result != 0,
      "EM_GETMODIFY returned zero, instead of non-zero on undo after setting text\n");
  
  /* set text with no flag to keep undo stack should not set modify flag */
  SendMessage(hwndRichEdit, EM_SETMODIFY, FALSE, 0);
  setText.flags = 0;
  SendMessage(hwndRichEdit, EM_SETTEXTEX, (WPARAM)&setText, (LPARAM)TestItem1);
  result = SendMessage(hwndRichEdit, EM_GETMODIFY, 0, 0);
  ok (result == 0,
      "EM_GETMODIFY returned non-zero, instead of zero when setting text while not keeping undo stack\n");
  
  /* WM_SETTEXT doesn't modify */
  SendMessage(hwndRichEdit, EM_SETMODIFY, FALSE, 0);
  SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM)TestItem2);
  result = SendMessage(hwndRichEdit, EM_GETMODIFY, 0, 0);
  todo_wine {
  ok (result == 0,
      "EM_GETMODIFY returned non-zero for WM_SETTEXT\n");
  }
  
  /* clear the text */
  SendMessage(hwndRichEdit, EM_SETMODIFY, FALSE, 0);
  SendMessage(hwndRichEdit, WM_CLEAR, 0, 0);
  result = SendMessage(hwndRichEdit, EM_GETMODIFY, 0, 0);
  ok (result == 0,
      "EM_GETMODIFY returned non-zero, instead of zero for WM_CLEAR\n");
  
  /* replace text */
  SendMessage(hwndRichEdit, EM_SETMODIFY, FALSE, 0);
  SendMessage(hwndRichEdit, EM_SETTEXTEX, (WPARAM)&setText, (LPARAM)TestItem1);
  SendMessage(hwndRichEdit, EM_SETSEL, 0, 2);
  SendMessage(hwndRichEdit, EM_REPLACESEL, TRUE, (LPARAM)TestItem2);
  result = SendMessage(hwndRichEdit, EM_GETMODIFY, 0, 0);
  ok (result != 0,
      "EM_GETMODIFY returned zero, instead of non-zero when replacing text\n");
  
  /* copy/paste text 1 */
  SendMessage(hwndRichEdit, EM_SETMODIFY, FALSE, 0);
  SendMessage(hwndRichEdit, EM_SETSEL, 0, 2);
  SendMessage(hwndRichEdit, WM_COPY, 0, 0);
  SendMessage(hwndRichEdit, WM_PASTE, 0, 0);
  result = SendMessage(hwndRichEdit, EM_GETMODIFY, 0, 0);
  ok (result != 0,
      "EM_GETMODIFY returned zero, instead of non-zero when pasting identical text\n");
  
  /* copy/paste text 2 */
  SendMessage(hwndRichEdit, EM_SETMODIFY, FALSE, 0);
  SendMessage(hwndRichEdit, EM_SETSEL, 0, 2);
  SendMessage(hwndRichEdit, WM_COPY, 0, 0);
  SendMessage(hwndRichEdit, EM_SETSEL, 0, 3);
  SendMessage(hwndRichEdit, WM_PASTE, 0, 0);
  result = SendMessage(hwndRichEdit, EM_GETMODIFY, 0, 0);
  ok (result != 0,
      "EM_GETMODIFY returned zero, instead of non-zero when pasting different text\n");
  
  /* press char */
  SendMessage(hwndRichEdit, EM_SETMODIFY, FALSE, 0);
  SendMessage(hwndRichEdit, EM_SETSEL, 0, 1);
  SendMessage(hwndRichEdit, WM_CHAR, 'A', 0);
  result = SendMessage(hwndRichEdit, EM_GETMODIFY, 0, 0);
  ok (result != 0,
      "EM_GETMODIFY returned zero, instead of non-zero for WM_CHAR\n");
  
  /* set char format */
  SendMessage(hwndRichEdit, EM_SETMODIFY, FALSE, 0);
  cf2.cbSize = sizeof(CHARFORMAT2);
  SendMessage(hwndRichEdit, EM_GETCHARFORMAT, (WPARAM) SCF_DEFAULT,
             (LPARAM) &cf2);
  cf2.dwMask = CFM_ITALIC | cf2.dwMask;
  cf2.dwEffects = CFE_ITALIC ^ cf2.dwEffects;
  SendMessage(hwndRichEdit, EM_SETCHARFORMAT, (WPARAM) SCF_ALL, (LPARAM) &cf2);
  result = SendMessage(hwndRichEdit, EM_GETMODIFY, 0, 0);
  ok (result != 0,
      "EM_GETMODIFY returned zero, instead of non-zero for EM_SETCHARFORMAT\n");
  
  /* set para format */
  SendMessage(hwndRichEdit, EM_SETMODIFY, FALSE, 0);
  pf2.cbSize = sizeof(PARAFORMAT2);
  SendMessage(hwndRichEdit, EM_GETPARAFORMAT, 0,
             (LPARAM) &pf2);
  pf2.dwMask = PFM_ALIGNMENT | pf2.dwMask;
  pf2.wAlignment = PFA_RIGHT;
  SendMessage(hwndRichEdit, EM_SETPARAFORMAT, 0, (LPARAM) &pf2);
  result = SendMessage(hwndRichEdit, EM_GETMODIFY, 0, 0);
  ok (result == 0,
      "EM_GETMODIFY returned zero, instead of non-zero for EM_SETPARAFORMAT\n");

  /* EM_STREAM */
  SendMessage(hwndRichEdit, EM_SETMODIFY, FALSE, 0);
  es.dwCookie = (DWORD_PTR)&streamText;
  es.dwError = 0;
  es.pfnCallback = test_EM_GETMODIFY_esCallback;
  SendMessage(hwndRichEdit, EM_STREAMIN, 
              (WPARAM)(SF_TEXT), (LPARAM)&es);
  result = SendMessage(hwndRichEdit, EM_GETMODIFY, 0, 0);
  ok (result != 0,
      "EM_GETMODIFY returned zero, instead of non-zero for EM_STREAM\n");

  DestroyWindow(hwndRichEdit);
}

struct exsetsel_s {
  long min;
  long max;
  long expected_retval;
  int expected_getsel_start;
  int expected_getsel_end;
  int _exsetsel_todo_wine;
  int _getsel_todo_wine;
};

const struct exsetsel_s exsetsel_tests[] = {
  /* sanity tests */
  {5, 10, 10, 5, 10, 0, 0},
  {15, 17, 17, 15, 17, 0, 0},
  /* test cpMax > strlen() */
  {0, 100, 18, 0, 18, 0, 1},
  /* test cpMin == cpMax */
  {5, 5, 5, 5, 5, 0, 0},
  /* test cpMin < 0 && cpMax >= 0 (bug 4462) */
  {-1, 0, 5, 5, 5, 0, 0},
  {-1, 17, 5, 5, 5, 0, 0},
  {-1, 18, 5, 5, 5, 0, 0},
  /* test cpMin < 0 && cpMax < 0 */
  {-1, -1, 17, 17, 17, 0, 0},
  {-4, -5, 17, 17, 17, 0, 0},
  /* test cMin >=0 && cpMax < 0 (bug 6814) */
  {0, -1, 18, 0, 18, 0, 1},
  {17, -5, 18, 17, 18, 0, 1},
  {18, -3, 17, 17, 17, 0, 0},
  /* test if cpMin > cpMax */
  {15, 19, 18, 15, 18, 0, 1},
  {19, 15, 18, 15, 18, 0, 1}
};

static void check_EM_EXSETSEL(HWND hwnd, const struct exsetsel_s *setsel, int id) {
    CHARRANGE cr;
    long result;
    int start, end;

    cr.cpMin = setsel->min;
    cr.cpMax = setsel->max;
    result = SendMessage(hwnd, EM_EXSETSEL, 0, (LPARAM) &cr);

    if (setsel->_exsetsel_todo_wine) {
        todo_wine {
            ok(result == setsel->expected_retval, "EM_EXSETSEL(%d): expected: %ld actual: %ld\n", id, setsel->expected_retval, result);
        }
    } else {
        ok(result == setsel->expected_retval, "EM_EXSETSEL(%d): expected: %ld actual: %ld\n", id, setsel->expected_retval, result);
    }

    SendMessage(hwnd, EM_GETSEL, (WPARAM) &start, (LPARAM) &end);

    if (setsel->_getsel_todo_wine) {
        todo_wine {
            ok(start == setsel->expected_getsel_start && end == setsel->expected_getsel_end, "EM_EXSETSEL(%d): expected (%d,%d) actual:(%d,%d)\n", id, setsel->expected_getsel_start, setsel->expected_getsel_end, start, end);
        }
    } else {
        ok(start == setsel->expected_getsel_start && end == setsel->expected_getsel_end, "EM_EXSETSEL(%d): expected (%d,%d) actual:(%d,%d)\n", id, setsel->expected_getsel_start, setsel->expected_getsel_end, start, end);
    }
}

static void test_EM_EXSETSEL(void)
{
    HWND hwndRichEdit = new_richedit(NULL);
    int i;
    const int num_tests = sizeof(exsetsel_tests)/sizeof(struct exsetsel_s);

    /* sending some text to the window */
    SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) "testing selection");
    /*                                                 01234567890123456*/
    /*                                                          10      */

    for (i = 0; i < num_tests; i++) {
        check_EM_EXSETSEL(hwndRichEdit, &exsetsel_tests[i], i);
    }

    DestroyWindow(hwndRichEdit);
}

static void test_WM_PASTE(void)
{
    int result;
    char buffer[1024] = {0};
    const char* text1 = "testing paste\r";
    const char* text2 = "testing paste\r\rtesting paste";
    const char* text3 = "testing paste\rpaste\rtesting paste";
    HWND hwndRichEdit = new_richedit(NULL);

    SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) text1);
    SendMessage(hwndRichEdit, EM_SETSEL, 0, 14);
    SendMessage(hwndRichEdit, WM_CHAR, 3, 0);  /* ctrl-c */
    SendMessage(hwndRichEdit, EM_SETSEL, 14, 14);
    SendMessage(hwndRichEdit, WM_CHAR, 22, 0);  /* ctrl-v */
    SendMessage(hwndRichEdit, WM_CHAR, 26, 0);  /* ctrl-z */
    SendMessage(hwndRichEdit, WM_GETTEXT, 1024, (LPARAM) buffer);
    result = strcmp(text1, buffer);
    ok(result == 0,
        "test paste: strcmp = %i\n", result);

    SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) text2);
    SendMessage(hwndRichEdit, EM_SETSEL, 8, 13);
    SendMessage(hwndRichEdit, WM_CHAR, 3, 0);  /* ctrl-c */
    SendMessage(hwndRichEdit, EM_SETSEL, 14, 14);
    SendMessage(hwndRichEdit, WM_CHAR, 22, 0);  /* ctrl-v */
    SendMessage(hwndRichEdit, WM_CHAR, 26, 0);  /* ctrl-z */
    SendMessage(hwndRichEdit, WM_CHAR, 25, 0);  /* ctrl-y */
    SendMessage(hwndRichEdit, WM_GETTEXT, 1024, (LPARAM) buffer);
    result = strcmp(buffer,text3);
    ok(result == 0,
        "test paste: strcmp = %i\n", result);

    DestroyWindow(hwndRichEdit);
}

static int nCallbackCount = 0;

static DWORD CALLBACK EditStreamCallback(DWORD_PTR dwCookie, LPBYTE pbBuff,
				 LONG cb, LONG* pcb)
{
  const char text[] = {'t','e','s','t'};

  if (sizeof(text) <= cb)
  {
    if ((int)dwCookie != nCallbackCount)
    {
      *pcb = 0;
      return 0;
    }

    memcpy (pbBuff, text, sizeof(text));
    *pcb = sizeof(text);

    nCallbackCount++;

    return 0;
  }
  else
    return 1; /* indicates callback failed */
}

static void test_EM_StreamIn_Undo(void)
{
  /* The purpose of this test is to determine when a EM_StreamIn should be
   * undoable. This is important because WM_PASTE currently uses StreamIn and
   * pasting should always be undoable but streaming isn't always.
   *
   * cases to test:
   * StreamIn plain text without SFF_SELECTION.
   * StreamIn plain text with SFF_SELECTION set but a zero-length selection
   * StreamIn plain text with SFF_SELECTION and a valid, normal selection
   * StreamIn plain text with SFF_SELECTION and a backwards-selection (from>to)
   * Feel free to add tests for other text modes or StreamIn things.
   */


  HWND hwndRichEdit = new_richedit(NULL);
  LRESULT result;
  EDITSTREAM es;
  char buffer[1024] = {0};
  const char randomtext[] = "Some text";

  es.pfnCallback = (EDITSTREAMCALLBACK) EditStreamCallback;

  /* StreamIn, no SFF_SELECTION */
  es.dwCookie = nCallbackCount;
  SendMessage(hwndRichEdit,EM_EMPTYUNDOBUFFER, 0,0);
  SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) randomtext);
  SendMessage(hwndRichEdit, EM_SETSEL,0,0);
  SendMessage(hwndRichEdit, EM_STREAMIN, (WPARAM)SF_TEXT, (LPARAM)&es);
  SendMessage(hwndRichEdit, WM_GETTEXT, 1024, (LPARAM) buffer);
  result = strcmp (buffer,"test");
  ok (result  == 0,
      "EM_STREAMIN: Test 1 set wrong text: Result: %s\n",buffer);

  result = SendMessage(hwndRichEdit, EM_CANUNDO, 0, 0);
  ok (result == FALSE,
      "EM_STREAMIN without SFF_SELECTION wrongly allows undo\n");

  /* StreamIn, SFF_SELECTION, but nothing selected */
  es.dwCookie = nCallbackCount;
  SendMessage(hwndRichEdit,EM_EMPTYUNDOBUFFER, 0,0);
  SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) randomtext);
  SendMessage(hwndRichEdit, EM_SETSEL,0,0);
  SendMessage(hwndRichEdit, EM_STREAMIN,
	      (WPARAM)(SF_TEXT|SFF_SELECTION), (LPARAM)&es);
  SendMessage(hwndRichEdit, WM_GETTEXT, 1024, (LPARAM) buffer);
  result = strcmp (buffer,"testSome text");
  ok (result  == 0,
      "EM_STREAMIN: Test 2 set wrong text: Result: %s\n",buffer);

  result = SendMessage(hwndRichEdit, EM_CANUNDO, 0, 0);
  ok (result == TRUE,
     "EM_STREAMIN with SFF_SELECTION but no selection set "
      "should create an undo\n");

  /* StreamIn, SFF_SELECTION, with a selection */
  es.dwCookie = nCallbackCount;
  SendMessage(hwndRichEdit,EM_EMPTYUNDOBUFFER, 0,0);
  SendMessage(hwndRichEdit, WM_SETTEXT, 0, (LPARAM) randomtext);
  SendMessage(hwndRichEdit, EM_SETSEL,4,5);
  SendMessage(hwndRichEdit, EM_STREAMIN,
	      (WPARAM)(SF_TEXT|SFF_SELECTION), (LPARAM)&es);
  SendMessage(hwndRichEdit, WM_GETTEXT, 1024, (LPARAM) buffer);
  result = strcmp (buffer,"Sometesttext");
  ok (result  == 0,
      "EM_STREAMIN: Test 2 set wrong text: Result: %s\n",buffer);

  result = SendMessage(hwndRichEdit, EM_CANUNDO, 0, 0);
  ok (result == TRUE,
      "EM_STREAMIN with SFF_SELECTION and selection set "
      "should create an undo\n");

}

START_TEST( editor )
{
  MSG msg;
  time_t end;

  /* Must explicitly LoadLibrary(). The test has no references to functions in
   * RICHED20.DLL, so the linker doesn't actually link to it. */
  hmoduleRichEdit = LoadLibrary("RICHED20.DLL");
  ok(hmoduleRichEdit != NULL, "error: %d\n", (int) GetLastError());
  test_EM_FINDTEXT();
  test_EM_GETLINE();
  test_EM_SCROLLCARET();
  test_EM_SCROLL();
  test_EM_SETTEXTMODE();
  test_TM_PLAINTEXT();
  test_EM_SETOPTIONS();
  test_WM_GETTEXT();
  test_EM_AUTOURLDETECT();
  test_EM_SETUNDOLIMIT();
  test_ES_PASSWORD();
  test_EM_SETTEXTEX();
  test_EM_LIMITTEXT();
  test_EM_EXLIMITTEXT();
  test_EM_GETLIMITTEXT();
  test_WM_SETFONT();
  test_EM_GETMODIFY();
  test_EM_EXSETSEL();
  test_WM_PASTE();
  test_EM_StreamIn_Undo();

  /* Set the environment variable WINETEST_RICHED20 to keep windows
   * responsive and open for 30 seconds. This is useful for debugging.
   *
   * The message pump uses PeekMessage() to empty the queue and then sleeps for
   * 50ms before retrying the queue. */
  end = time(NULL) + 30;
  if (getenv( "WINETEST_RICHED20" )) {
    while (time(NULL) < end) {
      if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      } else {
        Sleep(50);
      }
    }
  }

  OleFlushClipboard();
  ok(FreeLibrary(hmoduleRichEdit) != 0, "error: %d\n", (int) GetLastError());
}
