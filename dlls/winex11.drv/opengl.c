/*
 * X11DRV OpenGL functions
 *
 * Copyright 2000 Lionel Ulmer
 * Copyright 2005 Alex Woods
 * Copyright 2005 Raphael Junqueira
 * Copyright 2006 Roderick Colenbrander
 * Copyright 2006 Tomas Carnecky
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

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "x11drv.h"
#include "winternl.h"
#include "wine/library.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(wgl);
WINE_DECLARE_DEBUG_CHANNEL(opengl);

#if defined(HAVE_GL_GL_H) && defined(HAVE_GL_GLX_H)

#undef APIENTRY
#undef CALLBACK
#undef WINAPI

#ifdef HAVE_GL_GL_H
# include <GL/gl.h>
#endif
#ifdef HAVE_GL_GLX_H
# include <GL/glx.h>
#endif
#ifdef HAVE_GL_GLEXT_H
# include <GL/glext.h>
#endif

#include "wine/wgl.h"

#undef APIENTRY
#undef CALLBACK
#undef WINAPI

/* Redefines the constants */
#define CALLBACK    __stdcall
#define WINAPI      __stdcall
#define APIENTRY    WINAPI


WINE_DECLARE_DEBUG_CHANNEL(fps);

typedef struct wine_glcontext {
    HDC hdc;
    XVisualInfo *vis;
    GLXFBConfig fb_conf;
    GLXContext ctx;
    BOOL do_escape;
    X11DRV_PDEVICE *physDev;
    RECT viewport;
    RECT scissor;
    BOOL scissor_enabled;
    struct wine_glcontext *next;
    struct wine_glcontext *prev;
} Wine_GLContext;

typedef struct wine_glpbuffer {
    Drawable   drawable;
    Display*   display;
    int        pixelFormat;
    int        width;
    int        height;
    int*       attribList;
    HDC        hdc;

    int        use_render_texture;
    GLuint     texture_target;
    GLuint     texture_bind_target;
    GLuint     texture;
    int        texture_level;
    HDC        prev_hdc;
    HGLRC      prev_ctx;
    HDC        render_hdc;
    HGLRC      render_ctx;
} Wine_GLPBuffer;

typedef struct wine_glextension {
    const char *extName;
    struct {
        const char *funcName;
        void *funcAddress;
    } extEntryPoints[8];
} WineGLExtension;

struct WineGLInfo {
    const char *glVersion;
    const char *glExtensions;

    int glxVersion[2];

    const char *glxServerVersion;
    const char *glxServerExtensions;

    const char *glxClientVersion;
    const char *glxClientExtensions;

    const char *glxExtensions;

    BOOL glxDirect;
    char wglExtensions[4096];
};

typedef struct wine_glpixelformat {
    int iPixelFormat;
    int fbconfig;
    int fmt_index;
} WineGLPixelFormat;

static Wine_GLContext *context_list;
static struct WineGLInfo WineGLInfo = { 0 };
static int use_render_texture_emulation = 0;
static int use_render_texture_ati = 0;
static int swap_interval = 1;

#define MAX_EXTENSIONS 16
static const WineGLExtension *WineGLExtensionList[MAX_EXTENSIONS];
static int WineGLExtensionListSize;

#define MAX_GLPIXELFORMATS 32
static WineGLPixelFormat WineGLPixelFormatList[MAX_GLPIXELFORMATS];
static int WineGLPixelFormatListSize = 0;

static void X11DRV_WineGL_LoadExtensions(void);

static void dump_PIXELFORMATDESCRIPTOR(const PIXELFORMATDESCRIPTOR *ppfd) {
  TRACE("  - size / version : %d / %d\n", ppfd->nSize, ppfd->nVersion);
  TRACE("  - dwFlags : ");
#define TEST_AND_DUMP(t,tv) if ((t) & (tv)) TRACE(#tv " ")
  TEST_AND_DUMP(ppfd->dwFlags, PFD_DEPTH_DONTCARE);
  TEST_AND_DUMP(ppfd->dwFlags, PFD_DOUBLEBUFFER);
  TEST_AND_DUMP(ppfd->dwFlags, PFD_DOUBLEBUFFER_DONTCARE);
  TEST_AND_DUMP(ppfd->dwFlags, PFD_DRAW_TO_WINDOW);
  TEST_AND_DUMP(ppfd->dwFlags, PFD_DRAW_TO_BITMAP);
  TEST_AND_DUMP(ppfd->dwFlags, PFD_GENERIC_ACCELERATED);
  TEST_AND_DUMP(ppfd->dwFlags, PFD_GENERIC_FORMAT);
  TEST_AND_DUMP(ppfd->dwFlags, PFD_NEED_PALETTE);
  TEST_AND_DUMP(ppfd->dwFlags, PFD_NEED_SYSTEM_PALETTE);
  TEST_AND_DUMP(ppfd->dwFlags, PFD_STEREO);
  TEST_AND_DUMP(ppfd->dwFlags, PFD_STEREO_DONTCARE);
  TEST_AND_DUMP(ppfd->dwFlags, PFD_SUPPORT_GDI);
  TEST_AND_DUMP(ppfd->dwFlags, PFD_SUPPORT_OPENGL);
  TEST_AND_DUMP(ppfd->dwFlags, PFD_SWAP_COPY);
  TEST_AND_DUMP(ppfd->dwFlags, PFD_SWAP_EXCHANGE);
  TEST_AND_DUMP(ppfd->dwFlags, PFD_SWAP_LAYER_BUFFERS);
#undef TEST_AND_DUMP
  TRACE("\n");

  TRACE("  - iPixelType : ");
  switch (ppfd->iPixelType) {
  case PFD_TYPE_RGBA: TRACE("PFD_TYPE_RGBA"); break;
  case PFD_TYPE_COLORINDEX: TRACE("PFD_TYPE_COLORINDEX"); break;
  }
  TRACE("\n");

  TRACE("  - Color   : %d\n", ppfd->cColorBits);
  TRACE("  - Red     : %d\n", ppfd->cRedBits);
  TRACE("  - Green   : %d\n", ppfd->cGreenBits);
  TRACE("  - Blue    : %d\n", ppfd->cBlueBits);
  TRACE("  - Alpha   : %d\n", ppfd->cAlphaBits);
  TRACE("  - Accum   : %d\n", ppfd->cAccumBits);
  TRACE("  - Depth   : %d\n", ppfd->cDepthBits);
  TRACE("  - Stencil : %d\n", ppfd->cStencilBits);
  TRACE("  - Aux     : %d\n", ppfd->cAuxBuffers);

  TRACE("  - iLayerType : ");
  switch (ppfd->iLayerType) {
  case PFD_MAIN_PLANE: TRACE("PFD_MAIN_PLANE"); break;
  case PFD_OVERLAY_PLANE: TRACE("PFD_OVERLAY_PLANE"); break;
  case (BYTE)PFD_UNDERLAY_PLANE: TRACE("PFD_UNDERLAY_PLANE"); break;
  }
  TRACE("\n");
}

/* No need to load any other libraries as according to the ABI, libGL should be self-sufficient and
   include all dependencies
*/
#ifndef SONAME_LIBGL
#define SONAME_LIBGL "libGL.so"
#endif

#define PUSH1(attribs,att)        do { attribs[nAttribs++] = (att); } while (0)
#define PUSH2(attribs,att,value)  do { attribs[nAttribs++] = (att); attribs[nAttribs++] = (value); } while(0)

#define MAKE_FUNCPTR(f) static typeof(f) * p##f;
/* GLX 1.0 */
MAKE_FUNCPTR(glXChooseVisual)
MAKE_FUNCPTR(glXCreateContext)
MAKE_FUNCPTR(glXCreateGLXPixmap)
MAKE_FUNCPTR(glXGetCurrentContext)
MAKE_FUNCPTR(glXDestroyContext)
MAKE_FUNCPTR(glXDestroyGLXPixmap)
MAKE_FUNCPTR(glXGetConfig)
MAKE_FUNCPTR(glXIsDirect)
MAKE_FUNCPTR(glXMakeCurrent)
MAKE_FUNCPTR(glXSwapBuffers)
MAKE_FUNCPTR(glXQueryExtension)
MAKE_FUNCPTR(glXQueryVersion)
MAKE_FUNCPTR(glXUseXFont)

/* GLX 1.1 */
MAKE_FUNCPTR(glXGetClientString)
MAKE_FUNCPTR(glXQueryExtensionsString)
MAKE_FUNCPTR(glXQueryServerString)

/* GLX 1.3 */
MAKE_FUNCPTR(glXGetFBConfigs)
MAKE_FUNCPTR(glXChooseFBConfig)
MAKE_FUNCPTR(glXCreatePbuffer)
MAKE_FUNCPTR(glXDestroyPbuffer)
MAKE_FUNCPTR(glXGetFBConfigAttrib)
MAKE_FUNCPTR(glXGetVisualFromFBConfig)
MAKE_FUNCPTR(glXMakeContextCurrent)
MAKE_FUNCPTR(glXQueryDrawable)
MAKE_FUNCPTR(glXGetCurrentReadDrawable)

/* GLX Extensions */
static void* (*pglXGetProcAddressARB)(const GLubyte *);
static BOOL  (*pglXBindTexImageARB)(Display *dpy, GLXPbuffer pbuffer, int buffer);
static BOOL  (*pglXReleaseTexImageARB)(Display *dpy, GLXPbuffer pbuffer, int buffer);
static BOOL  (*pglXDrawableAttribARB)(Display *dpy, GLXDrawable draw, const int *attribList);
static int   (*pglXSwapIntervalSGI)(int);

/* NV GLX Extension */
static void* (*pglXAllocateMemoryNV)(GLsizei size, GLfloat readfreq, GLfloat writefreq, GLfloat priority);
static void  (*pglXFreeMemoryNV)(GLvoid *pointer);

/* Standard OpenGL */
MAKE_FUNCPTR(glBindTexture)
MAKE_FUNCPTR(glBitmap)
MAKE_FUNCPTR(glCopyTexSubImage1D)
MAKE_FUNCPTR(glCopyTexSubImage2D)
MAKE_FUNCPTR(glDisable)
MAKE_FUNCPTR(glDrawBuffer)
MAKE_FUNCPTR(glEnable)
MAKE_FUNCPTR(glEndList)
MAKE_FUNCPTR(glGetError)
MAKE_FUNCPTR(glGetIntegerv)
MAKE_FUNCPTR(glGetString)
MAKE_FUNCPTR(glIsEnabled)
MAKE_FUNCPTR(glNewList)
MAKE_FUNCPTR(glPixelStorei)
MAKE_FUNCPTR(glScissor)
MAKE_FUNCPTR(glViewport)
#undef MAKE_FUNCPTR

static BOOL X11DRV_WineGL_InitOpenglInfo(void)
{
    static BOOL infoInitialized = FALSE;

    int screen = DefaultScreen(gdi_display);
    Window win = RootWindow(gdi_display, screen);
    Visual *visual;
    XVisualInfo template;
    XVisualInfo *vis;
    int num;
    GLXContext ctx = NULL;

    if (infoInitialized)
        return TRUE;
    infoInitialized = TRUE;

    wine_tsx11_lock();

    visual = DefaultVisual(gdi_display, screen);
    template.visualid = XVisualIDFromVisual(visual);
    vis = XGetVisualInfo(gdi_display, VisualIDMask, &template, &num);
    if (vis) {
        WORD old_fs = wine_get_fs();
        /* Create a GLX Context. Without one we can't query GL information */
        ctx = pglXCreateContext(gdi_display, vis, None, GL_TRUE);
        if (wine_get_fs() != old_fs)
        {
            wine_set_fs( old_fs );
            wine_tsx11_unlock();
            ERR( "%%fs register corrupted, probably broken ATI driver, disabling OpenGL.\n" );
            ERR( "You need to set the \"UseFastTls\" option to \"2\" in your X config file.\n" );
            return FALSE;
        }
    }

    if (ctx) {
        pglXMakeCurrent(gdi_display, win, ctx);
    } else {
        ERR(" couldn't initialize OpenGL, expect problems\n");
        wine_tsx11_unlock();
        return FALSE;
    }

    WineGLInfo.glVersion = (const char *) pglGetString(GL_VERSION);
    WineGLInfo.glExtensions = strdup((const char *) pglGetString(GL_EXTENSIONS));

    /* Get the common GLX version supported by GLX client and server ( major/minor) */
    pglXQueryVersion(gdi_display, &WineGLInfo.glxVersion[0], &WineGLInfo.glxVersion[1]);

    WineGLInfo.glxServerVersion = pglXQueryServerString(gdi_display, screen, GLX_VERSION);
    WineGLInfo.glxServerExtensions = pglXQueryServerString(gdi_display, screen, GLX_EXTENSIONS);

    WineGLInfo.glxClientVersion = pglXGetClientString(gdi_display, GLX_VERSION);
    WineGLInfo.glxClientExtensions = pglXGetClientString(gdi_display, GLX_EXTENSIONS);

    WineGLInfo.glxExtensions = pglXQueryExtensionsString(gdi_display, screen);
    WineGLInfo.glxDirect = pglXIsDirect(gdi_display, ctx);

    TRACE("GL version             : %s.\n", WineGLInfo.glVersion);
    TRACE("GLX version            : %d.%d.\n", WineGLInfo.glxVersion[0], WineGLInfo.glxVersion[1]);
    TRACE("Server GLX version     : %s.\n", WineGLInfo.glxServerVersion);
    TRACE("Client GLX version     : %s.\n", WineGLInfo.glxClientVersion);
    TRACE("Direct rendering enabled: %s\n", WineGLInfo.glxDirect ? "True" : "False");

    if(vis) XFree(vis);
    if(ctx) {
        pglXMakeCurrent(gdi_display, None, NULL);    
        pglXDestroyContext(gdi_display, ctx);
    }
    wine_tsx11_unlock();
    return TRUE;
}

