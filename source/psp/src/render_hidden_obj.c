/**
 * render_hidden_obj.c — Billboard rendering for item boxes and track sub-objects.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "r_types.h"
#include "r_state.h"
#include "r_draw.h"
#include "nearclip.h"   /* g_scissorEdge for split-screen fog suppression */
#include <math.h>

#ifdef SONICR_DC
extern __attribute__((aligned(32))) pvr_vertex_t s_ptScratch[16];
extern void R_DrawPvrQuad(pvr_vertex_t *v);
#endif

/* View matrix: g_viewMtx[16] with stride-4 layout (matches binary).
 * Binary addresses: [44]=Mtx00 [48]=Mtx01 [4C]=Mtx02
 *                   [54]=Mtx10 [58]=Mtx11 [5C]=Mtx12
 *                   [64]=Mtx20 [68]=Mtx21 [6C]=Mtx22 */

#define BILLBOARD_HALF 12   /* ±12 world units for billboard size */
#define FOG_FAR   0.9
#define FOG_NEAR  0.7
#define UV_STEP   0.0625f   /* 1/16 of texture = one sprite frame */

/* Fog alpha: same thresholds as RenderPlayfieldGridD3D */
static int BillboardFogAlpha(sr_double depth)
{
    if (depth > FOG_FAR) {
        return 0;
    }
    if (depth <= FOG_NEAR) {
        return 0xFF;
    }
    int alpha = (int)((depth + (-0.9)) * 1275.0);
    return (alpha < 0) ? -alpha : alpha;
}

/**
 * RenderHiddenSubEntry — 0x0044F584 — 1031 bytes
 * Renders a billboard quad for an item/object at the given world position.
 * Transforms through view matrix, projects to screen, applies fog,
 * submits textured quad to tpage batch at g_tpageCharBase.
 *
 * Watcom: EAX=worldX, EDX=worldZ, EBX=worldY
 */
