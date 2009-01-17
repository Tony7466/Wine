/*
 * Copyright 2008 David Adam
 * Copyright 2008 Philip Nilsson
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

#include "wine/test.h"
#include "d3dx9.h"

#define ARRAY_SIZE 5

#define admitted_error 0.0001f

#define relative_error(exp, out) ((exp == out) ? 0.0f : (fabs(out - exp) / fabs(exp)))

static inline BOOL compare_matrix(const D3DXMATRIX *m1, const D3DXMATRIX *m2)
{
    int i, j;

    for (i = 0; i < 4; ++i)
    {
        for (j = 0; j < 4; ++j)
        {
            if (fabs(U(*m1).m[i][j] - U(*m2).m[i][j]) > admitted_error)
                return FALSE;
        }
    }

    return TRUE;
}

#define expect_mat(expectedmat, gotmat) \
do { \
    const D3DXMATRIX *__m1 = (expectedmat); \
    const D3DXMATRIX *__m2 = (gotmat); \
    ok(compare_matrix(__m1, __m2), "Expected matrix=\n(%f,%f,%f,%f\n %f,%f,%f,%f\n %f,%f,%f,%f\n %f,%f,%f,%f\n)\n\n" \
            "Got matrix=\n(%f,%f,%f,%f\n %f,%f,%f,%f\n %f,%f,%f,%f\n %f,%f,%f,%f)\n", \
            U(*__m1).m[0][0], U(*__m1).m[0][1], U(*__m1).m[0][2], U(*__m1).m[0][3], \
            U(*__m1).m[1][0], U(*__m1).m[1][1], U(*__m1).m[1][2], U(*__m1).m[1][3], \
            U(*__m1).m[2][0], U(*__m1).m[2][1], U(*__m1).m[2][2], U(*__m1).m[2][3], \
            U(*__m1).m[3][0], U(*__m1).m[3][1], U(*__m1).m[3][2], U(*__m1).m[3][3], \
            U(*__m2).m[0][0], U(*__m2).m[0][1], U(*__m2).m[0][2], U(*__m2).m[0][3], \
            U(*__m2).m[1][0], U(*__m2).m[1][1], U(*__m2).m[1][2], U(*__m2).m[1][3], \
            U(*__m2).m[2][0], U(*__m2).m[2][1], U(*__m2).m[2][2], U(*__m2).m[2][3], \
            U(*__m2).m[3][0], U(*__m2).m[3][1], U(*__m2).m[3][2], U(*__m2).m[3][3]); \
} while(0)

#define compare_rotation(exp, got) \
    ok(fabs(exp.w - got.w) < admitted_error && \
       fabs(exp.x - got.x) < admitted_error && \
       fabs(exp.y - got.y) < admitted_error && \
       fabs(exp.z - got.z) < admitted_error, \
       "Expected rotation = (%f, %f, %f, %f), \
        got rotation = (%f, %f, %f, %f)\n", \
        exp.w, exp.x, exp.y, exp.z, got.w, got.x, got.y, got.z)

#define compare_scale(exp, got) \
    ok(fabs(exp.x - got.x) < admitted_error && \
       fabs(exp.y - got.y) < admitted_error && \
       fabs(exp.z - got.z) < admitted_error, \
       "Expected scale = (%f, %f, %f), \
        got scale = (%f, %f, %f)\n", \
        exp.x, exp.y, exp.z, got.x, got.y, got.z)

#define compare_translation(exp, got) \
    ok(fabs(exp.x - got.x) < admitted_error && \
       fabs(exp.y - got.y) < admitted_error && \
       fabs(exp.z - got.z) < admitted_error, \
       "Expected translation = (%f, %f, %f), \
        got translation = (%f, %f, %f)\n", \
        exp.x, exp.y, exp.z, got.x, got.y, got.z)

#define compare_vectors(exp, out) \
    for (i = 0; i < ARRAY_SIZE + 2; ++i) { \
        ok(relative_error(exp[i].x, out[i].x) < admitted_error && \
           relative_error(exp[i].y, out[i].y) < admitted_error && \
           relative_error(exp[i].z, out[i].z) < admitted_error && \
           relative_error(exp[i].w, out[i].w) < admitted_error, \
            "Got (%f, %f, %f, %f), expected (%f, %f, %f, %f) for index %d.\n", \
            out[i].x, out[i].y, out[i].z, out[i].w, \
            exp[i].x, exp[i].y, exp[i].z, exp[i].w, \
            i); \
    }

#define compare_planes(exp, out) \
    for (i = 0; i < ARRAY_SIZE + 2; ++i) { \
        ok(relative_error(exp[i].a, out[i].a) < admitted_error && \
           relative_error(exp[i].b, out[i].b) < admitted_error && \
           relative_error(exp[i].c, out[i].c) < admitted_error && \
           relative_error(exp[i].d, out[i].d) < admitted_error, \
            "Got (%f, %f, %f, %f), expected (%f, %f, %f, %f) for index %d.\n", \
            out[i].a, out[i].b, out[i].c, out[i].d, \
            exp[i].a, exp[i].b, exp[i].c, exp[i].d, \
            i); \
    }

/* The mathematical properties are checked in the d3dx8 testsuite.
 *
 * These tests check:
 *   That the functions work.
 *   That the stride functionality works.
 *   That nothing is written where it should not be.
 *
 * These tests should check:
 *   That inp_vec is not modified.
 *   That the input and output arrays can be the same (MSDN
 *     says they can, and some testing with a native DLL
 *     says so too).
 */