static BOOL has_opengl(void)
{
    static int init_done;
    static void *opengl_handle;
    const char *glx_extensions;

    int error_base, event_base;

    if (init_done) return (opengl_handle != NULL);
    init_done = 1;

    opengl_handle = wine_dlopen(SONAME_LIBGL, RTLD_NOW|RTLD_GLOBAL, NULL, 0);
    if (opengl_handle == NULL) return FALSE;

    pglXGetProcAddressARB = wine_dlsym(opengl_handle, "glXGetProcAddressARB", NULL, 0);
    if (pglXGetProcAddressARB == NULL) {
        ERR("could not find glXGetProcAddressARB in libGL.\n");
        return FALSE;
    }

#define LOAD_FUNCPTR(f) if((p##f = (void*)pglXGetProcAddressARB((const unsigned char*)#f)) == NULL) goto sym_not_found;
/* GLX 1.0 */
LOAD_FUNCPTR(glXChooseVisual)
LOAD_FUNCPTR(glXCreateContext)
LOAD_FUNCPTR(glXCreateGLXPixmap)
LOAD_FUNCPTR(glXGetCurrentContext)
LOAD_FUNCPTR(glXDestroyContext)
LOAD_FUNCPTR(glXDestroyGLXPixmap)
LOAD_FUNCPTR(glXGetConfig)
LOAD_FUNCPTR(glXIsDirect)
LOAD_FUNCPTR(glXMakeCurrent)
LOAD_FUNCPTR(glXSwapBuffers)
LOAD_FUNCPTR(glXQueryExtension)
LOAD_FUNCPTR(glXQueryVersion)
LOAD_FUNCPTR(glXUseXFont)

/* GLX 1.1 */
LOAD_FUNCPTR(glXGetClientString)
LOAD_FUNCPTR(glXQueryExtensionsString)
LOAD_FUNCPTR(glXQueryServerString)

/* GLX 1.3 */
LOAD_FUNCPTR(glXCreatePbuffer)
LOAD_FUNCPTR(glXDestroyPbuffer)
LOAD_FUNCPTR(glXMakeContextCurrent)
LOAD_FUNCPTR(glXGetCurrentReadDrawable)
LOAD_FUNCPTR(glXGetFBConfigs)

/* Standard OpenGL calls */
LOAD_FUNCPTR(glBindTexture)
LOAD_FUNCPTR(glBitmap)
LOAD_FUNCPTR(glCopyTexSubImage1D)
LOAD_FUNCPTR(glCopyTexSubImage2D)
LOAD_FUNCPTR(glDisable)
LOAD_FUNCPTR(glDrawBuffer)
LOAD_FUNCPTR(glEnable)
LOAD_FUNCPTR(glEndList)
LOAD_FUNCPTR(glGetError)
LOAD_FUNCPTR(glGetIntegerv)
LOAD_FUNCPTR(glGetString)
LOAD_FUNCPTR(glIsEnabled)
LOAD_FUNCPTR(glNewList)
LOAD_FUNCPTR(glPixelStorei)
LOAD_FUNCPTR(glScissor)
LOAD_FUNCPTR(glViewport)
#undef LOAD_FUNCPTR

/* It doesn't matter if these fail. They'll only be used if the driver reports
   the associated extension is available (and if a driver reports the extension
   is available but fails to provide the functions, it's quite broken) */
#define LOAD_FUNCPTR(f) p##f = (void*)pglXGetProcAddressARB((const unsigned char*)#f);
/* NV GLX Extension */
LOAD_FUNCPTR(glXAllocateMemoryNV)
LOAD_FUNCPTR(glXFreeMemoryNV)
#undef LOAD_FUNCPTR

    if(!X11DRV_WineGL_InitOpenglInfo()) {
        wine_dlclose(opengl_handle, NULL, 0);
        opengl_handle = NULL;
        return FALSE;
    }

    wine_tsx11_lock();
    if (pglXQueryExtension(gdi_display, &error_base, &event_base) == True) {
        TRACE("GLX is up and running error_base = %d\n", error_base);
    } else {
        wine_dlclose(opengl_handle, NULL, 0);
        opengl_handle = NULL;
    }

    /* In case of GLX you have direct and indirect rendering. Most of the time direct rendering is used
     * as in general only that is hardware accelerated. In some cases like in case of remote X indirect
     * rendering is used.
     *
     * The main problem for our OpenGL code is that we need certain GLX calls but their presence
     * depends on the reported GLX client / server version and on the client / server extension list.
     * Those don't have to be the same.
     *
     * In general the server GLX information should be used in case of indirect rendering. When direct
     * rendering is used, the OpenGL client library is responsible for which GLX calls are available.
     * Nvidia's OpenGL drivers are the best in terms of GLX features. At the moment of writing their
     * 8762 drivers support 1.3 for the server and 1.4 for the client and they support lots of extensions.
     * Unfortunately it is much more complicated for Mesa/DRI-based drivers and ATI's drivers.
     * Both sets of drivers report a server version of 1.2 and the client version can be 1.3 or 1.4.
     * Further, in case of at least ATI's drivers, one crucial extension needed for our pixel format code
     * is only available in the list of server extensions and not in the client list.
     *
     * The versioning checks below try to take into account the comments from above.
     */

    /* Depending on the use of direct or indirect rendering we need either the list of extensions
     * exported by the client or by the server.
     */
    if(WineGLInfo.glxDirect)
        glx_extensions = WineGLInfo.glxClientExtensions;
    else
        glx_extensions = WineGLInfo.glxServerExtensions;

    /* Based on the default opengl context we decide whether direct or indirect rendering is used.
     * In case of indirect rendering we check if the GLX version of the server is 1.2 and else
     * the client version is checked.
     */
    if ((!WineGLInfo.glxDirect && !strcmp("1.2", WineGLInfo.glxServerVersion)) ||
        (WineGLInfo.glxDirect && !strcmp("1.2", WineGLInfo.glxClientVersion)))
    {
        if (NULL != strstr(glx_extensions, "GLX_SGIX_fbconfig")) {
            pglXChooseFBConfig = (void*)pglXGetProcAddressARB((const GLubyte *) "glXChooseFBConfigSGIX");
            pglXGetFBConfigAttrib = (void*)pglXGetProcAddressARB((const GLubyte *) "glXGetFBConfigAttribSGIX");
            pglXGetVisualFromFBConfig = (void*)pglXGetProcAddressARB((const GLubyte *) "glXGetVisualFromFBConfigSGIX");
        } else {
            ERR(" glx_version is %s and GLX_SGIX_fbconfig extension is unsupported. Expect problems.\n", WineGLInfo.glxClientVersion);
        }
    } else {
        pglXChooseFBConfig = (void*)pglXGetProcAddressARB((const GLubyte *) "glXChooseFBConfig");
        pglXGetFBConfigAttrib = (void*)pglXGetProcAddressARB((const GLubyte *) "glXGetFBConfigAttrib");
        pglXGetVisualFromFBConfig = (void*)pglXGetProcAddressARB((const GLubyte *) "glXGetVisualFromFBConfig");
    }

    /* The mesa libGL client library seems to forward glXQueryDrawable to the Xserver, so only
     * enable this function when the Xserver understand GLX 1.3 or newer
     */
    if (!strcmp("1.2", WineGLInfo.glxServerVersion))
        pglXQueryDrawable = NULL;
    else
        pglXQueryDrawable = wine_dlsym(RTLD_DEFAULT, "glXQueryDrawable", NULL, 0);

    if (NULL != strstr(glx_extensions, "GLX_ATI_render_texture")) {
        pglXBindTexImageARB = (void*)pglXGetProcAddressARB((const GLubyte *) "glXBindTexImageARB");
        pglXReleaseTexImageARB = (void*)pglXGetProcAddressARB((const GLubyte *) "glXReleaseTexImageARB");
        pglXDrawableAttribARB = (void*)pglXGetProcAddressARB((const GLubyte *) "glXDrawableAttribARB");
    }

    X11DRV_WineGL_LoadExtensions();

    wine_tsx11_unlock();
    return (opengl_handle != NULL);

sym_not_found:
    wine_dlclose(opengl_handle, NULL, 0);
    opengl_handle = NULL;
    return FALSE;
}

static inline Wine_GLContext *alloc_context(void)
{
    Wine_GLContext *ret;

    if ((ret = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Wine_GLContext))))
    {
        ret->next = context_list;
        if (context_list) context_list->prev = ret;
        context_list = ret;
    }
    return ret;
}

static inline void free_context(Wine_GLContext *context)
{
    if (context->next != NULL) context->next->prev = context->prev;
    if (context->prev != NULL) context->prev->next = context->next;
    else context_list = context->next;

    HeapFree(GetProcessHeap(), 0, context);
}

static inline Wine_GLContext *get_context_from_GLXContext(GLXContext ctx)
{
    Wine_GLContext *ret;
    if (!ctx) return NULL;
    for (ret = context_list; ret; ret = ret->next) if (ctx == ret->ctx) break;
    return ret;
}

/**
 * get_hdc_from_Drawable (internal)
 *
 * For use by wglGetCurrentReadDCARB.
 */
static inline HDC get_hdc_from_Drawable(GLXDrawable d)
{
    Wine_GLContext *ret;
    for (ret = context_list; ret; ret = ret->next) {
        if (d == ret->physDev->drawable) {
            return ret->hdc;
        }
    }
    return NULL;
}

static inline BOOL is_valid_context( Wine_GLContext *ctx )
{
    Wine_GLContext *ptr;
    for (ptr = context_list; ptr; ptr = ptr->next) if (ptr == ctx) break;
    return (ptr != NULL);
}

static int describeContext(Wine_GLContext* ctx) {
    int tmp;
    int ctx_vis_id;
    TRACE(" Context %p have (vis:%p):\n", ctx, ctx->vis);
    pglXGetFBConfigAttrib(gdi_display, ctx->fb_conf, GLX_FBCONFIG_ID, &tmp);
    TRACE(" - FBCONFIG_ID 0x%x\n", tmp);
    pglXGetFBConfigAttrib(gdi_display, ctx->fb_conf, GLX_VISUAL_ID, &tmp);
    TRACE(" - VISUAL_ID 0x%x\n", tmp);
    ctx_vis_id = tmp;
    return ctx_vis_id;
}

static int describeDrawable(Wine_GLContext* ctx, Drawable drawable) {
    int tmp;
    int nElements;
    int attribList[3] = { GLX_FBCONFIG_ID, 0, None };
    GLXFBConfig *fbCfgs;

    if (pglXQueryDrawable == NULL)  {
        /** glXQueryDrawable not available so returns not supported */
        return -1;
    }

    TRACE(" Drawable %p have :\n", (void*) drawable);
    pglXQueryDrawable(gdi_display, drawable, GLX_WIDTH, (unsigned int*) &tmp);
    TRACE(" - WIDTH as %d\n", tmp);
    pglXQueryDrawable(gdi_display, drawable, GLX_HEIGHT, (unsigned int*) &tmp);
    TRACE(" - HEIGHT as %d\n", tmp);
    pglXQueryDrawable(gdi_display, drawable, GLX_FBCONFIG_ID, (unsigned int*) &tmp);
    TRACE(" - FBCONFIG_ID as 0x%x\n", tmp);

    attribList[1] = tmp;
    fbCfgs = pglXChooseFBConfig(gdi_display, DefaultScreen(gdi_display), attribList, &nElements);
    if (fbCfgs == NULL) {
        return -1;
    }

    pglXGetFBConfigAttrib(gdi_display, fbCfgs[0], GLX_VISUAL_ID, &tmp);
    TRACE(" - VISUAL_ID as 0x%x\n", tmp);

    XFree(fbCfgs);

    return tmp;
}

static int ConvertAttribWGLtoGLX(const int* iWGLAttr, int* oGLXAttr, Wine_GLPBuffer* pbuf) {
  int nAttribs = 0;
  unsigned cur = 0; 
  int pop;
  int drawattrib = 0;
  int isColor = 0;
  int wantColorBits = 0;
  int sz_alpha = 0;

  /* The list of WGL attributes is allowed to be NULL, so don't return -1 (error) but just 0 */
  if(iWGLAttr == NULL)
    return 0;

  while (0 != iWGLAttr[cur]) {
    TRACE("pAttr[%d] = %x\n", cur, iWGLAttr[cur]);

    switch (iWGLAttr[cur]) {
    case WGL_COLOR_BITS_ARB:
      pop = iWGLAttr[++cur];
      wantColorBits = pop; /** see end */
      break;
    case WGL_BLUE_BITS_ARB:
      pop = iWGLAttr[++cur];
      PUSH2(oGLXAttr, GLX_BLUE_SIZE, pop);
      TRACE("pAttr[%d] = GLX_BLUE_SIZE: %d\n", cur, pop);
      break;
    case WGL_RED_BITS_ARB:
      pop = iWGLAttr[++cur];
      PUSH2(oGLXAttr, GLX_RED_SIZE, pop);
      TRACE("pAttr[%d] = GLX_RED_SIZE: %d\n", cur, pop);
      break;
    case WGL_GREEN_BITS_ARB:
      pop = iWGLAttr[++cur];
      PUSH2(oGLXAttr, GLX_GREEN_SIZE, pop);
      TRACE("pAttr[%d] = GLX_GREEN_SIZE: %d\n", cur, pop);
      break;
    case WGL_ALPHA_BITS_ARB:
      pop = iWGLAttr[++cur];
      sz_alpha = pop;
      PUSH2(oGLXAttr, GLX_ALPHA_SIZE, pop);
      TRACE("pAttr[%d] = GLX_ALPHA_SIZE: %d\n", cur, pop);
      break;
    case WGL_DEPTH_BITS_ARB:
      pop = iWGLAttr[++cur];
      PUSH2(oGLXAttr, GLX_DEPTH_SIZE, pop);
      TRACE("pAttr[%d] = GLX_DEPTH_SIZE: %d\n", cur, pop);
      break;
    case WGL_STENCIL_BITS_ARB:
      pop = iWGLAttr[++cur];
      PUSH2(oGLXAttr, GLX_STENCIL_SIZE, pop);
      TRACE("pAttr[%d] = GLX_STENCIL_SIZE: %d\n", cur, pop);
      break;
    case WGL_DOUBLE_BUFFER_ARB:
      pop = iWGLAttr[++cur];
      PUSH2(oGLXAttr, GLX_DOUBLEBUFFER, pop);
      TRACE("pAttr[%d] = GLX_DOUBLEBUFFER: %d\n", cur, pop);
      break;

    case WGL_PIXEL_TYPE_ARB:
      pop = iWGLAttr[++cur];
      switch (pop) {
      case WGL_TYPE_COLORINDEX_ARB: pop = GLX_COLOR_INDEX_BIT; isColor = 1; break ;
      case WGL_TYPE_RGBA_ARB: pop = GLX_RGBA_BIT; break ;
      case WGL_TYPE_RGBA_FLOAT_ATI: pop = GLX_RGBA_FLOAT_ATI_BIT; break ;
      default:
        ERR("unexpected PixelType(%x)\n", pop);	
        pop = 0;
      }
      PUSH2(oGLXAttr, GLX_RENDER_TYPE, pop);
      TRACE("pAttr[%d] = GLX_RENDER_TYPE: %d\n", cur, pop);
      break;

    case WGL_SUPPORT_GDI_ARB:
      pop = iWGLAttr[++cur];
      /* We only support a limited number of formats which are all renderable by X (similar to GDI).
       * Ignore this attribute to prevent us from not finding a match due to the limited
       * amount of formats supported right now. This option could be matched to GLX_X_RENDERABLE
       * but the issue is that when a program asks for no GDI support, there's no format we can return
       * as all our supported formats are renderable by X.
       */
      TRACE("pAttr[%d] = WGL_SUPPORT_GDI_ARB: %d\n", cur, pop);
      break;

    case WGL_DRAW_TO_BITMAP_ARB:
      pop = iWGLAttr[++cur];
      TRACE("pAttr[%d] = WGL_DRAW_TO_BITMAP_ARB: %d\n", cur, pop);
      /* GLX_DRAWABLE_TYPE flags need to be OR'd together. See below. */
      if (pop) {
        drawattrib |= GLX_PIXMAP_BIT;
      }
      break;

    case WGL_DRAW_TO_WINDOW_ARB:
      pop = iWGLAttr[++cur];
      TRACE("pAttr[%d] = WGL_DRAW_TO_WINDOW_ARB: %d\n", cur, pop);
      /* GLX_DRAWABLE_TYPE flags need to be OR'd together. See below. */
      if (pop) {
        drawattrib |= GLX_WINDOW_BIT;
      }
      break;

    case WGL_DRAW_TO_PBUFFER_ARB:
      pop = iWGLAttr[++cur];
      TRACE("pAttr[%d] = WGL_DRAW_TO_PBUFFER_ARB: %d\n", cur, pop);
      /* GLX_DRAWABLE_TYPE flags need to be OR'd together. See below. */
      if (pop) {
        drawattrib |= GLX_PBUFFER_BIT;
      }
      break;

    case WGL_ACCELERATION_ARB:
    case WGL_SUPPORT_OPENGL_ARB:
      pop = iWGLAttr[++cur];
      /** nothing to do, if we are here, supposing support Accelerated OpenGL */
      TRACE("pAttr[%d] = WGL_SUPPORT_OPENGL_ARB: %d\n", cur, pop);
      break;

    case WGL_PBUFFER_LARGEST_ARB:
      pop = iWGLAttr[++cur];
      PUSH2(oGLXAttr, GLX_LARGEST_PBUFFER, pop);
      TRACE("pAttr[%d] = GLX_LARGEST_PBUFFER: %x\n", cur, pop);
      break;

    case WGL_SAMPLE_BUFFERS_ARB:
      pop = iWGLAttr[++cur];
      PUSH2(oGLXAttr, GLX_SAMPLE_BUFFERS_ARB, pop);
      TRACE("pAttr[%d] = GLX_SAMPLE_BUFFERS_ARB: %x\n", cur, pop);
      break;

    case WGL_SAMPLES_ARB:
      pop = iWGLAttr[++cur];
      PUSH2(oGLXAttr, GLX_SAMPLES_ARB, pop);
      TRACE("pAttr[%d] = GLX_SAMPLES_ARB: %x\n", cur, pop);
      break;

    case WGL_TEXTURE_FORMAT_ARB:
    case WGL_TEXTURE_TARGET_ARB:
    case WGL_MIPMAP_TEXTURE_ARB:
      TRACE("WGL_render_texture Attributes: %x as %x\n", iWGLAttr[cur], iWGLAttr[cur + 1]);
      pop = iWGLAttr[++cur];
      if (NULL == pbuf) {
        ERR("trying to use GLX_Pbuffer Attributes without Pbuffer (was %x)\n", iWGLAttr[cur]);
      }
      if (use_render_texture_ati) {
        /** nothing to do here */
      }
      else if (!use_render_texture_emulation) {
        if (WGL_NO_TEXTURE_ARB != pop) {
          ERR("trying to use WGL_render_texture Attributes without support (was %x)\n", iWGLAttr[cur]);
          return -1; /** error: don't support it */
        } else {
          PUSH2(oGLXAttr, GLX_X_RENDERABLE, pop);
          drawattrib |= GLX_PBUFFER_BIT;
        }
      }
      break ;

    case WGL_BIND_TO_TEXTURE_RGB_ARB:
    case WGL_BIND_TO_TEXTURE_RGBA_ARB:
      pop = iWGLAttr[++cur];
      /** cannot be converted, see direct handling on 
       *   - wglGetPixelFormatAttribivARB
       *  TODO: wglChoosePixelFormat
       */
      break ;

    default:
      FIXME("unsupported %x WGL Attribute\n", iWGLAttr[cur]);
      break;
    }
    ++cur;
  }

  /**
   * Trick as WGL_COLOR_BITS_ARB != GLX_BUFFER_SIZE
   *    WGL_COLOR_BITS_ARB + WGL_ALPHA_BITS_ARB == GLX_BUFFER_SIZE
   *
   *  WGL_COLOR_BITS_ARB
   *     The number of color bitplanes in each color buffer. For RGBA
   *     pixel types, it is the size of the color buffer, excluding the
   *     alpha bitplanes. For color-index pixels, it is the size of the
   *     color index buffer.
   *
   *  GLX_BUFFER_SIZE   
   *     This attribute defines the number of bits per color buffer. 
   *     For GLX FBConfigs that correspond to a PseudoColor or StaticColor visual, 
   *     this is equal to the depth value reported in the X11 visual. 
   *     For GLX FBConfigs that correspond to TrueColor or DirectColor visual, 
   *     this is the sum of GLX_RED_SIZE, GLX_GREEN_SIZE, GLX_BLUE_SIZE, and GLX_ALPHA_SIZE.
   * 
   */
  if (0 < wantColorBits) {
    if (!isColor) { 
      wantColorBits += sz_alpha; 
    }
    if (32 < wantColorBits) {
      ERR("buggy %d GLX_BUFFER_SIZE default to 32\n", wantColorBits);
      wantColorBits = 32;
    }
    PUSH2(oGLXAttr, GLX_BUFFER_SIZE, wantColorBits);
    TRACE("pAttr[%d] = WGL_COLOR_BITS_ARB: %d\n", cur, wantColorBits);
  }

  /* Apply the OR'd drawable type bitmask now. */
  if (drawattrib) {
    PUSH2(oGLXAttr, GLX_DRAWABLE_TYPE, drawattrib);
    TRACE("pAttr[?] = GLX_DRAWABLE_TYPE: %#x\n", drawattrib);
  }

  return nAttribs;
}

