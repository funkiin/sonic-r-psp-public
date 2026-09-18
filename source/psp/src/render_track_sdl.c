/**
 * render_track_sdl.c — SDL/desktop RenderTrackD3D (0x004533C4)
 *
 * Split out of render_track_d3d.c so it is obvious which body is live on which
 * platform. This is the faithful integer translation: integer view matrix
 * (g_viewMtx00..22), integer camera position (g_camIntX/Y/Z), and g_sinTable /
 * g_cosTable lookups — the same transform the software renderer, weather
 * particles, and pickup ground shadows use.
 *
 * The DC counterpart is dc/src/render_track_dc.c (float matrix + FIPR).
 * Anything that is about the TRANSLATION rather than the arithmetic
 * representation must be changed in both. Shared helpers live in
 * render_track_internal.h.
 *
 * Excluded from the DC build via SDL_REPLACED in dc/Makefile.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include <math.h>
#include "r_types.h"
#include "r_state.h"
#include "r_draw.h"
#include "nearclip.h"
#include "render_track_internal.h"

extern void RenderHiddenSubEntry(int worldX, int worldZ, int worldY);

/* Build one RenderVertex from a near-clip vertex (track format: fog + color tint). */
static RenderVertex TrackClipEmitVert(const NearClipVert *v, float invFarSafe) {
    RenderVertex rv;
    int projZ = v->depth;
    if (projZ < 1) {
        projZ = 1;
    }

    NearClipVert_ProjectFloat(v->camX, v->camY, v->depth > 0 ? v->depth : 1, &rv.sx, &rv.sy);
    rv.sz = (float)projZ * invFarSafe;
    rv.rhw = reciprocal((float)projZ);

    int r = v->colorR >> 13;
    int g = v->colorG >> 13;
    int b = v->colorB >> 13;
    if (g_colorTintEnable) {
        r += g_colorTintR; 
        g += g_colorTintG; 
        b += g_colorTintB; 
    }
    if (r > 255) {
        r = 255;
    }
    if (g > 255) {
        g = 255;
    }
    if (b > 255) {
        b = 255;
    }
    /* Distance fog ramp — same shape as SubmitTrackPoly above. */
    int fogAlpha;
    sr_double fogD = (sr_double)projZ * (sr_double)invFarSafe;
    if (fogD > FOG_FAR) {
        fogAlpha = 0;
    }
    else if (fogD <= FOG_NEAR) {
        fogAlpha = 0xFF;
    }
    else {
        int raw = (int)((fogD + FOG_NEG_FAR) * FOG_MUL);
        fogAlpha = SRABS(raw);
    }

    rv.color = (unsigned int)fogAlpha << 24 |
               (unsigned int)r << 16 |
               (unsigned int)g << 8 |
               (unsigned int)b;
    rv.specular = 0;
    rv.u = v->u;
    rv.v = v->v;
    return rv;
}

/* Clip a 4-vertex convex quad against the near plane (z=1) as a single
 * polygon (Sutherland-Hodgman) and emit as a triangle fan. Compared to
 * splitting into two triangles up front this saves one R_SetTexture call
 * and avoids re-clipping the shared diagonal edge. */
