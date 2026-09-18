/**
 * title_render.c — Title screen logo rendering
 *
 * RenderTitleLogo — FUN_004dcb14 — 1207 bytes
 * Renders the "SONIC R" title logo as a perspective-projected textured quad.
 * The quad rotates slowly using g_pressStartAngle and bobs up/down with
 * a sine wave from g_totalFrames. Per-vertex colors animate from the
 * title color interpolation arrays.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "r_types.h"
#include "r_state.h"
#include "r_draw.h"
#include <math.h>

/* Title animation state */

extern int g_lightingRamp13bit[188*3];

/* Env-mapped 3D model data extracted from PE (envmap_model_data.c).
 * 7 entries: idx 0 = "R" letter (title), idx 1..6 = trophies/placements. */
extern const int *s_envMapVtxTable[7];
extern const int *s_envMapPolyTable[7];
extern const int s_envMapVtxCount[7];
extern const int s_envMapPolyCount[7];

/* UV lookup table (256 floats, computed by InitUVLUT / FUN_0044ea40) */
float g_uvLUT256[256];  /* DAT_0063FCDC — shared with DrawOtherParticles */

/* InitUVLUT — FUN_0044ea40
 *
 * Every UV in the track, character, HUD and sprite renderers is a lookup into
 * this table indexed by (uvFixed >> 16); the binary never computes UVs inline.
 * Called once from startup init (binary 0x4cdbfe, immediately before the ghost
 * time reconcile at 0x4cdc03).
 *
 * The binary's edge inset is 0.0005; UV_LUT_BIAS widens it to half a texel on
 * DC so bilinear stops sampling across atlas sub-texture boundaries. See the
 * comment on UV_LUT_BIAS in sonicr_globals.h. */
void InitUVLUT(void) {
    const float scale = 0.00390625f;   /* 1/256 */
    const float even_off = UV_LUT_BIAS;
    const float odd_off = -UV_LUT_BIAS;
    for (int i = 0; i < 256; i++) {
        float val = (float)((i & 1) + i) * scale;
        val += (i & 1) ? odd_off : even_off;
        g_uvLUT256[i] = val;
    }
}

/* ============================================================================
 * EnvMapComputeVertexUV — step 3e helper for RenderEnvMappedModel3D.
 *
 * DIVERGENCE FROM BINARY — PLEASE READ:
 *   The binary function at 0x00468744 does NOT call a helper. It INLINES this
 *   entire algorithm three times per triangle (once per vertex), across the
 *   binary address ranges:
 *       v0:  0x00468d49..0x004690d1  (~908 bytes)
 *       v1:  0x004690dc..0x0046947d  (~929 bytes)
 *       v2:  0x00469488..0x004697ed  (~869 bytes)
 *   All three ranges are algorithmically identical — they differ only in:
 *       - which vertex pointer register holds the vtx[] base (esi/ebx/ecx)
 *       - which stack slots hold the scratch locals (ebp-0x18 vs -0x1c vs
 *         -0x20 for the isqrt result int, -0x28 vs -0x24 vs -0x30 for
 *         the save of the camera-Z, etc.)
 *   We collapse all three into ONE C function, passing the vertex pointer.
 *   The modelStruct pointer is shared across all three calls (and all
 *   vertices within a triangle), so it's also passed in.
 *
 *   This collapse is a pragmatic C-readability choice. It doesn't change any
 *   observable semantics: same math, same inputs, same outputs, same
 *   arithmetic precision, same handling of overflow/sign edge cases. The
 *   binary's three copies were almost certainly Watcom's register allocator
 *   choosing to unroll rather than emit a call — the algorithm is pure
 *   function-of-vertex, which is exactly what a C helper expresses.
 *
 * ALGORITHM OVERVIEW:
 *   1. Rotate the vertex normal by the model's inverse-rotation matrix:
 *        (tn0, tn1, tn2) = M * (N.x, -N.y, N.z)  (note the N.y sign flip)
 *   2. Compute a simplified camera-space view direction to the vertex:
 *        vd = -cam / |cam|  where cam = (vtx[6], vtx[7], vtx[8])
 *      vtx[6] and vtx[7] are ALWAYS 0 (never written, zero in ROM), so
 *      this reduces to (0, 0, -4096*sign(Z)) — a unit view along -Z.
 *      The binary computes the full formula; we do too, for literalism.
 *   3. Compute dot(tn, vd) / 4096.
 *   4. If dot >= 0 (front-facing): r = 2*dot*tn - vd (expressed as
 *      tn*dot/2048 - vd due to Q12 fixed-point scaling). facing=1.
 *      Else (back-facing): r = -vd. facing=0.
 *   5. Normalize (rX, rY) to length ~4096 using 2D magnitude isqrt.
 *   6. Map the 2D-normalized (rX, rY) to texture U/V via two different
 *      affine formulas based on `facing`. Back-facing uses a straight
 *      (norm + 0x1000) * 0x400 + 0x200 (plain sphere-map); front-facing
 *      additionally scales by isqrt(refZ*2 + 0x2000) — this is the Watcom
 *      cheat for a "cheaper" env-map that's close enough for a rotating
 *      logo.
 *
 * INPUTS:
 *   vtx          — pointer to one int[16] entry in the vertex work buffer:
 *                    [3..5]  = ROM vertex normal (Q12, ≈ 4096 magnitude)
 *                    [6..7]  = ROM zeros (used as "fake cam X/Y")
 *                    [8]     = transformed Z (camera distance), written
 *                              earlier by the vertex loop (step 3c)
 *   modelStruct  — pointer to the per-model scratch; reads from
 *                    modelStruct[8..18] which holds the 3×3 rotation
 *                    matrix copied in by step 3b. Indices map to byte
 *                    offsets +0x20..+0x48 in the binary:
 *                      modelStruct[8+0] = mod[+0x20]  (row 0, col 0: N.x→tn0)
 *                      modelStruct[8+4] = mod[+0x30]  (row 0, col 1: -N.y→tn0)
 *                      modelStruct[8+8] = mod[+0x40]  (row 0, col 2: N.z→tn0)
 *                      (and similarly for tn1 at +1/+5/+9, tn2 at +2/+6/+10)
 *
 * OUTPUTS (written destructively into vtx[]):
 *   vtx[12]  = texture U (Q10 fixed-point, tpage address space)
 *   vtx[13]  = texture V
 *   vtx[14]  = 1 (cache-valid flag — skips recompute on neighboring triangles)
 *
 * NUMERIC NOTES ON THE `sqrtf` CALLS:
 *   The binary uses `fild; fsqrt; call 0x4e08ca; fistp` — a 3-step pattern
 *   for (int)sqrt(int). 0x4e08ca is Watcom's `__CHP`, a frndint with truncate-
 *   toward-zero rounding. For non-negative inputs, `(int)sqrtf((float)x)` in
 *   C (which truncates toward zero on integer cast) produces the same result.
 *   Inputs here are always non-negative (squared magnitudes, and a bounded
 *   refZ*2+0x2000 which stays in [0, 0x4000] for |refZ| <= 0x1000).
 * ============================================================================ */
