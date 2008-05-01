/*
 * IWineD3DDevice implementation
 *
 * Copyright 2002 Lionel Ulmer
 * Copyright 2002-2005 Jason Edmeades
 * Copyright 2003-2004 Raphael Junqueira
 * Copyright 2004 Christian Costa
 * Copyright 2005 Oliver Stieber
 * Copyright 2006 Stefan D�singer for CodeWeavers
 * Copyright 2006 Henri Verbeet
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
#include <stdio.h>
#ifdef HAVE_FLOAT_H
# include <float.h>
#endif
#include "wined3d_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d);
WINE_DECLARE_DEBUG_CHANNEL(d3d_shader);
#define GLINFO_LOCATION ((IWineD3DImpl *)(This->wineD3D))->gl_info

/* Define the default light parameters as specified by MSDN */
const WINED3DLIGHT WINED3D_default_light = {

    WINED3DLIGHT_DIRECTIONAL, /* Type */
    { 1.0, 1.0, 1.0, 0.0 },   /* Diffuse r,g,b,a */
    { 0.0, 0.0, 0.0, 0.0 },   /* Specular r,g,b,a */
    { 0.0, 0.0, 0.0, 0.0 },   /* Ambient r,g,b,a, */
    { 0.0, 0.0, 0.0 },        /* Position x,y,z */
    { 0.0, 0.0, 1.0 },        /* Direction x,y,z */
    0.0,                      /* Range */
    0.0,                      /* Falloff */
    0.0, 0.0, 0.0,            /* Attenuation 0,1,2 */
    0.0,                      /* Theta */
    0.0                       /* Phi */
};

/* x11drv GDI escapes */
#define X11DRV_ESCAPE 6789
enum x11drv_escape_codes
{
    X11DRV_GET_DISPLAY,   /* get X11 display for a DC */
    X11DRV_GET_DRAWABLE,  /* get current drawable for a DC */
    X11DRV_GET_FONT,      /* get current X font for a DC */
};

/* retrieve the X display to use on a given DC */
inline static Display *get_display( HDC hdc )
{
    Display *display;
    enum x11drv_escape_codes escape = X11DRV_GET_DISPLAY;

    if (!ExtEscape( hdc, X11DRV_ESCAPE, sizeof(escape), (LPCSTR)&escape,
                    sizeof(display), (LPSTR)&display )) display = NULL;
    return display;
}

/* allocate one pbuffer per surface */
BOOL pbuffer_per_surface = FALSE;

/* static function declarations */
static void WINAPI IWineD3DDeviceImpl_AddResource(IWineD3DDevice *iface, IWineD3DResource *resource);

static void set_depth_stencil_fbo(IWineD3DDevice *iface, IWineD3DSurface *depth_stencil);

/* helper macros */
#define D3DMEMCHECK(object, ppResult) if(NULL == object) { *ppResult = NULL; WARN("Out of memory\n"); return WINED3DERR_OUTOFVIDEOMEMORY;}

#define D3DCREATEOBJECTINSTANCE(object, type) { \
    object=HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IWineD3D##type##Impl)); \
    D3DMEMCHECK(object, pp##type); \
    object->lpVtbl = &IWineD3D##type##_Vtbl;  \
    object->wineD3DDevice = This; \
    object->parent       = parent; \
    object->ref          = 1; \
    *pp##type = (IWineD3D##type *) object; \
}

#define D3DCREATESHADEROBJECTINSTANCE(object, type) { \
    object=HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IWineD3D##type##Impl)); \
    D3DMEMCHECK(object, pp##type); \
    object->lpVtbl = &IWineD3D##type##_Vtbl;  \
    object->parent       = parent; \
    object->ref          = 1; \
    object->baseShader.device = (IWineD3DDevice*) This; \
    *pp##type = (IWineD3D##type *) object; \
}

#define  D3DCREATERESOURCEOBJECTINSTANCE(object, type, d3dtype, _size){ \
    object=HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IWineD3D##type##Impl)); \
    D3DMEMCHECK(object, pp##type); \
    object->lpVtbl = &IWineD3D##type##_Vtbl;  \
    object->resource.wineD3DDevice   = This; \
    object->resource.parent          = parent; \
    object->resource.resourceType    = d3dtype; \
    object->resource.ref             = 1; \
    object->resource.pool            = Pool; \
    object->resource.format          = Format; \
    object->resource.usage           = Usage; \
    object->resource.size            = _size; \
    /* Check that we have enough video ram left */ \
    if (Pool == WINED3DPOOL_DEFAULT) { \
        if (IWineD3DDevice_GetAvailableTextureMem(iface) <= _size) { \
            WARN("Out of 'bogus' video memory\n"); \
            HeapFree(GetProcessHeap(), 0, object); \
            *pp##type = NULL; \
            return WINED3DERR_OUTOFVIDEOMEMORY; \
        } \
        globalChangeGlRam(_size); \
    } \
    object->resource.allocatedMemory = (0 == _size ? NULL : Pool == WINED3DPOOL_DEFAULT ? NULL : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, _size)); \
    if (object->resource.allocatedMemory == NULL && _size != 0 && Pool != WINED3DPOOL_DEFAULT) { \
        FIXME("Out of memory!\n"); \
        HeapFree(GetProcessHeap(), 0, object); \
        *pp##type = NULL; \
        return WINED3DERR_OUTOFVIDEOMEMORY; \
    } \
    *pp##type = (IWineD3D##type *) object; \
    IWineD3DDeviceImpl_AddResource(iface, (IWineD3DResource *)object) ;\
    TRACE("(%p) : Created resource %p\n", This, object); \
}

#define D3DINITIALIZEBASETEXTURE(_basetexture) { \
    _basetexture.levels     = Levels; \
    _basetexture.filterType = (Usage & WINED3DUSAGE_AUTOGENMIPMAP) ? WINED3DTEXF_LINEAR : WINED3DTEXF_NONE; \
    _basetexture.LOD        = 0; \
    _basetexture.dirty      = TRUE; \
}

/**********************************************************
 * Global variable / Constants follow
 **********************************************************/
const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};  /* When needed for comparisons */

/**********************************************************
 * Utility functions follow
 **********************************************************/
/* Convert the WINED3DLIGHT properties into equivalent gl lights */
static void setup_light(IWineD3DDevice *iface, LONG Index, PLIGHTINFOEL *lightInfo) {

    float quad_att;
    float colRGBA[] = {0.0, 0.0, 0.0, 0.0};
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    /* Light settings are affected by the model view in OpenGL, the View transform in direct3d*/
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadMatrixf((float *)&This->stateBlock->transforms[WINED3DTS_VIEW].u.m[0][0]);

    /* Diffuse: */
    colRGBA[0] = lightInfo->OriginalParms.Diffuse.r;
    colRGBA[1] = lightInfo->OriginalParms.Diffuse.g;
    colRGBA[2] = lightInfo->OriginalParms.Diffuse.b;
    colRGBA[3] = lightInfo->OriginalParms.Diffuse.a;
    glLightfv(GL_LIGHT0+Index, GL_DIFFUSE, colRGBA);
    checkGLcall("glLightfv");

    /* Specular */
    colRGBA[0] = lightInfo->OriginalParms.Specular.r;
    colRGBA[1] = lightInfo->OriginalParms.Specular.g;
    colRGBA[2] = lightInfo->OriginalParms.Specular.b;
    colRGBA[3] = lightInfo->OriginalParms.Specular.a;
    glLightfv(GL_LIGHT0+Index, GL_SPECULAR, colRGBA);
    checkGLcall("glLightfv");

    /* Ambient */
    colRGBA[0] = lightInfo->OriginalParms.Ambient.r;
    colRGBA[1] = lightInfo->OriginalParms.Ambient.g;
    colRGBA[2] = lightInfo->OriginalParms.Ambient.b;
    colRGBA[3] = lightInfo->OriginalParms.Ambient.a;
    glLightfv(GL_LIGHT0+Index, GL_AMBIENT, colRGBA);
    checkGLcall("glLightfv");

    /* Attenuation - Are these right? guessing... */
    glLightf(GL_LIGHT0+Index, GL_CONSTANT_ATTENUATION,  lightInfo->OriginalParms.Attenuation0);
    checkGLcall("glLightf");
    glLightf(GL_LIGHT0+Index, GL_LINEAR_ATTENUATION,    lightInfo->OriginalParms.Attenuation1);
    checkGLcall("glLightf");

    if ((lightInfo->OriginalParms.Range *lightInfo->OriginalParms.Range) >= FLT_MIN) {
        quad_att = 1.4/(lightInfo->OriginalParms.Range *lightInfo->OriginalParms.Range);
    } else {
        quad_att = 0; /*  0 or  MAX?  (0 seems to be ok) */
    }

    if (quad_att < lightInfo->OriginalParms.Attenuation2) quad_att = lightInfo->OriginalParms.Attenuation2;
    glLightf(GL_LIGHT0+Index, GL_QUADRATIC_ATTENUATION, quad_att);
    checkGLcall("glLightf");

    switch (lightInfo->OriginalParms.Type) {
    case WINED3DLIGHT_POINT:
        /* Position */
        glLightfv(GL_LIGHT0+Index, GL_POSITION, &lightInfo->lightPosn[0]);
        checkGLcall("glLightfv");
        glLightf(GL_LIGHT0 + Index, GL_SPOT_CUTOFF, lightInfo->cutoff);
        checkGLcall("glLightf");
        /* FIXME: Range */
        break;

    case WINED3DLIGHT_SPOT:
        /* Position */
        glLightfv(GL_LIGHT0+Index, GL_POSITION, &lightInfo->lightPosn[0]);
        checkGLcall("glLightfv");
        /* Direction */
        glLightfv(GL_LIGHT0+Index, GL_SPOT_DIRECTION, &lightInfo->lightDirn[0]);
        checkGLcall("glLightfv");
        glLightf(GL_LIGHT0 + Index, GL_SPOT_EXPONENT, lightInfo->exponent);
        checkGLcall("glLightf");
        glLightf(GL_LIGHT0 + Index, GL_SPOT_CUTOFF, lightInfo->cutoff);
        checkGLcall("glLightf");
        /* FIXME: Range */
        break;

    case WINED3DLIGHT_DIRECTIONAL:
        /* Direction */
        glLightfv(GL_LIGHT0+Index, GL_POSITION, &lightInfo->lightPosn[0]); /* Note gl uses w position of 0 for direction! */
        checkGLcall("glLightfv");
        glLightf(GL_LIGHT0+Index, GL_SPOT_CUTOFF, lightInfo->cutoff);
        checkGLcall("glLightf");
        glLightf(GL_LIGHT0+Index, GL_SPOT_EXPONENT, 0.0f);
        checkGLcall("glLightf");
        break;

    default:
        FIXME("Unrecognized light type %d\n", lightInfo->OriginalParms.Type);
    }

    /* Restore the modelview matrix */
    glPopMatrix();
}

/**********************************************************
 * GLSL helper functions follow
 **********************************************************/

/** Detach the GLSL pixel or vertex shader object from the shader program */
static void detach_glsl_shader(IWineD3DDevice *iface, GLhandleARB shaderObj, GLhandleARB programId) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    if (shaderObj != 0 && programId != 0) {
        TRACE_(d3d_shader)("Detaching GLSL shader object %u from program %u\n", shaderObj, programId);
        GL_EXTCALL(glDetachObjectARB(programId, shaderObj));
        checkGLcall("glDetachObjectARB");
    }
}

/** Delete a GLSL shader program */
static void delete_glsl_shader_program(IWineD3DDevice *iface, GLhandleARB obj) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    
    if (obj != 0) {
        TRACE_(d3d_shader)("Deleting GLSL shader program %u\n", obj);
        GL_EXTCALL(glDeleteObjectARB(obj));
        checkGLcall("glDeleteObjectARB");
    }
}

/** Delete the list of linked programs this shader is associated with.
 * Also at this point, check to see if there are any objects left attached
 * to each GLSL program.  If not, delete the GLSL program object.
 * This will be run when a device is released. */
static void delete_glsl_shader_list(IWineD3DDevice* iface) {
    
    struct list *ptr                       = NULL;
    struct glsl_shader_prog_link *curLink  = NULL;
    IWineD3DDeviceImpl *This               = (IWineD3DDeviceImpl *)iface;
    
    int numAttached = 0;
    int i;
    GLhandleARB objList[2];   /* There should never be more than 2 objects attached 
                                 (one pixel shader and one vertex shader at most) */
    
    ptr = list_head( &This->glsl_shader_progs );
    while (ptr) {
        /* First, get the current item,
         * save the link to the next pointer, 
         * detach and delete shader objects,
         * then de-allocate the list item's memory */
        curLink = LIST_ENTRY( ptr, struct glsl_shader_prog_link, entry );
        ptr = list_next( &This->glsl_shader_progs, ptr );

        /* See if this object is still attached to the program - it may have been detached already */
        GL_EXTCALL(glGetAttachedObjectsARB(curLink->programId, 2, &numAttached, objList));
        TRACE_(d3d_shader)("%i GLSL objects are currently attached to program %u\n", numAttached, curLink->programId);
        for (i = 0; i < numAttached; i++) {
            detach_glsl_shader(iface, objList[i], curLink->programId);
        }
        
        delete_glsl_shader_program(iface, curLink->programId);

        /* Free the uniform locations */
        HeapFree(GetProcessHeap(), 0, curLink->vuniformF_locations);
        HeapFree(GetProcessHeap(), 0, curLink->puniformF_locations);

        /* Free the memory for this list item */    
        HeapFree(GetProcessHeap(), 0, curLink);
    }
}

/**********************************************************
 * IUnknown parts follows
 **********************************************************/

static HRESULT WINAPI IWineD3DDeviceImpl_QueryInterface(IWineD3DDevice *iface,REFIID riid,LPVOID *ppobj)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    TRACE("(%p)->(%s,%p)\n",This,debugstr_guid(riid),ppobj);
    if (IsEqualGUID(riid, &IID_IUnknown)
        || IsEqualGUID(riid, &IID_IWineD3DBase)
        || IsEqualGUID(riid, &IID_IWineD3DDevice)) {
        IUnknown_AddRef(iface);
        *ppobj = This;
        return S_OK;
    }
    *ppobj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI IWineD3DDeviceImpl_AddRef(IWineD3DDevice *iface) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    ULONG refCount = InterlockedIncrement(&This->ref);

    TRACE("(%p) : AddRef increasing from %d\n", This, refCount - 1);
    return refCount;
}

static ULONG WINAPI IWineD3DDeviceImpl_Release(IWineD3DDevice *iface) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    ULONG refCount = InterlockedDecrement(&This->ref);

    TRACE("(%p) : Releasing from %d\n", This, refCount + 1);

    if (!refCount) {
        if (This->fbo) {
            GL_EXTCALL(glDeleteFramebuffersEXT(1, &This->fbo));
        }

        HeapFree(GetProcessHeap(), 0, This->render_targets);

        HeapFree(GetProcessHeap(), 0, This->draw_buffers);

        /* TODO: Clean up all the surfaces and textures! */
        /* NOTE: You must release the parent if the object was created via a callback
        ** ***************************/

        /* Delete any GLSL shader programs that may exist */
        if (This->vs_selected_mode == SHADER_GLSL ||
            This->ps_selected_mode == SHADER_GLSL)
            delete_glsl_shader_list(iface);
    
        /* Release the update stateblock */
        if(IWineD3DStateBlock_Release((IWineD3DStateBlock *)This->updateStateBlock) > 0){
            if(This->updateStateBlock != This->stateBlock)
                FIXME("(%p) Something's still holding the Update stateblock\n",This);
        }
        This->updateStateBlock = NULL;
        { /* because were not doing proper internal refcounts releasing the primary state block
            causes recursion with the extra checks in ResourceReleased, to avoid this we have
            to set this->stateBlock = NULL; first */
            IWineD3DStateBlock *stateBlock = (IWineD3DStateBlock *)This->stateBlock;
            This->stateBlock = NULL;

            /* Release the stateblock */
            if(IWineD3DStateBlock_Release(stateBlock) > 0){
                    FIXME("(%p) Something's still holding the Update stateblock\n",This);
            }
        }

        if (This->resources != NULL ) {
            FIXME("(%p) Device released with resources still bound, acceptable but unexpected\n", This);
            dumpResources(This->resources);
        }


        IWineD3D_Release(This->wineD3D);
        This->wineD3D = NULL;
        HeapFree(GetProcessHeap(), 0, This);
        TRACE("Freed device  %p\n", This);
        This = NULL;
    }
    return refCount;
}

/**********************************************************
 * IWineD3DDevice implementation follows
 **********************************************************/
static HRESULT WINAPI IWineD3DDeviceImpl_GetParent(IWineD3DDevice *iface, IUnknown **pParent) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    *pParent = This->parent;
    IUnknown_AddRef(This->parent);
    return WINED3D_OK;
}

static void CreateVBO(IWineD3DVertexBufferImpl *object) {
    IWineD3DDeviceImpl *This = object->resource.wineD3DDevice;  /* Needed for GL_EXTCALL */
    GLenum error, glUsage;
    DWORD vboUsage = object->resource.usage;
    if(object->Flags & VBFLAG_VBOCREATEFAIL) {
        WARN("Creating a vbo failed once, not trying again\n");
        return;
    }

    TRACE("Creating an OpenGL vertex buffer object for IWineD3DVertexBuffer %p  Usage(%s)\n", object, debug_d3dusage(vboUsage));

    ENTER_GL();
    /* Make sure that the gl error is cleared. Do not use checkGLcall
      * here because checkGLcall just prints a fixme and continues. However,
      * if an error during VBO creation occurs we can fall back to non-vbo operation
      * with full functionality(but performance loss)
      */
    while(glGetError() != GL_NO_ERROR);

    /* Basically the FVF parameter passed to CreateVertexBuffer is no good
      * It is the FVF set with IWineD3DDevice::SetFVF or the Vertex Declaration set with
      * IWineD3DDevice::SetVertexDeclaration that decides how the vertices in the buffer
      * look like. This means that on each DrawPrimitive call the vertex buffer has to be verified
      * to check if the rhw and color values are in the correct format.
      */

    GL_EXTCALL(glGenBuffersARB(1, &object->vbo));
    error = glGetError();
    if(object->vbo == 0 || error != GL_NO_ERROR) {
        WARN("Failed to create a VBO with error %d\n", error);
        goto error;
    }

    GL_EXTCALL(glBindBufferARB(GL_ARRAY_BUFFER_ARB, object->vbo));
    error = glGetError();
    if(error != GL_NO_ERROR) {
        WARN("Failed to bind the VBO, error %d\n", error);
        goto error;
    }

    /* Don't use static, because dx apps tend to update the buffer
      * quite often even if they specify 0 usage
      */
    switch(vboUsage & (D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC) ) {
        case D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC:
            TRACE("Gl usage = GL_STREAM_DRAW\n");
            glUsage = GL_STREAM_DRAW_ARB;
            break;
        case D3DUSAGE_WRITEONLY:
            TRACE("Gl usage = GL_DYNAMIC_DRAW\n");
            glUsage = GL_DYNAMIC_DRAW_ARB;
            break;
        case D3DUSAGE_DYNAMIC:
            TRACE("Gl usage = GL_STREAM_COPY\n");
            glUsage = GL_STREAM_COPY_ARB;
            break;
        default:
            TRACE("Gl usage = GL_DYNAMIC_COPY\n");
            glUsage = GL_DYNAMIC_COPY_ARB;
            break;
    }

    /* Reserve memory for the buffer. The amount of data won't change
      * so we are safe with calling glBufferData once with a NULL ptr and
      * calling glBufferSubData on updates
      */
    GL_EXTCALL(glBufferDataARB(GL_ARRAY_BUFFER_ARB, object->resource.size, NULL, glUsage));
    error = glGetError();
    if(error != GL_NO_ERROR) {
        WARN("glBufferDataARB failed with error %d\n", error);
        goto error;
    }

    LEAVE_GL();

    return;
    error:
    /* Clean up all vbo init, but continue because we can work without a vbo :-) */
    FIXME("Failed to create a vertex buffer object. Continuing, but performance issues can occur\n");
    if(object->vbo) GL_EXTCALL(glDeleteBuffersARB(1, &object->vbo));
    object->vbo = 0;
    object->Flags |= VBFLAG_VBOCREATEFAIL;
    LEAVE_GL();
    return;
}

static HRESULT WINAPI IWineD3DDeviceImpl_CreateVertexBuffer(IWineD3DDevice *iface, UINT Size, DWORD Usage, 
                             DWORD FVF, WINED3DPOOL Pool, IWineD3DVertexBuffer** ppVertexBuffer, HANDLE *sharedHandle,
                             IUnknown *parent) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DVertexBufferImpl *object;
    WINED3DFORMAT Format = WINED3DFMT_VERTEXDATA; /* Dummy format for now */
    int dxVersion = ( (IWineD3DImpl *) This->wineD3D)->dxVersion;
    BOOL conv;
    D3DCREATERESOURCEOBJECTINSTANCE(object, VertexBuffer, WINED3DRTYPE_VERTEXBUFFER, Size)

    TRACE("(%p) : Size=%d, Usage=%d, FVF=%x, Pool=%d - Memory@%p, Iface@%p\n", This, Size, Usage, FVF, Pool, object->resource.allocatedMemory, object);
    *ppVertexBuffer = (IWineD3DVertexBuffer *)object;

    if(Size == 0) return WINED3DERR_INVALIDCALL;

    if (Pool == WINED3DPOOL_DEFAULT ) { /* Allocate some system memory for now */
        object->resource.allocatedMemory  = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, object->resource.size);
    }
    object->fvf = FVF;

    /* Observations show that drawStridedSlow is faster on dynamic VBs than converting +
     * drawStridedFast (half-life 2).
     *
     * Basically converting the vertices in the buffer is quite expensive, and observations
     * show that drawStridedSlow is faster than converting + uploading + drawStridedFast.
     * Therefore do not create a VBO for WINED3DUSAGE_DYNAMIC buffers.
     *
     * Direct3D7 has another problem: Its vertexbuffer api doesn't offer a way to specify
     * the range of vertices being locked, so each lock will require the whole buffer to be transformed.
     * Moreover geometry data in dx7 is quite simple, so drawStridedSlow isn't a big hit. A plus
     * is that the vertex buffers fvf can be trusted in dx7. So only create non-converted vbos for
     * dx7 apps.
     * There is a IDirect3DVertexBuffer7::Optimize call after which the buffer can't be locked any
     * more. In this call we can convert dx7 buffers too.
     */
    conv = ((FVF & WINED3DFVF_POSITION_MASK) == WINED3DFVF_XYZRHW ) || (FVF & (WINED3DFVF_DIFFUSE | WINED3DFVF_SPECULAR));
    if( GL_SUPPORT(ARB_VERTEX_BUFFER_OBJECT) && Pool != WINED3DPOOL_SYSTEMMEM && !(Usage & WINED3DUSAGE_DYNAMIC) && 
        (dxVersion > 7 || !conv) ) {
        CreateVBO(object);

        /* DX7 buffers can be locked directly into the VBO (no conversion, see above */
        if(dxVersion == 7 && object->vbo) {
            HeapFree(GetProcessHeap(), 0, object->resource.allocatedMemory);
            object->resource.allocatedMemory = NULL;
        }

    }
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_CreateIndexBuffer(IWineD3DDevice *iface, UINT Length, DWORD Usage, 
                                                    WINED3DFORMAT Format, WINED3DPOOL Pool, IWineD3DIndexBuffer** ppIndexBuffer,
                                                    HANDLE *sharedHandle, IUnknown *parent) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DIndexBufferImpl *object;
    TRACE("(%p) Creating index buffer\n", This);
    
    /* Allocate the storage for the device */
    D3DCREATERESOURCEOBJECTINSTANCE(object,IndexBuffer,WINED3DRTYPE_INDEXBUFFER, Length)
    
    /*TODO: use VBO's */
    if (Pool == WINED3DPOOL_DEFAULT ) { /* Allocate some system memory for now */
        object->resource.allocatedMemory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,object->resource.size);
    }

    TRACE("(%p) : Len=%d, Use=%x, Format=(%u,%s), Pool=%d - Memory@%p, Iface@%p\n", This, Length, Usage, Format, 
                           debug_d3dformat(Format), Pool, object, object->resource.allocatedMemory);
    *ppIndexBuffer = (IWineD3DIndexBuffer *) object;

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_CreateStateBlock(IWineD3DDevice* iface, WINED3DSTATEBLOCKTYPE Type, IWineD3DStateBlock** ppStateBlock, IUnknown *parent) {

    IWineD3DDeviceImpl     *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DStateBlockImpl *object;
    int i, j;
    HRESULT temp_result;

    D3DCREATEOBJECTINSTANCE(object, StateBlock)
    object->blockType     = Type;

    /* Special case - Used during initialization to produce a placeholder stateblock
          so other functions called can update a state block                         */
    if (Type == WINED3DSBT_INIT) {
        /* Don't bother increasing the reference count otherwise a device will never
           be freed due to circular dependencies                                   */
        return WINED3D_OK;
    }
    
    temp_result = allocate_shader_constants(object);
    if (WINED3D_OK != temp_result)
        return temp_result;

    /* Otherwise, might as well set the whole state block to the appropriate values  */
    if (This->stateBlock != NULL)
        stateblock_copy((IWineD3DStateBlock*) object, (IWineD3DStateBlock*) This->stateBlock);
    else
        memset(object->streamFreq, 1, sizeof(object->streamFreq));

    /* Reset the ref and type after kludging it */
    object->wineD3DDevice = This;
    object->ref           = 1;
    object->blockType     = Type;

    TRACE("Updating changed flags appropriate for type %d\n", Type);

    if (Type == WINED3DSBT_ALL) {

        TRACE("ALL => Pretend everything has changed\n");
        stateblock_savedstates_set((IWineD3DStateBlock*) object, &object->changed, TRUE);
    
    } else if (Type == WINED3DSBT_PIXELSTATE) {

        TRACE("PIXELSTATE => Pretend all pixel shates have changed\n");
        stateblock_savedstates_set((IWineD3DStateBlock*) object, &object->changed, FALSE);

        object->changed.pixelShader = TRUE;

        /* Pixel Shader Constants */
        for (i = 0; i < GL_LIMITS(pshader_constantsF); ++i)
            object->changed.pixelShaderConstantsF[i] = TRUE;
        for (i = 0; i < MAX_CONST_B; ++i)
            object->changed.pixelShaderConstantsB[i] = TRUE;
        for (i = 0; i < MAX_CONST_I; ++i)
            object->changed.pixelShaderConstantsI[i] = TRUE;
        
        for (i = 0; i < NUM_SAVEDPIXELSTATES_R; i++) {
            object->changed.renderState[SavedPixelStates_R[i]] = TRUE;
        }
        for (j = 0; j < GL_LIMITS(texture_stages); j++) {
            for (i = 0; i < NUM_SAVEDPIXELSTATES_T; i++) {
                object->changed.textureState[j][SavedPixelStates_T[i]] = TRUE;
            }
        }
        for (j = 0 ; j < 16; j++) {
            for (i =0; i < NUM_SAVEDPIXELSTATES_S;i++) {

                object->changed.samplerState[j][SavedPixelStates_S[i]] = TRUE;
            }
        }

    } else if (Type == WINED3DSBT_VERTEXSTATE) {

        TRACE("VERTEXSTATE => Pretend all vertex shates have changed\n");
        stateblock_savedstates_set((IWineD3DStateBlock*) object, &object->changed, FALSE);

        object->changed.vertexShader = TRUE;

        /* Vertex Shader Constants */
        for (i = 0; i < GL_LIMITS(vshader_constantsF); ++i)
            object->changed.vertexShaderConstantsF[i] = TRUE;
        for (i = 0; i < MAX_CONST_B; ++i)
            object->changed.vertexShaderConstantsB[i] = TRUE;
        for (i = 0; i < MAX_CONST_I; ++i)
            object->changed.vertexShaderConstantsI[i] = TRUE;
 
        for (i = 0; i < NUM_SAVEDVERTEXSTATES_R; i++) {
            object->changed.renderState[SavedVertexStates_R[i]] = TRUE;
        }
        for (j = 0; j < GL_LIMITS(texture_stages); j++) {
            for (i = 0; i < NUM_SAVEDVERTEXSTATES_T; i++) {
                object->changed.textureState[j][SavedVertexStates_T[i]] = TRUE;
            }
        }
        for (j = 0 ; j < 16; j++){
            for (i =0; i < NUM_SAVEDVERTEXSTATES_S;i++) {
                object->changed.samplerState[j][SavedVertexStates_S[i]] = TRUE;
            }
        }

    /* Duplicate light chain */
    {
        PLIGHTINFOEL *src = NULL;
        PLIGHTINFOEL *dst = NULL;
        PLIGHTINFOEL *newEl = NULL;
        src = This->stateBlock->lights;
        object->lights = NULL;


        while (src) {
            newEl = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(PLIGHTINFOEL));
            if (newEl == NULL) return WINED3DERR_OUTOFVIDEOMEMORY;
            memcpy(newEl, src, sizeof(PLIGHTINFOEL));
            newEl->prev = dst;
            newEl->changed = TRUE;
            newEl->enabledChanged = TRUE;
            if (dst == NULL) {
                object->lights = newEl;
            } else {
                dst->next = newEl;
            }
            dst = newEl;
            src = src->next;
        }

     }

    } else {
        FIXME("Unrecognized state block type %d\n", Type);
    }

    TRACE("(%p) returning token (ptr to stateblock) of %p\n", This, object);
    return WINED3D_OK;
}


/* ************************************
MSDN:
[in] Render targets are not lockable unless the application specifies TRUE for Lockable. Note that lockable render targets reduce performance on some graphics hardware.

Discard
 [in] Set this flag to TRUE to enable z-buffer discarding, and FALSE otherwise. 

If this flag is set, the contents of the depth stencil buffer will be invalid after calling either IDirect3DDevice9::Present or IDirect3DDevice9::SetDepthStencilSurface with a different depth surface.

******************************** */
 
static HRESULT  WINAPI IWineD3DDeviceImpl_CreateSurface(IWineD3DDevice *iface, UINT Width, UINT Height, WINED3DFORMAT Format, BOOL Lockable, BOOL Discard, UINT Level, IWineD3DSurface **ppSurface,WINED3DRESOURCETYPE Type, DWORD Usage, WINED3DPOOL Pool, WINED3DMULTISAMPLE_TYPE MultiSample ,DWORD MultisampleQuality, HANDLE* pSharedHandle, WINED3DSURFTYPE Impl, IUnknown *parent) {
    IWineD3DDeviceImpl  *This = (IWineD3DDeviceImpl *)iface;    
    IWineD3DSurfaceImpl *object; /*NOTE: impl ref allowed since this is a create function */
    unsigned int pow2Width, pow2Height;
    unsigned int Size       = 1;
    const PixelFormatDesc *tableEntry = getFormatDescEntry(Format);
    TRACE("(%p) Create surface\n",This);
    
    /** FIXME: Check ranges on the inputs are valid 
     * MSDN
     *   MultisampleQuality
     *    [in] Quality level. The valid range is between zero and one less than the level
     *    returned by pQualityLevels used by IDirect3D9::CheckDeviceMultiSampleType. 
     *    Passing a larger value returns the error WINED3DERR_INVALIDCALL. The MultisampleQuality
     *    values of paired render targets, depth stencil surfaces, and the MultiSample type
     *    must all match.
      *******************************/


    /**
    * TODO: Discard MSDN
    * [in] Set this flag to TRUE to enable z-buffer discarding, and FALSE otherwise.
    *
    * If this flag is set, the contents of the depth stencil buffer will be
    * invalid after calling either IDirect3DDevice9::Present or  * IDirect3DDevice9::SetDepthStencilSurface
    * with a different depth surface.
    *
    *This flag has the same behavior as the constant, D3DPRESENTFLAG_DISCARD_DEPTHSTENCIL, in D3DPRESENTFLAG.
    ***************************/

    if(MultisampleQuality < 0) {
        FIXME("Invalid multisample level %d\n", MultisampleQuality);
        return WINED3DERR_INVALIDCALL; /* TODO: Check that this is the case! */
    }

    if(MultisampleQuality > 0) {
        FIXME("MultisampleQuality set to %d, substituting 0\n", MultisampleQuality);
        MultisampleQuality=0;
    }

    /** FIXME: Check that the format is supported
    *    by the device.
      *******************************/

    /* Non-power2 support */
    if (wined3d_settings.nonpower2_mode == NP2_NATIVE) {
        pow2Width = Width;
        pow2Height = Height;
    } else {
        /* Find the nearest pow2 match */
        pow2Width = pow2Height = 1;
        while (pow2Width < Width) pow2Width <<= 1;
        while (pow2Height < Height) pow2Height <<= 1;
    }

    if (pow2Width > Width || pow2Height > Height) {
         /** TODO: add support for non power two compressed textures (OpenGL 2 provices support for * non-power-two textures gratis) **/
        if (Format == WINED3DFMT_DXT1 || Format == WINED3DFMT_DXT2 || Format == WINED3DFMT_DXT3
               || Format == WINED3DFMT_DXT4 || Format == WINED3DFMT_DXT5) {
            FIXME("(%p) Compressed non-power-two textures are not supported w(%d) h(%d)\n",
                    This, Width, Height);
            return WINED3DERR_NOTAVAILABLE;
        }
    }

    /** DXTn mipmaps use the same number of 'levels' down to eg. 8x1, but since
     *  it is based around 4x4 pixel blocks it requires padding, so allocate enough
     *  space!
      *********************************/
    if (WINED3DFMT_UNKNOWN == Format) {
        Size = 0;
    } else if (Format == WINED3DFMT_DXT1) {
        /* DXT1 is half byte per pixel */
       Size = ((max(pow2Width,4) * tableEntry->bpp) * max(pow2Height,4)) >> 1;

    } else if (Format == WINED3DFMT_DXT2 || Format == WINED3DFMT_DXT3 ||
               Format == WINED3DFMT_DXT4 || Format == WINED3DFMT_DXT5) {
       Size = ((max(pow2Width,4) * tableEntry->bpp) * max(pow2Height,4));
    } else {
       /* The pitch is a multiple of 4 bytes */
       Size = ((pow2Width * tableEntry->bpp) + SURFACE_ALIGNMENT - 1) & ~(SURFACE_ALIGNMENT - 1);
       Size *= pow2Height;
    }

    /** Create and initialise the surface resource **/
    D3DCREATERESOURCEOBJECTINSTANCE(object,Surface,WINED3DRTYPE_SURFACE, Size)
    /* "Standalone" surface */
    IWineD3DSurface_SetContainer((IWineD3DSurface *)object, NULL);

    object->currentDesc.Width      = Width;
    object->currentDesc.Height     = Height;
    object->currentDesc.MultiSampleType    = MultiSample;
    object->currentDesc.MultiSampleQuality = MultisampleQuality;

    /* Setup some glformat defaults */
    object->glDescription.glFormat         = tableEntry->glFormat;
    object->glDescription.glFormatInternal = tableEntry->glInternal;
    object->glDescription.glType           = tableEntry->glType;

    object->glDescription.textureName      = 0;
    object->glDescription.level            = Level;
    object->glDescription.target           = GL_TEXTURE_2D;

    /* Internal data */
    object->pow2Width  = pow2Width;
    object->pow2Height = pow2Height;

    /* Flags */
    object->Flags      = 0; /* We start without flags set */
    object->Flags     |= (pow2Width != Width || pow2Height != Height) ? SFLAG_NONPOW2 : 0;
    object->Flags     |= Discard ? SFLAG_DISCARD : 0;
    object->Flags     |= (WINED3DFMT_D16_LOCKABLE == Format) ? SFLAG_LOCKABLE : 0;
    object->Flags     |= Lockable ? SFLAG_LOCKABLE : 0;


    if (WINED3DFMT_UNKNOWN != Format) {
        object->bytesPerPixel = tableEntry->bpp;
        object->pow2Size = ((pow2Width * object->bytesPerPixel) + SURFACE_ALIGNMENT - 1) & ~(SURFACE_ALIGNMENT - 1);
        object->pow2Size *= pow2Height;
    } else {
        object->bytesPerPixel = 0;
        object->pow2Size      = 0;
    }

    /** TODO: change this into a texture transform matrix so that it's processed in hardware **/

    TRACE("Pool %d %d %d %d\n",Pool, WINED3DPOOL_DEFAULT, WINED3DPOOL_MANAGED, WINED3DPOOL_SYSTEMMEM);

    /** Quick lockable sanity check TODO: remove this after surfaces, usage and lockability have been debugged properly
    * this function is too deep to need to care about things like this.
    * Levels need to be checked too, and possibly Type since they all affect what can be done.
    * ****************************************/
    switch(Pool) {
    case WINED3DPOOL_SCRATCH:
        if(!Lockable)
            FIXME("Create surface called with a pool of SCRATCH and a Lockable of FALSE "
                "which are mutually exclusive, setting lockable to TRUE\n");
                Lockable = TRUE;
    break;
    case WINED3DPOOL_SYSTEMMEM:
        if(!Lockable) FIXME("Create surface called with a pool of SYSTEMMEM and a Lockable of FALSE, "
                                    "this is acceptable but unexpected (I can't know how the surface can be usable!)\n");
    case WINED3DPOOL_MANAGED:
        if(Usage == WINED3DUSAGE_DYNAMIC) FIXME("Create surface called with a pool of MANAGED and a "
                                                "Usage of DYNAMIC which are mutually exclusive, not doing "
                                                "anything just telling you.\n");
    break;
    case WINED3DPOOL_DEFAULT: /*TODO: Create offscreen plain can cause this check to fail..., find out if it should */
        if(!(Usage & WINED3DUSAGE_DYNAMIC) && !(Usage & WINED3DUSAGE_RENDERTARGET)
           && !(Usage && WINED3DUSAGE_DEPTHSTENCIL ) && Lockable)
            WARN("Creating a surface with a POOL of DEFAULT with Lockable true, that doesn't specify DYNAMIC usage.\n");
    break;
    default:
        FIXME("(%p) Unknown pool %d\n", This, Pool);
    break;
    };

    if (Usage & WINED3DUSAGE_RENDERTARGET && Pool != WINED3DPOOL_DEFAULT) {
        FIXME("Trying to create a render target that isn't in the default pool\n");
    }

    /* mark the texture as dirty so that it gets loaded first time around*/
    IWineD3DSurface_AddDirtyRect(*ppSurface, NULL);
    TRACE("(%p) : w(%d) h(%d) fmt(%d,%s) lockable(%d) surf@%p, surfmem@%p, %d bytes\n",
           This, Width, Height, Format, debug_d3dformat(Format),
           (WINED3DFMT_D16_LOCKABLE == Format), *ppSurface, object->resource.allocatedMemory, object->resource.size);

    /* Store the DirectDraw primary surface. This is the first rendertarget surface created */
    if( (Usage & WINED3DUSAGE_RENDERTARGET) && (!This->ddraw_primary) )
        This->ddraw_primary = (IWineD3DSurface *) object;

    /* Look at the implementation and set the correct Vtable */
    switch(Impl) {
        case SURFACE_OPENGL:
            /* Nothing to do, it's set already */
            break;

        case SURFACE_GDI:
            object->lpVtbl = &IWineGDISurface_Vtbl;
            break;

        default:
            /* To be sure to catch this */
            ERR("Unknown requested surface implementation %d!\n", Impl);
            IWineD3DSurface_Release((IWineD3DSurface *) object);
            return WINED3DERR_INVALIDCALL;
    }

    /* Call the private setup routine */
    return IWineD3DSurface_PrivateSetup( (IWineD3DSurface *) object );

}

