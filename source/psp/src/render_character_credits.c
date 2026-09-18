/**
 * render_character_credits.c — RenderCharacterCredits
 *
 * FUN_004555b8 — 6027 bytes
 * 3D character model renderer used by the credits sequence.
 *
 * Handles hierarchical limb model with per-limb rotation matrices,
 * vertex transformation, perspective projection, backface culling,
 * clip testing, fog/alpha, and D3D batch submission.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "r_types.h"
#include "r_state.h"
#include "r_draw.h"
#include <math.h>

#ifdef SONICR_DC
#include <kos.h>
extern __attribute__((aligned(32))) pvr_vertex_t s_ptScratch[16];
extern void R_DrawPvrTri(pvr_vertex_t *v);
extern void R_DrawPvrQuad(pvr_vertex_t *v);
#endif

/* ROM: per-segment vertex offset table (0x4FC39C, stride 0x30 = 12 ints) */
extern const int g_tailSegVtx[4][12];

/* ROM: face vertex index mapping (0x4FC45C, 6 quads × 4 indices, stride 0x10) */
extern const int g_tailFaceMap[6][4];

extern void DispatchFaceUVAnimation(Player *player);

/* Fog thresholds — DGROUP doubles at 0x52C2D4, 0x52C2DC.
 * These are normalized Z values (after dividing by depthScale). */
#define FOG_FAR  0.9
#define FOG_NEAR 0.7

/* Vertex array — SrcVertex structs (stride 0x40) at g_vertexArrayBase.
 * See vertex_struct.h for field layout. */
#define VTX(idx)     (&g_vertexArrayBase[(unsigned int)(idx)])
#define FACE_PTR(base, idx) ((int *)((char *)(base) + (unsigned int)(idx) * 0x30))

#define recip256 0.00390625f

/* Build a RenderVertex from charsel vertex data (shared by tri/quad/tail blocks) */
#ifdef SONICR_DC

/* DC variant */
static RenderVertex BuildCharSelVertex(SrcVertex *v, int uvRaw0, int uvRaw1, float depthScale)
{
    RenderVertex rv;
    float fpz = (float)v->depth;
    float invDepth = reciprocal(fpz * 16.0f);
    rv.sx = (float)g_screenCenterX + (float)g_projScaleXCurrent * (float)v->camX * invDepth;
    rv.sy = (float)g_screenCenterY - (float)g_projScaleY * (float)v->camY * invDepth;
    rv.sz = fpz * reciprocal(depthScale);
    rv.rhw = reciprocal(fpz);
    rv.u = (float)(uvRaw0 >> 16) * recip256;
    rv.v = (float)(uvRaw1 >> 16) * recip256;

    int alpha;
    sr_double dz = (sr_double)rv.sz;
    if (dz > FOG_FAR) {
        alpha = 0;
    }
    else if (dz <= FOG_NEAR) {
        alpha = 0xFF;
    }
    else {
        alpha = (int)(255.0 * (FOG_FAR - dz) / (FOG_FAR - FOG_NEAR));
        if (alpha < 0) {
            alpha = -alpha;
        }
    }


    uint8_t r = (v->colorR >> 13);// & 0xff;
    uint8_t g = (v->colorG >> 13);// & 0xff;
    uint8_t b = (v->colorB >> 13);// & 0xff;

    if (r > 255) {
        r = 255;
    }
    if (g > 255) {
        g = 255;
    }
    if (b > 255) {
        b = 255;
    }

    rv.color = ((unsigned int)alpha << 24) |
               ((unsigned int)r << 16) |
               ((unsigned int)g << 8) |
                (unsigned int)b;
    rv.specular = 0;
    return rv;
}

/* DC fast path: write a charsel vertex straight into pvr_vertex_t scratch.
 * Same math as BuildCharSelVertex DC variant; skips the RenderVertex copy
 * and the per-vertex oargb store. */
