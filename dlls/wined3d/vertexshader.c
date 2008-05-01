/*
 * shaders implementation
 *
 * Copyright 2002-2003 Jason Edmeades
 * Copyright 2002-2003 Raphael Junqueira
 * Copyright 2004 Christian Costa
 * Copyright 2005 Oliver Stieber
 * Copyright 2006 Ivan Gyurdiev
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

#include <math.h>
#include <stdio.h>

#include "wined3d_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d_shader);

#define GLINFO_LOCATION ((IWineD3DImpl *)(((IWineD3DDeviceImpl *)This->baseShader.device)->wineD3D))->gl_info

/* Shader debugging - Change the following line to enable debugging of software
      vertex shaders                                                             */
#if 0 /* Musxt not be 1 in cvs version */
# define VSTRACE(A) TRACE A
# define TRACE_VSVECTOR(name) TRACE( #name "=(%f, %f, %f, %f)\n", name.x, name.y, name.z, name.w)
#else
# define VSTRACE(A)
# define TRACE_VSVECTOR(name)
#endif

/**
 * DirectX9 SDK download
 *  http://msdn.microsoft.com/library/default.asp?url=/downloads/list/directx.asp
 *
 * Exploring D3DX
 *  http://msdn.microsoft.com/library/default.asp?url=/library/en-us/dndrive/html/directx07162002.asp
 *
 * Using Vertex Shaders
 *  http://msdn.microsoft.com/library/default.asp?url=/library/en-us/dndrive/html/directx02192001.asp
 *
 * Dx9 New
 *  http://msdn.microsoft.com/library/default.asp?url=/library/en-us/directx9_c/directx/graphics/whatsnew.asp
 *
 * Dx9 Shaders
 *  http://msdn.microsoft.com/library/default.asp?url=/library/en-us/directx9_c/directx/graphics/reference/Shaders/VertexShader2_0/VertexShader2_0.asp
 *  http://msdn.microsoft.com/library/default.asp?url=/library/en-us/directx9_c/directx/graphics/reference/Shaders/VertexShader2_0/Instructions/Instructions.asp
 *  http://msdn.microsoft.com/library/default.asp?url=/library/en-us/directx9_c/directx/graphics/programmingguide/GettingStarted/VertexDeclaration/VertexDeclaration.asp
 *  http://msdn.microsoft.com/library/default.asp?url=/library/en-us/directx9_c/directx/graphics/reference/Shaders/VertexShader3_0/VertexShader3_0.asp
 *
 * Dx9 D3DX
 *  http://msdn.microsoft.com/library/default.asp?url=/library/en-us/directx9_c/directx/graphics/programmingguide/advancedtopics/VertexPipe/matrixstack/matrixstack.asp
 *
 * FVF
 *  http://msdn.microsoft.com/library/en-us/directx9_c/directx/graphics/programmingguide/GettingStarted/VertexFormats/vformats.asp
 *
 * NVIDIA: DX8 Vertex Shader to NV Vertex Program
 *  http://developer.nvidia.com/view.asp?IO=vstovp
 *
 * NVIDIA: Memory Management with VAR
 *  http://developer.nvidia.com/view.asp?IO=var_memory_management
 */

/* TODO: Vertex and Pixel shaders are almost identicle, the only exception being the way that some of the data is looked up or the availablity of some of the data i.e. some instructions are only valid for pshaders and some for vshaders
because of this the bulk of the software pipeline can be shared between pixel and vertex shaders... and it wouldn't supprise me if the programes can be cross compiled using a large body body shared code */

#define GLNAME_REQUIRE_GLSL  ((const char *)1)

/*******************************
 * vshader functions software VM
 */

static void vshader_add(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0, WINED3DSHADERVECTOR* s1) {
  d->x = s0->x + s1->x;
  d->y = s0->y + s1->y;
  d->z = s0->z + s1->z;
  d->w = s0->w + s1->w;
  VSTRACE(("executing add: s0=(%f, %f, %f, %f) s1=(%f, %f, %f, %f) => d=(%f, %f, %f, %f)\n",
	         s0->x, s0->y, s0->z, s0->w, s1->x, s1->y, s1->z, s1->w, d->x, d->y, d->z, d->w));
}

static void vshader_dp3(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0, WINED3DSHADERVECTOR* s1) {
  d->x = d->y = d->z = d->w = s0->x * s1->x + s0->y * s1->y + s0->z * s1->z;
  VSTRACE(("executing dp3: s0=(%f, %f, %f, %f) s1=(%f, %f, %f, %f) => d=(%f, %f, %f, %f)\n",
	         s0->x, s0->y, s0->z, s0->w, s1->x, s1->y, s1->z, s1->w, d->x, d->y, d->z, d->w));
}

static void vshader_dp4(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0, WINED3DSHADERVECTOR* s1) {
  d->x = d->y = d->z = d->w = s0->x * s1->x + s0->y * s1->y + s0->z * s1->z + s0->w * s1->w;
  VSTRACE(("executing dp4: s0=(%f, %f, %f, %f) s1=(%f, %f, %f, %f) => d=(%f, %f, %f, %f)\n",
	  s0->x, s0->y, s0->z, s0->w, s1->x, s1->y, s1->z, s1->w, d->x, d->y, d->z, d->w));
}

static void vshader_dst(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0, WINED3DSHADERVECTOR* s1) {
  d->x = 1.0f;
  d->y = s0->y * s1->y;
  d->z = s0->z;
  d->w = s1->w;
  VSTRACE(("executing dst: s0=(%f, %f, %f, %f) s1=(%f, %f, %f, %f) => d=(%f, %f, %f, %f)\n",
	  s0->x, s0->y, s0->z, s0->w, s1->x, s1->y, s1->z, s1->w, d->x, d->y, d->z, d->w));
}

static void vshader_expp(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0) {
  union {
    float f;
    DWORD d;
  } tmp;

  tmp.f = floorf(s0->w);
  d->x  = powf(2.0f, tmp.f);
  d->y  = s0->w - tmp.f;
  tmp.f = powf(2.0f, s0->w);
  tmp.d &= 0xFFFFFF00U;
  d->z  = tmp.f;
  d->w  = 1.0f;
  VSTRACE(("executing exp: s0=(%f, %f, %f, %f) => d=(%f, %f, %f, %f)\n",
                s0->x, s0->y, s0->z, s0->w, d->x, d->y, d->z, d->w));
}

static void vshader_lit(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0) {
  d->x = 1.0f;
  d->y = (0.0f < s0->x) ? s0->x : 0.0f;
  d->z = (0.0f < s0->x && 0.0f < s0->y) ? powf(s0->y, s0->w) : 0.0f;
  d->w = 1.0f;
  VSTRACE(("executing lit: s0=(%f, %f, %f, %f) => d=(%f, %f, %f, %f)\n",
	         s0->x, s0->y, s0->z, s0->w, d->x, d->y, d->z, d->w));
}

static void vshader_logp(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0) {
  float tmp_f = fabsf(s0->w);
  d->x = d->y = d->z = d->w = (0.0f != tmp_f) ? logf(tmp_f) / logf(2.0f) : -HUGE_VAL;
  VSTRACE(("executing logp: s0=(%f, %f, %f, %f) => d=(%f, %f, %f, %f)\n",
	         s0->x, s0->y, s0->z, s0->w, d->x, d->y, d->z, d->w));
}

static void vshader_mad(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0, WINED3DSHADERVECTOR* s1, WINED3DSHADERVECTOR* s2) {
  d->x = s0->x * s1->x + s2->x;
  d->y = s0->y * s1->y + s2->y;
  d->z = s0->z * s1->z + s2->z;
  d->w = s0->w * s1->w + s2->w;
  VSTRACE(("executing mad: s0=(%f, %f, %f, %f) s1=(%f, %f, %f, %f) s2=(%f, %f, %f, %f) => d=(%f, %f, %f, %f)\n",
	  s0->x, s0->y, s0->z, s0->w, s1->x, s1->y, s1->z, s1->w, s2->x, s2->y, s2->z, s2->w, d->x, d->y, d->z, d->w));
}

static void vshader_max(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0, WINED3DSHADERVECTOR* s1) {
  d->x = (s0->x >= s1->x) ? s0->x : s1->x;
  d->y = (s0->y >= s1->y) ? s0->y : s1->y;
  d->z = (s0->z >= s1->z) ? s0->z : s1->z;
  d->w = (s0->w >= s1->w) ? s0->w : s1->w;
  VSTRACE(("executing max: s0=(%f, %f, %f, %f) s1=(%f, %f, %f, %f) => d=(%f, %f, %f, %f)\n",
	  s0->x, s0->y, s0->z, s0->w, s1->x, s1->y, s1->z, s1->w, d->x, d->y, d->z, d->w));
}