void RenderHiddenSubEntry(int worldX, int worldZ, int worldY)
{
    int tp = g_tpageCharBase;
    if (g_tpageStateArray[tp] != 4) {
        return;
    }

    /* View transform: world → camera space */
    int dx = worldX - g_camIntX;
    int dz = worldZ - g_camIntY;
    int dy = worldY - g_camIntZ;

#ifdef SONICR_DC
    /* DC fast path: 3 fipr dot products + 1 fsrra reciprocal for the 4
     * corner perspective divides. Direct pvr_vertex_t emit into
     * s_ptScratch in PVR strip order, single R_DrawPvrQuad submit. */
    float dx_f = (float)dx, dy_f = (float)dy, dz_f = (float)dz;
    float camX_f, camY_f, camZ_f;
    /* Note source-axis order matches the legacy int path: (dx, dz, dy). */
    vec3f_dot(dx_f, dz_f, dy_f,
              g_viewMtxF[2], g_viewMtxF[6], g_viewMtxF[10], camZ_f);

    /* Binary 0x44F5EA-0x44F5F9: cull against [0x8FB360] = g_farClipTimes8,
     * with NO lower bound. The port used to recompute the threshold locally
     * and floor it at 0x15800 (= 0x2B00 << 3, the Island default far clip),
     * which pinned billboards to the full draw distance no matter how near
     * the world was clipped — most visibly in split-screen, where the far
     * plane is reduced per viewport. Reading the shared global instead means
     * this tracks every draw-distance change automatically. */
    if (camZ_f < 1.0f) {
        return;
    }
    if (camZ_f > (float)g_farClipTimes8) {
        return;
    }

    vec3f_dot(dx_f, dz_f, dy_f,
              g_viewMtxF[0], g_viewMtxF[4], g_viewMtxF[8], camX_f);
    vec3f_dot(dx_f, dz_f, dy_f,
              g_viewMtxF[1], g_viewMtxF[5], g_viewMtxF[9], camY_f);

    float inv_cz = reciprocal(camZ_f);
    float halfF = (float)BILLBOARD_HALF;
    float scrX0_f = (float)g_screenCenterX + (float)g_projScaleXCurrent * (camX_f - halfF) * inv_cz;
    float scrY0_f = (float)g_screenCenterY - (float)g_projScaleY * (camY_f + halfF) * inv_cz;
    float scrX1_f = (float)g_screenCenterX + (float)g_projScaleXCurrent * (camX_f + halfF) * inv_cz;
    float scrY1_f = (float)g_screenCenterY - (float)g_projScaleY * (camY_f - halfF) * inv_cz;

    int scrX0 = (int)scrX0_f, scrY0 = (int)scrY0_f;
    int scrX1 = (int)scrX1_f, scrY1 = (int)scrY1_f;

    if (scrX0 > g_clipRight || scrX1 < g_clipLeft ||
        scrY0 > g_clipBottom || scrY1 < g_clipTop)
    {
        return;
    }

    float baseUV = (float)g_ringAnimFrame * UV_STEP;
    float uvRight = baseUV + UV_STEP;
    float zBuf = camZ_f * reciprocal(g_farClipFloat);
    int fogAlpha = BillboardFogAlpha((sr_double)zBuf);
    /* Fully fogged out — the distance cull rejects at the far plane, fog
     * reaches 0 at 0.9x of it, so this band was submitted invisible. */
    if (fogAlpha == 0) {
        return;
    }
    unsigned int color = ((unsigned int)fogAlpha << 24) | VERTEX_WHITE_RGB;

    R_SetTexture(tp);
    R_SetTexEnv(R_TEXENV_MODULATE);

    /* Strip slots in PVR order TL,TR,BL,BR (= source TL,TR,BR,BL via 0,1,3,2). */
    pvr_vertex_t *vs = s_ptScratch;

    vs->flags = PVR_CMD_VERTEX;
    vs->x = scrX0_f;
    vs->y = scrY0_f;
    vs->z = inv_cz;
    vs->u = baseUV;
    vs->v = 0.0f;
    vs->argb = color;
    vs++->oargb = 0;

    vs->flags = PVR_CMD_VERTEX;
    vs->x = scrX1_f;
    vs->y = scrY0_f;
    vs->z = inv_cz;
    vs->u = uvRight;
    vs->v = 0.0f;
    vs->argb = color;
    vs++->oargb = 0;

    vs->flags = PVR_CMD_VERTEX;
    vs->x = scrX0_f;
    vs->y = scrY1_f;
    vs->z = inv_cz;
    vs->u = baseUV;
    vs->v = UV_STEP;
    vs->argb = color;
    vs++->oargb = 0;

    vs->flags = PVR_CMD_VERTEX_EOL;
    vs->x = scrX1_f;
    vs->y = scrY1_f;
    vs->z = inv_cz;
    vs->u = uvRight;
    vs->v = UV_STEP;
    vs->argb = color;
    vs->oargb = 0;

    R_DrawPvrQuad(s_ptScratch);
#else
    /* Camera-space Z (depth) — binary: [4C]*dx + [5C]*dz + [6C]*dy = forward column */
    int camZ = (int)(((long long)g_viewMtx02 * dx + (long long)g_viewMtx12 * dz + (long long)g_viewMtx22 * dy) >> 12);
    /* Same cull as the float path above — binary 0x44F5EA-0x44F5F9, against
     * g_farClipTimes8 with no floor. */
    if (camZ < 1) {
        return;
    }
    if (camZ > g_farClipTimes8) {
        return;
    }

    /* Camera-space X — binary: [44]*dx + [54]*dz + [64]*dy = right column */
    int camX = (int)(((long long)g_viewMtx00 * dx + (long long)g_viewMtx10 * dz + (long long)g_viewMtx20 * dy) >> 12);

    /* Camera-space Y — binary: [48]*dx + [58]*dz + [68]*dy = up column */
    int camY = (int)(((long long)g_viewMtx01 * dx + (long long)g_viewMtx11 * dz + (long long)g_viewMtx21 * dy) >> 12);

    /* Project to screen with ±BILLBOARD_HALF offset */
    int scrX0 = g_screenCenterX + (g_projScaleXCurrent * (camX - BILLBOARD_HALF)) / camZ;
    int scrY0 = g_screenCenterY - (g_projScaleY * (camY + BILLBOARD_HALF)) / camZ;
    int scrX1 = g_screenCenterX + (g_projScaleXCurrent * (camX + BILLBOARD_HALF)) / camZ;
    int scrY1 = g_screenCenterY - (g_projScaleY * (camY - BILLBOARD_HALF)) / camZ;

    /* Screen clip */
    if (scrX0 > g_clipRight || scrX1 < g_clipLeft ||
        scrY0 > g_clipBottom || scrY1 < g_clipTop)
    {
        return;
    }

    /* Animated UV (16-frame atlas) */
    float baseUV = (float)g_ringAnimFrame * UV_STEP;

    /* Fog */
    float zBuf = (float)camZ * reciprocal(g_farClipFloat);
    int fogAlpha = BillboardFogAlpha((sr_double)zBuf);
    if (fogAlpha == 0) {
        return;          /* fully fogged out — see the sibling path above */
    }

    /* Submit quad via immediate mode */
    float rhw = reciprocal((float)camZ);
    unsigned int color = ((unsigned int)fogAlpha << 24) | VERTEX_WHITE_RGB;
    float uvRight = baseUV + UV_STEP;

    R_SetTexture(tp);
    R_SetTexEnv(R_TEXENV_MODULATE);

    RenderVertex verts[4] = {
        { (float)scrX0, (float)scrY0, zBuf, rhw, color, 0, baseUV,  0.0f },
        { (float)scrX1, (float)scrY0, zBuf, rhw, color, 0, uvRight, 0.0f },
        { (float)scrX1, (float)scrY1, zBuf, rhw, color, 0, uvRight, UV_STEP },
        { (float)scrX0, (float)scrY1, zBuf, rhw, color, 0, baseUV,  UV_STEP },
    };
    R_DrawQuad(verts);
#endif
}