static HRESULT  WINAPI IWineD3DDeviceImpl_CreateTexture(IWineD3DDevice *iface, UINT Width, UINT Height, UINT Levels,
                                                 DWORD Usage, WINED3DFORMAT Format, WINED3DPOOL Pool,
                                                 IWineD3DTexture** ppTexture, HANDLE* pSharedHandle, IUnknown *parent,
                                                 D3DCB_CREATESURFACEFN D3DCB_CreateSurface) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DTextureImpl *object;
    unsigned int i;
    UINT tmpW;
    UINT tmpH;
    HRESULT hr;
    unsigned int pow2Width;
    unsigned int pow2Height;


    TRACE("(%p) : Width %d, Height %d, Levels %d, Usage %#x\n", This, Width, Height, Levels, Usage);
    TRACE("Format %#x (%s), Pool %#x, ppTexture %p, pSharedHandle %p, parent %p\n",
            Format, debug_d3dformat(Format), Pool, ppTexture, pSharedHandle, parent);

    /* TODO: It should only be possible to create textures for formats 
             that are reported as supported */
    if (WINED3DFMT_UNKNOWN >= Format) {
        WARN("(%p) : Texture cannot be created with a format of WINED3DFMT_UNKNOWN\n", This);
        return WINED3DERR_INVALIDCALL;
    }

    D3DCREATERESOURCEOBJECTINSTANCE(object, Texture, WINED3DRTYPE_TEXTURE, 0);
    D3DINITIALIZEBASETEXTURE(object->baseTexture);    
    object->width  = Width;
    object->height = Height;

    /** Non-power2 support **/
    if (wined3d_settings.nonpower2_mode == NP2_NATIVE) {
        pow2Width = Width;
        pow2Height = Height;
    } else {
        /* Find the nearest pow2 match */
        pow2Width = pow2Height = 1;
        while (pow2Width < Width) pow2Width <<= 1;
        while (pow2Height < Height) pow2Height <<= 1;
    }

    /** FIXME: add support for real non-power-two if it's provided by the video card **/
    /* Precalculated scaling for 'faked' non power of two texture coords */
    object->pow2scalingFactorX  =  (((float)Width)  / ((float)pow2Width));
    object->pow2scalingFactorY  =  (((float)Height) / ((float)pow2Height));
    TRACE(" xf(%f) yf(%f)\n", object->pow2scalingFactorX, object->pow2scalingFactorY);

    /* Calculate levels for mip mapping */
    if (Levels == 0) {
        TRACE("calculating levels %d\n", object->baseTexture.levels);
        object->baseTexture.levels++;
        tmpW = Width;
        tmpH = Height;
        while (tmpW > 1 || tmpH > 1) {
            tmpW = max(1, tmpW >> 1);
            tmpH = max(1, tmpH >> 1);
            object->baseTexture.levels++;
        }
        TRACE("Calculated levels = %d\n", object->baseTexture.levels);
    }

    /* Generate all the surfaces */
    tmpW = Width;
    tmpH = Height;
    for (i = 0; i < object->baseTexture.levels; i++)
    {
        /* use the callback to create the texture surface */
        hr = D3DCB_CreateSurface(This->parent, parent, tmpW, tmpH, Format, Usage, Pool, i, &object->surfaces[i],NULL);
        if (hr!= WINED3D_OK || ( (IWineD3DSurfaceImpl *) object->surfaces[i])->Flags & SFLAG_OVERSIZE) {
            FIXME("Failed to create surface  %p\n", object);
            /* clean up */
            object->surfaces[i] = NULL;
            IWineD3DTexture_Release((IWineD3DTexture *)object);

            *ppTexture = NULL;
            return hr;
        }

        IWineD3DSurface_SetContainer(object->surfaces[i], (IWineD3DBase *)object);
        TRACE("Created surface level %d @ %p\n", i, object->surfaces[i]);
        /* calculate the next mipmap level */
        tmpW = max(1, tmpW >> 1);
        tmpH = max(1, tmpH >> 1);
    }

    TRACE("(%p) : Created  texture %p\n", This, object);
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_CreateVolumeTexture(IWineD3DDevice *iface,
                                                      UINT Width, UINT Height, UINT Depth,
                                                      UINT Levels, DWORD Usage,
                                                      WINED3DFORMAT Format, WINED3DPOOL Pool,
                                                      IWineD3DVolumeTexture **ppVolumeTexture,
                                                      HANDLE *pSharedHandle, IUnknown *parent,
                                                      D3DCB_CREATEVOLUMEFN D3DCB_CreateVolume) {

    IWineD3DDeviceImpl        *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DVolumeTextureImpl *object;
    unsigned int               i;
    UINT                       tmpW;
    UINT                       tmpH;
    UINT                       tmpD;

    /* TODO: It should only be possible to create textures for formats 
             that are reported as supported */
    if (WINED3DFMT_UNKNOWN >= Format) {
        WARN("(%p) : Texture cannot be created with a format of WINED3DFMT_UNKNOWN\n", This);
        return WINED3DERR_INVALIDCALL;
    }

    D3DCREATERESOURCEOBJECTINSTANCE(object, VolumeTexture, WINED3DRTYPE_VOLUMETEXTURE, 0);
    D3DINITIALIZEBASETEXTURE(object->baseTexture);

    TRACE("(%p) : W(%d) H(%d) D(%d), Lvl(%d) Usage(%d), Fmt(%u,%s), Pool(%s)\n", This, Width, Height,
          Depth, Levels, Usage, Format, debug_d3dformat(Format), debug_d3dpool(Pool));

    object->width  = Width;
    object->height = Height;
    object->depth  = Depth;

    /* Calculate levels for mip mapping */
    if (Levels == 0) {
        object->baseTexture.levels++;
        tmpW = Width;
        tmpH = Height;
        tmpD = Depth;
        while (tmpW > 1 || tmpH > 1 || tmpD > 1) {
            tmpW = max(1, tmpW >> 1);
            tmpH = max(1, tmpH >> 1);
            tmpD = max(1, tmpD >> 1);
            object->baseTexture.levels++;
        }
        TRACE("Calculated levels = %d\n", object->baseTexture.levels);
    }

    /* Generate all the surfaces */
    tmpW = Width;
    tmpH = Height;
    tmpD = Depth;

    for (i = 0; i < object->baseTexture.levels; i++)
    {
        /* Create the volume */
        D3DCB_CreateVolume(This->parent, parent, Width, Height, Depth, Format, Pool, Usage,
                           (IWineD3DVolume **)&object->volumes[i], pSharedHandle);

        /* Set its container to this object */
        IWineD3DVolume_SetContainer(object->volumes[i], (IWineD3DBase *)object);

        /* calcualte the next mipmap level */
        tmpW = max(1, tmpW >> 1);
        tmpH = max(1, tmpH >> 1);
        tmpD = max(1, tmpD >> 1);
    }

    *ppVolumeTexture = (IWineD3DVolumeTexture *) object;
    TRACE("(%p) : Created volume texture %p\n", This, object);
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_CreateVolume(IWineD3DDevice *iface,
                                               UINT Width, UINT Height, UINT Depth,
                                               DWORD Usage,
                                               WINED3DFORMAT Format, WINED3DPOOL Pool,
                                               IWineD3DVolume** ppVolume,
                                               HANDLE* pSharedHandle, IUnknown *parent) {

    IWineD3DDeviceImpl        *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DVolumeImpl        *object; /** NOTE: impl ref allowed since this is a create function **/
    const PixelFormatDesc *formatDesc  = getFormatDescEntry(Format);

    D3DCREATERESOURCEOBJECTINSTANCE(object, Volume, WINED3DRTYPE_VOLUME, ((Width * formatDesc->bpp) * Height * Depth))

    TRACE("(%p) : W(%d) H(%d) D(%d), Usage(%d), Fmt(%u,%s), Pool(%s)\n", This, Width, Height,
          Depth, Usage, Format, debug_d3dformat(Format), debug_d3dpool(Pool));

    object->currentDesc.Width   = Width;
    object->currentDesc.Height  = Height;
    object->currentDesc.Depth   = Depth;
    object->bytesPerPixel       = formatDesc->bpp;

    /** Note: Volume textures cannot be dxtn, hence no need to check here **/
    object->lockable            = TRUE;
    object->locked              = FALSE;
    memset(&object->lockedBox, 0, sizeof(WINED3DBOX));
    object->dirty               = TRUE;

    return IWineD3DVolume_AddDirtyBox((IWineD3DVolume *) object, NULL);
}

static HRESULT WINAPI IWineD3DDeviceImpl_CreateCubeTexture(IWineD3DDevice *iface, UINT EdgeLength,
                                                    UINT Levels, DWORD Usage,
                                                    WINED3DFORMAT Format, WINED3DPOOL Pool,
                                                    IWineD3DCubeTexture **ppCubeTexture,
                                                    HANDLE *pSharedHandle, IUnknown *parent,
                                                    D3DCB_CREATESURFACEFN D3DCB_CreateSurface) {

    IWineD3DDeviceImpl      *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DCubeTextureImpl *object; /** NOTE: impl ref allowed since this is a create function **/
    unsigned int             i, j;
    UINT                     tmpW;
    HRESULT                  hr;
    unsigned int pow2EdgeLength  = EdgeLength;

    /* TODO: It should only be possible to create textures for formats 
             that are reported as supported */
    if (WINED3DFMT_UNKNOWN >= Format) {
        WARN("(%p) : Texture cannot be created with a format of WINED3DFMT_UNKNOWN\n", This);
        return WINED3DERR_INVALIDCALL;
    }

    D3DCREATERESOURCEOBJECTINSTANCE(object, CubeTexture, WINED3DRTYPE_CUBETEXTURE, 0);
    D3DINITIALIZEBASETEXTURE(object->baseTexture);

    TRACE("(%p) Create Cube Texture\n", This);

    /** Non-power2 support **/

    /* Find the nearest pow2 match */
    pow2EdgeLength = 1;
    while (pow2EdgeLength < EdgeLength) pow2EdgeLength <<= 1;

    object->edgeLength           = EdgeLength;
    /* TODO: support for native non-power 2 */
    /* Precalculated scaling for 'faked' non power of two texture coords */
    object->pow2scalingFactor    = ((float)EdgeLength) / ((float)pow2EdgeLength);

    /* Calculate levels for mip mapping */
    if (Levels == 0) {
        object->baseTexture.levels++;
        tmpW = EdgeLength;
        while (tmpW > 1) {
            tmpW = max(1, tmpW >> 1);
            object->baseTexture.levels++;
        }
        TRACE("Calculated levels = %d\n", object->baseTexture.levels);
    }

    /* Generate all the surfaces */
    tmpW = EdgeLength;
    for (i = 0; i < object->baseTexture.levels; i++) {

        /* Create the 6 faces */
        for (j = 0; j < 6; j++) {

            hr=D3DCB_CreateSurface(This->parent, parent, tmpW, tmpW, Format, Usage, Pool,
                                   i /* Level */, &object->surfaces[j][i],pSharedHandle);

            if(hr!= WINED3D_OK) {
                /* clean up */
                int k;
                int l;
                for (l = 0; l < j; l++) {
                    IWineD3DSurface_Release(object->surfaces[j][i]);
                }
                for (k = 0; k < i; k++) {
                    for (l = 0; l < 6; l++) {
                    IWineD3DSurface_Release(object->surfaces[l][j]);
                    }
                }

                FIXME("(%p) Failed to create surface\n",object);
                HeapFree(GetProcessHeap(),0,object);
                *ppCubeTexture = NULL;
                return hr;
            }
            IWineD3DSurface_SetContainer(object->surfaces[j][i], (IWineD3DBase *)object);
            TRACE("Created surface level %d @ %p,\n", i, object->surfaces[j][i]);
        }
        tmpW = max(1, tmpW >> 1);
    }

    TRACE("(%p) : Created Cube Texture %p\n", This, object);
    *ppCubeTexture = (IWineD3DCubeTexture *) object;
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_CreateQuery(IWineD3DDevice *iface, WINED3DQUERYTYPE Type, IWineD3DQuery **ppQuery, IUnknown* parent) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DQueryImpl *object; /*NOTE: impl ref allowed since this is a create function */

    if (NULL == ppQuery) {
        /* Just a check to see if we support this type of query */
        HRESULT hr = WINED3DERR_NOTAVAILABLE;
        switch(Type) {
        case WINED3DQUERYTYPE_OCCLUSION:
            TRACE("(%p) occlusion query\n", This);
            if (GL_SUPPORT(ARB_OCCLUSION_QUERY))
                hr = WINED3D_OK;
            else
                WARN("Unsupported in local OpenGL implementation: ARB_OCCLUSION_QUERY/NV_OCCLUSION_QUERY\n");
            break;
        case WINED3DQUERYTYPE_VCACHE:
        case WINED3DQUERYTYPE_RESOURCEMANAGER:
        case WINED3DQUERYTYPE_VERTEXSTATS:
        case WINED3DQUERYTYPE_EVENT:
        case WINED3DQUERYTYPE_TIMESTAMP:
        case WINED3DQUERYTYPE_TIMESTAMPDISJOINT:
        case WINED3DQUERYTYPE_TIMESTAMPFREQ:
        case WINED3DQUERYTYPE_PIPELINETIMINGS:
        case WINED3DQUERYTYPE_INTERFACETIMINGS:
        case WINED3DQUERYTYPE_VERTEXTIMINGS:
        case WINED3DQUERYTYPE_PIXELTIMINGS:
        case WINED3DQUERYTYPE_BANDWIDTHTIMINGS:
        case WINED3DQUERYTYPE_CACHEUTILIZATION:
        default:
            FIXME("(%p) Unhandled query type %d\n", This, Type);
        }
        return hr;
    }

    D3DCREATEOBJECTINSTANCE(object, Query)
    object->type         = Type;
    /* allocated the 'extended' data based on the type of query requested */
    switch(Type){
    case WINED3DQUERYTYPE_OCCLUSION:
        if(GL_SUPPORT(ARB_OCCLUSION_QUERY)) {
            TRACE("(%p) Allocating data for an occlusion query\n", This);
            object->extendedData = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(WineQueryOcclusionData));
            GL_EXTCALL(glGenQueriesARB(1, &((WineQueryOcclusionData *)(object->extendedData))->queryId));
            break;
        }
    case WINED3DQUERYTYPE_VCACHE:
    case WINED3DQUERYTYPE_RESOURCEMANAGER:
    case WINED3DQUERYTYPE_VERTEXSTATS:
    case WINED3DQUERYTYPE_EVENT:
    case WINED3DQUERYTYPE_TIMESTAMP:
    case WINED3DQUERYTYPE_TIMESTAMPDISJOINT:
    case WINED3DQUERYTYPE_TIMESTAMPFREQ:
    case WINED3DQUERYTYPE_PIPELINETIMINGS:
    case WINED3DQUERYTYPE_INTERFACETIMINGS:
    case WINED3DQUERYTYPE_VERTEXTIMINGS:
    case WINED3DQUERYTYPE_PIXELTIMINGS:
    case WINED3DQUERYTYPE_BANDWIDTHTIMINGS:
    case WINED3DQUERYTYPE_CACHEUTILIZATION:
    default:
        object->extendedData = 0;
        FIXME("(%p) Unhandled query type %d\n",This , Type);
    }
    TRACE("(%p) : Created Query %p\n", This, object);
    return WINED3D_OK;
}

/*****************************************************************************
 * IWineD3DDeviceImpl_SetupFullscreenWindow
 *
 * Helper function that modifies a HWND's Style and ExStyle for proper
 * fullscreen use.
 *
 * Params:
 *  iface: Pointer to the IWineD3DDevice interface
 *  window: Window to setup
 *
 *****************************************************************************/
static void WINAPI IWineD3DDeviceImpl_SetupFullscreenWindow(IWineD3DDevice *iface, HWND window) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    LONG style, exStyle;
    /* Don't do anything if an original style is stored.
     * That shouldn't happen
     */
    TRACE("(%p): Setting up window %p for exclusive mode\n", This, window);
    if (This->style && This->exStyle) {
        ERR("(%p): Want to change the window parameters of HWND %p, but "
            "another style is stored for restoration afterwards\n", This, window);
    }

    /* Get the parameters and save them */
    style = GetWindowLongW(window, GWL_STYLE);
    exStyle = GetWindowLongW(window, GWL_EXSTYLE);
    This->style = style;
    This->exStyle = exStyle;

    /* Filter out window decorations */
    style &= ~WS_CAPTION;
    style &= ~WS_THICKFRAME;
    exStyle &= ~WS_EX_WINDOWEDGE;
    exStyle &= ~WS_EX_CLIENTEDGE;

    /* Make sure the window is managed, otherwise we won't get keyboard input */
    style |= WS_POPUP | WS_SYSMENU;

    TRACE("Old style was %08x,%08x, setting to %08x,%08x\n",
          This->style, This->exStyle, style, exStyle);

    SetWindowLongW(window, GWL_STYLE, style);
    SetWindowLongW(window, GWL_EXSTYLE, exStyle);

    /* Inform the window about the update. */
    SetWindowPos(window, HWND_TOP, 0, 0,
            This->ddraw_width, This->ddraw_height, SWP_FRAMECHANGED);
}

/*****************************************************************************
 * IWineD3DDeviceImpl_RestoreWindow
 *
 * Helper function that restores a windows' properties when taking it out
 * of fullscreen mode
 *
 * Params:
 *  iface: Pointer to the IWineD3DDevice interface
 *  window: Window to setup
 *
 *****************************************************************************/
static void WINAPI IWineD3DDeviceImpl_RestoreWindow(IWineD3DDevice *iface, HWND window) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    /* This could be a DDSCL_NORMAL -> DDSCL_NORMAL
     * switch, do nothing
     */
    if (!This->style && !This->exStyle) return;

    TRACE("(%p): Restoring window settings of window %p to %08x, %08x\n",
          This, window, This->style, This->exStyle);

    SetWindowLongW(window, GWL_STYLE, This->style);
    SetWindowLongW(window, GWL_EXSTYLE, This->exStyle);

    /* Delete the old values */
    This->style = 0;
    This->exStyle = 0;

    /* Inform the window about the update */
    SetWindowPos(window, 0 /* InsertAfter, ignored */,
                 0, 0, 0, 0, /* Pos, Size, ignored */
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
}

/* example at http://www.fairyengine.com/articles/dxmultiviews.htm */
static HRESULT WINAPI IWineD3DDeviceImpl_CreateAdditionalSwapChain(IWineD3DDevice* iface, WINED3DPRESENT_PARAMETERS*  pPresentationParameters,                                                                   IWineD3DSwapChain** ppSwapChain,
                                                            IUnknown* parent,
                                                            D3DCB_CREATERENDERTARGETFN D3DCB_CreateRenderTarget,
                                                            D3DCB_CREATEDEPTHSTENCILSURFACEFN D3DCB_CreateDepthStencil) {
    IWineD3DDeviceImpl      *This = (IWineD3DDeviceImpl *)iface;

    HDC                     hDc;
    IWineD3DSwapChainImpl  *object; /** NOTE: impl ref allowed since this is a create function **/
    int                     num;
    XVisualInfo             template;
    GLXContext              oldContext;
    Drawable                oldDrawable;
    HRESULT                 hr = WINED3D_OK;

    TRACE("(%p) : Created Aditional Swap Chain\n", This);

   /** FIXME: Test under windows to find out what the life cycle of a swap chain is,
   * does a device hold a reference to a swap chain giving them a lifetime of the device
   * or does the swap chain notify the device of its destruction.
    *******************************/

    /* Check the params */
    if(*pPresentationParameters->BackBufferCount > D3DPRESENT_BACK_BUFFER_MAX) {
        ERR("App requested %d back buffers, this is not supported for now\n", *pPresentationParameters->BackBufferCount);
        return WINED3DERR_INVALIDCALL;
    } else if (*pPresentationParameters->BackBufferCount > 1) {
        FIXME("The app requests more than one back buffer, this can't be supported properly. Please configure the application to use double buffering(=1 back buffer) if possible\n");
    }

    D3DCREATEOBJECTINSTANCE(object, SwapChain)

    /*********************
    * Lookup the window Handle and the relating X window handle
    ********************/

    /* Setup hwnd we are using, plus which display this equates to */
    object->win_handle = *(pPresentationParameters->hDeviceWindow);
    if (!object->win_handle) {
        object->win_handle = This->createParms.hFocusWindow;
    }

    object->win_handle = GetAncestor(object->win_handle, GA_ROOT);
    if ( !( object->win = (Window)GetPropA(object->win_handle, "__wine_x11_whole_window") ) ) {
        ERR("Can't get drawable (window), HWND:%p doesn't have the property __wine_x11_whole_window\n", object->win_handle);
        return WINED3DERR_NOTAVAILABLE;
    }
    hDc                = GetDC(object->win_handle);
    object->display    = get_display(hDc);
    ReleaseDC(object->win_handle, hDc);
    TRACE("Using a display of %p %p\n", object->display, hDc);

    if (NULL == object->display || NULL == hDc) {
        WARN("Failed to get a display and HDc for Window %p\n", object->win_handle);
        return WINED3DERR_NOTAVAILABLE;
    }

    if (object->win == 0) {
        WARN("Failed to get a valid XVisuial ID for the window %p\n", object->win_handle);
        return WINED3DERR_NOTAVAILABLE;
    }

    object->orig_width = GetSystemMetrics(SM_CXSCREEN);
    object->orig_height = GetSystemMetrics(SM_CYSCREEN);

    /**
    * Create an opengl context for the display visual
    *  NOTE: the visual is chosen as the window is created and the glcontext cannot
    *     use different properties after that point in time. FIXME: How to handle when requested format
    *     doesn't match actual visual? Cannot choose one here - code removed as it ONLY works if the one
    *     it chooses is identical to the one already being used!
     **********************************/

    /** FIXME: Handle stencil appropriately via EnableAutoDepthStencil / AutoDepthStencilFormat **/
    ENTER_GL();

    /* Create a new context for this swapchain */
    template.visualid = (VisualID)GetPropA(GetDesktopWindow(), "__wine_x11_visual_id");
    /* TODO: change this to find a similar visual, but one with a stencil/zbuffer buffer that matches the request
    (or the best possible if none is requested) */
    TRACE("Found x visual ID  : %ld\n", template.visualid);

    object->visInfo   = XGetVisualInfo(object->display, VisualIDMask, &template, &num);
    if (NULL == object->visInfo) {
        ERR("cannot really get XVisual\n");
        LEAVE_GL();
        return WINED3DERR_NOTAVAILABLE;
    } else {
        int n, value;
        /* Write out some debug info about the visual/s */
        TRACE("Using x visual ID  : %ld\n", template.visualid);
        TRACE("        visual info: %p\n", object->visInfo);
        TRACE("        num items  : %d\n", num);
        for (n = 0;n < num; n++) {
            TRACE("=====item=====: %d\n", n + 1);
            TRACE("   visualid      : %ld\n", object->visInfo[n].visualid);
            TRACE("   screen        : %d\n",  object->visInfo[n].screen);
            TRACE("   depth         : %u\n",  object->visInfo[n].depth);
            TRACE("   class         : %d\n",  object->visInfo[n].class);
            TRACE("   red_mask      : %ld\n", object->visInfo[n].red_mask);
            TRACE("   green_mask    : %ld\n", object->visInfo[n].green_mask);
            TRACE("   blue_mask     : %ld\n", object->visInfo[n].blue_mask);
            TRACE("   colormap_size : %d\n",  object->visInfo[n].colormap_size);
            TRACE("   bits_per_rgb  : %d\n",  object->visInfo[n].bits_per_rgb);
            /* log some extra glx info */
            glXGetConfig(object->display, object->visInfo, GLX_AUX_BUFFERS, &value);
            TRACE("   gl_aux_buffers  : %d\n",  value);
            glXGetConfig(object->display, object->visInfo, GLX_BUFFER_SIZE ,&value);
            TRACE("   gl_buffer_size  : %d\n",  value);
            glXGetConfig(object->display, object->visInfo, GLX_RED_SIZE, &value);
            TRACE("   gl_red_size  : %d\n",  value);
            glXGetConfig(object->display, object->visInfo, GLX_GREEN_SIZE, &value);
            TRACE("   gl_green_size  : %d\n",  value);
            glXGetConfig(object->display, object->visInfo, GLX_BLUE_SIZE, &value);
            TRACE("   gl_blue_size  : %d\n",  value);
            glXGetConfig(object->display, object->visInfo, GLX_ALPHA_SIZE, &value);
            TRACE("   gl_alpha_size  : %d\n",  value);
            glXGetConfig(object->display, object->visInfo, GLX_DEPTH_SIZE ,&value);
            TRACE("   gl_depth_size  : %d\n",  value);
            glXGetConfig(object->display, object->visInfo, GLX_STENCIL_SIZE, &value);
            TRACE("   gl_stencil_size : %d\n",  value);
        }
        /* Now choose a similar visual ID*/
    }
#ifdef USE_CONTEXT_MANAGER

    /** TODO: use a context mamager **/
#endif

    {
        IWineD3DSwapChain *implSwapChain;
        if (WINED3D_OK != IWineD3DDevice_GetSwapChain(iface, 0, &implSwapChain)) {
            /* The first time around we create the context that is shared with all other swapchains and render targets */
            object->glCtx = glXCreateContext(object->display, object->visInfo, NULL, GL_TRUE);
            TRACE("Creating implicit context for vis %p, hwnd %p\n", object->display, object->visInfo);
        } else {

            TRACE("Creating context for vis %p, hwnd %p\n", object->display, object->visInfo);
            /* TODO: don't use Impl structures outside of create functions! (a context manager will replace the ->glCtx) */
            /* and create a new context with the implicit swapchains context as the shared context */
            object->glCtx = glXCreateContext(object->display, object->visInfo, ((IWineD3DSwapChainImpl *)implSwapChain)->glCtx, GL_TRUE);
            IWineD3DSwapChain_Release(implSwapChain);
        }
    }

    /* Cleanup */
    XFree(object->visInfo);
    object->visInfo = NULL;

    LEAVE_GL();

    if (!object->glCtx) {
        ERR("Failed to create GLX context\n");
        return WINED3DERR_NOTAVAILABLE;
    } else {
        TRACE("Context created (HWND=%p, glContext=%p, Window=%ld, VisInfo=%p)\n",
                object->win_handle, object->glCtx, object->win, object->visInfo);
    }

   /*********************
   * Windowed / Fullscreen
   *******************/

   /**
   * TODO: MSDN says that we are only allowed one fullscreen swapchain per device,
   * so we should really check to see if there is a fullscreen swapchain already
   * I think Windows and X have different ideas about fullscreen, does a single head count as full screen?
    **************************************/

   if (!*(pPresentationParameters->Windowed)) {

        DEVMODEW devmode;
        HDC      hdc;
        int      bpp = 0;
        RECT     clip_rc;

        /* Get info on the current display setup */
        hdc = GetDC(0);
        bpp = GetDeviceCaps(hdc, BITSPIXEL);
        ReleaseDC(0, hdc);

        /* Change the display settings */
        memset(&devmode, 0, sizeof(DEVMODEW));
        devmode.dmFields     = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
        devmode.dmBitsPerPel = (bpp >= 24) ? 32 : bpp; /* Stupid XVidMode cannot change bpp */
        devmode.dmPelsWidth  = *(pPresentationParameters->BackBufferWidth);
        devmode.dmPelsHeight = *(pPresentationParameters->BackBufferHeight);
        MultiByteToWideChar(CP_ACP, 0, "Gamers CG", -1, devmode.dmDeviceName, CCHDEVICENAME);
        ChangeDisplaySettingsExW(devmode.dmDeviceName, &devmode, object->win_handle, CDS_FULLSCREEN, NULL);

        /* For GetDisplayMode */
        This->ddraw_width = devmode.dmPelsWidth;
        This->ddraw_height = devmode.dmPelsHeight;
        This->ddraw_format = *(pPresentationParameters->BackBufferFormat);

        IWineD3DDeviceImpl_SetupFullscreenWindow(iface, object->win_handle);

        /* And finally clip mouse to our screen */
        SetRect(&clip_rc, 0, 0, devmode.dmPelsWidth, devmode.dmPelsHeight);
        ClipCursor(&clip_rc);
    }


    /** MSDN: If Windowed is TRUE and either of the BackBufferWidth/Height values is zero,
     *  then the corresponding dimension of the client area of the hDeviceWindow
     *  (or the focus window, if hDeviceWindow is NULL) is taken.
      **********************/

    if (*(pPresentationParameters->Windowed) &&
        ((*(pPresentationParameters->BackBufferWidth)  == 0) ||
         (*(pPresentationParameters->BackBufferHeight) == 0))) {

        RECT Rect;
        GetClientRect(object->win_handle, &Rect);

        if (*(pPresentationParameters->BackBufferWidth) == 0) {
           *(pPresentationParameters->BackBufferWidth) = Rect.right;
           TRACE("Updating width to %d\n", *(pPresentationParameters->BackBufferWidth));
        }
        if (*(pPresentationParameters->BackBufferHeight) == 0) {
           *(pPresentationParameters->BackBufferHeight) = Rect.bottom;
           TRACE("Updating height to %d\n", *(pPresentationParameters->BackBufferHeight));
        }
    }

   /*********************
   * finish off parameter initialization
   *******************/

    /* Put the correct figures in the presentation parameters */
    TRACE("Copying across presentation parameters\n");
    object->presentParms.BackBufferWidth                = *(pPresentationParameters->BackBufferWidth);
    object->presentParms.BackBufferHeight               = *(pPresentationParameters->BackBufferHeight);
    object->presentParms.BackBufferFormat               = *(pPresentationParameters->BackBufferFormat);
    object->presentParms.BackBufferCount                = *(pPresentationParameters->BackBufferCount);
    object->presentParms.MultiSampleType                = *(pPresentationParameters->MultiSampleType);
    object->presentParms.MultiSampleQuality             = NULL == pPresentationParameters->MultiSampleQuality ? 0 : *(pPresentationParameters->MultiSampleQuality);
    object->presentParms.SwapEffect                     = *(pPresentationParameters->SwapEffect);
    object->presentParms.hDeviceWindow                  = *(pPresentationParameters->hDeviceWindow);
    object->presentParms.Windowed                       = *(pPresentationParameters->Windowed);
    object->presentParms.EnableAutoDepthStencil         = *(pPresentationParameters->EnableAutoDepthStencil);
    object->presentParms.AutoDepthStencilFormat         = *(pPresentationParameters->AutoDepthStencilFormat);
    object->presentParms.Flags                          = *(pPresentationParameters->Flags);
    object->presentParms.FullScreen_RefreshRateInHz     = *(pPresentationParameters->FullScreen_RefreshRateInHz);
    object->presentParms.PresentationInterval           = *(pPresentationParameters->PresentationInterval);


   /*********************
   * Create the back, front and stencil buffers
   *******************/

    TRACE("calling rendertarget CB\n");
    hr = D3DCB_CreateRenderTarget((IUnknown *) This->parent,
                             parent,
                             object->presentParms.BackBufferWidth,
                             object->presentParms.BackBufferHeight,
                             object->presentParms.BackBufferFormat,
                             object->presentParms.MultiSampleType,
                             object->presentParms.MultiSampleQuality,
                             TRUE /* Lockable */,
                             &object->frontBuffer,
                             NULL /* pShared (always null)*/);
    if (object->frontBuffer != NULL)
        IWineD3DSurface_SetContainer(object->frontBuffer, (IWineD3DBase *)object);

    if(object->presentParms.BackBufferCount > 0) {
        int i;

        object->backBuffer = HeapAlloc(GetProcessHeap(), 0, sizeof(IWineD3DSurface *) * object->presentParms.BackBufferCount);
        if(!object->backBuffer) {
            ERR("Out of memory\n");

            if (object->frontBuffer) {
                IUnknown *bufferParent;
                IWineD3DSurface_GetParent(object->frontBuffer, &bufferParent);
                IUnknown_Release(bufferParent); /* once for the get parent */
                if (IUnknown_Release(bufferParent) > 0) {
                    FIXME("(%p) Something's still holding the front buffer\n",This);
                }
            }
            HeapFree(GetProcessHeap(), 0, object);
            return E_OUTOFMEMORY;
        }

        for(i = 0; i < object->presentParms.BackBufferCount; i++) {
            TRACE("calling rendertarget CB\n");
            hr = D3DCB_CreateRenderTarget((IUnknown *) This->parent,
                                    parent,
                                    object->presentParms.BackBufferWidth,
                                    object->presentParms.BackBufferHeight,
                                    object->presentParms.BackBufferFormat,
                                    object->presentParms.MultiSampleType,
                                    object->presentParms.MultiSampleQuality,
                                    TRUE /* Lockable */,
                                    &object->backBuffer[i],
                                    NULL /* pShared (always null)*/);
            if(hr == WINED3D_OK && object->backBuffer[i]) {
                IWineD3DSurface_SetContainer(object->backBuffer[i], (IWineD3DBase *)object);
            } else {
                break;
            }
        }
    } else {
        object->backBuffer = NULL;
    }

    if (object->backBuffer != NULL) {
        ENTER_GL();
        glDrawBuffer(GL_BACK);
        checkGLcall("glDrawBuffer(GL_BACK)");
        LEAVE_GL();
    } else {
        /* Single buffering - draw to front buffer */
        ENTER_GL();
        glDrawBuffer(GL_FRONT);
        checkGLcall("glDrawBuffer(GL_FRONT)");
        LEAVE_GL();
    }

    /* Under directX swapchains share the depth stencil, so only create one depth-stencil */
    if (*(pPresentationParameters->EnableAutoDepthStencil) && hr == WINED3D_OK) {
        TRACE("Creating depth stencil buffer\n");
        if (This->depthStencilBuffer == NULL ) {
            hr = D3DCB_CreateDepthStencil((IUnknown *) This->parent,
                                    parent,
                                    object->presentParms.BackBufferWidth,
                                    object->presentParms.BackBufferHeight,
                                    object->presentParms.AutoDepthStencilFormat,
                                    object->presentParms.MultiSampleType,
                                    object->presentParms.MultiSampleQuality,
                                    FALSE /* FIXME: Discard */,
                                    &This->depthStencilBuffer,
                                    NULL /* pShared (always null)*/  );
            if (This->depthStencilBuffer != NULL)
                IWineD3DSurface_SetContainer(This->depthStencilBuffer, 0);
        }

        /** TODO: A check on width, height and multisample types
        *(since the zbuffer must be at least as large as the render target and have the same multisample parameters)
         ****************************/
        object->wantsDepthStencilBuffer = TRUE;
    } else {
        object->wantsDepthStencilBuffer = FALSE;
    }

    TRACE("FrontBuf @ %p, BackBuf @ %p, DepthStencil %d\n",object->frontBuffer, object->backBuffer ? object->backBuffer[0] : NULL, object->wantsDepthStencilBuffer);


   /*********************
   * init the default renderTarget management
   *******************/
    object->drawable     = object->win;
    object->render_ctx   = object->glCtx;

    if (hr == WINED3D_OK) {
        /*********************
         * Setup some defaults and clear down the buffers
         *******************/
        ENTER_GL();
        /** save current context and drawable **/
        oldContext  = glXGetCurrentContext();
        oldDrawable = glXGetCurrentDrawable();

        TRACE("Activating context (display %p context %p drawable %ld)!\n", object->display, object->glCtx, object->win);
        if (glXMakeCurrent(object->display, object->win, object->glCtx) == False) {
            ERR("Error in setting current context (display %p context %p drawable %ld)!\n", object->display, object->glCtx, object->win);
        }
        checkGLcall("glXMakeCurrent");

        TRACE("Setting up the screen\n");
        /* Clear the screen */
        glClearColor(1.0, 0.0, 0.0, 0.0);
        checkGLcall("glClearColor");
        glClearIndex(0);
        glClearDepth(1);
        glClearStencil(0xffff);

        checkGLcall("glClear");

        glColor3f(1.0, 1.0, 1.0);
        checkGLcall("glColor3f");

        glEnable(GL_LIGHTING);
        checkGLcall("glEnable");

        glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);
        checkGLcall("glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);");

        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_EXT);
        checkGLcall("glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_EXT);");

        glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL, GL_SEPARATE_SPECULAR_COLOR);
        checkGLcall("glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL, GL_SEPARATE_SPECULAR_COLOR);");

        /* switch back to the original context (if there was one)*/
        if (This->swapchains) {
            /** TODO: restore the context and drawable **/
            glXMakeCurrent(object->display, oldDrawable, oldContext);
        }

        /* Set the surface alignment. This never changes, so we are safe to set it once per context*/
        glPixelStorei(GL_PACK_ALIGNMENT, SURFACE_ALIGNMENT);
        checkGLcall("glPixelStorei(GL_PACK_ALIGNMENT, SURFACE_ALIGNMENT);");
        glPixelStorei(GL_UNPACK_ALIGNMENT, SURFACE_ALIGNMENT);
        checkGLcall("glPixelStorei(GL_UNPACK_ALIGNMENT, SURFACE_ALIGNMENT);");

        LEAVE_GL();

        TRACE("Set swapchain to %p\n", object);
    } else { /* something went wrong so clean up */
        IUnknown* bufferParent;
        if (object->frontBuffer) {

            IWineD3DSurface_GetParent(object->frontBuffer, &bufferParent);
            IUnknown_Release(bufferParent); /* once for the get parent */
            if (IUnknown_Release(bufferParent) > 0) {
                FIXME("(%p) Something's still holding the front buffer\n",This);
            }
        }
        if (object->backBuffer) {
            int i;
            for(i = 0; i < object->presentParms.BackBufferCount; i++) {
                if(object->backBuffer[i]) {
                    IWineD3DSurface_GetParent(object->backBuffer[i], &bufferParent);
                    IUnknown_Release(bufferParent); /* once for the get parent */
                    if (IUnknown_Release(bufferParent) > 0) {
                        FIXME("(%p) Something's still holding the back buffer\n",This);
                    }
                }
            }
            HeapFree(GetProcessHeap(), 0, object->backBuffer);
            object->backBuffer = NULL;
        }
        /* NOTE: don't clean up the depthstencil buffer because it belongs to the device */
        /* Clean up the context */
        /* check that we are the current context first (we shouldn't be though!) */
        if (object->glCtx != 0) {
            if(glXGetCurrentContext() == object->glCtx) {
                glXMakeCurrent(object->display, None, NULL);
            }
            glXDestroyContext(object->display, object->glCtx);
        }
        HeapFree(GetProcessHeap(), 0, object);

    }

    return hr;
}