static void test_Matrix_AffineTransformation2D(void)
{
    D3DXMATRIX exp_mat, got_mat;
    D3DXVECTOR2 center, position;
    FLOAT angle, scale;

    center.x = 3.0f;
    center.y = 4.0f;

    position.x = -6.0f;
    position.y = 7.0f;

    angle = D3DX_PI/3.0f;

    scale = 20.0f;

    U(exp_mat).m[0][0] = 10.0f;
    U(exp_mat).m[1][0] = -17.320507f;
    U(exp_mat).m[2][0] = 0.0f;
    U(exp_mat).m[3][0] = -1.035898f;
    U(exp_mat).m[0][1] = 17.320507f;
    U(exp_mat).m[1][1] = 10.0f;
    U(exp_mat).m[2][1] = 0.0f;
    U(exp_mat).m[3][1] = 6.401924f;
    U(exp_mat).m[0][2] = 0.0f;
    U(exp_mat).m[1][2] = 0.0f;
    U(exp_mat).m[2][2] = 1.0f;
    U(exp_mat).m[3][2] = 0.0f;
    U(exp_mat).m[0][3] = 0.0f;
    U(exp_mat).m[1][3] = 0.0f;
    U(exp_mat).m[2][3] = 0.0f;
    U(exp_mat).m[3][3] = 1.0f;

    D3DXMatrixAffineTransformation2D(&got_mat, scale, &center, angle, &position);

    expect_mat(&exp_mat, &got_mat);

/*______________*/

    center.x = 3.0f;
    center.y = 4.0f;

    angle = D3DX_PI/3.0f;

    scale = 20.0f;

    U(exp_mat).m[0][0] = 10.0f;
    U(exp_mat).m[1][0] = -17.320507f;
    U(exp_mat).m[2][0] = 0.0f;
    U(exp_mat).m[3][0] = 4.964102f;
    U(exp_mat).m[0][1] = 17.320507f;
    U(exp_mat).m[1][1] = 10.0f;
    U(exp_mat).m[2][1] = 0.0f;
    U(exp_mat).m[3][1] = -0.598076f;
    U(exp_mat).m[0][2] = 0.0f;
    U(exp_mat).m[1][2] = 0.0f;
    U(exp_mat).m[2][2] = 1.0f;
    U(exp_mat).m[3][2] = 0.0f;
    U(exp_mat).m[0][3] = 0.0f;
    U(exp_mat).m[1][3] = 0.0f;
    U(exp_mat).m[2][3] = 0.0f;
    U(exp_mat).m[3][3] = 1.0f;

    D3DXMatrixAffineTransformation2D(&got_mat, scale, &center, angle, NULL);

    expect_mat(&exp_mat, &got_mat);

/*______________*/

    position.x = -6.0f;
    position.y = 7.0f;

    angle = D3DX_PI/3.0f;

    scale = 20.0f;

    U(exp_mat).m[0][0] = 10.0f;
    U(exp_mat).m[1][0] = -17.320507f;
    U(exp_mat).m[2][0] = 0.0f;
    U(exp_mat).m[3][0] = -6.0f;
    U(exp_mat).m[0][1] = 17.320507f;
    U(exp_mat).m[1][1] = 10.0f;
    U(exp_mat).m[2][1] = 0.0f;
    U(exp_mat).m[3][1] = 7.0f;
    U(exp_mat).m[0][2] = 0.0f;
    U(exp_mat).m[1][2] = 0.0f;
    U(exp_mat).m[2][2] = 1.0f;
    U(exp_mat).m[3][2] = 0.0f;
    U(exp_mat).m[0][3] = 0.0f;
    U(exp_mat).m[1][3] = 0.0f;
    U(exp_mat).m[2][3] = 0.0f;
    U(exp_mat).m[3][3] = 1.0f;

    D3DXMatrixAffineTransformation2D(&got_mat, scale, NULL, angle, &position);

    expect_mat(&exp_mat, &got_mat);

/*______________*/

    angle = 5.0f * D3DX_PI/4.0f;

    scale = -20.0f;

    U(exp_mat).m[0][0] = 14.142133f;
    U(exp_mat).m[1][0] = -14.142133f;
    U(exp_mat).m[2][0] = 0.0f;
    U(exp_mat).m[3][0] = 0.0f;
    U(exp_mat).m[0][1] = 14.142133;
    U(exp_mat).m[1][1] = 14.142133f;
    U(exp_mat).m[2][1] = 0.0f;
    U(exp_mat).m[3][1] = 0.0f;
    U(exp_mat).m[0][2] = 0.0f;
    U(exp_mat).m[1][2] = 0.0f;
    U(exp_mat).m[2][2] = 1.0f;
    U(exp_mat).m[3][2] = 0.0f;
    U(exp_mat).m[0][3] = 0.0f;
    U(exp_mat).m[1][3] = 0.0f;
    U(exp_mat).m[2][3] = 0.0f;
    U(exp_mat).m[3][3] = 1.0f;

    D3DXMatrixAffineTransformation2D(&got_mat, scale, NULL, angle, NULL);

    expect_mat(&exp_mat, &got_mat);
}