static void EnvMapComputeVertexUV(int *vtx, const int *modelStruct)
{
    /* 0x468d49..0x468d88 (v0 copy): first row of tn = M * (N.x, -N.y, N.z).
     * Binary operation order: load -N.y into eax, multiply by mod[+0x30];
     * then load N.x, multiply by mod[+0x20], sum; then load N.z, multiply
     * by mod[+0x40], sum; SDIV 4096. We use a single C expression — the
     * order of additions doesn't matter for int arithmetic. */
    const int N_x = vtx[3];   /* [vtx + 0xc]  */
    const int N_y = vtx[4];   /* [vtx + 0x10] — negated at point of use */
    const int N_z = vtx[5];   /* [vtx + 0x14] */

    const int tn0 = (N_x * modelStruct[8 + 0]      /* * mod[+0x20] */
                     + (-N_y) * modelStruct[8 + 4] /* * mod[+0x30] */
                     + N_z * modelStruct[8 + 8])   /* * mod[+0x40] */
                    / 4096;                        /* 0x468d7b..0x468d85 SDIV */

    /* 0x468d8e..0x468dbd (v0 copy): second row. */
    const int tn1 = (N_x * modelStruct[8 + 1]      /* * mod[+0x24] */
                     + (-N_y) * modelStruct[8 + 5] /* * mod[+0x34] */
                     + N_z * modelStruct[8 + 9])   /* * mod[+0x44] */
                    / 4096;

    /* 0x468dc3..0x468df2 (v0 copy): third row. */
    const int tn2 = (N_x * modelStruct[8 + 2]      /* * mod[+0x28] */
                     + (-N_y) * modelStruct[8 + 6] /* * mod[+0x38] */
                     + N_z * modelStruct[8 + 10])  /* * mod[+0x48] */
                    / 4096;

    /* 0x468df8..0x468e4d: build |cam|² where cam = -(vtx[6], vtx[7], vtx[8]).
     * In ROM, vtx[6] and vtx[7] are always 0 — the binary reads and squares
     * them anyway; we keep the full expression so the compiler folds it
     * rather than us. vtx[8] is the transformed Z written in step 3c. */
    const int cam_x_neg = -vtx[6];  /* [vtx + 0x18] — always 0 */
    const int cam_y_neg = -vtx[7];  /* [vtx + 0x1c] — always 0 */
    const int cam_z_neg = -vtx[8];  /* [vtx + 0x20] = -(transformed Z) */

    const int mag_sq = cam_x_neg * cam_x_neg
                     + cam_y_neg * cam_y_neg
                     + cam_z_neg * cam_z_neg;

    /* 0x468e53..0x468e69: isqrt with /0 guard.
     *   fild; fsqrt; __CHP(trunc); fistp; if 0 then 1. */
    int mag = (int)sr_sqrtf((float)mag_sq);
    if (mag == 0) {
        mag = 1;
    }

    /* 0x468e70..0x468ea9: vd = cam_neg * 4096 / mag.
     * Binary encodes as `shl edx, 0xc; sar edx, 0x1f; idiv [mag]` — a
     * signed int division of (cam_neg << 12) by mag. C equivalent is
     * `(cam_neg * 4096) / mag`, with truncate-toward-zero semantics. */
    const int vdX = (cam_x_neg * 4096) / mag;
    const int vdY = (cam_y_neg * 4096) / mag;
    const int vdZ = (cam_z_neg * 4096) / mag;

    /* 0x468eaf..0x468ee7: dot(tn, vd) / 4096.
     * Binary accumulates edx across three imul/add, then SDIV 4096. */
    const int dot = (tn0 * vdX + tn1 * vdY + tn2 * vdZ) / 4096;

    /* 0x468ee9..0x468eeb: `test eax, eax; jge` dispatches to reflection
     * path (dot >= 0) or simple -vd path (dot < 0). The flag `facing`
     * drives the UV encoding below. */
    int refX;
    int refY;
    int refZ;
    int facing;
    if (dot >= 0) {
        /* 0x468f31..0x468f99 (v0 copy): reflection path.
         *   r = 2 * dot * n - v  expressed as  (tn*dot / 2048) - vd
         * The /2048 plays the role of "2 * /4096" — Q12 × Q12 → Q24,
         * shifted back to Q13 which is effectively *2 compared to Q12.
         * Binary uses the SDIV-by-2048 trick (sbb + sar 0xb); C plain
         * division is equivalent. */
        refX = (tn0 * dot) / 2048 - vdX;
        refY = (tn1 * dot) / 2048 - vdY;
        refZ = (tn2 * dot) / 2048 - vdZ;
        facing = 1;   /* 0x468f99: mov eax, 1 */
    }
    else {
        /* 0x468eed..0x468f2f (v0 copy): simple path — r = -vd.
         * Binary writes each vd to refX/Y/Z, then `neg` and re-writes
         * (two back-to-back stores to the same stack slot). We collapse
         * to a direct negation. */
        refX = -vdX;
        refY = -vdY;
        refZ = -vdZ;
        facing = 0;   /* 0x468f27: xor eax, eax */
    }

    /* 0x468f9e..0x468fdb: prep for the 2D normalization of (refX, refY).
     * Binary computes refX² + refY² and also stashes refX*4096 and
     * refY*4096 in separate slots for the upcoming divides. C expresses
     * the divides inline so we only need the sum-of-squares local. */
    const int mag2d_sq = refX * refX + refY * refY;

    /* 0x468fdd..0x468fe7 (facing=0 path) / 0x469041..0x00469094 (facing=1
     * path): isqrt of mag2d_sq. Binary divisor is always `mag2d + 1` via
     * `inc edi` — guards against /0 without needing a branch. */
    const int mag2d = (int)sr_sqrtf((float)mag2d_sq);
    const int mag2d_divisor = mag2d + 1;

    /* 0x468fed..0x46900d: normalize to ~4096 magnitude. Note the negation
     * on the Y term — binary does `mov edx, temp_refY_shifted; neg edx`
     * before the divide. Equivalent to negating refY first. */
    const int normX = ( refX * 4096) / mag2d_divisor;
    const int normY = (-refY * 4096) / mag2d_divisor;

    int u, v;
    if (facing == 0) {
        /* 0x468fed..0x0046903c: simple UV.
         *   U = (normX + 0x1000) * 0x400 + 0x200
         *   V = (normY + 0x1000) * 0x400 + 0x200
         * Maps the normalized component from [-4096, +4096] to the
         * target texture space. */
        u = (normX + 0x1000) * 0x400 + 0x200;
        v = (normY + 0x1000) * 0x400 + 0x200;
    }
    else {
        /* 0x469077..0x004690c3: reflection UV with refZ-derived scale.
         *   scale = isqrt(refZ * 2 + 0x2000)
         *   U     = normX * scale * 8 + 0x400000
         *   V     = normY * scale * 8 + 0x400000
         * refZ is bounded by the magnitude of the reflection vector; in
         * practice refZ*2 + 0x2000 ∈ [0, 0x4000] for valid inputs. */
        const int refZ_scaled = refZ * 2 + 0x2000;
        const int scale = (int)sr_sqrtf((float)refZ_scaled);
        u = normX * scale * 8 + 0x400000;
        v = normY * scale * 8 + 0x400000;
    }

    /* 0x469026 / 0x4690b4 (U), 0x4690c8 (V), 0x4690cb (cache flag).
     * Both code paths converge on these stores. */
    vtx[12] = u;   /* byte offset 0x30 */
    vtx[13] = v;   /* byte offset 0x34 */
    vtx[14] = 1;   /* byte offset 0x38 — env-map-cache-valid */
}