static BOOL get_fbconfig_from_visualid(Display *display, Visual *visual, int *fmt_id, int *fmt_index)
{
    GLXFBConfig* cfgs = NULL;
    int i;
    int nCfgs;
    int tmp_fmt_id;
    int tmp_vis_id;
    VisualID visualid;

    if(!display || !visual) {
        ERR("Invalid display or visual\n");
	return FALSE;
    }
    visualid = XVisualIDFromVisual(visual);

    /* Get a list of all available framebuffer configurations */
    cfgs = pglXGetFBConfigs(display, DefaultScreen(display), &nCfgs);
    if (NULL == cfgs || 0 == nCfgs) {
        ERR("glXChooseFBConfig returns NULL\n");
        if(cfgs != NULL) XFree(cfgs);
        return FALSE;
    }

    /* Find the requested offscreen format and count the number of offscreen formats */
    for(i=0; i<nCfgs; i++) {
        pglXGetFBConfigAttrib(display, cfgs[i], GLX_VISUAL_ID, &tmp_vis_id);
        pglXGetFBConfigAttrib(display, cfgs[i], GLX_FBCONFIG_ID, &tmp_fmt_id);

        /* We are looking up the GLX index of our main visual and have found it :) */
        if(visualid == tmp_vis_id) {
            TRACE("Found FBCONFIG_ID 0x%x at index %d for VISUAL_ID 0x%x\n", tmp_fmt_id, i, tmp_vis_id);
            XFree(cfgs);
            *fmt_id = tmp_fmt_id;
            *fmt_index = i;
            return TRUE;
        }
    }

    ERR("No fbconfig found for Wine's main visual (0x%lx), expect problems!\n", visualid);
    XFree(cfgs);
    return FALSE;
}

static BOOL init_formats(Display *display, int screen, Visual *visual)
{
    int fmt_id, fmt_index;

    /* Locate the fbconfig correspondig to our main visual */
    if(!get_fbconfig_from_visualid(display, visual, &fmt_id, &fmt_index)) {
        ERR("Can't get the FBCONFIG_ID for the main visual, expect problems!\n");
        return FALSE;
    }

    /* Put Wine's internal format at the first index */
    WineGLPixelFormatList[0].iPixelFormat = 1;
    WineGLPixelFormatList[0].fbconfig = fmt_id;
    WineGLPixelFormatList[0].fmt_index = fmt_index;
    WineGLPixelFormatListSize = 1;

    /* In the future test for compatible formats here */

    return TRUE;
}

/* GLX can advertise dozens of different pixelformats including offscreen and onscreen ones.
 * In our WGL implementation we only support a subset of these formats namely the format of
 * Wine's main visual and offscreen formats (if they are available).
 * This function converts a WGL format to its corresponding GLX one. It returns the index (zero-based)
 * into the GLX FB config table and it returns the number of supported WGL formats in fmt_count.
 */
static BOOL ConvertPixelFormatWGLtoGLX(Display *display, int iPixelFormat, int *fmt_index, int *fmt_count)
{
    int ret;

    /* Init the list of pixel formats when we need it */
    if(!WineGLPixelFormatListSize)
        init_formats(display, DefaultScreen(display), visual);

    if((iPixelFormat <= 0) || (iPixelFormat > WineGLPixelFormatListSize)) {
        ERR("invalid iPixelFormat %d\n", iPixelFormat);
        ret = FALSE;
        *fmt_index = -1;
    }
    else {
        ret = TRUE;
        *fmt_index = WineGLPixelFormatList[iPixelFormat-1].fmt_index;
    }

    *fmt_count = WineGLPixelFormatListSize;
    TRACE("Returning fmt_index=%d, fmt_count=%d for iPixelFormat=%d\n", *fmt_index, *fmt_count, iPixelFormat);

    return ret;
}

/* Search our internal pixelformat list for the WGL format corresponding to the given fbconfig */
static int ConvertPixelFormatGLXtoWGL(Display *display, int fbconfig)
{
    int i;

    /* Init the list of pixel formats when we need it */
    if(!WineGLPixelFormatListSize)
        init_formats(display, DefaultScreen(display), visual);

    for(i=0; i<WineGLPixelFormatListSize; i++) {
        if(WineGLPixelFormatList[i].fbconfig == fbconfig) {
            TRACE("Returning iPixelFormat %d for fbconfig 0x%x\n", WineGLPixelFormatList[i].iPixelFormat, fbconfig);
            return WineGLPixelFormatList[i].iPixelFormat;
        }
    }
    TRACE("No compatible format found for FBCONFIG_ID=0x%x\n", fbconfig);
    return 0;
}

/**
 * X11DRV_ChoosePixelFormat
 *
 * Equivalent to glXChooseVisual.
 */
int X11DRV_ChoosePixelFormat(X11DRV_PDEVICE *physDev, 
			     const PIXELFORMATDESCRIPTOR *ppfd) {
  GLXFBConfig* cfgs = NULL;
  int ret = 0;
  int nCfgs = 0;
  int value = 0;
  int fmt_index = 0;

  if (!has_opengl()) {
    ERR("No libGL on this box - disabling OpenGL support !\n");
    return 0;
  }

  if (TRACE_ON(opengl)) {
    TRACE("(%p,%p)\n", physDev, ppfd);

    dump_PIXELFORMATDESCRIPTOR((const PIXELFORMATDESCRIPTOR *) ppfd);
  }

  wine_tsx11_lock(); 
  if(!visual) {
    ERR("Can't get an opengl visual!\n");
    goto choose_exit;
  }

  /* Get a list containing all supported FB configurations */
  cfgs = pglXGetFBConfigs(gdi_display, DefaultScreen(gdi_display), &nCfgs);
  if (NULL == cfgs || 0 == nCfgs) {
    ERR("glXGetFBConfigs returns NULL (glError: %d)\n", pglGetError());
    goto choose_exit;
  }

  /* In case an fbconfig was found, check if it matches to the requirements of the ppfd */
  if(!ConvertPixelFormatWGLtoGLX(gdi_display, 1 /* main visual */, &fmt_index, &value)) {
    ERR("Can't find a matching FBCONFIG_ID for VISUAL_ID 0x%lx!\n", visual->visualid);
  } else {
    int dwFlags = 0;
    int iPixelType = 0;
    int value = 0;

    /* Pixel type */
    pglXGetFBConfigAttrib(gdi_display, cfgs[fmt_index], GLX_RENDER_TYPE, &value);
    if (value & GLX_RGBA_BIT)
      iPixelType = PFD_TYPE_RGBA;
    else
      iPixelType = PFD_TYPE_COLORINDEX;

    if (ppfd->iPixelType != iPixelType) {
      TRACE("pixel type mismatch\n");
      goto choose_exit;
    }

    /* Doublebuffer */
    pglXGetFBConfigAttrib(gdi_display, cfgs[fmt_index], GLX_DOUBLEBUFFER, &value); if (value) dwFlags |= PFD_DOUBLEBUFFER;
    if (!(ppfd->dwFlags & PFD_DOUBLEBUFFER_DONTCARE) && (ppfd->dwFlags & PFD_DOUBLEBUFFER)) {
      if (!(dwFlags & PFD_DOUBLEBUFFER)) {
        TRACE("dbl buffer mismatch\n");
        goto choose_exit;
      }
    }

    /* Stereo */
    pglXGetFBConfigAttrib(gdi_display, cfgs[fmt_index], GLX_STEREO, &value); if (value) dwFlags |= PFD_STEREO;
    if (!(ppfd->dwFlags & PFD_STEREO_DONTCARE) && (ppfd->dwFlags & PFD_STEREO)) {
      if (!(dwFlags & PFD_STEREO)) {
        TRACE("stereo mismatch\n");
        goto choose_exit;
      }
    }

    /* Alpha bits */
    pglXGetFBConfigAttrib(gdi_display, cfgs[fmt_index], GLX_ALPHA_SIZE, &value);
    if (ppfd->iPixelType==PFD_TYPE_RGBA && ppfd->cAlphaBits && !value) {
      TRACE("alpha mismatch\n");
      goto choose_exit;
    }

    /* Depth bits */
    pglXGetFBConfigAttrib(gdi_display, cfgs[fmt_index], GLX_DEPTH_SIZE, &value);
    if (ppfd->cDepthBits && !value) {
      TRACE("depth mismatch\n");
      goto choose_exit;
    }

    /* Stencil bits */
    pglXGetFBConfigAttrib(gdi_display, cfgs[fmt_index], GLX_STENCIL_SIZE, &value);
    if (ppfd->cStencilBits && !value) {
      TRACE("stencil mismatch\n");
      goto choose_exit;
    }

    /* Aux buffers */
    pglXGetFBConfigAttrib(gdi_display, cfgs[fmt_index], GLX_AUX_BUFFERS, &value);
    if (ppfd->cAuxBuffers && !value) {
      TRACE("aux mismatch\n");
      goto choose_exit;
    }

    /* When we pass all the checks we have found a matching format :) */
    ret = 1;
    TRACE("Successfully found a matching mode, returning index: %d\n", ret);
  }

choose_exit:
  if(!ret)
    TRACE("No matching mode was found returning 0\n");

  if (NULL != cfgs) XFree(cfgs);
  wine_tsx11_unlock();
  return ret;
}

/**
 * X11DRV_DescribePixelFormat
 *
 * Get the pixel-format descriptor associated to the given id
 */
int X11DRV_DescribePixelFormat(X11DRV_PDEVICE *physDev,
			       int iPixelFormat,
			       UINT nBytes,
			       PIXELFORMATDESCRIPTOR *ppfd) {
  /*XVisualInfo *vis;*/
  int value;
  int rb,gb,bb,ab;

  GLXFBConfig* cfgs = NULL;
  GLXFBConfig cur;
  int nCfgs = 0;
  int ret = 0;
  int fmt_count = 0;
  int fmt_index = 0;
  BOOL res = FALSE;

  if (!has_opengl()) {
    ERR("No libGL on this box - disabling OpenGL support !\n");
    return 0;
  }
  
  TRACE("(%p,%d,%d,%p)\n", physDev, iPixelFormat, nBytes, ppfd);

  wine_tsx11_lock();
  cfgs = pglXGetFBConfigs(gdi_display, DefaultScreen(gdi_display), &nCfgs);
  wine_tsx11_unlock();

  if (NULL == cfgs || 0 == nCfgs) {
    ERR("unexpected iPixelFormat(%d), returns NULL\n", iPixelFormat);
    return 0; /* unespected error */
  }

  /* Look for the iPixelFormat in our list of supported formats. If it is supported we get the index in the FBConfig table and the number of supported formats back */
  res = ConvertPixelFormatWGLtoGLX(gdi_display, iPixelFormat, &fmt_index, &fmt_count);

  if (ppfd == NULL) {
      /* The application is only querying the number of visuals */
      wine_tsx11_lock();
      if (NULL != cfgs) XFree(cfgs);
      wine_tsx11_unlock();
      return fmt_count;
  } else if(res == FALSE) {
      WARN("unexpected iPixelFormat(%d): not >=1 and <=nFormats(%d), returning NULL!\n", iPixelFormat, fmt_count);
      wine_tsx11_lock();
      if (NULL != cfgs) XFree(cfgs);
      wine_tsx11_unlock();
      return 0;
  }

  if (nBytes < sizeof(PIXELFORMATDESCRIPTOR)) {
    ERR("Wrong structure size !\n");
    /* Should set error */
    return 0;
  }

  ret = fmt_count;
  cur = cfgs[fmt_index];

  memset(ppfd, 0, sizeof(PIXELFORMATDESCRIPTOR));
  ppfd->nSize = sizeof(PIXELFORMATDESCRIPTOR);
  ppfd->nVersion = 1;

  /* These flags are always the same... */
  ppfd->dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
  /* Now the flags extracted from the Visual */

  wine_tsx11_lock();

  pglXGetFBConfigAttrib(gdi_display, cur, GLX_CONFIG_CAVEAT, &value);
  if(value == GLX_SLOW_CONFIG)
      ppfd->dwFlags |= PFD_GENERIC_ACCELERATED;

  pglXGetFBConfigAttrib(gdi_display, cur, GLX_DOUBLEBUFFER, &value); if (value) ppfd->dwFlags |= PFD_DOUBLEBUFFER;
  pglXGetFBConfigAttrib(gdi_display, cur, GLX_STEREO, &value); if (value) ppfd->dwFlags |= PFD_STEREO;

  /* Pixel type */
  pglXGetFBConfigAttrib(gdi_display, cur, GLX_RENDER_TYPE, &value);
  if (value & GLX_RGBA_BIT)
    ppfd->iPixelType = PFD_TYPE_RGBA;
  else
    ppfd->iPixelType = PFD_TYPE_COLORINDEX;

  /* Color bits */
  pglXGetFBConfigAttrib(gdi_display, cur, GLX_BUFFER_SIZE, &value);
  ppfd->cColorBits = value;

  /* Red, green, blue and alpha bits / shifts */
  if (ppfd->iPixelType == PFD_TYPE_RGBA) {
    pglXGetFBConfigAttrib(gdi_display, cur, GLX_RED_SIZE, &rb);
    pglXGetFBConfigAttrib(gdi_display, cur, GLX_GREEN_SIZE, &gb);
    pglXGetFBConfigAttrib(gdi_display, cur, GLX_BLUE_SIZE, &bb);
    pglXGetFBConfigAttrib(gdi_display, cur, GLX_ALPHA_SIZE, &ab);

    ppfd->cRedBits = rb;
    ppfd->cRedShift = gb + bb + ab;
    ppfd->cBlueBits = bb;
    ppfd->cBlueShift = ab;
    ppfd->cGreenBits = gb;
    ppfd->cGreenShift = bb + ab;
    ppfd->cAlphaBits = ab;
    ppfd->cAlphaShift = 0;
  } else {
    ppfd->cRedBits = 0;
    ppfd->cRedShift = 0;
    ppfd->cBlueBits = 0;
    ppfd->cBlueShift = 0;
    ppfd->cGreenBits = 0;
    ppfd->cGreenShift = 0;
    ppfd->cAlphaBits = 0;
    ppfd->cAlphaShift = 0;
  }
  /* Accums : to do ... */

  /* Depth bits */
  pglXGetFBConfigAttrib(gdi_display, cur, GLX_DEPTH_SIZE, &value);
  ppfd->cDepthBits = value;

  /* stencil bits */
  pglXGetFBConfigAttrib(gdi_display, cur, GLX_STENCIL_SIZE, &value);
  ppfd->cStencilBits = value;

  wine_tsx11_unlock();

  /* Aux : to do ... */

  ppfd->iLayerType = PFD_MAIN_PLANE;

  if (TRACE_ON(opengl)) {
    dump_PIXELFORMATDESCRIPTOR(ppfd);
  }

  wine_tsx11_lock();
  if (NULL != cfgs) XFree(cfgs);
  wine_tsx11_unlock();

  return ret;
}

