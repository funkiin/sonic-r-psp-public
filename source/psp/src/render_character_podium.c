/**
 * render_character_podium.c — RenderCharacterOnPodium
 *
 * FUN_004437fc — 3204 bytes
 * 3D character model renderer used by the character-select podium and
 * the results podium.
 *
 * Translated from 0x004437fc disasm using RenderCharacterCredits
 * (0x4555b8, render_character_credits.c) as structural template — 90% overlap.
 * Same per-character model data at 0x92568c, same sin/cos vertex
 * transforms, same hierarchical limb model.
 *
 * Original binary used sort-list face submission; this translation
 * uses the D3D tpage path since tpage batches are already wired for
 * the SDL D3D emulation layer.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "r_types.h"
#include "r_state.h"
#include "r_draw.h"
#include <math.h>

extern void DispatchFaceUVAnimation(Player *player);   /* FUN_0047fea0 */

#define VTX_P(idx)            (&g_vertexArrayBase[(unsigned int)(idx)])
#define FACE_PTR_P(base, idx) ((int *)((char *)(base) + (unsigned int)(idx) * 0x30))

/* Fog thresholds — DGROUP doubles. Normalized Z values. */
#define FOG_FAR_P  0.9
#define FOG_NEAR_P 0.7

#define recip256 0.00390625f

/* ROM: per-segment vertex offset table (0x4FC39C, stride 0x30 = 12 ints) */
extern const int g_tailSegVtx[4][12];

/* ROM: face vertex index mapping (0x4FC45C, 6 quads × 4 indices, stride 0x10) */
extern const int g_tailFaceMap[6][4];