static void test_Matrix_Decompose(void)
{
    D3DXMATRIX pm;
    D3DXQUATERNION exp_rotation, got_rotation;
    D3DXVECTOR3 exp_scale, got_scale, exp_translation, got_translation;
    HRESULT hr;

/*___________*/

    U(pm).m[0][0] = -0.9238790f;
    U(pm).m[1][0] = -0.2705984f;
    U(pm).m[2][0] = 0.2705984f;
    U(pm).m[3][0] = -5.0f;
    U(pm).m[0][1] = 0.2705984f;
    U(pm).m[1][1] = 0.03806049f;
    U(pm).m[2][1] = 0.9619395f;
    U(pm).m[3][1] = 0.0f;
    U(pm).m[0][2] = -0.2705984f;
    U(pm).m[1][2] = 0.9619395f;
    U(pm).m[2][2] = 0.03806049f;
    U(pm).m[3][2] = 10.0f;
    U(pm).m[0][3] = 0.0f;
    U(pm).m[1][3] = 0.0f;
    U(pm).m[2][3] = 0.0f;
    U(pm).m[3][3] = 1.0f;

    exp_scale.x = 1.0f;
    exp_scale.y = 1.0f;
    exp_scale.z = 1.0f;

    exp_rotation.w = 0.195091f;
    exp_rotation.x = 0.0f;
    exp_rotation.y = 0.693520f;
    exp_rotation.z = 0.693520f;

    exp_translation.x = -5.0f;
    exp_translation.y = 0.0f;
    exp_translation.z = 10.0f;

    D3DXMatrixDecompose(&got_scale, &got_rotation, &got_translation, &pm);

    compare_scale(exp_scale, got_scale);
    compare_rotation(exp_rotation, got_rotation);
    compare_translation(exp_translation, got_translation);

/*_________*/

    U(pm).m[0][0] = -2.255813f;
    U(pm).m[1][0] = 1.302324f;
    U(pm).m[2][0] = 1.488373f;
    U(pm).m[3][0] = 1.0f;
    U(pm).m[0][1] = 1.302327f;
    U(pm).m[1][1] = -0.7209296f;
    U(pm).m[2][1] = 2.60465f;
    U(pm).m[3][1] = 2.0f;
    U(pm).m[0][2] = 1.488371f;
    U(pm).m[1][2] = 2.604651f;
    U(pm).m[2][2] = -0.02325551f;
    U(pm).m[3][2] = 3.0f;
    U(pm).m[0][3] = 0.0f;
    U(pm).m[1][3] = 0.0f;
    U(pm).m[2][3] = 0.0f;
    U(pm).m[3][3] = 1.0f;

    exp_scale.x = 3.0f;
    exp_scale.y = 3.0f;
    exp_scale.z = 3.0f;

    exp_rotation.w = 0.0;
    exp_rotation.x = 0.352180f;
    exp_rotation.y = 0.616316f;
    exp_rotation.z = 0.704361f;

    exp_translation.x = 1.0f;
    exp_translation.y = 2.0f;
    exp_translation.z = 3.0f;

    D3DXMatrixDecompose(&got_scale, &got_rotation, &got_translation, &pm);

    compare_scale(exp_scale, got_scale);
    compare_rotation(exp_rotation, got_rotation);
    compare_translation(exp_translation, got_translation);

/*_____________*/

    U(pm).m[0][0] = 2.427051f;
    U(pm).m[1][0] = 0.0f;
    U(pm).m[2][0] = 1.763355f;
    U(pm).m[3][0] = 5.0f;
    U(pm).m[0][1] = 0.0f;
    U(pm).m[1][1] = 3.0f;
    U(pm).m[2][1] = 0.0f;
    U(pm).m[3][1] = 5.0f;
    U(pm).m[0][2] = -1.763355f;
    U(pm).m[1][2] = 0.0f;
    U(pm).m[2][2] = 2.427051f;
    U(pm).m[3][2] = 5.0f;
    U(pm).m[0][3] = 0.0f;
    U(pm).m[1][3] = 0.0f;
    U(pm).m[2][3] = 0.0f;
    U(pm).m[3][3] = 1.0f;

    exp_scale.x = 3.0f;
    exp_scale.y = 3.0f;
    exp_scale.z = 3.0f;

    exp_rotation.w = 0.951057f;
    exp_rotation.x = 0.0f;
    exp_rotation.y = 0.309017f;
    exp_rotation.z = 0.0f;

    exp_translation.x = 5.0f;
    exp_translation.y = 5.0f;
    exp_translation.z = 5.0f;

    D3DXMatrixDecompose(&got_scale, &got_rotation, &got_translation, &pm);

    compare_scale(exp_scale, got_scale);
    compare_rotation(exp_rotation, got_rotation);
    compare_translation(exp_translation, got_translation);

/*_____________*/

    U(pm).m[0][0] = -0.9238790f;
    U(pm).m[1][0] = -0.2705984f;
    U(pm).m[2][0] = 0.2705984f;
    U(pm).m[3][0] = -5.0f;
    U(pm).m[0][1] = 0.2705984f;
    U(pm).m[1][1] = 0.03806049f;
    U(pm).m[2][1] = 0.9619395f;
    U(pm).m[3][1] = 0.0f;
    U(pm).m[0][2] = -0.2705984f;
    U(pm).m[1][2] = 0.9619395f;
    U(pm).m[2][2] = 0.03806049f;
    U(pm).m[3][2] = 10.0f;
    U(pm).m[0][3] = 0.0f;
    U(pm).m[1][3] = 0.0f;
    U(pm).m[2][3] = 0.0f;
    U(pm).m[3][3] = 1.0f;

    exp_scale.x = 1.0f;
    exp_scale.y = 1.0f;
    exp_scale.z = 1.0f;

    exp_rotation.w = 0.195091f;
    exp_rotation.x = 0.0f;
    exp_rotation.y = 0.693520f;
    exp_rotation.z = 0.693520f;

    exp_translation.x = -5.0f;
    exp_translation.y = 0.0f;
    exp_translation.z = 10.0f;

    D3DXMatrixDecompose(&got_scale, &got_rotation, &got_translation, &pm);

    compare_scale(exp_scale, got_scale);
    compare_rotation(exp_rotation, got_rotation);
    compare_translation(exp_translation, got_translation);

/*__________*/

    U(pm).m[0][0] = -0.9238790f;
    U(pm).m[1][0] = -0.5411968f;
    U(pm).m[2][0] = 0.8117952f;
    U(pm).m[3][0] = -5.0f;
    U(pm).m[0][1] = 0.2705984f;
    U(pm).m[1][1] = 0.07612098f;
    U(pm).m[2][1] = 2.8858185f;
    U(pm).m[3][1] = 0.0f;
    U(pm).m[0][2] = -0.2705984f;
    U(pm).m[1][2] = 1.9238790f;
    U(pm).m[2][2] = 0.11418147f;
    U(pm).m[3][2] = 10.0f;
    U(pm).m[0][3] = 0.0f;
    U(pm).m[1][3] = 0.0f;
    U(pm).m[2][3] = 0.0f;
    U(pm).m[3][3] = 1.0f;

    exp_scale.x = 1.0f;
    exp_scale.y = 2.0f;
    exp_scale.z = 3.0f;

    exp_rotation.w = 0.195091f;
    exp_rotation.x = 0.0f;
    exp_rotation.y = 0.693520f;
    exp_rotation.z = 0.693520f;

    exp_translation.x = -5.0f;
    exp_translation.y = 0.0f;
    exp_translation.z = 10.0f;

    D3DXMatrixDecompose(&got_scale, &got_rotation, &got_translation, &pm);

    compare_scale(exp_scale, got_scale);
    compare_rotation(exp_rotation, got_rotation);
    compare_translation(exp_translation, got_translation);

/*__________*/

    U(pm).m[0][0] = 0.7156004f;
    U(pm).m[1][0] = -0.5098283f;
    U(pm).m[2][0] = -0.4774843f;
    U(pm).m[3][0] = -5.0f;
    U(pm).m[0][1] = -0.6612288f;
    U(pm).m[1][1] = -0.7147621f;
    U(pm).m[2][1] = -0.2277977f;
    U(pm).m[3][1] = 0.0f;
    U(pm).m[0][2] = -0.2251499f;
    U(pm).m[1][2] = 0.4787385f;
    U(pm).m[2][2] = -0.8485972f;
    U(pm).m[3][2] = 10.0f;
    U(pm).m[0][3] = 0.0f;
    U(pm).m[1][3] = 0.0f;
    U(pm).m[2][3] = 0.0f;
    U(pm).m[3][3] = 1.0f;

    exp_scale.x = 1.0f;
    exp_scale.y = 1.0f;
    exp_scale.z = 1.0f;

    exp_rotation.w = 0.195091f;
    exp_rotation.x = 0.905395f;
    exp_rotation.y = -0.323355f;
    exp_rotation.z = -0.194013f;

    exp_translation.x = -5.0f;
    exp_translation.y = 0.0f;
    exp_translation.z = 10.0f;

    D3DXMatrixDecompose(&got_scale, &got_rotation, &got_translation, &pm);

    compare_scale(exp_scale, got_scale);
    compare_rotation(exp_rotation, got_rotation);
    compare_translation(exp_translation, got_translation);

/*_____________*/

    U(pm).m[0][0] = 0.06554436f;
    U(pm).m[1][0] = -0.6873012f;
    U(pm).m[2][0] = 0.7234092f;
    U(pm).m[3][0] = -5.0f;
    U(pm).m[0][1] = -0.9617381f;
    U(pm).m[1][1] = -0.2367795f;
    U(pm).m[2][1] = -0.1378230f;
    U(pm).m[3][1] = 0.0f;
    U(pm).m[0][2] = 0.2660144f;
    U(pm).m[1][2] = -0.6866967f;
    U(pm).m[2][2] = -0.6765233f;
    U(pm).m[3][2] = 10.0f;
    U(pm).m[0][3] = 0.0f;
    U(pm).m[1][3] = 0.0f;
    U(pm).m[2][3] = 0.0f;
    U(pm).m[3][3] = 1.0f;

    exp_scale.x = 1.0f;
    exp_scale.y = 1.0f;
    exp_scale.z = 1.0f;

    exp_rotation.w = -0.195091f;
    exp_rotation.x = 0.703358f;
    exp_rotation.y = -0.586131f;
    exp_rotation.z = 0.351679f;

    exp_translation.x = -5.0f;
    exp_translation.y = 0.0f;
    exp_translation.z = 10.0f;

    D3DXMatrixDecompose(&got_scale, &got_rotation, &got_translation, &pm);

    compare_scale(exp_scale, got_scale);
    compare_rotation(exp_rotation, got_rotation);
    compare_translation(exp_translation, got_translation);

/*_________*/

    U(pm).m[0][0] = 7.121047f;
    U(pm).m[1][0] = -5.883487f;
    U(pm).m[2][0] = 11.81843f;
    U(pm).m[3][0] = -5.0f;
    U(pm).m[0][1] = 5.883487f;
    U(pm).m[1][1] = -10.60660f;
    U(pm).m[2][1] = -8.825232f;
    U(pm).m[3][1] = 0.0f;
    U(pm).m[0][2] = 11.81843f;
    U(pm).m[1][2] = 8.8252320f;
    U(pm).m[2][2] = -2.727645f;
    U(pm).m[3][2] = 2.0f;
    U(pm).m[0][3] = 0.0f;
    U(pm).m[1][3] = 0.0f;
    U(pm).m[2][3] = 0.0f;
    U(pm).m[3][3] = 1.0f;

    exp_scale.x = 15.0f;
    exp_scale.y = 15.0f;
    exp_scale.z = 15.0f;

    exp_rotation.w = 0.382684f;
    exp_rotation.x = 0.768714f;
    exp_rotation.y = 0.0f;
    exp_rotation.z = 0.512476f;

    exp_translation.x = -5.0f;
    exp_translation.y = 0.0f;
    exp_translation.z = 2.0f;

    D3DXMatrixDecompose(&got_scale, &got_rotation, &got_translation, &pm);

    compare_scale(exp_scale, got_scale);
    compare_rotation(exp_rotation, got_rotation);
    compare_translation(exp_translation, got_translation);

/*__________*/

    U(pm).m[0][0] = 0.0f;
    U(pm).m[1][0] = 4.0f;
    U(pm).m[2][0] = 5.0f;
    U(pm).m[3][0] = -5.0f;
    U(pm).m[0][1] = 0.0f;
    U(pm).m[1][1] = -10.60660f;
    U(pm).m[2][1] = -8.825232f;
    U(pm).m[3][1] = 6.0f;
    U(pm).m[0][2] = 0.0f;
    U(pm).m[1][2] = 8.8252320f;
    U(pm).m[2][2] = 2.727645;
    U(pm).m[3][2] = 3.0f;
    U(pm).m[0][3] = 0.0f;
    U(pm).m[1][3] = 0.0f;
    U(pm).m[2][3] = 0.0f;
    U(pm).m[3][3] = 1.0f;

    hr = D3DXMatrixDecompose(&got_scale, &got_rotation, &got_translation, &pm);
    ok(hr == D3DERR_INVALIDCALL, "Expected D3DERR_INVALIDCALL, got %x\n", hr);
}