/**
 * X11DRV_GetPixelFormat
 *
 * Get the pixel-format id used by this DC
 */
int X11DRV_GetPixelFormat(X11DRV_PDEVICE *physDev) {
  TRACE("(%p): returns %d\n", physDev, physDev->current_pf);

  return physDev->current_pf;
}

/**
 * X11DRV_SetPixelFormat
 *
 * Set the pixel-format id used by this DC
 */
BOOL X11DRV_SetPixelFormat(X11DRV_PDEVICE *physDev,
			   int iPixelFormat,
			   const PIXELFORMATDESCRIPTOR *ppfd) {
  int fmt_index = 0;
  int value;

  TRACE("(%p,%d,%p)\n", physDev, iPixelFormat, ppfd);

  if (!has_opengl()) {
    ERR("No libGL on this box - disabling OpenGL support !\n");
    return 0;
  }

  /* Check if iPixelFormat is in our list of supported formats to see if it is supported. */
  if(!ConvertPixelFormatWGLtoGLX(gdi_display, iPixelFormat, &fmt_index, &value)) {
    ERR("Invalid iPixelFormat: %d\n", iPixelFormat);
    return 0;
  }

  physDev->current_pf = iPixelFormat;

  if (TRACE_ON(opengl)) {
    int nCfgs_fmt = 0;
    GLXFBConfig* cfgs_fmt = NULL;
    GLXFBConfig cur_cfg;
    int gl_test = 0;

    /*
     * How to test if hdc current drawable is compatible (visual/FBConfig) ?
     *
     * in case of root window created HDCs we crash here :(
     *
    Drawable drawable =  get_drawable( physDev->hdc );
    TRACE(" drawable (%p,%p) have :\n", drawable, root_window);
    pglXQueryDrawable(gdi_display, drawable, GLX_FBCONFIG_ID, (unsigned int*) &value);
    TRACE(" - FBCONFIG_ID as 0x%x\n", tmp);
    pglXQueryDrawable(gdi_display, drawable, GLX_VISUAL_ID, (unsigned int*) &value);
    TRACE(" - VISUAL_ID as 0x%x\n", tmp);
    pglXQueryDrawable(gdi_display, drawable, GLX_WIDTH, (unsigned int*) &value);
    TRACE(" - WIDTH as %d\n", tmp);
    pglXQueryDrawable(gdi_display, drawable, GLX_HEIGHT, (unsigned int*) &value);
    TRACE(" - HEIGHT as %d\n", tmp);
    */
    cfgs_fmt = pglXGetFBConfigs(gdi_display, DefaultScreen(gdi_display), &nCfgs_fmt);
    cur_cfg = cfgs_fmt[fmt_index];
    gl_test = pglXGetFBConfigAttrib(gdi_display, cur_cfg, GLX_FBCONFIG_ID, &value);
    if (gl_test) {
      ERR("Failed to retrieve FBCONFIG_ID from GLXFBConfig, expect problems.\n");
    } else {
      TRACE(" FBConfig have :\n");
      TRACE(" - FBCONFIG_ID   0x%x\n", value);
      pglXGetFBConfigAttrib(gdi_display, cur_cfg, GLX_VISUAL_ID, &value);
      TRACE(" - VISUAL_ID     0x%x\n", value);
      pglXGetFBConfigAttrib(gdi_display, cur_cfg, GLX_DRAWABLE_TYPE, &value);
      TRACE(" - DRAWABLE_TYPE 0x%x\n", value);
    }
    XFree(cfgs_fmt);
  }
  return TRUE;
}

/**
 * X11DRV_wglCreateContext
 *
 * For OpenGL32 wglCreateContext.
 */
HGLRC X11DRV_wglCreateContext(X11DRV_PDEVICE *physDev)
{
    Wine_GLContext *ret;
    GLXFBConfig* cfgs_fmt = NULL;
    GLXFBConfig cur_cfg;
    int hdcPF = physDev->current_pf;
    int fmt_count = 0;
    int fmt_index = 0;
    int nCfgs_fmt = 0;
    int value = 0;
    int gl_test = 0;
    HDC hdc = physDev->hdc;

    TRACE("(%p)->(PF:%d)\n", hdc, hdcPF);

    if (!has_opengl()) {
        ERR("No libGL on this box - disabling OpenGL support !\n");
        return 0;
    }

    /* First, get the visual in use by the X11DRV */
    if (!gdi_display) return 0;

    /* We can only render using the iPixelFormat (1) of Wine's Main visual, we need to get the corresponding GLX format.
    * If this fails something is very wrong on the system. */
    if(!ConvertPixelFormatWGLtoGLX(gdi_display, hdcPF, &fmt_index, &fmt_count)) {
        ERR("Cannot get FB Config for main iPixelFormat 1, expect problems!\n");
        SetLastError(ERROR_INVALID_PIXEL_FORMAT);
        return NULL;
    }

    cfgs_fmt = pglXGetFBConfigs(gdi_display, DefaultScreen(gdi_display), &nCfgs_fmt);
    if (NULL == cfgs_fmt || 0 == nCfgs_fmt) {
        ERR("Cannot get FB Configs, expect problems.\n");
        SetLastError(ERROR_INVALID_PIXEL_FORMAT);
        return NULL;
    }

    if (fmt_count < hdcPF) {
        ERR("(%p): unexpected pixelFormat(%d) > nFormats(%d), returns NULL\n", hdc, hdcPF, fmt_count);
        SetLastError(ERROR_INVALID_PIXEL_FORMAT);
        return NULL;
    }

    cur_cfg = cfgs_fmt[fmt_index];
    gl_test = pglXGetFBConfigAttrib(gdi_display, cur_cfg, GLX_FBCONFIG_ID, &value);
    if (gl_test) {
        ERR("Failed to retrieve FBCONFIG_ID from GLXFBConfig, expect problems.\n");
        SetLastError(ERROR_INVALID_PIXEL_FORMAT);
        return NULL;
    }
    XFree(cfgs_fmt);

    /* The context will be allocated in the wglMakeCurrent call */
    wine_tsx11_lock();
    ret = alloc_context();
    wine_tsx11_unlock();
    ret->hdc = hdc;
    ret->physDev = physDev;
    ret->fb_conf = cur_cfg;
    /*ret->vis = vis;*/
    ret->vis = pglXGetVisualFromFBConfig(gdi_display, cur_cfg);

    TRACE(" creating context %p (GL context creation delayed)\n", ret);
    return (HGLRC) ret;
}

/**
 * X11DRV_wglDeleteContext
 *
 * For OpenGL32 wglDeleteContext.
 */
BOOL X11DRV_wglDeleteContext(HGLRC hglrc)
{
    Wine_GLContext *ctx = (Wine_GLContext *) hglrc;
    BOOL ret = TRUE;

    TRACE("(%p)\n", hglrc);

    if (!has_opengl()) {
        ERR("No libGL on this box - disabling OpenGL support !\n");
        return 0;
    }

    wine_tsx11_lock();
    /* A game (Half Life not to name it) deletes twice the same context,
    * so make sure it is valid first */
    if (is_valid_context( ctx ))
    {
        if (ctx->ctx) pglXDestroyContext(gdi_display, ctx->ctx);
        free_context(ctx);
    }
    else
    {
        WARN("Error deleting context !\n");
        SetLastError(ERROR_INVALID_HANDLE);
        ret = FALSE;
    }
    wine_tsx11_unlock();

    return ret;
}

/**
 * X11DRV_wglGetCurrentReadDCARB
 *
 * For OpenGL32 wglGetCurrentReadDCARB.
 */
static HDC WINAPI X11DRV_wglGetCurrentReadDCARB(void) 
{
    GLXDrawable gl_d;
    HDC ret;

    TRACE("()\n");

    wine_tsx11_lock();
    gl_d = pglXGetCurrentReadDrawable();
    ret = get_hdc_from_Drawable(gl_d);
    wine_tsx11_unlock();

    TRACE(" returning %p (GL drawable %lu)\n", ret, gl_d);
    return ret;
}

/**
 * X11DRV_wglGetProcAddress
 *
 * For OpenGL32 wglGetProcAddress.
 */
PROC X11DRV_wglGetProcAddress(LPCSTR lpszProc)
{
    int i, j;
    const WineGLExtension *ext;

    int padding = 32 - strlen(lpszProc);
    if (padding < 0)
        padding = 0;

    if (!has_opengl()) {
        ERR("No libGL on this box - disabling OpenGL support !\n");
        return 0;
    }

    /* Check the table of WGL extensions to see if we need to return a WGL extension
     * or a function pointer to a native OpenGL function. */
    if(strncmp(lpszProc, "wgl", 3) != 0) {
        return pglXGetProcAddressARB((const GLubyte*)lpszProc);
    } else {
        TRACE("('%s'):%*s", lpszProc, padding, " ");
        for (i = 0; i < WineGLExtensionListSize; ++i) {
            ext = WineGLExtensionList[i];
            for (j = 0; ext->extEntryPoints[j].funcName; ++j) {
                if (strcmp(ext->extEntryPoints[j].funcName, lpszProc) == 0) {
                    TRACE("(%p) - WineGL\n", ext->extEntryPoints[j].funcAddress);
                    return ext->extEntryPoints[j].funcAddress;
                }
            }
        }
    }

    ERR("(%s) - not found\n", lpszProc);
    return NULL;
}

/***********************************************************************
 *		sync_current_drawable
 *
 * Adjust the current viewport and scissor in order to position
 * and size the current drawable correctly on the parent window.
 */
static void sync_current_drawable(BOOL updatedc)
{
    int dy;
    int width;
    int height;
    RECT rc;
    Wine_GLContext *ctx = (Wine_GLContext *) NtCurrentTeb()->glContext;

    TRACE("\n");

    if (ctx && ctx->physDev)
    {
        if (updatedc)
            GetClipBox(ctx->physDev->hdc, &rc); /* Make sure physDev is up to date */

        dy = ctx->physDev->drawable_rect.bottom - ctx->physDev->drawable_rect.top -
            ctx->physDev->dc_rect.bottom;
        width = ctx->physDev->dc_rect.right - ctx->physDev->dc_rect.left;
        height = ctx->physDev->dc_rect.bottom - ctx->physDev->dc_rect.top;

        wine_tsx11_lock();

        pglViewport(ctx->physDev->dc_rect.left + ctx->viewport.left,
            dy + ctx->viewport.top,
            ctx->viewport.right ? (ctx->viewport.right - ctx->viewport.left) : width,
            ctx->viewport.bottom ? (ctx->viewport.bottom - ctx->viewport.top) : height);

        pglEnable(GL_SCISSOR_TEST);

        if (ctx->scissor_enabled)
            pglScissor(ctx->physDev->dc_rect.left + min(width, max(0, ctx->scissor.left)),
                dy + min(height, max(0, ctx->scissor.top)),
                min(width, max(0, ctx->scissor.right - ctx->scissor.left)),
                min(height, max(0, ctx->scissor.bottom - ctx->scissor.top)));
        else
            pglScissor(ctx->physDev->dc_rect.left, dy, width, height);

        wine_tsx11_unlock();
    }
}

/**
 * X11DRV_wglMakeCurrent
 *
 * For OpenGL32 wglMakeCurrent.
 */
BOOL X11DRV_wglMakeCurrent(X11DRV_PDEVICE *physDev, HGLRC hglrc) {
    BOOL ret;
    HDC hdc = physDev->hdc;
    DWORD type = GetObjectType(hdc);

    TRACE("(%p,%p)\n", hdc, hglrc);

    if (!has_opengl()) {
        ERR("No libGL on this box - disabling OpenGL support !\n");
        return 0;
    }

    wine_tsx11_lock();
    if (hglrc == NULL) {
        ret = pglXMakeCurrent(gdi_display, None, NULL);
        NtCurrentTeb()->glContext = NULL;
    } else {
        Wine_GLContext *ctx = (Wine_GLContext *) hglrc;
        Drawable drawable = physDev->drawable;
        if (ctx->ctx == NULL) {
            /* The describe lines below are for debugging purposes only */
            if (TRACE_ON(wgl)) {
                describeDrawable(ctx, drawable);
                describeContext(ctx);
            }

            /* Create a GLX context using the same visual as chosen earlier in wglCreateContext.
             * We are certain that the drawable and context are compatible as we only allow compatible formats.
             */
            TRACE(" Creating GLX Context\n");
            ctx->ctx = pglXCreateContext(gdi_display, ctx->vis, NULL, type == OBJ_MEMDC ? False : True);
            TRACE(" created a delayed OpenGL context (%p)\n", ctx->ctx);
        }
        TRACE(" make current for dis %p, drawable %p, ctx %p\n", gdi_display, (void*) drawable, ctx->ctx);
        ret = pglXMakeCurrent(gdi_display, drawable, ctx->ctx);
        NtCurrentTeb()->glContext = ctx;
        if(ret)
        {
            ctx->physDev = physDev;

            if (type == OBJ_MEMDC)
            {
                ctx->do_escape = TRUE;
                pglDrawBuffer(GL_FRONT_LEFT);
            }
            else
            {
                sync_current_drawable(FALSE);
            }
        }
    }
    wine_tsx11_unlock();
    TRACE(" returning %s\n", (ret ? "True" : "False"));
    return ret;
}

/**
 * X11DRV_wglMakeContextCurrentARB
 *
 * For OpenGL32 wglMakeContextCurrentARB
 */
BOOL X11DRV_wglMakeContextCurrentARB(X11DRV_PDEVICE* hDrawDev, X11DRV_PDEVICE* hReadDev, HGLRC hglrc) 
{
    BOOL ret;
    TRACE("(%p,%p,%p)\n", hDrawDev, hReadDev, hglrc);

    if (!has_opengl()) {
        ERR("No libGL on this box - disabling OpenGL support !\n");
        return 0;
    }

    wine_tsx11_lock();
    if (hglrc == NULL) {
        ret = pglXMakeCurrent(gdi_display, None, NULL);
        NtCurrentTeb()->glContext = NULL;
    } else {
        if (NULL == pglXMakeContextCurrent) {
            ret = FALSE;
        } else {
            Wine_GLContext *ctx = (Wine_GLContext *) hglrc;
            Drawable d_draw = get_glxdrawable(hDrawDev);
            Drawable d_read = get_glxdrawable(hReadDev);

            if (ctx->ctx == NULL) {
                ctx->ctx = pglXCreateContext(gdi_display, ctx->vis, NULL, GetObjectType(hDrawDev->hdc) == OBJ_MEMDC ? False : True);
                TRACE(" created a delayed OpenGL context (%p)\n", ctx->ctx);
            }
            ret = pglXMakeContextCurrent(gdi_display, d_draw, d_read, ctx->ctx);
            NtCurrentTeb()->glContext = ctx;
        }
    }
    wine_tsx11_unlock();

    TRACE(" returning %s\n", (ret ? "True" : "False"));
    return ret;
}

/**
 * X11DRV_wglShareLists
 *
 * For OpenGL32 wglShaderLists.
 */
