/*
 * Unit test suite for fonts
 *
 * Copyright 2002 Mike McCormack
 * Copyright 2004 Dmitry Timoshkov
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
#include <assert.h>

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"

#include "wine/test.h"

static INT CALLBACK is_font_installed_proc(const LOGFONT *elf, const TEXTMETRIC *ntm, DWORD type, LPARAM lParam)
{
    return 0;
}

static BOOL is_font_installed(const char *name)
{
    HDC hdc = GetDC(0);
    BOOL ret = FALSE;

    if(!EnumFontFamiliesA(hdc, name, is_font_installed_proc, 0))
        ret = TRUE;

    ReleaseDC(0, hdc);
    return ret;
}

static void check_font(const char* test, const LOGFONTA* lf, HFONT hfont)
{
    LOGFONTA getobj_lf;
    int ret, minlen = 0;

    if (!hfont)
        return;

    ret = GetObject(hfont, sizeof(getobj_lf), &getobj_lf);
    /* NT4 tries to be clever and only returns the minimum length */
    while (lf->lfFaceName[minlen] && minlen < LF_FACESIZE-1)
        minlen++;
    minlen += FIELD_OFFSET(LOGFONTA, lfFaceName) + 1;
    ok(ret == sizeof(LOGFONTA) || ret == minlen, "%s: GetObject returned %d\n", test, ret);
    ok(!memcmp(&lf, &lf, FIELD_OFFSET(LOGFONTA, lfFaceName)), "%s: fonts don't match\n", test);
    ok(!lstrcmpA(lf->lfFaceName, getobj_lf.lfFaceName),
       "%s: font names don't match: %s != %s\n", test, lf->lfFaceName, getobj_lf.lfFaceName);
}

static HFONT create_font(const char* test, const LOGFONTA* lf)
{
    HFONT hfont = CreateFontIndirectA(lf);
    ok(hfont != 0, "%s: CreateFontIndirect failed\n", test);
    if (hfont)
        check_font(test, lf, hfont);
    return hfont;
}

static void test_logfont(void)
{
    LOGFONTA lf;
    HFONT hfont;

    memset(&lf, 0, sizeof lf);

    lf.lfCharSet = ANSI_CHARSET;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfWeight = FW_DONTCARE;
    lf.lfHeight = 16;
    lf.lfWidth = 16;
    lf.lfQuality = DEFAULT_QUALITY;

    lstrcpyA(lf.lfFaceName, "Arial");
    hfont = create_font("Arial", &lf);
    DeleteObject(hfont);

    memset(&lf, 'A', sizeof(lf));
    hfont = CreateFontIndirectA(&lf);
    ok(hfont != 0, "CreateFontIndirectA with strange LOGFONT failed\n");
    
    lf.lfFaceName[LF_FACESIZE - 1] = 0;
    check_font("AAA...", &lf, hfont);
    DeleteObject(hfont);
}

static INT CALLBACK font_enum_proc(const LOGFONT *elf, const TEXTMETRIC *ntm, DWORD type, LPARAM lParam)
{
    if (type & RASTER_FONTTYPE)
    {
	LOGFONT *lf = (LOGFONT *)lParam;
	*lf = *elf;
	return 0; /* stop enumeration */
    }

    return 1; /* continue enumeration */
}

static void test_font_metrics(HDC hdc, HFONT hfont, const char *test_str,
			      INT test_str_len, const TEXTMETRICA *tm_orig,
			      const SIZE *size_orig, INT width_orig,
			      INT scale_x, INT scale_y)
{
    HFONT old_hfont;
    TEXTMETRICA tm;
    SIZE size;
    INT width;

    if (!hfont)
        return;

    old_hfont = SelectObject(hdc, hfont);

    GetTextMetricsA(hdc, &tm);

    ok(tm.tmHeight == tm_orig->tmHeight * scale_y, "%d != %d\n", tm.tmHeight, tm_orig->tmHeight * scale_y);
    ok(tm.tmAscent == tm_orig->tmAscent * scale_y, "%d != %d\n", tm.tmAscent, tm_orig->tmAscent * scale_y);
    ok(tm.tmDescent == tm_orig->tmDescent * scale_y, "%d != %d\n", tm.tmDescent, tm_orig->tmDescent * scale_y);
    ok(tm.tmAveCharWidth == tm_orig->tmAveCharWidth * scale_x, "%d != %d\n", tm.tmAveCharWidth, tm_orig->tmAveCharWidth * scale_x);

    GetTextExtentPoint32A(hdc, test_str, test_str_len, &size);

    ok(size.cx == size_orig->cx * scale_x, "%d != %d\n", size.cx, size_orig->cx * scale_x);
    ok(size.cy == size_orig->cy * scale_y, "%d != %d\n", size.cy, size_orig->cy * scale_y);

    GetCharWidthA(hdc, 'A', 'A', &width);

    ok(width == width_orig * scale_x, "%d != %d\n", width, width_orig * scale_x);

    SelectObject(hdc, old_hfont);
}

