/**
 * view_matrix.c — View matrix construction
 *
 * BuildViewMatrix, ComputeCameraBasis, BuildD3DViewMatrix.
 * These build the camera transform matrices from yaw/pitch angles.
 *
 * In the original binary, all three functions receive the camera struct
 * pointer via in_EAX (Watcom fastcall). For recompilation, we use a
 * global camera pointer that is set before these are called.
 *
 * The camera struct layout (byte offsets from base):
 *   +0x18: yaw index (int, 12-bit angle into g_sinTable)
 *   +0x1C: pitch index (int, 12-bit angle into g_sinTable)
 *   +0x24: basis vector Y component (int)
 *   +0x28: basis vector X component (int, negated sine)
 *   +0x2C: basis vector Z component (int)
 *   +0x3C: yaw angle as float (for D3D matrix)
 *   +0x40: pitch angle as float (for D3D matrix)
 *   +0x48..+0x70: 3x3 integer rotation matrix (9 ints)
 *   +0x88..+0xB0: 3x3 float rotation matrix (9 floats, for D3D)
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include <math.h>

/* Global camera struct pointer — set by RenderScene3D before calling these */
static int *s_cam = NULL;

void SetCameraStructPtr(int *cam)
{
    s_cam = cam;
}

/**
 * BuildViewMatrix — 0x0042316C — 160 bytes
 * Constructs a 3x3 integer rotation matrix from yaw and pitch angles
 * using the sine/cosine lookup tables.
 *
 * Matrix layout at cam+0x48 (row-major, 3x3):
 *   [0x48] [0x4C] [0x50]     cosPitch   -sinPitch*-sinYaw   -sinPitch*cosYaw
 *   [0x58] [0x5C] [0x60]     0          cosYaw              sinYaw
 *   [0x68] [0x6C] [0x70]     sinPitch   -sinYaw*cosPitch    cosYaw*cosPitch
 */
void BuildViewMatrix(void)
{
    if (s_cam == NULL) {
        return;
    }

    int *cam = s_cam;

    int yawIdx = *(int *)((char *)cam + 0x18);
    int pitchIdx = *(int *)((char *)cam + 0x1C);

    int sinYaw = g_sinTable[yawIdx & 0xFFF];
    int sinPitch = g_sinTable[pitchIdx & 0xFFF];
    int negSinPitch = -(sinPitch >> 2);
    int negSinYaw = -(sinYaw >> 2);
    int cosPitch = g_cosTable[pitchIdx & 0xFFF] >> 2;
    int cosYaw = g_cosTable[yawIdx & 0xFFF] >> 2;

    /* Row 0 */
    *(int *)((char *)cam + 0x48) = cosPitch;
    *(int *)((char *)cam + 0x4C) = (negSinPitch * negSinYaw) >> 12;
    *(int *)((char *)cam + 0x50) = (negSinPitch * cosYaw) >> 12;

    /* Row 1 */
    *(int *)((char *)cam + 0x58) = 0;
    *(int *)((char *)cam + 0x5C) = cosYaw;
    *(int *)((char *)cam + 0x60) = sinYaw >> 2;

    /* Row 2 */
    *(int *)((char *)cam + 0x68) = sinPitch >> 2;
    *(int *)((char *)cam + 0x6C) = (negSinYaw * cosPitch) >> 12;
    *(int *)((char *)cam + 0x70) = (cosYaw * cosPitch) >> 12;

    /* Mirror mode: negate X-axis components */
    if (g_mirrorMode != 0) {
        *(int *)((char *)cam + 0x48) = -cosPitch;
        *(int *)((char *)cam + 0x68) = negSinPitch;
    }
}

/**
 * ComputeCameraBasis — 0x004230E0 — 139 bytes
 * Derives camera right/up vectors from yaw and pitch for billboard alignment.
 *
 * Output at cam+0x24..+0x2C (3 ints: Y component, X component, Z component)
 * These are used to orient billboarded sprites to face the camera.
 */
void ComputeCameraBasis(void)
{
    if (s_cam == NULL) {
        return;
    }

    int *cam = s_cam;

    int yawIdx = *(int *)((char *)cam + 0x18);
    int pitchIdx = *(int *)((char *)cam + 0x1C);

    unsigned int invYaw = (-yawIdx) & 0xFFF;
    unsigned int invPitch = (-pitchIdx) & 0xFFF;

    int sinInvYaw = g_sinTable[invYaw];
    int sinInvPitch = g_sinTable[invPitch] >> 2;
    int cosInvPitch = g_cosTable[invPitch] >> 2;
    /* 0x423114 loads cos(invYaw) into ecx once and keeps it across BOTH
     * multiplies below — it is the shared term, not cos(invPitch). */
    int cosInvYaw = g_cosTable[invYaw] >> 2;

    /* X component: negated sine of inverted yaw — 0x4230FB */
    *(int *)((char *)cam + 0x28) = -(sinInvYaw >> 2);

    /* Y component: sin(invPitch) * cos(invYaw) >> 12 — 0x42312F */
    *(int *)((char *)cam + 0x24) = (sinInvPitch * cosInvYaw) / 4096;

    /* Z component: cos(invPitch) * cos(invYaw) >> 12 — 0x423153 */
    *(int *)((char *)cam + 0x2C) = (cosInvPitch * cosInvYaw) / 4096;
}

