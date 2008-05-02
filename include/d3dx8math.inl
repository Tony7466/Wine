/*
 * Copyright (C) 2007 David Adam
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

#ifndef __D3DX8MATH_INL__
#define __D3DX8MATH_INL__

/*_______________D3DXCOLOR_____________________*/

static inline D3DXCOLOR* D3DXColorAdd(D3DXCOLOR *pout, CONST D3DXCOLOR *pc1, CONST D3DXCOLOR *pc2)
{
    if ( !pout || !pc1 || !pc2 ) return NULL;
    pout->r = (pc1->r) + (pc2->r);
    pout->g = (pc1->g) + (pc2->g);
    pout->b = (pc1->b) + (pc2->b);
    pout->a = (pc1->a) + (pc2->a);
    return pout;
}

static inline D3DXCOLOR* D3DXColorLerp(D3DXCOLOR *pout, CONST D3DXCOLOR *pc1, CONST D3DXCOLOR *pc2, FLOAT s)
{
    if ( !pout || !pc1 || !pc2 ) return NULL;
    pout->r = (1-s) * (pc1->r) + s *(pc2->r);
    pout->g = (1-s) * (pc1->g) + s *(pc2->g);
    pout->b = (1-s) * (pc1->b) + s *(pc2->b);
    pout->a = (1-s) * (pc1->a) + s *(pc2->a);
    return pout;
}

static inline D3DXCOLOR* D3DXColorModulate(D3DXCOLOR *pout, CONST D3DXCOLOR *pc1, CONST D3DXCOLOR *pc2)
{
    if ( !pout || !pc1 || !pc2 ) return NULL;
    pout->r = (pc1->r) * (pc2->r);
    pout->g = (pc1->g) * (pc2->g);
    pout->b = (pc1->b) * (pc2->b);
    pout->a = (pc1->a) * (pc2->a);
    return pout;
}

static inline D3DXCOLOR* D3DXColorNegative(D3DXCOLOR *pout, CONST D3DXCOLOR *pc)
{
    if ( !pout || !pc ) return NULL;
    pout->r = 1.0f - pc->r;
    pout->g = 1.0f - pc->g;
    pout->b = 1.0f - pc->b;
    pout->a = pc->a;
    return pout;
}

static inline D3DXCOLOR* D3DXColorScale(D3DXCOLOR *pout, CONST D3DXCOLOR *pc, FLOAT s)
{
    if ( !pout || !pc ) return NULL;
    pout->r = s* (pc->r);
    pout->g = s* (pc->g);
    pout->b = s* (pc->b);
    pout->a = s* (pc->a);
    return pout;
}

static inline D3DXCOLOR* D3DXColorSubtract(D3DXCOLOR *pout, CONST D3DXCOLOR *pc1, CONST D3DXCOLOR *pc2)
{
    if ( !pout || !pc1 || !pc2 ) return NULL;
    pout->r = (pc1->r) - (pc2->r);
    pout->g = (pc1->g) - (pc2->g);
    pout->b = (pc1->b) - (pc2->b);
    pout->a = (pc1->a) - (pc2->a);
    return pout;
}

/*_______________D3DXVECTOR2________________________*/

static inline D3DXVECTOR2* D3DXVec2Add(D3DXVECTOR2 *pout, CONST D3DXVECTOR2 *pv1, CONST D3DXVECTOR2 *pv2)
{
    if ( !pout || !pv1 || !pv2) return NULL;
    pout->x = pv1->x + pv2->x;
    pout->y = pv1->y + pv2->y;
    return pout;
}

static inline FLOAT D3DXVec2CCW(CONST D3DXVECTOR2 *pv1, CONST D3DXVECTOR2 *pv2)
{
    if ( !pv1 || !pv2) return 0.0f;
    return ( (pv1->x) * (pv2->y) - (pv1->y) * (pv2->x) );
}

static inline FLOAT D3DXVec2Dot(CONST D3DXVECTOR2 *pv1, CONST D3DXVECTOR2 *pv2)
{
    if ( !pv1 || !pv2) return 0.0f;
    return ( (pv1->x * pv2->x + pv1->y * pv2->y) );
}

static inline FLOAT D3DXVec2Length(CONST D3DXVECTOR2 *pv)
{
    if (!pv) return 0.0f;
    return sqrt( (pv->x) * (pv->x) + (pv->y) * (pv->y) );
}

static inline FLOAT D3DXVec2LengthSq(CONST D3DXVECTOR2 *pv)
{
    if (!pv) return 0.0f;
    return( (pv->x) * (pv->x) + (pv->y) * (pv->y) );
}