/* see whether GDI scales bitmap font metrics */
static void test_bitmap_font(void)
{
    static const char test_str[11] = "Test String";
    HDC hdc;
    LOGFONTA bitmap_lf;
    HFONT hfont, old_hfont;
    TEXTMETRICA tm_orig;
    SIZE size_orig;
    INT ret, i, width_orig, height_orig;

    hdc = GetDC(0);

    /* "System" has only 1 pixel size defined, otherwise the test breaks */
    ret = EnumFontFamiliesA(hdc, "System", font_enum_proc, (LPARAM)&bitmap_lf);
    if (ret)
    {
	ReleaseDC(0, hdc);
	trace("no bitmap fonts were found, skipping the test\n");
	return;
    }

    trace("found bitmap font %s, height %d\n", bitmap_lf.lfFaceName, bitmap_lf.lfHeight);

    height_orig = bitmap_lf.lfHeight;
    hfont = create_font("bitmap", &bitmap_lf);

    old_hfont = SelectObject(hdc, hfont);
    ok(GetTextMetricsA(hdc, &tm_orig), "GetTextMetricsA failed\n");
    ok(GetTextExtentPoint32A(hdc, test_str, sizeof(test_str), &size_orig), "GetTextExtentPoint32A failed\n");
    ok(GetCharWidthA(hdc, 'A', 'A', &width_orig), "GetCharWidthA failed\n");
    SelectObject(hdc, old_hfont);
    DeleteObject(hfont);

    /* test fractional scaling */
    for (i = 1; i < height_orig; i++)
    {
	hfont = create_font("fractional", &bitmap_lf);
	test_font_metrics(hdc, hfont, test_str, sizeof(test_str), &tm_orig, &size_orig, width_orig, 1, 1);
	DeleteObject(hfont);
    }

    /* test integer scaling 3x2 */
    bitmap_lf.lfHeight = height_orig * 2;
    bitmap_lf.lfWidth *= 3;
    hfont = create_font("3x2", &bitmap_lf);
todo_wine
{
    test_font_metrics(hdc, hfont, test_str, sizeof(test_str), &tm_orig, &size_orig, width_orig, 3, 2);
}
    DeleteObject(hfont);

    /* test integer scaling 3x3 */
    bitmap_lf.lfHeight = height_orig * 3;
    bitmap_lf.lfWidth = 0;
    hfont = create_font("3x3", &bitmap_lf);

todo_wine
{
    test_font_metrics(hdc, hfont, test_str, sizeof(test_str), &tm_orig, &size_orig, width_orig, 3, 3);
}
    DeleteObject(hfont);

    ReleaseDC(0, hdc);
}

static INT CALLBACK find_font_proc(const LOGFONT *elf, const TEXTMETRIC *ntm, DWORD type, LPARAM lParam)
{
    LOGFONT *lf = (LOGFONT *)lParam;

    if (elf->lfHeight == lf->lfHeight && !strcmp(elf->lfFaceName, lf->lfFaceName))
    {
        *lf = *elf;
        return 0; /* stop enumeration */
    }
    return 1; /* continue enumeration */
}

#define CP1252_BIT    0x00000001
#define CP1250_BIT    0x00000002
#define CP1251_BIT    0x00000004
#define CP1253_BIT    0x00000008
#define CP1254_BIT    0x00000010
#define CP1255_BIT    0x00000020
#define CP1256_BIT    0x00000040
#define CP1257_BIT    0x00000080
#define CP1258_BIT    0x00000100
#define CP874_BIT     0x00010000
#define CP932_BIT     0x00020000
#define CP936_BIT     0x00040000
#define CP949_BIT     0x00080000
#define CP950_BIT     0x00100000