static int TrackClipAndEmitQuad(const NearClipVert v[4], int tpage, float farSafe,
                                unsigned char flags) {
    /* Binary 0x451504. Explicit case analysis rather than a generic
     * Sutherland-Hodgman walk, so the resulting fan is triangulated the same
     * way the original triangulates it:
     *
     *   one behind (k)      → pentagon, three triangles fanned from v[k+2]
     *   two adjacent (k,k+1)→ quad, two triangles fanned from the first lerp
     *   two diagonal        → nothing (a convex quad cannot straddle that way)
     *   three behind (m in front) → single triangle (A, v[m], B)
     *
     * NearClipLerp takes (behind, front), matching the binary's operand order
     * where the denominator is front.depth - behind.depth. */
    NearClipVert poly[5];
    NearClipVert A;
    NearClipVert B;
    int behind[4];
    int count = 0;
    int n = 0;

    for (int i = 0; i < 4; i++) {
        behind[i] = (v[i].depth < 1);
        count += behind[i];
    }

    if (count == 4) {
        return 0;
    }

    if (count == 0) {
        for (int i = 0; i < 4; i++) {
            poly[i] = v[i];
        }
        n = 4;
    }
    else if (count == 1) {
        int k = behind[0] ? 0 : behind[1] ? 1 : behind[2] ? 2 : 3;
        int nx = (k + 1) & 3;
        int pr = (k + 3) & 3;
        int opp = (k + 2) & 3;

        NearClipLerp(&v[k], &v[nx], &A);
        NearClipLerp(&v[k], &v[pr], &B);

        /* Binary emits (opp,A,nx), (opp,B,A), (opp,pr,B) — as a fan from opp
         * that is the perimeter order below. */
        poly[0] = v[opp];
        poly[1] = v[pr];
        poly[2] = B;
        poly[3] = A;
        poly[4] = v[nx];
        n = 5;
    }
    else if (count == 2) {
        int k = -1;
        for (int i = 0; i < 4; i++) {
            if (behind[i] && behind[(i + 1) & 3]) {
                k = i;
                break;
            }
        }
        if (k < 0) {
            return 0;              /* diagonal pair — binary 0x451E08 draws nothing */
        }
        int a2 = (k + 2) & 3;
        int a3 = (k + 3) & 3;

        NearClipLerp(&v[(k + 1) & 3], &v[a2], &A);
        NearClipLerp(&v[k],           &v[a3], &B);

        poly[0] = A;
        poly[1] = v[a2];
        poly[2] = v[a3];
        poly[3] = B;
        n = 4;
    }
    else {
        int m  = !behind[0] ? 0 : !behind[1] ? 1 : !behind[2] ? 2 : 3;
        int pr = (m + 3) & 3;
        int nx = (m + 1) & 3;

        NearClipLerp(&v[pr], &v[m], &A);
        NearClipLerp(&v[nx], &v[m], &B);

        poly[0] = A;
        poly[1] = v[m];
        poly[2] = B;
        n = 3;
    }

    /* Backface cull AFTER clipping, on the clipped screen positions — the
     * binary culls at 0x4519EB and its siblings, past the lerp calls. Any three
     * consecutive vertices of a convex polygon share its orientation, so the
     * first three stand in for the binary's specific (A, pivot, B) triple.
     * Doing it here is also what lets the pre-clip camera-space fallback go:
     * every vertex is in front of the near plane by this point. */
    if ((flags & 4) == 0) {
        if (TrackBackfaceCrossM(poly[0].screenY, poly[1].screenY,
                                poly[2].screenX, poly[1].screenX,
                                poly[0].screenX, poly[2].screenY) < 0) {
            return 0;
        }
    }

    R_SetTexture(tpage);
    R_SetTexEnv(R_TEXENV_ADD_SIGNED);

    RenderVertex rv[5];
    float invFarSafe = reciprocal(farSafe);
    for (int i = 0; i < n; i++) {
        rv[i] = TrackClipEmitVert(&poly[i], invFarSafe);
    }
    R_DrawTriFan(rv, n);
    return n - 2;
}

/* Clip triangle cv[0..2] against near plane and emit via immediate mode.
 * Returns number of triangles emitted (0, 1, or 2). */