static RenderVertex BuildPodiumVertex(SrcVertex *v, int uvRaw0, int uvRaw1, float depthScale)
{
    RenderVertex rv;
    float fpz = (float)v->depth;
    float invDepth = reciprocal((fpz * 16.0f));
    rv.sx = (float)g_screenCenterX + (float)g_projScaleXCurrent * (float)v->camX * invDepth;
    rv.sy = (float)g_screenCenterY - (float)g_projScaleY * (float)v->camY * invDepth;
    rv.sz = fpz * reciprocal(depthScale);
    rv.rhw = reciprocal(fpz);
    rv.u = (float)(uvRaw0 >> 16) * recip256;
    rv.v = (float)(uvRaw1 >> 16) * recip256;

    int alpha;
    sr_double dz = (sr_double)rv.sz;
    if (dz > FOG_FAR_P) {
        alpha = 0;
    }
    else if (dz <= FOG_NEAR_P) {
        alpha = 0xFF;
    }
    else {
        alpha = (int)(255.0 * (FOG_FAR_P - dz) / (FOG_FAR_P - FOG_NEAR_P));
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

/**
 * RenderCharacterOnPodium — FUN_004437fc — 3204 bytes
 *
 * Params (Watcom fastcall):
 *   EAX=xOff, EDX=yOff, EBX=zOffset, ECX=angleC(0xF00),
 *   stack[0]=angleD(rotation), stack[1]=angleE(0),
 *   stack[2]=playerPtr, stack[3]=depthSortBase(unused)
 */
void RenderCharacterOnPodium(int xOff, int yOff, int zOffset,
                             int angleC, int angleD, int angleE,
                             Player *player)
{
    if (player == NULL) {
        return;
    }
    /* Depth scale for Z normalization (0x8FB364) */
    float depthScale = g_farClipFloat;
    if (depthScale <= 0.0f) {
        depthScale = 88064.0f;
    }

    /* Update limb keyframes before rendering */
    DispatchFaceUVAnimation(player);

    /* Scale position offsets */
    int xPos = xOff << 4;
    int yPos = yOff << 4;

    /* Trig lookups for 3 rotation angles
     * angleC = head orientation (ECX, typically 0xF00)
     * angleD = character rotation ([ebp+8])
     * angleE = tilt ([ebp+C], typically 0) */
    int sinC = g_sinTable[angleC & 0xFFF] >> 4;
    int cosC = g_cosTable[angleC & 0xFFF] >> 4;
    int sinD = g_sinTable[angleD & 0xFFF] >> 4;
    int cosD = g_cosTable[angleD & 0xFFF] >> 4;
    int cosE = g_cosTable[angleE & 0xFFF] >> 4;
    int sinE = g_sinTable[angleE & 0xFFF] >> 4;

    /* Global rotation matrix
     * Verified: identical formulas to RenderCharacterCredits */
    int g00 = (cosD * cosE) >> 8;
    int g01 = ((cosD * sinE * cosC) >> 18) + ((sinD * sinC) >> 8);
    int g02 = ((cosD * sinE * sinC) >> 18) + ((-sinD * cosC) >> 8);
    int g10 = (cosE * cosC) >> 8;
    int g11 = (sinC * cosE) >> 8;
    int g20 = (cosE * sinD) >> 8;
    int g21 = ((sinD * sinE * cosC) >> 18) + ((-sinC * cosD) >> 8);
    int g22 = ((sinD * sinE * sinC) >> 18) + ((cosC * cosD) >> 8);

    /* Model hierarchy lookup */
    int modelIdx = player->_unk_0x1E0;

    int *limbMeta = (int *)((char *)g_limbMetaTable +
                    g_modelMeta[modelIdx].limbStart * 0x18);
    int limbCount = g_modelMeta[modelIdx].limbCount;

    int animLimbBase = g_modelMeta[modelIdx].animFrameBase;
    int animFrameIdx = player->animFrameIdx;
    int *animFrame = (int *)((char *)g_animFrameData +
                    (animLimbBase + animFrameIdx * limbCount) * 0x18);

    /* Per-limb loop */
    do {
        /* Per-limb local rotation from animation angles */
        int lAngZ = animFrame[5];
        int lAngY = (animFrame[4] + 0x400) & 0xFFF;
        int lAngX = animFrame[3];

        int lsinZ = g_sinTable[lAngZ & 0xFFF] >> 4;
        int negLsinZ = -lsinZ;
        int lsinY = g_sinTable[lAngY] >> 4;
        int lcosY = g_cosTable[lAngY] >> 4;
        int lsinX = g_sinTable[lAngX & 0xFFF] >> 4;
        int negLsinX = -lsinX;
        int lcosX = g_cosTable[lAngX & 0xFFF] >> 4;
        int lcosZ = g_cosTable[lAngZ & 0xFFF] >> 4;

        /* Per-limb local rotation matrix */
        int l00 = ((lcosY * lcosX) >> 8) + (((-lsinY) * negLsinZ * negLsinX) >> 18);
        int l01 = ((lcosY * lsinX) >> 8) + (((-lsinY) * negLsinZ * lcosX) >> 18);
        int l10 = ((-lsinY) * lcosZ) >> 8;
        int l11 = (negLsinX * lcosZ) >> 8;
        int l12 = (lcosZ * lcosX) >> 8;
        int l20 = (lcosZ * lcosY) >> 8;
        int l21 = ((lsinX * lsinY) >> 8) + ((lcosX * negLsinZ * lcosY) >> 18);
        int l22 = ((lsinY * lcosX) >> 8) + ((negLsinZ * lcosY * negLsinX) >> 18);

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

            /* Local limb rotation */
            int lx = limbX + ((l22 * wz + l11 * wy + l00 * wx) >> 12);
            int ly = ((l01 * wx + l12 * wy + l21 * wz) >> 12) + limbY;
            int lz = -(((wx * l10 + wy * (lsinZ * 4) + wz * l20) >> 12) + limbZ);

            /* Global rotation + Z offset */
            int projZ = zOffset + ((g22 * lz + g02 * lx + ly * g11) >> 16);
            vtxPtr->depth = projZ;

            if (projZ > 0) {
                int sxRaw = g20 * lz + g00 * lx + ly * (-sinE);
                int sxDiv = sxRaw / 4096;

                int syRaw = g21 * lz + ly * g10 + lx * g01;
                int syDiv = syRaw / 4096;

                vtxPtr->camX = xPos + sxDiv;
                vtxPtr->camY = yPos + syDiv;
                vtxPtr->screenX = g_screenCenterX + (vtxPtr->camX * g_projScaleXCurrent) / (projZ * 16);
                vtxPtr->screenY = g_screenCenterY - (g_projScaleY * vtxPtr->camY) / (projZ * 16);
            }

            vtxPtr++;
        }

        /* Process faces for this limb (disasm lines 370-700) */
        int faceStartIdx = limbMeta[2];
        unsigned int faceCount = (unsigned int)limbMeta[3];
        int *face = FACE_PTR_P(g_charFaceBase, faceStartIdx);

        for (unsigned int fi = 0; fi < faceCount; fi++, face = (int *)((char *)face + 0x30)) {
            unsigned short viA = ((unsigned short *)face)[0x20 / 2];
            unsigned short viB = ((unsigned short *)face)[0x22 / 2];
            unsigned short viC = ((unsigned short *)face)[0x24 / 2];

            SrcVertex *vA = VTX_P(viA);
            SrcVertex *vB = VTX_P(viB);
            SrcVertex *vC = VTX_P(viC);

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

            if ((flags & 1) == 0) {
                /* Triangle */
                if ((flags & 4) == 0) {
                    int cross = (vA->screenY - vB->screenY) * (vC->screenX - vB->screenX) -
                                (vA->screenX - vB->screenX) * (vC->screenY - vB->screenY);
                    if (cross < 0) {
                        continue;
                    }
                }

                if ((vA->screenX < g_clipLeft && vB->screenX < g_clipLeft && vC->screenX < g_clipLeft) ||
                    (vA->screenY < g_clipTop  && vB->screenY < g_clipTop  && vC->screenY < g_clipTop) ||
                    (vA->screenX > g_clipRight  && vB->screenX > g_clipRight  && vC->screenX > g_clipRight) ||
                    (vA->screenY > g_clipBottom && vB->screenY > g_clipBottom && vC->screenY > g_clipBottom))
                {
                    continue;
                }

                int tpage = *(unsigned char *)((char *)face + 0x28);
                if (tpage < 0 || tpage >= 52) {
                    continue;
                }

                RenderVertex tri[3];

                SrcVertex *triVerts[3] = { vA, vB, vC };
                for (int k = 0; k < 3; k++) {
                    tri[k] = BuildPodiumVertex(triVerts[k], face[k * 2], face[k * 2 + 1], depthScale);
                }

                R_SetTexture(tpage);
                R_SetTexEnv(R_TEXENV_ADD_SIGNED);
                R_DrawTri(tri);
            }
            else {
                /* Quad */
                unsigned short viD = ((unsigned short *)face)[0x26 / 2];
                SrcVertex *vD = VTX_P(viD);
                if (vD->depth <= 0 || vD->depth > farClip) {
                    continue;
                }

                if ((flags & 4) == 0) {
                    int cross;
                    if ((flags & 2) == 0) {
                        cross = (vA->screenY - vB->screenY) * (vC->screenX - vB->screenX) -
                                (vA->screenX - vB->screenX) * (vC->screenY - vB->screenY);
                    } else {
                        int cross1 = (vA->screenY - vB->screenY) * (vC->screenX - vB->screenX) -
                                     (vA->screenX - vB->screenX) * (vC->screenY - vB->screenY);
                        if (cross1 >= 0) {
                            goto podium_quad_submit;
                        }
                        cross = (vA->screenY - vC->screenY) * (vD->screenX - vC->screenX) -
                                (vA->screenX - vC->screenX) * (vD->screenY - vC->screenY);
                    }
                    if (cross < 0) {
                        continue;
                    }
                }

            podium_quad_submit:
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

                RenderVertex quad[4];

                SrcVertex *quadVerts[4] = { vA, vB, vC, vD };
                for (int k = 0; k < 4; k++) {
                    quad[k] = BuildPodiumVertex(quadVerts[k], face[k * 2], face[k * 2 + 1], depthScale);
                }

                R_SetTexture(tpage);
                R_SetTexEnv(R_TEXENV_ADD_SIGNED);
                R_DrawQuad(quad);
            }
        }

        /* Advance to next limb */
        limbMeta += 6;       /* stride 0x18 = 6 ints */
        animFrame += 6;      /* stride 0x18 = 6 ints */
        limbCount--;
    } while (limbCount > 0);

    /* Tails-specific tail rendering (disasm lines 709-927)
     * 4 tail segments × 6 quads each. */
    if (player->charId == CHAR_TAILS) {
        int tailVertBase = g_modelMeta[1].vertexStart;
        unsigned char tailTpage = (unsigned char)g_tpageCharacters;

        for (int seg = 0; seg < 4; seg++) {
            const int *segVtx = g_tailSegVtx[seg];

            for (int fq = 0; fq < 6; fq++) {
                int idxA = tailVertBase + segVtx[g_tailFaceMap[fq][0]];
                int idxB = tailVertBase + segVtx[g_tailFaceMap[fq][1]];
                int idxC = tailVertBase + segVtx[g_tailFaceMap[fq][2]];
                int idxD = tailVertBase + segVtx[g_tailFaceMap[fq][3]];

                SrcVertex *vA = VTX_P(idxA);
                SrcVertex *vB = VTX_P(idxB);
                SrcVertex *vC = VTX_P(idxC);
                SrcVertex *vD = VTX_P(idxD);

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

                RenderVertex tailQuad[4];
                SrcVertex *tailVQuads[4] = { vA, vB, vC, vD };

                for (int k = 0; k < 4; k++) {
                    tailQuad[k] = BuildPodiumVertex(tailVQuads[k], tailUV[k * 2], tailUV[k * 2 + 1], depthScale);
                }

                R_SetTexture(tailTpage);
                R_SetTexEnv(R_TEXENV_ADD_SIGNED);
                R_DrawQuad(tailQuad);
            }
        }
    }
}