/** NOTE: These are ahead of the other getters and setters to save using a forward declaration **/
static UINT     WINAPI  IWineD3DDeviceImpl_GetNumberOfSwapChains(IWineD3DDevice *iface) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p)\n", This);

    return This->NumberOfSwapChains;
}

static HRESULT  WINAPI  IWineD3DDeviceImpl_GetSwapChain(IWineD3DDevice *iface, UINT iSwapChain, IWineD3DSwapChain **pSwapChain) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : swapchain %d\n", This, iSwapChain);

    if(iSwapChain < This->NumberOfSwapChains) {
        *pSwapChain = This->swapchains[iSwapChain];
        IWineD3DSwapChain_AddRef(*pSwapChain);
        TRACE("(%p) returning %p\n", This, *pSwapChain);
        return WINED3D_OK;
    } else {
        TRACE("Swapchain out of range\n");
        *pSwapChain = NULL;
        return WINED3DERR_INVALIDCALL;
    }
}

/*****
 * Vertex Declaration
 *****/
static HRESULT WINAPI IWineD3DDeviceImpl_CreateVertexDeclaration(IWineD3DDevice* iface, CONST VOID* pDeclaration, IWineD3DVertexDeclaration** ppVertexDeclaration, IUnknown *parent) {
    IWineD3DDeviceImpl            *This   = (IWineD3DDeviceImpl *)iface;
    IWineD3DVertexDeclarationImpl *object = NULL;
    HRESULT hr = WINED3D_OK;
    TRACE("(%p) : directXVersion=%u, pFunction=%p, ppDecl=%p\n", This, ((IWineD3DImpl *)This->wineD3D)->dxVersion, pDeclaration, ppVertexDeclaration);
    D3DCREATEOBJECTINSTANCE(object, VertexDeclaration)
    object->allFVF = 0;

    hr = IWineD3DVertexDeclaration_SetDeclaration((IWineD3DVertexDeclaration *)object, (void *)pDeclaration);

    return hr;
}

/* http://msdn.microsoft.com/archive/default.asp?url=/archive/en-us/directx9_c/directx/graphics/programmingguide/programmable/vertexshaders/vscreate.asp */
static HRESULT WINAPI IWineD3DDeviceImpl_CreateVertexShader(IWineD3DDevice *iface, CONST DWORD *pDeclaration, CONST DWORD *pFunction, IWineD3DVertexShader **ppVertexShader, IUnknown *parent) {
    IWineD3DDeviceImpl       *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DVertexShaderImpl *object;  /* NOTE: impl usage is ok, this is a create */
    HRESULT hr = WINED3D_OK;
    D3DCREATESHADEROBJECTINSTANCE(object, VertexShader)
    object->baseShader.shader_ins = IWineD3DVertexShaderImpl_shader_ins;

    TRACE("(%p) : Created Vertex shader %p\n", This, *ppVertexShader);

    /* If a vertex declaration has been passed, save it to the vertex shader, this affects d3d8 only. */
    /* Further it needs to be set before calling SetFunction as SetFunction needs the declaration. */
    if (pDeclaration != NULL) {
        IWineD3DVertexDeclaration *vertexDeclaration;
        hr = IWineD3DDevice_CreateVertexDeclaration(iface, pDeclaration, &vertexDeclaration ,NULL);
        if (WINED3D_OK == hr) {
            TRACE("(%p) : Setting vertex declaration to %p\n", This, vertexDeclaration);
            object->vertexDeclaration = vertexDeclaration;
        } else {
            FIXME("(%p) : Failed to set the declaration, returning WINED3DERR_INVALIDCALL\n", iface);
            IWineD3DVertexShader_Release(*ppVertexShader);
            return WINED3DERR_INVALIDCALL;
        }
    }

    hr = IWineD3DVertexShader_SetFunction(*ppVertexShader, pFunction);

    if (WINED3D_OK != hr) {
        FIXME("(%p) : Failed to set the function, returning WINED3DERR_INVALIDCALL\n", iface);
        IWineD3DVertexShader_Release(*ppVertexShader);
        return WINED3DERR_INVALIDCALL;
    }

#if 0 /* TODO: In D3D* SVP is atatched to the shader, in D3D9 it's attached to the device and isn't stored in the stateblock. */
    if(Usage == WINED3DUSAGE_SOFTWAREVERTEXPROCESSING) {
        /* Foo */
    } else {
        /* Bar */
    }

#endif

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_CreatePixelShader(IWineD3DDevice *iface, CONST DWORD *pFunction, IWineD3DPixelShader **ppPixelShader, IUnknown *parent) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DPixelShaderImpl *object; /* NOTE: impl allowed, this is a create */
    HRESULT hr = WINED3D_OK;

    D3DCREATESHADEROBJECTINSTANCE(object, PixelShader)
    object->baseShader.shader_ins = IWineD3DPixelShaderImpl_shader_ins;
    hr = IWineD3DPixelShader_SetFunction(*ppPixelShader, pFunction);
    if (WINED3D_OK == hr) {
        TRACE("(%p) : Created Pixel shader %p\n", This, *ppPixelShader);
    } else {
        WARN("(%p) : Failed to create pixel shader\n", This);
    }

    return hr;
}

static HRESULT WINAPI IWineD3DDeviceImpl_CreatePalette(IWineD3DDevice *iface, DWORD Flags, PALETTEENTRY *PalEnt, IWineD3DPalette **Palette, IUnknown *Parent) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;
    IWineD3DPaletteImpl *object;
    HRESULT hr;
    TRACE("(%p)->(%x, %p, %p, %p)\n", This, Flags, PalEnt, Palette, Parent);

    /* Create the new object */
    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IWineD3DPaletteImpl));
    if(!object) {
        ERR("Out of memory when allocating memory for a IWineD3DPalette implementation\n");
        return E_OUTOFMEMORY;
    }

    object->lpVtbl = &IWineD3DPalette_Vtbl;
    object->ref = 1;
    object->Flags = Flags;
    object->parent = Parent;
    object->wineD3DDevice = This;
    object->palNumEntries = IWineD3DPaletteImpl_Size(Flags);
	
    object->hpal = CreatePalette((const LOGPALETTE*)&(object->palVersion));

    if(!object->hpal) {
        HeapFree( GetProcessHeap(), 0, object);
        return E_OUTOFMEMORY;
    }

    hr = IWineD3DPalette_SetEntries((IWineD3DPalette *) object, 0, 0, IWineD3DPaletteImpl_Size(Flags), PalEnt);
    if(FAILED(hr)) {
        IWineD3DPalette_Release((IWineD3DPalette *) object);
        return hr;
    }

    *Palette = (IWineD3DPalette *) object;

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_Init3D(IWineD3DDevice *iface, WINED3DPRESENT_PARAMETERS* pPresentationParameters, D3DCB_CREATEADDITIONALSWAPCHAIN D3DCB_CreateAdditionalSwapChain) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;
    IWineD3DSwapChainImpl *swapchain;
    DWORD state;

    TRACE("(%p)->(%p,%p)\n", This, pPresentationParameters, D3DCB_CreateAdditionalSwapChain);
    if(This->d3d_initialized) return WINED3DERR_INVALIDCALL;

    /* TODO: Test if OpenGL is compiled in and loaded */

    /* Initialize the texture unit mapping to a 1:1 mapping */
    for(state = 0; state < MAX_SAMPLERS; state++) {
        This->texUnitMap[state] = state;
    }
    This->oneToOneTexUnitMap = TRUE;

    /* Setup the implicit swapchain */
    TRACE("Creating implicit swapchain\n");
    if (D3D_OK != D3DCB_CreateAdditionalSwapChain((IUnknown *) This->parent, pPresentationParameters, (IWineD3DSwapChain **)&swapchain) || swapchain == NULL) {
        WARN("Failed to create implicit swapchain\n");
        return WINED3DERR_INVALIDCALL;
    }

    This->NumberOfSwapChains = 1;
    This->swapchains = HeapAlloc(GetProcessHeap(), 0, This->NumberOfSwapChains * sizeof(IWineD3DSwapChain *));
    if(!This->swapchains) {
        ERR("Out of memory!\n");
        IWineD3DSwapChain_Release( (IWineD3DSwapChain *) swapchain);
        return E_OUTOFMEMORY;
    }
    This->swapchains[0] = (IWineD3DSwapChain *) swapchain;

    if(swapchain->backBuffer && swapchain->backBuffer[0]) {
        TRACE("Setting rendertarget to %p\n", swapchain->backBuffer);
        This->render_targets[0] = swapchain->backBuffer[0];
    }
    else {
        TRACE("Setting rendertarget to %p\n", swapchain->frontBuffer);
        This->render_targets[0] = swapchain->frontBuffer;
    }
    IWineD3DSurface_AddRef(This->render_targets[0]);
    /* Depth Stencil support */
    This->stencilBufferTarget = This->depthStencilBuffer;
    if (wined3d_settings.offscreen_rendering_mode == ORM_FBO) {
        set_depth_stencil_fbo(iface, This->depthStencilBuffer);
    }
    if (NULL != This->stencilBufferTarget) {
        IWineD3DSurface_AddRef(This->stencilBufferTarget);
    }

    /* Set up some starting GL setup */
    ENTER_GL();
    /*
    * Initialize openGL extension related variables
    *  with Default values
    */

    ((IWineD3DImpl *) This->wineD3D)->isGLInfoValid = IWineD3DImpl_FillGLCaps( This->wineD3D, swapchain->display);
    /* Setup all the devices defaults */
    IWineD3DStateBlock_InitStartupStateBlock((IWineD3DStateBlock *)This->stateBlock);
#if 0
    IWineD3DImpl_CheckGraphicsMemory();
#endif
    LEAVE_GL();

    /* Initialize our list of GLSL programs */
    list_init(&This->glsl_shader_progs);

    { /* Set a default viewport */
        WINED3DVIEWPORT vp;
        vp.X      = 0;
        vp.Y      = 0;
        vp.Width  = *(pPresentationParameters->BackBufferWidth);
        vp.Height = *(pPresentationParameters->BackBufferHeight);
        vp.MinZ   = 0.0f;
        vp.MaxZ   = 1.0f;
        IWineD3DDevice_SetViewport((IWineD3DDevice *)This, &vp);
    }

    /* Initialize the current view state */
    This->view_ident = 1;
    This->last_was_rhw = 0;
    glGetIntegerv(GL_MAX_LIGHTS, &This->maxConcurrentLights);
    TRACE("(%p) All defaults now set up, leaving Init3D with %p\n", This, This);

    /* Clear the screen */
    IWineD3DDevice_Clear((IWineD3DDevice *) This, 0, NULL, WINED3DCLEAR_STENCIL|WINED3DCLEAR_ZBUFFER|WINED3DCLEAR_TARGET, 0x00, 1.0, 0);

    /* Mark all states dirty. The Setters will not mark a state dirty when the new value is equal to the old value
     * This might create a problem in 2 situations:
     * ->The D3D default value is 0, but the opengl default value is something else
     * ->D3D7 unintialized D3D and reinitializes it. This way the context is destroyed, be the stateblock unchanged
     */
    for(state = 0; state <= STATE_HIGHEST; state++) {
        IWineD3DDeviceImpl_MarkStateDirty(This, state);
    }

    This->d3d_initialized = TRUE;
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_Uninit3D(IWineD3DDevice *iface, D3DCB_DESTROYSURFACEFN D3DCB_DestroyDepthStencilSurface, D3DCB_DESTROYSWAPCHAINFN D3DCB_DestroySwapChain) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;
    int sampler;
    uint i;
    TRACE("(%p)\n", This);

    if(!This->d3d_initialized) return WINED3DERR_INVALIDCALL;

    /* Delete the mouse cursor texture */
    if(This->cursorTexture) {
        ENTER_GL();
        glDeleteTextures(1, &This->cursorTexture);
        LEAVE_GL();
        This->cursorTexture = 0;
    }

    for(sampler = 0; sampler < GL_LIMITS(sampler_stages); ++sampler) {
        IWineD3DDevice_SetTexture(iface, sampler, NULL);
    }

    /* Release the buffers (with sanity checks)*/
    TRACE("Releasing the depth stencil buffer at %p\n", This->stencilBufferTarget);
    if(This->stencilBufferTarget != NULL && (IWineD3DSurface_Release(This->stencilBufferTarget) >0)){
        if(This->depthStencilBuffer != This->stencilBufferTarget)
            FIXME("(%p) Something's still holding the depthStencilBuffer\n",This);
    }
    This->stencilBufferTarget = NULL;

    TRACE("Releasing the render target at %p\n", This->render_targets[0]);
    if(IWineD3DSurface_Release(This->render_targets[0]) >0){
          /* This check is a bit silly, itshould be in swapchain_release FIXME("(%p) Something's still holding the renderTarget\n",This); */
    }
    TRACE("Setting rendertarget to NULL\n");
    This->render_targets[0] = NULL;

    if (This->depthStencilBuffer) {
        if(D3DCB_DestroyDepthStencilSurface(This->depthStencilBuffer) > 0) {
            FIXME("(%p) Something's still holding the depthStencilBuffer\n", This);
        }
        This->depthStencilBuffer = NULL;
    }

    for(i=0; i < This->NumberOfSwapChains; i++) {
        TRACE("Releasing the implicit swapchain %d\n", i);
        if (D3DCB_DestroySwapChain(This->swapchains[i])  > 0) {
            FIXME("(%p) Something's still holding the implicit swapchain\n", This);
        }
    }

    HeapFree(GetProcessHeap(), 0, This->swapchains);
    This->swapchains = NULL;
    This->NumberOfSwapChains = 0;

    This->d3d_initialized = FALSE;
    return WINED3D_OK;
}

static void WINAPI IWineD3DDeviceImpl_SetFullscreen(IWineD3DDevice *iface, BOOL fullscreen) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;
    TRACE("(%p) Setting DDraw fullscreen mode to %s\n", This, fullscreen ? "true" : "false");

    /* DirectDraw apps can change between fullscreen and windowed mode after device creation with
     * IDirectDraw7::SetCooperativeLevel. The GDI surface implementation needs to know this.
     * DDraw doesn't necessarily have a swapchain, so we have to store the fullscreen flag
     * separately.
     */
    This->ddraw_fullscreen = fullscreen;
}

static HRESULT WINAPI IWineD3DDeviceImpl_EnumDisplayModes(IWineD3DDevice *iface, DWORD Flags, UINT Width, UINT Height, WINED3DFORMAT pixelformat, LPVOID context, D3DCB_ENUMDISPLAYMODESCALLBACK callback) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    DEVMODEW DevModeW;
    int i;
    const PixelFormatDesc *formatDesc  = getFormatDescEntry(pixelformat);

    TRACE("(%p)->(%x,%d,%d,%d,%p,%p)\n", This, Flags, Width, Height, pixelformat, context, callback);

    for (i = 0; EnumDisplaySettingsExW(NULL, i, &DevModeW, 0); i++) {
        /* Ignore some modes if a description was passed */
        if ( (Width > 0)  && (Width != DevModeW.dmPelsWidth)) continue;
        if ( (Height > 0)  && (Height != DevModeW.dmPelsHeight)) continue;
        if ( (pixelformat != WINED3DFMT_UNKNOWN) && ( formatDesc->bpp != DevModeW.dmBitsPerPel) ) continue;

        TRACE("Enumerating %dx%d@%s\n", DevModeW.dmPelsWidth, DevModeW.dmPelsHeight, debug_d3dformat(pixelformat_for_depth(DevModeW.dmBitsPerPel)));

        if (callback((IUnknown *) This, (UINT) DevModeW.dmPelsWidth, (UINT) DevModeW.dmPelsHeight, pixelformat_for_depth(DevModeW.dmBitsPerPel), 60.0, context) == DDENUMRET_CANCEL)
            return D3D_OK;
    }

    return D3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetDisplayMode(IWineD3DDevice *iface, UINT iSwapChain, WINED3DDISPLAYMODE* pMode) {
    DEVMODEW devmode;
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    LONG ret;
    const PixelFormatDesc *formatDesc  = getFormatDescEntry(pMode->Format);
    RECT clip_rc;

    TRACE("(%p)->(%d,%p) Mode=%dx%dx@%d, %s\n", This, iSwapChain, pMode, pMode->Width, pMode->Height, pMode->RefreshRate, debug_d3dformat(pMode->Format));

    /* Resize the screen even without a window:
     * The app could have unset it with SetCooperativeLevel, but not called
     * RestoreDisplayMode first. Then the release will call RestoreDisplayMode,
     * but we don't have any hwnd
     */

    devmode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
    devmode.dmBitsPerPel = formatDesc->bpp * 8;
    if(devmode.dmBitsPerPel == 24) devmode.dmBitsPerPel = 32;
    devmode.dmPelsWidth  = pMode->Width;
    devmode.dmPelsHeight = pMode->Height;

    devmode.dmDisplayFrequency = pMode->RefreshRate;
    if (pMode->RefreshRate != 0)  {
        devmode.dmFields |= DM_DISPLAYFREQUENCY;
    }

    /* Only change the mode if necessary */
    if( (This->ddraw_width == pMode->Width) &&
        (This->ddraw_height == pMode->Height) &&
        (This->ddraw_format == pMode->Format) &&
        (pMode->RefreshRate == 0) ) {
        return D3D_OK;
    }

    ret = ChangeDisplaySettingsExW(NULL, &devmode, NULL, CDS_FULLSCREEN, NULL);
    if (ret != DISP_CHANGE_SUCCESSFUL) {
        if(devmode.dmDisplayFrequency != 0) {
            WARN("ChangeDisplaySettingsExW failed, trying without the refresh rate\n");
            devmode.dmFields &= ~DM_DISPLAYFREQUENCY;
            devmode.dmDisplayFrequency = 0;
            ret = ChangeDisplaySettingsExW(NULL, &devmode, NULL, CDS_FULLSCREEN, NULL) != DISP_CHANGE_SUCCESSFUL;
        }
        if(ret != DISP_CHANGE_SUCCESSFUL) {
            return DDERR_INVALIDMODE;
        }
    }

    /* Store the new values */
    This->ddraw_width = pMode->Width;
    This->ddraw_height = pMode->Height;
    This->ddraw_format = pMode->Format;

    /* Only do this with a window of course */
    if(This->ddraw_window)
      MoveWindow(This->ddraw_window, 0, 0, pMode->Width, pMode->Height, TRUE);

    /* And finally clip mouse to our screen */
    SetRect(&clip_rc, 0, 0, pMode->Width, pMode->Height);
    ClipCursor(&clip_rc);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetDirect3D(IWineD3DDevice *iface, IWineD3D **ppD3D) {
   IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
   *ppD3D= This->wineD3D;
   TRACE("(%p) : wineD3D returning %p\n", This,  *ppD3D);
   IWineD3D_AddRef(*ppD3D);
   return WINED3D_OK;
}

static UINT WINAPI IWineD3DDeviceImpl_GetAvailableTextureMem(IWineD3DDevice *iface) {
    /** NOTE: There's a probably  a hack-around for this one by putting as many pbuffers, VBOs (or whatever)
    * into the video ram as possible and seeing how many fit
    * you can also get the correct initial value from nvidia and ATI's driver via X
    * texture memory is video memory + AGP memory
    *******************/
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    static BOOL showfixmes = TRUE;
    if (showfixmes) {
        FIXME("(%p) : stub, simulating %dMB for now, returning %dMB left\n", This,
         (wined3d_settings.emulated_textureram/(1024*1024)),
         ((wined3d_settings.emulated_textureram - wineD3DGlobalStatistics->glsurfaceram) / (1024*1024)));
         showfixmes = FALSE;
    }
    TRACE("(%p) : simulating %dMB, returning %dMB left\n",  This,
         (wined3d_settings.emulated_textureram/(1024*1024)),
         ((wined3d_settings.emulated_textureram - wineD3DGlobalStatistics->glsurfaceram) / (1024*1024)));
    /* return simulated texture memory left */
    return (wined3d_settings.emulated_textureram - wineD3DGlobalStatistics->glsurfaceram);
}



/*****
 * Get / Set FVF
 *****/
static HRESULT WINAPI IWineD3DDeviceImpl_SetFVF(IWineD3DDevice *iface, DWORD fvf) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    /* Update the current state block */
    This->updateStateBlock->changed.fvf      = TRUE;
    This->updateStateBlock->set.fvf          = TRUE;

    if(This->updateStateBlock->fvf == fvf) {
        TRACE("Application is setting the old fvf over, nothing to do\n");
        return WINED3D_OK;
    }

    This->updateStateBlock->fvf              = fvf;
    TRACE("(%p) : FVF Shader FVF set to %x\n", This, fvf);
    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_VDECL);
    return WINED3D_OK;
}


static HRESULT WINAPI IWineD3DDeviceImpl_GetFVF(IWineD3DDevice *iface, DWORD *pfvf) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : GetFVF returning %x\n", This, This->stateBlock->fvf);
    *pfvf = This->stateBlock->fvf;
    return WINED3D_OK;
}

/*****
 * Get / Set Stream Source
 *****/
static HRESULT WINAPI IWineD3DDeviceImpl_SetStreamSource(IWineD3DDevice *iface, UINT StreamNumber,IWineD3DVertexBuffer* pStreamData, UINT OffsetInBytes, UINT Stride) {
        IWineD3DDeviceImpl       *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DVertexBuffer     *oldSrc;

    /**TODO: instance and index data, see
    http://msdn.microsoft.com/library/default.asp?url=/library/en-us/directx9_c/directx/graphics/programmingguide/advancedtopics/DrawingMultipleInstances.asp
    and
    http://msdn.microsoft.com/library/default.asp?url=/library/en-us/directx9_c/directx/graphics/reference/d3d/interfaces/idirect3ddevice9/SetStreamSourceFreq.asp
     **************/

    /* D3d9 only, but shouldn't  hurt d3d8 */
    UINT streamFlags;

    streamFlags = StreamNumber &(WINED3DSTREAMSOURCE_INDEXEDDATA | WINED3DSTREAMSOURCE_INSTANCEDATA);
    if (streamFlags) {
        if (streamFlags & WINED3DSTREAMSOURCE_INDEXEDDATA) {
           FIXME("stream index data not supported\n");
        }
        if (streamFlags & WINED3DSTREAMSOURCE_INDEXEDDATA) {
           FIXME("stream instance data not supported\n");
        }
    }

    StreamNumber&= ~(WINED3DSTREAMSOURCE_INDEXEDDATA | WINED3DSTREAMSOURCE_INSTANCEDATA);

    if (StreamNumber >= MAX_STREAMS) {
        WARN("Stream out of range %d\n", StreamNumber);
        return WINED3DERR_INVALIDCALL;
    }

    oldSrc = This->stateBlock->streamSource[StreamNumber];
    TRACE("(%p) : StreamNo: %d, OldStream (%p), NewStream (%p), NewStride %d\n", This, StreamNumber, oldSrc, pStreamData, Stride);

    This->updateStateBlock->changed.streamSource[StreamNumber] = TRUE;
    This->updateStateBlock->set.streamSource[StreamNumber]     = TRUE;

    if(oldSrc == pStreamData &&
       This->updateStateBlock->streamStride[StreamNumber] == Stride &&
       This->updateStateBlock->streamOffset[StreamNumber] == OffsetInBytes &&
       This->updateStateBlock->streamFlags[StreamNumber] == streamFlags) {
       TRACE("Application is setting the old values over, nothing to do\n");
       return WINED3D_OK;
    }

    This->updateStateBlock->streamSource[StreamNumber]         = pStreamData;
    if (pStreamData) {
        This->updateStateBlock->streamStride[StreamNumber]     = Stride;
        This->updateStateBlock->streamOffset[StreamNumber]     = OffsetInBytes;
    }
    This->updateStateBlock->streamFlags[StreamNumber]          = streamFlags;

    /* Handle recording of state blocks */
    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return WINED3D_OK;
    }

    /* Same stream object: no action */
    if (oldSrc == pStreamData)
        return WINED3D_OK;

    /* Need to do a getParent and pass the reffs up */
    /* MSDN says ..... When an application no longer holds a references to this interface, the interface will automatically be freed.
    which suggests that we shouldn't be ref counting? and do need a _release on the stream source to reset the stream source
    so for now, just count internally   */
    if (pStreamData != NULL) {
        IWineD3DVertexBufferImpl *vbImpl = (IWineD3DVertexBufferImpl *) pStreamData;
        InterlockedIncrement(&vbImpl->bindCount);
    }
    if (oldSrc != NULL) {
        InterlockedDecrement(&((IWineD3DVertexBufferImpl *) oldSrc)->bindCount);
    }

    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_STREAMSRC);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetStreamSource(IWineD3DDevice *iface, UINT StreamNumber,IWineD3DVertexBuffer** pStream, UINT *pOffset, UINT* pStride) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    UINT streamFlags;

    TRACE("(%p) : StreamNo: %d, Stream (%p), Stride %d\n", This, StreamNumber,
           This->stateBlock->streamSource[StreamNumber], This->stateBlock->streamStride[StreamNumber]);


    streamFlags = StreamNumber &(WINED3DSTREAMSOURCE_INDEXEDDATA | WINED3DSTREAMSOURCE_INSTANCEDATA);
    if (streamFlags) {
        if (streamFlags & WINED3DSTREAMSOURCE_INDEXEDDATA) {
           FIXME("stream index data not supported\n");
        }
        if (streamFlags & WINED3DSTREAMSOURCE_INDEXEDDATA) {
            FIXME("stream instance data not supported\n");
        }
    }

    StreamNumber&= ~(WINED3DSTREAMSOURCE_INDEXEDDATA | WINED3DSTREAMSOURCE_INSTANCEDATA);

    if (StreamNumber >= MAX_STREAMS) {
        WARN("Stream out of range %d\n", StreamNumber);
        return WINED3DERR_INVALIDCALL;
    }
    *pStream = This->stateBlock->streamSource[StreamNumber];
    *pStride = This->stateBlock->streamStride[StreamNumber];
    if (pOffset) {
        *pOffset = This->stateBlock->streamOffset[StreamNumber];
    }

    if (*pStream != NULL) {
        IWineD3DVertexBuffer_AddRef(*pStream); /* We have created a new reference to the VB */
    }
    return WINED3D_OK;
}

/*Should be quite easy, just an extension of vertexdata
ref...
http://msdn.microsoft.com/archive/default.asp?url=/archive/en-us/directx9_c_Summer_04/directx/graphics/programmingguide/advancedtopics/DrawingMultipleInstances.asp

The divider is a bit odd though

VertexOffset = StartVertex / Divider * StreamStride +
               VertexIndex / Divider * StreamStride + StreamOffset

*/
static HRESULT WINAPI IWineD3DDeviceImpl_SetStreamSourceFreq(IWineD3DDevice *iface,  UINT StreamNumber, UINT Divider) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    TRACE("(%p) StreamNumber(%d), Divider(%d)\n", This, StreamNumber, Divider);
    This->updateStateBlock->streamFlags[StreamNumber] = Divider & (WINED3DSTREAMSOURCE_INSTANCEDATA  | WINED3DSTREAMSOURCE_INDEXEDDATA );

    This->updateStateBlock->changed.streamFreq[StreamNumber]  = TRUE;
    This->updateStateBlock->set.streamFreq[StreamNumber]      = TRUE;
    This->updateStateBlock->streamFreq[StreamNumber]          = Divider & 0x7FFFFF;

    if (This->updateStateBlock->streamFlags[StreamNumber] || This->updateStateBlock->streamFreq[StreamNumber] != 1) {
        FIXME("Stream indexing not fully supported\n");
    }

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetStreamSourceFreq(IWineD3DDevice *iface,  UINT StreamNumber, UINT* Divider) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    TRACE("(%p) StreamNumber(%d), Divider(%p)\n", This, StreamNumber, Divider);
    *Divider = This->updateStateBlock->streamFreq[StreamNumber] | This->updateStateBlock->streamFlags[StreamNumber];

    TRACE("(%p) : returning %d\n", This, *Divider);

    return WINED3D_OK;
}

/*****
 * Get / Set & Multiply Transform
 *****/
static HRESULT  WINAPI  IWineD3DDeviceImpl_SetTransform(IWineD3DDevice *iface, WINED3DTRANSFORMSTATETYPE d3dts, CONST WINED3DMATRIX* lpmatrix) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    /* Most of this routine, comments included copied from ddraw tree initially: */
    TRACE("(%p) : Transform State=%s\n", This, debug_d3dtstype(d3dts));

    /* Handle recording of state blocks */
    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        This->updateStateBlock->changed.transform[d3dts] = TRUE;
        This->updateStateBlock->set.transform[d3dts]     = TRUE;
        memcpy(&This->updateStateBlock->transforms[d3dts], lpmatrix, sizeof(WINED3DMATRIX));
        return WINED3D_OK;
    }

    /*
     * If the new matrix is the same as the current one,
     * we cut off any further processing. this seems to be a reasonable
     * optimization because as was noticed, some apps (warcraft3 for example)
     * tend towards setting the same matrix repeatedly for some reason.
     *
     * From here on we assume that the new matrix is different, wherever it matters.
     */
    if (!memcmp(&This->stateBlock->transforms[d3dts].u.m[0][0], lpmatrix, sizeof(WINED3DMATRIX))) {
        TRACE("The app is setting the same matrix over again\n");
        return WINED3D_OK;
    } else {
        conv_mat(lpmatrix, &This->stateBlock->transforms[d3dts].u.m[0][0]);
    }

    /*
       ScreenCoord = ProjectionMat * ViewMat * WorldMat * ObjectCoord
       where ViewMat = Camera space, WorldMat = world space.

       In OpenGL, camera and world space is combined into GL_MODELVIEW
       matrix.  The Projection matrix stay projection matrix.
     */

    /* Capture the times we can just ignore the change for now */
    if (d3dts == WINED3DTS_VIEW) { /* handle the VIEW matrice */
        This->view_ident = !memcmp(lpmatrix, identity, 16 * sizeof(float));
        /* Handled by the state manager */
    }

    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_TRANSFORM(d3dts));
    return WINED3D_OK;

}
static HRESULT WINAPI IWineD3DDeviceImpl_GetTransform(IWineD3DDevice *iface, WINED3DTRANSFORMSTATETYPE State, WINED3DMATRIX* pMatrix) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : for Transform State %s\n", This, debug_d3dtstype(State));
    memcpy(pMatrix, &This->stateBlock->transforms[State], sizeof(WINED3DMATRIX));
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_MultiplyTransform(IWineD3DDevice *iface, WINED3DTRANSFORMSTATETYPE State, CONST WINED3DMATRIX* pMatrix) {
    WINED3DMATRIX *mat = NULL;
    WINED3DMATRIX temp;

    /* Note: Using 'updateStateBlock' rather than 'stateblock' in the code
     * below means it will be recorded in a state block change, but it
     * works regardless where it is recorded.
     * If this is found to be wrong, change to StateBlock.
     */
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : For state %s\n", This, debug_d3dtstype(State));

    if (State < HIGHEST_TRANSFORMSTATE)
    {
        mat = &This->updateStateBlock->transforms[State];
    } else {
        FIXME("Unhandled transform state!!\n");
    }

    multiply_matrix(&temp, mat, (const WINED3DMATRIX *) pMatrix);

    /* Apply change via set transform - will reapply to eg. lights this way */
    return IWineD3DDeviceImpl_SetTransform(iface, State, &temp);
}

/*****
 * Get / Set Light
 *****/
/* Note lights are real special cases. Although the device caps state only eg. 8 are supported,
   you can reference any indexes you want as long as that number max are enabled at any
   one point in time! Therefore since the indexes can be anything, we need a linked list of them.
   However, this causes stateblock problems. When capturing the state block, I duplicate the list,
   but when recording, just build a chain pretty much of commands to be replayed.                  */

static HRESULT WINAPI IWineD3DDeviceImpl_SetLight(IWineD3DDevice *iface, DWORD Index, CONST WINED3DLIGHT* pLight) {
    float rho;
    PLIGHTINFOEL *object, *temp;

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : Idx(%d), pLight(%p)\n", This, Index, pLight);

    /* If recording state block, just add to end of lights chain */
    if (This->isRecordingState) {
        object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(PLIGHTINFOEL));
        if (NULL == object) {
            return WINED3DERR_OUTOFVIDEOMEMORY;
        }
        memcpy(&object->OriginalParms, pLight, sizeof(WINED3DLIGHT));
        object->OriginalIndex = Index;
        object->glIndex = -1;
        object->changed = TRUE;

        /* Add to the END of the chain of lights changes to be replayed */
        if (This->updateStateBlock->lights == NULL) {
            This->updateStateBlock->lights = object;
        } else {
            temp = This->updateStateBlock->lights;
            while (temp->next != NULL) temp=temp->next;
            temp->next = object;
        }
        TRACE("Recording... not performing anything more\n");
        return WINED3D_OK;
    }

    /* Ok, not recording any longer so do real work */
    object = This->stateBlock->lights;
    while (object != NULL && object->OriginalIndex != Index) object = object->next;

    /* If we didn't find it in the list of lights, time to add it */
    if (object == NULL) {
        PLIGHTINFOEL *insertAt,*prevPos;

        object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(PLIGHTINFOEL));
        if (NULL == object) {
            return WINED3DERR_OUTOFVIDEOMEMORY;
        }
        object->OriginalIndex = Index;
        object->glIndex = -1;

        /* Add it to the front of list with the idea that lights will be changed as needed
           BUT after any lights currently assigned GL indexes                             */
        insertAt = This->stateBlock->lights;
        prevPos  = NULL;
        while (insertAt != NULL && insertAt->glIndex != -1) {
            prevPos  = insertAt;
            insertAt = insertAt->next;
        }

        if (insertAt == NULL && prevPos == NULL) { /* Start of list */
            This->stateBlock->lights = object;
        } else if (insertAt == NULL) { /* End of list */
            prevPos->next = object;
            object->prev = prevPos;
        } else { /* Middle of chain */
            if (prevPos == NULL) {
                This->stateBlock->lights = object;
            } else {
                prevPos->next = object;
            }
            object->prev = prevPos;
            object->next = insertAt;
            insertAt->prev = object;
        }
    }

    /* Initialize the object */
    TRACE("Light %d setting to type %d, Diffuse(%f,%f,%f,%f), Specular(%f,%f,%f,%f), Ambient(%f,%f,%f,%f)\n", Index, pLight->Type,
          pLight->Diffuse.r, pLight->Diffuse.g, pLight->Diffuse.b, pLight->Diffuse.a,
          pLight->Specular.r, pLight->Specular.g, pLight->Specular.b, pLight->Specular.a,
          pLight->Ambient.r, pLight->Ambient.g, pLight->Ambient.b, pLight->Ambient.a);
    TRACE("... Pos(%f,%f,%f), Dirn(%f,%f,%f)\n", pLight->Position.x, pLight->Position.y, pLight->Position.z,
          pLight->Direction.x, pLight->Direction.y, pLight->Direction.z);
    TRACE("... Range(%f), Falloff(%f), Theta(%f), Phi(%f)\n", pLight->Range, pLight->Falloff, pLight->Theta, pLight->Phi);

    /* Save away the information */
    memcpy(&object->OriginalParms, pLight, sizeof(WINED3DLIGHT));

    switch (pLight->Type) {
    case WINED3DLIGHT_POINT:
        /* Position */
        object->lightPosn[0] = pLight->Position.x;
        object->lightPosn[1] = pLight->Position.y;
        object->lightPosn[2] = pLight->Position.z;
        object->lightPosn[3] = 1.0f;
        object->cutoff = 180.0f;
        /* FIXME: Range */
        break;

    case WINED3DLIGHT_DIRECTIONAL:
        /* Direction */
        object->lightPosn[0] = -pLight->Direction.x;
        object->lightPosn[1] = -pLight->Direction.y;
        object->lightPosn[2] = -pLight->Direction.z;
        object->lightPosn[3] = 0.0;
        object->exponent     = 0.0f;
        object->cutoff       = 180.0f;
        break;

    case WINED3DLIGHT_SPOT:
        /* Position */
        object->lightPosn[0] = pLight->Position.x;
        object->lightPosn[1] = pLight->Position.y;
        object->lightPosn[2] = pLight->Position.z;
        object->lightPosn[3] = 1.0;

        /* Direction */
        object->lightDirn[0] = pLight->Direction.x;
        object->lightDirn[1] = pLight->Direction.y;
        object->lightDirn[2] = pLight->Direction.z;
        object->lightDirn[3] = 1.0;

        /*
         * opengl-ish and d3d-ish spot lights use too different models for the
         * light "intensity" as a function of the angle towards the main light direction,
         * so we only can approximate very roughly.
         * however spot lights are rather rarely used in games (if ever used at all).
         * furthermore if still used, probably nobody pays attention to such details.
         */
        if (pLight->Falloff == 0) {
            rho = 6.28f;
        } else {
            rho = pLight->Theta + (pLight->Phi - pLight->Theta)/(2*pLight->Falloff);
        }
        if (rho < 0.0001) rho = 0.0001f;
        object->exponent = -0.3/log(cos(rho/2));
	if (object->exponent > 128.0) {
		object->exponent = 128.0;
	}
        object->cutoff = pLight->Phi*90/M_PI;

        /* FIXME: Range */
        break;

    default:
        FIXME("Unrecognized light type %d\n", pLight->Type);
    }

    /* Update the live definitions if the light is currently assigned a glIndex */
    if (object->glIndex != -1) {
        setup_light(iface, object->glIndex, object);
    }
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetLight(IWineD3DDevice *iface, DWORD Index, WINED3DLIGHT* pLight) {
    PLIGHTINFOEL *lightInfo = NULL;
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : Idx(%d), pLight(%p)\n", This, Index, pLight);

    /* Locate the light in the live lights */
    lightInfo = This->stateBlock->lights;
    while (lightInfo != NULL && lightInfo->OriginalIndex != Index) lightInfo = lightInfo->next;

    if (lightInfo == NULL) {
        TRACE("Light information requested but light not defined\n");
        return WINED3DERR_INVALIDCALL;
    }

    memcpy(pLight, &lightInfo->OriginalParms, sizeof(WINED3DLIGHT));
    return WINED3D_OK;
}

