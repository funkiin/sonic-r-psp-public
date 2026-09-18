#ifndef RENDER_TRACK_INTERNAL_H
#define RENDER_TRACK_INTERNAL_H
/**
 * render_track_internal.h — pieces of the track renderer shared by all three
 * translation units of RenderTrackD3D (0x004533C4):
 *
 *   render_track_d3d.c   — shared code: sky/water/parallax/balloon/shadow
 *   render_track_sdl.c   — SDL RenderTrackD3D (integer view matrix + sin tables)
 *   dc/src/render_track_dc.c — DC RenderTrackD3D (float view matrix + FIPR)
 *
 * The two RenderTrackD3D bodies are a deliberate fork along the fixed-point /
 * float axis, not drift. They share 22 globals and the helpers below; keep any
 * change that is about the *translation* (culling rules, traversal order,
 * poly/UV pairing) applied to BOTH.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "nearclip.h"

/* ---------------------------------------------------------------------------
 * Track UV conversion form.
 *
 * The binary indexes g_uvLUT256, whose odd entries expand to (i+1)/256 — a
 * sub-rect spanning UV bytes a..b covers texels a..b+1, its full width, inset
 * UV_LUT_BIAS at each end. Faithful, and what NearClipFillVert does.
 *
 * TRACK_UV_LEGACY selects the form the port used before f745e01 — the texel
 * CENTRE, (i + 0.5)/256, with no parity term. A sub-rect spanning bytes a..b
 * maps to texel centres a+0.5 .. b+0.5, so it renders one texel narrower than
 * its span (3% on a 32-texel tile) and never reaches the boundary texel.
 *
 * Less accurate, but it reads better on the track: the shrink is uniform
 * across every polygon so neighbours still line up, and landing on centres
 * means nearest never coin-flips between adjacent texels the way the edge
 * form does. Note the 0.5 is NOT optional — dropping it to a plain i/256
 * shifts every track UV half a texel and looks visibly wrong.
 *
 * Equivalence: this is exactly the accurate path with UV_LUT_BIAS set to
 * 0.5/256, where the LUT's parity term collapses. At the binary's 0.0005 bias
 * the two forms disagree by up to 0.87 texel on odd indices. So with
 * UV_TEXEL_CENTRE (sonicr_globals.h) set, this flag is a NO-OP — both branches
 * produce (i + 0.5)/256 and the whole game is on one UV form.
 *
 * TRACK SURFACES ONLY. The character renderer, HUD, sprites and weather keep
 * the LUT unconditionally via NearClipFillVert — the binary's own VMAs pin
 * those to the table (0x450BD0, 0x462EC0, …) and they were never part of this.
 * ------------------------------------------------------------------------- */
#define TRACK_UV_LEGACY  0

static inline float TrackUV(int uvFixed)
{
#if TRACK_UV_LEGACY
    return (float)(uvFixed >> 16) * (1.0f / 256.0f) + (0.5f / 256.0f);
#else
    return g_uvLUT256[uvFixed >> 16];
#endif
}

/* Track counterpart to NearClipFillVert: same vertex fill, TRACK_UV_LEGACY
 * picking the UV form. The LUT store NearClipFillVert makes is overwritten
 * with no intervening read, so it dead-stores away in the legacy build. */
static inline void NearClipFillVertTrack(NearClipVert *cv, const SrcVertex *v,
                                         const int *face, int idx)
{
    NearClipFillVert(cv, v, face, idx);
    cv->u = TrackUV(face[idx * 2]);
    cv->v = TrackUV(face[idx * 2 + 1]);
}

/* ROM double constants from binary */
#define FOG_MUL      1275.0                   /* 0x52C2AC */
#define FOG_FAR      0.9                      /* 0x52C2B4 */
#define FOG_NEAR     0.7                      /* 0x52C2BC */

/* Skip a polygon whose vertices are ALL past FOG_FAR — every one of them
 * would take fogAlpha 0, so the whole primitive is invisible.
 *
 * The band is real: fogD is depth / g_farClipTimes8 (track_init.c:790 sets
 * g_farClipFloat = (float)g_farClipTimes8, and that is what invFarSafe is
 * built from), FOG_FAR is 0.9, and the far cull only rejects at 1.0. So
 * everything between 0.9x and 1.0x of the far plane was being submitted fully
 * transparent — and routed to TR, because is_tr is already set by then, so it
 * cost staging bytes, binning and fill for nothing.
 *
 * The grid equivalent is GRID_FULL_FOG_CULL (render_grid_d3d.c), on since it
 * was verified there. Set to 0 to A/B it.
 */
