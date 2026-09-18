/**
 * nearclip.h — Near-plane triangle clipping for integer SrcVertex data
 *
 * Clips triangles against Z = 1 (near plane). Used by both the character
 * renderer (render_character_d3d.c) and track renderer (render_track_d3d.c).
 */
#ifndef NEARCLIP_H
#define NEARCLIP_H

#include "sonicr_globals.h"
#include "vertex_struct.h"

typedef struct {
    int screenX, screenY, depth, camX, camY;
    int colorR, colorG, colorB;
    float u, v;
} NearClipVert;

/* DC viewport-edge scissoring.
 *
 * A viewport's real screen edges are culled by PVR at tile boundaries for
 * free; only the virtual split lines need a CPU clip. A 2-player split has
 * exactly ONE such edge. A 4-player quadrant has TWO (e.g. the bottom-left
 * view needs BOTTOM|RIGHT).
 *
 * Set by per-viewport setup code (where g_clipLeft/Right/Top/Bottom is
 * configured) to indicate which edges need CPU clipping. SCISSOR_NONE
 * (the default) means full screen — no extra CPU clip; PVR handles it.
 *
 * DISJOINT BITS so two inward edges can be set at once. Two invariants
 * hold the rest of the renderer together:
 *
 *  - NONE stays 0, so every `g_scissorEdge != SCISSOR_NONE` "am I in
 *    split-screen" test across the renderer keeps working untouched.
 *  - With exactly one bit set, every path below takes the same single-edge
 *    branch it took before the bitmask conversion and does the same work,
 *    so 1- and 2-player output is unchanged.
 */
typedef enum {
    SCISSOR_NONE   = 0,
    SCISSOR_TOP    = 1,  /* keep screenY >= g_clipTop    */
    SCISSOR_BOTTOM = 2,  /* keep screenY <= g_clipBottom */
    SCISSOR_LEFT   = 4,  /* keep screenX >= g_clipLeft   */
    SCISSOR_RIGHT  = 8,  /* keep screenX <= g_clipRight  */
} ScissorEdge;

#define SCISSOR_EDGE_MASK (SCISSOR_TOP | SCISSOR_BOTTOM | SCISSOR_LEFT | SCISSOR_RIGHT)

extern int g_scissorEdge;

/* The edge set the remaining CPU scissor path clips against.
 *
 * g_scissorEdge does double duty: it specifies the clip AND acts as the
 * "am I in split-screen" flag for fog suppression, grid LOD radius, the
 * far-clip reduction and the static-water switch. Those must keep seeing
 * split-screen even where the CPU no longer clips, so the clip specification
 * lives in its own global.
 *
 * After the hardware-clip work (PLAN_DC_HW_TILE_CLIP.md) there is exactly ONE
 * consumer left: the grid path in render_grid_d3d.c, which still CPU-clips on
 * SDL. Everything else was deleted — DC gets its viewport edges from the PVR
 * user tile clip, and near-plane clipping is the only CPU clip there.
 *
 * SDL keeps the CPU path even though its GL scissor makes it redundant; that
 * is untested rather than required, and could go the same way.
 *
 * Set beside g_scissorEdge in hud_full.c: SCISSOR_NONE on DC, g_scissorEdge
 * on SDL. */
extern int g_cpuClipEdge;