/*****
 * Get / Set Light Enable
 *   (Note for consistency, renamed d3dx function by adding the 'set' prefix)
 *****/
static HRESULT WINAPI IWineD3DDeviceImpl_SetLightEnable(IWineD3DDevice *iface, DWORD Index, BOOL Enable) {
    PLIGHTINFOEL *lightInfo = NULL;
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : Idx(%d), enable? %d\n", This, Index, Enable);

    /* Tests show true = 128...not clear why */

    Enable = Enable? 128: 0;

    /* If recording state block, just add to end of lights chain with changedEnable set to true */
    if (This->isRecordingState) {
        lightInfo = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(PLIGHTINFOEL));
        if (NULL == lightInfo) {
            return WINED3DERR_OUTOFVIDEOMEMORY;
        }
        lightInfo->OriginalIndex = Index;
        lightInfo->glIndex = -1;
        lightInfo->enabledChanged = TRUE;
        lightInfo->lightEnabled = Enable;

        /* Add to the END of the chain of lights changes to be replayed */
        if (This->updateStateBlock->lights == NULL) {
            This->updateStateBlock->lights = lightInfo;
        } else {
            PLIGHTINFOEL *temp = This->updateStateBlock->lights;
            while (temp->next != NULL) temp=temp->next;
            temp->next = lightInfo;
        }
        TRACE("Recording... not performing anything more\n");
        return WINED3D_OK;
    }

    /* Not recording... So, locate the light in the live lights */
    lightInfo = This->stateBlock->lights;
    while (lightInfo != NULL && lightInfo->OriginalIndex != Index) lightInfo = lightInfo->next;

    /* Special case - enabling an undefined light creates one with a strict set of parms! */
    if (lightInfo == NULL) {

        TRACE("Light enabled requested but light not defined, so defining one!\n");
        IWineD3DDeviceImpl_SetLight(iface, Index, &WINED3D_default_light);

        /* Search for it again! Should be fairly quick as near head of list */
        lightInfo = This->stateBlock->lights;
        while (lightInfo != NULL && lightInfo->OriginalIndex != Index) lightInfo = lightInfo->next;
        if (lightInfo == NULL) {
            FIXME("Adding default lights has failed dismally\n");
            return WINED3DERR_INVALIDCALL;
        }
    }

    /* OK, we now have a light... */
    if (!Enable) {

        /* If we are disabling it, check it was enabled, and
           still only do something if it has assigned a glIndex (which it should have!)   */
        if ((lightInfo->lightEnabled) && (lightInfo->glIndex != -1)) {
            TRACE("Disabling light set up at gl idx %d\n", lightInfo->glIndex);
            ENTER_GL();
            glDisable(GL_LIGHT0 + lightInfo->glIndex);
            checkGLcall("glDisable GL_LIGHT0+Index");
            LEAVE_GL();
        } else {
            TRACE("Nothing to do as light was not enabled\n");
        }
        lightInfo->lightEnabled = Enable;
    } else {

        /* We are enabling it. If it is enabled, it's really simple */
        if (lightInfo->lightEnabled) {
            /* nop */
            TRACE("Nothing to do as light was enabled\n");

        /* If it already has a glIndex, it's still simple */
        } else if (lightInfo->glIndex != -1) {
            TRACE("Reusing light as already set up at gl idx %d\n", lightInfo->glIndex);
            lightInfo->lightEnabled = Enable;
            ENTER_GL();
            glEnable(GL_LIGHT0 + lightInfo->glIndex);
            checkGLcall("glEnable GL_LIGHT0+Index already setup");
            LEAVE_GL();

        /* Otherwise got to find space - lights are ordered gl indexes first */
        } else {
            PLIGHTINFOEL *bsf  = NULL;
            PLIGHTINFOEL *pos  = This->stateBlock->lights;
            PLIGHTINFOEL *prev = NULL;
            int           Index= 0;
            int           glIndex = -1;

            /* Try to minimize changes as much as possible */
            while (pos != NULL && pos->glIndex != -1 && Index < This->maxConcurrentLights) {

                /* Try to remember which index can be replaced if necessary */
                if (bsf==NULL && !pos->lightEnabled) {
                    /* Found a light we can replace, save as best replacement */
                    bsf = pos;
                }

                /* Step to next space */
                prev = pos;
                pos = pos->next;
                Index ++;
            }

            /* If we have too many active lights, fail the call */
            if ((Index == This->maxConcurrentLights) && (bsf == NULL)) {
                FIXME("Program requests too many concurrent lights\n");
                return WINED3DERR_INVALIDCALL;

            /* If we have allocated all lights, but not all are enabled,
               reuse one which is not enabled                           */
            } else if (Index == This->maxConcurrentLights) {
                /* use bsf - Simply swap the new light and the BSF one */
                PLIGHTINFOEL *bsfNext = bsf->next;
                PLIGHTINFOEL *bsfPrev = bsf->prev;

                /* Sort out ends */
                if (lightInfo->next != NULL) lightInfo->next->prev = bsf;
                if (bsf->prev != NULL) {
                    bsf->prev->next = lightInfo;
                } else {
                    This->stateBlock->lights = lightInfo;
                }

                /* If not side by side, lots of chains to update */
                if (bsf->next != lightInfo) {
                    lightInfo->prev->next = bsf;
                    bsf->next->prev = lightInfo;
                    bsf->next       = lightInfo->next;
                    bsf->prev       = lightInfo->prev;
                    lightInfo->next = bsfNext;
                    lightInfo->prev = bsfPrev;

                } else {
                    /* Simple swaps */
                    bsf->prev = lightInfo;
                    bsf->next = lightInfo->next;
                    lightInfo->next = bsf;
                    lightInfo->prev = bsfPrev;
                }


                /* Update states */
                glIndex = bsf->glIndex;
                bsf->glIndex = -1;
                lightInfo->glIndex = glIndex;
                lightInfo->lightEnabled = Enable;

                /* Finally set up the light in gl itself */
                TRACE("Replacing light which was set up at gl idx %d\n", lightInfo->glIndex);
                ENTER_GL();
                setup_light(iface, glIndex, lightInfo);
                glEnable(GL_LIGHT0 + glIndex);
                checkGLcall("glEnable GL_LIGHT0 new setup");
                LEAVE_GL();

            /* If we reached the end of the allocated lights, with space in the
               gl lights, setup a new light                                     */
            } else if (pos->glIndex == -1) {

                /* We reached the end of the allocated gl lights, so already
                    know the index of the next one!                          */
                glIndex = Index;
                lightInfo->glIndex = glIndex;
                lightInfo->lightEnabled = Enable;

                /* In an ideal world, it's already in the right place */
                if (lightInfo->prev == NULL || lightInfo->prev->glIndex!=-1) {
                   /* No need to move it */
                } else {
                    /* Remove this light from the list */
                    lightInfo->prev->next = lightInfo->next;
                    if (lightInfo->next != NULL) {
                        lightInfo->next->prev = lightInfo->prev;
                    }

                    /* Add in at appropriate place (inbetween prev and pos) */
                    lightInfo->prev = prev;
                    lightInfo->next = pos;
                    if (prev == NULL) {
                        This->stateBlock->lights = lightInfo;
                    } else {
                        prev->next = lightInfo;
                    }
                    if (pos != NULL) {
                        pos->prev = lightInfo;
                    }
                }

                /* Finally set up the light in gl itself */
                TRACE("Defining new light at gl idx %d\n", lightInfo->glIndex);
                ENTER_GL();
                setup_light(iface, glIndex, lightInfo);
                glEnable(GL_LIGHT0 + glIndex);
                checkGLcall("glEnable GL_LIGHT0 new setup");
                LEAVE_GL();

            }
        }
    }
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetLightEnable(IWineD3DDevice *iface, DWORD Index,BOOL* pEnable) {

    PLIGHTINFOEL *lightInfo = NULL;
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : for idx(%d)\n", This, Index);

    /* Locate the light in the live lights */
    lightInfo = This->stateBlock->lights;
    while (lightInfo != NULL && lightInfo->OriginalIndex != Index) lightInfo = lightInfo->next;

    if (lightInfo == NULL) {
        TRACE("Light enabled state requested but light not defined\n");
        return WINED3DERR_INVALIDCALL;
    }
    *pEnable = lightInfo->lightEnabled;
    return WINED3D_OK;
}

/*****
 * Get / Set Clip Planes
 *****/
static HRESULT WINAPI IWineD3DDeviceImpl_SetClipPlane(IWineD3DDevice *iface, DWORD Index, CONST float *pPlane) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : for idx %d, %p\n", This, Index, pPlane);

    /* Validate Index */
    if (Index >= GL_LIMITS(clipplanes)) {
        TRACE("Application has requested clipplane this device doesn't support\n");
        return WINED3DERR_INVALIDCALL;
    }

    This->updateStateBlock->changed.clipplane[Index] = TRUE;
    This->updateStateBlock->set.clipplane[Index] = TRUE;
    This->updateStateBlock->clipplane[Index][0] = pPlane[0];
    This->updateStateBlock->clipplane[Index][1] = pPlane[1];
    This->updateStateBlock->clipplane[Index][2] = pPlane[2];
    This->updateStateBlock->clipplane[Index][3] = pPlane[3];

    /* Handle recording of state blocks */
    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return WINED3D_OK;
    }

    /* Apply it */

    ENTER_GL();

    /* Clip Plane settings are affected by the model view in OpenGL, the View transform in direct3d */
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadMatrixf((float *) &This->stateBlock->transforms[WINED3DTS_VIEW].u.m[0][0]);

    TRACE("Clipplane [%f,%f,%f,%f]\n",
          This->updateStateBlock->clipplane[Index][0],
          This->updateStateBlock->clipplane[Index][1],
          This->updateStateBlock->clipplane[Index][2],
          This->updateStateBlock->clipplane[Index][3]);
    glClipPlane(GL_CLIP_PLANE0 + Index, This->updateStateBlock->clipplane[Index]);
    checkGLcall("glClipPlane");

    glPopMatrix();
    LEAVE_GL();

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetClipPlane(IWineD3DDevice *iface, DWORD Index, float *pPlane) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : for idx %d\n", This, Index);

    /* Validate Index */
    if (Index >= GL_LIMITS(clipplanes)) {
        TRACE("Application has requested clipplane this device doesn't support\n");
        return WINED3DERR_INVALIDCALL;
    }

    pPlane[0] = This->stateBlock->clipplane[Index][0];
    pPlane[1] = This->stateBlock->clipplane[Index][1];
    pPlane[2] = This->stateBlock->clipplane[Index][2];
    pPlane[3] = This->stateBlock->clipplane[Index][3];
    return WINED3D_OK;
}

/*****
 * Get / Set Clip Plane Status
 *   WARNING: This code relies on the fact that D3DCLIPSTATUS8 == D3DCLIPSTATUS9
 *****/
static HRESULT  WINAPI  IWineD3DDeviceImpl_SetClipStatus(IWineD3DDevice *iface, CONST WINED3DCLIPSTATUS* pClipStatus) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    FIXME("(%p) : stub\n", This);
    if (NULL == pClipStatus) {
      return WINED3DERR_INVALIDCALL;
    }
    This->updateStateBlock->clip_status.ClipUnion = pClipStatus->ClipUnion;
    This->updateStateBlock->clip_status.ClipIntersection = pClipStatus->ClipIntersection;
    return WINED3D_OK;
}

static HRESULT  WINAPI  IWineD3DDeviceImpl_GetClipStatus(IWineD3DDevice *iface, WINED3DCLIPSTATUS* pClipStatus) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    FIXME("(%p) : stub\n", This);
    if (NULL == pClipStatus) {
      return WINED3DERR_INVALIDCALL;
    }
    pClipStatus->ClipUnion = This->updateStateBlock->clip_status.ClipUnion;
    pClipStatus->ClipIntersection = This->updateStateBlock->clip_status.ClipIntersection;
    return WINED3D_OK;
}

/*****
 * Get / Set Material
 *****/
static HRESULT WINAPI IWineD3DDeviceImpl_SetMaterial(IWineD3DDevice *iface, CONST WINED3DMATERIAL* pMaterial) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    This->updateStateBlock->changed.material = TRUE;
    This->updateStateBlock->set.material = TRUE;
    memcpy(&This->updateStateBlock->material, pMaterial, sizeof(WINED3DMATERIAL));

    /* Handle recording of state blocks */
    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return WINED3D_OK;
    }

    ENTER_GL();
    TRACE("(%p) : Diffuse (%f,%f,%f,%f)\n", This, pMaterial->Diffuse.r, pMaterial->Diffuse.g,
        pMaterial->Diffuse.b, pMaterial->Diffuse.a);
    TRACE("(%p) : Ambient (%f,%f,%f,%f)\n", This, pMaterial->Ambient.r, pMaterial->Ambient.g,
        pMaterial->Ambient.b, pMaterial->Ambient.a);
    TRACE("(%p) : Specular (%f,%f,%f,%f)\n", This, pMaterial->Specular.r, pMaterial->Specular.g,
        pMaterial->Specular.b, pMaterial->Specular.a);
    TRACE("(%p) : Emissive (%f,%f,%f,%f)\n", This, pMaterial->Emissive.r, pMaterial->Emissive.g,
        pMaterial->Emissive.b, pMaterial->Emissive.a);
    TRACE("(%p) : Power (%f)\n", This, pMaterial->Power);

    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, (float*) &This->updateStateBlock->material.Ambient);
    checkGLcall("glMaterialfv(GL_AMBIENT)");
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, (float*) &This->updateStateBlock->material.Diffuse);
    checkGLcall("glMaterialfv(GL_DIFFUSE)");

    /* Only change material color if specular is enabled, otherwise it is set to black */
    if (This->stateBlock->renderState[WINED3DRS_SPECULARENABLE]) {
       glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, (float*) &This->updateStateBlock->material.Specular);
       checkGLcall("glMaterialfv(GL_SPECULAR");
    } else {
       float black[4] = {0.0f, 0.0f, 0.0f, 0.0f};
       glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, &black[0]);
       checkGLcall("glMaterialfv(GL_SPECULAR");
    }
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, (float*) &This->updateStateBlock->material.Emissive);
    checkGLcall("glMaterialfv(GL_EMISSION)");
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, This->updateStateBlock->material.Power);
    checkGLcall("glMaterialf(GL_SHININESS");

    LEAVE_GL();
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetMaterial(IWineD3DDevice *iface, WINED3DMATERIAL* pMaterial) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    memcpy(pMaterial, &This->updateStateBlock->material, sizeof (WINED3DMATERIAL));
    TRACE("(%p) : Diffuse (%f,%f,%f,%f)\n", This, pMaterial->Diffuse.r, pMaterial->Diffuse.g,
        pMaterial->Diffuse.b, pMaterial->Diffuse.a);
    TRACE("(%p) : Ambient (%f,%f,%f,%f)\n", This, pMaterial->Ambient.r, pMaterial->Ambient.g,
        pMaterial->Ambient.b, pMaterial->Ambient.a);
    TRACE("(%p) : Specular (%f,%f,%f,%f)\n", This, pMaterial->Specular.r, pMaterial->Specular.g,
        pMaterial->Specular.b, pMaterial->Specular.a);
    TRACE("(%p) : Emissive (%f,%f,%f,%f)\n", This, pMaterial->Emissive.r, pMaterial->Emissive.g,
        pMaterial->Emissive.b, pMaterial->Emissive.a);
    TRACE("(%p) : Power (%f)\n", This, pMaterial->Power);

    return WINED3D_OK;
}

/*****
 * Get / Set Indices
 *****/
static HRESULT WINAPI IWineD3DDeviceImpl_SetIndices(IWineD3DDevice *iface, IWineD3DIndexBuffer* pIndexData,
                                             UINT BaseVertexIndex) {
    IWineD3DDeviceImpl  *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DIndexBuffer *oldIdxs;
    UINT oldBaseIndex = This->updateStateBlock->baseVertexIndex;

    TRACE("(%p) : Setting to %p, base %d\n", This, pIndexData, BaseVertexIndex);
    oldIdxs = This->updateStateBlock->pIndexData;

    This->updateStateBlock->changed.indices = TRUE;
    This->updateStateBlock->set.indices = TRUE;
    This->updateStateBlock->pIndexData = pIndexData;
    This->updateStateBlock->baseVertexIndex = BaseVertexIndex;

    /* Handle recording of state blocks */
    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return WINED3D_OK;
    }

    /* So far only the base vertex index is tracked */
    if(BaseVertexIndex != oldBaseIndex) {
        IWineD3DDeviceImpl_MarkStateDirty(This, STATE_STREAMSRC);
    }
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetIndices(IWineD3DDevice *iface, IWineD3DIndexBuffer** ppIndexData, UINT* pBaseVertexIndex) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    *ppIndexData = This->stateBlock->pIndexData;

    /* up ref count on ppindexdata */
    if (*ppIndexData) {
        IWineD3DIndexBuffer_AddRef(*ppIndexData);
        *pBaseVertexIndex = This->stateBlock->baseVertexIndex;
        TRACE("(%p) index data set to %p + %u\n", This, ppIndexData, This->stateBlock->baseVertexIndex);
    }else{
        TRACE("(%p) No index data set\n", This);
    }
    TRACE("Returning %p %d\n", *ppIndexData, *pBaseVertexIndex);

    return WINED3D_OK;
}

/* Method to offer d3d9 a simple way to set the base vertex index without messing with the index buffer */
static HRESULT WINAPI IWineD3DDeviceImpl_SetBasevertexIndex(IWineD3DDevice *iface, UINT BaseIndex) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p)->(%d)\n", This, BaseIndex);

    if(This->updateStateBlock->baseVertexIndex == BaseIndex) {
        TRACE("Application is setting the old value over, nothing to do\n");
        return WINED3D_OK;
    }

    This->updateStateBlock->baseVertexIndex = BaseIndex;

    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return WINED3D_OK;
    }
    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_STREAMSRC);
    return WINED3D_OK;
}

/*****
 * Get / Set Viewports
 *****/
static HRESULT WINAPI IWineD3DDeviceImpl_SetViewport(IWineD3DDevice *iface, CONST WINED3DVIEWPORT* pViewport) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    TRACE("(%p)\n", This);
    This->updateStateBlock->changed.viewport = TRUE;
    This->updateStateBlock->set.viewport = TRUE;
    memcpy(&This->updateStateBlock->viewport, pViewport, sizeof(WINED3DVIEWPORT));

    /* Handle recording of state blocks */
    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return WINED3D_OK;
    }

    TRACE("(%p) : x=%d, y=%d, wid=%d, hei=%d, minz=%f, maxz=%f\n", This,
          pViewport->X, pViewport->Y, pViewport->Width, pViewport->Height, pViewport->MinZ, pViewport->MaxZ);

    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_VIEWPORT);
    return WINED3D_OK;

}

static HRESULT WINAPI IWineD3DDeviceImpl_GetViewport(IWineD3DDevice *iface, WINED3DVIEWPORT* pViewport) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p)\n", This);
    memcpy(pViewport, &This->stateBlock->viewport, sizeof(WINED3DVIEWPORT));
    return WINED3D_OK;
}

/*****
 * Get / Set Render States
 * TODO: Verify against dx9 definitions
 *****/
static HRESULT WINAPI IWineD3DDeviceImpl_SetRenderState(IWineD3DDevice *iface, WINED3DRENDERSTATETYPE State, DWORD Value) {

    IWineD3DDeviceImpl  *This     = (IWineD3DDeviceImpl *)iface;
    DWORD oldValue = This->stateBlock->renderState[State];

    TRACE("(%p)->state = %s(%d), value = %d\n", This, debug_d3drenderstate(State), State, Value);

    This->updateStateBlock->changed.renderState[State] = TRUE;
    This->updateStateBlock->set.renderState[State] = TRUE;
    This->updateStateBlock->renderState[State] = Value;

    /* Handle recording of state blocks */
    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return WINED3D_OK;
    }

    /* Compared here and not before the assignment to allow proper stateblock recording */
    if(Value == oldValue) {
        TRACE("Application is setting the old value over, nothing to do\n");
    } else {
        IWineD3DDeviceImpl_MarkStateDirty(This, STATE_RENDER(State));
    }

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetRenderState(IWineD3DDevice *iface, WINED3DRENDERSTATETYPE State, DWORD *pValue) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) for State %d = %d\n", This, State, This->stateBlock->renderState[State]);
    *pValue = This->stateBlock->renderState[State];
    return WINED3D_OK;
}

/*****
 * Get / Set Sampler States
 * TODO: Verify against dx9 definitions
 *****/

static HRESULT WINAPI IWineD3DDeviceImpl_SetSamplerState(IWineD3DDevice *iface, DWORD Sampler, WINED3DSAMPLERSTATETYPE Type, DWORD Value) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    DWORD oldValue = This->stateBlock->samplerState[Sampler][Type];

    /**
    * SetSampler is designed to allow for more than the standard up to 8 textures
    *  and Geforce has stopped supporting more than 6 standard textures in openGL.
    * So I have to use ARB for Gforce. (maybe if the sampler > 4 then use ARB?)
    *
    * http://developer.nvidia.com/object/General_FAQ.html#t6
    *
    * There are two new settings for GForce
    * the sampler one:
    * GL_MAX_TEXTURE_IMAGE_UNITS_ARB
    * and the texture one:
    * GL_MAX_TEXTURE_COORDS_ARB.
    * Ok GForce say it's ok to use glTexParameter/glGetTexParameter(...).
     ******************/
    /** NOTE: States are applied in IWineD3DBaseTextre ApplyStateChanges the sampler state handler**/
    if(Sampler >  GL_LIMITS(sampler_stages) || Sampler < 0 || Type > WINED3D_HIGHEST_SAMPLER_STATE || Type < 0) {
         FIXME("sampler %d type %s(%u) is out of range [max_samplers=%d, highest_state=%d]\n",
            Sampler, debug_d3dsamplerstate(Type), Type, GL_LIMITS(sampler_stages), WINED3D_HIGHEST_SAMPLER_STATE);
        return WINED3DERR_INVALIDCALL;
    }

    TRACE("(%p) : Sampler=%d, Type=%s(%d), Value=%d\n", This, Sampler,
        debug_d3dsamplerstate(Type), Type, Value);
    This->updateStateBlock->samplerState[Sampler][Type]         = Value;
    This->updateStateBlock->set.samplerState[Sampler][Type]     = Value;
    This->updateStateBlock->changed.samplerState[Sampler][Type] = Value;

    /* Handle recording of state blocks */
    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return WINED3D_OK;
    }

    if(oldValue == Value) {
        TRACE("Application is setting the old value over, nothing to do\n");
        return WINED3D_OK;
    }

    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_SAMPLER(Sampler));

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetSamplerState(IWineD3DDevice *iface, DWORD Sampler, WINED3DSAMPLERSTATETYPE Type, DWORD* Value) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    /** TODO: check that sampler is in  range **/
    *Value = This->stateBlock->samplerState[Sampler][Type];
    TRACE("(%p) : Sampler %d Type %u Returning %d\n", This, Sampler, Type, *Value);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetScissorRect(IWineD3DDevice *iface, CONST RECT* pRect) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    RECT windowRect;
    UINT winHeight;

    This->updateStateBlock->set.scissorRect = TRUE;
    This->updateStateBlock->changed.scissorRect = TRUE;
    memcpy(&This->updateStateBlock->scissorRect, pRect, sizeof(*pRect));

    if(This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return WINED3D_OK;
    }

    GetClientRect(((IWineD3DSwapChainImpl *)This->swapchains[0])->win_handle, &windowRect);
    /* Warning: glScissor uses window coordinates, not viewport coordinates, so our viewport correction does not apply
    * Warning2: Even in windowed mode the coords are relative to the window, not the screen
    */
    winHeight = windowRect.bottom - windowRect.top;
    TRACE("(%p)Setting new Scissor Rect to %d:%d-%d:%d\n", This, pRect->left, pRect->bottom - winHeight,
          pRect->right - pRect->left, pRect->bottom - pRect->top);
    ENTER_GL();
    glScissor(pRect->left, winHeight - pRect->bottom, pRect->right - pRect->left, pRect->bottom - pRect->top);
    checkGLcall("glScissor");
    LEAVE_GL();

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetScissorRect(IWineD3DDevice *iface, RECT* pRect) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    memcpy(pRect, &This->updateStateBlock->scissorRect, sizeof(pRect));
    TRACE("(%p)Returning a Scissor Rect of %d:%d-%d:%d\n", This, pRect->left, pRect->top, pRect->right, pRect->bottom);
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetVertexDeclaration(IWineD3DDevice* iface, IWineD3DVertexDeclaration* pDecl) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;
    IWineD3DVertexDeclaration *oldDecl = This->updateStateBlock->vertexDecl;

    TRACE("(%p) : pDecl=%p\n", This, pDecl);

    This->updateStateBlock->vertexDecl = pDecl;
    This->updateStateBlock->changed.vertexDecl = TRUE;
    This->updateStateBlock->set.vertexDecl = TRUE;

    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return WINED3D_OK;
    } else if(pDecl == oldDecl) {
        /* Checked after the assignment to allow proper stateblock recording */
        TRACE("Application is setting the old declaration over, nothing to do\n");
        return WINED3D_OK;
    }

    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_VDECL);
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetVertexDeclaration(IWineD3DDevice* iface, IWineD3DVertexDeclaration** ppDecl) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    TRACE("(%p) : ppDecl=%p\n", This, ppDecl);

    *ppDecl = This->stateBlock->vertexDecl;
    if (NULL != *ppDecl) IWineD3DVertexDeclaration_AddRef(*ppDecl);
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetVertexShader(IWineD3DDevice *iface, IWineD3DVertexShader* pShader) {
    IWineD3DDeviceImpl *This        = (IWineD3DDeviceImpl *)iface;
    IWineD3DVertexShader* oldShader = This->updateStateBlock->vertexShader;

    This->updateStateBlock->vertexShader         = pShader;
    This->updateStateBlock->changed.vertexShader = TRUE;
    This->updateStateBlock->set.vertexShader     = TRUE;

    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return WINED3D_OK;
    } else if(oldShader == pShader) {
        /* Checked here to allow proper stateblock recording */
        TRACE("App is setting the old shader over, nothing to do\n");
        return WINED3D_OK;
    }

    TRACE("(%p) : setting pShader(%p)\n", This, pShader);

    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_VSHADER);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetVertexShader(IWineD3DDevice *iface, IWineD3DVertexShader** ppShader) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    if (NULL == ppShader) {
        return WINED3DERR_INVALIDCALL;
    }
    *ppShader = This->stateBlock->vertexShader;
    if( NULL != *ppShader)
        IWineD3DVertexShader_AddRef(*ppShader);

    TRACE("(%p) : returning %p\n", This, *ppShader);
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetVertexShaderConstantB(
    IWineD3DDevice *iface,
    UINT start,
    CONST BOOL *srcData,
    UINT count) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    int i, cnt = min(count, MAX_CONST_B - start);

    TRACE("(iface %p, srcData %p, start %d, count %d)\n",
            iface, srcData, start, count);

    if (srcData == NULL || cnt < 0)
        return WINED3DERR_INVALIDCALL;

    memcpy(&This->updateStateBlock->vertexShaderConstantB[start], srcData, cnt * sizeof(BOOL));
    for (i = 0; i < cnt; i++)
        TRACE("Set BOOL constant %u to %s\n", start + i, srcData[i]? "true":"false");

    for (i = start; i < cnt + start; ++i) {
        This->updateStateBlock->changed.vertexShaderConstantsB[i] = TRUE;
        This->updateStateBlock->set.vertexShaderConstantsB[i]     = TRUE;
    }

    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_VERTEXSHADERCONSTANT);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetVertexShaderConstantB(
    IWineD3DDevice *iface,
    UINT start,
    BOOL *dstData,
    UINT count) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    int cnt = min(count, MAX_CONST_B - start);

    TRACE("(iface %p, dstData %p, start %d, count %d)\n",
            iface, dstData, start, count);

    if (dstData == NULL || cnt < 0)
        return WINED3DERR_INVALIDCALL;

    memcpy(dstData, &This->stateBlock->vertexShaderConstantB[start], cnt * sizeof(BOOL));
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetVertexShaderConstantI(
    IWineD3DDevice *iface,
    UINT start,
    CONST int *srcData,
    UINT count) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    int i, cnt = min(count, MAX_CONST_I - start);

    TRACE("(iface %p, srcData %p, start %d, count %d)\n",
            iface, srcData, start, count);

    if (srcData == NULL || cnt < 0)
        return WINED3DERR_INVALIDCALL;

    memcpy(&This->updateStateBlock->vertexShaderConstantI[start * 4], srcData, cnt * sizeof(int) * 4);
    for (i = 0; i < cnt; i++)
        TRACE("Set INT constant %u to { %d, %d, %d, %d }\n", start + i,
           srcData[i*4], srcData[i*4+1], srcData[i*4+2], srcData[i*4+3]);

    for (i = start; i < cnt + start; ++i) {
        This->updateStateBlock->changed.vertexShaderConstantsI[i] = TRUE;
        This->updateStateBlock->set.vertexShaderConstantsI[i]     = TRUE;
    }

    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_VERTEXSHADERCONSTANT);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetVertexShaderConstantI(
    IWineD3DDevice *iface,
    UINT start,
    int *dstData,
    UINT count) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    int cnt = min(count, MAX_CONST_I - start);

    TRACE("(iface %p, dstData %p, start %d, count %d)\n",
            iface, dstData, start, count);

    if (dstData == NULL || ((signed int) MAX_CONST_I - (signed int) start) <= (signed int) 0)
        return WINED3DERR_INVALIDCALL;

    memcpy(dstData, &This->stateBlock->vertexShaderConstantI[start * 4], cnt * sizeof(int) * 4);
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetVertexShaderConstantF(
    IWineD3DDevice *iface,
    UINT start,
    CONST float *srcData,
    UINT count) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    int i, cnt = min(count, GL_LIMITS(vshader_constantsF) - start);

    TRACE("(iface %p, srcData %p, start %d, count %d)\n",
            iface, srcData, start, count);

    if (srcData == NULL || ((signed int) GL_LIMITS(vshader_constantsF) - (signed int) start) <= (signed int) 0)
        return WINED3DERR_INVALIDCALL;

    memcpy(&This->updateStateBlock->vertexShaderConstantF[start * 4], srcData, cnt * sizeof(float) * 4);
    for (i = 0; i < cnt; i++)
        TRACE("Set FLOAT constant %u to { %f, %f, %f, %f }\n", start + i,
           srcData[i*4], srcData[i*4+1], srcData[i*4+2], srcData[i*4+3]);

    for (i = start; i < cnt + start; ++i) {
        if (!This->updateStateBlock->set.vertexShaderConstantsF[i]) {
            constant_entry *ptr = HeapAlloc(GetProcessHeap(), 0, sizeof(constant_entry));
            ptr->idx = i;
            list_add_head(&This->updateStateBlock->set_vconstantsF, &ptr->entry);
            This->updateStateBlock->set.vertexShaderConstantsF[i] = TRUE;
        }
        This->updateStateBlock->changed.vertexShaderConstantsF[i] = TRUE;
    }

    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_VERTEXSHADERCONSTANT);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetVertexShaderConstantF(
    IWineD3DDevice *iface,
    UINT start,
    float *dstData,
    UINT count) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    int cnt = min(count, GL_LIMITS(vshader_constantsF) - start);

    TRACE("(iface %p, dstData %p, start %d, count %d)\n",
            iface, dstData, start, count);

    if (dstData == NULL || cnt < 0)
        return WINED3DERR_INVALIDCALL;

    memcpy(dstData, &This->stateBlock->vertexShaderConstantF[start * 4], cnt * sizeof(float) * 4);
    return WINED3D_OK;
}

static inline void markTextureStagesDirty(IWineD3DDeviceImpl *This, DWORD stage) {
    DWORD i;
    for(i = 0; i < WINED3D_HIGHEST_TEXTURE_STATE; i++) {
        IWineD3DDeviceImpl_MarkStateDirty(This, STATE_TEXTURESTAGE(stage, i));
    }
}