static void test_bitmap_font_metrics(void)
{
    static const struct font_data
    {
        const char face_name[LF_FACESIZE];
        int weight, height, ascent, descent, int_leading, ext_leading;
        int ave_char_width, max_char_width;
        DWORD ansi_bitfield;
    } fd[] =
    {
        { "MS Sans Serif", FW_NORMAL, 13, 11, 2, 2, 0, 5, 11, CP1252_BIT | CP1250_BIT | CP1251_BIT },
        { "MS Sans Serif", FW_NORMAL, 16, 13, 3, 3, 0, 7, 14, CP1252_BIT | CP1250_BIT | CP1251_BIT },
        { "MS Sans Serif", FW_NORMAL, 20, 16, 4, 4, 0, 8, 16, CP1252_BIT | CP1251_BIT },
        { "MS Sans Serif", FW_NORMAL, 20, 16, 4, 4, 0, 8, 18, CP1250_BIT },
        { "MS Sans Serif", FW_NORMAL, 24, 19, 5, 6, 0, 9, 19, CP1252_BIT },
        { "MS Sans Serif", FW_NORMAL, 24, 19, 5, 6, 0, 9, 24, CP1250_BIT },
        { "MS Sans Serif", FW_NORMAL, 24, 19, 5, 6, 0, 9, 20, CP1251_BIT },
        { "MS Sans Serif", FW_NORMAL, 29, 23, 6, 5, 0, 12, 24, CP1252_BIT },
        { "MS Sans Serif", FW_NORMAL, 29, 23, 6, 6, 0, 12, 24, CP1250_BIT },
        { "MS Sans Serif", FW_NORMAL, 29, 23, 6, 5, 0, 12, 25, CP1251_BIT },
        { "MS Sans Serif", FW_NORMAL, 37, 29, 8, 5, 0, 16, 32, CP1252_BIT | CP1250_BIT | CP1251_BIT },
        { "MS Serif", FW_NORMAL, 10, 8, 2, 2, 0, 4, 8, CP1252_BIT | CP1250_BIT },
        { "MS Serif", FW_NORMAL, 10, 8, 2, 2, 0, 5, 8, CP1251_BIT },
        { "MS Serif", FW_NORMAL, 11, 9, 2, 2, 0, 5, 9, CP1252_BIT | CP1250_BIT | CP1251_BIT },
        { "MS Serif", FW_NORMAL, 13, 11, 2, 2, 0, 5, 11, CP1252_BIT },
        { "MS Serif", FW_NORMAL, 13, 11, 2, 2, 0, 5, 12, CP1250_BIT | CP1251_BIT },
        { "MS Serif", FW_NORMAL, 16, 13, 3, 3, 0, 6, 14, CP1252_BIT | CP1250_BIT },
        { "MS Serif", FW_NORMAL, 16, 13, 3, 3, 0, 6, 16, CP1251_BIT },
        { "MS Serif", FW_NORMAL, 19, 15, 4, 3, 0, 8, 18, CP1252_BIT | CP1250_BIT },
        { "MS Serif", FW_NORMAL, 19, 15, 4, 3, 0, 8, 19, CP1251_BIT },
        { "MS Serif", FW_NORMAL, 21, 16, 5, 3, 0, 9, 17, CP1252_BIT },
        { "MS Serif", FW_NORMAL, 21, 16, 5, 3, 0, 9, 22, CP1250_BIT },
        { "MS Serif", FW_NORMAL, 21, 16, 5, 3, 0, 9, 23, CP1251_BIT },
        { "MS Serif", FW_NORMAL, 27, 21, 6, 3, 0, 12, 23, CP1252_BIT },
        { "MS Serif", FW_NORMAL, 27, 21, 6, 3, 0, 12, 26, CP1250_BIT },
        { "MS Serif", FW_NORMAL, 27, 21, 6, 3, 0, 12, 27, CP1251_BIT },
        { "MS Serif", FW_NORMAL, 35, 27, 8, 3, 0, 16, 33, CP1252_BIT | CP1250_BIT },
        { "MS Serif", FW_NORMAL, 35, 27, 8, 3, 0, 16, 34, CP1251_BIT },
        { "Courier", FW_NORMAL, 13, 11, 2, 0, 0, 8, 8, CP1252_BIT | CP1250_BIT | CP1251_BIT },
        { "Courier", FW_NORMAL, 16, 13, 3, 0, 0, 9, 9, CP1252_BIT | CP1250_BIT | CP1251_BIT },
        { "Courier", FW_NORMAL, 20, 16, 4, 0, 0, 12, 12, CP1252_BIT | CP1250_BIT | CP1251_BIT },
        { "System", FW_BOLD, 16, 13, 3, 3, 0, 7, 14, CP1252_BIT },
        { "System", FW_BOLD, 16, 13, 3, 3, 0, 7, 15, CP1250_BIT | CP1251_BIT },
        { "Small Fonts", FW_NORMAL, 3, 2, 1, 0, 0, 1, 2, CP1252_BIT},
        { "Small Fonts", FW_NORMAL, 3, 2, 1, 0, 0, 1, 8, CP1250_BIT | CP1251_BIT },
        { "Small Fonts", FW_NORMAL, 5, 4, 1, 1, 0, 3, 4, CP1252_BIT },
        { "Small Fonts", FW_NORMAL, 5, 4, 1, 1, 0, 2, 8, CP1250_BIT | CP1251_BIT },
        { "Small Fonts", FW_NORMAL, 6, 5, 1, 1, 0, 3, 13, CP1252_BIT },
        { "Small Fonts", FW_NORMAL, 6, 5, 1, 1, 0, 3, 8, CP1250_BIT | CP1251_BIT },
        { "Small Fonts", FW_NORMAL, 8, 7, 1, 1, 0, 4, 7, CP1252_BIT },
        { "Small Fonts", FW_NORMAL, 8, 7, 1, 1, 0, 4, 8, CP1250_BIT | CP1251_BIT },
        { "Small Fonts", FW_NORMAL, 10, 8, 2, 2, 0, 4, 8, CP1252_BIT | CP1250_BIT },
        { "Small Fonts", FW_NORMAL, 10, 8, 2, 2, 0, 5, 8, CP1251_BIT },
        { "Small Fonts", FW_NORMAL, 11, 9, 2, 2, 0, 5, 9, CP1252_BIT | CP1250_BIT | CP1251_BIT },
        { "Fixedsys", FW_NORMAL, 15, 12, 3, 3, 0, 8, 8, CP1252_BIT | CP1250_BIT },
        { "Fixedsys", FW_NORMAL, 16, 12, 4, 3, 0, 8, 8, CP1251_BIT }

        /* FIXME: add "Terminal" */
    };
    HDC hdc;
    LOGFONT lf;
    HFONT hfont, old_hfont;
    TEXTMETRIC tm;
    INT ret, i;

    hdc = CreateCompatibleDC(0);
    assert(hdc);

    for (i = 0; i < sizeof(fd)/sizeof(fd[0]); i++)
    {
        int bit;

        memset(&lf, 0, sizeof(lf));

        lf.lfHeight = fd[i].height;
        strcpy(lf.lfFaceName, fd[i].face_name);

        for(bit = 0; bit < 32; bit++)
        {
            DWORD fs[2];
            CHARSETINFO csi;

            fs[0] = 1L << bit;
            fs[1] = 0;
            if((fd[i].ansi_bitfield & fs[0]) == 0) continue;
            if(!TranslateCharsetInfo( fs, &csi, TCI_SRCFONTSIG )) continue;

            lf.lfCharSet = csi.ciCharset;
            ret = EnumFontFamiliesEx(hdc, &lf, find_font_proc, (LPARAM)&lf, 0);
            if (ret) continue;

            trace("found font %s, height %d charset %x\n", lf.lfFaceName, lf.lfHeight, lf.lfCharSet);

            hfont = create_font(lf.lfFaceName, &lf);
            old_hfont = SelectObject(hdc, hfont);
            ok(GetTextMetrics(hdc, &tm), "GetTextMetrics error %d\n", GetLastError());

            ok(tm.tmWeight == fd[i].weight, "%s(%d): tm.tmWeight %d != %d\n", fd[i].face_name, fd[i].height, tm.tmWeight, fd[i].weight);
            ok(tm.tmHeight == fd[i].height, "%s(%d): tm.tmHeight %d != %d\n", fd[i].face_name, fd[i].height, tm.tmHeight, fd[i].height);
            ok(tm.tmAscent == fd[i].ascent, "%s(%d): tm.tmAscent %d != %d\n", fd[i].face_name, fd[i].height, tm.tmAscent, fd[i].ascent);
            ok(tm.tmDescent == fd[i].descent, "%s(%d): tm.tmDescent %d != %d\n", fd[i].face_name, fd[i].height, tm.tmDescent, fd[i].descent);
            ok(tm.tmInternalLeading == fd[i].int_leading, "%s(%d): tm.tmInternalLeading %d != %d\n", fd[i].face_name, fd[i].height, tm.tmInternalLeading, fd[i].int_leading);
            ok(tm.tmExternalLeading == fd[i].ext_leading, "%s(%d): tm.tmExternalLeading %d != %d\n", fd[i].face_name, fd[i].height, tm.tmExternalLeading, fd[i].ext_leading);
            ok(tm.tmAveCharWidth == fd[i].ave_char_width, "%s(%d): tm.tmAveCharWidth %d != %d\n", fd[i].face_name, fd[i].height, tm.tmAveCharWidth, fd[i].ave_char_width);

            /* Don't run the max char width test on System/ANSI_CHARSET.  We have extra characters in our font
               that make the max width bigger */
            if(strcmp(lf.lfFaceName, "System") || lf.lfCharSet != ANSI_CHARSET)
                ok(tm.tmMaxCharWidth == fd[i].max_char_width, "%s(%d): tm.tmMaxCharWidth %d != %d\n", fd[i].face_name, fd[i].height, tm.tmMaxCharWidth, fd[i].max_char_width);

            SelectObject(hdc, old_hfont);
            DeleteObject(hfont);
        }
    }

    DeleteDC(hdc);
}