/* ---------------------------------------------------------------------------
 * Split-screen shadow toggles — DREAMCAST ONLY, PORT ADDITION.
 *
 * Each viewport is a full geometry pass, so shadow work multiplies by the
 * player count. These can drop the three shadow systems in split-screen only;
 * single-player is untouched, and SDL is untouched at every player count.
 *
 * ALL THREE RESTORED 2026-08-13. They were switched off while DC split-screen
 * was unaffordable — which turned out not to be shadow cost at all. The grid
 * had no viewport reject and was being binned into every pass
 * (project_island_lowquality_crash), and separately the PVR USERCLIP erratum
 * was letting geometry rasterise under the wrong viewport's clip region
 * (project_dc_userclip_erratum). With both fixed, DC runs 2P/3P/4P at a 4/3/2
 * draw-distance ladder, so the shadows are affordable again.
 *
 * Independent on purpose — set any one back to 0 to drop just that system.
 * Each gate reads g_scissorEdge, which is non-zero exactly in split-screen
 * (see below), so the condition is uniform across all three.
 *
 *   SPLIT_FOOT_SHADOWS    the footstep trail behind each character (decoration)
 *   SPLIT_PICKUP_SHADOWS  the blobs under rings and items (decoration)
 *   SPLIT_CHAR_SHADOW     the character's own drop shadow. NOTE this one is
 *                         gameplay-relevant — it is how you judge height above
 *                         ground when jumping — so it is the LAST to drop if
 *                         split-screen ever needs trimming again.
 * ------------------------------------------------------------------------- */
#define SPLIT_FOOT_SHADOWS    1
#define SPLIT_PICKUP_SHADOWS  1
#define SPLIT_CHAR_SHADOW     1

/* True when this system's shadows should be drawn for the current viewport.
 * Split-screen is g_scissorEdge != SCISSOR_NONE. */
#define SHADOWS_ENABLED(which) ((which) || g_scissorEdge == SCISSOR_NONE)

/* Interpolate an edge at the near plane (depth = 1).
 * `behind` has depth < 1, `front` has depth >= 1. */
static inline void NearClipLerp(const NearClipVert *behind, const NearClipVert *front, NearClipVert *out) {
    float t = (1.0f - (float)behind->depth) * reciprocal((float)front->depth - (float)behind->depth);
    out->camX = behind->camX + (int)((float)(front->camX - behind->camX) * t);
    out->camY = behind->camY + (int)((float)(front->camY - behind->camY) * t);
    out->depth = 1;
    out->screenX = g_screenCenterX + g_projScaleXCurrent * out->camX;
    out->screenY = g_screenCenterY - g_projScaleY * out->camY;
#if 0
    out->colorR = behind->colorR + (int)((float)(front->colorR - behind->colorR) * t);
    out->colorG = behind->colorG + (int)((float)(front->colorG - behind->colorG) * t);
    out->colorB = behind->colorB + (int)((float)(front->colorB - behind->colorB) * t);
#endif
    out->colorR = behind->colorR;
    out->colorG = behind->colorG;
    out->colorB = behind->colorB;
    out->u = behind->u + (front->u - behind->u) * t;
    out->v = behind->v + (front->v - behind->v) * t;
}

/* Fill a NearClipVert from a SrcVertex + face polygon UV data.
 * `idx` is the vertex index within the face (0, 1, 2, or 3). */
static inline void NearClipFillVert(NearClipVert *cv, const SrcVertex *v, const int *face, int idx) {
    cv->screenX = v->screenX;
    cv->screenY = v->screenY;
    cv->depth = v->depth;
    cv->camX = v->camX;
    cv->camY = v->camY;
    cv->colorR = v->colorR;
    cv->colorG = v->colorG;
    cv->colorB = v->colorB;
    /* LUT form — the binary's fast paths and the whole character renderer.
     * The 256-entry table at 0x63FCDC carries a parity-dependent offset
     * (even: i/256 + bias, odd: (i+1)/256 - bias) that a flat divide does not
     * reproduce. 112 refs, including RenderTrackD3D's fast path (0x4541BC
     * family) and RenderCharacterD3D (0x462EC0, 22 sites).
     *
     * DIVERGENCE — the binary's track CLIP path does not use the LUT. It
     * converts kept vertices with (fixed >> 16) * 1/255 (0x451198, fmul dword
     * [0x52C288]) and lerped vertices with fixed * 2^-24 (0x450F44, fmul qword
     * [0x52C278]). Neither carries the parity term, so a clipped polygon's UVs
     * disagree with its unclipped neighbour's by up to 0.88 texel — worst at
     * odd index 1 (1.872 vs 1.004) and even index 254 (254.128 vs 254.996).
     * Nearest floors nearly all of that away; bilinear shows every bit of it
     * as a visible texture step along the clip seam.
     *
     * So this fills from the LUT, and lerped vertices come out right for free:
     * NearClipLerp interpolates u/v as floats, and interpolating two
     * LUT-converted corners is the correct linear texture mapping. Same class
     * of deliberate divergence as R_TEXENV_ADD_SIGNED — the D3D path was a
     * hasty port and is not the reference.
     *
     * TRACK SURFACES DO NOT COME THROUGH HERE by default — they call
     * NearClipFillVertTrack (render_track_internal.h), which TRACK_UV_LEGACY
     * points at a flat divide instead. This function is the character
     * renderer's clip path and the accurate track form. */
    cv->u = g_uvLUT256[face[idx * 2] >> 16];
    cv->v = g_uvLUT256[face[idx * 2 + 1] >> 16];
}

