/**
 * fade_overlay.c — Fade iris overlay
 *
 * RenderIrisQuad  — FUN_00461824 — 472 bytes
 * RenderFadeOverlay   — FUN_00461df4 — 1000 bytes
 *
 * The fade effect is a rotating iris/iris made of 32 black quad
 * segments arranged in a ring. The inner ring scales with g_fadeLevel
 * (closing/opening the iris). The outer ring extends past the screen
 * edges. A slow rotation is applied based on g_totalFrames2.
 *
 * The vertex table (DAT_004fc008) contains two concentric circles
 * of 32 vertices each: inner radius ~400, outer radius ~500.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "r_types.h"
#include "r_state.h"
#include "r_draw.h"

#ifdef SONICR_PSP
extern int R_IsFrameOpen(void);
#endif

/* Iris vertex table — DAT_004fc008
 * 64 pairs of (X, Y) ints. First 32 = inner ring, next 32 = outer ring.
 * Extracted from SONICR.EXE .data section. */
static const int s_irisVerts[128] = {
    /* Inner ring (32 vertices, radius ~400) */
        0,  400,   -78,  392,  -153,  369,  -222,  332,
     -282,  282,  -332,  222,  -369,  153,  -392,   78,
     -399,    0,  -392,  -78,  -369, -153,  -332, -222,
     -282, -282,  -222, -332,  -153, -369,   -78, -392,
        0, -399,    78, -392,   153, -369,   222, -332,
      282, -282,   332, -222,   369, -153,   392,  -78,
      399,    0,   392,   78,   369,  153,   332,  222,
      282,  282,   222,  332,   153,  369,    78,  392,
    /* Outer ring (32 vertices, radius ~500) */
        0,  500,   -97,  490,  -191,  461,  -277,  415,
     -353,  353,  -415,  277,  -461,  191,  -490,   97,
     -499,    0,  -490,  -97,  -461, -191,  -415, -277,
     -353, -353,  -277, -415,  -191, -461,   -97, -490,
        0, -499,    97, -490,   191, -461,   277, -415,
      353, -353,   415, -277,   461, -191,   490,  -97,
      499,    0,   490,   97,   461,  191,   415,  277,
      353,  353,   277,  415,   191,  461,    97,  490,
};

/* Quad screen coords — used as temp storage by RenderFadeOverlay */
static int s_quadX0;      /* 0x008FB794 */
static int s_quadY0;      /* 0x008FB798 */
static int s_quadX1;      /* 0x008FB7B0 */
static int s_quadY1;      /* 0x008FB7B4 */
static int s_quadX2;      /* 0x008FB7CC */
static int s_quadY2;      /* 0x008FB7D0 */
static int s_quadX3;      /* 0x008FB7E8 */
static int s_quadY3;      /* 0x008FB7EC */

/**
 * RenderIrisQuad — FUN_00461824 — 472 bytes
 * Submits one black quad via the immediate-mode render API.
 * Untextured, opaque black, flat depth.
 *
 * params: 4 screen-space corner positions + depth value.
 */
void RenderIrisQuad(float x0, float y0, float x1, float y1,
                        float x2, float y2, float x3, float y3,
                        float depth)
{
    /* Self-contained: this used to inherit whatever texture the previous draw
     * left bound. RenderFadeOverlay happened to set -1 before its own calls, so
     * the iris was fine, but DrawSplitBorder did not — and its comment claimed
     * this function already did it.
     *
     * On DC the consequence was intermittent: the verts carry alpha 0xFF so the
     * prim takes the PT route, PT alpha-tests against 0xFE, and the UVs sample
     * one texel near the middle of whatever atlas was bound. Colour is black
     * either way under MODULATE, but ALPHA comes from that texel — land on a
     * colour-keyed one and every pixel is discarded. The split-screen
     * separators appeared and disappeared depending on what drew last. */
    R_SetTexture(-1);
    R_SetTexEnv(R_TEXENV_MODULATE);

    float z = (g_farClipFloat > 0.0f) ? (depth / g_farClipFloat) : 0.5f;
    float invZ = 1.0f / depth;
    uint32_t black = 0xFF000000;

    RenderVertex v[4] = {
        { x0, y0, z, invZ, black, 0, 0.5f,       0.5f },
        { x1, y1, z, invZ, black, 0, 0.5100098f, 0.5f },
        { x2, y2, z, invZ, black, 0, 0.5100098f, 0.5100098f },
        { x3, y3, z, invZ, black, 0, 0.5f,       0.5100098f },
    };

    R_DrawQuad(v);
}