static void test_GdiGetCharDimensions(void)
{
    HDC hdc;
    TEXTMETRICW tm;
    LONG ret;
    SIZE size;
    LONG avgwidth, height;
    static const char szAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    typedef LONG (WINAPI *fnGdiGetCharDimensions)(HDC hdc, LPTEXTMETRICW lptm, LONG *height);
    fnGdiGetCharDimensions GdiGetCharDimensions = (fnGdiGetCharDimensions)GetProcAddress(LoadLibrary("gdi32"), "GdiGetCharDimensions");
    if (!GdiGetCharDimensions) return;

    hdc = CreateCompatibleDC(NULL);

    GetTextExtentPoint(hdc, szAlphabet, strlen(szAlphabet), &size);
    avgwidth = ((size.cx / 26) + 1) / 2;

    ret = GdiGetCharDimensions(hdc, &tm, &height);
    ok(ret == avgwidth, "GdiGetCharDimensions should have returned width of %d instead of %d\n", avgwidth, ret);
    ok(height == tm.tmHeight, "GdiGetCharDimensions should have set height to %d instead of %d\n", tm.tmHeight, height);

    ret = GdiGetCharDimensions(hdc, &tm, NULL);
    ok(ret == avgwidth, "GdiGetCharDimensions should have returned width of %d instead of %d\n", avgwidth, ret);

    ret = GdiGetCharDimensions(hdc, NULL, NULL);
    ok(ret == avgwidth, "GdiGetCharDimensions should have returned width of %d instead of %d\n", avgwidth, ret);

    height = 0;
    ret = GdiGetCharDimensions(hdc, NULL, &height);
    ok(ret == avgwidth, "GdiGetCharDimensions should have returned width of %d instead of %d\n", avgwidth, ret);
    ok(height == size.cy, "GdiGetCharDimensions should have set height to %d instead of %d\n", size.cy, height);

    DeleteDC(hdc);
}