/* Build a RenderVertex from env-map vertex data (immediate-mode version) */
static RenderVertex BuildEnvMapRenderVertex(const int *vtx, int colorIdx,
                                            int modelIdx, int zNearBias, float farClipDivisor)
{
    RenderVertex rv;
    rv.sx = (float)vtx[9];
    rv.sy = (float)vtx[10];
    float z_cam = (float)(vtx[8] + zNearBias);
    float farClipSafe = (farClipDivisor > 0.0f) ? farClipDivisor : 88064.0f;
    rv.sz = z_cam / farClipSafe;
    rv.rhw = 1.0f / z_cam;

    if (modelIdx == 0) {
        unsigned cr = ((unsigned)g_lightingRamp13bit[colorIdx * 3 + 0]) >> 13;
        unsigned cg = ((unsigned)g_lightingRamp13bit[colorIdx * 3 + 1]) >> 13;
        unsigned cb = ((unsigned)g_lightingRamp13bit[colorIdx * 3 + 2]) >> 13;

        /* jn64: richer color on the "R" model */
#ifdef SONICR_DC
        cr = (cr * cr) >> 8;
        cg = (cg * cg) >> 8;
        cb = (cb * cb) >> 8;
#endif
        rv.color = 0xff000000u | (cr << 16) | (cg << 8) | cb;
    }
    else {
        rv.color = VERTEX_WHITE;
    }
    rv.specular = 0;
    rv.u = g_uvLUT256[vtx[12] >> 16];
    rv.v = g_uvLUT256[vtx[13] >> 16];
    return rv;
}

/**
 * RenderEnvMappedModel3D — FUN_00468744 — 5020 bytes
 * Generic env-mapped 3D letter/trophy model renderer.
 * Title screen uses it for the rotating "R" letter (modelIdx=0).
 * Results screen uses it for the "1" trophy (modelIdx=placement).
 *
 * Parameters (from binary entry prologue at 0x00468744):
 *   xOff      (EAX,  ebp-0x6c) — X projection offset
 *   yOff      (EDX,  ebp-0x74) — Y projection offset
 *   zBase     (EBX,  ebp-0x70) — camera Z distance
 *   rotA      (ECX)            — rotation angle A (trig table index)
 *   rotB      ([ebp+0x08])     — rotation angle B
 *   rotC      ([ebp+0x0c])     — rotation angle C
 *   modelIdx  ([ebp+0x10])     — index into ROM model table at 0x4fc9b8
 *   zNearBias ([ebp+0x14])     — per-vertex Z bias (used in near-clip, far-divide)
 */