static void IWineD3DDeviceImpl_FindTexUnitMap(IWineD3DDeviceImpl *This) {
    DWORD i, tex;
    /* This code can assume that GL_NV_register_combiners are supported, otherwise
     * it is never called.
     *
     * Rules are:
     * -> Pixel shaders need a 1:1 map. In theory the shader input could be mapped too, but
     * that would be really messy and require shader recompilation
     * -> When the mapping of a stage is changed, sampler and ALL texture stage states have
     * to be reset. Because of that try to work with a 1:1 mapping as much as possible
     * -> Whith a 1:1 mapping oneToOneTexUnitMap is set to avoid checking MAX_SAMPLERS array
     * entries to make pixel shaders cheaper. MAX_SAMPLERS will be 128 in dx10
     */
    if(This->stateBlock->pixelShader || This->stateBlock->lowest_disabled_stage <= GL_LIMITS(textures)) {
        if(This->oneToOneTexUnitMap) {
            TRACE("Not touching 1:1 map\n");
            return;
        }
        TRACE("Restoring 1:1 texture unit mapping\n");
        /* Restore a 1:1 mapping */
        for(i = 0; i < MAX_SAMPLERS; i++) {
            if(This->texUnitMap[i] != i) {
                This->texUnitMap[i] = i;
                IWineD3DDeviceImpl_MarkStateDirty(This, STATE_SAMPLER(i));
                markTextureStagesDirty(This, i);
            }
        }
        This->oneToOneTexUnitMap = TRUE;
        return;
    } else {
        /* No pixel shader, and we do not have enough texture units available. Try to skip NULL textures
         * First, see if we can succeed at all
         */
        tex = 0;
        for(i = 0; i < This->stateBlock->lowest_disabled_stage; i++) {
            if(This->stateBlock->textures[i] == NULL) tex++;
        }

        if(GL_LIMITS(textures) + tex < This->stateBlock->lowest_disabled_stage) {
            FIXME("Too many bound textures to support the combiner settings\n");
            return;
        }

        /* Now work out the mapping */
        tex = 0;
        This->oneToOneTexUnitMap = FALSE;
        WARN("Non 1:1 mapping UNTESTED!\n");
        for(i = 0; i < This->stateBlock->lowest_disabled_stage; i++) {
            /* Skip NULL textures */
            if (!This->stateBlock->textures[i]) {
                /* Map to -1, so the check below doesn't fail if a non-NULL
                 * texture is set on this stage */
                TRACE("Mapping texture stage %d to -1\n", i);
                This->texUnitMap[i] = -1;

                continue;
            }

            TRACE("Mapping texture stage %d to unit %d\n", i, tex);
            if(This->texUnitMap[i] != tex) {
                This->texUnitMap[i] = tex;
                IWineD3DDeviceImpl_MarkStateDirty(This, STATE_SAMPLER(i));
                markTextureStagesDirty(This, i);
            }

            ++tex;
        }
    }
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetPixelShader(IWineD3DDevice *iface, IWineD3DPixelShader *pShader) {
    IWineD3DDeviceImpl *This        = (IWineD3DDeviceImpl *)iface;
    IWineD3DPixelShader *oldShader  = This->updateStateBlock->pixelShader;
    This->updateStateBlock->pixelShader         = pShader;
    This->updateStateBlock->changed.pixelShader = TRUE;
    This->updateStateBlock->set.pixelShader     = TRUE;

    /* Handle recording of state blocks */
    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
    }

    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return WINED3D_OK;
    }

    if(pShader == oldShader) {
        TRACE("App is setting the old pixel shader over, nothing to do\n");
        return WINED3D_OK;
    }

    TRACE("(%p) : setting pShader(%p)\n", This, pShader);
    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_PIXELSHADER);

    /* Rebuild the texture unit mapping if nvrc's are supported */
    if(GL_SUPPORT(NV_REGISTER_COMBINERS)) {
        IWineD3DDeviceImpl_FindTexUnitMap(This);
    }

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetPixelShader(IWineD3DDevice *iface, IWineD3DPixelShader **ppShader) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    if (NULL == ppShader) {
        WARN("(%p) : PShader is NULL, returning INVALIDCALL\n", This);
        return WINED3DERR_INVALIDCALL;
    }

    *ppShader =  This->stateBlock->pixelShader;
    if (NULL != *ppShader) {
        IWineD3DPixelShader_AddRef(*ppShader);
    }
    TRACE("(%p) : returning %p\n", This, *ppShader);
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetPixelShaderConstantB(
    IWineD3DDevice *iface,
    UINT start,
    CONST BOOL *srcData,
    UINT count) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    int i, cnt = min(count, MAX_CONST_B - start);

    TRACE("(iface %p, srcData %p, start %d, count %d)\n",
            iface, srcData, start, count);

    if (srcData == NULL || cnt < 0)
        return WINED3DERR_INVALIDCALL;

    memcpy(&This->updateStateBlock->pixelShaderConstantB[start], srcData, cnt * sizeof(BOOL));
    for (i = 0; i < cnt; i++)
        TRACE("Set BOOL constant %u to %s\n", start + i, srcData[i]? "true":"false");

    for (i = start; i < cnt + start; ++i) {
        This->updateStateBlock->changed.pixelShaderConstantsB[i] = TRUE;
        This->updateStateBlock->set.pixelShaderConstantsB[i]     = TRUE;
    }

    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_PIXELSHADERCONSTANT);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetPixelShaderConstantB(
    IWineD3DDevice *iface,
    UINT start,
    BOOL *dstData,
    UINT count) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    int cnt = min(count, MAX_CONST_B - start);

    TRACE("(iface %p, dstData %p, start %d, count %d)\n",
            iface, dstData, start, count);

    if (dstData == NULL || cnt < 0)
        return WINED3DERR_INVALIDCALL;

    memcpy(dstData, &This->stateBlock->pixelShaderConstantB[start], cnt * sizeof(BOOL));
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetPixelShaderConstantI(
    IWineD3DDevice *iface,
    UINT start,
    CONST int *srcData,
    UINT count) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    int i, cnt = min(count, MAX_CONST_I - start);

    TRACE("(iface %p, srcData %p, start %d, count %d)\n",
            iface, srcData, start, count);

    if (srcData == NULL || cnt < 0)
        return WINED3DERR_INVALIDCALL;

    memcpy(&This->updateStateBlock->pixelShaderConstantI[start * 4], srcData, cnt * sizeof(int) * 4);
    for (i = 0; i < cnt; i++)
        TRACE("Set INT constant %u to { %d, %d, %d, %d }\n", start + i,
           srcData[i*4], srcData[i*4+1], srcData[i*4+2], srcData[i*4+3]);

    for (i = start; i < cnt + start; ++i) {
        This->updateStateBlock->changed.pixelShaderConstantsI[i] = TRUE;
        This->updateStateBlock->set.pixelShaderConstantsI[i]     = TRUE;
    }

    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_PIXELSHADERCONSTANT);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetPixelShaderConstantI(
    IWineD3DDevice *iface,
    UINT start,
    int *dstData,
    UINT count) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    int cnt = min(count, MAX_CONST_I - start);

    TRACE("(iface %p, dstData %p, start %d, count %d)\n",
            iface, dstData, start, count);

    if (dstData == NULL || cnt < 0)
        return WINED3DERR_INVALIDCALL;

    memcpy(dstData, &This->stateBlock->pixelShaderConstantI[start * 4], cnt * sizeof(int) * 4);
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetPixelShaderConstantF(
    IWineD3DDevice *iface,
    UINT start,
    CONST float *srcData,
    UINT count) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    int i, cnt = min(count, GL_LIMITS(pshader_constantsF) - start);

    TRACE("(iface %p, srcData %p, start %d, count %d)\n",
            iface, srcData, start, count);

    if (srcData == NULL || cnt < 0)
        return WINED3DERR_INVALIDCALL;

    memcpy(&This->updateStateBlock->pixelShaderConstantF[start * 4], srcData, cnt * sizeof(float) * 4);
    for (i = 0; i < cnt; i++)
        TRACE("Set FLOAT constant %u to { %f, %f, %f, %f }\n", start + i,
           srcData[i*4], srcData[i*4+1], srcData[i*4+2], srcData[i*4+3]);

    for (i = start; i < cnt + start; ++i) {
        if (!This->updateStateBlock->set.pixelShaderConstantsF[i]) {
            constant_entry *ptr = HeapAlloc(GetProcessHeap(), 0, sizeof(constant_entry));
            ptr->idx = i;
            list_add_head(&This->updateStateBlock->set_pconstantsF, &ptr->entry);
            This->updateStateBlock->set.pixelShaderConstantsF[i] = TRUE;
        }
        This->updateStateBlock->changed.pixelShaderConstantsF[i] = TRUE;
    }

    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_PIXELSHADERCONSTANT);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetPixelShaderConstantF(
    IWineD3DDevice *iface,
    UINT start,
    float *dstData,
    UINT count) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    int cnt = min(count, GL_LIMITS(pshader_constantsF) - start);

    TRACE("(iface %p, dstData %p, start %d, count %d)\n",
            iface, dstData, start, count);

    if (dstData == NULL || cnt < 0)
        return WINED3DERR_INVALIDCALL;

    memcpy(dstData, &This->stateBlock->pixelShaderConstantF[start * 4], cnt * sizeof(float) * 4);
    return WINED3D_OK;
}

#define copy_and_next(dest, src, size) memcpy(dest, src, size); dest += (size)
static HRESULT
process_vertices_strided(IWineD3DDeviceImpl *This, DWORD dwDestIndex, DWORD dwCount, WineDirect3DVertexStridedData *lpStrideData, DWORD SrcFVF, IWineD3DVertexBufferImpl *dest, DWORD dwFlags) {
    char *dest_ptr, *dest_conv = NULL;
    unsigned int i;
    DWORD DestFVF = dest->fvf;
    WINED3DVIEWPORT vp;
    WINED3DMATRIX mat, proj_mat, view_mat, world_mat;
    BOOL doClip;
    int numTextures;

    if (SrcFVF & WINED3DFVF_NORMAL) {
        WARN(" lighting state not saved yet... Some strange stuff may happen !\n");
    }

    if ( (SrcFVF & WINED3DFVF_POSITION_MASK) != WINED3DFVF_XYZ) {
        ERR("Source has no position mask\n");
        return WINED3DERR_INVALIDCALL;
    }

    /* We might access VBOs from this code, so hold the lock */
    ENTER_GL();

    if (dest->resource.allocatedMemory == NULL) {
        /* This may happen if we do direct locking into a vbo. Unlikely,
         * but theoretically possible(ddraw processvertices test)
         */
        dest->resource.allocatedMemory = HeapAlloc(GetProcessHeap(), 0, dest->resource.size);
        if(!dest->resource.allocatedMemory) {
            LEAVE_GL();
            ERR("Out of memory\n");
            return E_OUTOFMEMORY;
        }
        if(dest->vbo) {
            void *src;
            GL_EXTCALL(glBindBufferARB(GL_ARRAY_BUFFER_ARB, dest->vbo));
            checkGLcall("glBindBufferARB");
            src = GL_EXTCALL(glMapBufferARB(GL_ARRAY_BUFFER_ARB, GL_READ_ONLY_ARB));
            if(src) {
                memcpy(dest->resource.allocatedMemory, src, dest->resource.size);
            }
            GL_EXTCALL(glUnmapBufferARB(GL_ARRAY_BUFFER_ARB));
            checkGLcall("glUnmapBufferARB");
        }
    }

    /* Get a pointer into the destination vbo(create one if none exists) and
     * write correct opengl data into it. It's cheap and allows us to run drawStridedFast
     */
    if(!dest->vbo && GL_SUPPORT(ARB_VERTEX_BUFFER_OBJECT)) {
        CreateVBO(dest);
    }

    if(dest->vbo) {
        GL_EXTCALL(glBindBufferARB(GL_ARRAY_BUFFER_ARB, dest->vbo));
        dest_conv = GL_EXTCALL(glMapBufferARB(GL_ARRAY_BUFFER_ARB, GL_WRITE_ONLY_ARB));
        if(!dest_conv) {
            ERR("glMapBuffer failed\n");
            /* Continue without storing converted vertices */
        }
    }

    /* Should I clip?
     * a) WINED3DRS_CLIPPING is enabled
     * b) WINED3DVOP_CLIP is passed
     */
    if(This->stateBlock->renderState[WINED3DRS_CLIPPING]) {
        static BOOL warned = FALSE;
        /*
         * The clipping code is not quite correct. Some things need
         * to be checked against IDirect3DDevice3 (!), d3d8 and d3d9,
         * so disable clipping for now.
         * (The graphics in Half-Life are broken, and my processvertices
         *  test crashes with IDirect3DDevice3)
        doClip = TRUE;
         */
        doClip = FALSE;
        if(!warned) {
           warned = TRUE;
           FIXME("Clipping is broken and disabled for now\n");
        }
    } else doClip = FALSE;
    dest_ptr = ((char *) dest->resource.allocatedMemory) + dwDestIndex * get_flexible_vertex_size(DestFVF);
    if(dest_conv) {
        dest_conv = ((char *) dest_conv) + dwDestIndex * get_flexible_vertex_size(DestFVF);
    }

    IWineD3DDevice_GetTransform( (IWineD3DDevice *) This,
                                 WINED3DTS_VIEW,
                                 &view_mat);
    IWineD3DDevice_GetTransform( (IWineD3DDevice *) This,
                                 WINED3DTS_PROJECTION,
                                 &proj_mat);
    IWineD3DDevice_GetTransform( (IWineD3DDevice *) This,
                                 WINED3DTS_WORLDMATRIX(0),
                                 &world_mat);

    TRACE("View mat:\n");
    TRACE("%f %f %f %f\n", view_mat.u.s._11, view_mat.u.s._12, view_mat.u.s._13, view_mat.u.s._14);
    TRACE("%f %f %f %f\n", view_mat.u.s._21, view_mat.u.s._22, view_mat.u.s._23, view_mat.u.s._24);
    TRACE("%f %f %f %f\n", view_mat.u.s._31, view_mat.u.s._32, view_mat.u.s._33, view_mat.u.s._34);
    TRACE("%f %f %f %f\n", view_mat.u.s._41, view_mat.u.s._42, view_mat.u.s._43, view_mat.u.s._44);

    TRACE("Proj mat:\n");
    TRACE("%f %f %f %f\n", proj_mat.u.s._11, proj_mat.u.s._12, proj_mat.u.s._13, proj_mat.u.s._14);
    TRACE("%f %f %f %f\n", proj_mat.u.s._21, proj_mat.u.s._22, proj_mat.u.s._23, proj_mat.u.s._24);
    TRACE("%f %f %f %f\n", proj_mat.u.s._31, proj_mat.u.s._32, proj_mat.u.s._33, proj_mat.u.s._34);
    TRACE("%f %f %f %f\n", proj_mat.u.s._41, proj_mat.u.s._42, proj_mat.u.s._43, proj_mat.u.s._44);

    TRACE("World mat:\n");
    TRACE("%f %f %f %f\n", world_mat.u.s._11, world_mat.u.s._12, world_mat.u.s._13, world_mat.u.s._14);
    TRACE("%f %f %f %f\n", world_mat.u.s._21, world_mat.u.s._22, world_mat.u.s._23, world_mat.u.s._24);
    TRACE("%f %f %f %f\n", world_mat.u.s._31, world_mat.u.s._32, world_mat.u.s._33, world_mat.u.s._34);
    TRACE("%f %f %f %f\n", world_mat.u.s._41, world_mat.u.s._42, world_mat.u.s._43, world_mat.u.s._44);

    /* Get the viewport */
    IWineD3DDevice_GetViewport( (IWineD3DDevice *) This, &vp);
    TRACE("Viewport: X=%d, Y=%d, Width=%d, Height=%d, MinZ=%f, MaxZ=%f\n",
          vp.X, vp.Y, vp.Width, vp.Height, vp.MinZ, vp.MaxZ);

    multiply_matrix(&mat,&view_mat,&world_mat);
    multiply_matrix(&mat,&proj_mat,&mat);

    numTextures = (DestFVF & WINED3DFVF_TEXCOUNT_MASK) >> WINED3DFVF_TEXCOUNT_SHIFT;

    for (i = 0; i < dwCount; i+= 1) {
        unsigned int tex_index;

        if ( ((DestFVF & WINED3DFVF_POSITION_MASK) == WINED3DFVF_XYZ ) ||
             ((DestFVF & WINED3DFVF_POSITION_MASK) == WINED3DFVF_XYZRHW ) ) {
            /* The position first */
            float *p =
              (float *) (((char *) lpStrideData->u.s.position.lpData) + i * lpStrideData->u.s.position.dwStride);
            float x, y, z, rhw;
            TRACE("In: ( %06.2f %06.2f %06.2f )\n", p[0], p[1], p[2]);

            /* Multiplication with world, view and projection matrix */
            x =   (p[0] * mat.u.s._11) + (p[1] * mat.u.s._21) + (p[2] * mat.u.s._31) + (1.0 * mat.u.s._41);
            y =   (p[0] * mat.u.s._12) + (p[1] * mat.u.s._22) + (p[2] * mat.u.s._32) + (1.0 * mat.u.s._42);
            z =   (p[0] * mat.u.s._13) + (p[1] * mat.u.s._23) + (p[2] * mat.u.s._33) + (1.0 * mat.u.s._43);
            rhw = (p[0] * mat.u.s._14) + (p[1] * mat.u.s._24) + (p[2] * mat.u.s._34) + (1.0 * mat.u.s._44);

            TRACE("x=%f y=%f z=%f rhw=%f\n", x, y, z, rhw);

            /* WARNING: The following things are taken from d3d7 and were not yet checked
             * against d3d8 or d3d9!
             */

            /* Clipping conditions: From
             * http://msdn.microsoft.com/archive/default.asp?url=/archive/en-us/directx9_c/directx/graphics/programmingguide/fixedfunction/viewportsclipping/clippingvolumes.asp
             *
             * A vertex is clipped if it does not match the following requirements
             * -rhw < x <= rhw
             * -rhw < y <= rhw
             *    0 < z <= rhw
             *    0 < rhw ( Not in d3d7, but tested in d3d7)
             *
             * If clipping is on is determined by the D3DVOP_CLIP flag in D3D7, and
             * by the D3DRS_CLIPPING in D3D9(according to the msdn, not checked)
             *
             */

            if( !doClip ||
                ( (-rhw -eps < x) && (-rhw -eps < y) && ( -eps < z) &&
                  (x <= rhw + eps) && (y <= rhw + eps ) && (z <= rhw + eps) && 
                  ( rhw > eps ) ) ) {

                /* "Normal" viewport transformation (not clipped)
                 * 1) The values are divided by rhw
                 * 2) The y axis is negative, so multiply it with -1
                 * 3) Screen coordinates go from -(Width/2) to +(Width/2) and
                 *    -(Height/2) to +(Height/2). The z range is MinZ to MaxZ
                 * 4) Multiply x with Width/2 and add Width/2
                 * 5) The same for the height
                 * 6) Add the viewpoint X and Y to the 2D coordinates and
                 *    The minimum Z value to z
                 * 7) rhw = 1 / rhw Reciprocal of Homogeneous W....
                 *
                 * Well, basically it's simply a linear transformation into viewport
                 * coordinates
                 */

                x /= rhw;
                y /= rhw;
                z /= rhw;

                y *= -1;

                x *= vp.Width / 2;
                y *= vp.Height / 2;
                z *= vp.MaxZ - vp.MinZ;

                x += vp.Width / 2 + vp.X;
                y += vp.Height / 2 + vp.Y;
                z += vp.MinZ;

                rhw = 1 / rhw;
            } else {
                /* That vertex got clipped
                 * Contrary to OpenGL it is not dropped completely, it just
                 * undergoes a different calculation.
                 */
                TRACE("Vertex got clipped\n");
                x += rhw;
                y += rhw;

                x  /= 2;
                y  /= 2;

                /* Msdn mentions that Direct3D9 keeps a list of clipped vertices
                 * outside of the main vertex buffer memory. That needs some more
                 * investigation...
                 */
            }

            TRACE("Writing (%f %f %f) %f\n", x, y, z, rhw);


            ( (float *) dest_ptr)[0] = x;
            ( (float *) dest_ptr)[1] = y;
            ( (float *) dest_ptr)[2] = z;
            ( (float *) dest_ptr)[3] = rhw; /* SIC, see ddraw test! */

            dest_ptr += 3 * sizeof(float);

            if((DestFVF & WINED3DFVF_POSITION_MASK) == WINED3DFVF_XYZRHW) {
                dest_ptr += sizeof(float);
            }

            if(dest_conv) {
                float w = 1 / rhw;
                ( (float *) dest_conv)[0] = x * w;
                ( (float *) dest_conv)[1] = y * w;
                ( (float *) dest_conv)[2] = z * w;
                ( (float *) dest_conv)[3] = w;

                dest_conv += 3 * sizeof(float);

                if((DestFVF & WINED3DFVF_POSITION_MASK) == WINED3DFVF_XYZRHW) {
                    dest_conv += sizeof(float);
                }
            }
        }
        if (DestFVF & WINED3DFVF_PSIZE) {
            dest_ptr += sizeof(DWORD);
            if(dest_conv) dest_conv += sizeof(DWORD);
        }
        if (DestFVF & WINED3DFVF_NORMAL) {
            float *normal =
              (float *) (((float *) lpStrideData->u.s.normal.lpData) + i * lpStrideData->u.s.normal.dwStride);
            /* AFAIK this should go into the lighting information */
            FIXME("Didn't expect the destination to have a normal\n");
            copy_and_next(dest_ptr, normal, 3 * sizeof(float));
            if(dest_conv) {
                copy_and_next(dest_conv, normal, 3 * sizeof(float));
            }
        }

        if (DestFVF & WINED3DFVF_DIFFUSE) {
            DWORD *color_d = 
              (DWORD *) (((char *) lpStrideData->u.s.diffuse.lpData) + i * lpStrideData->u.s.diffuse.dwStride);
            if(!color_d) {
                static BOOL warned = FALSE;

                if(!warned) {
                    ERR("No diffuse color in source, but destination has one\n");
                    warned = TRUE;
                }

                *( (DWORD *) dest_ptr) = 0xffffffff;
                dest_ptr += sizeof(DWORD);

                if(dest_conv) {
                    *( (DWORD *) dest_conv) = 0xffffffff;
                    dest_conv += sizeof(DWORD);
                }
            }
            else {
                copy_and_next(dest_ptr, color_d, sizeof(DWORD));
                if(dest_conv) {
                    *( (DWORD *) dest_conv)  = (*color_d & 0xff00ff00)      ; /* Alpha + green */
                    *( (DWORD *) dest_conv) |= (*color_d & 0x00ff0000) >> 16; /* Red */
                    *( (DWORD *) dest_conv) |= (*color_d & 0xff0000ff) << 16; /* Blue */
                    dest_conv += sizeof(DWORD);
                }
            }
        }

        if (DestFVF & WINED3DFVF_SPECULAR) { 
            /* What's the color value in the feedback buffer? */
            DWORD *color_s = 
              (DWORD *) (((char *) lpStrideData->u.s.specular.lpData) + i * lpStrideData->u.s.specular.dwStride);
            if(!color_s) {
                static BOOL warned = FALSE;

                if(!warned) {
                    ERR("No specular color in source, but destination has one\n");
                    warned = TRUE;
                }

                *( (DWORD *) dest_ptr) = 0xFF000000;
                dest_ptr += sizeof(DWORD);

                if(dest_conv) {
                    *( (DWORD *) dest_conv) = 0xFF000000;
                    dest_conv += sizeof(DWORD);
                }
            }
            else {
                copy_and_next(dest_ptr, color_s, sizeof(DWORD));
                if(dest_conv) {
                    *( (DWORD *) dest_conv)  = (*color_s & 0xff00ff00)      ; /* Alpha + green */
                    *( (DWORD *) dest_conv) |= (*color_s & 0x00ff0000) >> 16; /* Red */
                    *( (DWORD *) dest_conv) |= (*color_s & 0xff0000ff) << 16; /* Blue */
                    dest_conv += sizeof(DWORD);
                }
            }
        }

        for (tex_index = 0; tex_index < numTextures; tex_index++) {
            float *tex_coord =
              (float *) (((char *) lpStrideData->u.s.texCoords[tex_index].lpData) + 
                            i * lpStrideData->u.s.texCoords[tex_index].dwStride);
            if(!tex_coord) {
                ERR("No source texture, but destination requests one\n");
                dest_ptr+=GET_TEXCOORD_SIZE_FROM_FVF(DestFVF, tex_index) * sizeof(float);
                if(dest_conv) dest_conv += GET_TEXCOORD_SIZE_FROM_FVF(DestFVF, tex_index) * sizeof(float);
            }
            else {
                copy_and_next(dest_ptr, tex_coord, GET_TEXCOORD_SIZE_FROM_FVF(DestFVF, tex_index) * sizeof(float));
                if(dest_conv) {
                    copy_and_next(dest_conv, tex_coord, GET_TEXCOORD_SIZE_FROM_FVF(DestFVF, tex_index) * sizeof(float));
                }
            }
        }
    }

    if(dest_conv) {
        GL_EXTCALL(glUnmapBufferARB(GL_ARRAY_BUFFER_ARB));
        checkGLcall("glUnmapBufferARB(GL_ARRAY_BUFFER_ARB)");
    }

    LEAVE_GL();

    return WINED3D_OK;
}
#undef copy_and_next

static HRESULT WINAPI IWineD3DDeviceImpl_ProcessVertices(IWineD3DDevice *iface, UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IWineD3DVertexBuffer* pDestBuffer, IWineD3DVertexBuffer* pVertexDecl, DWORD Flags) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DVertexBufferImpl *SrcImpl = (IWineD3DVertexBufferImpl *) pVertexDecl;
    WineDirect3DVertexStridedData strided;
    TRACE("(%p)->(%d,%d,%d,%p,%p,%d\n", This, SrcStartIndex, DestIndex, VertexCount, pDestBuffer, pVertexDecl, Flags);

    if (!SrcImpl) {
        WARN("NULL source vertex buffer\n");
        return WINED3DERR_INVALIDCALL;
    }
    /* We don't need the source vbo because this buffer is only used as
     * a source for ProcessVertices. Avoid wasting resources by converting the
     * buffer and loading the VBO
     */
    if(SrcImpl->vbo) {
        TRACE("Releasing the source vbo, it won't be needed\n");

        if(!SrcImpl->resource.allocatedMemory) {
            /* Rescue the data from the buffer */
            void *src;
            SrcImpl->resource.allocatedMemory = HeapAlloc(GetProcessHeap(), 0, SrcImpl->resource.size);
            if(!SrcImpl->resource.allocatedMemory) {
                ERR("Out of memory\n");
                return E_OUTOFMEMORY;
            }

            ENTER_GL();
            GL_EXTCALL(glBindBufferARB(GL_ARRAY_BUFFER_ARB, SrcImpl->vbo));
            checkGLcall("glBindBufferARB");

            src = GL_EXTCALL(glMapBufferARB(GL_ARRAY_BUFFER_ARB, GL_READ_ONLY_ARB));
            if(src) {
                memcpy(SrcImpl->resource.allocatedMemory, src, SrcImpl->resource.size);
            }

            GL_EXTCALL(glUnmapBufferARB(GL_ARRAY_BUFFER_ARB));
            checkGLcall("glUnmapBufferARB");
        } else {
            ENTER_GL();
        }

        GL_EXTCALL(glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0));
        checkGLcall("glBindBufferARB");
        GL_EXTCALL(glDeleteBuffersARB(1, &SrcImpl->vbo));
        checkGLcall("glDeleteBuffersARB");
        LEAVE_GL();

        SrcImpl->vbo = 0;
    }

    memset(&strided, 0, sizeof(strided));
    primitiveConvertFVFtoOffset(SrcImpl->fvf, get_flexible_vertex_size(SrcImpl->fvf), SrcImpl->resource.allocatedMemory + get_flexible_vertex_size(SrcImpl->fvf) * SrcStartIndex, &strided, 0, 0);

    return process_vertices_strided(This, DestIndex, VertexCount, &strided, SrcImpl->fvf, (IWineD3DVertexBufferImpl *) pDestBuffer, Flags);
}

/*****
 * Get / Set Texture Stage States
 * TODO: Verify against dx9 definitions
 *****/
static HRESULT WINAPI IWineD3DDeviceImpl_SetTextureStageState(IWineD3DDevice *iface, DWORD Stage, WINED3DTEXTURESTAGESTATETYPE Type, DWORD Value) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    DWORD oldValue = This->updateStateBlock->textureState[Stage][Type];

    /* FIXME: Handle 3d textures? What if TSS value set before set texture? Need to reapply all values? */

    TRACE("(%p) : Stage=%d, Type=%s(%d), Value=%d\n", This, Stage, debug_d3dtexturestate(Type), Type, Value);

    /* Reject invalid texture units */
    if (Stage >= GL_LIMITS(texture_stages)) {
        TRACE("Attempt to access invalid texture rejected\n");
        return WINED3DERR_INVALIDCALL;
    }

    This->updateStateBlock->changed.textureState[Stage][Type] = TRUE;
    This->updateStateBlock->set.textureState[Stage][Type]     = TRUE;
    This->updateStateBlock->textureState[Stage][Type]         = Value;

    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return WINED3D_OK;
    }

    /* Checked after the assignments to allow proper stateblock recording */
    if(oldValue == Value) {
        TRACE("App is setting the old value over, nothing to do\n");
        return WINED3D_OK;
    }

    if(Stage > This->stateBlock->lowest_disabled_stage &&
       StateTable[STATE_TEXTURESTAGE(0, Type)].representative == STATE_TEXTURESTAGE(0, WINED3DTSS_COLOROP)) {
        /* Colorop change above lowest disabled stage? That won't change anything in the gl setup
         * Changes in other states are important on disabled stages too
         */
        return WINED3D_OK;
    }

    if(Type == WINED3DTSS_COLOROP) {
        int i;

        if(Value == WINED3DTOP_DISABLE && oldValue != WINED3DTOP_DISABLE) {
            /* Previously enabled stage disabled now. Make sure to dirtify all enabled stages above Stage,
             * they have to be disabled
             *
             * The current stage is dirtified below.
             */
            for(i = Stage + 1; i < This->stateBlock->lowest_disabled_stage; i++) {
                TRACE("Additionally dirtifying stage %d\n", i);
                IWineD3DDeviceImpl_MarkStateDirty(This, STATE_TEXTURESTAGE(i, WINED3DTSS_COLOROP));
            }
            This->stateBlock->lowest_disabled_stage = Stage;
            TRACE("New lowest disabled: %d\n", Stage);
        } else if(Value != WINED3DTOP_DISABLE && oldValue == WINED3DTOP_DISABLE) {
            /* Previously disabled stage enabled. Stages above it may need enabling
             * stage must be lowest_disabled_stage here, if it's bigger success is returned above,
             * and stages below the lowest disabled stage can't be enabled(because they are enabled already).
             *
             * Again stage Stage doesn't need to be dirtified here, it is handled below.
             */

            for(i = Stage + 1; i < GL_LIMITS(texture_stages); i++) {
                if(This->updateStateBlock->textureState[i][WINED3DTSS_COLOROP] == WINED3DTOP_DISABLE) {
                    break;
                }
                TRACE("Additionally dirtifying stage %d due to enable\n", i);
                IWineD3DDeviceImpl_MarkStateDirty(This, STATE_TEXTURESTAGE(i, WINED3DTSS_COLOROP));
            }
            This->stateBlock->lowest_disabled_stage = i;
            TRACE("New lowest disabled: %d\n", i);
        }
        if(GL_SUPPORT(NV_REGISTER_COMBINERS) && !This->stateBlock->pixelShader) {
            /* TODO: Built a stage -> texture unit mapping for register combiners */
        }
    }

    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_TEXTURESTAGE(Stage, Type));

    /* Rebuild the stage -> gl texture unit mapping if register combiners are supported
     * If there is a pixel shader there will be a 1:1 mapping, no need to touch it. SetPixelShader
     * will call FindTexUnitMap too.
     */
    if(GL_SUPPORT(NV_REGISTER_COMBINERS) && !This->stateBlock->pixelShader) {
        IWineD3DDeviceImpl_FindTexUnitMap(This);
    }
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetTextureStageState(IWineD3DDevice *iface, DWORD Stage, WINED3DTEXTURESTAGESTATETYPE Type, DWORD* pValue) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : requesting Stage %d, Type %d getting %d\n", This, Stage, Type, This->updateStateBlock->textureState[Stage][Type]);
    *pValue = This->updateStateBlock->textureState[Stage][Type];
    return WINED3D_OK;
}

/*****
 * Get / Set Texture
 *****/
static HRESULT WINAPI IWineD3DDeviceImpl_SetTexture(IWineD3DDevice *iface, DWORD Stage, IWineD3DBaseTexture* pTexture) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DBaseTexture   *oldTexture;

    oldTexture = This->updateStateBlock->textures[Stage];
    TRACE("(%p) : Stage(%d), Texture (%p)\n", This, Stage, pTexture);

#if 0 /* TODO: check so vertex textures */
    if (Stage >= D3DVERTEXTEXTURESAMPLER && Stage <= D3DVERTEXTEXTURESAMPLER3){
        This->updateStateBlock->vertexTextures[Stage - D3DVERTEXTEXTURESAMPLER] = pTexture;
        return WINED3D_OK;
    }
#endif

    /* Reject invalid texture units */
    if (Stage >= GL_LIMITS(sampler_stages) || Stage < 0) {
        WARN("Attempt to access invalid texture rejected\n");
        return WINED3DERR_INVALIDCALL;
    }

    if(pTexture != NULL) {
        /* SetTexture isn't allowed on textures in WINED3DPOOL_SCRATCH; 
         */
        if(((IWineD3DTextureImpl*)pTexture)->resource.pool == WINED3DPOOL_SCRATCH) {
            WARN("(%p) Attempt to set scratch texture rejected\n", pTexture);
            return WINED3DERR_INVALIDCALL;
        }
        This->stateBlock->textureDimensions[Stage] = IWineD3DBaseTexture_GetTextureDimensions(pTexture);
    }

    TRACE("GL_LIMITS %d\n",GL_LIMITS(sampler_stages));
    TRACE("(%p) : oldtexture(%p)\n", This,oldTexture);

    This->updateStateBlock->set.textures[Stage]     = TRUE;
    This->updateStateBlock->changed.textures[Stage] = TRUE;
    TRACE("(%p) : setting new texture to %p\n", This, pTexture);
    This->updateStateBlock->textures[Stage]         = pTexture;

    /* Handle recording of state blocks */
    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return WINED3D_OK;
    }

    if(oldTexture == pTexture) {
        TRACE("App is setting the same texture again, nothing to do\n");
        return WINED3D_OK;
    }

    /** NOTE: MSDN says that setTexture increases the reference count,
    * and the the application nust set the texture back to null (or have a leaky application),
    * This means we should pass the refcount up to the parent
     *******************************/
    if (NULL != This->updateStateBlock->textures[Stage]) {
        IWineD3DBaseTextureImpl *new = (IWineD3DBaseTextureImpl *) This->updateStateBlock->textures[Stage];
        ULONG bindCount = InterlockedIncrement(&new->baseTexture.bindCount);

        IWineD3DBaseTexture_AddRef(This->updateStateBlock->textures[Stage]);
        if(oldTexture == NULL && Stage < MAX_TEXTURES) {
            /* The source arguments for color and alpha ops have different meanings when a NULL texture is bound,
             * so the COLOROP and ALPHAOP have to be dirtified.
             */
            IWineD3DDeviceImpl_MarkStateDirty(This, STATE_TEXTURESTAGE(Stage, WINED3DTSS_COLOROP));
            IWineD3DDeviceImpl_MarkStateDirty(This, STATE_TEXTURESTAGE(Stage, WINED3DTSS_ALPHAOP));
        }
        if(bindCount == 1) {
            new->baseTexture.sampler = Stage;
        }
        /* More than one assignment? Doesn't matter, we only need one gl texture unit to use for uploading */

    }

    if (NULL != oldTexture) {
        IWineD3DBaseTextureImpl *old = (IWineD3DBaseTextureImpl *) oldTexture;
        LONG bindCount = InterlockedDecrement(&old->baseTexture.bindCount);

        IWineD3DBaseTexture_Release(oldTexture);
        if(pTexture == NULL && Stage < MAX_TEXTURES) {
            IWineD3DDeviceImpl_MarkStateDirty(This, STATE_TEXTURESTAGE(Stage, WINED3DTSS_COLOROP));
            IWineD3DDeviceImpl_MarkStateDirty(This, STATE_TEXTURESTAGE(Stage, WINED3DTSS_ALPHAOP));
        }

        if(bindCount && old->baseTexture.sampler == Stage) {
            int i;
            /* Have to do a search for the other sampler(s) where the texture is bound to
             * Shouldn't happen as long as apps bind a texture only to one stage
             */
            TRACE("Searcing for other sampler / stage id where the texture is bound to\n");
            for(i = 0; i < GL_LIMITS(sampler_stages); i++) {
                if(This->updateStateBlock->textures[i] == oldTexture) {
                    old->baseTexture.sampler = i;
                    break;
                }
            }
        }
    }

    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_SAMPLER(Stage));

    /* Verify the texture unit mapping(and rebuild it if needed) if we use nvrcs and no
     * pixel shader is used
     */
    if(GL_SUPPORT(NV_REGISTER_COMBINERS) && !This->stateBlock->pixelShader) {
        IWineD3DDeviceImpl_FindTexUnitMap(This);
    }

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetTexture(IWineD3DDevice *iface, DWORD Stage, IWineD3DBaseTexture** ppTexture) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : (%d /* Stage */,%p /* ppTexture */)\n", This, Stage, ppTexture);

    /* Reject invalid texture units */
    if (Stage >= GL_LIMITS(sampler_stages)) {
        TRACE("Attempt to access invalid texture rejected\n");
        return WINED3DERR_INVALIDCALL;
    }
    *ppTexture=This->stateBlock->textures[Stage];
    if (*ppTexture)
        IWineD3DBaseTexture_AddRef(*ppTexture);

    return WINED3D_OK;
}

/*****
 * Get Back Buffer
 *****/
static HRESULT WINAPI IWineD3DDeviceImpl_GetBackBuffer(IWineD3DDevice *iface, UINT iSwapChain, UINT BackBuffer, WINED3DBACKBUFFER_TYPE Type,
                                                IWineD3DSurface **ppBackBuffer) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DSwapChain *swapChain;
    HRESULT hr;

    TRACE("(%p) : BackBuf %d Type %d SwapChain %d returning %p\n", This, BackBuffer, Type, iSwapChain, *ppBackBuffer);

    hr = IWineD3DDeviceImpl_GetSwapChain(iface,  iSwapChain, &swapChain);
    if (hr == WINED3D_OK) {
        hr = IWineD3DSwapChain_GetBackBuffer(swapChain, BackBuffer, Type, ppBackBuffer);
            IWineD3DSwapChain_Release(swapChain);
    } else {
        *ppBackBuffer = NULL;
    }
    return hr;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetDeviceCaps(IWineD3DDevice *iface, WINED3DCAPS* pCaps) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    WARN("(%p) : stub, calling idirect3d for now\n", This);
    return IWineD3D_GetDeviceCaps(This->wineD3D, This->adapterNo, This->devType, pCaps);
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetDisplayMode(IWineD3DDevice *iface, UINT iSwapChain, WINED3DDISPLAYMODE* pMode) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DSwapChain *swapChain;
    HRESULT hr;

    if(iSwapChain > 0) {
        hr = IWineD3DDeviceImpl_GetSwapChain(iface,  iSwapChain, (IWineD3DSwapChain **)&swapChain);
        if (hr == WINED3D_OK) {
            hr = IWineD3DSwapChain_GetDisplayMode(swapChain, pMode);
            IWineD3DSwapChain_Release(swapChain);
        } else {
            FIXME("(%p) Error getting display mode\n", This);
        }
    } else {
        /* Don't read the real display mode,
           but return the stored mode instead. X11 can't change the color
           depth, and some apps are pretty angry if they SetDisplayMode from
           24 to 16 bpp and find out that GetDisplayMode still returns 24 bpp

           Also don't relay to the swapchain because with ddraw it's possible
           that there isn't a swapchain at all */
        pMode->Width = This->ddraw_width;
        pMode->Height = This->ddraw_height;
        pMode->Format = This->ddraw_format;
        pMode->RefreshRate = 0;
        hr = WINED3D_OK;
    }

    return hr;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetHWND(IWineD3DDevice *iface, HWND hWnd) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p)->(%p)\n", This, hWnd);

    This->ddraw_window = hWnd;
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetHWND(IWineD3DDevice *iface, HWND *hWnd) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p)->(%p)\n", This, hWnd);

    *hWnd = This->ddraw_window;
    return WINED3D_OK;
}

/*****
 * Stateblock related functions
 *****/