static int TrackClipAndEmitTri(NearClipVert cv[3], int tpage, float farSafe,
                               unsigned char flags) {
    /* Binary 0x452514. The case shapes below already matched the original —
     * one-behind fans exactly as the binary's (A,v1,v2),(A,v2,B) pair, and
     * two-behind is the same triangle up to rotation. What was missing is the
     * cull, which the binary runs after clipping rather than before. */
    NearClipVert poly[4];
    int b0 = (cv[0].depth < 1);
    int b1 = (cv[1].depth < 1);
    int b2 = (cv[2].depth < 1);
    int behindCount = b0 + b1 + b2;
    int n;

    if (behindCount == 3) {
        return 0;
    }

    if (behindCount == 0) {
        poly[0] = cv[0];
        poly[1] = cv[1];
        poly[2] = cv[2];
        n = 3;
    }
    else if (behindCount == 1) {
        if (b1) {
            NearClipRotate(cv, 1);
        }
        else if (b2) {
            NearClipRotate(cv, 2);
        }

        NearClipLerp(&cv[0], &cv[1], &poly[0]);   /* A */
        poly[1] = cv[1];
        poly[2] = cv[2];
        NearClipLerp(&cv[0], &cv[2], &poly[3]);   /* B */
        n = 4;
    }
    else {
        /* Two behind — rotate so cv[0] is in front */
        if (!b0) {
            /* already in front */
        }
        else if (!b1) {
            NearClipRotate(cv, 1);
        }
        else {
            NearClipRotate(cv, 2);
        }

        poly[0] = cv[0];
        NearClipLerp(&cv[1], &cv[0], &poly[1]);
        NearClipLerp(&cv[2], &cv[0], &poly[2]);
        n = 3;
    }

    /* Cull after clipping. Note the asymmetry: unlike the quad clip submit,
     * the binary's triangle clip submit has NO mirror-mode handling — 0x6E9920
     * has zero references inside 0x452514 — so this uses the unmirrored form
     * deliberately. */
    if ((flags & 4) == 0) {
        if (TrackBackfaceCross(poly[0].screenY, poly[1].screenY,
                               poly[2].screenX, poly[1].screenX,
                               poly[0].screenX, poly[2].screenY) < 0) {
            return 0;
        }
    }

    R_SetTexture(tpage);
    R_SetTexEnv(R_TEXENV_ADD_SIGNED);

    RenderVertex rv[4];
    float invFarSafe = reciprocal(farSafe);
    for (int i = 0; i < n; i++) {
        rv[i] = TrackClipEmitVert(&poly[i], invFarSafe);
    }
    if (n == 3) {
        R_DrawTri(rv);
        return 1;
    }
    R_DrawQuad(rv);
    return 2;
}

/**
 * RenderTrackD3D — 0x004533C4 — 6316 bytes
 *
 * Faithful translation of the D3D track/object renderer.
 * Uses integer view matrix (g_viewMtx00..22) and integer camera
 * position (g_camIntX/Y/Z).
 */
