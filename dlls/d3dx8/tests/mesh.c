/*
 * Copyright 2008 David Adam
 * Copyright 2008 Luis Busquets
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

#include "d3dx8.h"

#include "wine/test.h"

#define admitted_error 0.0001f

#define compare_vertex_sizes(type, exp) \
    got=D3DXGetFVFVertexSize(type); \
    ok(got==exp, "Expected: %d, Got: %d\n", exp, got);

static BOOL compare(FLOAT u, FLOAT v)
{
    return (fabs(u-v) < admitted_error);
}

static BOOL compare_vec3(D3DXVECTOR3 u, D3DXVECTOR3 v)
{
    return ( compare(u.x, v.x) && compare(u.y, v.y) && compare(u.z, v.z) );
}

static void D3DXBoundProbeTest(void)
{
    BOOL result;
    D3DXVECTOR3 bottom_point, center, top_point, raydirection, rayposition;
    FLOAT radius;

/*____________Test the Box case___________________________*/
    bottom_point.x = -3.0f; bottom_point.y = -2.0f; bottom_point.z = -1.0f;
    top_point.x = 7.0f; top_point.y = 8.0f; top_point.z = 9.0f;

    raydirection.x = -4.0f; raydirection.y = -5.0f; raydirection.z = -6.0f;
    rayposition.x = 5.0f; rayposition.y = 5.0f; rayposition.z = 11.0f;
    result = D3DXBoxBoundProbe(&bottom_point, &top_point, &rayposition, &raydirection);
    ok(result == TRUE, "expected TRUE, received FALSE\n");

    raydirection.x = 4.0f; raydirection.y = 5.0f; raydirection.z = 6.0f;
    rayposition.x = 5.0f; rayposition.y = 5.0f; rayposition.z = 11.0f;
    result = D3DXBoxBoundProbe(&bottom_point, &top_point, &rayposition, &raydirection);
    ok(result == FALSE, "expected FALSE, received TRUE\n");

    rayposition.x = -4.0f; rayposition.y = 1.0f; rayposition.z = -2.0f;
    result = D3DXBoxBoundProbe(&bottom_point, &top_point, &rayposition, &raydirection);
    ok(result == TRUE, "expected TRUE, received FALSE\n");

    bottom_point.x = 1.0f; bottom_point.y = 0.0f; bottom_point.z = 0.0f;
    top_point.x = 1.0f; top_point.y = 0.0f; top_point.z = 0.0f;
    rayposition.x = 0.0f; rayposition.y = 1.0f; rayposition.z = 0.0f;
    raydirection.x = 0.0f; raydirection.y = 3.0f; raydirection.z = 0.0f;
    result = D3DXBoxBoundProbe(&bottom_point, &top_point, &rayposition, &raydirection);
    ok(result == FALSE, "expected FALSE, received TRUE\n");

    bottom_point.x = 1.0f; bottom_point.y = 2.0f; bottom_point.z = 3.0f;
    top_point.x = 10.0f; top_point.y = 15.0f; top_point.z = 20.0f;

    raydirection.x = 7.0f; raydirection.y = 8.0f; raydirection.z = 9.0f;
    rayposition.x = 3.0f; rayposition.y = 7.0f; rayposition.z = -6.0f;
    result = D3DXBoxBoundProbe(&bottom_point, &top_point, &rayposition, &raydirection);
    ok(result == TRUE, "expected TRUE, received FALSE\n");

    bottom_point.x = 0.0f; bottom_point.y = 0.0f; bottom_point.z = 0.0f;
    top_point.x = 1.0f; top_point.y = 1.0f; top_point.z = 1.0f;

    raydirection.x = 0.0f; raydirection.y = 1.0f; raydirection.z = .0f;
    rayposition.x = -3.0f; rayposition.y = 0.0f; rayposition.z = 0.0f;
    result = D3DXBoxBoundProbe(&bottom_point, &top_point, &rayposition, &raydirection);
    ok(result == FALSE, "expected FALSE, received TRUE\n");

    raydirection.x = 1.0f; raydirection.y = 0.0f; raydirection.z = .0f;
    rayposition.x = -3.0f; rayposition.y = 0.0f; rayposition.z = 0.0f;
    result = D3DXBoxBoundProbe(&bottom_point, &top_point, &rayposition, &raydirection);
    ok(result == TRUE, "expected TRUE, received FALSE\n");