static inline D3DXVECTOR2* D3DXVec2Lerp(D3DXVECTOR2 *pout, CONST D3DXVECTOR2 *pv1, CONST D3DXVECTOR2 *pv2, FLOAT s)
{
    if ( !pout || !pv1 || !pv2) return NULL;
    pout->x = (1-s) * (pv1->x) + s * (pv2->x);
    pout->y = (1-s) * (pv1->y) + s * (pv2->y);
    return pout;
}

static inline D3DXVECTOR2* D3DXVec2Maximize(D3DXVECTOR2 *pout, CONST D3DXVECTOR2 *pv1, CONST D3DXVECTOR2 *pv2)
{
    if ( !pout || !pv1 || !pv2) return NULL;
    pout->x = max(pv1->x , pv2->x);
    pout->y = max(pv1->y , pv2->y);
    return pout;
}

static inline D3DXVECTOR2* D3DXVec2Minimize(D3DXVECTOR2 *pout, CONST D3DXVECTOR2 *pv1, CONST D3DXVECTOR2 *pv2)
{
    if ( !pout || !pv1 || !pv2) return NULL;
    pout->x = min(pv1->x , pv2->x);
    pout->y = min(pv1->y , pv2->y);
    return pout;
}

static inline D3DXVECTOR2* D3DXVec2Scale(D3DXVECTOR2 *pout, CONST D3DXVECTOR2 *pv, FLOAT s)
{
    if ( !pout || !pv) return NULL;
    pout->x = s * (pv->x);
    pout->y = s * (pv->y);
    return pout;
}

static inline D3DXVECTOR2* D3DXVec2Subtract(D3DXVECTOR2 *pout, CONST D3DXVECTOR2 *pv1, CONST D3DXVECTOR2 *pv2)
{
    if ( !pout || !pv1 || !pv2) return NULL;
    pout->x = pv1->x - pv2->x;
    pout->y = pv1->y - pv2->y;
    return pout;
}

/*__________________D3DXVECTOR3_______________________*/

static inline D3DXVECTOR3* D3DXVec3Add(D3DXVECTOR3 *pout, CONST D3DXVECTOR3 *pv1, CONST D3DXVECTOR3 *pv2)
{
    if ( !pout || !pv1 || !pv2) return NULL;
    pout->x = pv1->x + pv2->x;
    pout->y = pv1->y + pv2->y;
    pout->z = pv1->z + pv2->z;
    return pout;
}

static inline D3DXVECTOR3* D3DXVec3Cross(D3DXVECTOR3 *pout, CONST D3DXVECTOR3 *pv1, CONST D3DXVECTOR3 *pv2)
{
    if ( !pout || !pv1 || !pv2) return NULL;
    pout->x = (pv1->y) * (pv2->z) - (pv1->z) * (pv2->y);
    pout->y = (pv1->z) * (pv2->x) - (pv1->x) * (pv2->z);
    pout->z = (pv1->x) * (pv2->y) - (pv1->y) * (pv2->x);
    return pout;
}

static inline FLOAT D3DXVec3Dot(CONST D3DXVECTOR3 *pv1, CONST D3DXVECTOR3 *pv2)
{
    if ( !pv1 || !pv2 ) return 0.0f;
    return (pv1->x) * (pv2->x) + (pv1->y) * (pv2->y) + (pv1->z) * (pv2->z);
}

static inline FLOAT D3DXVec3Length(CONST D3DXVECTOR3 *pv)
{
    if (!pv) return 0.0f;
    return sqrt( (pv->x) * (pv->x) + (pv->y) * (pv->y) + (pv->z) * (pv->z) );
}

static inline FLOAT D3DXVec3LengthSq(CONST D3DXVECTOR3 *pv)
{
    if (!pv) return 0.0f;
    return (pv->x) * (pv->x) + (pv->y) * (pv->y) + (pv->z) * (pv->z);
}

static inline D3DXVECTOR3* D3DXVec3Lerp(D3DXVECTOR3 *pout, CONST D3DXVECTOR3 *pv1, CONST D3DXVECTOR3 *pv2, FLOAT s)
{
    if ( !pout || !pv1 || !pv2) return NULL;
    pout->x = (1-s) * (pv1->x) + s * (pv2->x);
    pout->y = (1-s) * (pv1->y) + s * (pv2->y);
    pout->z = (1-s) * (pv1->z) + s * (pv2->z);
    return pout;
}

static inline D3DXVECTOR3* D3DXVec3Maximize(D3DXVECTOR3 *pout, CONST D3DXVECTOR3 *pv1, CONST D3DXVECTOR3 *pv2)
{
    if ( !pout || !pv1 || !pv2) return NULL;
    pout->x = max(pv1->x , pv2->x);
    pout->y = max(pv1->y , pv2->y);
    pout->z = max(pv1->z , pv2->z);
    return pout;
}