static void test_GetCharABCWidthsW(void)
{
    BOOL ret;
    ABC abc[1];
    typedef BOOL (WINAPI *fnGetCharABCWidthsW)(HDC hdc, UINT first, UINT last, LPABC abc);
    fnGetCharABCWidthsW GetCharABCWidthsW = (fnGetCharABCWidthsW)GetProcAddress(LoadLibrary("gdi32"), "GetCharABCWidthsW");
    if (!GetCharABCWidthsW) return;

    ret = GetCharABCWidthsW(NULL, 'a', 'a', abc);
    ok(!ret, "GetCharABCWidthsW should have returned FALSE\n");
}

static void test_text_extents(void)
{
    static const WCHAR wt[] = {'O','n','e','\n','t','w','o',' ','3',0};
    LPINT extents;
    INT i, len, fit1, fit2;
    LOGFONTA lf;
    TEXTMETRICA tm;
    HDC hdc;
    HFONT hfont;
    SIZE sz;
    SIZE sz1, sz2;

    memset(&lf, 0, sizeof(lf));
    strcpy(lf.lfFaceName, "Arial");
    lf.lfHeight = 20;

    hfont = CreateFontIndirectA(&lf);
    hdc = GetDC(0);
    hfont = SelectObject(hdc, hfont);
    GetTextMetricsA(hdc, &tm);
    GetTextExtentPointA(hdc, "o", 1, &sz);
    ok(sz.cy == tm.tmHeight, "cy %d tmHeight %d\n", sz.cy, tm.tmHeight);

    SetLastError(0xdeadbeef);
    GetTextExtentExPointW(hdc, wt, 1, 1, &fit1, &fit2, &sz1);
    if (GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)
    {
        trace("Skipping remainder of text extents test on a Win9x platform\n");
        hfont = SelectObject(hdc, hfont);
        DeleteObject(hfont);
        ReleaseDC(0, hdc);
        return;
    }

    len = lstrlenW(wt);
    extents = HeapAlloc(GetProcessHeap(), 0, len * sizeof extents[0]);
    memset(extents, 0, len * sizeof extents[0]);
    extents[0] = 1;         /* So that the increasing sequence test will fail
                               if the extents array is untouched.  */
    GetTextExtentExPointW(hdc, wt, len, 32767, &fit1, extents, &sz1);
    GetTextExtentPointW(hdc, wt, len, &sz2);
    ok(sz1.cy == sz2.cy,
       "cy from GetTextExtentExPointW (%d) and GetTextExtentPointW (%d) differ\n", sz1.cy, sz2.cy);
    /* Because of the '\n' in the string GetTextExtentExPoint and
       GetTextExtentPoint return different widths under Win2k, but
       under WinXP they return the same width.  So we don't test that
       here. */

    for (i = 1; i < len; ++i)
        ok(extents[i-1] <= extents[i],
           "GetTextExtentExPointW generated a non-increasing sequence of partial extents (at position %d)\n",
           i);
    ok(extents[len-1] == sz1.cx, "GetTextExtentExPointW extents and size don't match\n");
    ok(0 <= fit1 && fit1 <= len, "GetTextExtentExPointW generated illegal value %d for fit\n", fit1);
    ok(0 < fit1, "GetTextExtentExPointW says we can't even fit one letter in 32767 logical units\n");
    GetTextExtentExPointW(hdc, wt, len, extents[2], &fit2, NULL, &sz2);
    ok(sz1.cx == sz2.cx && sz1.cy == sz2.cy, "GetTextExtentExPointW returned different sizes for the same string\n");
    ok(fit2 == 3, "GetTextExtentExPointW extents isn't consistent with fit\n");
    GetTextExtentExPointW(hdc, wt, len, extents[2]-1, &fit2, NULL, &sz2);
    ok(fit2 == 2, "GetTextExtentExPointW extents isn't consistent with fit\n");
    GetTextExtentExPointW(hdc, wt, 2, 0, NULL, extents + 2, &sz2);
    ok(extents[0] == extents[2] && extents[1] == extents[3],
       "GetTextExtentExPointW with lpnFit == NULL returns incorrect results\n");
    GetTextExtentExPointW(hdc, wt, 2, 0, NULL, NULL, &sz1);
    ok(sz1.cx == sz2.cx && sz1.cy == sz2.cy,
       "GetTextExtentExPointW with lpnFit and alpDx both NULL returns incorrect results\n");
    HeapFree(GetProcessHeap(), 0, extents);

    hfont = SelectObject(hdc, hfont);
    DeleteObject(hfont);
    ReleaseDC(NULL, hdc);
}