BOOL X11DRV_wglShareLists(HGLRC hglrc1, HGLRC hglrc2) {
    Wine_GLContext *org  = (Wine_GLContext *) hglrc1;
    Wine_GLContext *dest = (Wine_GLContext *) hglrc2;

    TRACE("(%p, %p)\n", org, dest);

    if (!has_opengl()) {
        ERR("No libGL on this box - disabling OpenGL support !\n");
        return 0;
    }

    if (NULL != dest && dest->ctx != NULL) {
        ERR("Could not share display lists, context already created !\n");
        return FALSE;
    } else {
        if (org->ctx == NULL) {
            wine_tsx11_lock();
            describeContext(org);
            org->ctx = pglXCreateContext(gdi_display, org->vis, NULL, GetObjectType(org->physDev->hdc) == OBJ_MEMDC ? False : True);
            wine_tsx11_unlock();
            TRACE(" created a delayed OpenGL context (%p) for Wine context %p\n", org->ctx, org);
        }
        if (NULL != dest) {
            wine_tsx11_lock();
            describeContext(dest);
            /* Create the destination context with display lists shared */
            dest->ctx = pglXCreateContext(gdi_display, dest->vis, org->ctx, GetObjectType(org->physDev->hdc) == OBJ_MEMDC ? False : True);
            wine_tsx11_unlock();
            TRACE(" created a delayed OpenGL context (%p) for Wine context %p sharing lists with OpenGL ctx %p\n", dest->ctx, dest, org->ctx);
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL internal_wglUseFontBitmaps(HDC hdc, DWORD first, DWORD count, DWORD listBase, DWORD (WINAPI *GetGlyphOutline_ptr)(HDC,UINT,UINT,LPGLYPHMETRICS,DWORD,LPVOID,const MAT2*))
{
     /* We are running using client-side rendering fonts... */
     GLYPHMETRICS gm;
     unsigned int glyph;
     int size = 0;
     void *bitmap = NULL, *gl_bitmap = NULL;
     int org_alignment;

     wine_tsx11_lock();
     pglGetIntegerv(GL_UNPACK_ALIGNMENT, &org_alignment);
     pglPixelStorei(GL_UNPACK_ALIGNMENT, 4);
     wine_tsx11_unlock();

     for (glyph = first; glyph < first + count; glyph++) {
         unsigned int needed_size = GetGlyphOutline_ptr(hdc, glyph, GGO_BITMAP, &gm, 0, NULL, NULL);
         int height, width_int;

         TRACE("Glyph : %3d / List : %d\n", glyph, listBase);
         if (needed_size == GDI_ERROR) {
             TRACE("  - needed size : %d (GDI_ERROR)\n", needed_size);
             goto error;
         } else {
             TRACE("  - needed size : %d\n", needed_size);
         }

         if (needed_size > size) {
             size = needed_size;
             HeapFree(GetProcessHeap(), 0, bitmap);
             HeapFree(GetProcessHeap(), 0, gl_bitmap);
             bitmap = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
             gl_bitmap = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
         }
         if (GetGlyphOutline_ptr(hdc, glyph, GGO_BITMAP, &gm, size, bitmap, NULL) == GDI_ERROR) goto error;
         if (TRACE_ON(opengl)) {
             unsigned int height, width, bitmask;
             unsigned char *bitmap_ = (unsigned char *) bitmap;

             TRACE("  - bbox : %d x %d\n", gm.gmBlackBoxX, gm.gmBlackBoxY);
             TRACE("  - origin : (%d , %d)\n", gm.gmptGlyphOrigin.x, gm.gmptGlyphOrigin.y);
             TRACE("  - increment : %d - %d\n", gm.gmCellIncX, gm.gmCellIncY);
             if (needed_size != 0) {
                 TRACE("  - bitmap :\n");
                 for (height = 0; height < gm.gmBlackBoxY; height++) {
                     TRACE("      ");
                     for (width = 0, bitmask = 0x80; width < gm.gmBlackBoxX; width++, bitmask >>= 1) {
                         if (bitmask == 0) {
                             bitmap_ += 1;
                             bitmask = 0x80;
                         }
                         if (*bitmap_ & bitmask)
                             TRACE("*");
                         else
                             TRACE(" ");
                     }
                     bitmap_ += (4 - ((UINT_PTR)bitmap_ & 0x03));
                     TRACE("\n");
                 }
             }
         }

         /* In OpenGL, the bitmap is drawn from the bottom to the top... So we need to invert the
         * glyph for it to be drawn properly.
         */
         if (needed_size != 0) {
             width_int = (gm.gmBlackBoxX + 31) / 32;
             for (height = 0; height < gm.gmBlackBoxY; height++) {
                 int width;
                 for (width = 0; width < width_int; width++) {
                     ((int *) gl_bitmap)[(gm.gmBlackBoxY - height - 1) * width_int + width] =
                     ((int *) bitmap)[height * width_int + width];
                 }
             }
         }

         wine_tsx11_lock();
         pglNewList(listBase++, GL_COMPILE);
         if (needed_size != 0) {
             pglBitmap(gm.gmBlackBoxX, gm.gmBlackBoxY,
                     0 - (int) gm.gmptGlyphOrigin.x, (int) gm.gmBlackBoxY - (int) gm.gmptGlyphOrigin.y,
                     gm.gmCellIncX, gm.gmCellIncY,
                     gl_bitmap);
         } else {
             /* This is the case of 'empty' glyphs like the space character */
             pglBitmap(0, 0, 0, 0, gm.gmCellIncX, gm.gmCellIncY, NULL);
         }
         pglEndList();
         wine_tsx11_unlock();
     }

     wine_tsx11_lock();
     pglPixelStorei(GL_UNPACK_ALIGNMENT, org_alignment);
     wine_tsx11_unlock();

     HeapFree(GetProcessHeap(), 0, bitmap);
     HeapFree(GetProcessHeap(), 0, gl_bitmap);
     return TRUE;

  error:
     wine_tsx11_lock();
     pglPixelStorei(GL_UNPACK_ALIGNMENT, org_alignment);
     wine_tsx11_unlock();

     HeapFree(GetProcessHeap(), 0, bitmap);
     HeapFree(GetProcessHeap(), 0, gl_bitmap);
     return FALSE;
}

/**
 * X11DRV_wglUseFontBitmapsA
 *
 * For OpenGL32 wglUseFontBitmapsA.
 */
BOOL X11DRV_wglUseFontBitmapsA(X11DRV_PDEVICE *physDev, DWORD first, DWORD count, DWORD listBase)
{
     Font fid = physDev->font;

     TRACE("(%p, %d, %d, %d) using font %ld\n", physDev->hdc, first, count, listBase, fid);

     if (!has_opengl()) {
        ERR("No libGL on this box - disabling OpenGL support !\n");
        return 0;
     }

     if (fid == 0) {
         return internal_wglUseFontBitmaps(physDev->hdc, first, count, listBase, GetGlyphOutlineA);
     }

     wine_tsx11_lock();
     /* I assume that the glyphs are at the same position for X and for Windows */
     pglXUseXFont(fid, first, count, listBase);
     wine_tsx11_unlock();
     return TRUE;
}

/**
 * X11DRV_wglUseFontBitmapsW
 *
 * For OpenGL32 wglUseFontBitmapsW.
 */
BOOL X11DRV_wglUseFontBitmapsW(X11DRV_PDEVICE *physDev, DWORD first, DWORD count, DWORD listBase)
{
     Font fid = physDev->font;

     TRACE("(%p, %d, %d, %d) using font %ld\n", physDev->hdc, first, count, listBase, fid);

     if (!has_opengl()) {
        ERR("No libGL on this box - disabling OpenGL support !\n");
        return 0;
     }

     if (fid == 0) {
         return internal_wglUseFontBitmaps(physDev->hdc, first, count, listBase, GetGlyphOutlineW);
     }

     WARN("Using the glX API for the WCHAR variant - some characters may come out incorrectly !\n");

     wine_tsx11_lock();
     /* I assume that the glyphs are at the same position for X and for Windows */
     pglXUseXFont(fid, first, count, listBase);
     wine_tsx11_unlock();
     return TRUE;
}

static void WINAPI X11DRV_wglDisable(GLenum cap)
{
    if (cap == GL_SCISSOR_TEST)
    {
       Wine_GLContext *ctx = (Wine_GLContext *) NtCurrentTeb()->glContext;

       if (ctx)
          ctx->scissor_enabled = FALSE;
    }
    else
    {
        wine_tsx11_lock();
        pglDisable(cap);
        wine_tsx11_unlock();
    }
}

static void WINAPI X11DRV_wglEnable(GLenum cap)
{
    if (cap == GL_SCISSOR_TEST)
    {
       Wine_GLContext *ctx = (Wine_GLContext *) NtCurrentTeb()->glContext;

       if (ctx)
           ctx->scissor_enabled = TRUE;
    }
    else
    {
        wine_tsx11_lock();
        pglEnable(cap);
        wine_tsx11_unlock();
    }
}

/* WGL helper function which handles differences in glGetIntegerv from WGL and GLX */
static void WINAPI X11DRV_wglGetIntegerv(GLenum pname, GLint* params)
{
    wine_tsx11_lock();
    switch(pname)
    {
    case GL_DEPTH_BITS:
        {
            GLXContext gl_ctx = pglXGetCurrentContext();
            Wine_GLContext* ret = get_context_from_GLXContext(gl_ctx);

            pglGetIntegerv(pname, params);
            /**
             * if we cannot find a Wine Context
             * we only have the default wine desktop context,
             * so if we have only a 24 depth say we have 32
             */
            if (NULL == ret && 24 == *params) {
                *params = 32;
            }
            TRACE("returns GL_DEPTH_BITS as '%d'\n", *params);
            break;
        }
    case GL_ALPHA_BITS:
        {
            GLXContext gl_ctx = pglXGetCurrentContext();
            Wine_GLContext* ret = get_context_from_GLXContext(gl_ctx);

            pglXGetFBConfigAttrib(gdi_display, ret->fb_conf, GLX_ALPHA_SIZE, params);
            TRACE("returns GL_ALPHA_BITS as '%d'\n", *params);
            break;
        }
    default:
        pglGetIntegerv(pname, params);
        break;
    }
    wine_tsx11_unlock();
}

static GLboolean WINAPI X11DRV_wglIsEnabled(GLenum cap)
{
    GLboolean enabled = False;

    if (cap == GL_SCISSOR_TEST)
    {
       Wine_GLContext *ctx = (Wine_GLContext *) NtCurrentTeb()->glContext;

       if (ctx)
           enabled = ctx->scissor_enabled;
    }
    else
    {
        wine_tsx11_lock();
        enabled = pglIsEnabled(cap);
        wine_tsx11_unlock();
    }
    return enabled;
}

static void WINAPI X11DRV_wglScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    Wine_GLContext *ctx = (Wine_GLContext *) NtCurrentTeb()->glContext;

    if (ctx)
    {
        ctx->scissor.left = x;
        ctx->scissor.top = y;
        ctx->scissor.right = x + width;
        ctx->scissor.bottom = y + height;

        sync_current_drawable(TRUE);
    }
}

static void WINAPI X11DRV_wglViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    Wine_GLContext *ctx = (Wine_GLContext *) NtCurrentTeb()->glContext;

    if (ctx)
    {
        ctx->viewport.left = x;
        ctx->viewport.top = y;
        ctx->viewport.right = x + width;
        ctx->viewport.bottom = y + height;

        sync_current_drawable(TRUE);
    }
}

/**
 * X11DRV_wglGetExtensionsStringARB
 *
 * WGL_ARB_extensions_string: wglGetExtensionsStringARB
 */
static const char * WINAPI X11DRV_wglGetExtensionsStringARB(HDC hdc) {
    TRACE("() returning \"%s\"\n", WineGLInfo.wglExtensions);
    return WineGLInfo.wglExtensions;
}

/**
 * X11DRV_wglCreatePbufferARB
 *
 * WGL_ARB_pbuffer: wglCreatePbufferARB
 */
static HPBUFFERARB WINAPI X11DRV_wglCreatePbufferARB(HDC hdc, int iPixelFormat, int iWidth, int iHeight, const int *piAttribList)
{
    Wine_GLPBuffer* object = NULL;
    GLXFBConfig* cfgs = NULL;
    int nCfgs = 0;
    int attribs[256];
    int nAttribs = 0;
    int fmt_index = 0;

    TRACE("(%p, %d, %d, %d, %p)\n", hdc, iPixelFormat, iWidth, iHeight, piAttribList);

    if (0 >= iPixelFormat) {
        ERR("(%p): unexpected iPixelFormat(%d) <= 0, returns NULL\n", hdc, iPixelFormat);
        SetLastError(ERROR_INVALID_PIXEL_FORMAT);
        return NULL; /* unexpected error */
    }

    cfgs = pglXGetFBConfigs(gdi_display, DefaultScreen(gdi_display), &nCfgs);

    if (NULL == cfgs || 0 == nCfgs) {
        ERR("(%p): Cannot get FB Configs for iPixelFormat(%d), returns NULL\n", hdc, iPixelFormat);
        SetLastError(ERROR_INVALID_PIXEL_FORMAT);
        return NULL; /* unexpected error */
    }

    /* Convert the WGL pixelformat to a GLX format, if it fails then the format is invalid */
    if(!ConvertPixelFormatWGLtoGLX(gdi_display, iPixelFormat, &fmt_index, &nCfgs)) {
        ERR("(%p): unexpected iPixelFormat(%d) > nFormats(%d), returns NULL\n", hdc, iPixelFormat, nCfgs);
        SetLastError(ERROR_INVALID_PIXEL_FORMAT);
        goto create_failed; /* unexpected error */
    }

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Wine_GLPBuffer));
    if (NULL == object) {
        SetLastError(ERROR_NO_SYSTEM_RESOURCES);
        goto create_failed; /* unexpected error */
    }
    object->hdc = hdc;
    object->display = gdi_display;
    object->width = iWidth;
    object->height = iHeight;
    object->pixelFormat = iPixelFormat;

    nAttribs = ConvertAttribWGLtoGLX(piAttribList, attribs, object);
    if (-1 == nAttribs) {
        WARN("Cannot convert WGL to GLX attributes\n");
        goto create_failed;
    }
    PUSH2(attribs, GLX_PBUFFER_WIDTH,  iWidth);
    PUSH2(attribs, GLX_PBUFFER_HEIGHT, iHeight); 
    while (piAttribList && 0 != *piAttribList) {
        int attr_v;
        switch (*piAttribList) {
            case WGL_TEXTURE_FORMAT_ARB: {
                ++piAttribList;
                attr_v = *piAttribList;
                TRACE("WGL_render_texture Attribute: WGL_TEXTURE_FORMAT_ARB as %x\n", attr_v);
                if (use_render_texture_ati) {
                    int type = 0;
                    switch (attr_v) {
                        case WGL_NO_TEXTURE_ARB: type = GLX_NO_TEXTURE_ATI; break ;
                        case WGL_TEXTURE_RGB_ARB: type = GLX_TEXTURE_RGB_ATI; break ;
                        case WGL_TEXTURE_RGBA_ARB: type = GLX_TEXTURE_RGBA_ATI; break ;
                        default:
                            SetLastError(ERROR_INVALID_DATA);
                            goto create_failed;
                    }
                    object->use_render_texture = 1;
                    PUSH2(attribs, GLX_TEXTURE_FORMAT_ATI, type);
                } else {
                    if (WGL_NO_TEXTURE_ARB == attr_v) {
                        object->use_render_texture = 0;
                    } else {
                        if (!use_render_texture_emulation) {
                            SetLastError(ERROR_INVALID_DATA);
                            goto create_failed;
                        }
                        switch (attr_v) {
                            case WGL_TEXTURE_RGB_ARB:
                                object->use_render_texture = GL_RGB;
                                break;
                            case WGL_TEXTURE_RGBA_ARB:
                                object->use_render_texture = GL_RGBA;
                                break;
                            default:
                                SetLastError(ERROR_INVALID_DATA);
                                goto create_failed;
                        }
                    }
                }
                break;
            }

            case WGL_TEXTURE_TARGET_ARB: {
                ++piAttribList;
                attr_v = *piAttribList;
                TRACE("WGL_render_texture Attribute: WGL_TEXTURE_TARGET_ARB as %x\n", attr_v);
                if (use_render_texture_ati) {
                    int type = 0;
                    switch (attr_v) {
                        case WGL_NO_TEXTURE_ARB: type = GLX_NO_TEXTURE_ATI; break ;
                        case WGL_TEXTURE_CUBE_MAP_ARB: type = GLX_TEXTURE_CUBE_MAP_ATI; break ;
                        case WGL_TEXTURE_1D_ARB: type = GLX_TEXTURE_1D_ATI; break ;
                        case WGL_TEXTURE_2D_ARB: type = GLX_TEXTURE_2D_ATI; break ;
                        default:
                            SetLastError(ERROR_INVALID_DATA);
                            goto create_failed;
                    }
                    PUSH2(attribs, GLX_TEXTURE_TARGET_ATI, type);
                } else {
                    if (WGL_NO_TEXTURE_ARB == attr_v) {
                        object->texture_target = 0;
                    } else {
                        if (!use_render_texture_emulation) {
                            SetLastError(ERROR_INVALID_DATA);
                            goto create_failed;
                        }
                        switch (attr_v) {
                            case WGL_TEXTURE_CUBE_MAP_ARB: {
                                if (iWidth != iHeight) {
                                    SetLastError(ERROR_INVALID_DATA);
                                    goto create_failed;
                                }
                                object->texture_target = GL_TEXTURE_CUBE_MAP;
                                object->texture_bind_target = GL_TEXTURE_CUBE_MAP;
                               break;
                            }
                            case WGL_TEXTURE_1D_ARB: {
                                if (1 != iHeight) {
                                    SetLastError(ERROR_INVALID_DATA);
                                    goto create_failed;
                                }
                                object->texture_target = GL_TEXTURE_1D;
                                object->texture_bind_target = GL_TEXTURE_1D;
                                break;
                            }
                            case WGL_TEXTURE_2D_ARB: {
                                object->texture_target = GL_TEXTURE_2D;
                                object->texture_bind_target = GL_TEXTURE_2D;
                                break;
                            }
                            default:
                                SetLastError(ERROR_INVALID_DATA);
                                goto create_failed;
                        }
                    }
                }
                break;
            }

            case WGL_MIPMAP_TEXTURE_ARB: {
                ++piAttribList;
                attr_v = *piAttribList;
                TRACE("WGL_render_texture Attribute: WGL_MIPMAP_TEXTURE_ARB as %x\n", attr_v);
                if (use_render_texture_ati) {
                    PUSH2(attribs, GLX_MIPMAP_TEXTURE_ATI, attr_v);
                } else {
                    if (!use_render_texture_emulation) {
                        SetLastError(ERROR_INVALID_DATA);
                        goto create_failed;
                    }
                }
                break;
            }
        }
        ++piAttribList;
    }

    PUSH1(attribs, None);
    object->drawable = pglXCreatePbuffer(gdi_display, cfgs[fmt_index], attribs);
    TRACE("new Pbuffer drawable as %p\n", (void*) object->drawable);
    if (!object->drawable) {
        SetLastError(ERROR_NO_SYSTEM_RESOURCES);
        goto create_failed; /* unexpected error */
    }
    TRACE("->(%p)\n", object);

    /** free list */
    XFree(cfgs);
    return (HPBUFFERARB) object;