static void vshader_min(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0, WINED3DSHADERVECTOR* s1) {
  d->x = (s0->x < s1->x) ? s0->x : s1->x;
  d->y = (s0->y < s1->y) ? s0->y : s1->y;
  d->z = (s0->z < s1->z) ? s0->z : s1->z;
  d->w = (s0->w < s1->w) ? s0->w : s1->w;
  VSTRACE(("executing min: s0=(%f, %f, %f, %f) s1=(%f, %f, %f, %f) => d=(%f, %f, %f, %f)\n",
	  s0->x, s0->y, s0->z, s0->w, s1->x, s1->y, s1->z, s1->w, d->x, d->y, d->z, d->w));
}

static void vshader_mov(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0) {
  d->x = s0->x;
  d->y = s0->y;
  d->z = s0->z;
  d->w = s0->w;
  VSTRACE(("executing mov: s0=(%f, %f, %f, %f) => d=(%f, %f, %f, %f)\n",
	  s0->x, s0->y, s0->z, s0->w, d->x, d->y, d->z, d->w));
}

static void vshader_mul(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0, WINED3DSHADERVECTOR* s1) {
  d->x = s0->x * s1->x;
  d->y = s0->y * s1->y;
  d->z = s0->z * s1->z;
  d->w = s0->w * s1->w;
  VSTRACE(("executing mul: s0=(%f, %f, %f, %f) s1=(%f, %f, %f, %f) => d=(%f, %f, %f, %f)\n",
	  s0->x, s0->y, s0->z, s0->w, s1->x, s1->y, s1->z, s1->w, d->x, d->y, d->z, d->w));
}

static void vshader_nop(void) {
    /* NOPPPP ahhh too easy ;) */
    VSTRACE(("executing nop\n"));
}

static void vshader_rcp(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0) {
  d->x = d->y = d->z = d->w = (0.0f == s0->w) ? HUGE_VAL : 1.0f / s0->w;
  VSTRACE(("executing rcp: s0=(%f, %f, %f, %f) => d=(%f, %f, %f, %f)\n",
	  s0->x, s0->y, s0->z, s0->w, d->x, d->y, d->z, d->w));
}

static void vshader_rsq(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0) {
  float tmp_f = fabsf(s0->w);
  d->x = d->y = d->z = d->w = (0.0f == tmp_f) ? HUGE_VAL : ((1.0f != tmp_f) ? 1.0f / sqrtf(tmp_f) : 1.0f);
  VSTRACE(("executing rsq: s0=(%f, %f, %f, %f) => d=(%f, %f, %f, %f)\n",
	  s0->x, s0->y, s0->z, s0->w, d->x, d->y, d->z, d->w));
}

static void vshader_sge(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0, WINED3DSHADERVECTOR* s1) {
  d->x = (s0->x >= s1->x) ? 1.0f : 0.0f;
  d->y = (s0->y >= s1->y) ? 1.0f : 0.0f;
  d->z = (s0->z >= s1->z) ? 1.0f : 0.0f;
  d->w = (s0->w >= s1->w) ? 1.0f : 0.0f;
  VSTRACE(("executing sge: s0=(%f, %f, %f, %f) s1=(%f, %f, %f, %f) => d=(%f, %f, %f, %f)\n",
	  s0->x, s0->y, s0->z, s0->w, s1->x, s1->y, s1->z, s1->w, d->x, d->y, d->z, d->w));
}

static void vshader_slt(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0, WINED3DSHADERVECTOR* s1) {
  d->x = (s0->x < s1->x) ? 1.0f : 0.0f;
  d->y = (s0->y < s1->y) ? 1.0f : 0.0f;
  d->z = (s0->z < s1->z) ? 1.0f : 0.0f;
  d->w = (s0->w < s1->w) ? 1.0f : 0.0f;
  VSTRACE(("executing slt: s0=(%f, %f, %f, %f) s1=(%f, %f, %f, %f) => d=(%f, %f, %f, %f)\n",
	  s0->x, s0->y, s0->z, s0->w, s1->x, s1->y, s1->z, s1->w, d->x, d->y, d->z, d->w));
}

static void vshader_sub(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0, WINED3DSHADERVECTOR* s1) {
  d->x = s0->x - s1->x;
  d->y = s0->y - s1->y;
  d->z = s0->z - s1->z;
  d->w = s0->w - s1->w;
  VSTRACE(("executing sub: s0=(%f, %f, %f, %f) s1=(%f, %f, %f, %f) => d=(%f, %f, %f, %f)\n",
	  s0->x, s0->y, s0->z, s0->w, s1->x, s1->y, s1->z, s1->w, d->x, d->y, d->z, d->w));
}

/**
 * Version 1.1 specific
 */

static void vshader_exp(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0) {
  d->x = d->y = d->z = d->w = powf(2.0f, s0->w);
  VSTRACE(("executing exp: s0=(%f, %f, %f, %f) => d=(%f, %f, %f, %f)\n",
	  s0->x, s0->y, s0->z, s0->w, d->x, d->y, d->z, d->w));
}

static void vshader_log(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0) {
  float tmp_f = fabsf(s0->w);
  d->x = d->y = d->z = d->w = (0.0f != tmp_f) ? logf(tmp_f) / logf(2.0f) : -HUGE_VAL;
  VSTRACE(("executing log: s0=(%f, %f, %f, %f) => d=(%f, %f, %f, %f)\n",
	  s0->x, s0->y, s0->z, s0->w, d->x, d->y, d->z, d->w));
}

static void vshader_frc(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0) {
  d->x = s0->x - floorf(s0->x);
  d->y = s0->y - floorf(s0->y);
  d->z = 0.0f;
  d->w = 1.0f;
  VSTRACE(("executing frc: s0=(%f, %f, %f, %f) => d=(%f, %f, %f, %f)\n",
	  s0->x, s0->y, s0->z, s0->w, d->x, d->y, d->z, d->w));
}

typedef FLOAT D3DMATRIX44[4][4];
typedef FLOAT D3DMATRIX43[4][3];
typedef FLOAT D3DMATRIX34[3][4];
typedef FLOAT D3DMATRIX33[3][3];
typedef FLOAT D3DMATRIX23[2][3];

static void vshader_m4x4(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0, /*WINED3DSHADERVECTOR* mat1*/ D3DMATRIX44 mat) {
  /*
   * Buggy CODE: here only if cast not work for copy/paste
  WINED3DSHADERVECTOR* mat2 = mat1 + 1;
  WINED3DSHADERVECTOR* mat3 = mat1 + 2;
  WINED3DSHADERVECTOR* mat4 = mat1 + 3;
  d->x = mat1->x * s0->x + mat2->x * s0->y + mat3->x * s0->z + mat4->x * s0->w;
  d->y = mat1->y * s0->x + mat2->y * s0->y + mat3->y * s0->z + mat4->y * s0->w;
  d->z = mat1->z * s0->x + mat2->z * s0->y + mat3->z * s0->z + mat4->z * s0->w;
  d->w = mat1->w * s0->x + mat2->w * s0->y + mat3->w * s0->z + mat4->w * s0->w;
  */
  d->x = mat[0][0] * s0->x + mat[0][1] * s0->y + mat[0][2] * s0->z + mat[0][3] * s0->w;
  d->y = mat[1][0] * s0->x + mat[1][1] * s0->y + mat[1][2] * s0->z + mat[1][3] * s0->w;
  d->z = mat[2][0] * s0->x + mat[2][1] * s0->y + mat[2][2] * s0->z + mat[2][3] * s0->w;
  d->w = mat[3][0] * s0->x + mat[3][1] * s0->y + mat[3][2] * s0->z + mat[3][3] * s0->w;
  VSTRACE(("executing m4x4(1): mat=(%f, %f, %f, %f)    s0=(%f)     d=(%f) \n", mat[0][0], mat[0][1], mat[0][2], mat[0][3], s0->x, d->x));
  VSTRACE(("executing m4x4(2): mat=(%f, %f, %f, %f)       (%f)       (%f) \n", mat[1][0], mat[1][1], mat[1][2], mat[1][3], s0->y, d->y));
  VSTRACE(("executing m4x4(3): mat=(%f, %f, %f, %f) X     (%f)  =    (%f) \n", mat[2][0], mat[2][1], mat[2][2], mat[2][3], s0->z, d->z));
  VSTRACE(("executing m4x4(4): mat=(%f, %f, %f, %f)       (%f)       (%f) \n", mat[3][0], mat[3][1], mat[3][2], mat[3][3], s0->w, d->w));
}