void RenderEnvMappedModel3D(int xOff, int yOff, int zBase, int rotA,
                            int rotB, int rotC, int modelIdx, int zNearBias)
{
    /* ------------------------------------------------------------------
     * prologue, viewport save/swap, ROM model-table lookup
     * (covers binary 0x00468744..0x004687fe, ~190 bytes)
     * ------------------------------------------------------------------ */

    /* 0x468758-0x468764: tpage-state gate. If the parallax2 tpage hasn't
     * reached state 4 (fully uploaded), bail via the "fast exit" branch that
     * SKIPS the viewport restore. The binary does this by jumping to
     * 0x469ad7, which is past the restore block. */
    if (g_tpageStateArray[g_tpageParallax2] != 4) {
        return;
    }

    /* 0x46876a-0x4687a0: save 5 viewport globals.
     * g_projScaleX (0x6e98ec, saved to ebp-0xf0) is preserved even though
     * the function never writes it below. Reproducing the binary's behavior
     * exactly — likely a vestigial save from an older revision of the
     * function, but cheap to preserve. */
    const int saved_centerX = g_screenCenterX;           /* 0x6e98d4 */
    const int saved_centerY = g_screenCenterY;           /* 0x6e98d8 */
    const int saved_projScaleX = g_projScaleX;           /* 0x6e98ec */
    const int saved_projScaleXCur = g_projScaleXCurrent; /* 0x6e98f0 */
    const int saved_projScaleY = g_projScaleY;           /* 0x6e98f4 */

    /* 0x4687a1-0x4687c3: UNCONDITIONAL alt-viewport overwrite.
     *   g_dispCenterX    (0x6e98a0) -> g_screenCenterX     (0x6e98d4)
     *   g_dispCenterY    (0x6e98a4) -> g_screenCenterY     (0x6e98d8)
     *   g_dispProjScaleX (0x6e9888) -> g_projScaleXCurrent (0x6e98f0)
     *   g_dispProjScaleY (0x6e988c) -> g_projScaleY        (0x6e98f4)
     * g_projScaleX (0x6e98ec) is intentionally NOT overwritten. */
    g_screenCenterX = g_dispCenterX;
    g_screenCenterY = g_dispCenterY;
    g_projScaleXCurrent = g_dispProjScaleX;
    g_projScaleY = g_dispProjScaleY;

    /* 0x4687c4-0x4687fe: ROM env-map model table at 0x4fc9b8.
     * 7 entries, 5 ints stride (20 bytes). Entry layout:
     *   [+0]  modelStruct  — runtime .bss scratch ptr (one per model:
     *                        0x8f7078, 0x8f70e0, 0x8f7148, 0x8f71b0,
     *                        0x8f7218, 0x8f7280, 0x8f72e8). Used as a
     *                        per-model cache of the rotation matrix +
     *                        origin + flags — re-written every call.
     *   [+4]  polyData     — ROM triangle list (0x50d180+)
     *   [+8]  vtxData      — ROM vertex array  (0x50a280+)
     *   [+12] polyCount
     *   [+16] vtxCount
     *
     * Binary uses 7 independent .bss structs; we emulate with a static
     * per-model scratch array. Each entry is 0x68 (104) bytes in the binary
     * — we round up to 28 ints (112 bytes) and index by modelIdx. */
    static int s_envMapModelScratch[7][28];
    int *const modelStruct = s_envMapModelScratch[modelIdx];
    const int *const polyData = s_envMapPolyTable[modelIdx];
    const int *const vtxDataRO = s_envMapVtxTable[modelIdx];
    const int polyCount = s_envMapPolyCount[modelIdx];
    const int vtxCount = s_envMapVtxCount[modelIdx];

    /* 0x0046880a: mov word ptr [eax + 0x64], 1
     * Writes a 16-bit "valid this frame" flag at byte offset 0x64.
     * int index = 0x64/4 = 25, low 16 bits of that int. */
    ((unsigned short *)modelStruct)[0x64 / 2] = 1;

    /* ------------------------------------------------------------------
     * trig lookups + rotation matrix build
     *           (covers binary 0x00468801..0x00468ad5, ~210 instructions)
     *
     * The binary interleaves register-allocated trig loads with matrix
     * element computation across ~210 instructions. Several writes are
     * dead stores (overwritten a few instructions later) and several
     * arithmetic patterns are shift-dances that collapse algebraically
     * (shl N followed later by sar N with only a negate in between).
     * We preserve the FINAL semantic values; dead stores and shift dances
     * are collapsed and flagged in comments.
     *
     * Also interleaved at 0x46897c..0x468994 is a completely unrelated
     * FPU op computing a per-call far-clip divisor (used later in the
     * poly loop). We hoist it out for clarity.
     * ------------------------------------------------------------------ */

    /* 0x468801-0x468860 + 0x4688cc-0x4688ce + 0x4689a3/0x4689a9/0x4689b7:
     * six pre-scaled trig values. Binary uses pointer arithmetic via
     * 0x51e074 (= &g_cosTable) for cos rather than explicit indexing; C
     * just uses the separate cos table. All 6 are pre-divided by 4 via
     * `sar 2`; this /4 scaling persists into the matrix math below.
     *
     * Caller contract: angles must be pre-masked to [0, 4095]. Binary
     * does NO masking (mirrored here). Title caller masks explicitly;
     * other callers must do likewise. */
    const int sA = g_sinTable[rotA] >> 2;       /* 0x468815, then sar at 0x4689a3 */
    const int cA = g_cosTable[rotA] >> 2;       /* held as ptr, deref 0x4689a9, sar 0x4689b7 */
    const int sB = g_sinTable[rotB] >> 2;       /* 0x468829 → sar 0x46882f */
    const int cB = g_cosTable[rotB] >> 2;       /* held as ptr, deref 0x4688cc, sar 0x4688ce */
    const int sC = g_sinTable[rotC] >> 2;       /* 0x468846 → sar 0x46884c */
    const int cC = g_cosTable[rotC] >> 2;       /* 0x46885b → sar 0x46885d */

    /* 0x46897c-0x468994: interleaved FPU. Hoisted here for clarity.
     * Stored at [ebp-0x34] in the binary; read in the polygon loop to
     * produce per-vertex Z (see step 3f: 0x469902, 0x46998b, 0x469a18).
     * We keep it as a local float declared now so step 3f picks it up. */
    const float farClipDivisor = (float)zNearBias + g_farClipFloat;

    /* ================================================================
     * Matrix construction — 16-int scratch array `mat[]`, then copied
     * to modelStruct[+0x20..+0x5c] via rep movsd. Mirror the layout
     * exactly; the vertex transform in step 3c reads `mat[]` directly
     * and the env-map helper in step 3e reads from modelStruct[].
     *
     * Scratch is a 3x3 rotation matrix stored column-major with 4-int row
     * stride (word slots 3, 7, 11, 15 are padding). Step 3c's vertex loop
     * computes `out = mat * (rx, ry_neg, rz)` (matches step 3e's env-map
     * `tn = mat * (N.x, -N.y, N.z)` — same matrix, different input vector).
     *
     *   w0  → +0x20 → mat[row=0][col=0]  (x-input coef for x-output)
     *   w1  → +0x24 → mat[row=1][col=0]  (x-input coef for y-output)
     *   w2  → +0x28 → mat[row=2][col=0]  (x-input coef for z-output)
     *   w3  → +0x2c   (padding — uninit in binary; we zero)
     *   w4  → +0x30 → mat[row=0][col=1]  (negY-input coef for x-output)
     *   w5  → +0x34 → mat[row=1][col=1]
     *   w6  → +0x38 → mat[row=2][col=1]
     *   w7  → +0x3c   (padding)
     *   w8  → +0x40 → mat[row=0][col=2]  (z-input coef for x-output)
     *   w9  → +0x44 → mat[row=1][col=2]
     *   w10 → +0x48 → mat[row=2][col=2]
     *   w11 → +0x4c   (padding)
     *   w12 → +0x50 → 4th column row 0 — always 0 (binary writes zero,
     *                 never updates; presumably a translation slot)
     *   w13 → +0x54 → same
     *   w14 → +0x58 → same
     *   w15 → +0x5c   (padding)
     *
     * DIVERGENCE: binary init (0x468866..0x4688bf) writes transient
     * placeholder values into several of these slots that are all
     * overwritten before use. We skip the placeholder writes — the
     * {0} zero-init plus the explicit final assignments below yield
     * identical end-state. This is the only place this step 3b
     * diverges line-for-line from the binary. No observable difference.
     * ================================================================ */
    int mat[16] = {0};

    /* 0x46896a..0x46896d: w0 = (cC * cB) >> 12
     *   temp_c0 at 0x4688dc = cC_adj * cB_adj; shifted at 0x46896a. */
    mat[0] = (cC * cB) >> 12;

    /* 0x4689db..0x468a8a: w1 = ((sC*cB >> 12)*cA + sB*sA) >> 12
     *   Intermediate written to scratch[w1] at 0x46897f = (sC*cB)>>12,
     *   then read back at 0x4689db to compute the full expression at
     *   0x4689ef (= temp_bc_new), finally shifted at 0x468a81 and stored
     *   at 0x468a8a. The intermediate write is a dead store. */
    {
        const int sC_cB_hi = (sC * cB) >> 12;   /* 0x46897f intermediate */
        const int t = sC_cB_hi * cA + sB * sA;  /* 0x4689e4 (with sign flip on sub) */
        mat[1] = t >> 12;                       /* 0x468a81/0x468a8a */
    }
    /* Note: 0x4689db reads [-0x134]; at that moment it still holds the
     * intermediate (sC*cB)>>12 from 0x46897f. The sub at 0x4689e4 is
     *   esi(= sC_cB_hi*cA) - edi(= -sB*sA), which equals +sB*sA add. */

    /* 0x468aa2: w2 = (-sB*cA + (sC*cB >> 12)*sA) >> 12
     *   temp_b8 at 0x468a12 = -sB*cA + (sC*cB)>>12 * sA. */
    {
        const int sC_cB_hi = (sC * cB) >> 12;
        const int t = (-sB) * cA + sC_cB_hi * sA;  /* 0x4689ec + 0x4689fe */
        mat[2] = t >> 12;                          /* 0x468aa2 */
    }

    /* mat[3] padding — already 0 from zero-init */

    /* 0x468997: w4 = -sC
     *   Binary arrives here via shift dance: (sC * 0x1000) stored to
     *   temp_90, negated, then sar 12 — algebraically equal to -sC.
     *   Collapsed. */
    mat[4] = -sC;

    /* 0x468ab7: w5 = (cC * cA) >> 12
     *   temp_88 at 0x468a09 = cC*cA; mirrored to temp_8c at 0x468a32;
     *   sar at 0x468ab1. */
    mat[5] = (cC * cA) >> 12;

    /* 0x468acf: w6 = (cC * sA) >> 12
     *   temp_a0 at 0x468a57; sar at 0x468ac9. */
    mat[6] = (cC * sA) >> 12;

    /* mat[7] padding — already 0 */

    /* 0x4689c3: w8 = (sB * cC) >> 12
     *   temp_b4 at 0x46894b = sB*cC; sar at 0x4689ba. */
    mat[8] = (sB * cC) >> 12;

    /* 0x468a84: w9 = ((sB*sC >> 12)*cA - cB*sA) >> 12
     *   Intermediate [-0x114] at 0x4689d5 = (sB*sC)>>12 (dead store,
     *   overwritten at 0x468a84), read back at 0x468a40, then computed
     *   at 0x468a63 as ((sB*sC)>>12 * cA) - (cB * sA). */
    {
        const int sB_sC_hi = (sB * sC) >> 12;      /* 0x4689d2 */
        const int t = sB_sC_hi * cA - cB * sA;     /* 0x468a46 - 0x468a63 */
        mat[9] = t >> 12;                          /* 0x468a7e/0x468a84 */
    }

    /* 0x468ac3: w10 = (cB*cA + sA * (sB*sC >> 12)) >> 12
     *   eax at 0x468a54 = cB*cA; edx at 0x468a67 = sA * (sB*sC)>>12
     *   (reading [-0x114] which still holds the intermediate at that
     *   point because the final-word-9 write hasn't happened yet).
     *   Sum at 0x468a76; shift at 0x468aae. */
    {
        const int sB_sC_hi = (sB * sC) >> 12;
        const int t = cB * cA + sA * sB_sC_hi;
        mat[10] = t >> 12;
    }

    /* mat[11], mat[12..14], mat[15] — already 0 from zero-init.
     * Binary explicitly writes zeroes to w12..w14 at 0x4688b3..0x4688bf. */

    /* 0x468a6e: ebx (vtxDataPtr) saved to [-0x78] for the poly loop.
     * We keep `vtxDataRO` in scope; no extra save needed. */

    /* 0x468a96..0x468ad8: rep movsd 0x10 dwords: scratch → modelStruct+0x20.
     * `memcpy` is semantically identical. Writing 16 ints starting at
     * modelStruct word index 8 (= byte offset 0x20). */
    memcpy(&modelStruct[8], mat, sizeof(mat));

    /* ------------------------------------------------------------------
     * vertex transform / projection loop
     *           (covers binary 0x00468ada..0x00468c2a)
     *
     * Each ROM vertex is 16 ints (64 bytes). Layout:
     *   [0..2]  raw (X, Y, Z)
     *   [3..5]  unit vertex normal (Q12 fixed-point, magnitude ≈ 4096)
     *   [6..7]  unused (0 in ROM)           <-- env-map reads [6]/[7] as 0
     *   [8..10] transformed (Z+zBase, screenX, screenY) — WRITTEN here
     *   [11]    unused
     *   [12..13] env-map U, V              — WRITTEN later (step 3e)
     *   [14]    env-map cache-valid flag   — cleared here, set in step 3e
     *   [15]    unused
     *
     * The binary transforms vertices destructively in the writable
     * .bss vertex array. C can't write to `const int *vtxDataRO` (which
     * points to a ROM blob), so we maintain a static writable workspace
     * sized to the largest model. Copy ROM → work, then transform in
     * place in the work buffer. Both the poly loop (step 3d/3e) and
     * the vertex submit (step 3f) read from the work buffer.
     *
     * Note: the binary writes ONLY vtx[+0x20..0x28] and vtx[+0x38] here.
     * The env-map cache-valid flag at [+0x38] is cleared at 0x468c1f
     * (NOT in a conditional — ALL vertices get the cache cleared every
     * frame, forcing env-map recomputation even for Z<=0 vertices).
     * ------------------------------------------------------------------ */

    /* Writable vertex workspace — sized to fit the largest model (idx 6
     * will be 205 verts per the ROM table once step 4 extracts it).
     * 256 verts × 16 ints = 4096 ints = 16 KiB .bss. */
    static int s_vtxWork[256 * 16];
    memcpy(s_vtxWork, vtxDataRO, (size_t)vtxCount * 16 * sizeof(int));
    {
        int *vtx = s_vtxWork;
        for (int vi = 0; vi < vtxCount; ++vi, vtx += 16) {
            /* 0x468ada..0x468b0f: SDIV8 of raw X, -Y, Z.
             * Binary Y-negation happens BEFORE the /8 — algebraically
             * equivalent because (-Y)/8 == -(Y/8) for two's-complement
             * truncate-toward-zero divide. We write it to match the
             * binary operation order. */
            const int rx     =  vtx[0] / 8;       /* 0x468ada SDIV8 */
            const int ry_neg = -vtx[1] / 8;       /* 0x468aec neg + SDIV8 → -Y/8 */
            const int rz     =  vtx[2] / 8;       /* 0x468aff SDIV8 */

            /* 0x468b11..0x468b52: Z-out via matrix row [mat[2], mat[6], mat[10]]
             *   Z_world = (rx*mat[2] + ry_neg*mat[6] + rz*mat[10]) / 4096 */
            const int z_world =
                (rx * mat[2] + ry_neg * mat[6] + rz * mat[10]) / 4096;

            /* 0x468b55..0x468b5d: z_out = zBase + z_world; written to vtx[+0x20] */
            const int z_out = zBase + z_world;
            vtx[8] = z_out;                       /* byte offset 0x20 */

            /* 0x468b60..0x468b62: if (z_out <= 0) skip projection.
             * Binary jumps to 0x468c19 (cache-clear + loop advance). */
            if (z_out > 0) {
                /* 0x468b68..0x468b9d: X-out via matrix row [mat[0], mat[4], mat[8]]
                 *   X_world = (rx*mat[0] + ry_neg*mat[4] + rz*mat[8]) / 4096 */
                const int x_world =
                    (rx * mat[0] + ry_neg * mat[4] + rz * mat[8]) / 4096;

                /* 0x468ba0..0x468bac: x_world += xOff, save as local_ac */
                const int x_cam = xOff + x_world;

                /* 0x468ba5..0x468bd0: Y-out via matrix row [mat[1], mat[5], mat[9]]
                 * The binary interleaves this with the X save for register
                 * allocation; we compute it as an independent C expression.
                 *   Y_world = (rx*mat[1] + ry_neg*mat[5] + rz*mat[9]) / 4096 */
                const int y_world =
                    (rx * mat[1] + ry_neg * mat[5] + rz * mat[9]) / 4096;

                /* 0x468be2..0x468be8: y_world += yOff (held in ecx) */
                const int y_cam = yOff + y_world;

                /* 0x468bd3..0x468bf8: screen_X = centerX + (x_cam * scaleXC) / z_out
                 * Binary uses signed `idiv edi` where edi = z_out. Plain C
                 * signed division matches (truncate-toward-zero). */
                const int screen_x =
                    g_screenCenterX + (x_cam * g_projScaleXCurrent) / z_out;

                /* 0x468bfa..0x468c16: screen_Y = centerY - (y_cam * scaleY) / z_out
                 * Note the SUBTRACT — Y is flipped (screen Y grows downward). */
                const int screen_y =
                    g_screenCenterY - (y_cam * g_projScaleY) / z_out;

                vtx[9]  = screen_x;               /* byte offset 0x24 */
                vtx[10] = screen_y;               /* byte offset 0x28 */
            }

            /* 0x468c19..0x468c1f: advance to next vertex and clear its
             * env-map cache flag. Binary uses `[ebx - 8] = 0` AFTER adding
             * 0x40 to ebx — effectively clearing vtx[+0x38] on the vertex
             * we JUST processed. Equivalent to clearing vtx[14] here. */
            vtx[14] = 0;                          /* byte offset 0x38 */
        }
    }

    /* ------------------------------------------------------------------
     * poly-loop header: per-triangle rejects
     *           (covers binary 0x00468c30..0x00468d43)
     *
     * The poly loop walks one triangle at a time. Each ROM triangle is
     * 6 ints (0x18 bytes):
     *   [0] v0 index  (scaled × 64 in binary via `shl esi, 6`)
     *   [1] v1 index
     *   [2] v2 index
     *   [3] red   (used in step 3f for per-vertex color)
     *   [4] green
     *   [5] blue
     *
     * The reject pipeline order (matches binary):
     *   1. Near-plane Z for all 3 verts: (vtx.z + zNearBias) < 1
     *   2. Backface cull via 2D cross product of screen-space verts
     *   3. Four bounding-box edges: left/right/top/bottom
     *   4. Env-map cache-valid check (that's the boundary to step 3e)
     *
     * The loop also has an outer guard: skip everything if the per-frame
     * polygon batch (g_polyCount) has already reached its cap
     * (g_maxPolyCount). Binary checks this BOTH as an entry guard
     * (0x468c42) AND as a tail continuation check (0x469a94). We express
     * it as an entry `if` plus a tail `break`.
     * ------------------------------------------------------------------ */

    /* 0x468c30..0x468c3f: save vtx-work base + polyCount into the loop
     * locals used by the binary. In C we just use the names directly. */

    /* 0x468c42..0x468c48: entry guard — skip entire loop if polygon
     * buffer is already full. */
    if (g_maxPolyCount != g_polyCount) {
        const int *pd = polyData;
        for (int pi = 0; pi < polyCount; ++pi, pd += 6) {
            /* 0x468c4e..0x468c92: load 3 vertex pointers. Binary multiplies
             * each index by 64 (byte stride) via `shl 6`; C uses 16-int
             * stride on the work buffer. */
            int *const vA = &s_vtxWork[pd[0] * 16];
            int *const vB = &s_vtxWork[pd[1] * 16];
            int *const vC = &s_vtxWork[pd[2] * 16];

            /* 0x468c5e..0x468c66: near-plane reject for vA.
             * Binary: `(vtx[+0x20] + zNearBias) < 1` means the vertex
             * lies on or behind the near plane. Reject whole triangle
             * if ANY vertex fails. */
            if (vA[8] + zNearBias < 1) {
                continue;      /* 0x468c5e..0x468c66 */
            }
            if (vB[8] + zNearBias < 1) {
                continue;      /* 0x468c77..0x468c7f */
            }
            if (vC[8] + zNearBias < 1) {
                continue;      /* 0x468c96..0x468c9e */
            }

            /* 0x468ca4..0x468cd5: 2D backface cull via cross product of
             * screen-space edges at vB:
             *   cross = (vA.y - vB.y) * (vC.x - vB.x)
             *         - (vC.y - vB.y) * (vA.x - vB.x)
             * Reject if cross < 0 (back-facing under binary's winding). */
            const int cross =
                (vA[10] - vB[10]) * (vC[9] - vB[9]) -
                (vC[10] - vB[10]) * (vA[9] - vB[9]);
            if (cross < 0) {
                continue;
            }

            /* 0x468cdb..0x468d39: bounding-box clip. Each edge rejects
             * only if ALL 3 verts fall entirely outside that edge.
             * Matches the "trivial reject" convention. */
            if (vA[9] < g_clipLeft && vB[9] < g_clipLeft && vC[9] < g_clipLeft) {
                continue;
            }
            if (vA[9] > g_clipRight && vB[9] > g_clipRight && vC[9] > g_clipRight) {
                continue;
            }
            if (vA[10] < g_clipTop && vB[10] < g_clipTop && vC[10] < g_clipTop) {
                continue;
            }
            if (vA[10] > g_clipBottom && vB[10] > g_clipBottom && vC[10] > g_clipBottom) {
                continue;
            }

            /* 0x468d3f..0x4690d1, 0x4690d2..0x0046947d, 0x0046947e..0x004697ed:
             * three inlined copies of the env-map UV computation, one per
             * triangle vertex, each guarded by the `vtx[+0x38] == 0` cache
             * check. We use a single helper (EnvMapComputeVertexUV) called
             * three times — see the helper's top-of-function comment for
             * the full divergence rationale. */
            if (vA[14] == 0) {
                EnvMapComputeVertexUV(vA, modelStruct);  /* 0x468d3f guard */
            }
            if (vB[14] == 0) {
                EnvMapComputeVertexUV(vB, modelStruct);  /* 0x4690d2 guard */
            }
            if (vC[14] == 0) {
                EnvMapComputeVertexUV(vC, modelStruct);  /* 0x0046947e guard */
            }

            /* 0x004697f7..0x00469a74: submit the triangle to the per-tpage
             * D3D vertex+index batch. */
            const int tpage = g_tpageParallax2;   /* 0x004697f7 reads [0x8f6c40] */

            RenderVertex tri[3];
            tri[0] = BuildEnvMapRenderVertex(vA, pd[0], modelIdx, zNearBias, farClipDivisor);
            tri[1] = BuildEnvMapRenderVertex(vB, pd[1], modelIdx, zNearBias, farClipDivisor);
            tri[2] = BuildEnvMapRenderVertex(vC, pd[2], modelIdx, zNearBias, farClipDivisor);

            R_SetTexture(tpage);
            if (modelIdx == 0) {
                R_SetTexEnv(R_TEXENV_ADD_SIGNED);
            }
            else {
                R_SetTexEnv(R_TEXENV_MODULATE);
            }
            R_DrawTri(tri);

            /* 0x469a8f..0x469a94: tail continuation check — break out
             * if the just-submitted triangle pushed the polygon buffer
             * to capacity. */
            if (g_maxPolyCount == g_polyCount) {
                break;
            }
        }
    }

    /* 0x469aa0-0x469ad1: viewport restore (reached at function exit, NOT at
     * the early tpage-state bail-out above). All 5 saved slots, including
     * the defensive g_projScaleX. */
    g_screenCenterX     = saved_centerX;
    g_screenCenterY     = saved_centerY;
    g_projScaleX        = saved_projScaleX;
    g_projScaleXCurrent = saved_projScaleXCur;
    g_projScaleY        = saved_projScaleY;
}