static HRESULT WINAPI IWineD3DDeviceImpl_BeginStateBlock(IWineD3DDevice *iface) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DStateBlockImpl *object;
    HRESULT temp_result;
    
    TRACE("(%p)\n", This);
    
    if (This->isRecordingState) {
        return WINED3DERR_INVALIDCALL;
    }
    
    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IWineD3DStateBlockImpl));
    if (NULL == object ) {
        FIXME("(%p)Error allocating memory for stateblock\n", This);
        return E_OUTOFMEMORY;
    }
    TRACE("(%p) created object %p\n", This, object);
    object->wineD3DDevice= This;
    /** FIXME: object->parent       = parent; **/
    object->parent       = NULL;
    object->blockType    = WINED3DSBT_ALL;
    object->ref          = 1;
    object->lpVtbl       = &IWineD3DStateBlock_Vtbl;
    
    temp_result = allocate_shader_constants(object);
    if (WINED3D_OK != temp_result)
        return temp_result;

    IWineD3DStateBlock_Release((IWineD3DStateBlock*)This->updateStateBlock);
    This->updateStateBlock = object;
    This->isRecordingState = TRUE;

    TRACE("(%p) recording stateblock %p\n",This , object);
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_EndStateBlock(IWineD3DDevice *iface, IWineD3DStateBlock** ppStateBlock) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    if (!This->isRecordingState) {
        FIXME("(%p) not recording! returning error\n", This);
        *ppStateBlock = NULL;
        return WINED3DERR_INVALIDCALL;
    }

    *ppStateBlock = (IWineD3DStateBlock*)This->updateStateBlock;
    This->isRecordingState = FALSE;
    This->updateStateBlock = This->stateBlock;
    IWineD3DStateBlock_AddRef((IWineD3DStateBlock*)This->updateStateBlock);
    /* IWineD3DStateBlock_AddRef(*ppStateBlock); don't need to do this, since we should really just release UpdateStateBlock first */
    TRACE("(%p) returning token (ptr to stateblock) of %p\n", This, *ppStateBlock);
    return WINED3D_OK;
}

/*****
 * Scene related functions
 *****/
static HRESULT WINAPI IWineD3DDeviceImpl_BeginScene(IWineD3DDevice *iface) {
    /* At the moment we have no need for any functionality at the beginning
       of a scene                                                          */
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : stub\n", This);
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_EndScene(IWineD3DDevice *iface) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p)\n", This);
    ENTER_GL();
    /* We only have to do this if we need to read the, swapbuffers performs a flush for us */
    glFlush();
    checkGLcall("glFlush");

    TRACE("End Scene\n");
    /* If we're using FBOs this isn't needed */
    if (wined3d_settings.offscreen_rendering_mode != ORM_FBO && This->render_targets[0] != NULL) {

        /* If the container of the rendertarget is a texture then we need to save the data from the pbuffer */
        IUnknown *targetContainer = NULL;
        if (WINED3D_OK == IWineD3DSurface_GetContainer(This->render_targets[0], &IID_IWineD3DBaseTexture, (void **)&targetContainer)
            || WINED3D_OK == IWineD3DSurface_GetContainer(This->render_targets[0], &IID_IWineD3DDevice, (void **)&targetContainer)) {
            TRACE("(%p) : Texture rendertarget %p\n", This ,This->render_targets[0]);
            /** always dirtify for now. we must find a better way to see that surface have been modified
            (Modifications should will only occur via draw-primitive, but we do need better locking
            switching to render-to-texture should remove the overhead though.
            */
            IWineD3DSurface_SetPBufferState(This->render_targets[0], TRUE /* inPBuffer */, FALSE /* inTexture */);
            IWineD3DSurface_AddDirtyRect(This->render_targets[0], NULL);
            IWineD3DSurface_PreLoad(This->render_targets[0]);
            IWineD3DSurface_SetPBufferState(This->render_targets[0], FALSE /* inPBuffer */, FALSE /* inTexture */);
            IUnknown_Release(targetContainer);
        }
    }
    This->sceneEnded = TRUE;
    LEAVE_GL();
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_Present(IWineD3DDevice *iface,
                                          CONST RECT* pSourceRect, CONST RECT* pDestRect,
                                          HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DSwapChain *swapChain = NULL;
    int i;
    int swapchains = IWineD3DDeviceImpl_GetNumberOfSwapChains(iface);

    TRACE("(%p) Presenting the frame\n", This);

    for(i = 0 ; i < swapchains ; i ++) {

        IWineD3DDeviceImpl_GetSwapChain(iface, i , (IWineD3DSwapChain **)&swapChain);
        TRACE("presentinng chain %d, %p\n", i, swapChain);
        IWineD3DSwapChain_Present(swapChain, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, 0);
        IWineD3DSwapChain_Release(swapChain);
    }

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_Clear(IWineD3DDevice *iface, DWORD Count, CONST WINED3DRECT* pRects,
                                        DWORD Flags, WINED3DCOLOR Color, float Z, DWORD Stencil) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    /* TODO: From MSDN This method fails if you specify the WINED3DCLEAR_ZBUFFER or WINED3DCLEAR_STENCIL flags when the
      render target does not have an attached depth buffer. Similarly, if you specify the WINED3DCLEAR_STENCIL flag
      when the depth-buffer format does not contain stencil buffer information, this method fails. */
    GLbitfield     glMask = 0;
    GLboolean      old_ztest;
    GLfloat        old_z_clear_value;
    GLint          old_stencil_clear_value;
    GLfloat        old_color_clear_value[4];
    unsigned int   i;
    CONST WINED3DRECT* curRect;

    TRACE("(%p) Count (%d), pRects (%p), Flags (%x), Z (%f), Stencil (%d)\n", This,
          Count, pRects, Flags, Z, Stencil);

    ENTER_GL();

    glEnable(GL_SCISSOR_TEST);
    checkGLcall("glEnable GL_SCISSOR_TEST");

    if (Count > 0 && pRects) {
        curRect = pRects;
    } else {
        curRect = NULL;
    }

    /* Only set the values up once, as they are not changing */
    if (Flags & WINED3DCLEAR_STENCIL) {
        glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &old_stencil_clear_value);
        glClearStencil(Stencil);
        checkGLcall("glClearStencil");
        glMask = glMask | GL_STENCIL_BUFFER_BIT;
        glStencilMask(0xFFFFFFFF);
    }

    if (Flags & WINED3DCLEAR_ZBUFFER) {
        glGetBooleanv(GL_DEPTH_WRITEMASK, &old_ztest);
        glDepthMask(GL_TRUE);
        glGetFloatv(GL_DEPTH_CLEAR_VALUE, &old_z_clear_value);
        glClearDepth(Z);
        checkGLcall("glClearDepth");
        glMask = glMask | GL_DEPTH_BUFFER_BIT;
    }

    if (Flags & WINED3DCLEAR_TARGET) {
        TRACE("Clearing screen with glClear to color %x\n", Color);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, old_color_clear_value);
        glClearColor(D3DCOLOR_R(Color),
                     D3DCOLOR_G(Color),
                     D3DCOLOR_B(Color),
                     D3DCOLOR_A(Color));
        checkGLcall("glClearColor");

        /* Clear ALL colors! */
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glMask = glMask | GL_COLOR_BUFFER_BIT;
    }

    /* Now process each rect in turn */
    for (i = 0; i < Count || i == 0; i++) {

        if (curRect) {
            /* Note gl uses lower left, width/height */
            TRACE("(%p) %p Rect=(%d,%d)->(%d,%d) glRect=(%d,%d), len=%d, hei=%d\n", This, curRect,
                  curRect->x1, curRect->y1, curRect->x2, curRect->y2,
                  curRect->x1, (((IWineD3DSurfaceImpl *)This->render_targets[0])->currentDesc.Height - curRect->y2),
                  curRect->x2 - curRect->x1, curRect->y2 - curRect->y1);
            glScissor(curRect->x1, (((IWineD3DSurfaceImpl *)This->render_targets[0])->currentDesc.Height - curRect->y2),
                      curRect->x2 - curRect->x1, curRect->y2 - curRect->y1);
            checkGLcall("glScissor");
        } else {
            glScissor(This->stateBlock->viewport.X,
                      (((IWineD3DSurfaceImpl *)This->render_targets[0])->currentDesc.Height -
                      (This->stateBlock->viewport.Y + This->stateBlock->viewport.Height)),
                      This->stateBlock->viewport.Width,
                      This->stateBlock->viewport.Height);
            checkGLcall("glScissor");
        }

        /* Clear the selected rectangle (or full screen) */
        glClear(glMask);
        checkGLcall("glClear");

        /* Step to the next rectangle */
        if (curRect) curRect = curRect + sizeof(WINED3DRECT);
    }

    /* Restore the old values (why..?) */
    if (Flags & WINED3DCLEAR_STENCIL) {
        glClearStencil(old_stencil_clear_value);
        glStencilMask(This->stateBlock->renderState[WINED3DRS_STENCILWRITEMASK]);
    }
    if (Flags & WINED3DCLEAR_ZBUFFER) {
        glDepthMask(old_ztest);
        glClearDepth(old_z_clear_value);
    }
    if (Flags & WINED3DCLEAR_TARGET) {
        glClearColor(old_color_clear_value[0],
                     old_color_clear_value[1],
                     old_color_clear_value[2],
                     old_color_clear_value[3]);
        glColorMask(This->stateBlock->renderState[WINED3DRS_COLORWRITEENABLE] & WINED3DCOLORWRITEENABLE_RED ? GL_TRUE : GL_FALSE,
                    This->stateBlock->renderState[WINED3DRS_COLORWRITEENABLE] & WINED3DCOLORWRITEENABLE_GREEN ? GL_TRUE : GL_FALSE,
                    This->stateBlock->renderState[WINED3DRS_COLORWRITEENABLE] & WINED3DCOLORWRITEENABLE_BLUE  ? GL_TRUE : GL_FALSE,
                    This->stateBlock->renderState[WINED3DRS_COLORWRITEENABLE] & WINED3DCOLORWRITEENABLE_ALPHA ? GL_TRUE : GL_FALSE);
    }

    glDisable(GL_SCISSOR_TEST);
    checkGLcall("glDisable");
    LEAVE_GL();

    return WINED3D_OK;
}

/*****
 * Drawing functions
 *****/
static HRESULT WINAPI IWineD3DDeviceImpl_DrawPrimitive(IWineD3DDevice *iface, WINED3DPRIMITIVETYPE PrimitiveType, UINT StartVertex,
                                                UINT PrimitiveCount) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    This->stateBlock->streamIsUP = FALSE;

    TRACE("(%p) : Type=(%d,%s), Start=%d, Count=%d\n", This, PrimitiveType,
                               debug_d3dprimitivetype(PrimitiveType),
                               StartVertex, PrimitiveCount);

    if(This->stateBlock->loadBaseVertexIndex != 0) {
        This->stateBlock->loadBaseVertexIndex = 0;
        IWineD3DDeviceImpl_MarkStateDirty(This, STATE_STREAMSRC);
    }
    /* Account for the loading offset due to index buffers. Instead of reloading all sources correct it with the startvertex parameter */
    drawPrimitive(iface, PrimitiveType, PrimitiveCount, StartVertex, 0/* NumVertices */, -1 /* indxStart */,
                  0 /* indxSize */, NULL /* indxData */, 0 /* minIndex */);
    return WINED3D_OK;
}

/* TODO: baseVIndex needs to be provided from This->stateBlock->baseVertexIndex when called from d3d8 */
static HRESULT  WINAPI  IWineD3DDeviceImpl_DrawIndexedPrimitive(IWineD3DDevice *iface,
                                                           WINED3DPRIMITIVETYPE PrimitiveType,
                                                           UINT minIndex, UINT NumVertices, UINT startIndex, UINT primCount) {

    IWineD3DDeviceImpl  *This = (IWineD3DDeviceImpl *)iface;
    UINT                 idxStride = 2;
    IWineD3DIndexBuffer *pIB;
    WINED3DINDEXBUFFER_DESC  IdxBufDsc;

    pIB = This->stateBlock->pIndexData;
    This->stateBlock->streamIsUP = FALSE;

    TRACE("(%p) : Type=(%d,%s), min=%d, CountV=%d, startIdx=%d, countP=%d\n", This,
          PrimitiveType, debug_d3dprimitivetype(PrimitiveType),
          minIndex, NumVertices, startIndex, primCount);

    IWineD3DIndexBuffer_GetDesc(pIB, &IdxBufDsc);
    if (IdxBufDsc.Format == WINED3DFMT_INDEX16) {
        idxStride = 2;
    } else {
        idxStride = 4;
    }

    if(This->stateBlock->loadBaseVertexIndex != This->stateBlock->baseVertexIndex) {
        This->stateBlock->loadBaseVertexIndex = This->stateBlock->baseVertexIndex;
        IWineD3DDeviceImpl_MarkStateDirty(This, STATE_STREAMSRC);
    }

    drawPrimitive(iface, PrimitiveType, primCount, 0, NumVertices, startIndex,
                   idxStride, ((IWineD3DIndexBufferImpl *) pIB)->resource.allocatedMemory, minIndex);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_DrawPrimitiveUP(IWineD3DDevice *iface, WINED3DPRIMITIVETYPE PrimitiveType,
                                                    UINT PrimitiveCount, CONST void* pVertexStreamZeroData,
                                                    UINT VertexStreamZeroStride) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    TRACE("(%p) : Type=(%d,%s), pCount=%d, pVtxData=%p, Stride=%d\n", This, PrimitiveType,
             debug_d3dprimitivetype(PrimitiveType),
             PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);

    /* Note in the following, it's not this type, but that's the purpose of streamIsUP */
    This->stateBlock->streamSource[0] = (IWineD3DVertexBuffer *)pVertexStreamZeroData;
    This->stateBlock->streamStride[0] = VertexStreamZeroStride;
    This->stateBlock->streamIsUP = TRUE;
    This->stateBlock->loadBaseVertexIndex = 0;

    /* TODO: Only mark dirty if drawing from a different UP address */
    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_STREAMSRC);

    drawPrimitive(iface, PrimitiveType, PrimitiveCount, 0 /* start vertex */, 0  /* NumVertices */,
                  0 /* indxStart*/, 0 /* indxSize*/, NULL /* indxData */, 0 /* indxMin */);

    /* MSDN specifies stream zero settings must be set to NULL */
    This->stateBlock->streamStride[0] = 0;
    This->stateBlock->streamSource[0] = NULL;

    /* stream zero settings set to null at end, as per the msdn. No need to mark dirty here, the app has to set
     * the new stream sources or use UP drawing again
     */
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_DrawIndexedPrimitiveUP(IWineD3DDevice *iface, WINED3DPRIMITIVETYPE PrimitiveType,
                                                             UINT MinVertexIndex, UINT NumVertices,
                                                             UINT PrimitiveCount, CONST void* pIndexData,
                                                             WINED3DFORMAT IndexDataFormat,CONST void* pVertexStreamZeroData,
                                                             UINT VertexStreamZeroStride) {
    int                 idxStride;
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    TRACE("(%p) : Type=(%d,%s), MinVtxIdx=%d, NumVIdx=%d, PCount=%d, pidxdata=%p, IdxFmt=%d, pVtxdata=%p, stride=%d\n",
             This, PrimitiveType, debug_d3dprimitivetype(PrimitiveType),
             MinVertexIndex, NumVertices, PrimitiveCount, pIndexData,
             IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride);

    if (IndexDataFormat == WINED3DFMT_INDEX16) {
        idxStride = 2;
    } else {
        idxStride = 4;
    }

    /* Note in the following, it's not this type, but that's the purpose of streamIsUP */
    This->stateBlock->streamSource[0] = (IWineD3DVertexBuffer *)pVertexStreamZeroData;
    This->stateBlock->streamIsUP = TRUE;
    This->stateBlock->streamStride[0] = VertexStreamZeroStride;

    /* Set to 0 as per msdn. Do it now due to the stream source loading during drawPrimitive */
    This->stateBlock->baseVertexIndex = 0;
    This->stateBlock->loadBaseVertexIndex = 0;
    /* Mark the state dirty until we have nicer tracking of the stream source pointers */
    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_VDECL);

    drawPrimitive(iface, PrimitiveType, PrimitiveCount, 0 /* vertexStart */, NumVertices, 0 /* indxStart */, idxStride, pIndexData, MinVertexIndex);

    /* MSDN specifies stream zero settings and index buffer must be set to NULL */
    This->stateBlock->streamSource[0] = NULL;
    This->stateBlock->streamStride[0] = 0;
    This->stateBlock->pIndexData = NULL;
    /* No need to mark the stream source state dirty here. Either the app calls UP drawing again, or it has to call
     * SetStreamSource to specify a vertex buffer
     */

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_DrawPrimitiveStrided (IWineD3DDevice *iface, WINED3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, WineDirect3DVertexStridedData *DrawPrimStrideData) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;

    /* Mark the state dirty until we have nicer tracking
     * its fine to change baseVertexIndex because that call is only called by ddraw which does not need
     * that value.
     */
    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_VDECL);
    This->stateBlock->baseVertexIndex = 0;
    This->up_strided = DrawPrimStrideData;
    drawPrimitive(iface, PrimitiveType, PrimitiveCount, 0, 0, 0, 0, NULL, 0);
    This->up_strided = NULL;
    return WINED3D_OK;
}
 /* Yet another way to update a texture, some apps use this to load default textures instead of using surface/texture lock/unlock */
static HRESULT WINAPI IWineD3DDeviceImpl_UpdateTexture (IWineD3DDevice *iface, IWineD3DBaseTexture *pSourceTexture,  IWineD3DBaseTexture *pDestinationTexture){
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    HRESULT hr = WINED3D_OK;
    WINED3DRESOURCETYPE sourceType;
    WINED3DRESOURCETYPE destinationType;
    int i ,levels;

    /* TODO: think about moving the code into IWineD3DBaseTexture  */

    TRACE("(%p) Source %p Destination %p\n", This, pSourceTexture, pDestinationTexture);

    /* verify that the source and destination textures aren't NULL */
    if (NULL == pSourceTexture || NULL == pDestinationTexture) {
        WARN("(%p) : source (%p) and destination (%p) textures must not be NULL, returning WINED3DERR_INVALIDCALL\n",
             This, pSourceTexture, pDestinationTexture);
        hr = WINED3DERR_INVALIDCALL;
    }

    if (pSourceTexture == pDestinationTexture) {
        WARN("(%p) : source (%p) and destination (%p) textures must be different, returning WINED3DERR_INVALIDCALL\n",
             This, pSourceTexture, pDestinationTexture);
        hr = WINED3DERR_INVALIDCALL;
    }
    /* Verify that the source and destination textures are the same type */
    sourceType      = IWineD3DBaseTexture_GetType(pSourceTexture);
    destinationType = IWineD3DBaseTexture_GetType(pDestinationTexture);

    if (sourceType != destinationType) {
        WARN("(%p) Sorce and destination types must match, returning WINED3DERR_INVALIDCALL\n",
             This);
        hr = WINED3DERR_INVALIDCALL;
    }

    /* check that both textures have the identical numbers of levels  */
    if (IWineD3DBaseTexture_GetLevelCount(pDestinationTexture)  != IWineD3DBaseTexture_GetLevelCount(pSourceTexture)) {
        WARN("(%p) : source (%p) and destination (%p) textures must have identicle numbers of levels, returning WINED3DERR_INVALIDCALL\n", This, pSourceTexture, pDestinationTexture);
        hr = WINED3DERR_INVALIDCALL;
    }

    if (WINED3D_OK == hr) {

        /* Make sure that the destination texture is loaded */
        IWineD3DBaseTexture_PreLoad(pDestinationTexture);

        /* Update every surface level of the texture */
        levels = IWineD3DBaseTexture_GetLevelCount(pDestinationTexture);

        switch (sourceType) {
        case WINED3DRTYPE_TEXTURE:
            {
                IWineD3DSurface *srcSurface;
                IWineD3DSurface *destSurface;

                for (i = 0 ; i < levels ; ++i) {
                    IWineD3DTexture_GetSurfaceLevel((IWineD3DTexture *)pSourceTexture,      i, &srcSurface);
                    IWineD3DTexture_GetSurfaceLevel((IWineD3DTexture *)pDestinationTexture, i, &destSurface);
                    hr = IWineD3DDevice_UpdateSurface(iface, srcSurface, NULL, destSurface, NULL);
                    IWineD3DSurface_Release(srcSurface);
                    IWineD3DSurface_Release(destSurface);
                    if (WINED3D_OK != hr) {
                        WARN("(%p) : Call to update surface failed\n", This);
                        return hr;
                    }
                }
            }
            break;
        case WINED3DRTYPE_CUBETEXTURE:
            {
                IWineD3DSurface *srcSurface;
                IWineD3DSurface *destSurface;
                WINED3DCUBEMAP_FACES faceType;

                for (i = 0 ; i < levels ; ++i) {
                    /* Update each cube face */
                    for (faceType = WINED3DCUBEMAP_FACE_POSITIVE_X; faceType <= WINED3DCUBEMAP_FACE_NEGATIVE_Z; ++faceType){
                        hr = IWineD3DCubeTexture_GetCubeMapSurface((IWineD3DCubeTexture *)pSourceTexture,      faceType, i, &srcSurface);
                        if (WINED3D_OK != hr) {
                            FIXME("(%p) : Failed to get src cube surface facetype %d, level %d\n", This, faceType, i);
                        } else {
                            TRACE("Got srcSurface %p\n", srcSurface);
                        }
                        hr = IWineD3DCubeTexture_GetCubeMapSurface((IWineD3DCubeTexture *)pDestinationTexture, faceType, i, &destSurface);
                        if (WINED3D_OK != hr) {
                            FIXME("(%p) : Failed to get src cube surface facetype %d, level %d\n", This, faceType, i);
                        } else {
                            TRACE("Got desrSurface %p\n", destSurface);
                        }
                        hr = IWineD3DDevice_UpdateSurface(iface, srcSurface, NULL, destSurface, NULL);
                        IWineD3DSurface_Release(srcSurface);
                        IWineD3DSurface_Release(destSurface);
                        if (WINED3D_OK != hr) {
                            WARN("(%p) : Call to update surface failed\n", This);
                            return hr;
                        }
                    }
                }
            }
            break;
#if 0 /* TODO: Add support for volume textures */
        case WINED3DRTYPE_VOLUMETEXTURE:
            {
                IWineD3DVolume  srcVolume  = NULL;
                IWineD3DSurface destVolume = NULL;

                for (i = 0 ; i < levels ; ++i) {
                    IWineD3DVolumeTexture_GetVolume((IWineD3DVolumeTexture *)pSourceTexture,      i, &srcVolume);
                    IWineD3DVolumeTexture_GetVolume((IWineD3DVolumeTexture *)pDestinationTexture, i, &destVolume);
                    hr =  IWineD3DFoo_UpdateVolume(iface, srcVolume, NULL, destVolume, NULL);
                    IWineD3DVolume_Release(srcSurface);
                    IWineD3DVolume_Release(destSurface);
                    if (WINED3D_OK != hr) {
                        WARN("(%p) : Call to update volume failed\n", This);
                        return hr;
                    }
                }
            }
            break;
#endif
        default:
            FIXME("(%p) : Unsupported source and destination type\n", This);
            hr = WINED3DERR_INVALIDCALL;
        }
    }

    return hr;
}

static HRESULT  WINAPI  IWineD3DDeviceImpl_GetRenderTargetData(IWineD3DDevice *iface, IWineD3DSurface *pRenderTarget, IWineD3DSurface *pSurface) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    /** TODO: remove remove casts to IWineD3DSurfaceImpl *
    *  NOTE It may be best to move the code into surface to occomplish this
    ****************************************/

    WINED3DSURFACE_DESC surfaceDesc;
    unsigned int surfaceWidth, surfaceHeight;
    glDescriptor *targetGlDescription  = NULL;
    glDescriptor *surfaceGlDescription = NULL;
    IWineD3DSwapChainImpl *container = NULL;
    
    IWineD3DSurface_GetGlDesc(pRenderTarget, &targetGlDescription);
    IWineD3DSurface_GetGlDesc(pSurface,      &surfaceGlDescription);
    memset(&surfaceDesc, 0, sizeof(surfaceDesc));

    surfaceDesc.Width  = &surfaceWidth;
    surfaceDesc.Height = &surfaceHeight;
    IWineD3DSurface_GetDesc(pSurface, &surfaceDesc);
   /* check to see if it's the backbuffer or the frontbuffer being requested (to make sure the data is up to date)*/

    /* Ok, I may need to setup some kind of active swapchain reference on the device */
    IWineD3DSurface_GetContainer(pRenderTarget, &IID_IWineD3DSwapChain, (void **)&container);
    ENTER_GL();
    /* TODO: opengl Context switching for swapchains etc... */
    if (NULL != container  || pRenderTarget == This->render_targets[0] || pRenderTarget == This->depthStencilBuffer) {
        if (NULL != container  && (pRenderTarget == container->backBuffer[0])) {
            glReadBuffer(GL_BACK);
            vcheckGLcall("glReadBuffer(GL_BACK)");
        } else if ((NULL != container  && (pRenderTarget == container->frontBuffer)) || (pRenderTarget == This->render_targets[0])) {
            glReadBuffer(GL_FRONT);
            vcheckGLcall("glReadBuffer(GL_FRONT)");
        } else if (pRenderTarget == This->depthStencilBuffer) {
            FIXME("Reading of depthstencil not yet supported\n");
        }

        glReadPixels(0,
                    0,
                    surfaceWidth,
                    surfaceHeight,
                    surfaceGlDescription->glFormat,
                    surfaceGlDescription->glType,
                    (void *)IWineD3DSurface_GetData(pSurface));
        vcheckGLcall("glReadPixels(...)");
        if(NULL != container ){
            IWineD3DSwapChain_Release((IWineD3DSwapChain*) container);
        }
    } else {
        IWineD3DBaseTexture *container;
        GLenum textureDimensions = GL_TEXTURE_2D;

        if (WINED3D_OK == IWineD3DSurface_GetContainer(pSurface, &IID_IWineD3DBaseTexture, (void **)&container)) {
            textureDimensions = IWineD3DBaseTexture_GetTextureDimensions(container);
            IWineD3DBaseTexture_Release(container);
        }
        /* TODO: 2D -> Cube surface coppies etc.. */
        if (surfaceGlDescription->target != textureDimensions) {
            FIXME("(%p) : Texture dimension mismatch\n", This);
        }
        glEnable(textureDimensions);
        vcheckGLcall("glEnable(GL_TEXTURE_...)");
        /* FIXME: this isn't correct, it need to add a dirty rect if nothing else... */
        glBindTexture(targetGlDescription->target, targetGlDescription->textureName);
        vcheckGLcall("glBindTexture");
        glGetTexImage(surfaceGlDescription->target,
                        surfaceGlDescription->level,
                        surfaceGlDescription->glFormat,
                        surfaceGlDescription->glType,
                        (void *)IWineD3DSurface_GetData(pSurface));
        glDisable(textureDimensions);
        vcheckGLcall("glDisable(GL_TEXTURE_...)");

    }
    LEAVE_GL();
    return WINED3D_OK;
}

static HRESULT  WINAPI  IWineD3DDeviceImpl_GetFrontBufferData(IWineD3DDevice *iface,UINT iSwapChain, IWineD3DSurface *pDestSurface) {
    IWineD3DSwapChain *swapChain;
    HRESULT hr;
    hr = IWineD3DDeviceImpl_GetSwapChain(iface,  iSwapChain, (IWineD3DSwapChain **)&swapChain);
    if(hr == WINED3D_OK) {
        hr = IWineD3DSwapChain_GetFrontBufferData(swapChain, pDestSurface);
                IWineD3DSwapChain_Release(swapChain);
    }
    return hr;
}

static HRESULT  WINAPI  IWineD3DDeviceImpl_ValidateDevice(IWineD3DDevice *iface, DWORD* pNumPasses) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    /* return a sensible default */
    *pNumPasses = 1;
    /* TODO: If the window is minimized then validate device should return something other than WINED3D_OK */
    FIXME("(%p) : stub\n", This);
    return WINED3D_OK;
}

static HRESULT  WINAPI  IWineD3DDeviceImpl_SetPaletteEntries(IWineD3DDevice *iface, UINT PaletteNumber, CONST PALETTEENTRY* pEntries) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    int j;
    TRACE("(%p) : PaletteNumber %u\n", This, PaletteNumber);
    if ( PaletteNumber < 0 || PaletteNumber >= MAX_PALETTES) {
        WARN("(%p) : (%u) Out of range 0-%u, returning Invalid Call\n", This, PaletteNumber, MAX_PALETTES);
        return WINED3DERR_INVALIDCALL;
    }
    for (j = 0; j < 256; ++j) {
        This->palettes[PaletteNumber][j].peRed   = pEntries[j].peRed;
        This->palettes[PaletteNumber][j].peGreen = pEntries[j].peGreen;
        This->palettes[PaletteNumber][j].peBlue  = pEntries[j].peBlue;
        This->palettes[PaletteNumber][j].peFlags = pEntries[j].peFlags;
    }
    TRACE("(%p) : returning\n", This);
    return WINED3D_OK;
}

static HRESULT  WINAPI  IWineD3DDeviceImpl_GetPaletteEntries(IWineD3DDevice *iface, UINT PaletteNumber, PALETTEENTRY* pEntries) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    int j;
    TRACE("(%p) : PaletteNumber %u\n", This, PaletteNumber);
    if ( PaletteNumber < 0 || PaletteNumber >= MAX_PALETTES) {
        WARN("(%p) : (%u) Out of range 0-%u, returning Invalid Call\n", This, PaletteNumber, MAX_PALETTES);
        return WINED3DERR_INVALIDCALL;
    }
    for (j = 0; j < 256; ++j) {
        pEntries[j].peRed   = This->palettes[PaletteNumber][j].peRed;
        pEntries[j].peGreen = This->palettes[PaletteNumber][j].peGreen;
        pEntries[j].peBlue  = This->palettes[PaletteNumber][j].peBlue;
        pEntries[j].peFlags = This->palettes[PaletteNumber][j].peFlags;
    }
    TRACE("(%p) : returning\n", This);
    return WINED3D_OK;
}

static HRESULT  WINAPI  IWineD3DDeviceImpl_SetCurrentTexturePalette(IWineD3DDevice *iface, UINT PaletteNumber) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : PaletteNumber %u\n", This, PaletteNumber);
    if ( PaletteNumber < 0 || PaletteNumber >= MAX_PALETTES) {
        WARN("(%p) : (%u) Out of range 0-%u, returning Invalid Call\n", This, PaletteNumber, MAX_PALETTES);
        return WINED3DERR_INVALIDCALL;
    }
    /*TODO: stateblocks */
    This->currentPalette = PaletteNumber;
    TRACE("(%p) : returning\n", This);
    return WINED3D_OK;
}

static HRESULT  WINAPI  IWineD3DDeviceImpl_GetCurrentTexturePalette(IWineD3DDevice *iface, UINT* PaletteNumber) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    if (PaletteNumber == NULL) {
        WARN("(%p) : returning Invalid Call\n", This);
        return WINED3DERR_INVALIDCALL;
    }
    /*TODO: stateblocks */
    *PaletteNumber = This->currentPalette;
    TRACE("(%p) : returning  %u\n", This, *PaletteNumber);
    return WINED3D_OK;
}

static HRESULT  WINAPI  IWineD3DDeviceImpl_SetSoftwareVertexProcessing(IWineD3DDevice *iface, BOOL bSoftware) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    static BOOL showFixmes = TRUE;
    if (showFixmes) {
        FIXME("(%p) : stub\n", This);
        showFixmes = FALSE;
    }

    This->softwareVertexProcessing = bSoftware;
    return WINED3D_OK;
}


static BOOL     WINAPI  IWineD3DDeviceImpl_GetSoftwareVertexProcessing(IWineD3DDevice *iface) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    static BOOL showFixmes = TRUE;
    if (showFixmes) {
        FIXME("(%p) : stub\n", This);
        showFixmes = FALSE;
    }
    return This->softwareVertexProcessing;
}


static HRESULT  WINAPI  IWineD3DDeviceImpl_GetRasterStatus(IWineD3DDevice *iface, UINT iSwapChain, WINED3DRASTER_STATUS* pRasterStatus) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DSwapChain *swapChain;
    HRESULT hr;

    TRACE("(%p) :  SwapChain %d returning %p\n", This, iSwapChain, pRasterStatus);

    hr = IWineD3DDeviceImpl_GetSwapChain(iface,  iSwapChain, (IWineD3DSwapChain **)&swapChain);
    if(hr == WINED3D_OK){
        hr = IWineD3DSwapChain_GetRasterStatus(swapChain, pRasterStatus);
        IWineD3DSwapChain_Release(swapChain);
    }else{
        FIXME("(%p) IWineD3DSwapChain_GetRasterStatus returned in error\n", This);
    }
    return hr;
}


static HRESULT  WINAPI  IWineD3DDeviceImpl_SetNPatchMode(IWineD3DDevice *iface, float nSegments) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    static BOOL showfixmes = TRUE;
    if(nSegments != 0.0f) {
        if( showfixmes) {
            FIXME("(%p) : stub nSegments(%f)\n", This, nSegments);
            showfixmes = FALSE;
        }
    }
    return WINED3D_OK;
}

static float    WINAPI  IWineD3DDeviceImpl_GetNPatchMode(IWineD3DDevice *iface) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    static BOOL showfixmes = TRUE;
    if( showfixmes) {
        FIXME("(%p) : stub returning(%f)\n", This, 0.0f);
        showfixmes = FALSE;
    }
    return 0.0f;
}

static HRESULT  WINAPI  IWineD3DDeviceImpl_UpdateSurface(IWineD3DDevice *iface, IWineD3DSurface *pSourceSurface, CONST RECT* pSourceRect, IWineD3DSurface *pDestinationSurface, CONST POINT* pDestPoint) {
    IWineD3DDeviceImpl  *This         = (IWineD3DDeviceImpl *) iface;
    /** TODO: remove casts to IWineD3DSurfaceImpl
     *       NOTE: move code to surface to accomplish this
      ****************************************/
    IWineD3DSurfaceImpl *pSrcSurface  = (IWineD3DSurfaceImpl *)pSourceSurface;
    int srcWidth, srcHeight;
    unsigned int  srcSurfaceWidth, srcSurfaceHeight, destSurfaceWidth, destSurfaceHeight;
    WINED3DFORMAT destFormat, srcFormat;
    UINT          destSize;
    int srcLeft, destLeft, destTop;
    WINED3DPOOL       srcPool, destPool;
    int offset    = 0;
    int rowoffset = 0; /* how many bytes to add onto the end of a row to wraparound to the beginning of the next */
    glDescriptor *glDescription = NULL;
    GLenum textureDimensions = GL_TEXTURE_2D;
    IWineD3DBaseTexture *baseTexture;

    WINED3DSURFACE_DESC  winedesc;

    TRACE("(%p) : Source (%p)  Rect (%p) Destination (%p) Point(%p)\n", This, pSourceSurface, pSourceRect, pDestinationSurface, pDestPoint);
    memset(&winedesc, 0, sizeof(winedesc));
    winedesc.Width  = &srcSurfaceWidth;
    winedesc.Height = &srcSurfaceHeight;
    winedesc.Pool   = &srcPool;
    winedesc.Format = &srcFormat;

    IWineD3DSurface_GetDesc(pSourceSurface, &winedesc);

    winedesc.Width  = &destSurfaceWidth;
    winedesc.Height = &destSurfaceHeight;
    winedesc.Pool   = &destPool;
    winedesc.Format = &destFormat;
    winedesc.Size   = &destSize;

    IWineD3DSurface_GetDesc(pDestinationSurface, &winedesc);

    if(srcPool != WINED3DPOOL_SYSTEMMEM  || destPool != WINED3DPOOL_DEFAULT){
        WARN("source %p must be SYSTEMMEM and dest %p must be DEFAULT, returning WINED3DERR_INVALIDCALL\n", pSourceSurface, pDestinationSurface);
        return WINED3DERR_INVALIDCALL;
    }

    if (destFormat == WINED3DFMT_UNKNOWN) {
        TRACE("(%p) : Converting destination surface from WINED3DFMT_UNKNOWN to the source format\n", This);
        IWineD3DSurface_SetFormat(pDestinationSurface, srcFormat);

        /* Get the update surface description */
        IWineD3DSurface_GetDesc(pDestinationSurface, &winedesc);
    }

    /* Make sure the surface is loaded and up to date */
    IWineD3DSurface_PreLoad(pDestinationSurface);

    IWineD3DSurface_GetGlDesc(pDestinationSurface, &glDescription);

    ENTER_GL();

    /* this needs to be done in lines if the sourceRect != the sourceWidth */
    srcWidth   = pSourceRect ? pSourceRect->right - pSourceRect->left   : srcSurfaceWidth;
    srcHeight  = pSourceRect ? pSourceRect->top   - pSourceRect->bottom : srcSurfaceHeight;
    srcLeft    = pSourceRect ? pSourceRect->left : 0;
    destLeft   = pDestPoint  ? pDestPoint->x : 0;
    destTop    = pDestPoint  ? pDestPoint->y : 0;


    /* This function doesn't support compressed textures
    the pitch is just bytesPerPixel * width */
    if(srcWidth != srcSurfaceWidth  || srcLeft ){
        rowoffset = (srcSurfaceWidth - srcWidth) * pSrcSurface->bytesPerPixel;
        offset   += srcLeft * pSrcSurface->bytesPerPixel;
        /* TODO: do we ever get 3bpp?, would a shift and an add be quicker than a mul (well maybe a cycle or two) */
    }
    /* TODO DXT formats */

    if(pSourceRect != NULL && pSourceRect->top != 0){
       offset +=  pSourceRect->top * srcWidth * pSrcSurface->bytesPerPixel;
    }
    TRACE("(%p) glTexSubImage2D, Level %d, left %d, top %d, width %d, height %d , ftm %d, type %d, memory %p\n"
    ,This
    ,glDescription->level
    ,destLeft
    ,destTop
    ,srcWidth
    ,srcHeight
    ,glDescription->glFormat
    ,glDescription->glType
    ,IWineD3DSurface_GetData(pSourceSurface)
    );

    /* Sanity check */
    if (IWineD3DSurface_GetData(pSourceSurface) == NULL) {

        /* need to lock the surface to get the data */
        FIXME("Surfaces has no allocated memory, but should be an in memory only surface\n");
    }

    /* TODO: Cube and volume support */
    if(rowoffset != 0){
        /* not a whole row so we have to do it a line at a time */
        int j;

        /* hopefully using pointer addtion will be quicker than using a point + j * rowoffset */
        const unsigned char* data =((const unsigned char *)IWineD3DSurface_GetData(pSourceSurface)) + offset;

        for(j = destTop ; j < (srcHeight + destTop) ; j++){

                glTexSubImage2D(glDescription->target
                    ,glDescription->level
                    ,destLeft
                    ,j
                    ,srcWidth
                    ,1
                    ,glDescription->glFormat
                    ,glDescription->glType
                    ,data /* could be quicker using */
                );
            data += rowoffset;
        }

    } else { /* Full width, so just write out the whole texture */

        if (WINED3DFMT_DXT1 == destFormat ||
            WINED3DFMT_DXT2 == destFormat ||
            WINED3DFMT_DXT3 == destFormat ||
            WINED3DFMT_DXT4 == destFormat ||
            WINED3DFMT_DXT5 == destFormat) {
            if (GL_SUPPORT(EXT_TEXTURE_COMPRESSION_S3TC)) {
                if (destSurfaceHeight != srcHeight || destSurfaceWidth != srcWidth) {
                    /* FIXME: The easy way to do this is to lock the destination, and copy the bits across */
                    FIXME("Updating part of a compressed texture is not supported at the moment\n");
                } if (destFormat != srcFormat) {
                    FIXME("Updating mixed format compressed texture is not curretly support\n");
                } else {
                    GL_EXTCALL(glCompressedTexImage2DARB)(glDescription->target,
                                                        glDescription->level,
                                                        glDescription->glFormatInternal,
                                                        srcWidth,
                                                        srcHeight,
                                                        0,
                                                        destSize,
                                                        IWineD3DSurface_GetData(pSourceSurface));
                }
            } else {
                FIXME("Attempting to update a DXT compressed texture without hardware support\n");
            }


        } else {
            if (NP2_REPACK == wined3d_settings.nonpower2_mode) {

                /* some applications cannot handle odd pitches returned by soft non-power2, so we have
                to repack the data from pow2Width/Height to expected Width,Height, this makes the
                data returned by GetData non-power2 width/height with hardware non-power2
                pow2Width/height are set to surface width height, repacking isn't needed so it
                doesn't matter which function gets called. */
                glTexSubImage2D(glDescription->target
                        ,glDescription->level
                        ,destLeft
                        ,destTop
                        ,srcWidth
                        ,srcHeight
                        ,glDescription->glFormat
                        ,glDescription->glType
                        ,IWineD3DSurface_GetData(pSourceSurface)
                    );
            } else {

                /* not repacked, the data returned by IWineD3DSurface_GetData is pow2Width x pow2Height */
                glTexSubImage2D(glDescription->target
                    ,glDescription->level
                    ,destLeft
                    ,destTop
                    ,((IWineD3DSurfaceImpl *)pSourceSurface)->pow2Width
                    ,((IWineD3DSurfaceImpl *)pSourceSurface)->pow2Height
                    ,glDescription->glFormat
                    ,glDescription->glType
                    ,IWineD3DSurface_GetData(pSourceSurface)
                );
            }

        }
     }
    checkGLcall("glTexSubImage2D");
    ((IWineD3DSurfaceImpl *)pDestinationSurface)->Flags |= SFLAG_GLDIRTY;

    /* I only need to look up baseTexture here, so it may be a good idea to hava a GL_TARGET ->
     * GL_DIMENSIONS lookup, or maybe store the dimensions on the surface (but that's making the
     * surface bigger than it needs to be hmm.. */
    if (WINED3D_OK == IWineD3DSurface_GetContainer(pDestinationSurface, &IID_IWineD3DBaseTexture, (void **)&baseTexture)) {
        textureDimensions = IWineD3DBaseTexture_GetTextureDimensions(baseTexture);
        IWineD3DBaseTexture_Release(baseTexture);
    }

    glDisable(textureDimensions); /* This needs to be managed better.... */
    LEAVE_GL();

    return WINED3D_OK;
}