static void vshader_m4x3(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0, D3DMATRIX34 mat) {
  d->x = mat[0][0] * s0->x + mat[0][1] * s0->y + mat[0][2] * s0->z + mat[0][3] * s0->w;
  d->y = mat[1][0] * s0->x + mat[1][1] * s0->y + mat[1][2] * s0->z + mat[1][3] * s0->w;
  d->z = mat[2][0] * s0->x + mat[2][1] * s0->y + mat[2][2] * s0->z + mat[2][3] * s0->w;
  d->w = 1.0f;
  VSTRACE(("executing m4x3(1): mat=(%f, %f, %f, %f)    s0=(%f)     d=(%f) \n", mat[0][0], mat[0][1], mat[0][2], mat[0][3], s0->x, d->x));
  VSTRACE(("executing m4x3(2): mat=(%f, %f, %f, %f)       (%f)       (%f) \n", mat[1][0], mat[1][1], mat[1][2], mat[1][3], s0->y, d->y));
  VSTRACE(("executing m4x3(3): mat=(%f, %f, %f, %f) X     (%f)  =    (%f) \n", mat[2][0], mat[2][1], mat[2][2], mat[2][3], s0->z, d->z));
  VSTRACE(("executing m4x3(4):                            (%f)       (%f) \n", s0->w, d->w));
}

static void vshader_m3x4(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0, D3DMATRIX43 mat) {
  d->x = mat[0][0] * s0->x + mat[0][1] * s0->y + mat[0][2] * s0->z;
  d->y = mat[1][0] * s0->x + mat[1][1] * s0->y + mat[1][2] * s0->z;
  d->z = mat[2][0] * s0->x + mat[2][1] * s0->y + mat[2][2] * s0->z;
  d->w = mat[3][0] * s0->x + mat[3][1] * s0->y + mat[3][2] * s0->z;
  VSTRACE(("executing m3x4(1): mat=(%f, %f, %f)    s0=(%f)     d=(%f) \n", mat[0][0], mat[0][1], mat[0][2], s0->x, d->x));
  VSTRACE(("executing m3x4(2): mat=(%f, %f, %f)       (%f)       (%f) \n", mat[1][0], mat[1][1], mat[1][2], s0->y, d->y));
  VSTRACE(("executing m3x4(3): mat=(%f, %f, %f) X     (%f)  =    (%f) \n", mat[2][0], mat[2][1], mat[2][2], s0->z, d->z));
  VSTRACE(("executing m3x4(4): mat=(%f, %f, %f)       (%f)       (%f) \n", mat[3][0], mat[3][1], mat[3][2], s0->w, d->w));
}

static void vshader_m3x3(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0, D3DMATRIX33 mat) {
  d->x = mat[0][0] * s0->x + mat[0][1] * s0->y + mat[0][2] * s0->z;
  d->y = mat[1][0] * s0->x + mat[1][1] * s0->y + mat[1][2] * s0->z;
  d->z = mat[2][0] * s0->x + mat[2][1] * s0->y + mat[2][2] * s0->z;
  d->w = 1.0f;
  VSTRACE(("executing m3x3(1): mat=(%f, %f, %f)    s0=(%f)     d=(%f) \n", mat[0][0], mat[0][1], mat[0][2], s0->x, d->x));
  VSTRACE(("executing m3x3(2): mat=(%f, %f, %f)       (%f)       (%f) \n", mat[1][0], mat[1][1], mat[1][2], s0->y, d->y));
  VSTRACE(("executing m3x3(3): mat=(%f, %f, %f) X     (%f)  =    (%f) \n", mat[2][0], mat[2][1], mat[2][2], s0->z, d->z));
  VSTRACE(("executing m3x3(4):                                       (%f) \n", d->w));
}

static void vshader_m3x2(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0, D3DMATRIX23 mat) {
  FIXME("check\n");
  d->x = mat[0][0] * s0->x + mat[0][1] * s0->y + mat[0][2] * s0->z;
  d->y = mat[1][0] * s0->x + mat[1][1] * s0->y + mat[1][2] * s0->z;
  d->z = 0.0f;
  d->w = 1.0f;
}

/**
 * Version 2.0 specific
 */
static void vshader_lrp(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0, WINED3DSHADERVECTOR* s1, WINED3DSHADERVECTOR* s2) {
  d->x = s0->x * (s1->x - s2->x) + s2->x;
  d->y = s0->y * (s1->y - s2->y) + s2->y;
  d->z = s0->z * (s1->z - s2->z) + s2->z;
  d->w = s0->w * (s1->w - s2->w) + s2->w;
}

static void vshader_crs(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0, WINED3DSHADERVECTOR* s1) {
  d->x = s0->y * s1->z - s0->z * s1->y;
  d->y = s0->z * s1->x - s0->x * s1->z;
  d->z = s0->x * s1->y - s0->y * s1->x;
  d->w = 0.9f; /* w is undefined, so set it to something safeish */

  VSTRACE(("executing crs: s0=(%f, %f, %f, %f) s1=(%f, %f, %f, %f) => d=(%f, %f, %f, %f)\n",
             s0->x, s0->y, s0->z, s0->w, s1->x, s1->y, s1->z, s1->w, d->x, d->y, d->z, d->w));
}

static void vshader_abs(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0) {

  d->x = fabsf(s0->x);
  d->y = fabsf(s0->y);
  d->z = fabsf(s0->z);
  d->w = fabsf(s0->w);
  VSTRACE(("executing abs: s0=(%f, %f, %f, %f)  => d=(%f, %f, %f, %f)\n",
             s0->x, s0->y, s0->z, s0->w, d->x, d->y, d->z, d->w));
}

    /* Stubs */

/* Def is C[n] = {n.nf, n.nf, n.nf, n.nf} */
static void vshader_def(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0, WINED3DSHADERVECTOR* s1, WINED3DSHADERVECTOR* s2, WINED3DSHADERVECTOR* s3) {
    FIXME(" : Stub\n");
}

static void vshader_call(WINED3DSHADERVECTOR* d) {
    FIXME(" : Stub\n");
}

static void vshader_callnz(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0) {
    FIXME(" : Stub\n");
}

static void vshader_loop(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0) {
    FIXME(" : Stub\n");
}

static void vshader_ret(void) {
    FIXME(" : Stub\n");
}

static void vshader_endloop(void) {
    FIXME(" : Stub\n");
}

static void vshader_dcl(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0) {
    FIXME(" : Stub\n");
}

static void vshader_pow(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0, WINED3DSHADERVECTOR* s1) {
    FIXME(" : Stub\n");
}

static void vshader_sgn(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0) {
    FIXME(" : Stub\n");
}

static void vshader_nrm(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0) {
    FIXME(" : Stub\n");
}

static void vshader_sincos3(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0) {
    FIXME(" : Stub\n");
}

static void vshader_sincos2(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0, WINED3DSHADERVECTOR* s1, WINED3DSHADERVECTOR* s2) {
     FIXME(" : Stub\n");
}

static void vshader_rep(WINED3DSHADERVECTOR* d) {
    FIXME(" : Stub\n");
}

static void vshader_endrep(void) {
    FIXME(" : Stub\n");
}

static void vshader_if(WINED3DSHADERVECTOR* d) {
    FIXME(" : Stub\n");
}

static void vshader_ifc(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0) {
    FIXME(" : Stub\n");
}

static void vshader_else(void) {
    FIXME(" : Stub\n");
}

static void vshader_label(WINED3DSHADERVECTOR* d) {
    FIXME(" : Stub\n");
}

static void vshader_endif(void) {
    FIXME(" : Stub\n");
}

static void vshader_break(void) {
    FIXME(" : Stub\n");
}

static void vshader_breakc(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0) {
    FIXME(" : Stub\n");
}

static void vshader_breakp(WINED3DSHADERVECTOR* d) {
    FIXME(" : Stub\n");
}

static void vshader_mova(WINED3DSHADERVECTOR* d) {
    FIXME(" : Stub\n");
}

static void vshader_defb(WINED3DSHADERVECTOR* d) {
    FIXME(" : Stub\n");
}

static void vshader_defi(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0, WINED3DSHADERVECTOR* s1, WINED3DSHADERVECTOR* s2, WINED3DSHADERVECTOR* s3) {
    FIXME(" : Stub\n");
}

static void vshader_setp(WINED3DSHADERVECTOR* d, WINED3DSHADERVECTOR* s0, WINED3DSHADERVECTOR* s1) {
    FIXME(" : Stub\n");
}

static void vshader_texldl(WINED3DSHADERVECTOR* d) {
    FIXME(" : Stub\n");
}