/**
 * BuildD3DViewMatrix — 0x0042320C — 433 bytes
 * Constructs the 3x3 float view matrix for Direct3D.
 * Uses fsin/fcos on float angles stored at cam+0x3C and cam+0x40.
 *
 * Float matrix layout at cam+0x88 (3x3, interleaved):
 *   [0x88] = cosB                    [0x8C] = 0 (then modified)
 *   [0x90] = -sinB (then modified)   [0x98] = 0
 *   [0x9C] = 1.0                     [0xA0] = 0
 *   [0xA8] = sinB                    [0xAC] = 0 (then modified)
 *   [0xB0] = cosB (then modified)
 *
 * After initial setup, the pitch rotation (sinA/cosA from cam+0x3C)
 * is composed by multiplying columns by cosA and adding sinA cross terms.
 */
void BuildD3DViewMatrix(void)
{
    if (s_cam == NULL) {
        return;
    }
    char *cam = (char *)s_cam;

    float sinA = sinf(*(float *)(cam + 0x3C));   /* yaw */
    float cosA = cosf(*(float *)(cam + 0x3C));
    float sinB = sinf(*(float *)(cam + 0x40));   /* pitch */
    float cosB = cosf(*(float *)(cam + 0x40));

    /* Start with pitch rotation matrix (around X axis) */
    *(float *)(cam + 0x8C) = 0.0f;
    *(float *)(cam + 0x9C) = 1.0f;
    *(float *)(cam + 0xAC) = 0.0f;

    float m8C = *(float *)(cam + 0x8C);  /* 0 */
    float m9C = *(float *)(cam + 0x9C);  /* 1 */
    float mAC = *(float *)(cam + 0xAC);  /* 0 */

    *(float *)(cam + 0x98) = 0.0f;
    *(float *)(cam + 0xA0) = 0.0f;
    *(float *)(cam + 0x88) = cosB;
    float negSinB = -sinB;
    *(float *)(cam + 0x90) = negSinB;
    *(float *)(cam + 0xA8) = sinB;
    *(float *)(cam + 0xB0) = cosB;

    /* Compose with yaw rotation (around Y axis):
     * For each column, new = old * cosA + cross * sinA */

    /* Column 0: [0x8C, 0x90] */
    float old_8C = *(float *)(cam + 0x8C);
    float old_90 = *(float *)(cam + 0x90);
    *(float *)(cam + 0x8C) = old_8C * cosA - old_90 * sinA;
    *(float *)(cam + 0x90) = old_90 * cosA + m8C * sinA;

    /* Column 1: [0x9C, 0xA0] */
    float old_9C = *(float *)(cam + 0x9C);
    float old_A0 = *(float *)(cam + 0xA0);
    *(float *)(cam + 0x9C) = old_9C * cosA - old_A0 * sinA;
    *(float *)(cam + 0xA0) = old_A0 * cosA + m9C * sinA;

    /* Column 2: [0xAC, 0xB0] */
    float old_AC = *(float *)(cam + 0xAC);
    float old_B0 = *(float *)(cam + 0xB0);
    *(float *)(cam + 0xAC) = old_AC * cosA - old_B0 * sinA;
    *(float *)(cam + 0xB0) = mAC * sinA + old_B0 * cosA;

    /* Mirror mode: negate X-axis */
    if (g_mirrorMode != 0) {
        *(float *)(cam + 0x88) = -cosB;
        *(float *)(cam + 0xA8) = negSinB;  /* -(-sinB) = sinB... wait */
        /* Original: *(float*)(cam+0xA8) = fVar6 which is -sinB */
        *(float *)(cam + 0xA8) = negSinB;
    }

#ifdef SONICR_DC
    /* DC: copy the just-built float view matrix at cam+0x88..+0xB0 into
     * g_viewMtxF[]. Same source the grid renderer uses (fMtx in
     * RenderPlayfieldGridD3D), so the track and grid see the same
     * rotation — no quantization drift between sin-table and sinf. */
    float *src = (float *)(cam + 0x88);
    g_viewMtxF[0] = src[0];
    g_viewMtxF[1] = src[1];
    g_viewMtxF[2] = src[2];
    g_viewMtxF[4] = src[4];
    g_viewMtxF[5] = src[5];
    g_viewMtxF[6] = src[6];
    g_viewMtxF[8] = src[8];
    g_viewMtxF[9] = src[9];
    g_viewMtxF[10] = src[10];
    /* Pad lanes zero — fipr's 4th component multiplies them to nothing */
    g_viewMtxF[3] = 0.0f;
    g_viewMtxF[7] = 0.0f;
    g_viewMtxF[11] = 0.0f;
    g_viewMtxF[12] = 0.0f;
    g_viewMtxF[13] = 0.0f;
    g_viewMtxF[14] = 0.0f;
    g_viewMtxF[15] = 0.0f;
#endif
}