static inline D3DXVECTOR3* D3DXVec3Minimize(D3DXVECTOR3 *pout, CONST D3DXVECTOR3 *pv1, CONST D3DXVECTOR3 *pv2)
{
    if ( !pout || !pv1 || !pv2) return NULL;
    pout->x = min(pv1->x , pv2->x);
    pout->y = min(pv1->y , pv2->y);
    pout->z = min(pv1->z , pv2->z);
    return pout;
}

static inline D3DXVECTOR3* D3DXVec3Scale(D3DXVECTOR3 *pout, CONST D3DXVECTOR3 *pv, FLOAT s)
{
    if ( !pout || !pv) return NULL;
    pout->x = s * (pv->x);
    pout->y = s * (pv->y);
    pout->z = s * (pv->z);
    return pout;
}

static inline D3DXVECTOR3* D3DXVec3Subtract(D3DXVECTOR3 *pout, CONST D3DXVECTOR3 *pv1, CONST D3DXVECTOR3 *pv2)
{
    if ( !pout || !pv1 || !pv2) return NULL;
    pout->x = pv1->x - pv2->x;
    pout->y = pv1->y - pv2->y;
    pout->z = pv1->z - pv2->z;
    return pout;
}
/*__________________D3DXVECTOR4_______________________*/

static inline D3DXVECTOR4* D3DXVec4Add(D3DXVECTOR4 *pout, CONST D3DXVECTOR4 *pv1, CONST D3DXVECTOR4 *pv2)
{
    if ( !pout || !pv1 || !pv2) return NULL;
    pout->x = pv1->x + pv2->x;
    pout->y = pv1->y + pv2->y;
    pout->z = pv1->z + pv2->z;
    pout->w = pv1->w + pv2->w;
    return pout;
}

static inline FLOAT D3DXVec4Dot(CONST D3DXVECTOR4 *pv1, CONST D3DXVECTOR4 *pv2)
{
    if (!pv1 || !pv2 ) return 0.0f;
    return (pv1->x) * (pv2->x) + (pv1->y) * (pv2->y) + (pv1->z) * (pv2->z) + (pv1->w) * (pv2->w);
}

static inline FLOAT D3DXVec4Length(CONST D3DXVECTOR4 *pv)
{
    if (!pv) return 0.0f;
    return sqrt( (pv->x) * (pv->x) + (pv->y) * (pv->y) + (pv->z) * (pv->z) + (pv->w) * (pv->w) );
}

static inline FLOAT D3DXVec4LengthSq(CONST D3DXVECTOR4 *pv)
{
    if (!pv) return 0.0f;
    return (pv->x) * (pv->x) + (pv->y) * (pv->y) + (pv->z) * (pv->z) + (pv->w) * (pv->w);
}

static inline D3DXVECTOR4* D3DXVec4Lerp(D3DXVECTOR4 *pout, CONST D3DXVECTOR4 *pv1, CONST D3DXVECTOR4 *pv2, FLOAT s)
{
    if ( !pout || !pv1 || !pv2) return NULL;
    pout->x = (1-s) * (pv1->x) + s * (pv2->x);
    pout->y = (1-s) * (pv1->y) + s * (pv2->y);
    pout->z = (1-s) * (pv1->z) + s * (pv2->z);
    pout->w = (1-s) * (pv1->w) + s * (pv2->w);
    return pout;
}


static inline D3DXVECTOR4* D3DXVec4Maximize(D3DXVECTOR4 *pout, CONST D3DXVECTOR4 *pv1, CONST D3DXVECTOR4 *pv2)
{
    if ( !pout || !pv1 || !pv2) return NULL;
    pout->x = max(pv1->x , pv2->x);
    pout->y = max(pv1->y , pv2->y);
    pout->z = max(pv1->z , pv2->z);
    pout->w = max(pv1->w , pv2->w);
    return pout;
}

static inline D3DXVECTOR4* D3DXVec4Minimize(D3DXVECTOR4 *pout, CONST D3DXVECTOR4 *pv1, CONST D3DXVECTOR4 *pv2)
{
    if ( !pout || !pv1 || !pv2) return NULL;
    pout->x = min(pv1->x , pv2->x);
    pout->y = min(pv1->y , pv2->y);
    pout->z = min(pv1->z , pv2->z);
    pout->w = min(pv1->w , pv2->w);
    return pout;
}

static inline D3DXVECTOR4* D3DXVec4Scale(D3DXVECTOR4 *pout, CONST D3DXVECTOR4 *pv, FLOAT s)
{
    if ( !pout || !pv) return NULL;
    pout->x = s * (pv->x);
    pout->y = s * (pv->y);
    pout->z = s * (pv->z);
    pout->w = s * (pv->w);
    return pout;
}