CONST SHADER_OPCODE IWineD3DVertexShaderImpl_shader_ins[] = {
    /* This table is not order or position dependent. */
    
    /* Arithmetic */ 
    {WINED3DSIO_NOP,  "nop",  "NOP", 0, 0, vshader_nop,  vshader_hw_map2gl, NULL, 0, 0},
    {WINED3DSIO_MOV,  "mov",  "MOV", 1, 2, vshader_mov,  vshader_hw_map2gl, shader_glsl_mov, 0, 0},
    {WINED3DSIO_MOVA, "mova",  NULL, 1, 2, vshader_mova, vshader_hw_map2gl, shader_glsl_mov, WINED3DVS_VERSION(2,0), -1},
    {WINED3DSIO_ADD,  "add",  "ADD", 1, 3, vshader_add,  vshader_hw_map2gl, shader_glsl_arith, 0, 0},
    {WINED3DSIO_SUB,  "sub",  "SUB", 1, 3, vshader_sub,  vshader_hw_map2gl, shader_glsl_arith, 0, 0},
    {WINED3DSIO_MAD,  "mad",  "MAD", 1, 4, vshader_mad,  vshader_hw_map2gl, shader_glsl_mad, 0, 0},
    {WINED3DSIO_MUL,  "mul",  "MUL", 1, 3, vshader_mul,  vshader_hw_map2gl, shader_glsl_arith, 0, 0},
    {WINED3DSIO_RCP,  "rcp",  "RCP", 1, 2, vshader_rcp,  vshader_hw_rsq_rcp,shader_glsl_rcp, 0, 0},
    {WINED3DSIO_RSQ,  "rsq",  "RSQ", 1, 2, vshader_rsq,  vshader_hw_rsq_rcp,shader_glsl_map2gl, 0, 0},
    {WINED3DSIO_DP3,  "dp3",  "DP3", 1, 3, vshader_dp3,  vshader_hw_map2gl, shader_glsl_dot, 0, 0},
    {WINED3DSIO_DP4,  "dp4",  "DP4", 1, 3, vshader_dp4,  vshader_hw_map2gl, shader_glsl_dot, 0, 0},
    {WINED3DSIO_MIN,  "min",  "MIN", 1, 3, vshader_min,  vshader_hw_map2gl, shader_glsl_map2gl, 0, 0},
    {WINED3DSIO_MAX,  "max",  "MAX", 1, 3, vshader_max,  vshader_hw_map2gl, shader_glsl_map2gl, 0, 0},
    {WINED3DSIO_SLT,  "slt",  "SLT", 1, 3, vshader_slt,  vshader_hw_map2gl, shader_glsl_compare, 0, 0},
    {WINED3DSIO_SGE,  "sge",  "SGE", 1, 3, vshader_sge,  vshader_hw_map2gl, shader_glsl_compare, 0, 0},
    {WINED3DSIO_ABS,  "abs",  "ABS", 1, 2, vshader_abs,  vshader_hw_map2gl, shader_glsl_map2gl, 0, 0},
    {WINED3DSIO_EXP,  "exp",  "EX2", 1, 2, vshader_exp,  vshader_hw_map2gl, shader_glsl_map2gl, 0, 0},
    {WINED3DSIO_LOG,  "log",  "LG2", 1, 2, vshader_log,  vshader_hw_map2gl, shader_glsl_map2gl, 0, 0},
    {WINED3DSIO_EXPP, "expp", "EXP", 1, 2, vshader_expp, vshader_hw_map2gl, shader_glsl_expp, 0, 0},
    {WINED3DSIO_LOGP, "logp", "LOG", 1, 2, vshader_logp, vshader_hw_map2gl, shader_glsl_map2gl, 0, 0},
    {WINED3DSIO_LIT,  "lit",  "LIT", 1, 2, vshader_lit,  vshader_hw_map2gl, shader_glsl_lit, 0, 0},
    {WINED3DSIO_DST,  "dst",  "DST", 1, 3, vshader_dst,  vshader_hw_map2gl, shader_glsl_dst, 0, 0},
    {WINED3DSIO_LRP,  "lrp",  "LRP", 1, 4, vshader_lrp,  NULL,              shader_glsl_lrp, 0, 0},
    {WINED3DSIO_FRC,  "frc",  "FRC", 1, 2, vshader_frc,  vshader_hw_map2gl, shader_glsl_map2gl, 0, 0},
    {WINED3DSIO_POW,  "pow",  "POW", 1, 3, vshader_pow,  NULL, shader_glsl_map2gl, 0, 0},
    {WINED3DSIO_CRS,  "crs",  "XPS", 1, 3, vshader_crs,  NULL, shader_glsl_cross, 0, 0},
    /* TODO: sng can possibly be performed a  s
        RCP tmp, vec
        MUL out, tmp, vec*/
    {WINED3DSIO_SGN,  "sgn",  NULL, 1, 2, vshader_sgn,  NULL,   shader_glsl_map2gl, 0, 0},
    /* TODO: xyz normalise can be performed as VS_ARB using one temporary register,
        DP3 tmp , vec, vec;
        RSQ tmp, tmp.x;
        MUL vec.xyz, vec, tmp;
    but I think this is better because it accounts for w properly.
        DP3 tmp , vec, vec;
        RSQ tmp, tmp.x;
        MUL vec, vec, tmp;

    */
    {WINED3DSIO_NRM,      "nrm",      NULL, 1, 2, vshader_nrm,    NULL, shader_glsl_map2gl, 0, 0},
    {WINED3DSIO_SINCOS,   "sincos",   NULL, 1, 4, vshader_sincos2, NULL, shader_glsl_sincos, WINED3DVS_VERSION(2,0), WINED3DVS_VERSION(2,1)},
    {WINED3DSIO_SINCOS,   "sincos",   NULL, 1, 2, vshader_sincos3, NULL, shader_glsl_sincos, WINED3DVS_VERSION(3,0), -1},

    /* Matrix */
    {WINED3DSIO_M4x4, "m4x4", "undefined", 1, 3, vshader_m4x4, vshader_hw_mnxn, shader_glsl_mnxn, 0, 0},
    {WINED3DSIO_M4x3, "m4x3", "undefined", 1, 3, vshader_m4x3, vshader_hw_mnxn, shader_glsl_mnxn, 0, 0},
    {WINED3DSIO_M3x4, "m3x4", "undefined", 1, 3, vshader_m3x4, vshader_hw_mnxn, shader_glsl_mnxn, 0, 0},
    {WINED3DSIO_M3x3, "m3x3", "undefined", 1, 3, vshader_m3x3, vshader_hw_mnxn, shader_glsl_mnxn, 0, 0},
    {WINED3DSIO_M3x2, "m3x2", "undefined", 1, 3, vshader_m3x2, vshader_hw_mnxn, shader_glsl_mnxn, 0, 0},

    /* Declare registers */
    {WINED3DSIO_DCL,      "dcl",      NULL,                0, 2, vshader_dcl,     NULL, NULL, 0, 0},

    /* Constant definitions */
    {WINED3DSIO_DEF,      "def",      NULL,                1, 5, vshader_def,     NULL, NULL, 0, 0},
    {WINED3DSIO_DEFB,     "defb",     GLNAME_REQUIRE_GLSL, 1, 2, vshader_defb,    NULL, NULL, 0, 0},
    {WINED3DSIO_DEFI,     "defi",     GLNAME_REQUIRE_GLSL, 1, 5, vshader_defi,    NULL, NULL, 0, 0},

    /* Flow control - requires GLSL or software shaders */
    {WINED3DSIO_REP ,     "rep",      NULL, 0, 1, vshader_rep,     NULL, shader_glsl_rep,    WINED3DVS_VERSION(2,0), -1},
    {WINED3DSIO_ENDREP,   "endrep",   NULL, 0, 0, vshader_endrep,  NULL, shader_glsl_end,    WINED3DVS_VERSION(2,0), -1},
    {WINED3DSIO_IF,       "if",       NULL, 0, 1, vshader_if,      NULL, shader_glsl_if,     WINED3DVS_VERSION(2,0), -1},
    {WINED3DSIO_IFC,      "ifc",      NULL, 0, 2, vshader_ifc,     NULL, shader_glsl_ifc,    WINED3DVS_VERSION(2,1), -1},
    {WINED3DSIO_ELSE,     "else",     NULL, 0, 0, vshader_else,    NULL, shader_glsl_else,   WINED3DVS_VERSION(2,0), -1},
    {WINED3DSIO_ENDIF,    "endif",    NULL, 0, 0, vshader_endif,   NULL, shader_glsl_end,    WINED3DVS_VERSION(2,0), -1},
    {WINED3DSIO_BREAK,    "break",    NULL, 0, 0, vshader_break,   NULL, shader_glsl_break,  WINED3DVS_VERSION(2,1), -1},
    {WINED3DSIO_BREAKC,   "breakc",   NULL, 0, 2, vshader_breakc,  NULL, shader_glsl_breakc, WINED3DVS_VERSION(2,1), -1},
    {WINED3DSIO_BREAKP,   "breakp",   GLNAME_REQUIRE_GLSL, 0, 1, vshader_breakp,  NULL, NULL, 0, 0},
    {WINED3DSIO_CALL,     "call",     NULL, 0, 1, vshader_call,    NULL, shader_glsl_call,   WINED3DVS_VERSION(2,0), -1},
    {WINED3DSIO_CALLNZ,   "callnz",   NULL, 0, 2, vshader_callnz,  NULL, shader_glsl_callnz, WINED3DVS_VERSION(2,0), -1},
    {WINED3DSIO_LOOP,     "loop",     NULL, 0, 2, vshader_loop,    NULL, shader_glsl_loop,   WINED3DVS_VERSION(2,0), -1},
    {WINED3DSIO_RET,      "ret",      NULL, 0, 0, vshader_ret,     NULL, NULL,               WINED3DVS_VERSION(2,0), -1},
    {WINED3DSIO_ENDLOOP,  "endloop",  NULL, 0, 0, vshader_endloop, NULL, shader_glsl_end,    WINED3DVS_VERSION(2,0), -1},
    {WINED3DSIO_LABEL,    "label",    NULL, 0, 1, vshader_label,   NULL, shader_glsl_label,  WINED3DVS_VERSION(2,0), -1},

    {WINED3DSIO_SETP,     "setp",     GLNAME_REQUIRE_GLSL, 1, 3, vshader_setp,    NULL, NULL, 0, 0},
    {WINED3DSIO_TEXLDL,   "texdl",    GLNAME_REQUIRE_GLSL, 1, 2, vshader_texldl,  NULL, NULL, 0, 0},
    {0,               NULL,       NULL,   0, 0, NULL,            NULL, 0, 0}
};