static void test_Matrix_Transformation2D(void)
{
    D3DXMATRIX exp_mat, got_mat;
    D3DXVECTOR2 rot_center, sca, sca_center, trans;
    FLOAT rot, sca_rot;

    rot_center.x = 3.0f;
    rot_center.y = 4.0f;

    sca.x = 12.0f;
    sca.y = -3.0f;

    sca_center.x = 9.0f;
    sca_center.y = -5.0f;

    trans.x = -6.0f;
    trans.y = 7.0f;

    rot = D3DX_PI/3.0f;

    sca_rot = 5.0f*D3DX_PI/4.0f;

    U(exp_mat).m[0][0] = -4.245192f;
    U(exp_mat).m[1][0] = -0.147116f;
    U(exp_mat).m[2][0] = 0.0f;
    U(exp_mat).m[3][0] = 45.265373f;
    U(exp_mat).m[0][1] = 7.647113f;
    U(exp_mat).m[1][1] = 8.745192f;
    U(exp_mat).m[2][1] = 0.0f;
    U(exp_mat).m[3][1] = -13.401899f;
    U(exp_mat).m[0][2] = 0.0f;
    U(exp_mat).m[1][2] = 0.0f;
    U(exp_mat).m[2][2] = 0.0f;
    U(exp_mat).m[3][2] = 0.0f;
    U(exp_mat).m[0][3] = 0.0f;
    U(exp_mat).m[1][3] = 0.0f;
    U(exp_mat).m[2][3] = 0.0f;
    U(exp_mat).m[3][3] = 1.0f;

    D3DXMatrixTransformation2D(&got_mat, &sca_center, sca_rot, &sca, &rot_center, rot, &trans);

    expect_mat(&exp_mat, &got_mat);

/*_________*/

    sca_center.x = 9.0f;
    sca_center.y = -5.0f;

    trans.x = -6.0f;
    trans.y = 7.0f;

    rot = D3DX_PI/3.0f;

    sca_rot = 5.0f*D3DX_PI/4.0f;

    U(exp_mat).m[0][0] = 0.0f;
    U(exp_mat).m[1][0] = 0.0f;
    U(exp_mat).m[2][0] = 0.0f;
    U(exp_mat).m[3][0] = 2.830127f;
    U(exp_mat).m[0][1] = 0.0f;
    U(exp_mat).m[1][1] = 0.0f;
    U(exp_mat).m[2][1] = 0.0f;
    U(exp_mat).m[3][1] = 12.294229f;
    U(exp_mat).m[0][2] = 0.0f;
    U(exp_mat).m[1][2] = 0.0f;
    U(exp_mat).m[2][2] = 0.0f;
    U(exp_mat).m[3][2] = 0.0f;
    U(exp_mat).m[0][3] = 0.0f;
    U(exp_mat).m[1][3] = 0.0f;
    U(exp_mat).m[2][3] = 0.0f;
    U(exp_mat).m[3][3] = 1.0f;

    D3DXMatrixTransformation2D(&got_mat, &sca_center, sca_rot, NULL, NULL, rot, &trans);

    expect_mat(&exp_mat, &got_mat);
}