/*____________Test the Sphere case________________________*/
    radius = sqrt(77.0f);
    center.x = 1.0f; center.y = 2.0f; center.z = 3.0f;
    raydirection.x = 2.0f; raydirection.y = -4.0f; raydirection.z = 2.0f;

    rayposition.x = 5.0f; rayposition.y = 5.0f; rayposition.z = 9.0f;
    result = D3DXSphereBoundProbe(&center, radius, &rayposition, &raydirection);
    ok(result == TRUE, "expected TRUE, received FALSE\n");

    rayposition.x = 45.0f; rayposition.y = -75.0f; rayposition.z = 49.0f;
    result = D3DXSphereBoundProbe(&center, radius, &rayposition, &raydirection);
    ok(result == FALSE, "expected FALSE, received TRUE\n");

    rayposition.x = 5.0f; rayposition.y = 7.0f; rayposition.z = 9.0f;
    result = D3DXSphereBoundProbe(&center, radius, &rayposition, &raydirection);
    ok(result == FALSE, "expected FALSE, received TRUE\n");

    rayposition.x = 5.0f; rayposition.y = 11.0f; rayposition.z = 9.0f;
    result = D3DXSphereBoundProbe(&center, radius, &rayposition, &raydirection);
    ok(result == FALSE, "expected FALSE, received TRUE\n");
}

static void D3DXComputeBoundingBoxTest(void)
{
    D3DXVECTOR3 exp_max, exp_min, got_max, got_min, vertex[5];
    HRESULT hr;

    vertex[0].x = 1.0f; vertex[0].y = 1.0f; vertex[0].z = 1.0f;
    vertex[1].x = 1.0f; vertex[1].y = 1.0f; vertex[1].z = 1.0f;
    vertex[2].x = 1.0f; vertex[2].y = 1.0f; vertex[2].z = 1.0f;
    vertex[3].x = 1.0f; vertex[3].y = 1.0f; vertex[3].z = 1.0f;
    vertex[4].x = 9.0f; vertex[4].y = 9.0f; vertex[4].z = 9.0f;

    exp_min.x = 1.0f; exp_min.y = 1.0f; exp_min.z = 1.0f;
    exp_max.x = 1.0f; exp_max.y = 1.0f; exp_max.z = 1.0f;

    hr = D3DXComputeBoundingBox(&vertex[3],2,D3DFVF_XYZ,&got_min,&got_max);

    ok( hr == D3D_OK, "Expected D3D_OK, got %#x\n", hr);
    ok( compare_vec3(exp_min,got_min), "Expected min: (%f, %f, %f), got: (%f, %f, %f)\n", exp_min.x,exp_min.y,exp_min.z,got_min.x,got_min.y,got_min.z);
    ok( compare_vec3(exp_max,got_max), "Expected max: (%f, %f, %f), got: (%f, %f, %f)\n", exp_max.x,exp_max.y,exp_max.z,got_max.x,got_max.y,got_max.z);

/*________________________*/

    vertex[0].x = 2.0f; vertex[0].y = 5.9f; vertex[0].z = -1.2f;
    vertex[1].x = -1.87f; vertex[1].y = 7.9f; vertex[1].z = 7.4f;
    vertex[2].x = 7.43f; vertex[2].y = -0.9f; vertex[2].z = 11.9f;
    vertex[3].x = -6.92f; vertex[3].y = 6.3f; vertex[3].z = -3.8f;
    vertex[4].x = 11.4f; vertex[4].y = -8.1f; vertex[4].z = 4.5f;

    exp_min.x = -6.92f; exp_min.y = -0.90f; exp_min.z = -3.80f;
    exp_max.x = 7.43f; exp_max.y = 7.90f; exp_max.z = 11.9f;

    hr = D3DXComputeBoundingBox(&vertex[0],5,D3DFVF_XYZ,&got_min,&got_max);

    ok( hr == D3D_OK, "Expected D3D_OK, got %#x\n", hr);
    ok( compare_vec3(exp_min,got_min), "Expected min: (%f, %f, %f), got: (%f, %f, %f)\n", exp_min.x,exp_min.y,exp_min.z,got_min.x,got_min.y,got_min.z);
    ok( compare_vec3(exp_max,got_max), "Expected max: (%f, %f, %f), got: (%f, %f, %f)\n", exp_max.x,exp_max.y,exp_max.z,got_max.x,got_max.y,got_max.z);

/*________________________*/

    vertex[0].x = 2.0f; vertex[0].y = 5.9f; vertex[0].z = -1.2f;
    vertex[1].x = -1.87f; vertex[1].y = 7.9f; vertex[1].z = 7.4f;
    vertex[2].x = 7.43f; vertex[2].y = -0.9f; vertex[2].z = 11.9f;
    vertex[3].x = -6.92f; vertex[3].y = 6.3f; vertex[3].z = -3.8f;
    vertex[4].x = 11.4f; vertex[4].y = -8.1f; vertex[4].z = 4.5f;

    exp_min.x = -1.87f; exp_min.y = -0.90f; exp_min.z = -1.20f;
    exp_max.x = 7.43f; exp_max.y = 7.90f; exp_max.z = 11.9f;

    hr = D3DXComputeBoundingBox(&vertex[0],4,D3DFVF_XYZ,&got_min,&got_max);

    ok( hr == D3D_OK, "Expected D3D_OK, got %#x\n", hr);
    ok( compare_vec3(exp_min,got_min), "Expected min: (%f, %f, %f), got: (%f, %f, %f)\n", exp_min.x,exp_min.y,exp_min.z,got_min.x,got_min.y,got_min.z);
    ok( compare_vec3(exp_max,got_max), "Expected max: (%f, %f, %f), got: (%f, %f, %f)\n", exp_max.x,exp_max.y,exp_max.z,got_max.x,got_max.y,got_max.z);

/*________________________*/
    hr = D3DXComputeBoundingBox(NULL,5,D3DFVF_XYZ,&got_min,&got_max);
    ok( hr == D3DERR_INVALIDCALL, "Expected D3DERR_INVALIDCALL, got %#x\n", hr);

/*________________________*/
    hr = D3DXComputeBoundingBox(&vertex[3],5,D3DFVF_XYZ,NULL,&got_max);
    ok( hr == D3DERR_INVALIDCALL, "Expected D3DERR_INVALIDCALL, got %#x\n", hr);

/*________________________*/
    hr = D3DXComputeBoundingBox(&vertex[3],5,D3DFVF_XYZ,&got_min,NULL);
    ok( hr == D3DERR_INVALIDCALL, "Expected D3DERR_INVALIDCALL, got %#x\n", hr);
}