void RenderTrackD3D(void)
{
    int remaining = g_totalObjects;   /* 0x6EAD30 */
    int *obj = (int *)g_objectStructArray;  /* 0x712D44 */
    float farClipF = g_farClipFloat;  /* 0x8FB364 */

    if (remaining <= 0) {
        return;
    }

    if (g_vertexArrayBase == NULL || g_polygonArrayBase == NULL) {
        return;
    }

    int farClipThresh = g_farClipDepth; /* 0x8FB35C */
    if (farClipThresh <= 0) {
        farClipThresh = 0x2B00; /* default: 88064 >> 3 = 11008 = 0x2B00 */
    }

    int farClipTimes8 = g_farClipDepth << 3;
    if (farClipTimes8 <= 0) {
        farClipTimes8 = 0x15800;
    }
    float farSafe = (farClipF > 0.0f) ? farClipF : 88064.0f;
    float invFarSafe = reciprocal(farSafe);

    do {
        /* Object visibility check
         * obj+0x2A >> 16: bounding sphere radius (-1 = hidden)
         * obj+0x2C >> 16: mode (0=track part, 2=decoration) */
        int visFlag = *(short *)((char *)obj + 0x2C);       /* EAX at 0x4533F0 */

        if (visFlag == -1) {
            /* Hidden objects have no geometry — skip to next_object.
             * Sub-entry (ring billboard) rendering handled at next_object for all objects. */
            goto next_object;
        }

        int mode = *(short *)((char *)obj + 0x2E);
        int dx;
        int dy;
        int dz;  /* camera-relative position */

        /* Compute camera-relative object origin */
        if (mode == 2) {
            /* Decoration: use pivot position at obj[8..10] */
            dx = obj[8]  - g_camIntX;
            dy = obj[9]  - g_camIntY;
            dz = obj[10] - g_camIntZ;
        }
        else {
            /* Track part: use world position at obj[0..2] */
            dx = obj[0] - g_camIntX;
            dy = obj[1] - g_camIntY;
            dz = obj[2] - g_camIntZ;
        }

        /* Camera-space Z (depth) of object origin
         * cz = (dx * m02 + dy * m12 + dz * m22) / 4096 */
        int cz = (dx * g_viewMtx02 + dy * g_viewMtx12 + dz * g_viewMtx22) / 4096;

        /* Object-level depth culling
         * Binary: (cz - boundSphere) / 8 > farClipDepth → skip
         *         cz < -boundSphere → skip (behind camera) */
        int boundSphere = visFlag;  /* obj+0x2A >> 16 */
        if (((cz - boundSphere) >> 3) > farClipThresh) {
            goto next_object;
        }
        if (cz < -boundSphere) {
            goto next_object;
        }
        if (cz < 1) {
            cz = 1;  /* prevent divide-by-zero */
        }

        int absDX = dx < 0 ? -dx : dx;
        int absDY = dy < 0 ? -dy : dy;
        int absDZ = dz < 0 ? -dz : dz;
        if ((absDX + boundSphere) > (g_farClipDepth << 4) &&
            (absDY + boundSphere) > (g_farClipDepth << 4) &&
            (absDZ + boundSphere) > (g_farClipDepth << 4)) {
            goto next_object;
        }

        /* Camera-space X of object origin
         * cx = (dx * m00 + dy * m10 + dz * m20) / 4096 */
        int cx = (dx * g_viewMtx00 + dy * g_viewMtx10 + dz * g_viewMtx20) / 4096;

        /* Project object origin to screen X */
        int originScrX = g_screenCenterX + (g_projScaleXCurrent * cx) / cz;

        /* Projected bounding sphere for frustum cull */
        int projBound = (g_projScaleXCurrent * boundSphere) / cz;

        /* Frustum cull X: object must overlap clip rect */
        if (originScrX - projBound > g_clipRight + 32) {
            goto next_object;
        }
        if (originScrX + projBound < g_clipLeft - 32) {
            goto next_object;
        }

        /* Camera-space Y of object origin
         * cy = (dx * m01 + dy * m11 + dz * m21) / 4096 */
        int cy = (dx * g_viewMtx01 + dy * g_viewMtx11 + dz * g_viewMtx21) / 4096;

        /* Project object origin to screen Y */
        int originScrY = g_screenCenterY - (g_projScaleY * cy) / cz;

        /* Frustum cull Y  */
        if (originScrY - projBound > g_clipBottom + 32) {
            goto next_object;
        }
        if (originScrY + projBound < g_clipTop - 32) {
            goto next_object;
        }

        /* Get vertex base pointer */
        unsigned int vtxStartIdx = *(unsigned short *)((char *)obj + 0x38);
        SrcVertex *vtxBase = &g_vertexArrayBase[vtxStartIdx];

        unsigned int vertCount = *(unsigned short *)((char *)obj + 0x3A);
        if (vertCount == 0) {
            goto next_object;
        }

        if (originScrX + projBound < g_clipLeft - 64 ||
            originScrX - projBound > g_clipRight + 64 ||
            originScrY + projBound < g_clipTop - 64 ||
            originScrY - projBound > g_clipBottom + 64) {
            goto next_object;
        }

        g_processedObjectCount++;
        /* Per-vertex transform */

        if (mode == 2) {
            /* MODE 2: Decoration with rotation
             * Binary 0x45360C-0x453A39 (1069 bytes).
             * Builds Euler rotation matrix from yaw/pitch/roll,
             * applies local rotation per vertex, adds pivot, subtracts
             * camera, then transforms through view matrix. */

            /* Extract angles (0x453612-0x453630) */
            int yaw   = *(short *)((char *)obj + 0x18);       /* 0x453617 */
            int pitch = *(short *)((char *)obj + 0x1A);      /* 0x453620 */
            int roll  = *(short *)((char *)obj + 0x1C);      /* 0x45362A */

            /* Trig lookups (0x453623-0x4536DA)
             * sinY/cosY/sinR/cosR are >>2; sinP/cosP get >>2 at stage 3 */
            int sinY  = g_sinTable[yaw] >> 2;      /* 0x45362D: EAX */
            int cosY  = g_cosTable[yaw] >> 2;      /* 0x453642: EAX */
            int cosR  = g_cosTable[roll] >> 2;      /* 0x45365F: ESI */
            int sinR  = g_sinTable[roll] >> 2;      /* 0x4536DA: ECX */
            int sinP2 = g_sinTable[pitch] >> 2;     /* 0x453801: sinP >> 2 */
            int cosP2 = g_cosTable[pitch] >> 2;     /* 0x453807: cosP >> 2 */

            /* yaw + roll combination (0x45366E-0x45381C).
             * Binary's imul-by-0 terms zero out all sinP cross-products
             * in stage 1, leaving a pure yaw matrix that roll is applied to.
             * Intermediate values used later: */
            int cYsR = (-(cosY * sinR)) >> 12;     /* [ebp-0x7c] after stage 2 */
            int sRsY = (sinR * sinY) >> 12;         /* [ebp-0x70] after stage 2 */

            /* Final 3×3 local rotation matrix (0x45388F-0x4538CE):
             *   r0 = L00*vx + L01*vy + L02*vz
             *   r1 = L10*vx + L11*vy + L12*vz
             *   r2 = L20*vx + L21*vy + L22*vz  */
            int L00 = (cosR * cosP2) >> 12;                           /* [ebp-0xb4] */
            int L01 = (cYsR * cosP2 + sinY * sinP2) >> 12;           /* [ebp-0x7c] */
            int L02 = (cosY * sinP2 + sRsY * cosP2) >> 12;           /* [ebp-0x70] */
            int L10 = sinR;                                            /* [ebp-0x84] */
            int L11 = (cosY * cosR) >> 12;                            /* [ebp-0x78] */
            int L12 = (-(sinY * cosR)) >> 12;                         /* [ebp-0x6c] */
            int L20 = (-(cosR * sinP2)) >> 12;                        /* [ebp-0x80] */
            int L21 = (sinY * cosP2 - cYsR * sinP2) >> 12;           /* [ebp-0x74] */
            int L22 = (cosP2 * cosY - sinP2 * sRsY) >> 12;           /* [ebp-0x68] */

            /* Pivot position */
            int pivX = obj[8], pivY = obj[9], pivZ = obj[10];

            /* Per-vertex transform (0x4538D4-0x453A35) */
            SrcVertex *vtx = vtxBase;
            for (unsigned int vi = 0; vi < vertCount; vi++) {
                int vx = vtx->posX;  /* +0x14: local X */
                int vy = vtx->posY;  /* +0x18: local Y */
                int vz = vtx->posZ;  /* +0x1C: local Z */

                /* Local rotation (0x4538D4-0x453960) */
                int r0 = (L00 * vx + L01 * vy + L02 * vz) >> 12;
                int r1 = (L10 * vx + L11 * vy + L12 * vz) >> 12;
                int r2 = (L20 * vx + L21 * vy + L22 * vz) >> 12;

                /* Rotated vertex + pivot - camera (0x45393A-0x453973) */
                int dx = r0 + pivX - g_camIntX;
                int dy = r1 + pivY - g_camIntY;
                int dz = r2 + pivZ - g_camIntZ;

                /* View matrix to camera space (0x45396D-0x4539E2)
                 * camSX drops g_viewMtx10*dy (same as mode 0) */
                int camSX = (g_viewMtx00 * dx + g_viewMtx20 * dz) >> 12;
                int camSY = (g_viewMtx01 * dx + g_viewMtx11 * dy + g_viewMtx21 * dz) >> 12;
                int camSZ = (g_viewMtx02 * dx + g_viewMtx12 * dy + g_viewMtx22 * dz) >> 12;

                vtx->camX = camSX;
                vtx->camY = camSY;
                vtx->depth = camSZ;

                /* Perspective projection (0x4539E5-0x453A22) */
                if (camSZ > 0) {
                    vtx->screenX = g_screenCenterX + (g_projScaleXCurrent * camSX) / camSZ;
                    vtx->screenY = g_screenCenterY - (g_projScaleY * camSY) / camSZ;
                }

                vtx++;
            }
        }
        else {
            /* MODE 0: Track part (no rotation)
             * Each vertex: subtract camera, transform through view matrix,
             * project to screen. Binary drops g_viewMtx10*dy from X transform
             * (optimization: m10 is typically 0 for non-banked cameras). */
            SrcVertex *vtx = vtxBase;
            for (unsigned int vi = 0; vi < vertCount; vi++) {
                int vdx = vtx->posX - g_camIntX;
                int vdy = vtx->posY - g_camIntY;
                int vdz = vtx->posZ - g_camIntZ;

                /* Camera-space X (binary drops m10*dy term) */
                int camSX = (g_viewMtx00 * vdx + g_viewMtx20 * vdz) >> 12;
                /* Camera-space Y (all 3 terms) */
                int camSY = (g_viewMtx01 * vdx + g_viewMtx11 * vdy + g_viewMtx21 * vdz) >> 12;
                /* Camera-space Z / depth (all 3 terms) */
                int camSZ = (g_viewMtx02 * vdx + g_viewMtx12 * vdy + g_viewMtx22 * vdz) >> 12;

                vtx->camX = camSX;
                vtx->camY = camSY;
                vtx->depth = camSZ;

                /* Project to screen */
                if (camSZ > 0) {
                    vtx->screenX = g_screenCenterX + (g_projScaleXCurrent * camSX) / camSZ;
                    vtx->screenY = g_screenCenterY - (g_projScaleY * camSY) / camSZ;
                }

                vtx++;
            }
        }

        /* 0x453B47: Radiant Emerald recolours this object's transformed
         * vertices before the poly loop. */
        if (g_trackId == TRACK_RADIANT_EMERALD) {
            TrackEmeraldRecolourObject(vtxBase, vertCount);
        }

        /* Polygon iteration
         * Iterate this object's polygons, do backface cull + clip test,
         * submit to tpage batches. Translated from 0x451504 pattern
         * (the original calls inner functions 0x450f44 / 0x451198). */
        unsigned int polyCount = *(unsigned short *)((char *)obj + 0x32);
        if (polyCount == 0) {
            goto next_object;
        }

        unsigned int polyStartIdx = *(unsigned short *)((char *)obj + 0x30);
        int *polyPtr = (int *)((char *)g_polygonArrayBase + polyStartIdx * 0x30);

        for (unsigned int pi = 0; pi < polyCount; pi++) {
            char *pp = (char *)polyPtr;

            /* Check tpage ready */
            if (g_tpageStateArray[*(unsigned char *)(pp + 0x28)] != 0x04) {
                goto next_poly;
            }

            /* Get vertex pointers (absolute indices from polygon struct) */
            SrcVertex *pv0 = &g_vertexArrayBase[*(unsigned short *)(pp + 0x20)];
            SrcVertex *pv1 = &g_vertexArrayBase[*(unsigned short *)(pp + 0x22)];
            SrcVertex *pv2 = &g_vertexArrayBase[*(unsigned short *)(pp + 0x24)];

            /* Far clip — binary 0x453280/0x45329c/0x4532b6 tests each vertex
             * independently and skips the poly if ANY one is beyond the far
             * plane. Quads test their 4th vertex below. */
            if (pv0->depth > farClipTimes8 || pv1->depth > farClipTimes8 ||
                pv2->depth > farClipTimes8)
            {
                goto next_poly;
            }

            if (pv0->screenX < g_clipLeft - 16 && pv1->screenX < g_clipLeft - 16 &&
                pv2->screenX < g_clipLeft - 16 && pv0->screenY < g_clipTop - 16 &&
                pv1->screenY < g_clipTop - 16 && pv2->screenY < g_clipTop - 16) {
                goto next_poly;
            }
            if (pv0->screenX > g_clipRight + 16 && pv1->screenX > g_clipRight + 16 &&
                pv2->screenX > g_clipRight + 16 && pv0->screenY > g_clipBottom + 16 &&
                pv1->screenY > g_clipBottom + 16 && pv2->screenY > g_clipBottom + 16) {
                goto next_poly;
            }

            unsigned char flags = *(unsigned char *)(pp + 0x2E);
            int tpage = *(unsigned char *)(pp + 0x28);

            /* The binary dispatches per polygon on how many vertices sit behind
             * the near plane: all of them → skip, none → an inline fast path
             * (0x453CCF quad / 0x45462F tri), otherwise the clip submit. We used
             * to route everything through the clip path, which meant every
             * polygon in the game paid for clipping it did not need and neither
             * the viewport reject nor the mirror-mode cull ever ran.
             *
             * The fast path below deliberately keeps our float re-projection
             * (TrackClipEmitVert) rather than the binary's integer screenX/screenY.
             * That is the sub-pixel change from b9ee087; going back to integers
             * reintroduces the vertical shift against the grid, the near-plane
             * seams, and on DC the integer-depth rhw problem. */

            /* Triangle */
            if ((flags & 1) == 0) {
                int behind = (pv0->depth < 1) + (pv1->depth < 1) + (pv2->depth < 1);
                if (behind == 3) {
                    goto next_poly;
                }

                {
                    SrcVertex *tri[3] = { pv0, pv1, pv2 };
                    if (TrackViewportReject(tri, 3)) {
                        goto next_poly;
                    }
                }

                NearClipVert cv[3];

                if (behind == 0) {
                    /* Fast path — 0x45462F */
                    if ((flags & 4) == 0) {
                        if (TrackBackfaceCrossM(pv0->screenY, pv1->screenY,
                                                pv2->screenX, pv1->screenX,
                                                pv0->screenX, pv2->screenY) < 0) 
                        {
                            goto next_poly;
                        }
                    }

                    NearClipFillVertTrack(&cv[0], pv0, polyPtr, 0);
                    NearClipFillVertTrack(&cv[1], pv1, polyPtr, 1);
                    NearClipFillVertTrack(&cv[2], pv2, polyPtr, 2);

                    R_SetTexture(tpage);
                    R_SetTexEnv(R_TEXENV_ADD_SIGNED);

                    RenderVertex rv[3];
                    rv[0] = TrackClipEmitVert(&cv[0], invFarSafe);
                    rv[1] = TrackClipEmitVert(&cv[1], invFarSafe);
                    rv[2] = TrackClipEmitVert(&cv[2], invFarSafe);
                    R_DrawTri(rv);
                    goto next_poly;
                }

                /* Clip path — cull happens inside, after clipping. UVs come from
                 * the same LUT as the fast path above; the binary used two flat
                 * conversions here instead — see NearClipFillVert. */
                NearClipFillVertTrack(&cv[0], pv0, polyPtr, 0);
                NearClipFillVertTrack(&cv[1], pv1, polyPtr, 1);
                NearClipFillVertTrack(&cv[2], pv2, polyPtr, 2);
                TrackClipAndEmitTri(cv, tpage, farSafe, flags);
            }
            /* Quad */
            else {
                SrcVertex *pv3 = &g_vertexArrayBase[*(unsigned short *)(pp + 0x26)];

                /* Binary 0x4532d0 — 4th vertex, gated on flags&1. Verts 0-2
                 * were already tested above. */
                if (pv3->depth > farClipTimes8) {
                    goto next_poly;
                }

                int behind = (pv0->depth < 1) + (pv1->depth < 1) +
                             (pv2->depth < 1) + (pv3->depth < 1);
                if (behind == 4) {
                    goto next_poly;
                }

                {
                    SrcVertex *quad[4] = { pv0, pv1, pv2, pv3 };
                    if (TrackViewportReject(quad, 4)) {
                        goto next_poly;
                    }
                }

                NearClipVert cv[4];

                if (behind == 0) {
                    /* Fast path — 0x453CCF. flags&2 selects the two-triangle
                     * variant, which retries the cull on (v0,v2,v3) before
                     * giving up. */
                    if ((flags & 4) == 0) {
                        int32_t cross = TrackBackfaceCrossM(pv0->screenY, pv1->screenY,
                                                            pv2->screenX, pv1->screenX,
                                                            pv0->screenX, pv2->screenY);
                        if ((flags & 2) == 0) {
                            if (cross < 0) {
                                goto next_poly;
                            }
                        }
                        else if (cross < 0) {
                            if (TrackBackfaceCrossM(pv0->screenY, pv2->screenY,
                                                    pv3->screenX, pv2->screenX,
                                                    pv0->screenX, pv3->screenY) < 0)
                            {
                                goto next_poly;
                            }
                        }
                    }

                    SrcVertex *quad[4];
                    quad[0] = pv0;
                    quad[1] = pv1;
                    quad[2] = pv2;
                    quad[3] = pv3;
                    if (TrackViewportReject(quad, 4)) {
                        goto next_poly;
                    }

                    NearClipFillVertTrack(&cv[0], pv0, polyPtr, 0);
                    NearClipFillVertTrack(&cv[1], pv1, polyPtr, 1);
                    NearClipFillVertTrack(&cv[2], pv2, polyPtr, 2);
                    NearClipFillVertTrack(&cv[3], pv3, polyPtr, 3);

                    R_SetTexture(tpage);
                    R_SetTexEnv(R_TEXENV_ADD_SIGNED);

                    RenderVertex rv[4];
                    rv[0] = TrackClipEmitVert(&cv[0], invFarSafe);
                    rv[1] = TrackClipEmitVert(&cv[1], invFarSafe);
                    rv[2] = TrackClipEmitVert(&cv[2], invFarSafe);
                    rv[3] = TrackClipEmitVert(&cv[3], invFarSafe);
                    R_DrawQuad(rv);
                    goto next_poly;
                }

                /* Clip path — cull happens inside, after clipping. UVs come from
                 * the same LUT as the fast path above; the binary used two flat
                 * conversions here instead — see NearClipFillVert. */
                NearClipFillVertTrack(&cv[0], pv0, polyPtr, 0);
                NearClipFillVertTrack(&cv[1], pv1, polyPtr, 1);
                NearClipFillVertTrack(&cv[2], pv2, polyPtr, 2);
                NearClipFillVertTrack(&cv[3], pv3, polyPtr, 3);

                TrackClipAndEmitQuad(cv, tpage, farSafe, flags);
            }

        next_poly:
            polyPtr = (int *)((char *)polyPtr + 0x30);
        }

    next_object:
        /* */ ;

        /* Ring sub-entry rendering for ALL track parts
         * For our D3D/GL port, we render them here using RenderHiddenSubEntry
         * (the D3D billboard renderer).
         * This covers non-hidden objects whose sub-entries the visFlag==-1 path above skips. */
        int mode_re = *(short *)((char *)obj + 0x2E);
        if (mode_re == 0 && g_raceType != RACE_TIMEATTACK && g_raceSubMode != SUBMODE_TAG) {
            unsigned short subCount_re = *(unsigned short *)((char *)obj + 0x36);
            if (subCount_re > 0) {
                if (g_ringSpawnArray != NULL) {
                    unsigned short subStart_re = *(unsigned short *)((char *)obj + 0x34);
                    int *entry_re = (int *)((char *)g_ringSpawnArray + subStart_re * 16);
                    for (int ri = 0; ri < subCount_re; ri++, entry_re += 4) {
                        if (entry_re[3] == 0) {
                            RenderHiddenSubEntry(entry_re[0], entry_re[1], entry_re[2]);
                        }
                    }
                }
            }
        }
        
        obj += 17;  /* stride 0x44 bytes = 17 ints */
        remaining--;
    } while (remaining > 0);
}