static void test_D3DXVec_Array(void)
{
    unsigned int i;
    D3DVIEWPORT9 viewport;
    D3DXMATRIX mat, projection, view, world;
    D3DXVECTOR4 inp_vec[ARRAY_SIZE];
    D3DXVECTOR4 out_vec[ARRAY_SIZE + 2];
    D3DXVECTOR4 exp_vec[ARRAY_SIZE + 2];
    D3DXPLANE inp_plane[ARRAY_SIZE];
    D3DXPLANE out_plane[ARRAY_SIZE + 2];
    D3DXPLANE exp_plane[ARRAY_SIZE + 2];

    viewport.Width = 800; viewport.MinZ = 0.2f; viewport.X = 10;
    viewport.Height = 680; viewport.MaxZ = 0.9f; viewport.Y = 5;

    for (i = 0; i < ARRAY_SIZE + 2; ++i) {
        out_vec[i].x = out_vec[i].y = out_vec[i].z = out_vec[i].w = 0.0f;
        exp_vec[i].x = exp_vec[i].y = exp_vec[i].z = exp_vec[i].w = 0.0f;
        out_plane[i].a = out_plane[i].b = out_plane[i].c = out_plane[i].d = 0.0f;
        exp_plane[i].a = exp_plane[i].b = exp_plane[i].c = exp_plane[i].d = 0.0f;
    }

    for (i = 0; i < ARRAY_SIZE; ++i) {
        inp_plane[i].a = inp_plane[i].c = inp_vec[i].x = inp_vec[i].z = i;
        inp_plane[i].b = inp_plane[i].d = inp_vec[i].y = inp_vec[i].w = ARRAY_SIZE - i;
    }

    U(mat).m[0][0] = 1.0f; U(mat).m[0][1] = 2.0f; U(mat).m[0][2] = 3.0f; U(mat).m[0][3] = 4.0f;
    U(mat).m[1][0] = 5.0f; U(mat).m[1][1] = 6.0f; U(mat).m[1][2] = 7.0f; U(mat).m[1][3] = 8.0f;
    U(mat).m[2][0] = 9.0f; U(mat).m[2][1] = 10.0f; U(mat).m[2][2] = 11.0f; U(mat).m[2][3] = 12.0f;
    U(mat).m[3][0] = 13.0f; U(mat).m[3][1] = 14.0f; U(mat).m[3][2] = 15.0f; U(mat).m[3][3] = 16.0f;

    D3DXMatrixPerspectiveFovLH(&projection,D3DX_PI/4.0f,20.0f/17.0f,1.0f,1000.0f);

    U(view).m[0][1] = 5.0f; U(view).m[0][2] = 7.0f; U(view).m[0][3] = 8.0f;
    U(view).m[1][0] = 11.0f; U(view).m[1][2] = 16.0f; U(view).m[1][3] = 33.0f;
    U(view).m[2][0] = 19.0f; U(view).m[2][1] = -21.0f; U(view).m[2][3] = 43.0f;
    U(view).m[3][0] = 2.0f; U(view).m[3][1] = 3.0f; U(view).m[3][2] = -4.0f;
    U(view).m[0][0] = 10.0f; U(view).m[1][1] = 20.0f; U(view).m[2][2] = 30.0f;
    U(view).m[3][3] = -40.0f;

    U(world).m[0][0] = 21.0f; U(world).m[0][1] = 2.0f; U(world).m[0][2] = 3.0f; U(world).m[0][3] = 4.0;
    U(world).m[1][0] = 5.0f; U(world).m[1][1] = 23.0f; U(world).m[1][2] = 7.0f; U(world).m[1][3] = 8.0f;
    U(world).m[2][0] = -8.0f; U(world).m[2][1] = -7.0f; U(world).m[2][2] = 25.0f; U(world).m[2][3] = -5.0f;
    U(world).m[3][0] = -4.0f; U(world).m[3][1] = -3.0f; U(world).m[3][2] = -2.0f; U(world).m[3][3] = 27.0f;

    /* D3DXVec2TransformCoordArray */
    exp_vec[1].x = 0.678571f; exp_vec[1].y = 0.785714f;
    exp_vec[2].x = 0.653846f; exp_vec[2].y = 0.769231f;
    exp_vec[3].x = 0.625f;    exp_vec[3].y = 0.75f;
    exp_vec[4].x = 0.590909f; exp_vec[4].y = 8.0f/11.0f;
    exp_vec[5].x = 0.55f;     exp_vec[5].y = 0.7f;
    D3DXVec2TransformCoordArray((D3DXVECTOR2*)(out_vec + 1), sizeof(D3DXVECTOR4), (D3DXVECTOR2*)inp_vec, sizeof(D3DXVECTOR4), &mat, ARRAY_SIZE);
    compare_vectors(exp_vec, out_vec);

    /* D3DXVec2TransformNormalArray */
    exp_vec[1].x = 25.0f; exp_vec[1].y = 30.0f;
    exp_vec[2].x = 21.0f; exp_vec[2].y = 26.0f;
    exp_vec[3].x = 17.0f; exp_vec[3].y = 22.0f;
    exp_vec[4].x = 13.0f; exp_vec[4].y = 18.0f;
    exp_vec[5].x =  9.0f; exp_vec[5].y = 14.0f;
    D3DXVec2TransformNormalArray((D3DXVECTOR2*)(out_vec + 1), sizeof(D3DXVECTOR4), (D3DXVECTOR2*)inp_vec, sizeof(D3DXVECTOR4), &mat, ARRAY_SIZE);
    compare_vectors(exp_vec, out_vec);

    /* D3DXVec3TransformCoordArray */
    exp_vec[1].x = 0.678571f; exp_vec[1].y = 0.785714f;  exp_vec[1].z = 0.892857f;
    exp_vec[2].x = 0.671875f; exp_vec[2].y = 0.78125f;   exp_vec[2].z = 0.890625f;
    exp_vec[3].x = 6.0f/9.0f; exp_vec[3].y = 7.0f/9.0f;  exp_vec[3].z = 8.0f/9.0f;
    exp_vec[4].x = 0.6625f;   exp_vec[4].y = 0.775f;     exp_vec[4].z = 0.8875f;
    exp_vec[5].x = 0.659091f; exp_vec[5].y = 0.772727f;  exp_vec[5].z = 0.886364f;
    D3DXVec3TransformCoordArray((D3DXVECTOR3*)(out_vec + 1), sizeof(D3DXVECTOR4), (D3DXVECTOR3*)inp_vec, sizeof(D3DXVECTOR4), &mat, ARRAY_SIZE);
    compare_vectors(exp_vec, out_vec);

    /* D3DXVec3TransformNormalArray */
    exp_vec[1].x = 25.0f; exp_vec[1].y = 30.0f; exp_vec[1].z = 35.0f;
    exp_vec[2].x = 30.0f; exp_vec[2].y = 36.0f; exp_vec[2].z = 42.0f;
    exp_vec[3].x = 35.0f; exp_vec[3].y = 42.0f; exp_vec[3].z = 49.0f;
    exp_vec[4].x = 40.0f; exp_vec[4].y = 48.0f; exp_vec[4].z = 56.0f;
    exp_vec[5].x = 45.0f; exp_vec[5].y = 54.0f; exp_vec[5].z = 63.0f;
    D3DXVec3TransformNormalArray((D3DXVECTOR3*)(out_vec + 1), sizeof(D3DXVECTOR4), (D3DXVECTOR3*)inp_vec, sizeof(D3DXVECTOR4), &mat, ARRAY_SIZE);
    compare_vectors(exp_vec, out_vec);

    /* D3DXVec3ProjectArray */
    exp_vec[1].x = 1089.554199f; exp_vec[1].y = -226.590622f; exp_vec[1].z = 0.215273f;
    exp_vec[2].x = 1068.903320f; exp_vec[2].y =  103.085129f; exp_vec[2].z = 0.183050f;
    exp_vec[3].x = 1051.778931f; exp_vec[3].y =  376.462250f; exp_vec[3].z = 0.156329f;
    exp_vec[4].x = 1037.348877f; exp_vec[4].y =  606.827393f; exp_vec[4].z = 0.133813f;
    exp_vec[5].x = 1025.023560f; exp_vec[5].y =  803.591248f; exp_vec[5].z = 0.114581f;
    D3DXVec3ProjectArray((D3DXVECTOR3*)(out_vec + 1), sizeof(D3DXVECTOR4), (CONST D3DXVECTOR3*)inp_vec, sizeof(D3DXVECTOR4), &viewport, &projection, &view, &world, ARRAY_SIZE);
    compare_vectors(exp_vec, out_vec);

    /* D3DXVec3UnprojectArray */
    exp_vec[1].x = -6.124031f; exp_vec[1].y = 3.225360f; exp_vec[1].z = 0.620571f;
    exp_vec[2].x = -3.807109f; exp_vec[2].y = 2.046579f; exp_vec[2].z = 0.446894f;
    exp_vec[3].x = -2.922839f; exp_vec[3].y = 1.596689f; exp_vec[3].z = 0.380609f;
    exp_vec[4].x = -2.456225f; exp_vec[4].y = 1.359290f; exp_vec[4].z = 0.345632f;
    exp_vec[5].x = -2.167897f; exp_vec[5].y = 1.212597f; exp_vec[5].z = 0.324019f;
    D3DXVec3UnprojectArray((D3DXVECTOR3*)(out_vec + 1), sizeof(D3DXVECTOR4), (CONST D3DXVECTOR3*)inp_vec, sizeof(D3DXVECTOR4), &viewport, &projection, &view, &world, ARRAY_SIZE);
    compare_vectors(exp_vec, out_vec);

    /* D3DXVec2TransformArray */
    exp_vec[1].x = 38.0f; exp_vec[1].y = 44.0f; exp_vec[1].z = 50.0f; exp_vec[1].w = 56.0f;
    exp_vec[2].x = 34.0f; exp_vec[2].y = 40.0f; exp_vec[2].z = 46.0f; exp_vec[2].w = 52.0f;
    exp_vec[3].x = 30.0f; exp_vec[3].y = 36.0f; exp_vec[3].z = 42.0f; exp_vec[3].w = 48.0f;
    exp_vec[4].x = 26.0f; exp_vec[4].y = 32.0f; exp_vec[4].z = 38.0f; exp_vec[4].w = 44.0f;
    exp_vec[5].x = 22.0f; exp_vec[5].y = 28.0f; exp_vec[5].z = 34.0f; exp_vec[5].w = 40.0f;
    D3DXVec2TransformArray(out_vec + 1, sizeof(D3DXVECTOR4), (D3DXVECTOR2*)inp_vec, sizeof(D3DXVECTOR4), &mat, ARRAY_SIZE);
    compare_vectors(exp_vec, out_vec);

    /* D3DXVec3TransformArray */
    exp_vec[1].x = 38.0f; exp_vec[1].y = 44.0f; exp_vec[1].z = 50.0f; exp_vec[1].w = 56.0f;
    exp_vec[2].x = 43.0f; exp_vec[2].y = 50.0f; exp_vec[2].z = 57.0f; exp_vec[2].w = 64.0f;
    exp_vec[3].x = 48.0f; exp_vec[3].y = 56.0f; exp_vec[3].z = 64.0f; exp_vec[3].w = 72.0f;
    exp_vec[4].x = 53.0f; exp_vec[4].y = 62.0f; exp_vec[4].z = 71.0f; exp_vec[4].w = 80.0f;
    exp_vec[5].x = 58.0f; exp_vec[5].y = 68.0f; exp_vec[5].z = 78.0f; exp_vec[5].w = 88.0f;
    D3DXVec3TransformArray(out_vec + 1, sizeof(D3DXVECTOR4), (D3DXVECTOR3*)inp_vec, sizeof(D3DXVECTOR4), &mat, ARRAY_SIZE);
    compare_vectors(exp_vec, out_vec);

    /* D3DXVec4TransformArray */
    exp_vec[1].x = 90.0f; exp_vec[1].y = 100.0f; exp_vec[1].z = 110.0f; exp_vec[1].w = 120.0f;
    exp_vec[2].x = 82.0f; exp_vec[2].y = 92.0f;  exp_vec[2].z = 102.0f; exp_vec[2].w = 112.0f;
    exp_vec[3].x = 74.0f; exp_vec[3].y = 84.0f;  exp_vec[3].z = 94.0f;  exp_vec[3].w = 104.0f;
    exp_vec[4].x = 66.0f; exp_vec[4].y = 76.0f;  exp_vec[4].z = 86.0f;  exp_vec[4].w = 96.0f;
    exp_vec[5].x = 58.0f; exp_vec[5].y = 68.0f;  exp_vec[5].z = 78.0f;  exp_vec[5].w = 88.0f;
    D3DXVec4TransformArray(out_vec + 1, sizeof(D3DXVECTOR4), inp_vec, sizeof(D3DXVECTOR4), &mat, ARRAY_SIZE);
    compare_vectors(exp_vec, out_vec);

    /* D3DXPlaneTransformArray */
    exp_plane[1].a = 90.0f; exp_plane[1].b = 100.0f; exp_plane[1].c = 110.0f; exp_plane[1].d = 120.0f;
    exp_plane[2].a = 82.0f; exp_plane[2].b = 92.0f;  exp_plane[2].c = 102.0f; exp_plane[2].d = 112.0f;
    exp_plane[3].a = 74.0f; exp_plane[3].b = 84.0f;  exp_plane[3].c = 94.0f;  exp_plane[3].d = 104.0f;
    exp_plane[4].a = 66.0f; exp_plane[4].b = 76.0f;  exp_plane[4].c = 86.0f;  exp_plane[4].d = 96.0f;
    exp_plane[5].a = 58.0f; exp_plane[5].b = 68.0f;  exp_plane[5].c = 78.0f;  exp_plane[5].d = 88.0f;
    D3DXPlaneTransformArray(out_plane + 1, sizeof(D3DXPLANE), inp_plane, sizeof(D3DXPLANE), &mat, ARRAY_SIZE);
    compare_planes(exp_plane, out_plane);
}

START_TEST(math)
{
    test_Matrix_AffineTransformation2D();
    test_Matrix_Decompose();
    test_Matrix_Transformation2D();
    test_D3DXVec_Array();
}
