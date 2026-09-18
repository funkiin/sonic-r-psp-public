/**
 * track_load.c — Track geometry and data loading
 *
 * LoadTerrain, LoadAI, and LoadTrack3 (partial).
 * See sonicr_annotated.c Track Loading section for format documentation.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "sonicr_paths.h"
#include "r_state.h"
#include "ter_types.h"
#include "endian_util.h"

extern void SFX_Stop(int slot);
extern void R_MarkTextureDirty(int tpage);
extern void R_SetTpageGreen6(int tpage, int on);
extern void R_SetNoColorKey(int);
extern void R_SetTpageRGBA8(int tpage, unsigned char *rgba);

static int ReadLE32(const void *p) {
    const unsigned char *b = (const unsigned char *)p;
    return (int)(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
}

#include "sonicr_paths.h"

/* Terrain sub-table pointers — from FUN_004d8904 @ 0x004d8904 (423 bytes) */
extern int g_terScalar5bc;
extern int g_terScalar5c0;
extern int g_terScalar5c4;
extern int g_terScalar5c8;
extern int g_terScalar5cc;
extern int g_terScalar5d4;
extern int g_terScalar5d8;
extern int g_terCollisionMeshSize;

/* Static buffer for .TER file data — largest is ISLAND.TER at 80,492 bytes. */
static uint32_t __attribute__((aligned(32))) s_terRawStorage[20123];  /* 80,492 bytes */
static void *s_terRawBuffer = s_terRawStorage;

/**
 * LoadTerrain — 0x0042A3F8 — 81 bytes
 * Reads the .TER file into s_terRawBuffer. Does NOT parse the header —
 * that's done separately by ParseTerrainHeader (called from InitLevel).
 */
void LoadTerrain(void)
{
    static const char *s_terPaths[] = {
        NULL, PATH_ISLAND_TER, PATH_CITY_TER, PATH_RUIN_TER,
        PATH_FACTORY_TER, PATH_EMERALD_TER
    };

    if (g_trackId < TRACK_RESORT_ISLAND || g_trackId > TRACK_RADIANT_EMERALD) {
        return;
    }
    FILE *fp = fOpen(s_terPaths[g_trackId], "rb");
    if (fp == NULL) {
        return;
    }

    /* Read entire file into buffer */
    fSeek(fp, 0, SEEK_END);
    long size = fTell(fp);
    fSeek(fp, 0, SEEK_SET);

    s_terRawBuffer = s_terRawStorage;
    fRead(s_terRawBuffer, 1, size, fp);
    fClose(fp);
}

/**
 * ParseTerrainHeader — FUN_004d8904 — 423 bytes
 * Parses the .TER header from s_terRawBuffer (loaded by LoadTerrain).
 * 10 int offsets (base-relative pointers) followed by 13 scalar ints.
 * Called from InitLevel after per-track init has loaded the file.
 *
 * No parameters (reads from fixed buffer at 0x92f68c in binary).
 */