static void D3DXComputeBoundingSphereTest(void)
{
    D3DXVECTOR3 exp_cen, got_cen, vertex[5];
    FLOAT exp_rad, got_rad;
    HRESULT hr;

    vertex[0].x = 1.0f; vertex[0].y = 1.0f; vertex[0].z = 1.0f;
    vertex[1].x = 1.0f; vertex[1].y = 1.0f; vertex[1].z = 1.0f;
    vertex[2].x = 1.0f; vertex[2].y = 1.0f; vertex[2].z = 1.0f;
    vertex[3].x = 1.0f; vertex[3].y = 1.0f; vertex[3].z = 1.0f;
    vertex[4].x = 9.0f; vertex[4].y = 9.0f; vertex[4].z = 9.0f;

    exp_rad = 6.928203f;
    exp_cen.x = 5.0; exp_cen.y = 5.0; exp_cen.z = 5.0;

    hr = D3DXComputeBoundingSphere(&vertex[3],2,D3DFVF_XYZ,&got_cen,&got_rad);

    ok( hr == D3D_OK, "Expected D3D_OK, got %#x\n", hr);
    ok( compare(exp_rad, got_rad), "Expected radius: %f, got radius: %f\n", exp_rad, got_rad);
    ok( compare_vec3(exp_cen,got_cen), "Expected center: (%f, %f, %f), got center: (%f, %f, %f)\n", exp_cen.x,exp_cen.y,exp_cen.z,got_cen.x,got_cen.y,got_cen.z);

/*________________________*/

    vertex[0].x = 2.0f; vertex[0].y = 5.9f; vertex[0].z = -1.2f;
    vertex[1].x = -1.87f; vertex[1].y = 7.9f; vertex[1].z = 7.4f;
    vertex[2].x = 7.43f; vertex[2].y = -0.9f; vertex[2].z = 11.9f;
    vertex[3].x = -6.92f; vertex[3].y = 6.3f; vertex[3].z = -3.8f;
    vertex[4].x = 11.4f; vertex[4].y = -8.1f; vertex[4].z = 4.5f;

    exp_rad = 13.707883f;
    exp_cen.x = 2.408f; exp_cen.y = 2.22f; exp_cen.z = 3.76f;

    hr = D3DXComputeBoundingSphere(&vertex[0],5,D3DFVF_XYZ,&got_cen,&got_rad);

    ok( hr == D3D_OK, "Expected D3D_OK, got %#x\n", hr);
    ok( compare(exp_rad, got_rad), "Expected radius: %f, got radius: %f\n", exp_rad, got_rad);
    ok( compare_vec3(exp_cen,got_cen), "Expected center: (%f, %f, %f), got center: (%f, %f, %f)\n", exp_cen.x,exp_cen.y,exp_cen.z,got_cen.x,got_cen.y,got_cen.z);

/*________________________*/
    hr = D3DXComputeBoundingSphere(NULL,5,D3DFVF_XYZ,&got_cen,&got_rad);
    ok( hr == D3DERR_INVALIDCALL, "Expected D3DERR_INVALIDCALL, got %#x\n", hr);

/*________________________*/
    hr = D3DXComputeBoundingSphere(&vertex[3],5,D3DFVF_XYZ,NULL,&got_rad);
    ok( hr == D3DERR_INVALIDCALL, "Expected D3DERR_INVALIDCALL, got %#x\n", hr);

/*________________________*/
    hr = D3DXComputeBoundingSphere(&vertex[3],5,D3DFVF_XYZ,&got_cen,NULL);
    ok( hr == D3DERR_INVALIDCALL, "Expected D3DERR_INVALIDCALL, got %#x\n", hr);
}