static void test_GetGlyphIndices()
{
    HDC      hdc;
    HFONT    hfont;
    DWORD    charcount;
    LOGFONTA lf;
    DWORD    flags = 0;
    WCHAR    testtext[] = {'T','e','s','t',0xffff,0};
    WORD     glyphs[(sizeof(testtext)/2)-1];
    TEXTMETRIC textm;

    typedef BOOL (WINAPI *fnGetGlyphIndicesW)(HDC hdc, LPCWSTR lpstr, INT count, LPWORD pgi, DWORD flags);
    fnGetGlyphIndicesW GetGlyphIndicesW = (fnGetGlyphIndicesW)GetProcAddress(LoadLibrary("gdi32"), 
                                           "GetGlyphIndicesW");
    if (!GetGlyphIndicesW) {
        trace("GetGlyphIndices not available on platform\n");
        return;
    }

    if(!is_font_installed("Symbol"))
    {
        trace("Symbol is not installed so skipping this test\n");
        return;
    }

    memset(&lf, 0, sizeof(lf));
    strcpy(lf.lfFaceName, "Symbol");
    lf.lfHeight = 20;

    hfont = CreateFontIndirectA(&lf);
    hdc = GetDC(0);

    ok(GetTextMetrics(hdc, &textm), "GetTextMetric failed\n");
    flags |= GGI_MARK_NONEXISTING_GLYPHS;
    charcount = GetGlyphIndicesW(hdc, testtext, (sizeof(testtext)/2)-1, glyphs, flags);
    ok(charcount == 5, "GetGlyphIndices count of glyphs should = 5 not %d\n", charcount);
    ok(glyphs[4] == 0x001f, "GetGlyphIndices should have returned a nonexistent char not %04x\n", glyphs[4]);
    flags = 0;
    charcount = GetGlyphIndicesW(hdc, testtext, (sizeof(testtext)/2)-1, glyphs, flags);
    ok(charcount == 5, "GetGlyphIndices count of glyphs should = 5 not %d\n", charcount);
    ok(glyphs[4] == textm.tmDefaultChar, "GetGlyphIndices should have returned a %04x not %04x\n", 
                    textm.tmDefaultChar, glyphs[4]);
}