void ParseTerrainHeader(void)
{
    char *base = (char *)s_terRawBuffer;
    char *hdr = base;

    /* 10 pointer offsets (base-relative, little-endian ints in file) */
    g_terVertexTable = base + ReadLE32(hdr + 0x00);   /* → 0x6DA55C */
    g_terFaceTable = base + ReadLE32(hdr + 0x04);     /* → 0x6DA560 */
    g_trackSurfaceData = base + ReadLE32(hdr + 0x08); /* → 0x6DA564 */
    g_terCollisionMesh = base + ReadLE32(hdr + 0x0C); /* → 0x6DA568 */
    g_terCollisionMeshSize = ReadLE32(hdr + 0x10) - ReadLE32(hdr + 0x0C);
    g_terEdgeList = base + ReadLE32(hdr + 0x10);    /* → 0x6DA56C */
    g_terLoopTable = base + ReadLE32(hdr + 0x14);   /* → 0x6DA570 */
    g_terUnknown74 = base + ReadLE32(hdr + 0x18);   /* → 0x6DA574 */
    g_terGridIndex = base + ReadLE32(hdr + 0x1C);   /* → 0x6DA57C */
    g_terGridData = base + ReadLE32(hdr + 0x20);    /* → 0x6DA580 */
    g_itemStateTable = base + ReadLE32(hdr + 0x24); /* → 0x6DA578 */

    /* 13 scalar values */
    g_terScalar5bc = ReadLE32(hdr + 0x28);        /* → 0x6DA5BC */
    g_terScalar5c0 = ReadLE32(hdr + 0x2C);        /* → 0x6DA5C0 */
    g_terScalar5c4 = ReadLE32(hdr + 0x30);        /* → 0x6DA5C4 */
    g_terScalar5c8 = ReadLE32(hdr + 0x34);        /* → 0x6DA5C8 */
    g_terScalar5cc = ReadLE32(hdr + 0x38);        /* → 0x6DA5CC */
    g_terLoopCount = ReadLE32(hdr + 0x3C);        /* → 0x6DA5D0 */
    g_terScalar5d4 = ReadLE32(hdr + 0x40);        /* → 0x6DA5D4 */
    g_terScalar5d8 = ReadLE32(hdr + 0x44);        /* → 0x6DA5D8 */
    g_terCollectibleCount = ReadLE32(hdr + 0x48); /* → 0x6DA5DC */
    g_aiGridOriginX = ReadLE32(hdr + 0x4C);       /* → 0x6DA5E0 */
    g_aiGridOriginZ = ReadLE32(hdr + 0x50);       /* → 0x6DA5E4 */
    g_aiGridCellWidth = ReadLE32(hdr + 0x54);     /* → 0x6DA5E8 */
    g_aiGridCellHeight = ReadLE32(hdr + 0x58);    /* → 0x6DA5EC */

#if SONICR_BIG_ENDIAN
    /* In-place byte-swap of all body sections so struct overlays work natively.
     * Section sizes derived from consecutive ascending header offsets. */
    int vertOff = (char *)g_terVertexTable - base;
    int faceOff = (char *)g_terFaceTable - base;
    int surfOff = (char *)g_trackSurfaceData - base;
    int collOff = (char *)g_terCollisionMesh - base;
    int edgeOff = (char *)g_terEdgeList - base;
    int lookAtOff = (char *)g_terLoopTable - base;
    int unk74Off = (char *)g_terUnknown74 - base;
    int gridIdxOff = (char *)g_terGridIndex - base;
    int gridDatOff = (char *)g_terGridData - base;
    int itemOff = (char *)g_itemStateTable - base;

    /* TerVertex (6B = 3 shorts) */
    bswap16_arr(g_terVertexTable, (faceOff - vertOff) / 2);

    /* TerFace (14B = 7 shorts) */
    bswap16_arr(g_terFaceTable, (surfOff - faceOff) / 2);

    /* TerSurface (16B): 4 shorts, 1 int32, 2 shorts */
    int ns = (collOff - surfOff) / 16;
    TerSurface *s = (TerSurface *)g_trackSurfaceData;
    for (int i = 0; i < ns; i++) {
        bswap16_inplace(&s[i].faceBase);
        bswap16_inplace(&s[i].faceCount);
        bswap16_inplace(&s[i].centerX);
        bswap16_inplace(&s[i].centerZ);
        bswap32_inplace(&s[i].radiusSq);
        bswap16_inplace(&s[i].misc);
        bswap16_inplace(&s[i].layer);
    }

    /* TerCollisionMesh (16B): short, 4 bytes, short, 2 shorts, int32 */
    int nc = g_terCollisionMeshSize / 16;
    TerCollisionMesh *m = (TerCollisionMesh *)g_terCollisionMesh;
    for (int i = 0; i < nc; i++) {
        bswap16_inplace(&m[i].vtxBase);
        bswap16_inplace(&m[i].mask);
        bswap16_inplace(&m[i].centerX);
        bswap16_inplace(&m[i].centerZ);
        bswap32_inplace(&m[i].radiusSq);
    }

    /* Edge list — same layout as TerVertex (6B = 3 shorts) */
    bswap16_arr(g_terEdgeList, (lookAtOff - edgeOff) / 2);

    /* TerLoopEntry (22B = 11 shorts) */
    bswap16_arr(g_terLoopTable, g_terLoopCount * 11);

    /* Loop-surface vertex data at unk74 (34B = 17 shorts per entry) */
    if (unk74Off != gridIdxOff) {
        bswap16_arr(g_terUnknown74, (gridIdxOff - unk74Off) / 2);
    }

    /* Grid index: 1024 shorts (32×32 cells) */
    bswap16_arr(g_terGridIndex, (gridDatOff - gridIdxOff) / 2);

    /* Grid data: shorts terminated by 0xFFFF sentinel */
    bswap16_arr(g_terGridData, (itemOff - gridDatOff) / 2);

    /* TerItemState (12B = 6 shorts) */
    bswap16_arr(g_itemStateTable, g_terCollectibleCount * 6);
#endif
}

static unsigned char __attribute__((aligned(32))) s_aiDataBuf[0x20000];  /* AI data buffer — large enough for all sections */

/**
 * LoadAI — 0x0042A44C — 413 bytes
 * Loads AI pathfinding data from a track-specific AI file.
 *
 * Data layout (sequential in file):
 *   Section 1: 0x2000 waypoint pairs (2 bytes each)
 *   Section 2: 0x80 path segments (4 bytes each)
 *   Section 3: 0xE00 collision tiles (1 byte each)
 *   Section 4: 0x400 visibility tiles (1 byte each)
 *   Section 5: Variable tail (track-dependent size)
 */