static void D3DXGetFVFVertexSizeTest(void)
{
    UINT got;

    compare_vertex_sizes (D3DFVF_XYZ, 12);

    compare_vertex_sizes (D3DFVF_XYZB3, 24);

    compare_vertex_sizes (D3DFVF_XYZB5, 32);

    compare_vertex_sizes (D3DFVF_XYZ | D3DFVF_NORMAL, 24);

    compare_vertex_sizes (D3DFVF_XYZ | D3DFVF_DIFFUSE, 16);

    compare_vertex_sizes (
        D3DFVF_XYZ |
        D3DFVF_TEX1 |
        D3DFVF_TEXCOORDSIZE1(0), 16);
    compare_vertex_sizes (
        D3DFVF_XYZ |
        D3DFVF_TEX2 |
        D3DFVF_TEXCOORDSIZE1(0) |
        D3DFVF_TEXCOORDSIZE1(1), 20);

    compare_vertex_sizes (
        D3DFVF_XYZ |
        D3DFVF_TEX1 |
        D3DFVF_TEXCOORDSIZE2(0), 20);

    compare_vertex_sizes (
        D3DFVF_XYZ |
        D3DFVF_TEX2 |
        D3DFVF_TEXCOORDSIZE2(0) |
        D3DFVF_TEXCOORDSIZE2(1), 28);

    compare_vertex_sizes (
        D3DFVF_XYZ |
        D3DFVF_TEX6 |
        D3DFVF_TEXCOORDSIZE2(0) |
        D3DFVF_TEXCOORDSIZE2(1) |
        D3DFVF_TEXCOORDSIZE2(2) |
        D3DFVF_TEXCOORDSIZE2(3) |
        D3DFVF_TEXCOORDSIZE2(4) |
        D3DFVF_TEXCOORDSIZE2(5), 60);

    compare_vertex_sizes (
        D3DFVF_XYZ |
        D3DFVF_TEX8 |
        D3DFVF_TEXCOORDSIZE2(0) |
        D3DFVF_TEXCOORDSIZE2(1) |
        D3DFVF_TEXCOORDSIZE2(2) |
        D3DFVF_TEXCOORDSIZE2(3) |
        D3DFVF_TEXCOORDSIZE2(4) |
        D3DFVF_TEXCOORDSIZE2(5) |
        D3DFVF_TEXCOORDSIZE2(6) |
        D3DFVF_TEXCOORDSIZE2(7), 76);

    compare_vertex_sizes (
        D3DFVF_XYZ |
        D3DFVF_TEX1 |
        D3DFVF_TEXCOORDSIZE3(0), 24);

    compare_vertex_sizes (
        D3DFVF_XYZ |
        D3DFVF_TEX4 |
        D3DFVF_TEXCOORDSIZE3(0) |
        D3DFVF_TEXCOORDSIZE3(1) |
        D3DFVF_TEXCOORDSIZE3(2) |
        D3DFVF_TEXCOORDSIZE3(3), 60);

    compare_vertex_sizes (
        D3DFVF_XYZ |
        D3DFVF_TEX1 |
        D3DFVF_TEXCOORDSIZE4(0), 28);

    compare_vertex_sizes (
        D3DFVF_XYZ |
        D3DFVF_TEX2 |
        D3DFVF_TEXCOORDSIZE4(0) |
        D3DFVF_TEXCOORDSIZE4(1), 44);

    compare_vertex_sizes (
        D3DFVF_XYZ |
        D3DFVF_TEX3 |
        D3DFVF_TEXCOORDSIZE4(0) |
        D3DFVF_TEXCOORDSIZE4(1) |
        D3DFVF_TEXCOORDSIZE4(2), 60);

    compare_vertex_sizes (
        D3DFVF_XYZB5 |
        D3DFVF_NORMAL |
        D3DFVF_DIFFUSE |
        D3DFVF_SPECULAR |
        D3DFVF_TEX8 |
        D3DFVF_TEXCOORDSIZE4(0) |
        D3DFVF_TEXCOORDSIZE4(1) |
        D3DFVF_TEXCOORDSIZE4(2) |
        D3DFVF_TEXCOORDSIZE4(3) |
        D3DFVF_TEXCOORDSIZE4(4) |
        D3DFVF_TEXCOORDSIZE4(5) |
        D3DFVF_TEXCOORDSIZE4(6) |
        D3DFVF_TEXCOORDSIZE4(7), 180);
}