/* Implementation details at http://developer.nvidia.com/attach/6494
and
http://oss.sgi.com/projects/ogl-sample/registry/NV/evaluators.txt
hmm.. no longer supported use
OpenGL evaluators or  tessellate surfaces within your application.
*/

/* http://msdn.microsoft.com/library/default.asp?url=/library/en-us/directx9_c/directx/graphics/reference/d3d/interfaces/idirect3ddevice9/DrawRectPatch.asp */
static HRESULT WINAPI IWineD3DDeviceImpl_DrawRectPatch(IWineD3DDevice *iface, UINT Handle, CONST float* pNumSegs, CONST WINED3DRECTPATCH_INFO* pRectPatchInfo) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) Handle(%d) noSegs(%p) rectpatch(%p)\n", This, Handle, pNumSegs, pRectPatchInfo);
    FIXME("(%p) : Stub\n", This);
    return WINED3D_OK;

}

/* http://msdn.microsoft.com/library/default.asp?url=/library/en-us/directx9_c/directx/graphics/reference/d3d/interfaces/idirect3ddevice9/DrawTriPatch.asp */
static HRESULT WINAPI IWineD3DDeviceImpl_DrawTriPatch(IWineD3DDevice *iface, UINT Handle, CONST float* pNumSegs, CONST WINED3DTRIPATCH_INFO* pTriPatchInfo) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) Handle(%d) noSegs(%p) tripatch(%p)\n", This, Handle, pNumSegs, pTriPatchInfo);
    FIXME("(%p) : Stub\n", This);
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_DeletePatch(IWineD3DDevice *iface, UINT Handle) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) Handle(%d)\n", This, Handle);
    FIXME("(%p) : Stub\n", This);
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_ColorFill(IWineD3DDevice *iface, IWineD3DSurface *pSurface, CONST WINED3DRECT* pRect, WINED3DCOLOR color) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;
    IWineD3DSurfaceImpl *surface = (IWineD3DSurfaceImpl *) pSurface;
    DDBLTFX BltFx;
    TRACE("(%p) Colour fill Surface: %p rect: %p color: %d\n", This, pSurface, pRect, color);

    if (surface->resource.pool != WINED3DPOOL_DEFAULT && surface->resource.pool != WINED3DPOOL_SYSTEMMEM) {
        FIXME("call to colorfill with non WINED3DPOOL_DEFAULT or WINED3DPOOL_SYSTEMMEM surface\n");
        return WINED3DERR_INVALIDCALL;
    }

    /* Just forward this to the DirectDraw blitting engine */
    memset(&BltFx, 0, sizeof(BltFx));
    BltFx.dwSize = sizeof(BltFx);
    BltFx.u5.dwFillColor = color;
    return IWineD3DSurface_Blt(pSurface, (RECT *) pRect, NULL, NULL, DDBLT_COLORFILL, &BltFx);
}

/* rendertarget and deptth stencil functions */
static HRESULT  WINAPI  IWineD3DDeviceImpl_GetRenderTarget(IWineD3DDevice* iface,DWORD RenderTargetIndex, IWineD3DSurface **ppRenderTarget) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    if (RenderTargetIndex >= GL_LIMITS(buffers)) {
        ERR("(%p) : Only %d render targets are supported.\n", This, GL_LIMITS(buffers));
        return WINED3DERR_INVALIDCALL;
    }

    *ppRenderTarget = This->render_targets[RenderTargetIndex];
    TRACE("(%p) : RenderTarget %d Index returning %p\n", This, RenderTargetIndex, *ppRenderTarget);
    /* Note inc ref on returned surface */
    if(*ppRenderTarget != NULL)
        IWineD3DSurface_AddRef(*ppRenderTarget);
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetFrontBackBuffers(IWineD3DDevice *iface, IWineD3DSurface *Front, IWineD3DSurface *Back) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DSurfaceImpl *FrontImpl = (IWineD3DSurfaceImpl *) Front;
    IWineD3DSurfaceImpl *BackImpl = (IWineD3DSurfaceImpl *) Back;
    IWineD3DSwapChainImpl *Swapchain;
    HRESULT hr;

    TRACE("(%p)->(%p,%p)\n", This, FrontImpl, BackImpl);

    hr = IWineD3DDevice_GetSwapChain(iface, 0, (IWineD3DSwapChain **) &Swapchain);
    if(hr != WINED3D_OK) {
        ERR("Can't get the swapchain\n");
        return hr;
    }

    /* Make sure to release the swapchain */
    IWineD3DSwapChain_Release((IWineD3DSwapChain *) Swapchain);

    if(FrontImpl && !(FrontImpl->resource.usage & WINED3DUSAGE_RENDERTARGET) ) {
        ERR("Trying to set a front buffer which doesn't have WINED3DUSAGE_RENDERTARGET usage\n");
        return WINED3DERR_INVALIDCALL;
    }
    else if(BackImpl && !(BackImpl->resource.usage & WINED3DUSAGE_RENDERTARGET)) {
        ERR("Trying to set a back buffer which doesn't have WINED3DUSAGE_RENDERTARGET usage\n");
        return WINED3DERR_INVALIDCALL;
    }

    if(Swapchain->frontBuffer != Front) {
        TRACE("Changing the front buffer from %p to %p\n", Swapchain->frontBuffer, Front);

        if(Swapchain->frontBuffer)
            IWineD3DSurface_SetContainer(Swapchain->frontBuffer, NULL);
        Swapchain->frontBuffer = Front;

        if(Swapchain->frontBuffer) {
            IWineD3DSurface_SetContainer(Swapchain->frontBuffer, (IWineD3DBase *) Swapchain);
        }
    }

    if(Back && !Swapchain->backBuffer) {
        /* We need memory for the back buffer array - only one back buffer this way */
        Swapchain->backBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IWineD3DSurface *));
        if(!Swapchain->backBuffer) {
            ERR("Out of memory\n");
            return E_OUTOFMEMORY;
        }
    }

    if(Swapchain->backBuffer[0] != Back) {
        TRACE("Changing the back buffer from %p to %p\n", Swapchain->backBuffer, Back);
        ENTER_GL();
        if(!Swapchain->backBuffer[0]) {
            /* GL was told to draw to the front buffer at creation,
             * undo that
             */
            glDrawBuffer(GL_BACK);
            checkGLcall("glDrawBuffer(GL_BACK)");
            /* Set the backbuffer count to 1 because other code uses it to fing the back buffers */
            Swapchain->presentParms.BackBufferCount = 1;
        } else if (!Back) {
            /* That makes problems - disable for now */
            /* glDrawBuffer(GL_FRONT); */
            checkGLcall("glDrawBuffer(GL_FRONT)");
            /* We have lost our back buffer, set this to 0 to avoid confusing other code */
            Swapchain->presentParms.BackBufferCount = 0;
        }
        LEAVE_GL();

        if(Swapchain->backBuffer[0])
            IWineD3DSurface_SetContainer(Swapchain->backBuffer[0], NULL);
        Swapchain->backBuffer[0] = Back;

        if(Swapchain->backBuffer[0]) {
            IWineD3DSurface_SetContainer(Swapchain->backBuffer[0], (IWineD3DBase *) Swapchain);
        } else {
            HeapFree(GetProcessHeap(), 0, Swapchain->backBuffer);
        }

    }

    return WINED3D_OK;
}

static HRESULT  WINAPI  IWineD3DDeviceImpl_GetDepthStencilSurface(IWineD3DDevice* iface, IWineD3DSurface **ppZStencilSurface) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    *ppZStencilSurface = This->depthStencilBuffer;
    TRACE("(%p) : zStencilSurface  returning %p\n", This,  *ppZStencilSurface);

    if(*ppZStencilSurface != NULL) {
        /* Note inc ref on returned surface */
        IWineD3DSurface_AddRef(*ppZStencilSurface);
    }
    return WINED3D_OK;
}

static void bind_fbo(IWineD3DDevice *iface) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    if (!This->fbo) {
        GL_EXTCALL(glGenFramebuffersEXT(1, &This->fbo));
        checkGLcall("glGenFramebuffersEXT()");
    }
    GL_EXTCALL(glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, This->fbo));
    checkGLcall("glBindFramebuffer()");
}

/* TODO: Handle stencil attachments */
static void set_depth_stencil_fbo(IWineD3DDevice *iface, IWineD3DSurface *depth_stencil) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DSurfaceImpl *depth_stencil_impl = (IWineD3DSurfaceImpl *)depth_stencil;

    This->depth_copy_state = WINED3D_DCS_NO_COPY;

    bind_fbo(iface);

    if (depth_stencil_impl) {
        GLenum texttarget, target;
        GLint old_binding = 0;

        IWineD3DSurface_PreLoad(depth_stencil);
        texttarget = depth_stencil_impl->glDescription.target;
        target = texttarget == GL_TEXTURE_2D ? GL_TEXTURE_2D : GL_TEXTURE_CUBE_MAP_ARB;

        glGetIntegerv(texttarget == GL_TEXTURE_2D ? GL_TEXTURE_BINDING_2D : GL_TEXTURE_BINDING_CUBE_MAP_ARB, &old_binding);
        glBindTexture(target, depth_stencil_impl->glDescription.textureName);
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(target, GL_DEPTH_TEXTURE_MODE_ARB, GL_LUMINANCE);
        glBindTexture(target, old_binding);

        GL_EXTCALL(glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, texttarget, depth_stencil_impl->glDescription.textureName, 0));
        checkGLcall("glFramebufferTexture2DEXT()");
    } else {
        GL_EXTCALL(glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_TEXTURE_2D, 0, 0));
        checkGLcall("glFramebufferTexture2DEXT()");
    }

    if (!This->render_offscreen) {
        GL_EXTCALL(glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0));
        checkGLcall("glBindFramebuffer()");
    }
}

static void set_render_target_fbo(IWineD3DDevice *iface, DWORD idx, IWineD3DSurface *render_target) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DSurfaceImpl *rtimpl = (IWineD3DSurfaceImpl *)render_target;

    if (idx >= GL_LIMITS(buffers)) {
        ERR("%p : Trying to set render target %d, but only %d supported\n", This, idx, GL_LIMITS(buffers));
    }

    bind_fbo(iface);

    if (rtimpl) {
        GLenum texttarget, target;
        GLint old_binding = 0;

        IWineD3DSurface_PreLoad(render_target);
        texttarget = rtimpl->glDescription.target;
        target = texttarget == GL_TEXTURE_2D ? GL_TEXTURE_2D : GL_TEXTURE_CUBE_MAP_ARB;

        glGetIntegerv(texttarget == GL_TEXTURE_2D ? GL_TEXTURE_BINDING_2D : GL_TEXTURE_BINDING_CUBE_MAP_ARB, &old_binding);
        glBindTexture(target, rtimpl->glDescription.textureName);
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(target, old_binding);

        GL_EXTCALL(glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT + idx, texttarget, rtimpl->glDescription.textureName, 0));
        checkGLcall("glFramebufferTexture2DEXT()");

        This->draw_buffers[idx] = GL_COLOR_ATTACHMENT0_EXT + idx;
    } else {
        GL_EXTCALL(glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT + idx, GL_TEXTURE_2D, 0, 0));
        checkGLcall("glFramebufferTexture2DEXT()");

        This->draw_buffers[idx] = GL_NONE;
    }

    if (GL_SUPPORT(ARB_DRAW_BUFFERS)) {
        GL_EXTCALL(glDrawBuffersARB(GL_LIMITS(buffers), This->draw_buffers));
        checkGLcall("glDrawBuffers()");
    }

    if (!This->render_offscreen) {
        GL_EXTCALL(glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0));
        checkGLcall("glBindFramebuffer()");
    }
}

/* internal static helper functions */
static HRESULT WINAPI IWineD3DDeviceImpl_ActiveRender(IWineD3DDevice* iface,
                                                IWineD3DSurface *RenderSurface);

static HRESULT WINAPI IWineD3DDeviceImpl_SetRenderTarget(IWineD3DDevice *iface, DWORD RenderTargetIndex, IWineD3DSurface *pRenderTarget) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    HRESULT  hr = WINED3D_OK;
    WINED3DVIEWPORT viewport;

    TRACE("(%p) : Setting rendertarget %d to %p\n", This, RenderTargetIndex, pRenderTarget);

    if (RenderTargetIndex >= GL_LIMITS(buffers)) {
        ERR("(%p) : Only %d render targets are supported.\n", This, GL_LIMITS(buffers));
        return WINED3DERR_INVALIDCALL;
    }

    /* MSDN says that null disables the render target
    but a device must always be associated with a render target
    nope MSDN says that we return invalid call to a null rendertarget with an index of 0

    see http://msdn.microsoft.com/library/default.asp?url=/library/en-us/directx9_c/directx/graphics/programmingguide/AdvancedTopics/PixelPipe/MultipleRenderTarget.asp
    for more details
    */
    if (RenderTargetIndex == 0 && pRenderTarget == NULL) {
        FIXME("Trying to set render target 0 to NULL\n");
        return WINED3DERR_INVALIDCALL;
    }
    /* TODO: replace Impl* usage with interface usage */
    if (pRenderTarget && !((IWineD3DSurfaceImpl *)pRenderTarget)->resource.usage & WINED3DUSAGE_RENDERTARGET) {
        FIXME("(%p)Trying to set the render target to a surface(%p) that wasn't created with a usage of WINED3DUSAGE_RENDERTARGET\n",This ,pRenderTarget);
        return WINED3DERR_INVALIDCALL;
    }
    /** TODO: check that the depth stencil format matches the render target, this is only done in debug
     *        builds, but I think wine counts as a 'debug' build for now.
      ******************************/
    /* If we are trying to set what we already have, don't bother */
    if (pRenderTarget == This->render_targets[RenderTargetIndex]) {
        TRACE("Trying to do a NOP SetRenderTarget operation\n");
    } else {
        /* Otherwise, set the render target up */

        if (!This->sceneEnded) {
            IWineD3DDevice_EndScene(iface);
        }
        TRACE("clearing renderer\n");
        /* IWineD3DDeviceImpl_CleanRender(iface); */
        /* OpenGL doesn't support 'sharing' of the stencilBuffer so we may incure an extra memory overhead
        depending on the renter target implementation being used.
        A shared context implementation will share all buffers between all rendertargets (including swapchains),
        implementations that use separate pbuffers for different swapchains or rendertargets will have to duplicate the
        stencil buffer and incure an extra memory overhead */
        if (RenderTargetIndex == 0) {
            hr = IWineD3DDeviceImpl_ActiveRender(iface, pRenderTarget);
        } else {
            hr = WINED3D_OK;
        }

        /* Replace the render target */
        if (This->render_targets[RenderTargetIndex]) IWineD3DSurface_Release(This->render_targets[RenderTargetIndex]);
        This->render_targets[RenderTargetIndex] = pRenderTarget;
        if (pRenderTarget) IWineD3DSurface_AddRef(pRenderTarget);

        if (wined3d_settings.offscreen_rendering_mode == ORM_FBO) {
            set_render_target_fbo(iface, RenderTargetIndex, pRenderTarget);
        }
    }

    if (SUCCEEDED(hr)) {
        /* Finally, reset the viewport as the MSDN states. */
        /* TODO: Replace impl usage */
        viewport.Height = ((IWineD3DSurfaceImpl *)This->render_targets[0])->currentDesc.Height;
        viewport.Width  = ((IWineD3DSurfaceImpl *)This->render_targets[0])->currentDesc.Width;
        viewport.X      = 0;
        viewport.Y      = 0;
        viewport.MaxZ   = 1.0f;
        viewport.MinZ   = 0.0f;
        IWineD3DDeviceImpl_SetViewport(iface, &viewport);
    } else {
        FIXME("Unknown error setting the render target\n");
    }
    This->sceneEnded = FALSE;
    return hr;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetDepthStencilSurface(IWineD3DDevice *iface, IWineD3DSurface *pNewZStencil) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    HRESULT  hr = WINED3D_OK;
    IWineD3DSurface *tmp;

    TRACE("(%p) Swapping z-buffer\n",This);

    if (pNewZStencil == This->stencilBufferTarget) {
        TRACE("Trying to do a NOP SetRenderTarget operation\n");
    } else {
        /** OpenGL doesn't support 'sharing' of the stencilBuffer so we may incure an extra memory overhead
        * depending on the renter target implementation being used.
        * A shared context implementation will share all buffers between all rendertargets (including swapchains),
        * implementations that use separate pbuffers for different swapchains or rendertargets will have to duplicate the
        * stencil buffer and incure an extra memory overhead
         ******************************************************/


        tmp = This->stencilBufferTarget;
        This->stencilBufferTarget = pNewZStencil;
        /* should we be calling the parent or the wined3d surface? */
        if (NULL != This->stencilBufferTarget) IWineD3DSurface_AddRef(This->stencilBufferTarget);
        if (NULL != tmp) IWineD3DSurface_Release(tmp);
        hr = WINED3D_OK;
        /** TODO: glEnable/glDisable on depth/stencil    depending on
         *   pNewZStencil is NULL and the depth/stencil is enabled in d3d
          **********************************************************/
        if (wined3d_settings.offscreen_rendering_mode == ORM_FBO) {
            set_depth_stencil_fbo(iface, pNewZStencil);
        }
    }

    return hr;
}


#ifdef GL_VERSION_1_3
/* Internal functions not in DirectX */
 /** TODO: move this off to the opengl context manager
 *(the swapchain doesn't need to know anything about offscreen rendering!)
  ****************************************************/

static HRESULT WINAPI IWineD3DDeviceImpl_CleanRender(IWineD3DDevice* iface, IWineD3DSwapChainImpl *swapchain)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    TRACE("(%p), %p\n", This, swapchain);

    if (swapchain->win != swapchain->drawable) {
        /* Set everything back the way it ws */
        swapchain->render_ctx = swapchain->glCtx;
        swapchain->drawable   = swapchain->win;
    }
    return WINED3D_OK;
}

/* TODO: move this off into a context manager so that GLX_ATI_render_texture and other types of surface can be used. */
static HRESULT WINAPI IWineD3DDeviceImpl_FindGLContext(IWineD3DDevice *iface, IWineD3DSurface *pSurface, glContext **context) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    int i;
    unsigned int width;
    unsigned int height;
    WINED3DFORMAT format;
    WINED3DSURFACE_DESC surfaceDesc;
    memset(&surfaceDesc, 0, sizeof(surfaceDesc));
    surfaceDesc.Width  = &width;
    surfaceDesc.Height = &height;
    surfaceDesc.Format = &format;
    IWineD3DSurface_GetDesc(pSurface, &surfaceDesc);
    *context = NULL;
    /* I need a get width/height function (and should do something with the format) */
    for (i = 0; i < CONTEXT_CACHE; ++i) {
        /** NOTE: the contextCache[i].pSurface == pSurface check ceates onepbuffer per surface
        ATI cards don't destroy pbuffers, but as soon as resource releasing callbacks are inplace
        the pSurface can be set to 0 allowing it to be reused from cache **/
        if (This->contextCache[i].Width == width && This->contextCache[i].Height == height
          && (!pbuffer_per_surface || This->contextCache[i].pSurface == pSurface || This->contextCache[i].pSurface == NULL)) {
            *context = &This->contextCache[i];
            break;
        }
        if (This->contextCache[i].Width == 0) {
            This->contextCache[i].pSurface = pSurface;
            This->contextCache[i].Width    = width;
            This->contextCache[i].Height   = height;
            *context = &This->contextCache[i];
            break;
        }
    }
    if (i == CONTEXT_CACHE) {
        int minUsage = 0x7FFFFFFF; /* MAX_INT */
        glContext *dropContext = 0;
        for (i = 0; i < CONTEXT_CACHE; i++) {
            if (This->contextCache[i].usedcount < minUsage) {
                dropContext = &This->contextCache[i];
                minUsage = This->contextCache[i].usedcount;
            }
        }
        /* clean up the context (this doesn't work for ATI at the moment */
#if 0
        glXDestroyContext(swapchain->display, dropContext->context);
        glXDestroyPbuffer(swapchain->display, dropContext->drawable);
#endif
        FIXME("Leak\n");
        dropContext->Width = 0;
        dropContext->pSurface = pSurface;
        *context = dropContext;
    } else {
        if (++This->contextCache[i].usedcount == 0x7FFFFFFF /* MAX_INT */ - 1 ) {
          for (i = 0; i < CONTEXT_CACHE; i++) {
             This->contextCache[i].usedcount = max(0, This->contextCache[i].usedcount - (0x7FFFFFFF /* MAX_INT */ >> 1));
          }
        }
    }
    if (*context != NULL)
        return WINED3D_OK;
    else
        return E_OUTOFMEMORY;
}
#endif

/* Reapply the device stateblock */
static void device_reapply_stateblock(IWineD3DDeviceImpl* This) {

    BOOL oldRecording;  
    IWineD3DStateBlockImpl *oldUpdateStateBlock;
    DWORD i;

    /* Disable recording */
    oldUpdateStateBlock = This->updateStateBlock;
    oldRecording= This->isRecordingState;
    This->isRecordingState = FALSE;
    This->updateStateBlock = This->stateBlock;

    /* Reapply the state block */ 
    IWineD3DStateBlock_Apply((IWineD3DStateBlock *)This->stateBlock);

    /* Temporaryily mark all render states dirty to force reapplication
     * until the context management for is integrated with the state management
     * The same for the pixel shader, vertex declaration and vertex shader
     * Sampler states and texture stage states are marked
     * dirty my StateBlock::Apply already.
     */
    for(i = 1; i < WINEHIGHEST_RENDER_STATE; i++) {
        IWineD3DDeviceImpl_MarkStateDirty(This, STATE_RENDER(i));
    }
    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_PIXELSHADER);
    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_VDECL);
    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_VSHADER);

    /* Restore recording */
    This->isRecordingState = oldRecording;
    This->updateStateBlock = oldUpdateStateBlock;
}

/* Set offscreen rendering. When rendering offscreen the surface will be
 * rendered upside down to compensate for the fact that D3D texture coordinates
 * are flipped compared to GL texture coordinates. The cullmode is affected by
 * this, so it must be updated. To update the cullmode stateblock recording has
 * to be temporarily disabled. The new state management code will hopefully
 * make this unnecessary */
static void device_render_to_texture(IWineD3DDeviceImpl* This, BOOL isTexture) {

    BOOL oldRecording;
    IWineD3DStateBlockImpl *oldUpdateStateBlock;

    /* Nothing to update, return. */
    if (This->render_offscreen == isTexture) return;

    /* Disable recording */
    oldUpdateStateBlock = This->updateStateBlock;
    oldRecording= This->isRecordingState;
    This->isRecordingState = FALSE;
    This->updateStateBlock = This->stateBlock;

    This->render_offscreen = isTexture;
    if (This->depth_copy_state != WINED3D_DCS_NO_COPY) {
        This->depth_copy_state = WINED3D_DCS_COPY;
    }
    This->last_was_rhw = FALSE;
    /* Viewport state will reapply the projection matrix for now */
    IWineD3DDeviceImpl_MarkStateDirty(This, WINED3DRS_CULLMODE);

    /* Restore recording */
    This->isRecordingState = oldRecording;
    This->updateStateBlock = oldUpdateStateBlock;
}

/* Returns an array of compatible FBconfig(s).
 * The array must be freed with XFree. Requires ENTER_GL() */

static GLXFBConfig* device_find_fbconfigs(
    IWineD3DDeviceImpl* This,
    IWineD3DSwapChainImpl* implicitSwapchainImpl,
    IWineD3DSurface* RenderSurface) {

    GLXFBConfig* cfgs = NULL;
    int nCfgs = 0;
    int attribs[256];
    int nAttribs = 0;

    IWineD3DSurface *StencilSurface = This->stencilBufferTarget;
    WINED3DFORMAT BackBufferFormat = ((IWineD3DSurfaceImpl *) RenderSurface)->resource.format;
    WINED3DFORMAT StencilBufferFormat = (NULL != StencilSurface) ? ((IWineD3DSurfaceImpl *) StencilSurface)->resource.format : 0;

    /**TODO:
        if StencilSurface == NULL && zBufferTarget != NULL then switch the zbuffer off, 
        it StencilSurface != NULL && zBufferTarget == NULL switch it on
    */

#define PUSH1(att)        attribs[nAttribs++] = (att);
#define PUSH2(att,value)  attribs[nAttribs++] = (att); attribs[nAttribs++] = (value);

    /* PUSH2(GLX_BIND_TO_TEXTURE_RGBA_ATI, True); examples of this are few and far between (but I've got a nice working one!)*/

    PUSH2(GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT);
    PUSH2(GLX_X_RENDERABLE,  TRUE);
    PUSH2(GLX_DOUBLEBUFFER,  TRUE);
    TRACE("calling makeglcfg\n");
    D3DFmtMakeGlCfg(BackBufferFormat, StencilBufferFormat, attribs, &nAttribs, FALSE /* alternate */);
    PUSH1(None);
    TRACE("calling chooseFGConfig\n");
    cfgs = glXChooseFBConfig(implicitSwapchainImpl->display,
                             DefaultScreen(implicitSwapchainImpl->display),
                             attribs, &nCfgs);
    if (cfgs == NULL) {
        /* OK we didn't find the exact config, so use any reasonable match */
        /* TODO: fill in the 'requested' and 'current' depths, and make sure that's
           why we failed. */
        static BOOL show_message = TRUE;
        if (show_message) {
            ERR("Failed to find exact match, finding alternative but you may "
                "suffer performance issues, try changing xfree's depth to match the requested depth\n");
            show_message = FALSE;
        }
        nAttribs = 0;
        PUSH2(GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT | GLX_WINDOW_BIT);
        /* PUSH2(GLX_X_RENDERABLE,  TRUE); */
        PUSH2(GLX_RENDER_TYPE,   GLX_RGBA_BIT);
        PUSH2(GLX_DOUBLEBUFFER, FALSE);
        TRACE("calling makeglcfg\n");
        D3DFmtMakeGlCfg(BackBufferFormat, StencilBufferFormat, attribs, &nAttribs, TRUE /* alternate */);
        PUSH1(None);
        cfgs = glXChooseFBConfig(implicitSwapchainImpl->display,
                                 DefaultScreen(implicitSwapchainImpl->display),
                                 attribs, &nCfgs);
    }
                                                                                                                                                                      
    if (cfgs == NULL) {
        ERR("Could not get a valid FBConfig for (%u,%s)/(%u,%s)\n",
            BackBufferFormat, debug_d3dformat(BackBufferFormat),
            StencilBufferFormat, debug_d3dformat(StencilBufferFormat));
    } else {
#ifdef EXTRA_TRACES
        int i;
        for (i = 0; i < nCfgs; ++i) {
            TRACE("for (%u,%s)/(%u,%s) found config[%d]@%p\n", BackBufferFormat,
            debug_d3dformat(BackBufferFormat), StencilBufferFormat,
            debug_d3dformat(StencilBufferFormat), i, cfgs[i]);
        }
        if (NULL != This->renderTarget) {
            glFlush();
            vcheckGLcall("glFlush");
            /** This is only useful if the old render target was a swapchain,
            * we need to supercede this with a function that displays
            * the current buffer on the screen. This is easy to do in glx1.3 but
            * we need to do copy-write pixels in glx 1.2.
            ************************************************/
            glXSwapBuffers(implicitSwapChainImpl->display,
                           implicitSwapChainImpl->drawable);
            printf("Hit Enter to get next frame ...\n");
            getchar();
        }
#endif
    }
#undef PUSH1
#undef PUSH2

   return cfgs;
}

/** FIXME: This is currently used called whenever SetRenderTarget or SetStencilBuffer are called
* the functionality needs splitting up so that we don't do more than we should do.
* this only seems to impact performance a little.
 ******************************/
static HRESULT WINAPI IWineD3DDeviceImpl_ActiveRender(IWineD3DDevice* iface,
                                               IWineD3DSurface *RenderSurface) {

    /**
    * Currently only active for GLX >= 1.3
    * for others versions we'll have to use GLXPixmaps
    *
    * normally we must test GLX_VERSION_1_3 but nvidia headers are not correct
    * as they implement GLX 1.3 but only define GLX_VERSION_1_2
    * so only check OpenGL version
    * ..........................
    * I don't believe that it is a problem with NVidia headers,
    * XFree only supports GLX1.2, nVidia (and ATI to some extent) provide 1.3 functions
    * in GLX 1.2, there is no mention of the correct way to tell if the extensions are provided.
    * ATI Note:
    * Your application will report GLX version 1.2 on glXQueryVersion.
    * However, it is safe to call the GLX 1.3 functions as described below.
    */
#if defined(GL_VERSION_1_3)

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    GLXFBConfig* cfgs = NULL;
    IWineD3DSwapChain     *currentSwapchain;
    IWineD3DSwapChainImpl *currentSwapchainImpl;
    IWineD3DSwapChain     *implicitSwapchain;
    IWineD3DSwapChainImpl *implicitSwapchainImpl;
    IWineD3DSwapChain     *renderSurfaceSwapchain;
    IWineD3DSwapChainImpl *renderSurfaceSwapchainImpl;

    /* Obtain a reference to the device implicit swapchain,
     * the swapchain of the current render target,
     * and the swapchain of the new render target.
     * Fallback to device implicit swapchain if the current render target doesn't have one */
    IWineD3DDevice_GetSwapChain(iface, 0, &implicitSwapchain);
    IWineD3DSurface_GetContainer(RenderSurface, &IID_IWineD3DSwapChain, (void**) &renderSurfaceSwapchain);
    IWineD3DSurface_GetContainer(This->render_targets[0], &IID_IWineD3DSwapChain, (void **)&currentSwapchain);
    if (currentSwapchain == NULL)
        IWineD3DDevice_GetSwapChain(iface, 0, &currentSwapchain);

    currentSwapchainImpl = (IWineD3DSwapChainImpl*) currentSwapchain;
    implicitSwapchainImpl = (IWineD3DSwapChainImpl*) implicitSwapchain;
    renderSurfaceSwapchainImpl = (IWineD3DSwapChainImpl*) renderSurfaceSwapchain;

    ENTER_GL();

    /**
    * TODO: remove the use of IWineD3DSwapChainImpl, a context manager will help since it will replace the
    *  renderTarget = swapchain->backBuffer[i] bit and anything to do with *glContexts
     **********************************************************************/
    if (renderSurfaceSwapchain != NULL) {

        /* We also need to make sure that the lights &co are also in the context of the swapchains */
        /* FIXME: If the render target gets sent to the frontBuffer, should we be presenting it raw? */
        TRACE("making swapchain active\n");
        if (RenderSurface != This->render_targets[0]) {
            BOOL backbuf = FALSE;
            int i;

            for(i = 0; i < renderSurfaceSwapchainImpl->presentParms.BackBufferCount; i++) {
                if(RenderSurface == renderSurfaceSwapchainImpl->backBuffer[i]) {
                    backbuf = TRUE;
                    break;
                }
            }

            if (backbuf) {
            } else {
                /* This could be flagged so that some operations work directly with the front buffer */
                FIXME("Attempting to set the  renderTarget to the frontBuffer\n");
            }
            if (glXMakeCurrent(renderSurfaceSwapchainImpl->display,
                               renderSurfaceSwapchainImpl->win,
                               renderSurfaceSwapchainImpl->glCtx) == False) {

                TRACE("Error in setting current context: context %p drawable %ld !\n",
                       implicitSwapchainImpl->glCtx, implicitSwapchainImpl->win);
            }
            checkGLcall("glXMakeContextCurrent");

            /* Clean up the old context */
            IWineD3DDeviceImpl_CleanRender(iface, currentSwapchainImpl);

            /* Reapply the stateblock, and set the device not to render to texture */
            device_reapply_stateblock(This);
            device_render_to_texture(This, FALSE);
        }

    /* Offscreen rendering: PBuffers (currently disabled).
     * Also note that this path is never reached if FBOs are supported */
    } else if (wined3d_settings.offscreen_rendering_mode == ORM_PBUFFER &&
               (cfgs = device_find_fbconfigs(This, implicitSwapchainImpl, RenderSurface)) != NULL) {

        /** ********************************************************************
        * This is a quickly hacked out implementation of offscreen textures.
        * It will work in most cases but there may be problems if the client
        * modifies the texture directly, or expects the contents of the rendertarget
        * to be persistent.
        *
        * There are some real speed vs compatibility issues here:
        *    we should really use a new context for every texture, but that eats ram.
        *    we should also be restoring the texture to the pbuffer but that eats CPU
        *    we can also 'reuse' the current pbuffer if the size is larger than the requested buffer,
        *    but if this means reusing the display backbuffer then we need to make sure that
        *    states are correctly preserved.
        * In many cases I would expect that we can 'skip' some functions, such as preserving states,
        * and gain a good performance increase at the cost of compatibility.
        * I would suggest that, when this is the case, a user configurable flag be made
        * available, allowing the user to choose the best emulated experience for them.
         *********************************************************************/

        XVisualInfo *visinfo;
        glContext   *newContext;

        /* Here were using a shared context model */
        if (WINED3D_OK != IWineD3DDeviceImpl_FindGLContext(iface, RenderSurface, &newContext)) {
            FIXME("(%p) : Failed to find a context for surface %p\n", iface, RenderSurface);
        }

        /* If the context doesn't exist then create a new one */
        /* TODO: This should really be part of findGlContext */
        if (NULL == newContext->context) {

            int attribs[256];
            int nAttribs = 0;

            TRACE("making new buffer\n");
            attribs[nAttribs++] = GLX_PBUFFER_WIDTH; 
            attribs[nAttribs++] = newContext->Width;
            attribs[nAttribs++] = GLX_PBUFFER_HEIGHT;
            attribs[nAttribs++] = newContext->Height;
            attribs[nAttribs++] = None;

            newContext->drawable = glXCreatePbuffer(implicitSwapchainImpl->display, cfgs[0], attribs);

            /** ****************************************
            *GLX1.3 isn't supported by XFree 'yet' until that point ATI emulates pBuffers
            *they note:
            *   In future releases, we may provide the calls glXCreateNewContext,
            *   glXQueryDrawable and glXMakeContextCurrent.
            *    so until then we have to use glXGetVisualFromFBConfig &co..
            ********************************************/

            visinfo = glXGetVisualFromFBConfig(implicitSwapchainImpl->display, cfgs[0]);
            if (!visinfo) {
                ERR("Error: couldn't get an RGBA, double-buffered visual\n");
            } else {
                newContext->context = glXCreateContext(
                    implicitSwapchainImpl->display, visinfo,
                    implicitSwapchainImpl->glCtx, GL_TRUE);

                XFree(visinfo);
            }
        }
        if (NULL == newContext || NULL == newContext->context) {
            ERR("(%p) : Failed to find a context for surface %p\n", iface, RenderSurface);
        } else {
            /* Debug logging, (this function leaks), change to a TRACE when the leak is plugged */
            if (glXMakeCurrent(implicitSwapchainImpl->display,
                newContext->drawable, newContext->context) == False) {

                TRACE("Error in setting current context: context %p drawable %ld\n",
                    newContext->context, newContext->drawable);
            }
            checkGLcall("glXMakeContextCurrent");

            /* Clean up the old context */
            IWineD3DDeviceImpl_CleanRender(iface, currentSwapchainImpl);

            /* Reapply stateblock, and set device to render to a texture */
            device_reapply_stateblock(This);
            device_render_to_texture(This, TRUE);

            /* Set the current context of the swapchain to the new context */
            implicitSwapchainImpl->drawable   = newContext->drawable;
            implicitSwapchainImpl->render_ctx = newContext->context;
        }
    } else {
        /* Same context, but update render_offscreen and cull mode */
        device_render_to_texture(This, TRUE);
    }

    if (cfgs != NULL)                   XFree(cfgs);
    if (implicitSwapchain != NULL)       IWineD3DSwapChain_Release(implicitSwapchain);
    if (currentSwapchain != NULL)       IWineD3DSwapChain_Release(currentSwapchain);
    if (renderSurfaceSwapchain != NULL) IWineD3DSwapChain_Release(renderSurfaceSwapchain);
    LEAVE_GL();
#endif
    return WINED3D_OK;
}