void LoadAI(void)
{
    DebugLog("LoadAI\n");

    static const char *s_aiPaths[] = {
        NULL, PATH_AI_ISLAND, PATH_AI_CITY, PATH_AI_RUIN,
        PATH_AI_FACTORY, PATH_AI_EMERALD
    };

    if (g_trackId < TRACK_RESORT_ISLAND || g_trackId > TRACK_RADIANT_EMERALD) {
        return;
    }

    FILE *fp = fOpen(s_aiPaths[g_trackId], "rb");
    if (fp == NULL) {
        DebugLog("  LoadAI: cannot open %s\n", s_aiPaths[g_trackId]);
        return;
    }
    DebugLog("  LoadAI: opened %s\n", s_aiPaths[g_trackId]);

    unsigned char *aiData = s_aiDataBuf;

    int i;

    /* Section 1: 0x2000 waypoint pairs (2 bytes each).
     * File stores each pair in BE order; the original x86 loader reverses
     * them to produce LE shorts.  On BE hosts the file bytes are already
     * in native order, so read straight through. */
#if SONICR_BIG_ENDIAN
    fRead(aiData, 1, 0x2000 * 2, fp);
    aiData += 0x2000 * 2;
#elif defined(SONICR_DC)
    {
        unsigned char *tmp = (unsigned char *)malloc(0x2000 * 2);
        if (tmp != NULL) {
            fRead(tmp, 1, 0x2000 * 2, fp);
            for (i = 0; i < 0x2000; i++) {
                *aiData++ = tmp[i*2 + 1];
                *aiData++ = tmp[i*2 + 0];
            }
            free(tmp);
        }
        else {
            for (i = 0; i < 0x2000; i++) {
                int b1, b2;
                fRead(&b1, 1, 1, fp);
                fRead(&b2, 1, 1, fp);
                *aiData++ = (unsigned char)b2;
                *aiData++ = (unsigned char)b1;
            }
        }
    }
#else
    for (i = 0; i < 0x2000; i++) {
        int b1, b2;
        fRead(&b1, 1, 1, fp);
        fRead(&b2, 1, 1, fp);
        *aiData++ = (unsigned char)b2;
        *aiData++ = (unsigned char)b1;
    }
#endif

    /* Section 2: 0x80 path segments (4 bytes each).
     * Same deal — file is BE, loader reverses to LE.  On BE, read as-is. */
#if SONICR_BIG_ENDIAN
    fRead(aiData, 1, 0x80 * 4, fp);
    aiData += 0x80 * 4;
#elif defined(SONICR_DC)
    {
        unsigned char tmp[0x80 * 4];  /* 512 bytes */
        fRead(tmp, 1, sizeof(tmp), fp);
        for (i = 0; i < 0x80; i++) {
            *aiData++ = tmp[i*4 + 3];
            *aiData++ = tmp[i*4 + 2];
            *aiData++ = tmp[i*4 + 1];
            *aiData++ = tmp[i*4 + 0];
        }
    }
#else
    for (i = 0; i < 0x80; i++) {
        int b1, b2, b3, b4;
        fRead(&b1, 1, 1, fp);
        fRead(&b2, 1, 1, fp);
        fRead(&b3, 1, 1, fp);
        fRead(&b4, 1, 1, fp);
        *aiData++ = (unsigned char)b4;
        *aiData++ = (unsigned char)b3;
        *aiData++ = (unsigned char)b2;
        *aiData++ = (unsigned char)b1;
    }
#endif

    /* Section 3: 0xE00 collision tiles (1 byte each) */
#ifdef SONICR_DC
    fRead(aiData, 1, 0xE00, fp);
    aiData += 0xE00;
#else
    for (i = 0; i < 0xE00; i++) {
        int b;
        fRead(&b, 1, 1, fp);
        *aiData++ = (unsigned char)b;
    }
#endif

    /* Section 4: 0x400 visibility tiles (1 byte each) */
#ifdef SONICR_DC
    fRead(aiData, 1, 0x400, fp);
    aiData += 0x400;
#else
    for (i = 0; i < 0x400; i++) {
        int b;
        fRead(&b, 1, 1, fp);
        *aiData++ = (unsigned char)b;
    }
#endif

    /* Section 5: Variable tail (per-track) */
    int tailSize;
    if (g_trackId == TRACK_RESORT_ISLAND) {
        tailSize = 0x320;
    }
    else if (g_trackId == TRACK_RADIANT_EMERALD) {
        tailSize = 0xA0;
    }
    else {
        tailSize = 0x280;
    }

#ifdef SONICR_DC
    fRead(aiData, 1, (size_t)tailSize, fp);
    aiData += tailSize;
#else
    for (i = 0; i < tailSize; i++) {
        int b;
        fRead(&b, 1, 1, fp);
        *aiData++ = (unsigned char)b;
    }
#endif

    fClose(fp);

    /* AI data pointers and rubber banding are assigned by
     * FUN_0041e494 (called from InitRaceStart), not here.
     * LoadAI just loads the raw data into s_aiDataBuf. */
}