static void vshader_set_limits(
      IWineD3DVertexShaderImpl *This) {

      This->baseShader.limits.texcoord = 0;
      This->baseShader.limits.attributes = 16;
      This->baseShader.limits.packed_input = 0;

      /* Must match D3DCAPS9.MaxVertexShaderConst: at least 256 for vs_2_0 */
      This->baseShader.limits.constant_float = GL_LIMITS(vshader_constantsF);

      switch (This->baseShader.hex_version) {
          case WINED3DVS_VERSION(1,0):
          case WINED3DVS_VERSION(1,1):
                   This->baseShader.limits.temporary = 12;
                   This->baseShader.limits.constant_bool = 0;
                   This->baseShader.limits.constant_int = 0;
                   This->baseShader.limits.address = 1;
                   This->baseShader.limits.packed_output = 0;
                   This->baseShader.limits.sampler = 0;
                   This->baseShader.limits.label = 0;
                   break;
      
          case WINED3DVS_VERSION(2,0):
          case WINED3DVS_VERSION(2,1):
                   This->baseShader.limits.temporary = 12;
                   This->baseShader.limits.constant_bool = 16;
                   This->baseShader.limits.constant_int = 16;
                   This->baseShader.limits.address = 1;
                   This->baseShader.limits.packed_output = 0;
                   This->baseShader.limits.sampler = 0;
                   This->baseShader.limits.label = 16;
                   break;

          case WINED3DVS_VERSION(3,0):
                   This->baseShader.limits.temporary = 32;
                   This->baseShader.limits.constant_bool = 32;
                   This->baseShader.limits.constant_int = 32;
                   This->baseShader.limits.address = 1;
                   This->baseShader.limits.packed_output = 12;
                   This->baseShader.limits.sampler = 4;
                   This->baseShader.limits.label = 16; /* FIXME: 2048 */
                   break;

          default: This->baseShader.limits.temporary = 12;
                   This->baseShader.limits.constant_bool = 16;
                   This->baseShader.limits.constant_int = 16;
                   This->baseShader.limits.address = 1;
                   This->baseShader.limits.packed_output = 0;
                   This->baseShader.limits.sampler = 0;
                   This->baseShader.limits.label = 16;
                   FIXME("Unrecognized vertex shader version %#x\n",
                       This->baseShader.hex_version);
      }
}

/* This is an internal function,
 * used to create fake semantics for shaders
 * that don't have them - d3d8 shaders where the declaration
 * stores the register for each input
 */
static void vshader_set_input(
    IWineD3DVertexShaderImpl* This,
    unsigned int regnum,
    BYTE usage, BYTE usage_idx) {

    /* Fake usage: set reserved bit, usage, usage_idx */
    DWORD usage_token = (0x1 << 31) |
        (usage << WINED3DSP_DCL_USAGE_SHIFT) | (usage_idx << WINED3DSP_DCL_USAGEINDEX_SHIFT);

    /* Fake register; set reserved bit, regnum, type: input, wmask: all */
    DWORD reg_token = (0x1 << 31) |
        WINED3DSP_WRITEMASK_ALL | (WINED3DSPR_INPUT << WINED3DSP_REGTYPE_SHIFT) | regnum;

    This->semantics_in[regnum].usage = usage_token;
    This->semantics_in[regnum].reg = reg_token;
}

static BOOL match_usage(BYTE usage1, BYTE usage_idx1, BYTE usage2, BYTE usage_idx2) {
    if (usage_idx1 != usage_idx2) return FALSE;
    if (usage1 == usage2) return TRUE;
    if (usage1 == WINED3DDECLUSAGE_POSITION && usage2 == WINED3DDECLUSAGE_POSITIONT) return TRUE;
    if (usage2 == WINED3DDECLUSAGE_POSITION && usage1 == WINED3DDECLUSAGE_POSITIONT) return TRUE;

    return FALSE;
}

BOOL vshader_get_input(
    IWineD3DVertexShader* iface,
    BYTE usage_req, BYTE usage_idx_req,
    unsigned int* regnum) {

    IWineD3DVertexShaderImpl* This = (IWineD3DVertexShaderImpl*) iface;
    int i;

    for (i = 0; i < MAX_ATTRIBS; i++) {
        DWORD usage_token = This->semantics_in[i].usage;
        DWORD usage = (usage_token & WINED3DSP_DCL_USAGE_MASK) >> WINED3DSP_DCL_USAGE_SHIFT;
        DWORD usage_idx = (usage_token & WINED3DSP_DCL_USAGEINDEX_MASK) >> WINED3DSP_DCL_USAGEINDEX_SHIFT;

        if (usage_token && match_usage(usage, usage_idx, usage_req, usage_idx_req)) {
            *regnum = i;
            return TRUE;
        }
    }
    return FALSE;
}

BOOL vshader_input_is_color(
    IWineD3DVertexShader* iface,
    unsigned int regnum) {

    IWineD3DVertexShaderImpl* This = (IWineD3DVertexShaderImpl*) iface;
    IWineD3DDeviceImpl* deviceImpl = (IWineD3DDeviceImpl*) This->baseShader.device;
    IWineD3DVertexDeclarationImpl *vertexDeclaration = (IWineD3DVertexDeclarationImpl *)deviceImpl->stateBlock->vertexDecl;

    DWORD usage_token = This->semantics_in[regnum].usage;
    DWORD usage = (usage_token & WINED3DSP_DCL_USAGE_MASK) >> WINED3DSP_DCL_USAGE_SHIFT;
    DWORD usage_idx = (usage_token & WINED3DSP_DCL_USAGEINDEX_MASK) >> WINED3DSP_DCL_USAGEINDEX_SHIFT;

    if (vertexDeclaration) {
        int i;
        /* Find the declaration element that matches our register, then check
         * if it has D3DCOLOR as it's type. This works for both d3d8 and d3d9. */
        for (i = 0; i < vertexDeclaration->declarationWNumElements-1; ++i) {
            WINED3DVERTEXELEMENT *element = vertexDeclaration->pDeclarationWine + i;
            if (match_usage(element->Usage, element->UsageIndex, usage, usage_idx)) {
                return element->Type == WINED3DDECLTYPE_D3DCOLOR;
            }
        }
    }

    ERR("Either no vertexdeclaration present, or register not matched. This should never happen.\n");
    return FALSE;
}

/** Generate a vertex shader string using either GL_VERTEX_PROGRAM_ARB
    or GLSL and send it to the card */
static VOID IWineD3DVertexShaderImpl_GenerateShader(
    IWineD3DVertexShader *iface,
    shader_reg_maps* reg_maps,
    CONST DWORD *pFunction) {

    IWineD3DVertexShaderImpl *This = (IWineD3DVertexShaderImpl *)iface;
    SHADER_BUFFER buffer;

#if 0 /* FIXME: Use the buffer that is held by the device, this is ok since fixups will be skipped for software shaders
        it also requires entering a critical section but cuts down the runtime footprint of wined3d and any memory fragmentation that may occur... */
    if (This->device->fixupVertexBufferSize < SHADER_PGMSIZE) {
        HeapFree(GetProcessHeap(), 0, This->fixupVertexBuffer);
        This->fixupVertexBuffer = HeapAlloc(GetProcessHeap() , 0, SHADER_PGMSIZE);
        This->fixupVertexBufferSize = PGMSIZE;
        This->fixupVertexBuffer[0] = 0;
    }
    buffer.buffer = This->device->fixupVertexBuffer;
#else
    buffer.buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, SHADER_PGMSIZE); 