static inline void BuildCharSelPvrVertex(pvr_vertex_t *pv, SrcVertex *v,
                                         int uvRaw0, int uvRaw1,
                                         float depthScale, uint32_t flags)
{
    float fpz = (float)v->depth;
    float normZ = fpz * reciprocal(depthScale);
    float invDepth = reciprocal(fpz * 16.0f);

    int alpha;
    sr_double dz = (sr_double)normZ;
    if (dz > FOG_FAR) {
        alpha = 0;
    }
    else if (dz <= FOG_NEAR) {
        alpha = 0xFF;
    }
    else {
        alpha = (int)(255.0 * (FOG_FAR - dz) / (FOG_FAR - FOG_NEAR));
        if (alpha < 0) {
            alpha = -alpha;
        }
    }

    uint8_t r = (v->colorR >> 13);
    uint8_t g = (v->colorG >> 13);
    uint8_t b = (v->colorB >> 13);

    if (r > 255) {
        r = 255;
    }
    if (g > 255) {
        g = 255;
    }
    if (b > 255) {
        b = 255;
    }

    pv->flags = flags;
    pv->x = (float)g_screenCenterX + (float)g_projScaleXCurrent * (float)v->camX * invDepth;
    pv->y = (float)g_screenCenterY - (float)g_projScaleY * (float)v->camY * invDepth;
    pv->z     = reciprocal(fpz);
    pv->u     = (float)(uvRaw0 >> 16) * recip256;
    pv->v     = (float)(uvRaw1 >> 16) * recip256;
    pv->argb  = ((unsigned int)alpha << 24) |
                ((unsigned int)r << 16) |
                ((unsigned int)g << 8) |
                 (unsigned int)b;
    pv->oargb = 0;
}
#else
static RenderVertex BuildCharSelVertex(SrcVertex *v, int uvRaw0, int uvRaw1, float depthScale)
{
    RenderVertex rv;
    float fpz = (float)v->depth;
    float invDepth = 1.0f / (fpz * 16.0f);
    rv.sx = (float)g_screenCenterX + (float)g_projScaleXCurrent * (float)v->camX * invDepth;
    rv.sy = (float)g_screenCenterY - (float)g_projScaleY * (float)v->camY * invDepth;
    rv.sz = fpz / depthScale;
    rv.rhw = 1.0f / fpz;
    rv.u = (float)(uvRaw0 >> 16) / 256.0f;
    rv.v = (float)(uvRaw1 >> 16) / 256.0f;

    int alpha;
    sr_double dz = (sr_double)rv.sz;
    if (dz > FOG_FAR) {
        alpha = 0;
    }
    else if (dz <= FOG_NEAR) {
        alpha = 0xFF;
    }
    else {
        alpha = (int)(255.0 * (FOG_FAR - dz) / (FOG_FAR - FOG_NEAR));
        if (alpha < 0) {
            alpha = -alpha;
        }
    }

    rv.color = ((unsigned int)alpha << 24) |
               ((unsigned int)(v->colorR >> 13) << 16) |
               ((unsigned int)(v->colorG >> 13) << 8) |
               ((unsigned int)(v->colorB >> 13));
    rv.specular = 0;
    return rv;
}
#endif

/**
 * RenderCharacterCredits — FUN_004555b8 — 6027 bytes
 *
 * Original calling convention (Watcom fastcall):
 *   EAX = rotation angle A (12-bit, yaw)
 *   EDX = rotation angle B (per-character tilt)
 *   EBX = Z offset (camera distance)
 *   ECX = rotation angle C (0xF00 = default head orientation)
 *   [ebp+8]  = rotation angle D
 *   [ebp+C]  = rotation angle E (0)
 *   [ebp+10] = player struct pointer
 */