/**
 * GetAIDataBuffer — returns the raw AI data buffer loaded by LoadAI.
 * Called by FUN_0041e494 (in player_init.c) to assign global pointers.
 */
unsigned char *GetAIDataBuffer(void)
{
    return s_aiDataBuf;
}

extern int g_soundActive[64];     /* 0x006DA080 — nonzero if buffer is playing */

/* COM_CALL and DSBUF_* vtable offsets defined in sonicr_types.h */

static void StopSoundBuffer(int slot)
{
    if (g_soundActive[slot] == 0) {
        return;
    }
    if (g_soundBuffers[slot] == NULL) {
        return;
    }

    SFX_Stop(slot);
}

/**
 * StopAmbientSounds — 0x004D0458 — 41 bytes
 * Stops and resets 4 DirectSound buffers used for looping ambient audio.
 * Calls FUN_004d0410 which does IDirectSoundBuffer::Stop + SetCurrentPosition(0).
 */
void StopAmbientSounds(void)
{
    /* 0x4D0458: stops exactly 4 ambient/looping sound slots.
     * Previous code called SFX_StopAll() which killed ALL sounds
     * including pause menu SFX. */
    StopSoundBuffer(0x0B);  /* 0x4D0458: eax = 0x0B */
    StopSoundBuffer(5);     /* 0x4D0462: eax = 5 */
    StopSoundBuffer(0x11);  /* 0x4D046C: eax = 0x11 */
    StopSoundBuffer(6);     /* 0x4D0476: eax = 6 */
}

void OpenTrackFileA(void) {
    g_fileHandle = fOpen(PATH_GENERAL_BIT, "rb");
}

void OpenTrackFileB(void) {
    /* Opens the track-specific data file. Filename varies by track ID. */
    static const char *s_trackFiles[] = {
        NULL,
        PATH_ISLAND_BIN,
        PATH_CITY_BIN,
        PATH_RUIN_BIN,
        PATH_FACTORY_BIN,
        PATH_EMERALD_BIN,
    };
    if (g_trackId >= TRACK_RESORT_ISLAND && g_trackId <= TRACK_RADIANT_EMERALD) {
        g_fileHandle = fOpen(s_trackFiles[g_trackId], "rb");
    }
}

void SkipTrackHeader(void) {
    FILE *fp = g_fileHandle;
    if (fp != NULL) {
        unsigned char dummy;
        fRead(&dummy, 1, 1, fp);
    }
}

#if !defined(SONICR_DC) && !defined(__EMSCRIPTEN__)
#define SONICR_SKY32 1
#endif

/* =====================================================================
 * CompositeRainbowOnParallax — translated from FUN_004e0094 (948 bytes)
 *
 * Loads RAINBOW.RAW (256×256, 3bpp) and additively blends it onto the
 * 16bpp parallax source buffer at a per-track sun angle position.
 * Called when weather is steady rain (weatherType == 1 == prevWeatherType)
 * on tracks 1-4 (not Radiant Emerald).
 *
 * The rainbow is drawn as two mirrored halves (128 px each) around
 * a center position, creating the arc shape.  Blending: dst += src/2.
 * ===================================================================== */
#define RAINBOW_DIM   256
#define RAINBOW_STRIDE (RAINBOW_DIM * 3)  /* 768 bytes per row */
#define RAINBOW_SIZE   (RAINBOW_DIM * RAINBOW_DIM * 3)  /* 196608 bytes */
#define PARA_W         1664
#define PARA_H         128