static void test_GetKerningPairs(void)
{
    static const struct kerning_data
    {
        const char face_name[LF_FACESIZE];
        LONG height;
        /* some interesting fields from OUTLINETEXTMETRIC */
        LONG tmHeight, tmAscent, tmDescent;
        UINT otmEMSquare;
        INT  otmAscent;
        INT  otmDescent;
        UINT otmLineGap;
        UINT otmsCapEmHeight;
        UINT otmsXHeight;
        INT  otmMacAscent;
        INT  otmMacDescent;
        UINT otmMacLineGap;
        UINT otmusMinimumPPEM;
        /* small subset of kerning pairs to test */
        DWORD total_kern_pairs;
        const KERNINGPAIR kern_pair[26];
    } kd[] =
    {
        {"Arial", 12, 12, 9, 3,
                  2048, 7, -2, 1, 5, 2, 8, -2, 0, 9,
                  26,
            {
                {' ','A',-1},{' ','T',0},{' ','Y',0},{'1','1',-1},
                {'A',' ',-1},{'A','T',-1},{'A','V',-1},{'A','W',0},
                {'A','Y',-1},{'A','v',0},{'A','w',0},{'A','y',0},
                {'F',',',-1},{'F','.',-1},{'F','A',-1},{'L',' ',0},
                {'L','T',-1},{'L','V',-1},{'L','W',-1},{'L','Y',-1},
                {915,912,+1},{915,913,-1},{910,912,+1},{910,913,-1},
                {933,970,+1},{933,972,-1}
                }
        },
        {"Arial", -34, 39, 32, 7,
                  2048, 25, -7, 5, 17, 9, 31, -7, 1, 9,
                  26,
            {
                {' ','A',-2},{' ','T',-1},{' ','Y',-1},{'1','1',-3},
                {'A',' ',-2},{'A','T',-3},{'A','V',-3},{'A','W',-1},
                {'A','Y',-3},{'A','v',-1},{'A','w',-1},{'A','y',-1},
                {'F',',',-4},{'F','.',-4},{'F','A',-2},{'L',' ',-1},
                {'L','T',-3},{'L','V',-3},{'L','W',-3},{'L','Y',-3},
                {915,912,+3},{915,913,-3},{910,912,+3},{910,913,-3},
                {933,970,+2},{933,972,-3}
            }
        },
        { "Arial", 120, 120, 97, 23,
                   2048, 79, -23, 16, 54, 27, 98, -23, 4, 9,
                   26,
            {
                {' ','A',-6},{' ','T',-2},{' ','Y',-2},{'1','1',-8},
                {'A',' ',-6},{'A','T',-8},{'A','V',-8},{'A','W',-4},
                {'A','Y',-8},{'A','v',-2},{'A','w',-2},{'A','y',-2},
                {'F',',',-12},{'F','.',-12},{'F','A',-6},{'L',' ',-4},
                {'L','T',-8},{'L','V',-8},{'L','W',-8},{'L','Y',-8},
                {915,912,+9},{915,913,-10},{910,912,+9},{910,913,-8},
                {933,970,+6},{933,972,-10}
            }
        },
#if 0 /* this set fails due to +1/-1 errors (rounding bug?), needs investigation. */
        { "Arial", 1024 /* usually 1/2 of EM Square */, 1024, 830, 194,
                   2048, 668, -193, 137, 459, 229, 830, -194, 30, 9,
                   26,
            {
                {' ','A',-51},{' ','T',-17},{' ','Y',-17},{'1','1',-68},
                {'A',' ',-51},{'A','T',-68},{'A','V',-68},{'A','W',-34},
                {'A','Y',-68},{'A','v',-17},{'A','w',-17},{'A','y',-17},
                {'F',',',-102},{'F','.',-102},{'F','A',-51},{'L',' ',-34},
                {'L','T',-68},{'L','V',-68},{'L','W',-68},{'L','Y',-68},
                {915,912,+73},{915,913,-84},{910,912,+76},{910,913,-68},
                {933,970,+54},{933,972,-83}
            }
        }
#endif
    };
    LOGFONT lf;
    HFONT hfont, hfont_old;
    KERNINGPAIR *kern_pair;
    HDC hdc;
    DWORD total_kern_pairs, ret, i, n, matches;

    hdc = GetDC(0);

    /* GetKerningPairsA maps unicode set of kerning pairs to current code page
     * which may render this test unusable, so we're trying to avoid that.
     */
    SetLastError(0xdeadbeef);
    GetKerningPairsW(hdc, 0, NULL);
    if (GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)
    {
        trace("Skipping the GetKerningPairs test on a Win9x platform\n");
        ReleaseDC(0, hdc);
        return;
    }

    for (i = 0; i < sizeof(kd)/sizeof(kd[0]); i++)
    {
        OUTLINETEXTMETRICW otm;

        if (!is_font_installed(kd[i].face_name))
        {
            trace("%s is not installed so skipping this test\n", kd[i].face_name);
            continue;
        }

        trace("testing font %s, height %d\n", kd[i].face_name, kd[i].height);

        memset(&lf, 0, sizeof(lf));
        strcpy(lf.lfFaceName, kd[i].face_name);
        lf.lfHeight = kd[i].height;
        hfont = CreateFontIndirect(&lf);
        assert(hfont != 0);

        hfont_old = SelectObject(hdc, hfont);

        SetLastError(0xdeadbeef);
        otm.otmSize = sizeof(otm); /* just in case for Win9x compatibility */
        ok(GetOutlineTextMetricsW(hdc, sizeof(otm), &otm) == sizeof(otm), "GetOutlineTextMetricsW error %d\n", GetLastError());

        ok(kd[i].tmHeight == otm.otmTextMetrics.tmHeight, "expected %d, got %d\n",
           kd[i].tmHeight, otm.otmTextMetrics.tmHeight);
        ok(kd[i].tmAscent == otm.otmTextMetrics.tmAscent, "expected %d, got %d\n",
           kd[i].tmAscent, otm.otmTextMetrics.tmAscent);
        ok(kd[i].tmDescent == otm.otmTextMetrics.tmDescent, "expected %d, got %d\n",
           kd[i].tmDescent, otm.otmTextMetrics.tmDescent);

        ok(kd[i].otmEMSquare == otm.otmEMSquare, "expected %u, got %u\n",
           kd[i].otmEMSquare, otm.otmEMSquare);
        ok(kd[i].otmAscent == otm.otmAscent, "expected %d, got %d\n",
           kd[i].otmAscent, otm.otmAscent);
        ok(kd[i].otmDescent == otm.otmDescent, "expected %d, got %d\n",
           kd[i].otmDescent, otm.otmDescent);
        ok(kd[i].otmLineGap == otm.otmLineGap, "expected %u, got %u\n",
           kd[i].otmLineGap, otm.otmLineGap);
todo_wine {
        ok(kd[i].otmsCapEmHeight == otm.otmsCapEmHeight, "expected %u, got %u\n",
           kd[i].otmsCapEmHeight, otm.otmsCapEmHeight);
        ok(kd[i].otmsXHeight == otm.otmsXHeight, "expected %u, got %u\n",
           kd[i].otmsXHeight, otm.otmsXHeight);
        ok(kd[i].otmMacAscent == otm.otmMacAscent, "expected %d, got %d\n",
           kd[i].otmMacAscent, otm.otmMacAscent);
        ok(kd[i].otmMacDescent == otm.otmMacDescent, "expected %d, got %d\n",
           kd[i].otmMacDescent, otm.otmMacDescent);
        /* FIXME: this one sometimes succeeds due to expected 0, enable it when removing todo */
        if (0) ok(kd[i].otmMacLineGap == otm.otmMacLineGap, "expected %u, got %u\n",
           kd[i].otmMacLineGap, otm.otmMacLineGap);
        ok(kd[i].otmusMinimumPPEM == otm.otmusMinimumPPEM, "expected %u, got %u\n",
           kd[i].otmusMinimumPPEM, otm.otmusMinimumPPEM);
}

        total_kern_pairs = GetKerningPairsW(hdc, 0, NULL);
        trace("total_kern_pairs %u\n", total_kern_pairs);
        kern_pair = HeapAlloc(GetProcessHeap(), 0, total_kern_pairs * sizeof(*kern_pair));

#if 0 /* Win98 (GetKerningPairsA) and XP behave differently here, the test passes on XP */
        SetLastError(0xdeadbeef);
        ret = GetKerningPairsW(hdc, 0, kern_pair);
        ok(GetLastError() == ERROR_INVALID_PARAMETER,
           "got error %ld, expected ERROR_INVALID_PARAMETER\n", GetLastError());
        ok(ret == 0, "got %lu, expected 0\n", ret);
#endif

        ret = GetKerningPairsW(hdc, 100, NULL);
        ok(ret == total_kern_pairs, "got %u, expected %u\n", ret, total_kern_pairs);

        ret = GetKerningPairsW(hdc, total_kern_pairs/2, kern_pair);
        ok(ret == total_kern_pairs/2, "got %u, expected %u\n", ret, total_kern_pairs/2);

        ret = GetKerningPairsW(hdc, total_kern_pairs, kern_pair);
        ok(ret == total_kern_pairs, "got %u, expected %u\n", ret, total_kern_pairs);

        matches = 0;

        for (n = 0; n < ret; n++)
        {
            DWORD j;
#if 0
            if (kern_pair[n].wFirst < 127 && kern_pair[n].wSecond < 127)
                trace("{'%c','%c',%d},\n",
                      kern_pair[n].wFirst, kern_pair[n].wSecond, kern_pair[n].iKernAmount);
#endif
            for (j = 0; j < kd[i].total_kern_pairs; j++)
            {
                if (kern_pair[n].wFirst == kd[i].kern_pair[j].wFirst &&
                    kern_pair[n].wSecond == kd[i].kern_pair[j].wSecond)
                {
                    ok(kern_pair[n].iKernAmount == kd[i].kern_pair[j].iKernAmount,
                       "pair %d:%d got %d, expected %d\n",
                       kern_pair[n].wFirst, kern_pair[n].wSecond,
                       kern_pair[n].iKernAmount, kd[i].kern_pair[j].iKernAmount);
                    matches++;
                }
            }
        }

        ok(matches == kd[i].total_kern_pairs, "got matches %u, expected %u\n",
           matches, kd[i].total_kern_pairs);

        HeapFree(GetProcessHeap(), 0, kern_pair);

        SelectObject(hdc, hfont_old);
        DeleteObject(hfont);
    }

    ReleaseDC(0, hdc);
}

START_TEST(font)
{
    test_logfont();
    test_bitmap_font();
    test_bitmap_font_metrics();
    test_GdiGetCharDimensions();
    test_GetCharABCWidthsW();
    test_text_extents();
    test_GetGlyphIndices();
    test_GetKerningPairs();
}
