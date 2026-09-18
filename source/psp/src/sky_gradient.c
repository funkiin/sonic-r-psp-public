/**
 * sky_gradient.c — Parallax sky gradient generation
 *
 * Binary generates 8 randomized gradient strips into the bottom portion
 * of the parallax D3D surface (offset 0x2D000, 3-byte RGB, 768 bytes/row).
 * The main texture content comes from other functions in the parallax chain.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include <stdlib.h>
#include <string.h>

extern int Random(void);
extern void R_MarkTextureDirty(int tpage);

/* =====================================================================
 * GenerateSkyGradientTpage — FUN_0046FB30 — 998 bytes
 *
 * Generates 8 randomized gradient strips into a tpage surface.
 * Each strip has 6 random color channels (R/G/B for top and bottom),
 * interpolated over 16 rows × 15 columns. Entry[0] wraps to entry[8].
 *
 * Binary writes to g_tpagePixelBuf at two offsets (+0xF000, +0x1E000)
 * with format determined by g_bitsPerPixel (8/15/16).
 * For our GL port: writes to tpage pixel buffer in 16bpp format.
 *
 * On DC, render_pvr.c provides a direct-VRAM override of this function
 * (writes twiddled into s_pvrTextures[g_tpageCharBase]) to avoid keeping
 * the 128KB system-RAM source buffer alive after upload.
 *
 * These gradients are used for the boost-pad trail effect.
 * 
 * They also get used to color the two flags at the Regal Ruins
 * starting line.
 *
 * No parameters. Uses g_tpageCharBase (0x8F6C28) for surface selection.
 * ===================================================================== */
#ifndef SONICR_DC
void GenerateSkyGradientTpage(void)
{
    /* Binary reads surface index from 0x8F6C28 (g_tpageCharBase) */
    int surfIdx = g_tpageCharBase;
    extern void GL_KeepPixels(int);
    GL_KeepPixels(surfIdx);
    unsigned char *surface = (unsigned char *)g_tpagePixelBuf[surfIdx];
    if (surface == NULL) {
        return;
    }

    /* Phase 1: Generate 8+1 random color values per channel.
     * Each channel is either 0x8000 (near-zero) or 0x1F8000 (near-max)
     * in 16.16 fixed point (>>16 gives 0 or 31 in 5-bit). */
    int topR[9];
    int topG[9];
    int topB[9];
    int botR[9];
    int botG[9];
    int botB[9];

    for (int i = 0; i < 8; i++) {
        topR[i] = (Random() > 0x4000) ? 0x8000 : 0x1F8000;
        topG[i] = (Random() > 0x4000) ? 0x8000 : 0x1F8000;
        topB[i] = (Random() > 0x4000) ? 0x8000 : 0x1F8000;
        botR[i] = (Random() > 0x4000) ? 0x8000 : 0x1F8000;
        botG[i] = (Random() > 0x4000) ? 0x8000 : 0x1F8000;
        botB[i] = (Random() > 0x4000) ? 0x8000 : 0x1F8000;
    }
    /* Wrap: entry[8] = entry[0] for seamless looping */
    topR[8] = topR[0];
    topG[8] = topG[0];
    topB[8] = topB[0];
    botR[8] = botR[0];
    botG[8] = botG[0];
    botB[8] = botB[0];

    /* Phase 2: For each of 8 strips, interpolate and write pixels.
     * Binary writes to two regions: surface+0xF000 (8bpp dest) and
     * surface+0x1E000 (16bpp dest). Row stride: 0x100 (8bpp) or 0x200 (16bpp). */
    unsigned char *dst8 = surface + 0xF000;
    unsigned short *dst16 = (unsigned short *)(surface + 0x1E000);

    int bpp = g_bitsPerPixel;

    for (int i = 0; i < 8; i++) {
        /* Current strip colors (fixed-point 16.16) */
        int curR = topR[i];
        int curG = topG[i];
        int curB = topB[i];
        int curR2 = botR[i];
        int curG2 = botG[i];
        int curB2 = botB[i];

        /* Step per row: (next - current) / 16 */
        int stepR = (topR[i + 1] - curR) / 16;
        int stepG = (topG[i + 1] - curG) / 16;
        int stepB = (topB[i + 1] - curB) / 16;
        int stepR2 = (botR[i + 1] - curR2) / 16;
        int stepG2 = (botG[i + 1] - curG2) / 16;
        int stepB2 = (botB[i + 1] - curB2) / 16;

        for (int row = 0; row < 16; row++) {
            /* Per-column interpolation: blend top→bottom over 15 steps */
            int colStepR = (curR2 - curR) / 15;
            int colStepG = (curG2 - curG) / 15;
            int colStepB = (curB2 - curB) / 15;

            int pixR = curR;
            int pixG = curG;
            int pixB = curB;

            for (int col = 0; col < 15; col++) {
                int r5 = pixR >> 16;
                int g5 = pixG >> 16;
                int b5 = pixB >> 16;

                if (bpp == 16) {
                    unsigned short pixel = (r5 << 11) | (g5 << 6) | b5;
                    *dst16 = pixel;
                    dst16 += 256;  /* binary: stride 0x200 = 512 bytes = 256 shorts */
                }
                else if (bpp == 15) {
                    unsigned short pixel = (r5 << 10) | (g5 << 5) | b5;
                    *dst16 = pixel;
                    dst16 += 256;
                }
                else {
                    /* 8bpp: remap through RGB555 palette table */
                    int rgb555 = (r5 << 10) | (g5 << 5) | b5;
                    *dst8 = g_rgb555Remap[rgb555];
                    dst8 += 256;  /* binary: stride 0x100 */
                }

                pixR += colStepR;
                pixG += colStepG;
                pixB += colStepB;
            }

            /* Advance to next row within strip */
            if (bpp >= 15) {
                dst16 -= 256 * 15 - 1;  /* back to column start, advance 1 row */
            }
            else {
                dst8 -= 256 * 15 - 1;
            }

            curR += stepR; curG += stepG; curB += stepB;
            curR2 += stepR2; curG2 += stepG2; curB2 += stepB2;
        }

        /* Advance to next strip — for 16bpp at +0x1E000, strips are side-by-side
         * (8 strips × 16 cols = 128 cols within 16 rows). dst16 already advanced
         * by 16 from the row loop, so no additional jump needed.
         * For 8bpp at +0xF000, strips occupy separate 16-row blocks. */
        if (bpp >= 15) {
            /* dst16 already at correct position — no adjustment */
        }
        else {
            dst8 += 256 * 16 - 16;
        }
    }
}
#endif /* !SONICR_DC */