create_failed:
    if (NULL != cfgs) XFree(cfgs);
    HeapFree(GetProcessHeap(), 0, object);
    TRACE("->(FAILED)\n");
    return (HPBUFFERARB) NULL;
}

/**
 * X11DRV_wglDestroyPbufferARB
 *
 * WGL_ARB_pbuffer: wglDestroyPbufferARB
 */
static GLboolean WINAPI X11DRV_wglDestroyPbufferARB(HPBUFFERARB hPbuffer)
{
    Wine_GLPBuffer* object = (Wine_GLPBuffer*) hPbuffer;
    TRACE("(%p)\n", hPbuffer);
    if (NULL == object) {
        SetLastError(ERROR_INVALID_HANDLE);
        return GL_FALSE;
    }
    pglXDestroyPbuffer(object->display, object->drawable);
    HeapFree(GetProcessHeap(), 0, object);
    return GL_TRUE;
}

/**
 * X11DRV_wglGetPbufferDCARB
 *
 * WGL_ARB_pbuffer: wglGetPbufferDCARB
 * The function wglGetPbufferDCARB returns a device context for a pbuffer.
 * Gdi32 implements the part of this function which creates a device context.
 * This part associates the physDev with the X drawable of the pbuffer.
 */
HDC X11DRV_wglGetPbufferDCARB(X11DRV_PDEVICE *physDev, HPBUFFERARB hPbuffer)
{
    Wine_GLPBuffer* object = (Wine_GLPBuffer*) hPbuffer;
    if (NULL == object) {
        SetLastError(ERROR_INVALID_HANDLE);
        return NULL;
    }

    /* The function wglGetPbufferDCARB returns a DC to which the pbuffer can be connected.
     * All formats in our pixelformat list are compatible with each other and the main drawable. */
    physDev->current_pf = object->pixelFormat;
    physDev->drawable = object->drawable;

    TRACE("(%p)->(%p)\n", hPbuffer, physDev->hdc);
    return physDev->hdc;
}

/**
 * X11DRV_wglQueryPbufferARB
 *
 * WGL_ARB_pbuffer: wglQueryPbufferARB
 */
static GLboolean WINAPI X11DRV_wglQueryPbufferARB(HPBUFFERARB hPbuffer, int iAttribute, int *piValue)
{
    Wine_GLPBuffer* object = (Wine_GLPBuffer*) hPbuffer;
    TRACE("(%p, 0x%x, %p)\n", hPbuffer, iAttribute, piValue);
    if (NULL == object) {
        SetLastError(ERROR_INVALID_HANDLE);
        return GL_FALSE;
    }
    switch (iAttribute) {
        case WGL_PBUFFER_WIDTH_ARB:
            pglXQueryDrawable(object->display, object->drawable, GLX_WIDTH, (unsigned int*) piValue);
            break;
        case WGL_PBUFFER_HEIGHT_ARB:
            pglXQueryDrawable(object->display, object->drawable, GLX_HEIGHT, (unsigned int*) piValue);
            break;

        case WGL_PBUFFER_LOST_ARB:
            FIXME("unsupported WGL_PBUFFER_LOST_ARB (need glXSelectEvent/GLX_DAMAGED work)\n");
            break;

        case WGL_TEXTURE_FORMAT_ARB:
            if (use_render_texture_ati) {
                unsigned int tmp;
                int type = WGL_NO_TEXTURE_ARB;
                pglXQueryDrawable(object->display, object->drawable, GLX_TEXTURE_FORMAT_ATI, &tmp);
                switch (tmp) {
                    case GLX_NO_TEXTURE_ATI: type = WGL_NO_TEXTURE_ARB; break ;
                    case GLX_TEXTURE_RGB_ATI: type = WGL_TEXTURE_RGB_ARB; break ;
                    case GLX_TEXTURE_RGBA_ATI: type = WGL_TEXTURE_RGBA_ARB; break ;
                }
                *piValue = type;
            } else {
                if (!object->use_render_texture) {
                    *piValue = WGL_NO_TEXTURE_ARB;
                } else {
                    if (!use_render_texture_emulation) {
                        SetLastError(ERROR_INVALID_HANDLE);
                        return GL_FALSE;
                    }
                    if (GL_RGBA == object->use_render_texture) {
                        *piValue = WGL_TEXTURE_RGBA_ARB;
                    } else {
                        *piValue = WGL_TEXTURE_RGB_ARB;
                    }
                }
            }
            break;

        case WGL_TEXTURE_TARGET_ARB:
            if (use_render_texture_ati) {
                unsigned int tmp;
                int type = WGL_NO_TEXTURE_ARB;
                pglXQueryDrawable(object->display, object->drawable, GLX_TEXTURE_TARGET_ATI, &tmp);
                switch (tmp) {
                    case GLX_NO_TEXTURE_ATI: type = WGL_NO_TEXTURE_ARB; break ;
                    case GLX_TEXTURE_CUBE_MAP_ATI: type = WGL_TEXTURE_CUBE_MAP_ARB; break ;
                    case GLX_TEXTURE_1D_ATI: type = WGL_TEXTURE_1D_ARB; break ;
                    case GLX_TEXTURE_2D_ATI: type = WGL_TEXTURE_2D_ARB; break ;
                }
                *piValue = type;
            } else {
            if (!object->texture_target) {
                *piValue = WGL_NO_TEXTURE_ARB;
            } else {
                if (!use_render_texture_emulation) {
                    SetLastError(ERROR_INVALID_DATA);      
                    return GL_FALSE;
                }
                switch (object->texture_target) {
                    case GL_TEXTURE_1D:       *piValue = WGL_TEXTURE_CUBE_MAP_ARB; break;
                    case GL_TEXTURE_2D:       *piValue = WGL_TEXTURE_1D_ARB; break;
                    case GL_TEXTURE_CUBE_MAP: *piValue = WGL_TEXTURE_2D_ARB; break;
                }
            }
        }
        break;

    case WGL_MIPMAP_TEXTURE_ARB:
        if (use_render_texture_ati) {
            pglXQueryDrawable(object->display, object->drawable, GLX_MIPMAP_TEXTURE_ATI, (unsigned int*) piValue);
        } else {
            *piValue = GL_FALSE; /** don't support that */
            FIXME("unsupported WGL_ARB_render_texture attribute query for 0x%x\n", iAttribute);
        }
        break;

    default:
        FIXME("unexpected attribute %x\n", iAttribute);
        break;
    }

    return GL_TRUE;
}

/**
 * X11DRV_wglReleasePbufferDCARB
 *
 * WGL_ARB_pbuffer: wglReleasePbufferDCARB
 */
static int WINAPI X11DRV_wglReleasePbufferDCARB(HPBUFFERARB hPbuffer, HDC hdc)
{
    TRACE("(%p, %p)\n", hPbuffer, hdc);
    DeleteDC(hdc);
    return 0;
}

/**
 * X11DRV_wglSetPbufferAttribARB
 *
 * WGL_ARB_pbuffer: wglSetPbufferAttribARB
 */
static GLboolean WINAPI X11DRV_wglSetPbufferAttribARB(HPBUFFERARB hPbuffer, const int *piAttribList)
{
    Wine_GLPBuffer* object = (Wine_GLPBuffer*) hPbuffer;
    WARN("(%p, %p): alpha-testing, report any problem\n", hPbuffer, piAttribList);
    if (NULL == object) {
        SetLastError(ERROR_INVALID_HANDLE);
        return GL_FALSE;
    }
    if (!object->use_render_texture) {
        SetLastError(ERROR_INVALID_HANDLE);
        return GL_FALSE;
    }
    if (!use_render_texture_ati && 1 == use_render_texture_emulation) {
        return GL_TRUE;
    }
    if (NULL != pglXDrawableAttribARB) {
        if (use_render_texture_ati) {
            FIXME("Need conversion for GLX_ATI_render_texture\n");
        }
        return pglXDrawableAttribARB(object->display, object->drawable, piAttribList); 
    }
    return GL_FALSE;
}

/**
 * X11DRV_wglChoosePixelFormatARB
 *
 * WGL_ARB_pixel_format: wglChoosePixelFormatARB
 */
static GLboolean WINAPI X11DRV_wglChoosePixelFormatARB(HDC hdc, const int *piAttribIList, const FLOAT *pfAttribFList, UINT nMaxFormats, int *piFormats, UINT *nNumFormats)
{
    int gl_test = 0;
    int attribs[256];
    int nAttribs = 0;
    GLXFBConfig* cfgs = NULL;
    int nCfgs = 0;
    UINT it;
    int fmt_id;

    GLXFBConfig* cfgs_fmt = NULL;
    int nCfgs_fmt = 0;

    int fmt = 0;
    int pfmt_it = 0;

    TRACE("(%p, %p, %p, %d, %p, %p): hackish\n", hdc, piAttribIList, pfAttribFList, nMaxFormats, piFormats, nNumFormats);
    if (NULL != pfAttribFList) {
        FIXME("unused pfAttribFList\n");
    }

    nAttribs = ConvertAttribWGLtoGLX(piAttribIList, attribs, NULL);
    if (-1 == nAttribs) {
        WARN("Cannot convert WGL to GLX attributes\n");
        return GL_FALSE;
    }
    PUSH1(attribs, None);

    /* Search for FB configurations matching the requirements in attribs */
    cfgs = pglXChooseFBConfig(gdi_display, DefaultScreen(gdi_display), attribs, &nCfgs);
    if (NULL == cfgs) {
        WARN("Compatible Pixel Format not found\n");
        return GL_FALSE;
    }

    /* Get a list of all FB configurations */
    cfgs_fmt = pglXGetFBConfigs(gdi_display, DefaultScreen(gdi_display), &nCfgs_fmt);
    if (NULL == cfgs_fmt) {
        ERR("Failed to get All FB Configs\n");
        XFree(cfgs);
        return GL_FALSE;
    }

    /* Loop through all matching formats and check if they are suitable.
    * Note that this function should at max return nMaxFormats different formats */
    for (it = 0; pfmt_it < nMaxFormats && it < nCfgs; ++it) {
        gl_test = pglXGetFBConfigAttrib(gdi_display, cfgs[it], GLX_FBCONFIG_ID, &fmt_id);
        if (gl_test) {
            ERR("Failed to retrieve FBCONFIG_ID from GLXFBConfig, expect problems.\n");
            continue;
        }

        /* Search for the format in our list of compatible formats */
        fmt = ConvertPixelFormatGLXtoWGL(gdi_display, fmt_id);
        if(!fmt)
            continue;

        piFormats[pfmt_it] = fmt;
        TRACE("at %d/%d found FBCONFIG_ID 0x%x (%d/%d)\n", it + 1, nCfgs, fmt_id, piFormats[pfmt_it], nCfgs_fmt);
        pfmt_it++;
    }

    *nNumFormats = pfmt_it;
    /** free list */
    XFree(cfgs);
    XFree(cfgs_fmt);
    return GL_TRUE;
}

/**
 * X11DRV_wglGetPixelFormatAttribivARB
 *
 * WGL_ARB_pixel_format: wglGetPixelFormatAttribivARB
 */