#endif
    buffer.bsize = 0;
    buffer.lineNo = 0;
    buffer.newline = TRUE;

    if (This->baseShader.shader_mode == SHADER_GLSL) {

        /* Create the hw GLSL shader program and assign it as the baseShader.prgId */
        GLhandleARB shader_obj = GL_EXTCALL(glCreateShaderObjectARB(GL_VERTEX_SHADER_ARB));

        /* Base Declarations */
        shader_generate_glsl_declarations( (IWineD3DBaseShader*) This, reg_maps, &buffer, &GLINFO_LOCATION);

        /* Base Shader Body */
        shader_generate_main( (IWineD3DBaseShader*) This, &buffer, reg_maps, pFunction);

        /* Unpack 3.0 outputs */
        if (This->baseShader.hex_version >= WINED3DVS_VERSION(3,0))
            vshader_glsl_output_unpack(&buffer, This->semantics_out);

        /* Clamp the fog from 0 to 1 if it's used */
        if (reg_maps->fog) {
            This->usesFog = 1;
            shader_addline(&buffer, "gl_FogFragCoord = clamp(gl_FogFragCoord, 0.0, 1.0);\n");
        }
        
        /* Write the final position.
         *
         * OpenGL coordinates specify the center of the pixel while d3d coords specify
         * the corner. The offsets are stored in z and w in the 2nd row of the projection
         * matrix to avoid wasting a free shader constant. Add them to the w and z coord
         * of the 2nd row
         */
        shader_addline(&buffer, "gl_Position.x = gl_Position.x + posFixup[2];\n");
        shader_addline(&buffer, "gl_Position.y = gl_Position.y + posFixup[3];\n");
        /* Account for any inverted textures (render to texture case) by reversing the y coordinate
         *  (this is handled in drawPrim() when it sets the MODELVIEW and PROJECTION matrices)
         */
        shader_addline(&buffer, "gl_Position.y = gl_Position.y * posFixup[1];\n");

        shader_addline(&buffer, "}\n");

        TRACE("Compiling shader object %u\n", shader_obj);
        GL_EXTCALL(glShaderSourceARB(shader_obj, 1, (const char**)&buffer.buffer, NULL));
        GL_EXTCALL(glCompileShaderARB(shader_obj));
        print_glsl_info_log(&GLINFO_LOCATION, shader_obj);

        /* Store the shader object */
        This->baseShader.prgId = shader_obj;

    } else if (This->baseShader.shader_mode == SHADER_ARB) {

        /*  Create the hw ARB shader */
        shader_addline(&buffer, "!!ARBvp1.0\n");

        /* Mesa supports only 95 constants */
        if (GL_VEND(MESA) || GL_VEND(WINE))
            This->baseShader.limits.constant_float = 
                min(95, This->baseShader.limits.constant_float);

        /* Base Declarations */
        shader_generate_arb_declarations( (IWineD3DBaseShader*) This, reg_maps, &buffer, &GLINFO_LOCATION);

        /* We need a constant to fixup the final position */
        shader_addline(&buffer, "PARAM posFixup = program.env[%d];\n", ARB_SHADER_PRIVCONST_POS);

        if (reg_maps->fog) {
            This->usesFog = 1;
            shader_addline(&buffer, "TEMP TMP_FOG;\n");
        }

        /* Base Shader Body */
        shader_generate_main( (IWineD3DBaseShader*) This, &buffer, reg_maps, pFunction);

        /* Make sure the fog value is positive - values above 1.0 are ignored */
        if (reg_maps->fog)
            shader_addline(&buffer, "MAX result.fogcoord, TMP_FOG, 0.0;\n");

        /* Write the final position.
         *
         * OpenGL coordinates specify the center of the pixel while d3d coords specify
         * the corner. The offsets are stored in the 2nd row of the projection matrix,
         * the x offset in z and the y offset in w. Add them to the resulting position
         */
        shader_addline(&buffer, "ADD TMP_OUT.x, TMP_OUT.x, posFixup.z;\n");
        shader_addline(&buffer, "ADD TMP_OUT.y, TMP_OUT.y, posFixup.w;\n");
        /* Account for any inverted textures (render to texture case) by reversing the y coordinate
         *  (this is handled in drawPrim() when it sets the MODELVIEW and PROJECTION matrices)
         */
        shader_addline(&buffer, "MUL TMP_OUT.y, TMP_OUT.y, posFixup.y;\n");

        shader_addline(&buffer, "MOV result.position, TMP_OUT;\n");
        
        shader_addline(&buffer, "END\n"); 

        /* TODO: change to resource.glObjectHandle or something like that */
        GL_EXTCALL(glGenProgramsARB(1, &This->baseShader.prgId));

        TRACE("Creating a hw vertex shader, prg=%d\n", This->baseShader.prgId);
        GL_EXTCALL(glBindProgramARB(GL_VERTEX_PROGRAM_ARB, This->baseShader.prgId));

        TRACE("Created hw vertex shader, prg=%d\n", This->baseShader.prgId);
        /* Create the program and check for errors */
        GL_EXTCALL(glProgramStringARB(GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB,
            buffer.bsize, buffer.buffer));

        if (glGetError() == GL_INVALID_OPERATION) {
            GLint errPos;
            glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB, &errPos);
            FIXME("HW VertexShader Error at position %d: %s\n",
                  errPos, debugstr_a((const char *)glGetString(GL_PROGRAM_ERROR_STRING_ARB)));
            This->baseShader.prgId = -1;
        }
    }

#if 1 /* if were using the data buffer of device then we don't need to free it */
  HeapFree(GetProcessHeap(), 0, buffer.buffer);
#endif
}

BOOL IWineD3DVertexShaderImpl_ExecuteHAL(IWineD3DVertexShader* iface, WINEVSHADERINPUTDATA* input, WINEVSHADEROUTPUTDATA* output) {
  /**
   * TODO: use the NV_vertex_program (or 1_1) extension
   *  and specifics vendors (ARB_vertex_program??) variants for it
   */
  return TRUE;
}

