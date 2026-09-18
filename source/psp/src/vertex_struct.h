/**
 * vertex_struct.h — Source vertex layout (stride 0x40 = 64 bytes, 16 ints)
 *
 * Every vertex in g_vertexArrayBase uses this layout.
 * Loaded from track BIN files and character model files.
 * The transform pipeline reads posX/Y/Z, writes screenX/Y and camX/Y/depth.
 * Color is stored in 13-bit fixed point (>>13 yields 0-255).
 */
#ifndef VERTEX_STRUCT_H
#define VERTEX_STRUCT_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int screenX;     /* [0]  0x00 — projected screen X (written by transform) */
    int screenY;     /* [1]  0x04 — projected screen Y (written by transform) */
    int colorR;      /* [2]  0x08 — red,   13-bit fixed point */
    int colorG;      /* [3]  0x0C — green, 13-bit fixed point */
    int colorB;      /* [4]  0x10 — blue,  13-bit fixed point */
    int posX;        /* [5]  0x14 — world/local X */
    int posY;        /* [6]  0x18 — world/local Y */
    int posZ;        /* [7]  0x1C — world/local Z */
#ifdef SONICR_DC
    float rhw;       /* [8]  0x20 — DC: float 1/depth from projection's
                      *      fsrra. Used by PVR strip path so the rasterizer
                      *      sees sub-int rhw precision instead of stepping
                      *      every frame from reciprocal((float)int_depth). */
#else
    int _pad8;       /* [8]  0x20 — unused */
#endif
    int _pad9;       /* [9]  0x24 — unused */
    int camX;        /* [10] 0x28 — camera-space X (written by transform) */
    int camY;        /* [11] 0x2C — camera-space Y (written by transform) */
    int depth;       /* [12] 0x30 — camera-space Z / depth (written by transform) */
    int normalX;     /* [13] 0x34 — surface normal X (character models) */
    int normalY;     /* [14] 0x38 — surface normal Y (character models) */
    int normalZ;     /* [15] 0x3C — surface normal Z (character models) */
} SrcVertex;

_Static_assert(sizeof(SrcVertex) == 0x40, "SrcVertex must be 64 bytes");
_Static_assert(offsetof(SrcVertex, screenX) == 0x00, "");
_Static_assert(offsetof(SrcVertex, screenY) == 0x04, "");
_Static_assert(offsetof(SrcVertex, colorR)  == 0x08, "");
_Static_assert(offsetof(SrcVertex, colorG)  == 0x0C, "");
_Static_assert(offsetof(SrcVertex, colorB)  == 0x10, "");
_Static_assert(offsetof(SrcVertex, posX)    == 0x14, "");
_Static_assert(offsetof(SrcVertex, posY)    == 0x18, "");
_Static_assert(offsetof(SrcVertex, posZ)    == 0x1C, "");
_Static_assert(offsetof(SrcVertex, camX)    == 0x28, "");
_Static_assert(offsetof(SrcVertex, camY)    == 0x2C, "");
_Static_assert(offsetof(SrcVertex, depth)   == 0x30, "");
_Static_assert(offsetof(SrcVertex, normalX) == 0x34, "");
_Static_assert(offsetof(SrcVertex, normalY) == 0x38, "");
_Static_assert(offsetof(SrcVertex, normalZ) == 0x3C, "");

extern int g_screenCenterX;
extern int g_screenCenterY;
extern int g_projScaleXCurrent;
extern int g_projScaleY;

#ifdef SONICR_DC
// only works for positive x
#define reciprocal(x) ((x) < 0 ? -(1.0f / sqrtf((float)(x)*(float)(x))) : (1.0f / sqrtf((float)(x)*(float)(x))))
#elif defined(SONICR_PSP)
static inline float psp_vfpu_recip(float x)
{
    float r;
    __asm__ volatile (
        "mtv %1, S000\n"
        "vrcp.s S000, S000\n"
        "mfv %0, S000\n"
        : "=r"(r)
        : "r"(x)
    );
    return r;
}
#define reciprocal(x) psp_vfpu_recip((float)(x))
#else
#define reciprocal(x) (1.0f / (x))
#endif

static inline void SrcVertex_ProjectFloat(const SrcVertex *v, float *outX, float *outY)
{
    float invDepth = reciprocal((float)v->depth);
    *outX = (float)g_screenCenterX + (float)g_projScaleXCurrent * (float)v->camX * invDepth;
    *outY = (float)g_screenCenterY - (float)g_projScaleY * (float)v->camY * invDepth;
}

static inline void NearClipVert_ProjectFloat(int camX, int camY, int depth, float *outX, float *outY)
{
    float invDepth = reciprocal((float)depth);
    *outX = (float)g_screenCenterX + (float)g_projScaleXCurrent * (float)camX * invDepth;
    *outY = (float)g_screenCenterY - (float)g_projScaleY * (float)camY * invDepth;
}

#endif /* VERTEX_STRUCT_H */