static GLboolean WINAPI X11DRV_wglGetPixelFormatAttribivARB(HDC hdc, int iPixelFormat, int iLayerPlane, UINT nAttributes, const int *piAttributes, int *piValues)
{
    UINT i;
    GLXFBConfig* cfgs = NULL;
    GLXFBConfig  curCfg = NULL;
    int nCfgs = 0;
    int hTest;
    int tmp;
    int curGLXAttr = 0;
    int nWGLFormats = 0;
    int fmt_index = 0;

    TRACE("(%p, %d, %d, %d, %p, %p)\n", hdc, iPixelFormat, iLayerPlane, nAttributes, piAttributes, piValues);

    if (0 < iLayerPlane) {
        FIXME("unsupported iLayerPlane(%d) > 0, returns FALSE\n", iLayerPlane);
        return GL_FALSE;
    }

    cfgs = pglXGetFBConfigs(gdi_display, DefaultScreen(gdi_display), &nCfgs);
    if (NULL == cfgs) {
        ERR("no FB Configs found for display(%p)\n", gdi_display);
        return GL_FALSE;
    }

    /* Convert the WGL pixelformat to a GLX one, if this fails then most likely the iPixelFormat isn't supoprted.
    * We don't have to fail yet as a program can specify an invaled iPixelFormat (lets say 0) if it wants to query
    * the number of supported WGL formats. Whether the iPixelFormat is valid is handled in the for-loop below. */
    if(!ConvertPixelFormatWGLtoGLX(gdi_display, iPixelFormat, &fmt_index, &nWGLFormats)) {
        WARN("Unable to convert iPixelFormat %d to a GLX one!\n", iPixelFormat);
    }

    for (i = 0; i < nAttributes; ++i) {
        const int curWGLAttr = piAttributes[i];
        TRACE("pAttr[%d] = %x\n", i, curWGLAttr);

        switch (curWGLAttr) {
            case WGL_NUMBER_PIXEL_FORMATS_ARB:
                piValues[i] = nWGLFormats; 
                continue;

            case WGL_SUPPORT_OPENGL_ARB:
                piValues[i] = GL_TRUE; 
                continue;

            case WGL_ACCELERATION_ARB:
                curGLXAttr = GLX_CONFIG_CAVEAT;

                if (nCfgs < iPixelFormat || 0 >= iPixelFormat) goto pix_error;
                    curCfg = cfgs[iPixelFormat - 1];

                hTest = pglXGetFBConfigAttrib(gdi_display, curCfg, curGLXAttr, &tmp);
                if (hTest) goto get_error;
                switch (tmp) {
                    case GLX_NONE: piValues[i] = WGL_FULL_ACCELERATION_ARB; break;
                    case GLX_SLOW_CONFIG: piValues[i] = WGL_NO_ACCELERATION_ARB; break;
                    case GLX_NON_CONFORMANT_CONFIG: piValues[i] = WGL_FULL_ACCELERATION_ARB; break;
                    default:
                        ERR("unexpected Config Caveat(%x)\n", tmp);
                        piValues[i] = WGL_NO_ACCELERATION_ARB;
                }
                continue;

            case WGL_TRANSPARENT_ARB:
                curGLXAttr = GLX_TRANSPARENT_TYPE;
                    /* Check if the format is supported by checking if iPixelFormat isn't larger than the max number of 
                     * supported WGLFormats and also check if the GLX fmt_index is valid. */
                if((iPixelFormat > nWGLFormats) || (fmt_index > nCfgs)) goto pix_error;
                    curCfg = cfgs[fmt_index];
                hTest = pglXGetFBConfigAttrib(gdi_display, curCfg, curGLXAttr, &tmp);
                if (hTest) goto get_error;
                    piValues[i] = GL_FALSE;
                if (GLX_NONE != tmp) piValues[i] = GL_TRUE;
                    continue;

            case WGL_PIXEL_TYPE_ARB:
                curGLXAttr = GLX_RENDER_TYPE;
                /* Check if the format is supported by checking if iPixelFormat isn't larger than the max number of 
                * supported WGLFormats and also check if the GLX fmt_index is valid. */
                if((iPixelFormat > nWGLFormats) || (fmt_index > nCfgs)) goto pix_error;
                    curCfg = cfgs[fmt_index];
                hTest = pglXGetFBConfigAttrib(gdi_display, curCfg, curGLXAttr, &tmp);
                if (hTest) goto get_error;
                TRACE("WGL_PIXEL_TYPE_ARB: GLX_RENDER_TYPE = 0x%x\n", tmp);
                if      (tmp & GLX_RGBA_BIT)           { piValues[i] = WGL_TYPE_RGBA_ARB; }
                else if (tmp & GLX_COLOR_INDEX_BIT)    { piValues[i] = WGL_TYPE_COLORINDEX_ARB; }
                else if (tmp & GLX_RGBA_FLOAT_BIT)     { piValues[i] = WGL_TYPE_RGBA_FLOAT_ATI; }
                else if (tmp & GLX_RGBA_FLOAT_ATI_BIT) { piValues[i] = WGL_TYPE_RGBA_FLOAT_ATI; }
                else {
                    ERR("unexpected RenderType(%x)\n", tmp);
                    piValues[i] = WGL_TYPE_RGBA_ARB;
                }
                continue;

            case WGL_COLOR_BITS_ARB:
                /** see ConvertAttribWGLtoGLX for explain */
                /* Check if the format is supported by checking if iPixelFormat isn't larger than the max number of 
                * supported WGLFormats and also check if the GLX fmt_index is valid. */
                if((iPixelFormat > nWGLFormats) || (fmt_index > nCfgs)) goto pix_error;
                    curCfg = cfgs[fmt_index];
                hTest = pglXGetFBConfigAttrib(gdi_display, curCfg, GLX_BUFFER_SIZE, piValues + i);
                if (hTest) goto get_error;
                TRACE("WGL_COLOR_BITS_ARB: GLX_BUFFER_SIZE = %d\n", piValues[i]);
                hTest = pglXGetFBConfigAttrib(gdi_display, curCfg, GLX_ALPHA_SIZE, &tmp);
                if (hTest) goto get_error;
                TRACE("WGL_COLOR_BITS_ARB: GLX_ALPHA_SIZE = %d\n", tmp);
                piValues[i] = piValues[i] - tmp;
                continue;

            case WGL_BIND_TO_TEXTURE_RGB_ARB:
                if (use_render_texture_ati) {
                    curGLXAttr = GLX_BIND_TO_TEXTURE_RGB_ATI;
                    break;
                }
            case WGL_BIND_TO_TEXTURE_RGBA_ARB:
                if (use_render_texture_ati) {
                    curGLXAttr = GLX_BIND_TO_TEXTURE_RGBA_ATI;
                    break;
                }
                if (!use_render_texture_emulation) {
                    piValues[i] = GL_FALSE;
                    continue;	
                }
                curGLXAttr = GLX_RENDER_TYPE;
                /* Check if the format is supported by checking if iPixelFormat isn't larger than the max number of 
                 * supported WGLFormats and also check if the GLX fmt_index is valid. */
                if((iPixelFormat > nWGLFormats) || (fmt_index > nCfgs)) goto pix_error;
                    curCfg = cfgs[fmt_index];
                hTest = pglXGetFBConfigAttrib(gdi_display, curCfg, curGLXAttr, &tmp);
                if (hTest) goto get_error;
                if (GLX_COLOR_INDEX_BIT == tmp) {
                    piValues[i] = GL_FALSE;  
                    continue;
                }
                hTest = pglXGetFBConfigAttrib(gdi_display, curCfg, GLX_DRAWABLE_TYPE, &tmp);
                if (hTest) goto get_error;
                    piValues[i] = (tmp & GLX_PBUFFER_BIT) ? GL_TRUE : GL_FALSE;
                continue;

            case WGL_BLUE_BITS_ARB:
                curGLXAttr = GLX_BLUE_SIZE;
                break;
            case WGL_RED_BITS_ARB:
                curGLXAttr = GLX_RED_SIZE;
                break;
            case WGL_GREEN_BITS_ARB:
                curGLXAttr = GLX_GREEN_SIZE;
                break;
            case WGL_ALPHA_BITS_ARB:
                curGLXAttr = GLX_ALPHA_SIZE;
                break;
            case WGL_DEPTH_BITS_ARB:
                curGLXAttr = GLX_DEPTH_SIZE;
                break;
            case WGL_STENCIL_BITS_ARB:
                curGLXAttr = GLX_STENCIL_SIZE;
                break;
            case WGL_DOUBLE_BUFFER_ARB:
                curGLXAttr = GLX_DOUBLEBUFFER;
                break;
            case WGL_STEREO_ARB:
                curGLXAttr = GLX_STEREO;
                break;
            case WGL_AUX_BUFFERS_ARB:
                curGLXAttr = GLX_AUX_BUFFERS;
                break;
            case WGL_SUPPORT_GDI_ARB:
            case WGL_DRAW_TO_WINDOW_ARB:
            case WGL_DRAW_TO_BITMAP_ARB:
                /* We only supported a limited number of formats right now which are all renderable by X 'GLX_X_RENDERABLE' */
                piValues[i] = GL_TRUE;
                continue;
            case WGL_DRAW_TO_PBUFFER_ARB:
                hTest = pglXGetFBConfigAttrib(gdi_display, curCfg, GLX_DRAWABLE_TYPE, &tmp);
                if (hTest) goto get_error;
                    piValues[i] = (tmp & GLX_PBUFFER_BIT) ? GL_TRUE : GL_FALSE;
                continue;

            case WGL_PBUFFER_LARGEST_ARB:
                curGLXAttr = GLX_LARGEST_PBUFFER;
                break;

            case WGL_SAMPLE_BUFFERS_ARB:
                curGLXAttr = GLX_SAMPLE_BUFFERS_ARB;
                break;

            case WGL_SAMPLES_ARB:
                curGLXAttr = GLX_SAMPLES_ARB;
                break;

            default:
                FIXME("unsupported %x WGL Attribute\n", curWGLAttr);
        }

        /* Retrieve a GLX FBConfigAttrib when the attribute to query is valid and
         * iPixelFormat != 0. When iPixelFormat is 0 the only value which makes
         * sense to query is WGL_NUMBER_PIXEL_FORMATS_ARB.
         *
         * TODO: properly test the behavior of wglGetPixelFormatAttrib*v on Windows
         *       and check which options can work using iPixelFormat=0 and which not.
         *       A problem would be that this function is an extension. This would
         *       mean that the behavior could differ between different vendors (ATI, Nvidia, ..).
         */
        if (0 != curGLXAttr && iPixelFormat != 0) {
            /* Check if the format is supported by checking if iPixelFormat isn't larger than the max number of 
            * supported WGLFormats and also check if the GLX fmt_index is valid. */
            if((iPixelFormat > 0) && ((iPixelFormat > nWGLFormats) || (fmt_index > nCfgs))) goto pix_error;
                curCfg = cfgs[fmt_index];
            hTest = pglXGetFBConfigAttrib(gdi_display, curCfg, curGLXAttr, piValues + i);
            if (hTest) goto get_error;
            curGLXAttr = 0;
        } else { 
            piValues[i] = GL_FALSE; 
        }
    }
    return GL_TRUE;

get_error:
    ERR("(%p): unexpected failure on GetFBConfigAttrib(%x) returns FALSE\n", hdc, curGLXAttr);
    XFree(cfgs);
    return GL_FALSE;

pix_error:
    ERR("(%p): unexpected iPixelFormat(%d) vs nFormats(%d), returns FALSE\n", hdc, iPixelFormat, nCfgs);
    XFree(cfgs);
    return GL_FALSE;
}

/**
 * X11DRV_wglGetPixelFormatAttribfvARB
 *
 * WGL_ARB_pixel_format: wglGetPixelFormatAttribfvARB
 */
static GLboolean WINAPI X11DRV_wglGetPixelFormatAttribfvARB(HDC hdc, int iPixelFormat, int iLayerPlane, UINT nAttributes, const int *piAttributes, FLOAT *pfValues)
{
    int *attr;
    int ret;
    int i;

    TRACE("(%p, %d, %d, %d, %p, %p)\n", hdc, iPixelFormat, iLayerPlane, nAttributes, piAttributes, pfValues);

    /* Allocate a temporary array to store integer values */
    attr = HeapAlloc(GetProcessHeap(), 0, nAttributes * sizeof(int));
    if (!attr) {
        ERR("couldn't allocate %d array\n", nAttributes);
        return GL_FALSE;
    }

    /* Piggy-back on wglGetPixelFormatAttribivARB */
    ret = X11DRV_wglGetPixelFormatAttribivARB(hdc, iPixelFormat, iLayerPlane, nAttributes, piAttributes, attr);
    if (ret) {
        /* Convert integer values to float. Should also check for attributes
           that can give decimal values here */
        for (i=0; i<nAttributes;i++) {
            pfValues[i] = attr[i];
        }
    }

    HeapFree(GetProcessHeap(), 0, attr);
    return ret;
}

/**
 * X11DRV_wglBindTexImageARB
 *
 * WGL_ARB_render_texture: wglBindTexImageARB
 */
static GLboolean WINAPI X11DRV_wglBindTexImageARB(HPBUFFERARB hPbuffer, int iBuffer)
{
    Wine_GLPBuffer* object = (Wine_GLPBuffer*) hPbuffer;
    TRACE("(%p, %d)\n", hPbuffer, iBuffer);
    if (NULL == object) {
        SetLastError(ERROR_INVALID_HANDLE);
        return GL_FALSE;
    }
    if (!object->use_render_texture) {
        SetLastError(ERROR_INVALID_HANDLE);
        return GL_FALSE;
    }
/* Disable WGL_ARB_render_texture support until it is implemented properly
 * using pbuffers or FBOs */
#if 0
    if (!use_render_texture_ati && 1 == use_render_texture_emulation) {
        int do_init = 0;
        GLint prev_binded_tex;
        pglGetIntegerv(object->texture_target, &prev_binded_tex);
        if (NULL == object->render_ctx) {
            object->render_hdc = X11DRV_wglGetPbufferDCARB(hPbuffer);
            /* FIXME: This is routed through gdi32.dll to winex11.drv, replace this with GLX calls */
            object->render_ctx = wglCreateContext(object->render_hdc);
            do_init = 1;
        }
        object->prev_hdc = wglGetCurrentDC();
        object->prev_ctx = wglGetCurrentContext();
        /* FIXME: This is routed through gdi32.dll to winex11.drv, replace this with GLX calls */
        wglMakeCurrent(object->render_hdc, object->render_ctx);
        /*
        if (do_init) {
            glBindTexture(object->texture_target, object->texture);
            if (GL_RGBA == object->use_render_texture) {
                glTexImage2D(object->texture_target, 0, GL_RGBA8, object->width, object->height, 0, GL_RGBA, GL_FLOAT, NULL);
            } else {
                glTexImage2D(object->texture_target, 0, GL_RGB8, object->width, object->height, 0, GL_RGB, GL_FLOAT, NULL);
            }
        }
        */
        object->texture = prev_binded_tex;
        return GL_TRUE;
    }
#endif
    if (NULL != pglXBindTexImageARB) {
        return pglXBindTexImageARB(object->display, object->drawable, iBuffer);
    }
    return GL_FALSE;
}

/**
 * X11DRV_wglReleaseTexImageARB
 *
 * WGL_ARB_render_texture: wglReleaseTexImageARB
 */
static GLboolean WINAPI X11DRV_wglReleaseTexImageARB(HPBUFFERARB hPbuffer, int iBuffer)
{
    Wine_GLPBuffer* object = (Wine_GLPBuffer*) hPbuffer;
    TRACE("(%p, %d)\n", hPbuffer, iBuffer);
    if (NULL == object) {
        SetLastError(ERROR_INVALID_HANDLE);
        return GL_FALSE;
    }
    if (!object->use_render_texture) {
        SetLastError(ERROR_INVALID_HANDLE);
        return GL_FALSE;
    }
    if (!use_render_texture_ati && 1 == use_render_texture_emulation) {
    /*
        GLint prev_binded_tex;
        glGetIntegerv(object->texture_target, &prev_binded_tex);    
        if (GL_TEXTURE_1D == object->texture_target) {
            glCopyTexSubImage1D(object->texture_target, object->texture_level, 0, 0, 0, object->width);
        } else {
            glCopyTexSubImage2D(object->texture_target, object->texture_level, 0, 0, 0, 0, object->width,       object->height);
        }
        glBindTexture(object->texture_target, prev_binded_tex);
        SwapBuffers(object->render_hdc);
        */
        pglBindTexture(object->texture_target, object->texture);
        if (GL_TEXTURE_1D == object->texture_target) {
            pglCopyTexSubImage1D(object->texture_target, object->texture_level, 0, 0, 0, object->width);
        } else {
            pglCopyTexSubImage2D(object->texture_target, object->texture_level, 0, 0, 0, 0, object->width, object->height);
        }

        /* FIXME: This is routed through gdi32.dll to winex11.drv, replace this with GLX calls */
        wglMakeCurrent(object->prev_hdc, object->prev_ctx);
        return GL_TRUE;
    }
    if (NULL != pglXReleaseTexImageARB) {
        return pglXReleaseTexImageARB(object->display, object->drawable, iBuffer);
    }
    return GL_FALSE;
}

/**
 * X11DRV_wglGetExtensionsStringEXT
 *
 * WGL_EXT_extensions_string: wglGetExtensionsStringEXT
 */
static const char * WINAPI X11DRV_wglGetExtensionsStringEXT(void) {
    TRACE("() returning \"%s\"\n", WineGLInfo.wglExtensions);
    return WineGLInfo.wglExtensions;
}

/**
 * X11DRV_wglGetSwapIntervalEXT
 *
 * WGL_EXT_swap_control: wglGetSwapIntervalEXT
 */
static int WINAPI X11DRV_wglGetSwapIntervalEXT(VOID) {
    FIXME("(),stub!\n");
    return swap_interval;
}

/**
 * X11DRV_wglSwapIntervalEXT
 *
 * WGL_EXT_swap_control: wglSwapIntervalEXT
 */
static BOOL WINAPI X11DRV_wglSwapIntervalEXT(int interval) {
    TRACE("(%d)\n", interval);
    swap_interval = interval;
    if (NULL != pglXSwapIntervalSGI) {
        return 0 == pglXSwapIntervalSGI(interval);
    }
    WARN("(): GLX_SGI_swap_control extension seems not supported\n");
    return TRUE;
}