HRESULT WINAPI IWineD3DVertexShaderImpl_ExecuteSW(IWineD3DVertexShader* iface, WINEVSHADERINPUTDATA* input, WINEVSHADEROUTPUTDATA* output) {
    IWineD3DVertexShaderImpl *This = (IWineD3DVertexShaderImpl *)iface;
    DWORD opcode_token;   
 
    /** Vertex Shader Temporary Registers */
    WINED3DSHADERVECTOR R[12];
      /*D3DSHADERSCALAR A0;*/
    WINED3DSHADERVECTOR A[1];
    /** temporary Vector for modifier management */
    WINED3DSHADERVECTOR d;
    WINED3DSHADERVECTOR s[3];
    /** parser datas */
    const DWORD* pToken = This->baseShader.function;
    const SHADER_OPCODE* curOpcode = NULL;
    /** functions parameters */
    WINED3DSHADERVECTOR* p[6];
    WINED3DSHADERVECTOR* p_send[6];
    DWORD i;

    /** init temporary register */
    memset(R, 0, 12 * sizeof(WINED3DSHADERVECTOR));

    /* vshader_program_parse(vshader); */
#if 0 /* Must not be 1 in cvs */
    TRACE("Input:\n");
    TRACE_VSVECTOR(This->data->C[0]);
    TRACE_VSVECTOR(This->data->C[1]);
    TRACE_VSVECTOR(This->data->C[2]);
    TRACE_VSVECTOR(This->data->C[3]);
    TRACE_VSVECTOR(This->data->C[4]);
    TRACE_VSVECTOR(This->data->C[5]);
    TRACE_VSVECTOR(This->data->C[6]);
    TRACE_VSVECTOR(This->data->C[7]);
    TRACE_VSVECTOR(This->data->C[8]);
    TRACE_VSVECTOR(This->data->C[64]);
    TRACE_VSVECTOR(input->V[D3DVSDE_POSITION]);
    TRACE_VSVECTOR(input->V[D3DVSDE_BLENDWEIGHT]);
    TRACE_VSVECTOR(input->V[D3DVSDE_BLENDINDICES]);
    TRACE_VSVECTOR(input->V[D3DVSDE_NORMAL]);
    TRACE_VSVECTOR(input->V[D3DVSDE_PSIZE]);
    TRACE_VSVECTOR(input->V[D3DVSDE_DIFFUSE]);
    TRACE_VSVECTOR(input->V[D3DVSDE_SPECULAR]);
    TRACE_VSVECTOR(input->V[D3DVSDE_TEXCOORD0]);
    TRACE_VSVECTOR(input->V[D3DVSDE_TEXCOORD1]);
#endif

    TRACE_VSVECTOR(vshader->data->C[64]);
    /* TODO: Run through all the tokens and find and labels, if, endifs, loops etc...., and make a labels list */

    /* the first dword is the version tag */
    /* TODO: parse it */

    if (shader_is_vshader_version(*pToken)) { /** version */
        ++pToken;
    }
    while (WINED3DVS_END() != *pToken) {
        if (shader_is_comment(*pToken)) { /** comment */
            DWORD comment_len = (*pToken & WINED3DSI_COMMENTSIZE_MASK) >> WINED3DSI_COMMENTSIZE_SHIFT;
            ++pToken;
            pToken += comment_len;
            continue ;
        }

        opcode_token = *pToken++;
        curOpcode = shader_get_opcode((IWineD3DBaseShader*) This, opcode_token);

        if (NULL == curOpcode) {
            FIXME("Unrecognized opcode: token=%08x\n", opcode_token);
            pToken += shader_skip_unrecognized((IWineD3DBaseShader*) This, pToken);
            /* return FALSE; */

        } else {
            if (curOpcode->num_params > 0) {
                /* TRACE(">> execting opcode: pos=%d opcode_name=%s token=%08lX\n", pToken - vshader->function, curOpcode->name, *pToken); */
                for (i = 0; i < curOpcode->num_params; ++i) {
                    DWORD reg = pToken[i] & WINED3DSP_REGNUM_MASK;
                    DWORD regtype = shader_get_regtype(pToken[i]);
    
                    switch (regtype) {
                    case WINED3DSPR_TEMP:
                        /* TRACE("p[%d]=R[%d]\n", i, reg); */
                        p[i] = &R[reg];
                        break;
                    case WINED3DSPR_INPUT:
                        /* TRACE("p[%d]=V[%s]\n", i, VertexShaderDeclRegister[reg]); */
                        p[i] = &input->V[reg];
                        break;
                    case WINED3DSPR_CONST:
                        if (pToken[i] & WINED3DSHADER_ADDRMODE_RELATIVE) {
                            p[i] = &This->data->C[(DWORD) A[0].x + reg];
                        } else {
                            p[i] = &This->data->C[reg];
                        }
                        break;
                    case WINED3DSPR_ADDR: /* case WINED3DSPR_TEXTURE: */
                        if (0 != reg) {
                            ERR("cannot handle address registers != a0, forcing use of a0\n");
                            reg = 0;
                        }
                        /* TRACE("p[%d]=A[%d]\n", i, reg); */
                        p[i] = &A[reg];
                        break;
                    case WINED3DSPR_RASTOUT:
                        switch (reg) {
                        case WINED3DSRO_POSITION:
                            p[i] = &output->oPos;
                            break;
                        case WINED3DSRO_FOG:
                            p[i] = &output->oFog;
                            break;
                        case WINED3DSRO_POINT_SIZE:
                            p[i] = &output->oPts;
                            break;
                        }
                        break;
                    case WINED3DSPR_ATTROUT:
                        /* TRACE("p[%d]=oD[%d]\n", i, reg); */
                        p[i] = &output->oD[reg];
                        break;
                    case WINED3DSPR_TEXCRDOUT:
                        /* TRACE("p[%d]=oT[%d]\n", i, reg); */
                        p[i] = &output->oT[reg];
                        break;
                    /* TODO Decls and defs */
#if 0
                    case WINED3DSPR_DCL:
                    case WINED3DSPR_DEF:
#endif
                    default:
                        break;
                    }

                    if (i > 0) { /* input reg */
                        DWORD swizzle = (pToken[i] & WINED3DVS_SWIZZLE_MASK) >> WINED3DVS_SWIZZLE_SHIFT;
                        UINT isNegative = ((pToken[i] & WINED3DSP_SRCMOD_MASK) == WINED3DSPSM_NEG);

                        if (!isNegative && (WINED3DVS_NOSWIZZLE >> WINED3DVS_SWIZZLE_SHIFT) == swizzle) {
                            /* TRACE("p[%d] not swizzled\n", i); */
                            p_send[i] = p[i];
                        } else {
                            DWORD swizzle_x = swizzle & 0x03;
                            DWORD swizzle_y = (swizzle >> 2) & 0x03;
                            DWORD swizzle_z = (swizzle >> 4) & 0x03;
                            DWORD swizzle_w = (swizzle >> 6) & 0x03;
                            /* TRACE("p[%d] swizzled\n", i); */
                            float* tt = (float*) p[i];
                            s[i].x = (isNegative) ? -tt[swizzle_x] : tt[swizzle_x];
                            s[i].y = (isNegative) ? -tt[swizzle_y] : tt[swizzle_y];
                            s[i].z = (isNegative) ? -tt[swizzle_z] : tt[swizzle_z];
                            s[i].w = (isNegative) ? -tt[swizzle_w] : tt[swizzle_w];
                            p_send[i] = &s[i];
                        }
                    } else { /* output reg */
                        if ((pToken[i] & WINED3DSP_WRITEMASK_ALL) == WINED3DSP_WRITEMASK_ALL) {
                            p_send[i] = p[i];
                        } else {
                            p_send[i] = &d; /* to be post-processed for modifiers management */
                        }
                    }
                }
            }

            switch (curOpcode->num_params) {
            case 0:
                curOpcode->soft_fct();
                break;
            case 1:
                curOpcode->soft_fct(p_send[0]);
            break;
            case 2:
                curOpcode->soft_fct(p_send[0], p_send[1]);
                break;
            case 3:
                curOpcode->soft_fct(p_send[0], p_send[1], p_send[2]);
                break;
            case 4:
                curOpcode->soft_fct(p_send[0], p_send[1], p_send[2], p_send[3]);
                break;
            case 5:
                curOpcode->soft_fct(p_send[0], p_send[1], p_send[2], p_send[3], p_send[4]);
                break;
            case 6:
                curOpcode->soft_fct(p_send[0], p_send[1], p_send[2], p_send[3], p_send[4], p_send[5]);
                break;
            default:
                ERR("%s too many params: %u\n", curOpcode->name, curOpcode->num_params);
            }

            /* check if output reg modifier post-process */
            if (curOpcode->num_params > 0 &&
                (pToken[0] & WINED3DSP_WRITEMASK_ALL) != WINED3DSP_WRITEMASK_ALL) {

                if (pToken[0] & WINED3DSP_WRITEMASK_0) p[0]->x = d.x; 
                if (pToken[0] & WINED3DSP_WRITEMASK_1) p[0]->y = d.y; 
                if (pToken[0] & WINED3DSP_WRITEMASK_2) p[0]->z = d.z; 
                if (pToken[0] & WINED3DSP_WRITEMASK_3) p[0]->w = d.w; 
            }
#if 0
            TRACE_VSVECTOR(output->oPos);
            TRACE_VSVECTOR(output->oD[0]);
            TRACE_VSVECTOR(output->oD[1]);
            TRACE_VSVECTOR(output->oT[0]);
            TRACE_VSVECTOR(output->oT[1]);
            TRACE_VSVECTOR(R[0]);
            TRACE_VSVECTOR(R[1]);
            TRACE_VSVECTOR(R[2]);
            TRACE_VSVECTOR(R[3]);
            TRACE_VSVECTOR(R[4]);
            TRACE_VSVECTOR(R[5]);
#endif

            /* to next opcode token */
            pToken += curOpcode->num_params;
        }
#if 0
        TRACE("End of current instruction:\n");
        TRACE_VSVECTOR(output->oPos);
        TRACE_VSVECTOR(output->oD[0]);
        TRACE_VSVECTOR(output->oD[1]);
        TRACE_VSVECTOR(output->oT[0]);
        TRACE_VSVECTOR(output->oT[1]);
        TRACE_VSVECTOR(R[0]);
        TRACE_VSVECTOR(R[1]);
        TRACE_VSVECTOR(R[2]);
        TRACE_VSVECTOR(R[3]);
        TRACE_VSVECTOR(R[4]);
        TRACE_VSVECTOR(R[5]);
#endif
    }
#if 0 /* Must not be 1 in cvs */
    TRACE("Output:\n");
    TRACE_VSVECTOR(output->oPos);
    TRACE_VSVECTOR(output->oD[0]);
    TRACE_VSVECTOR(output->oD[1]);
    TRACE_VSVECTOR(output->oT[0]);
    TRACE_VSVECTOR(output->oT[1]);
#endif
    return WINED3D_OK;
}

/* *******************************************
   IWineD3DVertexShader IUnknown parts follow
   ******************************************* */