#ifndef SONICR_SKY32
static void CompositeRainbowOnParallax(unsigned short *buf)
{
    if (buf == NULL) {
        return;
    }
    if (g_trackId < TRACK_RESORT_ISLAND || g_trackId > TRACK_REACTIVE_FACTORY) {
        return;
    }
    if (g_weatherType != WEATHER_RAIN) {
        return;
    }
    if (g_weatherType != g_timeOfDay) {
        return;
    }

    /* Per-track sun angle (upper 16 bits of ROM data at 0x4FF17E + idx*0x24).
     * Position = ((angle + 1664) % 3328) * 1664 / 3328 = ((angle + 1664) % 3328) / 2 */
    static const int s_angles[] = { 0, 0x07B4, 0x0372, 0x088C, 0x02E8 };
    int angle = s_angles[g_trackId];
    int pos = angle + PARA_W;
    if (pos >= PARA_W * 2) {
        pos -= PARA_W * 2;
    }
    int centerX = (pos * PARA_W) / (PARA_W * 2);

    /* Load rainbow.raw */
    FILE *fp = fOpen(PATH_RAINBOW_RAW, "rb");
    if (!fp) {
        return;
    }
    unsigned char *rbuf = (unsigned char *)malloc(RAINBOW_SIZE);
    if (!rbuf) {
        fClose(fp);
        return;
    }
    if (fRead(rbuf, 1, RAINBOW_SIZE, fp) != RAINBOW_SIZE) {
        free(rbuf);
        fClose(fp);
        return;
    }
    fClose(fp);

    int halfW = 128;
    int uvStep = 0xFFFFFF / (halfW - 1);

    /* Right edge start: centerX + 0xE0 - halfW/2 = centerX + 160 */
    int rightStart = centerX + 224 - halfW / 2;
    if (rightStart < 0) {
        rightStart += PARA_W;
    }
    if (rightStart >= PARA_W) {
        rightStart -= PARA_W;
    }

    /* Left edge start: centerX - 0x120 = centerX - 288 */
    int leftStart = centerX - 288;
    if (leftStart < 0) {
        leftStart += PARA_W;
    }
    if (leftStart >= PARA_W) {
        leftStart -= PARA_W;
    }

    /* right half (UV goes 0 → 1 across rainbow) */
    {
        int rowUV = 0;
        for (int row = 0; row < halfW; row++) {
            int rRow = rowUV >> 16;
            if (rRow >= RAINBOW_DIM) {
                rRow = RAINBOW_DIM - 1;
            }
            unsigned char *rSrc = rbuf + rRow * RAINBOW_STRIDE;

            int colUV = 0;
            int dx = rightStart;
            for (int col = 0; col < halfW; col++) {
                int rCol = (colUV >> 16) * 3;

                unsigned char rr = rSrc[rCol + 0] / 2;
                unsigned char rg = rSrc[rCol + 1] / 2;
                unsigned char rb_v = rSrc[rCol + 2] / 2;

                int idx = row * PARA_W + dx;
                unsigned short px = buf[idx];
                int r5 = (px >> 11) & 0x1F;
                int g6 = (px >> 5) & 0x3F;
                int b5 = px & 0x1F;

                r5 += rr >> 3;
                if (r5 > 31) {
                    r5 = 31;
                }
                g6 += rg >> 2;
                if (g6 > 63) {
                    g6 = 63;
                }
                b5 += rb_v >> 3; 
                if (b5 > 31) {
                    b5 = 31;
                }

                buf[idx] = (unsigned short)((r5 << 11) | (g6 << 5) | b5);

                dx++;
                if (dx >= PARA_W) {
                    dx -= PARA_W;
                }
                colUV += uvStep;
            }
            rowUV += uvStep;
        }
    }

    /* left half (UV goes 1 → 0, mirrored) */
    {
        int rowUV = 0;
        for (int row = 0; row < halfW; row++) {
            int rRow = rowUV >> 16;
            if (rRow >= RAINBOW_DIM) {
                rRow = RAINBOW_DIM - 1;
            }
            unsigned char *rSrc = rbuf + rRow * RAINBOW_STRIDE;

            int colUV = 0xFFFFFF;
            int dx = leftStart;
            for (int col = 0; col < halfW; col++) {
                int rCol = (colUV >> 16) * 3;

                unsigned char rr = rSrc[rCol + 0] / 2;
                unsigned char rg = rSrc[rCol + 1] / 2;
                unsigned char rb_v = rSrc[rCol + 2] / 2;

                int idx = row * PARA_W + dx;
                unsigned short px = buf[idx];
                int r5 = (px >> 11) & 0x1F;
                int g6 = (px >> 5) & 0x3F;
                int b5 = px & 0x1F;

                r5 += rr >> 3;
                if (r5 > 31) {
                    r5 = 31;
                }
                g6 += rg >> 2;
                if (g6 > 63) {
                    g6 = 63;
                }
                b5 += rb_v >> 3;
                if (b5 > 31) {
                    b5 = 31;
                }

                buf[idx] = (unsigned short)((r5 << 11) | (g6 << 5) | b5);

                dx++;
                if (dx >= PARA_W) {
                    dx -= PARA_W;
                }
                colUV -= uvStep;
            }
            rowUV += uvStep;
        }
    }

    free(rbuf);
}
#endif