/* Rotate a 3-element NearClipVert array so the vertex at `which` moves to [0].
 * which must be 1 or 2. */
static inline void NearClipRotate(NearClipVert cv[3], int which) {
    NearClipVert tmp;
    if (which == 1) {
        tmp = cv[0];
        cv[0] = cv[1];
        cv[1] = cv[2];
        cv[2] = tmp;
    }
    else {
        tmp = cv[2];
        cv[2] = cv[1];
        cv[1] = cv[0];
        cv[0] = tmp;
    }
}



/* Four-edge viewport reject — the NearClipVert form of TrackViewportReject
 * (binary 0x453EED / 0x4546E9). Returns 1 when every vertex falls outside the
 * same clip edge, so the polygon cannot touch this viewport.
 *
 * CULLING, not clipping: a compare per vertex per edge, no lerping and no
 * output buffer. Needed because the CPU scissor used to reject fully-outside
 * polys as a side effect of clipping them to nothing; the PVR tile clip does
 * not — the hardware discards them at raster time, but they still cost list
 * bandwidth and TR staging space to submit.
 *
 * Tests g_clipLeft/Top/Right/Bottom (set per viewport by SetViewportClipRect),
 * NOT g_cpuClipEdge, which is deliberately off on DC.
 *
 * CALLER MUST ensure every vertex is in front of the near plane — screenX/Y
 * are meaningless for verts with depth < 1. */
static inline int ScissorAllOutside(const NearClipVert *in, int n) {
    int outside;

    outside = 1;
    for (int i = 0; i < n; i++) {
        if (in[i].screenX >= g_clipLeft) { outside = 0; break; }
    }
    if (outside) return 1;

    outside = 1;
    for (int i = 0; i < n; i++) {
        if (in[i].screenX <= g_clipRight) { outside = 0; break; }
    }
    if (outside) return 1;

    outside = 1;
    for (int i = 0; i < n; i++) {
        if (in[i].screenY >= g_clipTop) { outside = 0; break; }
    }
    if (outside) return 1;

    outside = 1;
    for (int i = 0; i < n; i++) {
        if (in[i].screenY <= g_clipBottom) { outside = 0; break; }
    }
    return outside;
}

/* Sutherland-Hodgman clip a convex polygon against the near plane (depth=1).
 * in[0..inCount-1] in perimeter order; out[] receives survivors. Returns out
 * vertex count. Caller must ensure out[] has room for at least inCount+1. */
static inline int NearClipPolygon(const NearClipVert *in, int inCount, NearClipVert *out) {
    int outCount = 0;
    for (int i = 0; i < inCount; i++) {
        const NearClipVert *cur = &in[i];
        const NearClipVert *nxt = &in[(i + 1) % inCount];
        int curIn = (cur->depth >= 1);
        int nxtIn = (nxt->depth >= 1);

        if (curIn) {
            out[outCount++] = *cur;
            if (!nxtIn) {
                NearClipLerp(nxt, cur, &out[outCount++]);
            }
        }
        else if (nxtIn) {
            NearClipLerp(cur, nxt, &out[outCount++]);
        }
    }
    return outCount;
}


#endif /* NEARCLIP_H */
