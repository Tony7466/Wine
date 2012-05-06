/*
 * Console private definitions
 *
 * Copyright 2002 Eric Pouech
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

#ifndef __WINE_CONSOLE_PRIVATE_H
#define __WINE_CONSOLE_PRIVATE_H

/* console.c */
extern int      CONSOLE_HandleCtrlC(unsigned);
/* console.c */
extern int      CONSOLE_GetHistory(int idx, WCHAR* buf, int buf_len);
extern BOOL     CONSOLE_AppendHistory(const WCHAR *p);
extern unsigned CONSOLE_GetNumHistoryEntries(void);
extern void     CONSOLE_FillLineUniform(HANDLE hConsoleOutput, int i, int j, int len, LPCHAR_INFO lpFill);
extern BOOL     CONSOLE_GetEditionMode(HANDLE, int*);

/* editline.c */
extern WCHAR*   CONSOLE_Readline(HANDLE, BOOL);

/* term.c */
extern BOOL     TERM_Init(void);
extern BOOL     TERM_Exit(void);
extern unsigned TERM_FillSimpleChar(WCHAR real_inchar, INPUT_RECORD* ir);
extern int      TERM_FillInputRecord(const char* in, size_t len, INPUT_RECORD* ir);

#endif  /* __WINE_CONSOLE_PRIVATE_H */