static HRESULT  WINAPI  IWineD3DDeviceImpl_SetCursorProperties(IWineD3DDevice* iface, UINT XHotSpot,
                                                        UINT YHotSpot, IWineD3DSurface *pCursorBitmap) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;
    /* TODO: the use of Impl is deprecated. */
    IWineD3DSurfaceImpl * pSur = (IWineD3DSurfaceImpl *) pCursorBitmap;

    TRACE("(%p) : Spot Pos(%u,%u)\n", This, XHotSpot, YHotSpot);

    /* some basic validation checks */
    if(This->cursorTexture) {
        ENTER_GL();
        glDeleteTextures(1, &This->cursorTexture);
        LEAVE_GL();
        This->cursorTexture = 0;
    }

    if(pCursorBitmap) {
        /* MSDN: Cursor must be A8R8G8B8 */
        if (WINED3DFMT_A8R8G8B8 != pSur->resource.format) {
            ERR("(%p) : surface(%p) has an invalid format\n", This, pCursorBitmap);
            return WINED3DERR_INVALIDCALL;
        }

        /* MSDN: Cursor must be smaller than the display mode */
        if(pSur->currentDesc.Width > This->ddraw_width ||
           pSur->currentDesc.Height > This->ddraw_height) {
            ERR("(%p) : Surface(%p) is %dx%d pixels, but screen res is %dx%d\n", This, pSur, pSur->currentDesc.Width, pSur->currentDesc.Height, This->ddraw_width, This->ddraw_height);
            return WINED3DERR_INVALIDCALL;
        }

        /* TODO: MSDN: Cursor sizes must be a power of 2 */
        /* This is to tell our texture code to load a SCRATCH surface. This allows us to use out
         * Texture and Blitting code to draw the cursor
         */
        pSur->Flags |= SFLAG_FORCELOAD;
        IWineD3DSurface_PreLoad(pCursorBitmap);
        pSur->Flags &= ~SFLAG_FORCELOAD;
        /* Do not store the surface's pointer because the application may release
         * it after setting the cursor image. Windows doesn't addref the set surface, so we can't
         * do this either without creating circular refcount dependencies. Copy out the gl texture instead.
         */
        This->cursorTexture = pSur->glDescription.textureName;
        This->cursorWidth = pSur->currentDesc.Width;
        This->cursorHeight = pSur->currentDesc.Height;
        pSur->glDescription.textureName = 0; /* Prevent the texture from being changed or deleted */
    }

    This->xHotSpot = XHotSpot;
    This->yHotSpot = YHotSpot;
    return WINED3D_OK;
}

static void     WINAPI  IWineD3DDeviceImpl_SetCursorPosition(IWineD3DDevice* iface, int XScreenSpace, int YScreenSpace, DWORD Flags) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;
    TRACE("(%p) : SetPos to (%u,%u)\n", This, XScreenSpace, YScreenSpace);

    This->xScreenSpace = XScreenSpace;
    This->yScreenSpace = YScreenSpace;

    return;

}

static BOOL     WINAPI  IWineD3DDeviceImpl_ShowCursor(IWineD3DDevice* iface, BOOL bShow) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;
    BOOL oldVisible = This->bCursorVisible;
    TRACE("(%p) : visible(%d)\n", This, bShow);

    if(This->cursorTexture)
        This->bCursorVisible = bShow;

    return oldVisible;
}

static HRESULT  WINAPI  IWineD3DDeviceImpl_TestCooperativeLevel(IWineD3DDevice* iface) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;
    TRACE("(%p) : state (%u)\n", This, This->state);
    /* TODO: Implement wrapping of the WndProc so that mimimize and maxamise can be monitored and the states adjusted. */
    switch (This->state) {
    case WINED3D_OK:
        return WINED3D_OK;
    case WINED3DERR_DEVICELOST:
        {
            ResourceList *resourceList  = This->resources;
            while (NULL != resourceList) {
                if (((IWineD3DResourceImpl *)resourceList->resource)->resource.pool == WINED3DPOOL_DEFAULT /* TODO: IWineD3DResource_GetPool(resourceList->resource)*/)
                return WINED3DERR_DEVICENOTRESET;
                resourceList = resourceList->next;
            }
            return WINED3DERR_DEVICELOST;
        }
    case WINED3DERR_DRIVERINTERNALERROR:
        return WINED3DERR_DRIVERINTERNALERROR;
    }

    /* Unknown state */
    return WINED3DERR_DRIVERINTERNALERROR;
}


static HRESULT  WINAPI  IWineD3DDeviceImpl_EvictManagedResources(IWineD3DDevice* iface) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;
    /** FIXME: Resource tracking needs to be done,
    * The closes we can do to this is set the priorities of all managed textures low
    * and then reset them.
     ***********************************************************/
    FIXME("(%p) : stub\n", This);
    return WINED3D_OK;
}

void updateSurfaceDesc(IWineD3DSurfaceImpl *surface, WINED3DPRESENT_PARAMETERS* pPresentationParameters) {
    /* Reallocate proper memory for the front and back buffer and adjust their sizes */
    if(surface->Flags & SFLAG_DIBSECTION) {
        /* Release the DC */
        SelectObject(surface->hDC, surface->dib.holdbitmap);
        DeleteDC(surface->hDC);
        /* Release the DIB section */
        DeleteObject(surface->dib.DIBsection);
        surface->dib.bitmap_data = NULL;
        surface->resource.allocatedMemory = NULL;
        surface->Flags &= ~SFLAG_DIBSECTION;
    }
    surface->currentDesc.Width = *pPresentationParameters->BackBufferWidth;
    surface->currentDesc.Height = *pPresentationParameters->BackBufferHeight;
    if (wined3d_settings.nonpower2_mode == NP2_NATIVE) {
        surface->pow2Width = *pPresentationParameters->BackBufferWidth;
        surface->pow2Height = *pPresentationParameters->BackBufferHeight;
    } else {
        surface->pow2Width = surface->pow2Height = 1;
        while (surface->pow2Width < *pPresentationParameters->BackBufferWidth) surface->pow2Width <<= 1;
        while (surface->pow2Height < *pPresentationParameters->BackBufferHeight) surface->pow2Height <<= 1;
    }
    if(surface->glDescription.textureName) {
        ENTER_GL();
        glDeleteTextures(1, &surface->glDescription.textureName);
        LEAVE_GL();
        surface->glDescription.textureName = 0;
    }
    if(surface->pow2Width != *pPresentationParameters->BackBufferWidth ||
       surface->pow2Height != *pPresentationParameters->BackBufferHeight) {
        surface->Flags |= SFLAG_NONPOW2;
    } else  {
        surface->Flags &= ~SFLAG_NONPOW2;
    }
    HeapFree(GetProcessHeap(), 0, surface->resource.allocatedMemory);
    surface->resource.size = IWineD3DSurface_GetPitch((IWineD3DSurface *) surface) * surface->pow2Width;
}

static HRESULT WINAPI IWineD3DDeviceImpl_Reset(IWineD3DDevice* iface, WINED3DPRESENT_PARAMETERS* pPresentationParameters) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;
    IWineD3DSwapChainImpl *swapchain;
    HRESULT hr;
    BOOL DisplayModeChanged = FALSE;
    WINED3DDISPLAYMODE mode;
    TRACE("(%p)\n", This);

    hr = IWineD3DDevice_GetSwapChain(iface, 0, (IWineD3DSwapChain **) &swapchain);
    if(FAILED(hr)) {
        ERR("Failed to get the first implicit swapchain\n");
        return hr;
    }

    /* Is it necessary to recreate the gl context? Actually every setting can be changed
     * on an existing gl context, so there's no real need for recreation.
     *
     * TODO: Figure out how Reset influences resources in D3DPOOL_DEFAULT, D3DPOOL_SYSTEMMEMORY and D3DPOOL_MANAGED
     *
     * TODO: Figure out what happens to explicit swapchains, or if we have more than one implicit swapchain
     */
    TRACE("New params:\n");
    TRACE("BackBufferWidth = %d\n", *pPresentationParameters->BackBufferWidth);
    TRACE("BackBufferHeight = %d\n", *pPresentationParameters->BackBufferHeight);
    TRACE("BackBufferFormat = %s\n", debug_d3dformat(*pPresentationParameters->BackBufferFormat));
    TRACE("BackBufferCount = %d\n", *pPresentationParameters->BackBufferCount);
    TRACE("MultiSampleType = %d\n", *pPresentationParameters->MultiSampleType);
    TRACE("MultiSampleQuality = %d\n", *pPresentationParameters->MultiSampleQuality);
    TRACE("SwapEffect = %d\n", *pPresentationParameters->SwapEffect);
    TRACE("hDeviceWindow = %p\n", *pPresentationParameters->hDeviceWindow);
    TRACE("Windowed = %s\n", *pPresentationParameters->Windowed ? "true" : "false");
    TRACE("EnableAutoDepthStencil = %s\n", *pPresentationParameters->EnableAutoDepthStencil ? "true" : "false");
    TRACE("Flags = %08x\n", *pPresentationParameters->Flags);
    TRACE("FullScreen_RefreshRateInHz = %d\n", *pPresentationParameters->FullScreen_RefreshRateInHz);
    TRACE("PresentationInterval = %d\n", *pPresentationParameters->PresentationInterval);

    /* No special treatment of these parameters. Just store them */
    swapchain->presentParms.SwapEffect = *pPresentationParameters->SwapEffect;
    swapchain->presentParms.Flags = *pPresentationParameters->Flags;
    swapchain->presentParms.PresentationInterval = *pPresentationParameters->PresentationInterval;
    swapchain->presentParms.FullScreen_RefreshRateInHz = *pPresentationParameters->FullScreen_RefreshRateInHz;

    /* What to do about these? */
    if(*pPresentationParameters->BackBufferCount != 0 &&
        *pPresentationParameters->BackBufferCount != swapchain->presentParms.BackBufferCount) {
        ERR("Cannot change the back buffer count yet\n");
    }
    if(*pPresentationParameters->BackBufferFormat != WINED3DFMT_UNKNOWN &&
        *pPresentationParameters->BackBufferFormat != swapchain->presentParms.BackBufferFormat) {
        ERR("Cannot change the back buffer format yet\n");
    }
    if(*pPresentationParameters->hDeviceWindow != NULL &&
        *pPresentationParameters->hDeviceWindow != swapchain->presentParms.hDeviceWindow) {
        ERR("Cannot change the device window yet\n");
    }
    if(*pPresentationParameters->EnableAutoDepthStencil != swapchain->presentParms.EnableAutoDepthStencil) {
        ERR("What do do about a changed auto depth stencil parameter?\n");
    }

    if(*pPresentationParameters->Windowed) {
        mode.Width = swapchain->orig_width;
        mode.Height = swapchain->orig_height;
        mode.RefreshRate = 0;
        mode.Format = swapchain->presentParms.BackBufferFormat;
    } else {
        mode.Width = *pPresentationParameters->BackBufferWidth;
        mode.Height = *pPresentationParameters->BackBufferHeight;
        mode.RefreshRate = *pPresentationParameters->FullScreen_RefreshRateInHz;
        mode.Format = swapchain->presentParms.BackBufferFormat;
    }

    /* Should Width == 800 && Height == 0 set 800x600? */
    if(*pPresentationParameters->BackBufferWidth != 0 && *pPresentationParameters->BackBufferHeight != 0 &&
       (*pPresentationParameters->BackBufferWidth != swapchain->presentParms.BackBufferWidth ||
        *pPresentationParameters->BackBufferHeight != swapchain->presentParms.BackBufferHeight))
    {
        WINED3DVIEWPORT vp;
        int i;

        vp.X = 0;
        vp.Y = 0;
        vp.Width = *pPresentationParameters->BackBufferWidth;
        vp.Height = *pPresentationParameters->BackBufferHeight;
        vp.MinZ = 0;
        vp.MaxZ = 1;

        if(!*pPresentationParameters->Windowed) {
            DisplayModeChanged = TRUE;
        }
        swapchain->presentParms.BackBufferWidth = *pPresentationParameters->BackBufferWidth;
        swapchain->presentParms.BackBufferHeight = *pPresentationParameters->BackBufferHeight;

        updateSurfaceDesc((IWineD3DSurfaceImpl *)swapchain->frontBuffer, pPresentationParameters);
        for(i = 0; i < swapchain->presentParms.BackBufferCount; i++) {
            updateSurfaceDesc((IWineD3DSurfaceImpl *)swapchain->backBuffer[i], pPresentationParameters);
        }

        /* Now set the new viewport */
        IWineD3DDevice_SetViewport(iface, &vp);
    }

    if((*pPresentationParameters->Windowed && !swapchain->presentParms.Windowed) ||
       (swapchain->presentParms.Windowed && !*pPresentationParameters->Windowed) ||
        DisplayModeChanged) {
        IWineD3DDevice_SetDisplayMode(iface, 0, &mode);
    }

    IWineD3DSwapChain_Release((IWineD3DSwapChain *) swapchain);
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetDialogBoxMode(IWineD3DDevice *iface, BOOL bEnableDialogs) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    /** FIXME: always true at the moment **/
    if(!bEnableDialogs) {
        FIXME("(%p) Dialogs cannot be disabled yet\n", This);
    }
    return WINED3D_OK;
}


static HRESULT  WINAPI  IWineD3DDeviceImpl_GetCreationParameters(IWineD3DDevice *iface, WINED3DDEVICE_CREATION_PARAMETERS *pParameters) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;
    TRACE("(%p) : pParameters %p\n", This, pParameters);

    *pParameters = This->createParms;
    return WINED3D_OK;
}

static void WINAPI IWineD3DDeviceImpl_SetGammaRamp(IWineD3DDevice * iface, UINT iSwapChain, DWORD Flags, CONST WINED3DGAMMARAMP* pRamp) {
    IWineD3DSwapChain *swapchain;
    HRESULT hrc = WINED3D_OK;

    TRACE("Relaying  to swapchain\n");

    if ((hrc = IWineD3DDeviceImpl_GetSwapChain(iface, iSwapChain, &swapchain)) == WINED3D_OK) {
        IWineD3DSwapChain_SetGammaRamp(swapchain, Flags, (WINED3DGAMMARAMP *)pRamp);
        IWineD3DSwapChain_Release(swapchain);
    }
    return;
}

static void WINAPI IWineD3DDeviceImpl_GetGammaRamp(IWineD3DDevice *iface, UINT iSwapChain, WINED3DGAMMARAMP* pRamp) {
    IWineD3DSwapChain *swapchain;
    HRESULT hrc = WINED3D_OK;

    TRACE("Relaying  to swapchain\n");

    if ((hrc = IWineD3DDeviceImpl_GetSwapChain(iface, iSwapChain, &swapchain)) == WINED3D_OK) {
        hrc =IWineD3DSwapChain_GetGammaRamp(swapchain, pRamp);
        IWineD3DSwapChain_Release(swapchain);
    }
    return;
}


/** ********************************************************
*   Notification functions
** ********************************************************/
/** This function must be called in the release of a resource when ref == 0,
* the contents of resource must still be correct,
* any handels to other resource held by the caller must be closed
* (e.g. a texture should release all held surfaces because telling the device that it's been released.)
 *****************************************************/
static void WINAPI IWineD3DDeviceImpl_AddResource(IWineD3DDevice *iface, IWineD3DResource *resource){
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    ResourceList* resourceList;

    TRACE("(%p) : resource %p\n", This, resource);
#if 0
    EnterCriticalSection(&resourceStoreCriticalSection);
#endif
    /* add a new texture to the frot of the linked list */
    resourceList = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ResourceList));
    resourceList->resource = resource;

    /* Get the old head */
    resourceList->next = This->resources;

    This->resources = resourceList;
    TRACE("Added resource %p with element %p pointing to %p\n", resource, resourceList, resourceList->next);

#if 0
    LeaveCriticalSection(&resourceStoreCriticalSection);
#endif
    return;
}

static void WINAPI IWineD3DDeviceImpl_RemoveResource(IWineD3DDevice *iface, IWineD3DResource *resource){
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    ResourceList* resourceList = NULL;
    ResourceList* previousResourceList = NULL;
    
    TRACE("(%p) : resource %p\n", This, resource);

#if 0
    EnterCriticalSection(&resourceStoreCriticalSection);
#endif
    resourceList = This->resources;

    while (resourceList != NULL) {
        if(resourceList->resource == resource) break;
        previousResourceList = resourceList;
        resourceList = resourceList->next;
    }

    if (resourceList == NULL) {
        FIXME("Attempted to remove resource %p that hasn't been stored\n", resource);
#if 0
        LeaveCriticalSection(&resourceStoreCriticalSection);
#endif
        return;
    } else {
            TRACE("Found resource  %p with element %p pointing to %p (previous %p)\n", resourceList->resource, resourceList, resourceList->next, previousResourceList);
    }
    /* make sure we don't leave a hole in the list */
    if (previousResourceList != NULL) {
        previousResourceList->next = resourceList->next;
    } else {
        This->resources = resourceList->next;
    }

#if 0
    LeaveCriticalSection(&resourceStoreCriticalSection);
#endif
    return;
}


static void WINAPI IWineD3DDeviceImpl_ResourceReleased(IWineD3DDevice *iface, IWineD3DResource *resource){
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;
    int counter;

    TRACE("(%p) : resource %p\n", This, resource);
    switch(IWineD3DResource_GetType(resource)){
        case WINED3DRTYPE_SURFACE:
        /* TODO: check front and back buffers, rendertargets etc..  possibly swapchains? */
        break;
        case WINED3DRTYPE_TEXTURE:
        case WINED3DRTYPE_CUBETEXTURE:
        case WINED3DRTYPE_VOLUMETEXTURE:
                for (counter = 0; counter < GL_LIMITS(sampler_stages); counter++) {
                    if (This->stateBlock != NULL && This->stateBlock->textures[counter] == (IWineD3DBaseTexture *)resource) {
                        WARN("Texture being released is still by a stateblock, Stage = %u Texture = %p\n", counter, resource);
                        This->stateBlock->textures[counter] = NULL;
                    }
                    if (This->updateStateBlock != This->stateBlock ){
                        if (This->updateStateBlock->textures[counter] == (IWineD3DBaseTexture *)resource) {
                            WARN("Texture being released is still by a stateblock, Stage = %u Texture = %p\n", counter, resource);
                            This->updateStateBlock->textures[counter] = NULL;
                        }
                    }
                }
        break;
        case WINED3DRTYPE_VOLUME:
        /* TODO: nothing really? */
        break;
        case WINED3DRTYPE_VERTEXBUFFER:
        /* MSDN: When an application no longer holds a references to this interface, the interface will automatically be freed. */
        {
            int streamNumber;
            TRACE("Cleaning up stream pointers\n");

            for(streamNumber = 0; streamNumber < MAX_STREAMS; streamNumber ++){
                /* FINDOUT: should a warn be generated if were recording and updateStateBlock->streamSource is lost?
                FINDOUT: should changes.streamSource[StreamNumber] be set ?
                */
                if (This->updateStateBlock != NULL ) { /* ==NULL when device is being destroyed */
                    if ((IWineD3DResource *)This->updateStateBlock->streamSource[streamNumber] == resource) {
                        FIXME("Vertex buffer released while bound to a state block, stream %d\n", streamNumber);
                        This->updateStateBlock->streamSource[streamNumber] = 0;
                        /* Set changed flag? */
                    }
                }
                if (This->stateBlock != NULL ) { /* only happens if there is an error in the application, or on reset/release (because we don't manage internal tracking properly) */
                    if ((IWineD3DResource *)This->stateBlock->streamSource[streamNumber] == resource) {
                        TRACE("Vertex buffer released while bound to a state block, stream %d\n", streamNumber);
                        This->stateBlock->streamSource[streamNumber] = 0;
                    }
                }
#if 0   /* TODO: Manage internal tracking properly so that 'this shouldn't happen' */
                 else { /* This shouldn't happen */
                    FIXME("Calling application has released the device before relasing all the resources bound to the device\n");
                }
#endif

            }
        }
        break;
        case WINED3DRTYPE_INDEXBUFFER:
        /* MSDN: When an application no longer holds a references to this interface, the interface will automatically be freed.*/
        if (This->updateStateBlock != NULL ) { /* ==NULL when device is being destroyed */
            if (This->updateStateBlock->pIndexData == (IWineD3DIndexBuffer *)resource) {
                This->updateStateBlock->pIndexData =  NULL;
            }
        }
        if (This->stateBlock != NULL ) { /* ==NULL when device is being destroyed */
            if (This->stateBlock->pIndexData == (IWineD3DIndexBuffer *)resource) {
                This->stateBlock->pIndexData =  NULL;
            }
        }

        break;
        default:
        FIXME("(%p) unknown resource type %p %u\n", This, resource, IWineD3DResource_GetType(resource));
        break;
    }


    /* Remove the resoruce from the resourceStore */
    IWineD3DDeviceImpl_RemoveResource(iface, resource);

    TRACE("Resource released\n");

}

/**********************************************************
 * IWineD3DDevice VTbl follows
 **********************************************************/

const IWineD3DDeviceVtbl IWineD3DDevice_Vtbl =
{
    /*** IUnknown methods ***/
    IWineD3DDeviceImpl_QueryInterface,
    IWineD3DDeviceImpl_AddRef,
    IWineD3DDeviceImpl_Release,
    /*** IWineD3DDevice methods ***/
    IWineD3DDeviceImpl_GetParent,
    /*** Creation methods**/
    IWineD3DDeviceImpl_CreateVertexBuffer,
    IWineD3DDeviceImpl_CreateIndexBuffer,
    IWineD3DDeviceImpl_CreateStateBlock,
    IWineD3DDeviceImpl_CreateSurface,
    IWineD3DDeviceImpl_CreateTexture,
    IWineD3DDeviceImpl_CreateVolumeTexture,
    IWineD3DDeviceImpl_CreateVolume,
    IWineD3DDeviceImpl_CreateCubeTexture,
    IWineD3DDeviceImpl_CreateQuery,
    IWineD3DDeviceImpl_CreateAdditionalSwapChain,
    IWineD3DDeviceImpl_CreateVertexDeclaration,
    IWineD3DDeviceImpl_CreateVertexShader,
    IWineD3DDeviceImpl_CreatePixelShader,
    IWineD3DDeviceImpl_CreatePalette,
    /*** Odd functions **/
    IWineD3DDeviceImpl_Init3D,
    IWineD3DDeviceImpl_Uninit3D,
    IWineD3DDeviceImpl_SetFullscreen,
    IWineD3DDeviceImpl_EnumDisplayModes,
    IWineD3DDeviceImpl_EvictManagedResources,
    IWineD3DDeviceImpl_GetAvailableTextureMem,
    IWineD3DDeviceImpl_GetBackBuffer,
    IWineD3DDeviceImpl_GetCreationParameters,
    IWineD3DDeviceImpl_GetDeviceCaps,
    IWineD3DDeviceImpl_GetDirect3D,
    IWineD3DDeviceImpl_GetDisplayMode,
    IWineD3DDeviceImpl_SetDisplayMode,
    IWineD3DDeviceImpl_GetHWND,
    IWineD3DDeviceImpl_SetHWND,
    IWineD3DDeviceImpl_GetNumberOfSwapChains,
    IWineD3DDeviceImpl_GetRasterStatus,
    IWineD3DDeviceImpl_GetSwapChain,
    IWineD3DDeviceImpl_Reset,
    IWineD3DDeviceImpl_SetDialogBoxMode,
    IWineD3DDeviceImpl_SetCursorProperties,
    IWineD3DDeviceImpl_SetCursorPosition,
    IWineD3DDeviceImpl_ShowCursor,
    IWineD3DDeviceImpl_TestCooperativeLevel,
    /*** Getters and setters **/
    IWineD3DDeviceImpl_SetClipPlane,
    IWineD3DDeviceImpl_GetClipPlane,
    IWineD3DDeviceImpl_SetClipStatus,
    IWineD3DDeviceImpl_GetClipStatus,
    IWineD3DDeviceImpl_SetCurrentTexturePalette,
    IWineD3DDeviceImpl_GetCurrentTexturePalette,
    IWineD3DDeviceImpl_SetDepthStencilSurface,
    IWineD3DDeviceImpl_GetDepthStencilSurface,
    IWineD3DDeviceImpl_SetFVF,
    IWineD3DDeviceImpl_GetFVF,
    IWineD3DDeviceImpl_SetGammaRamp,
    IWineD3DDeviceImpl_GetGammaRamp,
    IWineD3DDeviceImpl_SetIndices,
    IWineD3DDeviceImpl_GetIndices,
    IWineD3DDeviceImpl_SetBasevertexIndex,
    IWineD3DDeviceImpl_SetLight,
    IWineD3DDeviceImpl_GetLight,
    IWineD3DDeviceImpl_SetLightEnable,
    IWineD3DDeviceImpl_GetLightEnable,
    IWineD3DDeviceImpl_SetMaterial,
    IWineD3DDeviceImpl_GetMaterial,
    IWineD3DDeviceImpl_SetNPatchMode,
    IWineD3DDeviceImpl_GetNPatchMode,
    IWineD3DDeviceImpl_SetPaletteEntries,
    IWineD3DDeviceImpl_GetPaletteEntries,
    IWineD3DDeviceImpl_SetPixelShader,
    IWineD3DDeviceImpl_GetPixelShader,
    IWineD3DDeviceImpl_SetPixelShaderConstantB,
    IWineD3DDeviceImpl_GetPixelShaderConstantB,
    IWineD3DDeviceImpl_SetPixelShaderConstantI,
    IWineD3DDeviceImpl_GetPixelShaderConstantI,
    IWineD3DDeviceImpl_SetPixelShaderConstantF,
    IWineD3DDeviceImpl_GetPixelShaderConstantF,
    IWineD3DDeviceImpl_SetRenderState,
    IWineD3DDeviceImpl_GetRenderState,
    IWineD3DDeviceImpl_SetRenderTarget,
    IWineD3DDeviceImpl_GetRenderTarget,
    IWineD3DDeviceImpl_SetFrontBackBuffers,
    IWineD3DDeviceImpl_SetSamplerState,
    IWineD3DDeviceImpl_GetSamplerState,
    IWineD3DDeviceImpl_SetScissorRect,
    IWineD3DDeviceImpl_GetScissorRect,
    IWineD3DDeviceImpl_SetSoftwareVertexProcessing,
    IWineD3DDeviceImpl_GetSoftwareVertexProcessing,
    IWineD3DDeviceImpl_SetStreamSource,
    IWineD3DDeviceImpl_GetStreamSource,
    IWineD3DDeviceImpl_SetStreamSourceFreq,
    IWineD3DDeviceImpl_GetStreamSourceFreq,
    IWineD3DDeviceImpl_SetTexture,
    IWineD3DDeviceImpl_GetTexture,
    IWineD3DDeviceImpl_SetTextureStageState,
    IWineD3DDeviceImpl_GetTextureStageState,
    IWineD3DDeviceImpl_SetTransform,
    IWineD3DDeviceImpl_GetTransform,
    IWineD3DDeviceImpl_SetVertexDeclaration,
    IWineD3DDeviceImpl_GetVertexDeclaration,
    IWineD3DDeviceImpl_SetVertexShader,
    IWineD3DDeviceImpl_GetVertexShader,
    IWineD3DDeviceImpl_SetVertexShaderConstantB,
    IWineD3DDeviceImpl_GetVertexShaderConstantB,
    IWineD3DDeviceImpl_SetVertexShaderConstantI,
    IWineD3DDeviceImpl_GetVertexShaderConstantI,
    IWineD3DDeviceImpl_SetVertexShaderConstantF,
    IWineD3DDeviceImpl_GetVertexShaderConstantF,
    IWineD3DDeviceImpl_SetViewport,
    IWineD3DDeviceImpl_GetViewport,
    IWineD3DDeviceImpl_MultiplyTransform,
    IWineD3DDeviceImpl_ValidateDevice,
    IWineD3DDeviceImpl_ProcessVertices,
    /*** State block ***/
    IWineD3DDeviceImpl_BeginStateBlock,
    IWineD3DDeviceImpl_EndStateBlock,
    /*** Scene management ***/
    IWineD3DDeviceImpl_BeginScene,
    IWineD3DDeviceImpl_EndScene,
    IWineD3DDeviceImpl_Present,
    IWineD3DDeviceImpl_Clear,
    /*** Drawing ***/
    IWineD3DDeviceImpl_DrawPrimitive,
    IWineD3DDeviceImpl_DrawIndexedPrimitive,
    IWineD3DDeviceImpl_DrawPrimitiveUP,
    IWineD3DDeviceImpl_DrawIndexedPrimitiveUP,
    IWineD3DDeviceImpl_DrawPrimitiveStrided,
    IWineD3DDeviceImpl_DrawRectPatch,
    IWineD3DDeviceImpl_DrawTriPatch,
    IWineD3DDeviceImpl_DeletePatch,
    IWineD3DDeviceImpl_ColorFill,
    IWineD3DDeviceImpl_UpdateTexture,
    IWineD3DDeviceImpl_UpdateSurface,
    IWineD3DDeviceImpl_GetRenderTargetData,
    IWineD3DDeviceImpl_GetFrontBufferData,
    IWineD3DDeviceImpl_SetupFullscreenWindow,
    IWineD3DDeviceImpl_RestoreWindow,
    /*** object tracking ***/
    IWineD3DDeviceImpl_ResourceReleased
};


const DWORD SavedPixelStates_R[NUM_SAVEDPIXELSTATES_R] = {
    WINED3DRS_ALPHABLENDENABLE   ,
    WINED3DRS_ALPHAFUNC          ,
    WINED3DRS_ALPHAREF           ,
    WINED3DRS_ALPHATESTENABLE    ,
    WINED3DRS_BLENDOP            ,
    WINED3DRS_COLORWRITEENABLE   ,
    WINED3DRS_DESTBLEND          ,
    WINED3DRS_DITHERENABLE       ,
    WINED3DRS_FILLMODE           ,
    WINED3DRS_FOGDENSITY         ,
    WINED3DRS_FOGEND             ,
    WINED3DRS_FOGSTART           ,
    WINED3DRS_LASTPIXEL          ,
    WINED3DRS_SHADEMODE          ,
    WINED3DRS_SRCBLEND           ,
    WINED3DRS_STENCILENABLE      ,
    WINED3DRS_STENCILFAIL        ,
    WINED3DRS_STENCILFUNC        ,
    WINED3DRS_STENCILMASK        ,
    WINED3DRS_STENCILPASS        ,
    WINED3DRS_STENCILREF         ,
    WINED3DRS_STENCILWRITEMASK   ,
    WINED3DRS_STENCILZFAIL       ,
    WINED3DRS_TEXTUREFACTOR      ,
    WINED3DRS_WRAP0              ,
    WINED3DRS_WRAP1              ,
    WINED3DRS_WRAP2              ,
    WINED3DRS_WRAP3              ,
    WINED3DRS_WRAP4              ,
    WINED3DRS_WRAP5              ,
    WINED3DRS_WRAP6              ,
    WINED3DRS_WRAP7              ,
    WINED3DRS_ZENABLE            ,
    WINED3DRS_ZFUNC              ,
    WINED3DRS_ZWRITEENABLE
};

const DWORD SavedPixelStates_T[NUM_SAVEDPIXELSTATES_T] = {
    WINED3DTSS_ADDRESSW              ,
    WINED3DTSS_ALPHAARG0             ,
    WINED3DTSS_ALPHAARG1             ,
    WINED3DTSS_ALPHAARG2             ,
    WINED3DTSS_ALPHAOP               ,
    WINED3DTSS_BUMPENVLOFFSET        ,
    WINED3DTSS_BUMPENVLSCALE         ,
    WINED3DTSS_BUMPENVMAT00          ,
    WINED3DTSS_BUMPENVMAT01          ,
    WINED3DTSS_BUMPENVMAT10          ,
    WINED3DTSS_BUMPENVMAT11          ,
    WINED3DTSS_COLORARG0             ,
    WINED3DTSS_COLORARG1             ,
    WINED3DTSS_COLORARG2             ,
    WINED3DTSS_COLOROP               ,
    WINED3DTSS_RESULTARG             ,
    WINED3DTSS_TEXCOORDINDEX         ,
    WINED3DTSS_TEXTURETRANSFORMFLAGS
};

const DWORD SavedPixelStates_S[NUM_SAVEDPIXELSTATES_S] = {
    WINED3DSAMP_ADDRESSU         ,
    WINED3DSAMP_ADDRESSV         ,
    WINED3DSAMP_ADDRESSW         ,
    WINED3DSAMP_BORDERCOLOR      ,
    WINED3DSAMP_MAGFILTER        ,
    WINED3DSAMP_MINFILTER        ,
    WINED3DSAMP_MIPFILTER        ,
    WINED3DSAMP_MIPMAPLODBIAS    ,
    WINED3DSAMP_MAXMIPLEVEL      ,
    WINED3DSAMP_MAXANISOTROPY    ,
    WINED3DSAMP_SRGBTEXTURE      ,
    WINED3DSAMP_ELEMENTINDEX
};

const DWORD SavedVertexStates_R[NUM_SAVEDVERTEXSTATES_R] = {
    WINED3DRS_AMBIENT                       ,
    WINED3DRS_AMBIENTMATERIALSOURCE         ,
    WINED3DRS_CLIPPING                      ,
    WINED3DRS_CLIPPLANEENABLE               ,
    WINED3DRS_COLORVERTEX                   ,
    WINED3DRS_DIFFUSEMATERIALSOURCE         ,
    WINED3DRS_EMISSIVEMATERIALSOURCE        ,
    WINED3DRS_FOGDENSITY                    ,
    WINED3DRS_FOGEND                        ,
    WINED3DRS_FOGSTART                      ,
    WINED3DRS_FOGTABLEMODE                  ,
    WINED3DRS_FOGVERTEXMODE                 ,
    WINED3DRS_INDEXEDVERTEXBLENDENABLE      ,
    WINED3DRS_LIGHTING                      ,
    WINED3DRS_LOCALVIEWER                   ,
    WINED3DRS_MULTISAMPLEANTIALIAS          ,
    WINED3DRS_MULTISAMPLEMASK               ,
    WINED3DRS_NORMALIZENORMALS              ,
    WINED3DRS_PATCHEDGESTYLE                ,
    WINED3DRS_POINTSCALE_A                  ,
    WINED3DRS_POINTSCALE_B                  ,
    WINED3DRS_POINTSCALE_C                  ,
    WINED3DRS_POINTSCALEENABLE              ,
    WINED3DRS_POINTSIZE                     ,
    WINED3DRS_POINTSIZE_MAX                 ,
    WINED3DRS_POINTSIZE_MIN                 ,
    WINED3DRS_POINTSPRITEENABLE             ,
    WINED3DRS_RANGEFOGENABLE                ,
    WINED3DRS_SPECULARMATERIALSOURCE        ,
    WINED3DRS_TWEENFACTOR                   ,
    WINED3DRS_VERTEXBLEND
};

const DWORD SavedVertexStates_T[NUM_SAVEDVERTEXSTATES_T] = {
    WINED3DTSS_TEXCOORDINDEX         ,
    WINED3DTSS_TEXTURETRANSFORMFLAGS
};

const DWORD SavedVertexStates_S[NUM_SAVEDVERTEXSTATES_S] = {
    WINED3DSAMP_DMAPOFFSET
};

void IWineD3DDeviceImpl_MarkStateDirty(IWineD3DDeviceImpl *This, DWORD state) {
    DWORD rep = StateTable[state].representative;
    DWORD idx;
    BYTE shift;

    if(!rep || isStateDirty(This, rep)) return;

    This->dirtyArray[This->numDirtyEntries++] = rep;
    idx = rep >> 5;
    shift = rep & 0x1f;
    This->isStateDirty[idx] |= (1 << shift);
}