static HRESULT WINAPI IWineD3DVertexShaderImpl_QueryInterface(IWineD3DVertexShader *iface, REFIID riid, LPVOID *ppobj)
{
    IWineD3DVertexShaderImpl *This = (IWineD3DVertexShaderImpl *)iface;
    TRACE("(%p)->(%s,%p)\n",This,debugstr_guid(riid),ppobj);
    if (IsEqualGUID(riid, &IID_IUnknown) 
        || IsEqualGUID(riid, &IID_IWineD3DBase)
        || IsEqualGUID(riid, &IID_IWineD3DBaseShader)
        || IsEqualGUID(riid, &IID_IWineD3DVertexShader)) {
        IUnknown_AddRef(iface);
        *ppobj = This;
        return S_OK;
    }
    *ppobj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI IWineD3DVertexShaderImpl_AddRef(IWineD3DVertexShader *iface) {
    IWineD3DVertexShaderImpl *This = (IWineD3DVertexShaderImpl *)iface;
    TRACE("(%p) : AddRef increasing from %d\n", This, This->ref);
    return InterlockedIncrement(&This->ref);
}

static ULONG WINAPI IWineD3DVertexShaderImpl_Release(IWineD3DVertexShader *iface) {
    IWineD3DVertexShaderImpl *This = (IWineD3DVertexShaderImpl *)iface;
    ULONG ref;
    TRACE("(%p) : Releasing from %d\n", This, This->ref);
    ref = InterlockedDecrement(&This->ref);
    if (ref == 0) {
        if (This->baseShader.shader_mode == SHADER_GLSL && This->baseShader.prgId != 0) {
            struct list *linked_programs = &This->baseShader.linked_programs;

            TRACE("Deleting linked programs\n");
            if (linked_programs->next) {
                struct glsl_shader_prog_link *entry, *entry2;
                LIST_FOR_EACH_ENTRY_SAFE(entry, entry2, linked_programs, struct glsl_shader_prog_link, vshader_entry) {
                    delete_glsl_program_entry(This->baseShader.device, entry);
                }
            }

            TRACE("Deleting shader object %u\n", This->baseShader.prgId);
            GL_EXTCALL(glDeleteObjectARB(This->baseShader.prgId));
            checkGLcall("glDeleteObjectARB");
        }
        shader_delete_constant_list(&This->baseShader.constantsF);
        shader_delete_constant_list(&This->baseShader.constantsB);
        shader_delete_constant_list(&This->baseShader.constantsI);
        HeapFree(GetProcessHeap(), 0, This);

    }
    return ref;
}

/* *******************************************
   IWineD3DVertexShader IWineD3DVertexShader parts follow
   ******************************************* */

static HRESULT WINAPI IWineD3DVertexShaderImpl_GetParent(IWineD3DVertexShader *iface, IUnknown** parent){
    IWineD3DVertexShaderImpl *This = (IWineD3DVertexShaderImpl *)iface;
    
    *parent = This->parent;
    IUnknown_AddRef(*parent);
    TRACE("(%p) : returning %p\n", This, *parent);
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DVertexShaderImpl_GetDevice(IWineD3DVertexShader* iface, IWineD3DDevice **pDevice){
    IWineD3DVertexShaderImpl *This = (IWineD3DVertexShaderImpl *)iface;
    IWineD3DDevice_AddRef(This->baseShader.device);
    *pDevice = This->baseShader.device;
    TRACE("(%p) returning %p\n", This, *pDevice);
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DVertexShaderImpl_GetFunction(IWineD3DVertexShader* impl, VOID* pData, UINT* pSizeOfData) {
    IWineD3DVertexShaderImpl *This = (IWineD3DVertexShaderImpl *)impl;
    TRACE("(%p) : pData(%p), pSizeOfData(%p)\n", This, pData, pSizeOfData);

    if (NULL == pData) {
        *pSizeOfData = This->baseShader.functionLength;
        return WINED3D_OK;
    }
    if (*pSizeOfData < This->baseShader.functionLength) {
        *pSizeOfData = This->baseShader.functionLength;
        return WINED3DERR_MOREDATA;
    }
    if (NULL == This->baseShader.function) { /* no function defined */
        TRACE("(%p) : GetFunction no User Function defined using NULL to %p\n", This, pData);
        (*(DWORD **) pData) = NULL;
    } else {
        if(This->baseShader.functionLength == 0){

        }
        TRACE("(%p) : GetFunction copying to %p\n", This, pData);
        memcpy(pData, This->baseShader.function, This->baseShader.functionLength);
    }
    return WINED3D_OK;
}

/* Note that for vertex shaders CompileShader isn't called until the
 * shader is first used. The reason for this is that we need the vertex
 * declaration the shader will be used with in order to determine if
 * the data in a register is of type D3DCOLOR, and needs swizzling. */
static HRESULT WINAPI IWineD3DVertexShaderImpl_SetFunction(IWineD3DVertexShader *iface, CONST DWORD *pFunction) {

    IWineD3DVertexShaderImpl *This =(IWineD3DVertexShaderImpl *)iface;
    IWineD3DDeviceImpl *deviceImpl = (IWineD3DDeviceImpl *) This->baseShader.device;
    HRESULT hr;
    shader_reg_maps *reg_maps = &This->baseShader.reg_maps;

    TRACE("(%p) : pFunction %p\n", iface, pFunction);

    /* First pass: trace shader */
    shader_trace_init((IWineD3DBaseShader*) This, pFunction);
    vshader_set_limits(This);

    /* Initialize immediate constant lists */
    list_init(&This->baseShader.constantsF);
    list_init(&This->baseShader.constantsB);
    list_init(&This->baseShader.constantsI);

    /* Second pass: figure out registers used, semantics, etc.. */
    memset(reg_maps, 0, sizeof(shader_reg_maps));
    hr = shader_get_registers_used((IWineD3DBaseShader*) This, reg_maps,
       This->semantics_in, This->semantics_out, pFunction, deviceImpl->stateBlock);
    if (hr != WINED3D_OK) return hr;

    This->baseShader.shader_mode = deviceImpl->vs_selected_mode;

    /* copy the function ... because it will certainly be released by application */
    if (NULL != pFunction) {
        void *function;

        function = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, This->baseShader.functionLength);
        if (!function) return E_OUTOFMEMORY;
        memcpy(function, pFunction, This->baseShader.functionLength);
        This->baseShader.function = function;
    } else {
        This->baseShader.function = NULL;
    }

    return WINED3D_OK;
}

/* Preload semantics for d3d8 shaders */
static void WINAPI IWineD3DVertexShaderImpl_FakeSemantics(IWineD3DVertexShader *iface, IWineD3DVertexDeclaration *vertex_declaration) {
    IWineD3DVertexShaderImpl *This =(IWineD3DVertexShaderImpl *)iface;
    IWineD3DVertexDeclarationImpl* vdecl = (IWineD3DVertexDeclarationImpl*)vertex_declaration;

    int i;
    for (i = 0; i < vdecl->declarationWNumElements - 1; ++i) {
        WINED3DVERTEXELEMENT* element = vdecl->pDeclarationWine + i;
        vshader_set_input(This, element->Reg, element->Usage, element->UsageIndex);
    }
}

/* Set local constants for d3d8 shaders */
static HRESULT WINAPI IWIneD3DVertexShaderImpl_SetLocalConstantsF(IWineD3DVertexShader *iface,
        UINT start_idx, const float *src_data, UINT count) {
    IWineD3DVertexShaderImpl *This =(IWineD3DVertexShaderImpl *)iface;
    UINT i, end_idx;

    TRACE("(%p) : start_idx %u, src_data %p, count %u\n", This, start_idx, src_data, count);

    end_idx = start_idx + count;
    if (end_idx > GL_LIMITS(vshader_constantsF)) {
        WARN("end_idx %u > float constants limit %u\n", end_idx, GL_LIMITS(vshader_constantsF));
        end_idx = GL_LIMITS(vshader_constantsF);
    }

    for (i = start_idx; i < end_idx; ++i) {
        local_constant* lconst = HeapAlloc(GetProcessHeap(), 0, sizeof(local_constant));
        if (!lconst) return E_OUTOFMEMORY;

        lconst->idx = i;
        CopyMemory(lconst->value, src_data + i * 4, 4 * sizeof(float));
        list_add_head(&This->baseShader.constantsF, &lconst->entry);
    }

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DVertexShaderImpl_CompileShader(IWineD3DVertexShader *iface) {
    IWineD3DVertexShaderImpl *This = (IWineD3DVertexShaderImpl *)iface;
    CONST DWORD *function = This->baseShader.function;

    TRACE("(%p) : function %p\n", iface, function);

    /* We're already compiled. */
    if (This->baseShader.is_compiled) return WINED3D_OK;

    /* We don't need to compile */
    if (!function || This->baseShader.shader_mode == SHADER_SW) {
        This->baseShader.is_compiled = TRUE;
        return WINED3D_OK;
    }

    /* Generate the HW shader */
    TRACE("(%p) : Generating hardware program\n", This);
    IWineD3DVertexShaderImpl_GenerateShader(iface, &This->baseShader.reg_maps, function);

    This->baseShader.is_compiled = TRUE;

    return WINED3D_OK;
}

const IWineD3DVertexShaderVtbl IWineD3DVertexShader_Vtbl =
{
    /*** IUnknown methods ***/
    IWineD3DVertexShaderImpl_QueryInterface,
    IWineD3DVertexShaderImpl_AddRef,
    IWineD3DVertexShaderImpl_Release,
    /*** IWineD3DBase methods ***/
    IWineD3DVertexShaderImpl_GetParent,
    /*** IWineD3DBaseShader methods ***/
    IWineD3DVertexShaderImpl_SetFunction,
    IWineD3DVertexShaderImpl_CompileShader,
    /*** IWineD3DVertexShader methods ***/
    IWineD3DVertexShaderImpl_GetDevice,
    IWineD3DVertexShaderImpl_GetFunction,
    IWineD3DVertexShaderImpl_FakeSemantics,
    IWIneD3DVertexShaderImpl_SetLocalConstantsF
};