/**
 * RenderTitleLogo — FUN_004dcb14 — 1207 bytes
 * Builds one textured quad (4 vertices, 6 indices) into tpage 0's
 * D3D vertex buffer. The quad is the title logo, with perspective
 * projection, rotation, and animated vertex colors.
 */
void RenderTitleLogo(void)
{
    if (g_tpageStateArray[0] != 0x04) {
        return;
    }

    /* Compute per-corner colors: base color + 0x40, clamped to 255 */
    int cornR[4], cornG[4], cornB[4];
    for (int i = 0; i < 4; i++) {
        cornR[i] = g_pressStartColorR[i] + 0x40;
        if (cornR[i] > 0xFF) {
            cornR[i] = 0xFF;
        }
        cornG[i] = g_pressStartColorG[i] + 0x40;
        if (cornG[i] > 0xFF) {
            cornG[i] = 0xFF;
        }
        cornB[i] = g_pressStartColorB[i] + 0x40;
        if (cornB[i] > 0xFF) {
            cornB[i] = 0xFF;
        }
    }

    /* Advance title seed angle */
    g_pressStartAngle = (g_pressStartAngle + 0x4B) & 0xFFF;

    /* Compute projection parameters */
    int projHalfW = (g_projScaleXCurrent * 3) / 2;
    {
        int sign = g_projScaleXCurrent * 0x195 >> 31;
        int xBase = g_clipLeft +
            ((int)((g_projScaleXCurrent * 0x195 + sign * -0x200) -
            (unsigned int)((sign << 8) < 0)) >> 9);
        int sign2 = g_projScaleY * 0x186 >> 31;
        int yBase = g_clipTop +
            ((int)((g_projScaleY * 0x186 + sign2 * -0x200) -
            (unsigned int)((sign2 << 8) < 0)) >> 9);

        /* Bobbing from sin(totalFrames) */
        int bob = g_sinTable[(g_totalFrames & 0x3F) * 0x40] / 0xC0 + 0x280;

        /* Rotation from title seed + track angle */
        unsigned int rotIdx = (unsigned int)(g_sinTable[g_pressStartAngle] / 0x2D +
                               g_titleLogoAngle) & 0xFFF;
        int sinRot = g_sinTable[rotIdx];
        int cosRot = g_cosTable[rotIdx];

        /* Compute two Z depths for top and bottom edges */
        int negSinSign = (-sinRot) >> 31;
        int zTop = ((int)((-sinRot + negSinSign * -0x80) -
                   (unsigned int)((negSinSign << 6) < 0)) >> 7) + bob;
        int negCosSign = (-cosRot) >> 31;
        int sinSign2 = sinRot >> 31;
        int zBot = ((int)((sinRot + sinSign2 * -0x80) -
                   (unsigned int)((sinSign2 << 6) < 0)) >> 7) + bob;

        /* Vertical span from projection */
        int projHalfH = ((g_projScaleY * 3) / 2) * 0x1C;
        int ySpanTop = projHalfH / zTop; 
        int ySpanBot = projHalfH / zBot; 

        /* Z values for vertex buffer.
         * Before InitLevel sets g_farClipFloat, it's 0 → produces inf.
         * D3D ignores inf Z for TLVerts; GL clips it → clamp to valid range. */
        float farClipSafe = (g_farClipFloat > 0.0f) ? g_farClipFloat : 88064.0f;
        float zTopF = (float)zTop * (1.0f / farClipSafe);
        float zBotF = (float)zBot * (1.0f / farClipSafe);

        /* Horizontal offset from cos rotation */
        int cosSign = cosRot >> 31;
        int cosScaled = ((int)((cosRot + cosSign * -0x80) -
                        (unsigned int)((cosSign << 6) < 0)) >> 7);
        int negCosScaled = ((int)((-cosRot + negCosSign * -0x80) -
                           (unsigned int)((negCosSign << 6) < 0)) >> 7);

        /* Faithful geometry (binary FUN_004dcb14): each vertical edge's X is
         * divided by that same edge's z — the zTop edge uses cosScaled/zTop,
         * the zBot edge uses negCosScaled/zBot — so X/Y-span/Z stay perspective-
         * consistent per edge. cosScaled and negCosScaled are +-cosRot/128, so
         * the two vertical edges sit on opposite sides of xBase and the sign of
         * cosRot decides which edge is which. */
        float xTopLeft = (float)(xBase + (projHalfW * cosScaled) / zTop);
        float xBotLeft = (float)(xBase + (projHalfW * negCosScaled) / zBot);

        /* Per-vertex ARGB colors */
        unsigned int col[4];
        for (int i = 0; i < 4; i++) {
            col[i] = 0xFF000000U | ((unsigned int)cornR[i] << 16) |
                     ((unsigned int)cornG[i] << 8) | (unsigned int)cornB[i];
        }

        R_SetTexture(0);
        R_SetTexEnv(R_TEXENV_MODULATE);

        RenderVertex verts[4] = {
            { xTopLeft, (float)(yBase - ySpanTop), zTopF, 1.0f / (float)zTop,
              col[0], 0, 0.0f, 0.78125f },
            { xBotLeft, (float)(yBase - ySpanBot), zBotF, 1.0f / (float)zBot,
              col[1], 0, 0.875f, 0.78125f },
            { xBotLeft, (float)(yBase + ySpanBot), zBotF, 1.0f / (float)zBot,
              col[2], 0, 0.875f, 1.0f },
            { xTopLeft, (float)(yBase + ySpanTop), zTopF, 1.0f / (float)zTop,
              col[3], 0, 0.0f, 1.0f },
        };
        R_DrawQuad(verts);
    }
}