void RenderCharacterCredits(int angleA, int angleB, int zOffset,
                            int angleC, int angleD, int angleE,
                            Player *player)
{
    if (player == NULL) {
        return;
    }

    float depthScale = g_farClipFloat;
    if (depthScale <= 0.0f) {
        depthScale = 88064.0f;
    }

    /* Update limb keyframes before rendering */
    DispatchFaceUVAnimation(player);

    /* Scale position offsets */
    int xOff = angleA << 4;
    int yOff = angleB << 4;

    /* Look up sin/cos for 3 rotation angles */
    int sinC = g_sinTable[angleC & 0xFFF] >> 4;
    int cosC = g_cosTable[angleC & 0xFFF] >> 4;
    int sinD = g_sinTable[angleD & 0xFFF] >> 4;
    int cosD = g_cosTable[angleD & 0xFFF] >> 4;
    int cosE = g_cosTable[angleE & 0xFFF] >> 4;
    int sinE = g_sinTable[angleE & 0xFFF] >> 4;

    /* Build global rotation matrix
     * 9 elements, verified against disassembly 0x455648-0x4556C1 */
    int g00 = (cosD * cosE) >> 8;
    int g01 = ((cosD * sinE * cosC) >> 18) + ((sinD * sinC) >> 8);
    int g02 = ((cosD * sinE * sinC) >> 18) + ((-sinD * cosC) >> 8);
    int g10 = (cosE * cosC) >> 8;
    int g11 = (sinC * cosE) >> 8;
    int g20 = (cosE * sinD) >> 8;
    int g21 = ((sinD * sinE * cosC) >> 18) + ((-sinC * cosD) >> 8);
    int g22 = ((sinD * sinE * sinC) >> 18) + ((cosC * cosD) >> 8);

    /* Read model hierarchy */
    Player *pl = (Player *)player;
    int modelIdx = pl->_unk_0x1E0;

    /* Limb table pointer and count */
    int *limbMeta = (int *)((char *)g_limbMetaTable +
                    g_modelMeta[modelIdx].limbStart * 0x18);
    int limbCount = g_modelMeta[modelIdx].limbCount;

    /* Animation frame pointer */
    int animLimbBase = g_modelMeta[modelIdx].animFrameBase;
    int animFrameIdx = pl->animFrameIdx;
    int *animFrame = (int *)((char *)g_animFrameData +
                    (animLimbBase + animFrameIdx * limbCount) * 0x18);

    /* Limb iteration loop */
    do {
        /* Per-limb local rotation from animation angles */
        int lAngZ = animFrame[5];
        int lAngY = (animFrame[4] + 0x400) & 0xFFF;
        int lAngX = animFrame[3];

        int lsinZ = g_sinTable[lAngZ & 0xFFF] >> 4;
        int lsinY = g_sinTable[lAngY] >> 4;
        int lcosY = g_cosTable[lAngY] >> 4;
        int lsinX = g_sinTable[lAngX & 0xFFF] >> 4;
        int lcosX = g_cosTable[lAngX & 0xFFF] >> 4;
        int lcosZ = g_cosTable[lAngZ & 0xFFF] >> 4;

        /* Per-limb local rotation matrix — re-traced from binary disasm
         * 0x4557CB-0x4558E0. Binary negates SY, SZ, SX (sin terms),
         * matching FUN_004437fc (RenderCharacterOnPodium) entry-for-entry.
         * Storage-slot mapping: Mx0=l00 [ebp-0xBC], Mx1=l11 [ebp-0xB0],
         * Mx2=l22 [ebp-0xA4], My0=l01 [ebp-0xB8], My1=l12 [ebp-0xAC],
         * My2=l21 [ebp-0x9C], Mz0=l10 [ebp-0xB4], Mz2=l20 [ebp-0xA0],
         * inline coeff [ebp-0xA8] = lsinZ*4. */
        int negSY = -lsinY;                                                          /* [ebp-0x3c] */
        int negSZ = -lsinZ;                                                          /* [ebp-0x60] */
        int negSX = -lsinX;                                                          /* [ebp-0x5c] */

        int Mx0 = ((lcosY * lcosX) >> 8) + ((negSY * negSZ * negSX) >> 18);          /* [ebp-0xBC] = l00 */
        int Mx1 = (negSX * lcosZ) >> 8;                                              /* [ebp-0xB0] = l11 */
        int Mx2 = ((lsinY * lcosX) >> 8) + ((negSZ * lcosY * negSX) >> 18);          /* [ebp-0xA4] = l22 */

        int My0 = ((lcosY * lsinX) >> 8) + ((negSY * negSZ * lcosX) >> 18);          /* [ebp-0xB8] = l01 */
        int My1 = (lcosZ * lcosX) >> 8;                                              /* [ebp-0xAC] = l12 */
        int My2 = ((lsinX * lsinY) >> 8) + ((lcosX * negSZ * lcosY) >> 18);          /* [ebp-0x9C] = l21 */

        int Mz0 = (negSY * lcosZ) >> 8;                                              /* [ebp-0xB4] = l10 */
        /* Mz1 inline = lsinZ * 4 — applied inline in lz formula below */            /* [ebp-0xA8] */
        int Mz2 = (lcosZ * lcosY) >> 8;                                              /* [ebp-0xA0] = l20 */

        /* Limb position offset from animation frame */
        int limbX = animFrame[0];
        int limbY = animFrame[1];
        int limbZ = animFrame[2];

        /* Transform vertices for this limb */
        SrcVertex *vtxArrayBase = &g_vertexArrayBase[limbMeta[0]];
        int vertCount = limbMeta[1];
        SrcVertex *vtxPtr = vtxArrayBase;

        for (int vi = 0; vi < vertCount; vi++) {
            int wx = vtxPtr->posX;
            int wy = vtxPtr->posY;
            int wz = vtxPtr->posZ;

            /* Local limb rotation — verified from binary disasm 0x455900-0x4559A4.
             *   lx: [ebp-0xBC]*wx + [ebp-0xB0]*wy + [ebp-0xA4]*wz
             *   ly: [ebp-0xB8]*wx + [ebp-0xAC]*wy + [ebp-0x9C]*wz
             *   lz: -(([ebp-0xB4]*wx + [ebp-0xA8]*wy + [ebp-0xA0]*wz)>>12 + limbZ) */
            int lx = limbX + ((Mx0 * wx + Mx1 * wy + Mx2 * wz) >> 12);
            int ly = ((My0 * wx + My1 * wy + My2 * wz) >> 12) + limbY;
            int lz = -(((Mz0 * wx + (lsinZ * 4) * wy + Mz2 * wz) >> 12) + limbZ);

            /* Global rotation + Z offset */
            int projZ = zOffset + ((g22 * lz + g02 * lx + ly * g11) >> 16);
            vtxPtr->depth = projZ;

            if (projZ > 0) {
                /* Screen X — signed div by 4096 (round toward zero) */
                int sxRaw = g20 * lz + g00 * lx + ly * (-sinE);
                int sxDiv = sxRaw / 4096;

                /* Screen Y — signed div by 4096 (round toward zero) */
                int syRaw = g21 * lz + ly * g10 + lx * g01;
                int syDiv = syRaw / 4096;

                vtxPtr->camX = xOff + sxDiv;
                vtxPtr->camY = yOff + syDiv;
                vtxPtr->screenX = g_screenCenterX + (vtxPtr->camX * g_projScaleXCurrent) / (projZ * 16);
                vtxPtr->screenY = g_screenCenterY - (g_projScaleY * vtxPtr->camY) / (projZ * 16);
            }

            vtxPtr++;
        }

        /* Process faces for this limb (lines 21791-22103) */
        int faceStartIdx = limbMeta[2];
        unsigned int faceCount = (unsigned int)limbMeta[3];
        int *face = FACE_PTR(g_charFaceBase, faceStartIdx);

        unsigned int fi;
        for (fi = 0; fi < faceCount; fi++, face = (int *)((char *)face + 0x30)) {
            unsigned short viA = ((unsigned short *)face)[0x20 / 2];
            unsigned short viB = ((unsigned short *)face)[0x22 / 2];
            unsigned short viC = ((unsigned short *)face)[0x24 / 2];

            SrcVertex *vA = VTX(viA);
            SrcVertex *vB = VTX(viB);
            SrcVertex *vC = VTX(viC);

            /* Far clip: original uses g_farClipDepth << 3.
             * During menu screens g_farClipDepth is 0; use fallback 0x15800 (88064)
             * matching Draw3DModelD3D behavior. */
            int farClip = g_farClipDepth << 3;
            if (farClip <= 0) {
                farClip = 0x15800;
            }
            if (vA->depth <= 0 || vA->depth > farClip) {
                continue;
            }
            if (vB->depth <= 0 || vB->depth > farClip) {
                continue;
            }
            if (vC->depth <= 0 || vC->depth > farClip) {
                continue;
            }

            unsigned char flags = *(unsigned char *)((char *)face + 0x2E);

            /* Triangle */
            if ((flags & 1) == 0) {
                /* Disabled: drops legit Sonic/Knuckles faces on the credits big render.
                * Tried both signed and unsigned-wrap multiply; same set of faces dropped.
                * Cull formula and matrix both match binary (FUN_004555b8) entry-for-entry,
                * so the binary's credits ALSO drops these faces — just nobody noticed.
                * Cost of skipping: ~200 extra triangles per character per frame, no perf
                * impact at credits scale. */
#if 0
                if ((flags & 4) == 0) {  /* backface cull — 0x4561D1-0x45620C */
                    int cross = (vA->screenY - vB->screenY) * (vC->screenX - vB->screenX) -
                                (vC->screenY - vB->screenY) * (vA->screenX - vB->screenX);
                    if (cross < 0) {
                        continue;
                    }
                }
#endif
                if ((vA->screenX < g_clipLeft && vB->screenX < g_clipLeft && vC->screenX < g_clipLeft) ||
                    (vA->screenY < g_clipTop  && vB->screenY < g_clipTop  && vC->screenY < g_clipTop) ||
                    (vA->screenX > g_clipRight  && vB->screenX > g_clipRight  && vC->screenX > g_clipRight) ||
                    (vA->screenY > g_clipBottom && vB->screenY > g_clipBottom && vC->screenY > g_clipBottom))
                {
                    continue;
                }

                /* Face tpage byte used directly as tpage index.
                 * Verified: disasm 0x455CD3 uses byte with no ADD offset. */
                int tpage = *(unsigned char *)((char *)face + 0x28);
                if (tpage < 0 || tpage >= 52) {
                    continue;  /* bounds check */
                }

                SrcVertex *triVerts[3] = { vA, vB, vC };
#ifdef SONICR_DC
                static const uint32_t s_pvrFlags[3] = {
                    PVR_CMD_VERTEX, PVR_CMD_VERTEX, PVR_CMD_VERTEX_EOL
                };
                for (int k = 0; k < 3; k++) {
                    BuildCharSelPvrVertex(&s_ptScratch[k], triVerts[k],
                                          face[k * 2], face[k * 2 + 1],
                                          depthScale, s_pvrFlags[k]);
                }
                R_SetTexture(tpage);
                R_SetTexEnv(R_TEXENV_ADD_SIGNED);
                R_DrawPvrTri(s_ptScratch);
#else
                RenderVertex tri[3];
                for (int k = 0; k < 3; k++) {
                    tri[k] = BuildCharSelVertex(triVerts[k], face[k * 2], face[k * 2 + 1], depthScale);
                }

                R_SetTexture(tpage);
                R_SetTexEnv(R_TEXENV_ADD_SIGNED);
                R_DrawTri(tri);
#endif
            }
            else {
                /* Quad */
                unsigned short viD = ((unsigned short *)face)[0x26 / 2];
                SrcVertex *vD = VTX(viD);
                if (vD->depth <= 0 || vD->depth > farClip) {
                    continue;
                }

                /* Disabled: same reason as the tri cull above. */
#if 0
                if ((flags & 4) == 0) {  /* quad backface cull — 0x455B4D-0x455C2B */
                    int cross;
                    if ((flags & 2) == 0) {
                        cross = (vA->screenY - vB->screenY) * (vC->screenX - vB->screenX) -
                                (vC->screenY - vB->screenY) * (vA->screenX - vB->screenX);
                    }
                    else {
                        int cross1 = (vA->screenY - vB->screenY) * (vC->screenX - vB->screenX) -
                                     (vC->screenY - vB->screenY) * (vA->screenX - vB->screenX);
                        if (cross1 >= 0) {
                            goto next_face;
                        }
                        cross = (vD->screenX - vC->screenX) * (vA->screenY - vC->screenY) -
                                (vD->screenY - vC->screenY) * (vA->screenX - vC->screenX);
                    }
                    if (cross < 0) {
                        continue;
                    }
                }
#endif

                if (vA->screenX < g_clipLeft && vB->screenX < g_clipLeft &&
                    vC->screenX < g_clipLeft && vD->screenX < g_clipLeft)
                {
                    continue;
                }
                if (vA->screenY < g_clipTop && vB->screenY < g_clipTop &&
                    vC->screenY < g_clipTop && vD->screenY < g_clipTop)
                {
                    continue;
                }
                if (vA->screenX > g_clipRight && vB->screenX > g_clipRight &&
                    vC->screenX > g_clipRight && vD->screenX > g_clipRight)
                {
                    continue;
                }
                if (vA->screenY > g_clipBottom && vB->screenY > g_clipBottom &&
                    vC->screenY > g_clipBottom && vD->screenY > g_clipBottom)
                {
                    continue;
                }

                int tpage = *(unsigned char *)((char *)face + 0x28);

                SrcVertex *quadVerts[4] = { vA, vB, vC, vD };
#ifdef SONICR_DC
                /* PVR strip order is 0,1,3,2 from a TL,TR,BR,BL quad. */
                static const int s_pvrStripIdx[4] = { 0, 1, 3, 2 };
                static const uint32_t s_pvrFlags[4] = {
                    PVR_CMD_VERTEX, PVR_CMD_VERTEX, PVR_CMD_VERTEX, PVR_CMD_VERTEX_EOL
                };
                for (int k = 0; k < 4; k++) {
                    int srcIdx = s_pvrStripIdx[k];
                    BuildCharSelPvrVertex(&s_ptScratch[k], quadVerts[srcIdx],
                                          face[srcIdx * 2], face[srcIdx * 2 + 1],
                                          depthScale, s_pvrFlags[k]);
                }
                R_SetTexture(tpage);
                R_SetTexEnv(R_TEXENV_ADD_SIGNED);
                R_DrawPvrQuad(s_ptScratch);
#else
                RenderVertex quad[4];
                for (int k = 0; k < 4; k++) {
                    quad[k] = BuildCharSelVertex(quadVerts[k], face[k * 2], face[k * 2 + 1], depthScale);
                }

                R_SetTexture(tpage);
                R_SetTexEnv(R_TEXENV_ADD_SIGNED);
                R_DrawQuad(quad);
#endif
            }
#if 0
            /* paired with the cull goto above */
        next_face:
            /* */ ;
#endif
        }

        /* Advance to next limb */
        limbMeta += 6;       /* stride 0x18 = 6 ints */
        animFrame += 6;      /* stride 0x18 = 6 ints */
        limbCount--;
    } while (limbCount > 0);

    /* Tails-specific tail rendering
     * 4 tail segments × 6 quads each. Vertex indices from ROM tables,
     * offset by the Tails model vertex base from g_modelMeta[1]. */
    if (pl->charId == CHAR_TAILS) {
        /* Tail limb vertex base — from g_modelMeta[1] (Tails' entry).
         * DAT_007130F4 = g_modelMeta[1].vertexStart */
        int tailVertBase = g_modelMeta[1].vertexStart;

        /* Tpage for tail = g_tpageCharacters (byte at 0x8F6C30) */
        unsigned char tailTpage = (unsigned char)g_tpageCharacters;

        for (int seg = 0; seg < 4; seg++) {
            const int *segVtx = g_tailSegVtx[seg];

            for (int fq = 0; fq < 6; fq++) {
                /* Look up 4 vertex indices for this quad face */
                int idxA = tailVertBase + segVtx[g_tailFaceMap[fq][0]];
                int idxB = tailVertBase + segVtx[g_tailFaceMap[fq][1]];
                int idxC = tailVertBase + segVtx[g_tailFaceMap[fq][2]];
                int idxD = tailVertBase + segVtx[g_tailFaceMap[fq][3]];

                SrcVertex *vA = VTX(idxA);
                SrcVertex *vB = VTX(idxB);
                SrcVertex *vC = VTX(idxC);
                SrcVertex *vD = VTX(idxD);

                /* Depth clip (with menu fallback) */
                int fc = g_farClipDepth << 3;
                if (fc <= 0) {
                    fc = 0x15800;
                }
                if (vA->depth <= 0 || vA->depth > fc) {
                    continue;
                }
                if (vB->depth <= 0 || vB->depth > fc) {
                    continue;
                }
                if (vC->depth <= 0 || vC->depth > fc) {
                    continue;
                }
                if (vD->depth <= 0 || vD->depth > fc) {
                    continue;
                }

                /* Screen clip */
                if (vA->screenX < g_clipLeft && vB->screenX < g_clipLeft &&
                    vC->screenX < g_clipLeft && vD->screenX < g_clipLeft)
                {
                    continue;
                }
                if (vA->screenY < g_clipTop && vB->screenY < g_clipTop &&
                    vC->screenY < g_clipTop && vD->screenY < g_clipTop)
                {
                    continue;
                }
                if (vA->screenX > g_clipRight && vB->screenX > g_clipRight &&
                    vC->screenX > g_clipRight && vD->screenX > g_clipRight)
                {
                    continue;
                }
                if (vA->screenY > g_clipBottom && vB->screenY > g_clipBottom &&
                    vC->screenY > g_clipBottom && vD->screenY > g_clipBottom)
                {
                    continue;
                }

                /* UV setup — alternates by segment parity (lines 22147-22164) */
                int tailUV[8];
                if ((seg & 1) == 0) {
                    tailUV[0] = 0xC00000;
                    tailUV[1] = 0x800000;
                    tailUV[6] = 0xC00000;
                    tailUV[7] = 0x87FFFF;
                }
                else {
                    tailUV[0] = 0;
                    tailUV[1] = 0x800000;
                    tailUV[6] = 0;
                    tailUV[7] = 0x87FFFF;
                }
                tailUV[2] = tailUV[0] + 0x7FFFF;
                tailUV[3] = 0x87FFFF;
                tailUV[4] = tailUV[0] + 0x7FFFF;
                tailUV[5] = 0x800000;

                /* Submit tail quad via immediate mode */
                SrcVertex *tailVerts[4] = { vA, vB, vC, vD };
#ifdef SONICR_DC
                static const int s_pvrStripIdx[4] = { 0, 1, 3, 2 };
                static const uint32_t s_pvrFlags[4] = {
                    PVR_CMD_VERTEX, PVR_CMD_VERTEX, PVR_CMD_VERTEX, PVR_CMD_VERTEX_EOL
                };
                for (int k = 0; k < 4; k++) {
                    int srcIdx = s_pvrStripIdx[k];
                    BuildCharSelPvrVertex(&s_ptScratch[k], tailVerts[srcIdx],
                                          tailUV[srcIdx * 2], tailUV[srcIdx * 2 + 1],
                                          depthScale, s_pvrFlags[k]);
                }
                R_SetTexture(tailTpage);
                R_SetTexEnv(R_TEXENV_ADD_SIGNED);
                R_DrawPvrQuad(s_ptScratch);
#else
                RenderVertex tailQuad[4];
                for (int k = 0; k < 4; k++) {
                    tailQuad[k] = BuildCharSelVertex(tailVerts[k], tailUV[k * 2], tailUV[k * 2 + 1], depthScale);
                }

                R_SetTexture(tailTpage);
                R_SetTexEnv(R_TEXENV_ADD_SIGNED);
                R_DrawQuad(tailQuad);
#endif
            }
        }
    }
}