#ifndef TRACK_FULL_FOG_CULL
#define TRACK_FULL_FOG_CULL 1
#endif
#define FOG_NEG_FAR  (-0.9)                   /* 0x52C2C4 */

/* Screen-space backface cross product for fully-projected polys.
 *
 * Binary 0x453da1 (quad) / 0x4545f7 (tri) build this with 32-bit `sub` and
 * `imul`, so every step wraps modulo 2^32 and the final `test`/`jge` reads the
 * wrapped result as signed. Grazing polys project to very large screen coords,
 * and the products do overflow there — the original's cull decision depends on
 * the wrap, so reproducing it exactly means staying in 32 bits rather than
 * widening to int64. Computed in uint32_t because signed overflow is UB in C;
 * the cast back to int32_t reproduces the signed `jge`. */
static inline int32_t TrackBackfaceCross(int aY, int bY, int cX, int bX,
                                         int aX, int cY)
{
    uint32_t p0 = ((uint32_t)aY - (uint32_t)bY) * ((uint32_t)cX - (uint32_t)bX);
    uint32_t p1 = ((uint32_t)aX - (uint32_t)bX) * ((uint32_t)cY - (uint32_t)bY);
    return (int32_t)(p0 - p1);
}

/* Backface cross with the mirror-mode sign flip the binary applies.
 *
 * g_mirrorMode (0x6E9920) selects between two operand orders that converge on
 * the same fmulp/fsubp pair, inverting the sign of the cross product. Present
 * in the fast path at 0x453CE4 / 0x453E49 / 0x454639 and at four sites in the
 * quad clip submit; both variants cull on a negative result. Argument order
 * matches TrackBackfaceCross above. */
static inline int32_t TrackBackfaceCrossM(int aY, int bY, int cX, int bX,
                                          int aX, int cY)
{
    int32_t c = TrackBackfaceCross(aY, bY, cX, bX, aX, cY);
    return g_mirrorMode ? -c : c;
}

int TrackViewportReject(SrcVertex *const *pv, int n);

/* Radiant Emerald per-object vertex recolour — binary 0x453B47-0x453BD9,
 * inline in RenderTrackD3D between an object's vertex transform loop and its
 * polygon loop, behind `cmp [0x8FB8EC], 5`.
 *
 * Emerald's track colours are a sin-table function of world position offset by
 * three phase counters. Those phases advance ONCE PER FRAME in the main race
 * loop (0x4CF18E), not per viewport — only the lookup lives here.
 *
 * Two things make this cheap in the binary and expensive if it is lifted out:
 * it runs per OBJECT, so only objects that survived the bounding-sphere and
 * screen-rect culls pay for it, and it skips vertices the transform left behind
 * the camera (0x453B7B: `cmp [eax+0x30], 0` / `jle`). The port previously did
 * it as one pass over the whole vertex array hoisted to the top of each
 * viewport, which recoloured every vertex in the track once per viewport —
 * four times a frame at 4P, most of it on geometry that was never drawn.
 *
 * Colour is 13-bit fixed point; `<< 6` plus the 0x100020 bias is the binary's
 * `shl 6` / `add esi` pair. See [[feedback_colortint_scale]]. */
static inline void TrackEmeraldRecolourObject(SrcVertex *base, unsigned int count)
{
    for (unsigned int i = 0; i < count; i++) {
        SrcVertex *v = &base[i];

        if (v->depth <= 0) {
            continue;                       /* 0x453B7B */
        }

        v->colorR = (g_sinTable[(v->posX + g_emeraldSineOffX) & 0xFFF] << 6) + 0x100020;
        v->colorG = (g_sinTable[(v->posY + g_emeraldSineOffY) & 0xFFF] << 6) + 0x100020;
        v->colorB = (g_sinTable[(v->posZ + g_emeraldSineOffZ) & 0xFFF] << 6) + 0x100020;
    }
}

#endif /* RENDER_TRACK_INTERNAL_H */