/**
 * S3D_LoadAndScaleParallax — FUN_0042cd14 — 686 bytes
 * Loads a parallax RAW file (1664×128, 3 bytes/pixel), converts to 16bpp,
 * and folds it into a 512×512 sky/parallax tpage (see below).
 *
 * Original: in_EAX = filename, param_2 (EDX) = dest width, unaff_EBX = dest height.
 * Source is always 1664 (0x680) wide × 128 tall = 0x34000 (212992) pixels.
 */
#define PARALLAX_SRC_W 0x680    /* 1664 */
#define PARALLAX_SRC_PIXELS 0x34000  /* 212992 = 1664 * 128 */

#ifdef SONICR_DC
/* DC-only error-diffusion dither (texture.c) — fixes parallax/sky banding. */
void DitherConvertImageRGB565(const unsigned char *raw,
                              unsigned short *dst, int w, int h);
#endif

/* Static parallax source buffer — 212992 src pixels (16bpp). */
static uint32_t __attribute__((aligned(32))) s_parallaxSrcStorage[PARALLAX_SRC_PIXELS / 2 + 1]; /* 212992 shorts */

#if !defined(SONICR_DC) && !defined(__EMSCRIPTEN__)
/* Full-precision 32-bit RGBA source for the desktop sky (1664×128), built
 * straight from the 24-bit RAW with no 5/6-bit truncation → no banding. */
static unsigned char s_parallaxSrc32[PARALLAX_SRC_PIXELS * 4];

/* Additive rainbow overlay on the 32-bit source — 8-bit-channel twin of
 * CompositeRainbowOnParallax (dst += src/2, clamp 255). Same geometry. */
static void CompositeRainbowOnParallax32(unsigned char *buf32)
{
    if (buf32 == NULL) {
        return;
    }
    if (g_trackId < TRACK_RESORT_ISLAND || g_trackId > TRACK_REACTIVE_FACTORY) {
        return;
    }
    if (g_weatherType != WEATHER_RAIN) {
        return;
    }
    if (g_weatherType != g_timeOfDay) {
        return;
    }

    static const int s_angles[] = { 0, 0x07B4, 0x0372, 0x088C, 0x02E8 };
    int angle = s_angles[g_trackId];
    int pos = angle + PARA_W;
    if (pos >= PARA_W * 2) {
        pos -= PARA_W * 2;
    }
    int centerX = (pos * PARA_W) / (PARA_W * 2);

    FILE *fp = fOpen(PATH_RAINBOW_RAW, "rb");
    if (!fp) {
        return;
    }
    unsigned char *rbuf = (unsigned char *)malloc(RAINBOW_SIZE);
    if (!rbuf) {
        fClose(fp);
        return;
    }
    if (fRead(rbuf, 1, RAINBOW_SIZE, fp) != RAINBOW_SIZE) {
        free(rbuf);
        fClose(fp);
        return;
    }
    fClose(fp);

    int halfW = 128;
    int uvStep = 0xFFFFFF / (halfW - 1);

    int rightStart = centerX + 224 - halfW / 2;
    if (rightStart < 0) {
        rightStart += PARA_W;
    }
    if (rightStart >= PARA_W) {
        rightStart -= PARA_W;
    }
    int leftStart = centerX - 288;
    if (leftStart < 0) {
        leftStart += PARA_W;
    }
    if (leftStart >= PARA_W) {
        leftStart -= PARA_W;
    }

    for (int pass = 0; pass < 2; pass++) {
        int rowUV = 0;
        for (int row = 0; row < halfW; row++) {
            int rRow = rowUV >> 16;
            if (rRow >= RAINBOW_DIM) {
                rRow = RAINBOW_DIM - 1;
            }
            unsigned char *rSrc = rbuf + rRow * RAINBOW_STRIDE;

            int colUV = (pass == 0) ? 0 : 0xFFFFFF;
            int dx = (pass == 0) ? rightStart : leftStart;
            for (int col = 0; col < halfW; col++) {
                int rCol = (colUV >> 16) * 3;
                int rr = rSrc[rCol + 0] / 2;
                int rg = rSrc[rCol + 1] / 2;
                int rb = rSrc[rCol + 2] / 2;

                int idx = (row * PARA_W + dx) * 4;
                int r = buf32[idx + 0] + rr;
                if (r > 255) {
                    r = 255;
                }
                int g = buf32[idx + 1] + rg;
                if (g > 255) {
                    g = 255;
                }
                int b = buf32[idx + 2] + rb;
                if (b > 255) {
                    b = 255;
                }
                buf32[idx + 0] = (unsigned char)r;
                buf32[idx + 1] = (unsigned char)g;
                buf32[idx + 2] = (unsigned char)b;

                dx++;
                if (dx >= PARA_W) {
                    dx -= PARA_W;
                }
                colUV += (pass == 0) ? uvStep : -uvStep;
            }
            rowUV += uvStep;
        }
    }

    free(rbuf);
}
#endif /* SONICR_SKY32 */