static inline D3DXVECTOR4* D3DXVec4Subtract(D3DXVECTOR4 *pout, CONST D3DXVECTOR4 *pv1, CONST D3DXVECTOR4 *pv2)
{
    if ( !pout || !pv1 || !pv2) return NULL;
    pout->x = pv1->x - pv2->x;
    pout->y = pv1->y - pv2->y;
    pout->z = pv1->z - pv2->z;
    pout->w = pv1->w - pv2->w;
    return pout;
}

/*__________________D3DXMatrix____________________*/

static inline D3DXMATRIX* D3DXMatrixIdentity(D3DXMATRIX *pout)
{
    if ( !pout ) return NULL;
    pout->m[0][1] = 0.0f;
    pout->m[0][2] = 0.0f;
    pout->m[0][3] = 0.0f;
    pout->m[1][0] = 0.0f;
    pout->m[1][2] = 0.0f;
    pout->m[1][3] = 0.0f;
    pout->m[2][0] = 0.0f;
    pout->m[2][1] = 0.0f;
    pout->m[2][3] = 0.0f;
    pout->m[3][0] = 0.0f;
    pout->m[3][1] = 0.0f;
    pout->m[3][2] = 0.0f;
    pout->m[0][0] = 1.0f;
    pout->m[1][1] = 1.0f;
    pout->m[2][2] = 1.0f;
    pout->m[3][3] = 1.0f;
    return pout;
}

static inline BOOL D3DXMatrixIsIdentity(D3DXMATRIX *pm)
{
    int i,j;
    D3DXMATRIX testmatrix;
    BOOL equal=TRUE;

    if ( !pm ) return FALSE;
    D3DXMatrixIdentity(&testmatrix);
    for (i=0; i<4; i++)
    {
     for (j=0; j<4; j++)
     {
      if ( fabs(pm->m[i][j] - testmatrix.m[i][j]) > 0.0001 ) equal = FALSE;
     }
    }
    return equal;
}

/*__________________D3DXPLANE____________________*/

static inline FLOAT D3DXPlaneDot(CONST D3DXPLANE *pp, CONST D3DXVECTOR4 *pv)
{
    if ( !pp || !pv ) return 0.0f;
    return ( (pp->a) * (pv->x) + (pp->b) * (pv->y) + (pp->c) * (pv->z) + (pp->d) * (pv->w) );
}

static inline FLOAT D3DXPlaneDotCoord(CONST D3DXPLANE *pp, CONST D3DXVECTOR4 *pv)
{
    if ( !pp || !pv ) return 0.0f;
    return ( (pp->a) * (pv->x) + (pp->b) * (pv->y) + (pp->c) * (pv->z) + (pp->d) );
}

static inline FLOAT D3DXPlaneDotNormal(CONST D3DXPLANE *pp, CONST D3DXVECTOR4 *pv)
{
    if ( !pp || !pv ) return 0.0f;
    return ( (pp->a) * (pv->x) + (pp->b) * (pv->y) + (pp->c) * (pv->z) );
}

/*__________________D3DXQUATERNION____________________*/

static inline D3DXQUATERNION* D3DXQuaternionConjugate(D3DXQUATERNION *pout, CONST D3DXQUATERNION *pq)
{
    if ( !pout || !pq) return NULL;
    pout->x = -pq->x;
    pout->y = -pq->y;
    pout->z = -pq->z;
    pout->w = pq->w;
    return pout;
}

static inline FLOAT D3DXQuaternionDot(CONST D3DXQUATERNION *pq1, CONST D3DXQUATERNION *pq2)
{
    if ( !pq1 || !pq2 ) return 0.0f;
    return (pq1->x) * (pq2->x) + (pq1->y) * (pq2->y) + (pq1->z) * (pq2->z) + (pq1->w) * (pq2->w);
}

static inline D3DXQUATERNION* D3DXQuaternionIdentity(D3DXQUATERNION *pout)
{
    if ( !pout) return NULL;
    pout->x = 0.0f;
    pout->y = 0.0f;
    pout->z = 0.0f;
    pout->w = 1.0f;
    return pout;
}

static inline BOOL D3DXQuaternionIsIdentity(D3DXQUATERNION *pq)
{
    if ( !pq) return FALSE;
    return ( (pq->x == 0.0f) && (pq->y == 0.0f) && (pq->z == 0.0f) && (pq->w == 1.0f) );
}

static inline FLOAT D3DXQuaternionLength(CONST D3DXQUATERNION *pq)
{
    if (!pq) return 0.0f;
    return sqrt( (pq->x) * (pq->x) + (pq->y) * (pq->y) + (pq->z) * (pq->z) + (pq->w) * (pq->w) );
}

static inline FLOAT D3DXQuaternionLengthSq(CONST D3DXQUATERNION *pq)
{
    if (!pq) return 0.0f;
    return (pq->x) * (pq->x) + (pq->y) * (pq->y) + (pq->z) * (pq->z) + (pq->w) * (pq->w);
}

#endif