/**
 * X11DRV_wglAllocateMemoryNV
 *
 * WGL_NV_vertex_array_range: wglAllocateMemoryNV
 */
static void* WINAPI X11DRV_wglAllocateMemoryNV(GLsizei size, GLfloat readfreq, GLfloat writefreq, GLfloat priority) {
    TRACE("(%d, %f, %f, %f)\n", size, readfreq, writefreq, priority );
    if (pglXAllocateMemoryNV == NULL)
        return NULL;

    return pglXAllocateMemoryNV(size, readfreq, writefreq, priority);
}

/**
 * X11DRV_wglFreeMemoryNV
 *
 * WGL_NV_vertex_array_range: wglFreeMemoryNV
 */
static void WINAPI X11DRV_wglFreeMemoryNV(GLvoid* pointer) {
    TRACE("(%p)\n", pointer);
    if (pglXFreeMemoryNV == NULL)
        return;

    pglXFreeMemoryNV(pointer);
}

/**
 * glxRequireVersion (internal)
 *
 * Check if the supported GLX version matches requiredVersion.
 */
static BOOL glxRequireVersion(int requiredVersion)
{
    /* Both requiredVersion and glXVersion[1] contains the minor GLX version */
    if(requiredVersion <= WineGLInfo.glxVersion[1])
        return TRUE;

    return FALSE;
}

static BOOL glxRequireExtension(const char *requiredExtension)
{
    if (strstr(WineGLInfo.glxClientExtensions, requiredExtension) == NULL) {
        return FALSE;
    }

    return TRUE;
}

static BOOL register_extension(const WineGLExtension * ext)
{
    int i;

    assert( WineGLExtensionListSize < MAX_EXTENSIONS );
    WineGLExtensionList[WineGLExtensionListSize++] = ext;

    strcat(WineGLInfo.wglExtensions, " ");
    strcat(WineGLInfo.wglExtensions, ext->extName);

    TRACE("'%s'\n", ext->extName);

    for (i = 0; ext->extEntryPoints[i].funcName; ++i)
        TRACE("    - '%s'\n", ext->extEntryPoints[i].funcName);

    return TRUE;
}

static const WineGLExtension WGL_internal_functions =
{
  "",
  {
    { "wglDisable", X11DRV_wglDisable },
    { "wglEnable", X11DRV_wglEnable },
    { "wglGetIntegerv", X11DRV_wglGetIntegerv },
    { "wglIsEnabled", X11DRV_wglIsEnabled },
    { "wglScissor", X11DRV_wglScissor },
    { "wglViewport", X11DRV_wglViewport },
  }
};


static const WineGLExtension WGL_ARB_extensions_string =
{
  "WGL_ARB_extensions_string",
  {
    { "wglGetExtensionsStringARB", X11DRV_wglGetExtensionsStringARB },
  }
};

static const WineGLExtension WGL_ARB_make_current_read =
{
  "WGL_ARB_make_current_read",
  {
    { "wglGetCurrentReadDCARB", X11DRV_wglGetCurrentReadDCARB },
    { "wglMakeContextCurrentARB", X11DRV_wglMakeContextCurrentARB },
  }
};

static const WineGLExtension WGL_ARB_multisample =
{
  "WGL_ARB_multisample",
};

static const WineGLExtension WGL_ARB_pbuffer =
{
  "WGL_ARB_pbuffer",
  {
    { "wglCreatePbufferARB", X11DRV_wglCreatePbufferARB },
    { "wglDestroyPbufferARB", X11DRV_wglDestroyPbufferARB },
    { "wglGetPbufferDCARB", X11DRV_wglGetPbufferDCARB },
    { "wglQueryPbufferARB", X11DRV_wglQueryPbufferARB },
    { "wglReleasePbufferDCARB", X11DRV_wglReleasePbufferDCARB },
    { "wglSetPbufferAttribARB", X11DRV_wglSetPbufferAttribARB },
  }
};

static const WineGLExtension WGL_ARB_pixel_format =
{
  "WGL_ARB_pixel_format",
  {
    { "wglChoosePixelFormatARB", X11DRV_wglChoosePixelFormatARB },
    { "wglGetPixelFormatAttribfvARB", X11DRV_wglGetPixelFormatAttribfvARB },
    { "wglGetPixelFormatAttribivARB", X11DRV_wglGetPixelFormatAttribivARB },
  }
};

static const WineGLExtension WGL_ARB_render_texture =
{
  "WGL_ARB_render_texture",
  {
    { "wglBindTexImageARB", X11DRV_wglBindTexImageARB },
    { "wglReleaseTexImageARB", X11DRV_wglReleaseTexImageARB },
  }
};

static const WineGLExtension WGL_EXT_extensions_string =
{
  "WGL_EXT_extensions_string",
  {
    { "wglGetExtensionsStringEXT", X11DRV_wglGetExtensionsStringEXT },
  }
};

static const WineGLExtension WGL_EXT_swap_control =
{
  "WGL_EXT_swap_control",
  {
    { "wglSwapIntervalEXT", X11DRV_wglSwapIntervalEXT },
    { "wglGetSwapIntervalEXT", X11DRV_wglGetSwapIntervalEXT },
  }
};

static const WineGLExtension WGL_NV_vertex_array_range =
{
  "WGL_NV_vertex_array_range",
  {
    { "wglAllocateMemoryNV", X11DRV_wglAllocateMemoryNV },
    { "wglFreeMemoryNV", X11DRV_wglFreeMemoryNV },
  }
};

/**
 * X11DRV_WineGL_LoadExtensions
 */
static void X11DRV_WineGL_LoadExtensions(void)
{
    WineGLInfo.wglExtensions[0] = 0;

    /* Load Wine internal functions */
    register_extension(&WGL_internal_functions);

    /* ARB Extensions */

    register_extension(&WGL_ARB_extensions_string);

    if (glxRequireVersion(3))
        register_extension(&WGL_ARB_make_current_read);

    if (glxRequireExtension("GLX_ARB_multisample"))
        register_extension(&WGL_ARB_multisample);

    /* In general pbuffer functionality requires support in the X-server. The functionality is
     * available either when the GLX_SGIX_pbuffer is present or when the GLX server version is 1.3.
     * All display drivers except for Nvidia's use the GLX module from Xfree86/Xorg which only
     * supports GLX 1.2. The endresult is that only Nvidia's drivers support pbuffers.
     *
     * The only other drive which has pbuffer support is Ati's FGLRX driver. They provide clientside GLX 1.3 support
     * without support in the X-server (which other Mesa based drivers require).
     *
     * Support pbuffers when the GLX version is 1.3 and GLX_SGIX_pbuffer is available. Further pbuffers can
     * also be supported when GLX_ATI_render_texture is available. This extension depends on pbuffers, so when it
     * is available pbuffers must be available too. */
    if ( (glxRequireVersion(3) && glxRequireExtension("GLX_SGIX_pbuffer")) || glxRequireExtension("GLX_ATI_render_texture"))
        register_extension(&WGL_ARB_pbuffer);

    register_extension(&WGL_ARB_pixel_format);

    if (glxRequireExtension("GLX_ATI_render_texture") ||
        glxRequireExtension("GLX_ARB_render_texture"))
        register_extension(&WGL_ARB_render_texture);

    /* EXT Extensions */

    register_extension(&WGL_EXT_extensions_string);

    if (glxRequireExtension("GLX_SGI_swap_control"))
        register_extension(&WGL_EXT_swap_control);

    /* The OpenGL extension GL_NV_vertex_array_range adds wgl/glX functions which aren't exported as 'real' wgl/glX extensions. */
    if(strstr(WineGLInfo.glExtensions, "GL_NV_vertex_array_range") != NULL)
        register_extension(&WGL_NV_vertex_array_range);
}


static XID create_glxpixmap(X11DRV_PDEVICE *physDev)
{
    GLXPixmap ret;
    XVisualInfo *vis;
    XVisualInfo template;
    int num;

    wine_tsx11_lock();

    /* Retrieve the visualid from our main visual which is the only visual we can use */
    template.visualid =  XVisualIDFromVisual(visual);
    vis = XGetVisualInfo(gdi_display, VisualIDMask, &template, &num);

    ret = pglXCreateGLXPixmap(gdi_display, vis, physDev->bitmap->pixmap);
    XFree(vis);
    wine_tsx11_unlock(); 
    TRACE("return %lx\n", ret);
    return ret;
}

Drawable get_glxdrawable(X11DRV_PDEVICE *physDev)
{
    Drawable ret;

    if(physDev->bitmap)
    {
        if (physDev->bitmap->hbitmap == BITMAP_stock_phys_bitmap.hbitmap)
            ret = physDev->drawable; /* PBuffer */
        else
        {
            if(!physDev->bitmap->glxpixmap)
                physDev->bitmap->glxpixmap = create_glxpixmap(physDev);
            ret = physDev->bitmap->glxpixmap;
        }
    }
    else
        ret = physDev->drawable;
    return ret;
}

BOOL destroy_glxpixmap(XID glxpixmap)
{
    wine_tsx11_lock(); 
    pglXDestroyGLXPixmap(gdi_display, glxpixmap);
    wine_tsx11_unlock(); 
    return TRUE;
}

/**
 * X11DRV_SwapBuffers
 *
 * Swap the buffers of this DC
 */
BOOL X11DRV_SwapBuffers(X11DRV_PDEVICE *physDev)
{
  GLXDrawable drawable;
  if (!has_opengl()) {
    ERR("No libGL on this box - disabling OpenGL support !\n");
    return 0;
  }
  
  TRACE_(opengl)("(%p)\n", physDev);

  drawable = get_glxdrawable(physDev);
  wine_tsx11_lock();
  pglXSwapBuffers(gdi_display, drawable);
  wine_tsx11_unlock();

  /* FPS support */
  if (TRACE_ON(fps))
  {
      static long prev_time, frames;

      DWORD time = GetTickCount();
      frames++;
      /* every 1.5 seconds */
      if (time - prev_time > 1500) {
          TRACE_(fps)("@ approx %.2ffps\n", 1000.0*frames/(time - prev_time));
          prev_time = time;
          frames = 0;
      }
  }

  return TRUE;
}

/***********************************************************************
 *		X11DRV_setup_opengl_visual
 *
 * Setup the default visual used for OpenGL and Direct3D, and the desktop
 * window (if it exists).  If OpenGL isn't available, the visual is simply
 * set to the default visual for the display
 */
XVisualInfo *X11DRV_setup_opengl_visual( Display *display )
{
    XVisualInfo *visual = NULL;
    int i;

    /* In order to support OpenGL or D3D, we require a double-buffered visual and stencil buffer support,
     * D3D and some applications can make use of aux buffers.
     */
    int visualProperties[][11] = {
        { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_DEPTH_SIZE, 24, GLX_STENCIL_SIZE, 8, GLX_ALPHA_SIZE, 8, GLX_AUX_BUFFERS, 1, None },
        { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_DEPTH_SIZE, 24, GLX_STENCIL_SIZE, 8, GLX_ALPHA_SIZE, 8, None },
        { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_DEPTH_SIZE, 16, GLX_STENCIL_SIZE, 8, None },
        { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_DEPTH_SIZE, 16, None },
    };

    if (!has_opengl())
        return NULL;

    wine_tsx11_lock();
    for (i = 0; i < sizeof(visualProperties)/sizeof(visualProperties[0]); ++i) {
        visual = pglXChooseVisual(display, DefaultScreen(display), visualProperties[i]);
        if (visual)
            break;
    }
    wine_tsx11_unlock();

    if (visual)
        TRACE("Visual ID %lx Chosen\n", visual->visualid);
    else
        WARN("No suitable visual found\n");

    return visual;
}

#else  /* no OpenGL includes */

/***********************************************************************
 *		ChoosePixelFormat (X11DRV.@)
 */
int X11DRV_ChoosePixelFormat(X11DRV_PDEVICE *physDev,
			     const PIXELFORMATDESCRIPTOR *ppfd) {
  ERR("No OpenGL support compiled in.\n");

  return 0;
}

/***********************************************************************
 *		DescribePixelFormat (X11DRV.@)
 */
int X11DRV_DescribePixelFormat(X11DRV_PDEVICE *physDev,
			       int iPixelFormat,
			       UINT nBytes,
			       PIXELFORMATDESCRIPTOR *ppfd) {
  ERR("No OpenGL support compiled in.\n");

  return 0;
}

/***********************************************************************
 *		GetPixelFormat (X11DRV.@)
 */
int X11DRV_GetPixelFormat(X11DRV_PDEVICE *physDev) {
  ERR("No OpenGL support compiled in.\n");

  return 0;
}

/***********************************************************************
 *		SetPixelFormat (X11DRV.@)
 */
BOOL X11DRV_SetPixelFormat(X11DRV_PDEVICE *physDev,
			   int iPixelFormat,
			   const PIXELFORMATDESCRIPTOR *ppfd) {
  ERR("No OpenGL support compiled in.\n");

  return FALSE;
}

/***********************************************************************
 *		SwapBuffers (X11DRV.@)
 */
BOOL X11DRV_SwapBuffers(X11DRV_PDEVICE *physDev) {
  ERR_(opengl)("No OpenGL support compiled in.\n");

  return FALSE;
}

/**
 * X11DRV_wglCreateContext
 *
 * For OpenGL32 wglCreateContext.
 */
HGLRC X11DRV_wglCreateContext(X11DRV_PDEVICE *physDev) {
    ERR_(opengl)("No OpenGL support compiled in.\n");
    return NULL;
}

/**
 * X11DRV_wglDeleteContext
 *
 * For OpenGL32 wglDeleteContext.
 */
BOOL X11DRV_wglDeleteContext(HGLRC hglrc) {
    ERR_(opengl)("No OpenGL support compiled in.\n");
    return FALSE;
}

/**
 * X11DRV_wglGetProcAddress
 *
 * For OpenGL32 wglGetProcAddress.
 */
PROC X11DRV_wglGetProcAddress(LPCSTR lpszProc) {
    ERR_(opengl)("No OpenGL support compiled in.\n");
    return NULL;
}

HDC X11DRV_wglGetPbufferDCARB(X11DRV_PDEVICE *hDevice, void *hPbuffer)
{
    ERR_(opengl)("No OpenGL support compiled in.\n");
    return NULL;
}

BOOL X11DRV_wglMakeContextCurrentARB(X11DRV_PDEVICE* hDrawDev, X11DRV_PDEVICE* hReadDev, HGLRC hglrc) {
    ERR_(opengl)("No OpenGL support compiled in.\n");
    return FALSE;
}

/**
 * X11DRV_wglMakeCurrent
 *
 * For OpenGL32 wglMakeCurrent.
 */
BOOL X11DRV_wglMakeCurrent(X11DRV_PDEVICE *physDev, HGLRC hglrc) {
    ERR_(opengl)("No OpenGL support compiled in.\n");
    return FALSE;
}

/**
 * X11DRV_wglShareLists
 *
 * For OpenGL32 wglShaderLists.
 */
BOOL X11DRV_wglShareLists(HGLRC hglrc1, HGLRC hglrc2) {
    ERR_(opengl)("No OpenGL support compiled in.\n");
    return FALSE;
}

/**
 * X11DRV_wglUseFontBitmapsA
 *
 * For OpenGL32 wglUseFontBitmapsA.
 */
BOOL X11DRV_wglUseFontBitmapsA(X11DRV_PDEVICE *physDev, DWORD first, DWORD count, DWORD listBase)
{
    ERR_(opengl)("No OpenGL support compiled in.\n");
    return FALSE;
}

/**
 * X11DRV_wglUseFontBitmapsW
 *
 * For OpenGL32 wglUseFontBitmapsW.
 */
BOOL X11DRV_wglUseFontBitmapsW(X11DRV_PDEVICE *physDev, DWORD first, DWORD count, DWORD listBase)
{
    ERR_(opengl)("No OpenGL support compiled in.\n");
    return FALSE;
}

XVisualInfo *X11DRV_setup_opengl_visual( Display *display )
{
  return NULL;
}

Drawable get_glxdrawable(X11DRV_PDEVICE *physDev)
{
    return 0;
}

BOOL destroy_glxpixmap(XID glxpixmap)
{
    return FALSE;
}

#endif /* defined(HAVE_OPENGL) */