void S3D_LoadAndScaleParallax(const char *filename, int destWidth, int destHeight)
{
    DebugLog("S3D_LoadAndScaleParallax: %s -> %dx%d\n", filename, destWidth, destHeight);

    FILE *fp = fOpen(filename, "rb");
    if (fp == NULL) {
        DebugLog("  Failed to open parallax: %s\n", filename);
        return;
    }

    unsigned short *srcBuf = (unsigned short *)s_parallaxSrcStorage;

    /* Read source file: 0x34000 pixels, 3 bytes each (RGB) */
#ifdef SONICR_DC
    {
        unsigned char *raw = (unsigned char *)malloc((size_t)PARALLAX_SRC_PIXELS * 3);
        if (raw != NULL) {
            fRead(raw, 1, (size_t)PARALLAX_SRC_PIXELS * 3, fp);
            unsigned short *dst = srcBuf;
            if (g_bitsPerPixel == 0x10) {
                /* Dither parallax/sky source to break up 8->5/6-bit banding.
                 * Source is 1664x128; sky/parallax both sample from this. */
                DitherConvertImageRGB565(raw, dst, PARALLAX_SRC_W,
                                         PARALLAX_SRC_PIXELS / PARALLAX_SRC_W);
            }
            else {
                for (int i = 0; i < PARALLAX_SRC_PIXELS; i++) {
                    unsigned char r = raw[i*3+0], g = raw[i*3+1], b = raw[i*3+2];
                    *dst++ = (unsigned short)((int)b >> 3) +
                             (unsigned short)(((int)r >> 3) << 10) +
                             (unsigned short)(((int)g >> 3) << 5);
                }
            }
            free(raw);
            goto parallax_loaded;
        }
    }
#endif
    {
        unsigned char *raw2 = (unsigned char *)malloc((size_t)PARALLAX_SRC_PIXELS * 3);
        if (raw2 != NULL) {
            fRead(raw2, 1, (size_t)PARALLAX_SRC_PIXELS * 3, fp);
            unsigned short *dst = srcBuf;
            if (g_bitsPerPixel == 0x10) {
                for (int i = 0; i < PARALLAX_SRC_PIXELS; i++) {
                    unsigned char r = raw2[i*3+0], g = raw2[i*3+1], b = raw2[i*3+2];
                    *dst++ = (unsigned short)(((int)g >> 2) << 5) +
                             (unsigned short)(((int)r >> 3) << 11) +
                             (unsigned short)((int)b >> 3);
#ifdef SONICR_SKY32
                    s_parallaxSrc32[i * 4 + 0] = r;
                    s_parallaxSrc32[i * 4 + 1] = g;
                    s_parallaxSrc32[i * 4 + 2] = b;
                    s_parallaxSrc32[i * 4 + 3] = 0xFF;
#endif
                }
            }
            else {
                for (int i = 0; i < PARALLAX_SRC_PIXELS; i++) {
                    unsigned char r = raw2[i*3+0], g = raw2[i*3+1], b = raw2[i*3+2];
                    *dst++ = (unsigned short)((int)b >> 3) +
                             (unsigned short)(((int)r >> 3) << 10) +
                             (unsigned short)(((int)g >> 3) << 5);
                }
            }
            free(raw2);
        }
        else if (g_bitsPerPixel == 0x10) {
            unsigned short *dst = srcBuf;
            for (int i = 0; i < PARALLAX_SRC_PIXELS; i++) {
                unsigned char r, g, b;
                fRead(&r, 1, 1, fp);
                fRead(&g, 1, 1, fp);
                fRead(&b, 1, 1, fp);
                *dst++ = (unsigned short)(((int)g >> 2) << 5) +
                         (unsigned short)(((int)r >> 3) << 11) +
                         (unsigned short)((int)b >> 3);
#ifdef SONICR_SKY32
                s_parallaxSrc32[i * 4 + 0] = r;
                s_parallaxSrc32[i * 4 + 1] = g;
                s_parallaxSrc32[i * 4 + 2] = b;
                s_parallaxSrc32[i * 4 + 3] = 0xFF;
#endif
            }
        }
        else {
            unsigned short *dst = srcBuf;
            for (int i = 0; i < PARALLAX_SRC_PIXELS; i++) {
                unsigned char r, g, b;
                fRead(&r, 1, 1, fp);
                fRead(&g, 1, 1, fp);
                fRead(&b, 1, 1, fp);
                *dst++ = (unsigned short)((int)b >> 3) +
                         (unsigned short)(((int)r >> 3) << 10) +
                         (unsigned short)(((int)g >> 3) << 5);
            }
        }
    }
#ifdef SONICR_DC
parallax_loaded:;
#endif

    /* Rainbow overlay for rain weather — FUN_004e0094 via FUN_0042ba9c.
     * Composite onto whichever buffer is actually displayed: the 32-bit
     * source on desktop (the 16bpp srcBuf is only the bind-gate there and is
     * never uploaded), or the 16bpp srcBuf on DC/web. */
#ifdef SONICR_SKY32
    CompositeRainbowOnParallax32(s_parallaxSrc32);
#else
    CompositeRainbowOnParallax(srcBuf);
#endif

    /* srcBuf is static — no free needed */
    fClose(fp);

    /* Folded D3D parallax layout — mirrors FUN_0042B6A0 in the binary.
     * The renderer's UV tables (uvPairs at 0x4FC4EC, vBase at 0x4FC524)
     * sample the panorama as 4 vertical panels stacked into one tpage:
     *
     *   V=0..0.25  : src cols 0..511    (panel 0, full width)
     *   V=0.25..0.5: src cols 512..1023 (panel 1)
     *   V=0.5..0.75: src cols 1024..1535(panel 2)
     *   V=0.75..1  : src cols 1536..1663(panel 3, U=0..0.25 only)
     *
     * 512×512, full source resolution, 1:1 copy (~512KB VRAM).
     */
    #define PARALLAX_TPAGE_W 512
    #define PARALLAX_TPAGE_H 512
    {
        int tpIdx = g_tpageCount;
        int tpW = PARALLAX_TPAGE_W;
        int tpH = PARALLAX_TPAGE_H;

        if (g_tpagePixelBuf[tpIdx] != NULL &&
            (g_tpageWidth[tpIdx] != tpW || g_tpageHeight[tpIdx] != tpH))
        {
            free(g_tpagePixelBuf[tpIdx]);
            g_tpagePixelBuf[tpIdx] = NULL;
        }
        if (g_tpagePixelBuf[tpIdx] == NULL) {
            /* calloc → panel 3's right region (unused cols) stays zero. */
            g_tpagePixelBuf[tpIdx] = calloc((size_t)tpW * (size_t)tpH,
                                            sizeof(unsigned short));
        }

        if (g_tpagePixelBuf[tpIdx] != NULL) {
            unsigned short *dst = (unsigned short *)g_tpagePixelBuf[tpIdx];

            /* full-resolution 1:1 copy per panel. */
            for (int panel = 0; panel < 4; panel++) {
                int srcColStart = panel * 512;
                int panelW = (panel < 3) ? 512 : 128;
                int dstRowStart = panel * 128;
                for (int y = 0; y < 128; y++) {
                    unsigned short *dstRow = dst + (dstRowStart + y) * tpW;
                    unsigned short *srcRow = srcBuf + y * PARALLAX_SRC_W + srcColStart;
                    memcpy(dstRow, srcRow, (size_t)panelW * sizeof(unsigned short));
                }
            }

            ClaimTpage(tpIdx, tpW, tpH, 4);
            /* Folded sky/parallax packs true 6-bit green (S3D 565 path at bits
             * 10:5) — flag before R_SetNoColorKey, which triggers the upload. */
            R_SetTpageGreen6(tpIdx, 1);
            R_SetNoColorKey(tpIdx);
            R_MarkTextureDirty(tpIdx);

#ifdef SONICR_SKY32
            /* Full-precision 32-bit sky: fold the RGBA8 source into a parallel
             * RGBA8 tpage buffer (same panel layout) so the sky uploads at
             * 8-bit-per-channel instead of the banded 16bpp fold above. The
             * 16bpp buffer stays as the bind-gate; GL_UploadTpage prefers this. */
            {
                unsigned char *rgba = (unsigned char *)calloc((size_t)tpW * tpH, 4);
                if (rgba != NULL) {
                    for (int panel32 = 0; panel32 < 4; panel32++) {
                        int srcColStart = panel32 * 512;
                        int panelW = (panel32 < 3) ? 512 : 128;
                        int dstRowStart = panel32 * 128;
                        for (int y = 0; y < 128; y++) {
                            unsigned char *dstRow = rgba + (size_t)(dstRowStart + y) * tpW * 4;
                            unsigned char *srcRow = s_parallaxSrc32 + ((size_t)y * PARALLAX_SRC_W + srcColStart) * 4;
                            memcpy(dstRow, srcRow, (size_t)panelW * 4);
                        }
                    }
                    R_SetTpageRGBA8(tpIdx, rgba);   /* takes ownership */
                }
            }
#endif
        }
    }
    #undef PARALLAX_TPAGE_W
    #undef PARALLAX_TPAGE_H
}

