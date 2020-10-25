description: build using system zlib
author: Michael Gilbert <mgilbert@debian.org>

--- a/tools/makedep.c
+++ b/tools/makedep.c
@@ -1554,14 +1554,6 @@ static struct file *open_include_file( c
         }
     }
 
-    if (pFile->type == INCL_SYSTEM && pFile->use_msvcrt)
-    {
-        if (!strcmp( pFile->name, "stdarg.h" )) return NULL;
-        fprintf( stderr, "%s:%d: error: system header %s cannot be used with msvcrt\n",
-                 pFile->included_by->file->name, pFile->included_line, pFile->name );
-        exit(1);
-    }
-
     if (pFile->type == INCL_SYSTEM) return NULL;  /* ignore system files we cannot find */
 
     /* try in src file directory */
--- a/dlls/cabinet/Makefile.in
+++ b/dlls/cabinet/Makefile.in
@@ -1,11 +1,11 @@
 MODULE    = cabinet.dll
 IMPORTLIB = cabinet
 
+CROSSLDFLAGS  = $(Z_LIBS)
 EXTRADLLFLAGS = -mno-cygwin
 
 C_SRCS = \
 	cabinet_main.c \
-	deflate.c \
 	fci.c \
 	fdi.c
 
--- a/dlls/cabinet/fci.c
+++ b/dlls/cabinet/fci.c
@@ -30,6 +30,9 @@ There is still some work to be done:
 
 */
 
+#include "zlib.h"
+#undef FAR
+
 #include <assert.h>
 #include <stdarg.h>
 #include <stdio.h>
@@ -42,7 +45,6 @@ There is still some work to be done:
 #include "winerror.h"
 #include "winternl.h"
 #include "fci.h"
-#include "zlib.h"
 #include "cabinet.h"
 #include "wine/list.h"
 #include "wine/debug.h"
--- a/dlls/opcservices/Makefile.in
+++ b/dlls/opcservices/Makefile.in
@@ -1,11 +1,11 @@
 MODULE  = opcservices.dll
 IMPORTS = uuid ole32 advapi32 urlmon xmllite oleaut32
 
+CROSSLDFLAGS  = $(Z_LIBS)
 EXTRADLLFLAGS = -mno-cygwin
 
 C_SRCS = \
 	compress.c \
-	deflate.c \
 	factory.c \
 	package.c \
 	uri.c
--- a/dlls/opcservices/compress.c
+++ b/dlls/opcservices/compress.c
@@ -16,6 +16,9 @@
  * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
  */
 
+#include "zlib.h"
+#undef FAR
+
 #define COBJMACROS
 
 #include <stdarg.h>
@@ -25,7 +28,6 @@
 #include "msopc.h"
 
 #include "opc_private.h"
-#include "zlib.h"
 
 #include "wine/debug.h"
 #include "wine/heap.h"
--- a/dlls/wininet/http.c
+++ b/dlls/wininet/http.c
@@ -27,6 +27,9 @@
  * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
  */
 
+#include "zlib.h"
+#undef FAR
+
 #include <stdlib.h>
 
 #include "winsock2.h"
@@ -53,7 +56,6 @@
 #include "winuser.h"
 
 #include "internet.h"
-#include "zlib.h"
 #include "wine/debug.h"
 #include "wine/exception.h"
 
--- a/dlls/wininet/Makefile.in
+++ b/dlls/wininet/Makefile.in
@@ -4,6 +4,7 @@ IMPORTLIB = wininet
 IMPORTS   = mpr shlwapi shell32 user32 ws2_32 advapi32
 DELAYIMPORTS = secur32 crypt32 cryptui dhcpcsvc iphlpapi
 
+CROSSLDFLAGS  = $(Z_LIBS)
 EXTRADLLFLAGS = -mno-cygwin
 
 C_SRCS = \
@@ -12,7 +13,6 @@ C_SRCS = \
 	ftp.c \
 	gopher.c \
 	http.c \
-	inflate.c \
 	internet.c \
 	netconnection.c \
 	urlcache.c \