static void D3DXIntersectTriTest(void)
{
    BOOL exp_res, got_res;
    D3DXVECTOR3 position, ray, vertex[3];
    FLOAT exp_dist, got_dist, exp_u, got_u, exp_v, got_v;

    vertex[0].x = 1.0f; vertex[0].y = 0.0f; vertex[0].z = 0.0f;
    vertex[1].x = 2.0f; vertex[1].y = 0.0f; vertex[1].z = 0.0f;
    vertex[2].x = 1.0f; vertex[2].y = 1.0f; vertex[2].z = 0.0f;

    position.x = -14.5f; position.y = -23.75f; position.z = -32.0f;

    ray.x = 2.0f; ray.y = 3.0f; ray.z = 4.0f;

    exp_res = TRUE; exp_u = 0.5f; exp_v = 0.25f; exp_dist = 8.0f;

    got_res = D3DXIntersectTri(&vertex[0],&vertex[1],&vertex[2],&position,&ray,&got_u,&got_v,&got_dist);
    ok( got_res == exp_res, "Expected result = %d, got %d\n",exp_res,got_res);
    ok( compare(exp_u,got_u), "Expected u = %f, got %f\n",exp_u,got_u);
    ok( compare(exp_v,got_v), "Expected v = %f, got %f\n",exp_v,got_v);
    ok( compare(exp_dist,got_dist), "Expected distance = %f, got %f\n",exp_dist,got_dist);

/*Only positive ray is taken in account*/

    vertex[0].x = 1.0f; vertex[0].y = 0.0f; vertex[0].z = 0.0f;
    vertex[1].x = 2.0f; vertex[1].y = 0.0f; vertex[1].z = 0.0f;
    vertex[2].x = 1.0f; vertex[2].y = 1.0f; vertex[2].z = 0.0f;

    position.x = 17.5f; position.y = 24.25f; position.z = 32.0f;

    ray.x = 2.0f; ray.y = 3.0f; ray.z = 4.0f;

    exp_res = FALSE;

    got_res = D3DXIntersectTri(&vertex[0],&vertex[1],&vertex[2],&position,&ray,&got_u,&got_v,&got_dist);
    ok( got_res == exp_res, "Expected result = %d, got %d\n",exp_res,got_res);

/*Intersection between ray and triangle in a same plane is considered as empty*/

    vertex[0].x = 4.0f; vertex[0].y = 0.0f; vertex[0].z = 0.0f;
    vertex[1].x = 6.0f; vertex[1].y = 0.0f; vertex[1].z = 0.0f;
    vertex[2].x = 4.0f; vertex[2].y = 2.0f; vertex[2].z = 0.0f;

    position.x = 1.0f; position.y = 1.0f; position.z = 0.0f;

    ray.x = 1.0f; ray.y = 0.0f; ray.z = 0.0f;

    exp_res = FALSE;

    got_res = D3DXIntersectTri(&vertex[0],&vertex[1],&vertex[2],&position,&ray,&got_u,&got_v,&got_dist);
    ok( got_res == exp_res, "Expected result = %d, got %d\n",exp_res,got_res);
}

START_TEST(mesh)
{
    D3DXBoundProbeTest();
    D3DXComputeBoundingBoxTest();
    D3DXComputeBoundingSphereTest();
    D3DXGetFVFVertexSizeTest();
    D3DXIntersectTriTest();
}