/**
 * RenderFadeOverlay — FUN_00461df4 — 1000 bytes
 * Builds and renders the fade iris: a 32-segment iris ring of black quads.
 *
 * The inner ring vertices scale with g_fadeLevel (0 = fully open, -256 = fully closed).
 * The outer ring vertices are rotated by sin/cos derived from g_totalFrames2.
 * Each segment is a quad connecting adjacent inner and outer vertices.
 *
 * The rotation creates a subtle spinning effect during fade transitions.
 */
void RenderFadeOverlay(void)
{
    /* Binary checks g_tpageStateArray[g_tpageCharacters] == 0x04 here,
     * but the fade draws untextured black quads — no tpage needed.
     * In our GL build, the tpage may not be ready yet during race-start
     * fade-in, causing the iris to run invisibly and complete before
     * the first visible frame. Skip the check. */

#ifdef SONICR_PSP
    int openedFrame = 0;
    if (!R_IsFrameOpen()) {
        BeginFrame();
        openedFrame = 1;
    }
#else
    int openedFrame = 1;
    BeginFrame();
#endif
    R_SetTexture(-1);         /* untextured — flat black quads */
    R_SetTexEnv(R_TEXENV_MODULATE);

    /* Compute fade scale: maps g_fadeLevel (-256..0) to an iris size (0..256) */
    int sign = (g_fadeLevel * -300) >> 31;
    int fadeScale = (int)((g_fadeLevel * -300 + sign * -0x100) -
                   (unsigned int)((sign << 7) < 0)) >> 8;
    if (fadeScale > 0x100) {
        fadeScale = 0x100;
    }

    /* Rotation angle from frame counter */
    int angleIdx = (g_totalFrames2 & 0x7F) * -0x20 + 0xFFF;
    int sinVal = g_sinTable[angleIdx];
    int cosVal = g_cosTable[angleIdx];

    fadeScale = 0x100 - fadeScale;
    const int *tablePtr = s_irisVerts;

    /* First loop: compute inner ring screen positions (32 vertices) */
    int screenX[64];
    int screenY[64];
    for (int i = 0; i < 32; i++) {
        int scaledY = tablePtr[1] * fadeScale >> 5;
        int scaledX = (tablePtr[0] * fadeScale >> 5) * 0x140 / 0xF0;
        int rotX = cosVal * scaledX;

        screenX[i] = (((rotX - scaledY * sinVal) >> 6) *
                      g_projScaleXCurrent >> 0x14) + g_screenCenterX;
        screenY[i] = g_screenCenterY -
                     (((scaledY * cosVal + scaledX * sinVal) >> 6) *
                      g_projScaleY >> 0x14);
        tablePtr += 2;
    }

    /* Second loop: compute outer ring screen positions (32 vertices) */
    for (int i = 32; i < 64; i++) {
        int rawX = tablePtr[0];
        int rawY = tablePtr[1];

        int rotated = rawX * cosVal - rawY * sinVal;
        int rotSign = rotated >> 31;
        int projX = (int)((rotated + rotSign * -0x1000) -
                   (unsigned int)((rotSign << 11) < 0)) >> 12;

        int rotated2 = rawY * cosVal + rawX * sinVal;
        int rot2Sign = rotated2 >> 31;
        int projY = (int)((rotated2 + rot2Sign * -0x1000) -
                   (unsigned int)((rot2Sign << 11) < 0)) >> 12;

        int sx = projX * g_projScaleXCurrent;
        int sxSign = sx >> 31;
        screenX[i] = ((int)((sx + sxSign * -0x200) -
                     (unsigned int)((sxSign << 8) < 0)) >> 9) + g_screenCenterX;

        int sy = projY * g_projScaleY;
        int sySign = sy >> 31;
        screenY[i] = g_screenCenterY -
                     ((int)((sy + sySign * -0x200) -
                     (unsigned int)((sySign << 8) < 0)) >> 9);

        tablePtr += 2;
    }

    /* Third loop: emit 32 quad segments */
    int segIdx = 0;
    int outerIdx = 0x21;  /* starts at 33 (wraps via % 32 + 32) */
    int innerOff = 0;
    int outerOff = 32;

    for (segIdx = 0; segIdx < 32; segIdx++) {
        s_quadX0 = screenX[innerOff + segIdx];
        s_quadY0 = screenY[innerOff + segIdx];
        s_quadX1 = screenX[outerOff + segIdx];
        s_quadY1 = screenY[outerOff + segIdx];

        int outerNext = outerIdx % 32 + 32;
        s_quadX2 = screenX[outerNext];
        s_quadY2 = screenY[outerNext];

        int innerNext = (segIdx + 1) % 32;
        s_quadX3 = screenX[innerNext];
        s_quadY3 = screenY[innerNext];

        /* Viewport clip check — skip if entirely outside screen */
        if (((g_clipLeft <= s_quadX0 || g_clipLeft <= s_quadX1 ||
              g_clipLeft <= s_quadX2 || g_clipLeft <= s_quadX3) &&
             (g_clipTop <= s_quadY0 || g_clipTop <= s_quadY1 ||
              g_clipTop <= s_quadY2 || g_clipTop <= s_quadY3) &&
             (s_quadX0 <= g_clipRight || s_quadX1 <= g_clipRight ||
              s_quadX2 <= g_clipRight || s_quadX3 <= g_clipRight) &&
             (s_quadY0 <= g_clipBottom || s_quadY1 <= g_clipBottom ||
              s_quadY2 <= g_clipBottom || s_quadY3 <= g_clipBottom)))
        {
            RenderIrisQuad(
                (float)s_quadX0, (float)s_quadY0,
                (float)s_quadX1, (float)s_quadY1,
                (float)s_quadX2, (float)s_quadY2,
                (float)s_quadX3, (float)s_quadY3,
                1.1f); /* 0x3f8ccccd == 1.1f */
        }

        outerIdx++;
    }

    if (openedFrame) {
        EndFrame();
    }
}

/**
 * UpdateFade — 0x004305D4
 * Increments or decrements g_fadeLevel toward the target.
 *
 * FADE_IN (1):  g_fadeLevel increases toward 0 (fully visible)
 * FADE_OUT (2): g_fadeLevel decreases toward -256 (fully black)
 *
 * When the target is reached, g_fadeState is set to FADE_VISIBLE (0).
 * g_fadeSpeed controls the rate (typically 0x0C per frame).
 */
void UpdateFade(void)
{
    if (g_fadeState == FADE_IN) {
        g_fadeLevel += g_fadeSpeed;
        if (g_fadeLevel >= 0) {
            g_fadeLevel = 0;
            g_fadeState = FADE_VISIBLE;
        }
    }
    else if (g_fadeState == FADE_OUT) {
        g_fadeLevel -= g_fadeSpeed;
        if (g_fadeLevel <= -256) {
            g_fadeLevel = -256;
            g_fadeState = FADE_VISIBLE;  /* 0x430611: mov [0x901c48], ecx (ecx=0) */
        }
    }
}
