/**
 * track_geometry_new.c — LoadTrack3 rewrite
 *
 * Reads the per-track BIN file (e.g., ISLAND_E.BIN) and populates:
 *   - g_vertexArrayBase (vertex data, stride 0x40)
 *   - g_polygonArrayBase (polygon data, stride 0x30 = 12 ints)
 *   - g_objectStructArray (object structs, stride 0x44 = 17 ints)
 *   - g_ringSpawnArray (ring positions)
 *   - Position data sections → g_introSplineBase, g_podiumCenter, etc.
 *
 * File format (from analysis + Sonic Retro docs):
 *   - Dword: header length
 *   - headerLength × 128 bytes: header data (skipped)
 *   - Track parts section
 *   - Decoration section
 *   - 7 position data sections
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "sonicr_paths.h"
#include <stdio.h>

/* Externs for globals set by LoadTrack3 */
static int g_subEntryWriteIdx;  /* running index into g_ringSpawnArray, reset per load */
extern int g_objectCount;
extern int g_objectVertexCount;
extern int g_sceneryCount;
extern int g_sceneryVertexCount;
extern int g_doubleSidedCount;
extern int g_totalPositions;
extern int g_posDataCount1, g_posDataCount2, g_posDataCount3;
extern int g_posDataCount5, g_posDataCount6, g_posDataCount7;

/* Position data pointers set after each section */

/* Additional position pointers */
static int *s_positionDataBase;   /* base of all position data */
static int *s_trackBoundary;      /* after section 2 */
static int *s_replayCamera;       /* after section 6 */
static int *s_section7Ptr;        /* after section 7 */

/* Ring spawn array */

/* File reading helpers */
static short ReadShort_LT(FILE *fp) {
    unsigned char lo, hi;
    fRead(&lo, 1, 1, fp);
    fRead(&hi, 1, 1, fp);
    return (short)((hi << 8) | lo);
}

static int ReadDword(FILE *fp) {
    unsigned char b[4];
    fRead(b, 1, 4, fp);
    return (int)(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
}

static unsigned char ReadByte_LT(FILE *fp) {
    unsigned char b;
    fRead(&b, 1, 1, fp);
    return b;
}

/* UV byte → 16.16 fixed with odd-bit rounding */
static int UVByte(unsigned char b) {
    int v = (unsigned int)b << 16;
    if (b & 1) {
        v += 0xFFFF;
    }
    return v;
}

/* LoadTrack3 - 0x00426DB8 */
void LoadTrack3(void)
{
    g_doubleSidedCount = 0;
    g_objectCount = 0;
    g_subEntryWriteIdx = 0;  /* reset sub-entry buffer index for new track */
    /* Start track data AFTER character model data to avoid overwriting it.
     * Character models occupy indices 0..g_modelVertexCount-1 (vertices)
     * and 0..g_modelPolygonCount-1 (polygons). */
    g_vertexIndexRunning = g_modelVertexCount;
    g_polygonIndexRunning = g_modelPolygonCount;
    g_ringCount = 0;
    g_objectVertexCount = 0;
    g_objectPolygonCount = 0;
    g_sceneryCount = 0;
    g_sceneryVertexCount = 0;
    g_sceneryPolygonCount = 0;
    g_totalPositions = 0;

    /* Open the track BIN file */
    if (g_nextLoadFilename == NULL) {
        return;
    }
    g_fileHandle = fOpen(g_nextLoadFilename, "rb");
    if (g_fileHandle == NULL) {
        return;
    }
    FILE *fp = g_fileHandle;

    /* Header: Dword length, then length×128 bytes (skip) */
    int headerLen = ReadDword(fp);
    fSeek(fp, headerLen * 128, SEEK_CUR);

    /* Start AFTER character model data to avoid overwriting it */
    int *polyPtr = (int *)((char *)g_polygonArrayBase + g_modelPolygonCount * 0x30);
    SrcVertex *vtxPtr = &g_vertexArrayBase[g_modelVertexCount];
    int *objPtr = (int *)g_objectStructArray;
    /* Ring data is written via g_ringSpawnArray (same pointer as g_ringSpawnArray)
     * during sub-entry loading below. g_ringCount accumulated per-object. */

    /* TRACK PARTS */
    int numParts = ReadDword(fp);
    g_objectCount = numParts;

    for (int partIdx = 0; partIdx < numParts; partIdx++) {
        /* Object origin: 3 Dwords (Z negated) + 1 Dword (low 16 = bsphere) */
        int originX = ReadDword(fp);
        int originY = ReadDword(fp);
        int originZ = ReadDword(fp);
        originZ = -originZ;  /* negate Z */
        int objFlags = ReadDword(fp);                    /* low 16 bits = bsphere radius */

        /* Store in object struct */
        objPtr[0] = originX;
        objPtr[1] = originY;
        objPtr[2] = originZ;
        *(short *)((char *)objPtr + 0x2E) = 0;          /* mode = 0 (track part) */
        *(unsigned short *)((char *)objPtr + 0x2C) = (unsigned short)(objFlags & 0xFFFF); /* bsphere */
        *(short *)(objPtr + 0x0E) = (short)g_vertexIndexRunning;
        *(short *)(objPtr + 0x0C) = (short)g_polygonIndexRunning;
        *(short *)(objPtr + 0x0D) = (short)g_ringCount;

        /* Vertex count: Dword (only low 16 used by the game).
         * Binary 0x00426fea-0x00426ff5: reads 4 bytes, stores low 16 to obj+0x3a. */
        int vertCount = ReadDword(fp);
        *(unsigned short *)((char *)objPtr + 0x3A) = (unsigned short)vertCount;

        /* Read vertices: 6 fread calls per vertex (3 short pos + 3 values for color) */
        for (int v = 0; v < vertCount; v++) {
            int vx = (int)ReadShort_LT(fp);
            int vy = (int)ReadShort_LT(fp);
            int vz = (int)ReadShort_LT(fp);

            /* Color: read as shorts, convert to 13-bit fixed */
            int cr = (int)(unsigned short)ReadShort_LT(fp);
            int cg = (int)(unsigned short)ReadShort_LT(fp);
            int cb = (int)(unsigned short)ReadShort_LT(fp);

            vtxPtr->posX = originX + vx;
            vtxPtr->posY = originY + vy;
            vtxPtr->posZ = originZ - vz;
            vtxPtr->colorR = cr * 0x2000 + 0x1000;
            vtxPtr->colorG = cg * 0x2000 + 0x1000;
            vtxPtr->colorB = cb * 0x2000 + 0x1000;

            if (g_trackId == TRACK_RADIANT_EMERALD) {
                vtxPtr->colorR = Random() * 0x40 + 0x20;
                vtxPtr->colorG = Random() * 0x40 + 0x20;
                vtxPtr->colorB = Random() * 0x40 + 0x20;
            }

            vtxPtr++;
        }

        /* Face count: Dword */
        int faceCount = ReadDword(fp);
        *(unsigned short *)((char *)objPtr + 0x32) = (unsigned short)faceCount;

        /* Read faces: quad strip — vertex indices are implicit */
        int vertOffset = 0;
        for (int f = 0; f < faceCount; f++) {
            short base = (short)g_vertexIndexRunning + (short)vertOffset;
            *(short *)((char *)polyPtr + 0x22) = base + 1;
            *(short *)(polyPtr + 8) = base;
            *(short *)(polyPtr + 9) = base + 3;
            *(short *)((char *)polyPtr + 0x26) = base + 2;

            /* Read 12 bytes of face data as a block */
            unsigned char faceData[12];
            fRead(faceData, 1, 12, fp);

            *(unsigned char *)(polyPtr + 10) = faceData[0];  /* tpage */
            /* faceData[1] = null */
            polyPtr[0] = UVByte(faceData[2]);
            polyPtr[1] = UVByte(faceData[3]);
            polyPtr[6] = UVByte(faceData[4]);
            polyPtr[7] = UVByte(faceData[5]);
            polyPtr[4] = UVByte(faceData[6]);
            polyPtr[5] = UVByte(faceData[7]);
            polyPtr[2] = UVByte(faceData[8]);
            polyPtr[3] = UVByte(faceData[9]);
            unsigned short flags = (unsigned short)(faceData[10] | (faceData[11] << 8));
            *(unsigned char *)((char *)polyPtr + 0x2E) = (unsigned char)(flags * 2 | 1);
            if ((*(unsigned char *)((char *)polyPtr + 0x2E) & 2) != 0) {
                g_doubleSidedCount++;
            }
            *(unsigned short *)(polyPtr + 0x0B) = 0xFFFF;

            vertOffset += 2;  /* stride 2 verts per face in the strip */
            polyPtr += 12;  /* polygon stride = 0x30 / 4 = 12 ints */
        }

        /* Sub-entry section (binary 0x42739b-0x427439): collision/billboard points
         * for hidden (visFlag==-1) objects. Read dwords until -1 sentinel.
         * Each entry: 3 dwords (X, Z, Y). Y is NEGATED. Stored to g_ringSpawnArray
         * as 16-byte records [X, Z, -Y, 0]. obj+0x34 = start index, obj+0x36 = count. */
        {
            *(unsigned short *)((char *)objPtr + 0x34) = (unsigned short)g_subEntryWriteIdx;
            *(unsigned short *)((char *)objPtr + 0x36) = 0;

            while (1) {
                int sentinel = ReadDword(fp);
                if (sentinel == -1) {
                    break;
                }

                /* Sentinel consumed. Now read 3 data dwords: X, Z, Y. */
                int subX = ReadDword(fp);
                int subZ = ReadDword(fp);
                int subY = ReadDword(fp);

                if (g_ringSpawnArray != NULL && g_subEntryWriteIdx < 1024) {
                    int *entry = (int *)((char *)g_ringSpawnArray + g_subEntryWriteIdx * 16);
                    entry[0] = subX;
                    entry[1] = subZ;
                    entry[2] = -subY;  /* binary negates Y (0x427422: neg ecx; 0x427435: mov [ebx+8],ecx) */
                    entry[3] = 0;
                }
                g_subEntryWriteIdx++;
                *(unsigned short *)((char *)objPtr + 0x36) += 1;
            }
        }

        /* Update running counters — binary 0x426EB1-0x426F14 */
        g_objectVertexCount += vertCount;
        g_objectPolygonCount += faceCount;
        g_vertexIndexRunning += vertCount;
        g_polygonIndexRunning += faceCount;
        /* Accumulate ring count from this object's sub-entries (binary: 0x426F0A-0x426F14) */
        g_ringCount += *(unsigned short *)((char *)objPtr + 0x36);
        objPtr += 17;  /* object stride = 0x44 / 4 = 17 ints */
    }

    /* DECORATION SECTION (decompile lines 3786-3968)
     * Each decoration has: position, pivot, rotation, then tri/quad/vertex data.
     * Object struct entries are populated AFTER the parts entries. */
    int decCount = ReadDword(fp);
    g_sceneryCount = decCount;

    for (int dIdx = 0; dIdx < decCount; dIdx++) {
        /* Position: 3 Dwords (Z negated) + 1 Dword (flags/bsphere) */
        int dPosX = ReadDword(fp);
        int dPosY = ReadDword(fp);
        int dPosZ = ReadDword(fp);
        dPosZ = -dPosZ;
        int dFlags = ReadDword(fp);

        objPtr[0] = dPosX;
        objPtr[1] = dPosY;
        objPtr[2] = dPosZ;
        *(short *)((char *)objPtr + 0x2E) = 1;
        *(unsigned short *)((char *)objPtr + 0x2C) = (unsigned short)(dFlags & 0xFFFF);

        /* Pivot position: 3 Dwords (Z negated) */
        objPtr[8] = ReadDword(fp);
        objPtr[9] = ReadDword(fp);
        int pivZ = ReadDword(fp);
        objPtr[10] = -pivZ;

        /* Rotation: 3 shorts */
        *(unsigned short *)((char *)objPtr + 0x18) = (unsigned short)ReadShort_LT(fp);
        *(unsigned short *)((char *)objPtr + 0x1A) = (unsigned short)ReadShort_LT(fp);
        *(unsigned short *)((char *)objPtr + 0x1C) = (unsigned short)ReadShort_LT(fp);

        /* Vertex/polygon start from running counters */
        *(short *)(objPtr + 0x0E) = (short)g_vertexIndexRunning;
        *(short *)(objPtr + 0x0C) = (short)g_polygonIndexRunning;

        /* Triangles */
        int triCount = ReadDword(fp);
        *(unsigned short *)((char *)objPtr + 0x32) = (unsigned short)triCount;

        for (int t = 0; t < triCount; t++) {
            /* 3 vertex indices (shorts, relative to running vtx index) */
            short vi0 = (short)g_vertexIndexRunning + ReadShort_LT(fp);
            short vi1 = (short)g_vertexIndexRunning + ReadShort_LT(fp);
            short vi2 = (short)g_vertexIndexRunning + ReadShort_LT(fp);
            *(short *)(polyPtr + 9) = vi0;
            *(short *)((char *)polyPtr + 0x22) = vi1;
            *(short *)(polyPtr + 8) = vi2;
            *(short *)((char *)polyPtr + 0x26) = vi0;

            /* 6 UV bytes: UV2, UV1, UV0 (reverse order in file) */
            unsigned char u2 = ReadByte_LT(fp);
            polyPtr[4] = UVByte(u2);
            unsigned char v2 = ReadByte_LT(fp);
            polyPtr[5] = UVByte(v2);
            unsigned char u1 = ReadByte_LT(fp);
            polyPtr[2] = UVByte(u1);
            unsigned char v1 = ReadByte_LT(fp);
            polyPtr[3] = UVByte(v1);
            unsigned char u0 = ReadByte_LT(fp);
            polyPtr[0] = UVByte(u0);
            unsigned char v0 = ReadByte_LT(fp);
            polyPtr[1] = UVByte(v0);

            polyPtr[6] = polyPtr[4];
            polyPtr[7] = polyPtr[5];

            /* Tpage + flags as single Dword (4 bytes) */
            int tpageFlags = ReadDword(fp);
            *(unsigned char *)(polyPtr + 10) = (unsigned char)(tpageFlags & 0xFF); /* tpage */
            unsigned short dflags = (unsigned short)(tpageFlags >> 16);
            *(unsigned char *)((char *)polyPtr + 0x2E) = (unsigned char)(dflags * 2); /* flags */
            *(unsigned short *)(polyPtr + 0x0B) = 0xFFFF; /* mode */

            polyPtr += 12;  /* polygon stride 0x30 / 4 */
        }

        /* Quads */
        int quadCount = ReadDword(fp);                   /* line 3868 */
        *(unsigned short *)((char *)objPtr + 0x32) += (unsigned short)quadCount; /* line 3869 */

        for (int q = 0; q < quadCount; q++) {
            /* 4 vertex indices (shorts, relative) */
            short qi3 = (short)g_vertexIndexRunning + ReadShort_LT(fp);
            short qi0 = (short)g_vertexIndexRunning + ReadShort_LT(fp);
            short qi1 = (short)g_vertexIndexRunning + ReadShort_LT(fp);
            short qi2 = (short)g_vertexIndexRunning + ReadShort_LT(fp);
            *(short *)((char *)polyPtr + 0x26) = qi3;
            *(short *)(polyPtr + 9) = qi0;
            *(short *)((char *)polyPtr + 0x22) = qi1;
            *(short *)(polyPtr + 8) = qi2;

            /* 8 UV bytes: UV3, UV2, UV1, UV0 */
            polyPtr[6] = UVByte(ReadByte_LT(fp));
            polyPtr[7] = UVByte(ReadByte_LT(fp));
            polyPtr[4] = UVByte(ReadByte_LT(fp));
            polyPtr[5] = UVByte(ReadByte_LT(fp));
            polyPtr[2] = UVByte(ReadByte_LT(fp));
            polyPtr[3] = UVByte(ReadByte_LT(fp));
            polyPtr[0] = UVByte(ReadByte_LT(fp));
            polyPtr[1] = UVByte(ReadByte_LT(fp));

            /* Tpage + flags as single Dword (4 bytes) */
            int qtpageFlags = ReadDword(fp);
            *(unsigned char *)(polyPtr + 10) = (unsigned char)(qtpageFlags & 0xFF); /* tpage */
            unsigned short qflags = (unsigned short)(qtpageFlags >> 16);
            *(unsigned char *)((char *)polyPtr + 0x2E) = (unsigned char)(qflags * 2 | 1); /* flags|quad bit */
            *(unsigned short *)(polyPtr + 0x0B) = 0xFFFF;

            polyPtr += 12;
        }

        /* Vertices */
        int dVertCount = ReadDword(fp);
        *(unsigned short *)((char *)objPtr + 0x3A) = (unsigned short)dVertCount;

        for (int dv = 0; dv < dVertCount; dv++) {
            /* Position: 3 shorts (Z negated) — decompile reads 4 bytes >> 16,
             * equivalent to reading a short when file stores 16-bit values. */
            int dvx = (int)ReadShort_LT(fp);
            int dvy = (int)ReadShort_LT(fp);
            int dvz = (int)ReadShort_LT(fp);
            dvz = -dvz;

            vtxPtr->posX = dvx;
            vtxPtr->posY = dvy;
            vtxPtr->posZ = dvz;

            /* Colors: 3 bytes → 13-bit fixed point */
            unsigned char cr = ReadByte_LT(fp);
            unsigned char cg = ReadByte_LT(fp);
            unsigned char cb = ReadByte_LT(fp);
            /* unsigned char cpad = */ ReadByte_LT(fp); // padding byte
            vtxPtr->colorR = (unsigned int)cr * 0x2000 + 0x1000;
            vtxPtr->colorG = (unsigned int)cg * 0x2000 + 0x1000;
            vtxPtr->colorB = (unsigned int)cb * 0x2000 + 0x1000;

            if (g_trackId == TRACK_RADIANT_EMERALD) {
                vtxPtr->colorR = Random() * 0x40 + 0x20;
                vtxPtr->colorG = Random() * 0x40 + 0x20;
                vtxPtr->colorB = Random() * 0x40 + 0x20;
            }

            vtxPtr++;
        }

        /* Update running counters (line 3962-3966) */
        g_sceneryVertexCount += dVertCount;
        g_sceneryPolygonCount += triCount + quadCount;
        g_vertexIndexRunning += dVertCount;
        g_polygonIndexRunning += triCount + quadCount;
        objPtr += 17;  /* object stride 0x44 / 4 */
    }

    /* 7 POSITION DATA SECTIONS */
    /* Each section: Dword count, then count × 3 Dwords (X, Y, -Z).
     * Section 7 uses 3 Words (6 bytes) per entry instead of 3 Dwords. */
    static int s_posDataBuf[32768];
    int *posPtr = s_posDataBuf;
    int cnt;

    /* Section 1: track path */
    s_positionDataBase = posPtr;
    g_posDataCount1 = cnt = ReadDword(fp);
    for (int i = 0; i < cnt; i++) {
        *posPtr++ = ReadDword(fp);
        *posPtr++ = ReadDword(fp);
        *posPtr++ = -ReadDword(fp);
    }

    /* Section 2: intro camera spline */
    g_introSplineBase = posPtr;
    g_posDataCount2 = cnt = ReadDword(fp);
    for (int i = 0; i < cnt; i++) {
        *posPtr++ = ReadDword(fp);
        *posPtr++ = ReadDword(fp);
        *posPtr++ = -ReadDword(fp);
    }

    /* Section 3: boundary waypoints */
    g_trackBoundaryWaypoints = posPtr;     /* binary: 0x484F5D */
    s_trackBoundary = posPtr;
    g_posDataCount3 = cnt = ReadDword(fp);
    for (int i = 0; i < cnt; i++) {
        *posPtr++ = ReadDword(fp) >> 12;
        *posPtr++ = ReadDword(fp) >> 12;
        *posPtr++ = -(ReadDword(fp) >> 12);
    }

    /* Section 4: spline waypoints */
    g_splineWaypoints = posPtr;
    g_posDataCount4 = cnt = ReadDword(fp);
    for (int i = 0; i < cnt; i++) {
        *posPtr++ = ReadDword(fp);
        *posPtr++ = ReadDword(fp);
        *posPtr++ = -ReadDword(fp);
    }

    /* Section 5: player start/end positions → g_podiumCenter */
    g_podiumCenter = posPtr;
    g_posDataCount5 = cnt = ReadDword(fp);
    for (int i = 0; i < cnt; i++) {
        *posPtr++ = ReadDword(fp);
        *posPtr++ = ReadDword(fp);
        *posPtr++ = -ReadDword(fp);
    }

    /* Section 6: flyover camera waypoints — binary 0x427FC4: [0x901EE4] = esi
     * Used by UpdateFlyoverCamera for pre-race flyover path. */
    s_replayCamera = posPtr;
    g_waypointTablePtr = posPtr;                 /* 0x00901EE4 */
    g_posDataCount6 = cnt = ReadDword(fp);
    g_numWaypoints = cnt;                        /* 0x00901EE8 — flyover camera waypoint count */
    for (int i = 0; i < cnt; i++) {
        *posPtr++ = ReadDword(fp);
        *posPtr++ = ReadDword(fp);
        *posPtr++ = -ReadDword(fp);
    }

    /* Gate waypoint pointer — binary 0x42805F: [0x901EF0] = posPtr
     * Points to end of Section 6 data. ProcessTrackTriggerResponse
     * indexes backwards from this pointer using gate state. */
    g_gateWaypointPtr = posPtr;

    /* Section 7: metadata (3 Words per entry, 6 bytes each) */
    s_section7Ptr = posPtr;
    g_posDataCount7 = cnt = ReadDword(fp);
    /* Cap to available file data to avoid overrun */
    for (int i = 0; i < cnt && !feof(fp); i++) {
        *posPtr++ = (int)ReadShort_LT(fp);
        *posPtr++ = -(int)ReadShort_LT(fp);
        *posPtr++ = (int)ReadShort_LT(fp);
    }

    g_totalPositions = g_posDataCount1 + g_posDataCount2 + g_posDataCount3 +
                       g_posDataCount4 + g_posDataCount5 + g_posDataCount6 +
                       g_posDataCount7;

    fClose(fp);
    g_fileHandle = NULL;

    /* Set g_totalObjects so DrawCharacterSpritesSoft knows how many to iterate.
     * In the original: _DAT_006ead30 = numParts + decCount.
     * Decorations aren't loaded into objectStructArray yet, so only count parts. */
    g_totalObjects = g_objectCount + g_sceneryCount;
    /* bsphere is now loaded from the BIN file's 4th Dword per object (low 16 bits) */
}

/* =====================================================================
 * InitTrackObjectModes — 0x0047256C — 2554 bytes
 * Per-track: marks specific decoration objects as mode 2 (rotatable),
 * then computes initial vertex positions for all decorations.
 * Called once during track init for each track.
 *
 * For each decoration object (mode != 0):
 *   1. Check object index against per-track list → set mode = 2
 *   2. Track 4 (Factory): apply UV rotation to specific objects
 *   3. Tracks 1-5: copy vertex world positions from local + pivot
 *   4. Other: build rotation matrix from yaw, transform vertices
 * ===================================================================== */
void InitTrackObjectModes(void)
{
    int totalObj = g_totalObjects;                              /* 0x472581 */
    int objByteOff = 0;                                         /* ebp-0x58 */

    for (int i = 0; i < totalObj; i++, objByteOff += 0x44) {
        char *ecx = (char *)g_objectStructArray + objByteOff;  /* 0x47259D */
        int mode = *(short *)(ecx + 0x2E);                      /* 0x4725A5 */

        if (mode == 0) {
            continue;                                /* 0x4725AD: track part, skip */
        }

        /* Per-track mode 2 assignment (jump table at 0x472558) */
        int trackId = g_trackId;                                /* 0x4729B3: [0x8FB8EC] */

        if (trackId == TRACK_RESORT_ISLAND) {                                     /* 0x4725D3 */
            static const int island_mode2[] = {
                0x1F2,0x1F7,0x259,0x1F4,0x258,0x1F3,0x25E,0x25F,0x1F6,0x25C,
                0x25D,0x223,0x21F,0x21D,0x21E,0x221,0x225,0x224,0x220,0x222,-1
            };
            const int *p;
            for (p = island_mode2; *p != -1; p++) {
                if (i == *p) { 
                    *(short *)(ecx + 0x2E) = 2;
                    goto next_obj;
                }
            }
        }
        else if (trackId == TRACK_RADICAL_CITY) {                                /* 0x472691 */
            static const int city_mode2[] = {
                0x383,0x2C3,0x3DF,0x3E0,0x37C,0x3ED,0x382,0x3F4,0x3F5,0x3F6,
                0x3F7,0x380,0x381,0x3D5,0x3D6,0x3D7,0x3D8,0x3D9,0x3DA,0x3DB,
                0x3DC,0x3DD,-1
            };
            const int *p;
            for (p = city_mode2; *p != -1; p++) {
                if (i == *p) {
                    *(short *)(ecx + 0x2E) = 2;
                    goto next_obj;
                }
            }
        }
        /* binary trackId 4 block at 0x47277F */
        else if (trackId == TRACK_REGAL_RUIN) {
            static const int ruin_mode2[] = {
                0x310,0x30C,0x3D5,0x3D6,0x30F,0x3DB,0x3DC,0x3DD,0x3DE,0x327,
                0x328,0x3CC,0x3CD,0x3CE,0x3CF,0x3D0,0x3D1,0x3D2,0x3D3,0x3D4,
                0x312,0x3DF,0x3E1,0x3E2,0x315,0x3E3,0x3E4,0x3E5,-1
            };
            const int *p;
            for (p = ruin_mode2; *p != -1; p++) {
                if (i == *p) {
                    *(short *)(ecx + 0x2E) = 2;
                    goto next_obj;
                }
            }
        }
        /* binary trackId 3 block at 0x4728B5 */
        else if (trackId == TRACK_REACTIVE_FACTORY) {
            static const int factory_mode2[] = {
                0x30C,0x30A,0x3D3,0x3D4,0x30B,0x3D5,0x3D6,0x3D7,0x3D8,0x309,
                0x30D,0x346,0x347,0x348,0x349,0x34A,0x34B,0x34C,0x34D,0x34E,-1
            };
            const int *p;
            for (p = factory_mode2; *p != -1; p++) {
                if (i == *p) {
                    *(short *)(ecx + 0x2E) = 2;
                    goto next_obj;
                }
            }
            /* Factory also has a range: 0x370..0x39D */
            if (i >= 0x370 && i <= 0x39D) {
                *(short *)(ecx + 0x2E) = 2;
                goto next_obj;
            }
        }
        else if (trackId == TRACK_RADIANT_EMERALD) {                                /* 0x47299F */
            if (i == 0x33E) {
                *(short *)(ecx + 0x2E) = 2;
                goto next_obj;
            }
        }

        /* REGAL RUIN object UV rotation — binary 0x4729C6 `cmp trackId,4`
         * = Ruin in binary convention; objects 0x31D/0x3EE are Ruin-track anim objects) */
        if (g_trackId == TRACK_REGAL_RUIN) {                                          /* 0x4729C4 */
            if (i == 0x31D || i == 0x3EE) {                     /* 0x4729D2/0x4729DA */
                unsigned short uv = *(unsigned short *)(ecx + 0x1A);
                uv = (unsigned short)((uv - 0x200) & 0x0FFF);   /* 0x4729E6-0x4729EF */
                *(unsigned short *)(ecx + 0x1A) = uv;
            }
        }

        /* Vertex position update for tracks 1-5 — 0x4729F3 */
        if (trackId >= TRACK_RESORT_ISLAND && trackId <= TRACK_RADIANT_EMERALD) {                     /* 0x4729F9-0x472A10 */
            /* Simple path: copy vertex world positions from local + pivot */
            unsigned short vtxStart = *(unsigned short *)(ecx + 0x38);  /* 0x472A1A */
            unsigned short vtxCount = *(unsigned short *)(ecx + 0x3A);  /* 0x472A23 */
            SrcVertex *vtxBase = &g_vertexArrayBase[vtxStart];

            int pivotX = *(int *)(ecx + 0x20);                 /* 0x472A29 */
            int pivotY = *(int *)(ecx + 0x24);
            int pivotZ = *(int *)(ecx + 0x28);

            for (unsigned short vi = 0; vi < vtxCount; vi++) {
                SrcVertex *v = &vtxBase[vi];
                int localX = v->posX;
                int localY = v->posY;
                int localZ = v->posZ;
                v->posX = localX + pivotX;                       /* 0x472A42 */
                v->posY = localY + pivotY;                       /* 0x472A45 */
                int negZ = -(localZ) - pivotZ;                   /* 0x472A3F-0x472A41 */
                v->posZ = -negZ;                                 /* 0x472A55-0x472A58: neg then store */
            }
        }
        else {
            /* Non-standard: build rotation matrix from yaw, transform vertices */
            /* Read yaw angle from obj+0x18 */
            int yawRaw = *(int *)(ecx + 0x18);
            int yaw = (yawRaw >> 16) & 0xFFF;                   /* 0x472A64-0x472A77 */
            int yawIdx = (yaw + 0x400) & 0xFFF;                 /* 0x472A6E: +0x400 = +90° */

            /* UV index from obj+0x1C */
            int uvRaw = *(unsigned short *)(ecx + 0x1C);
            int uvIdx = uvRaw & 0x0FFF;

            /* Sin/cos lookups for rotation matrix */
            int sinY = g_sinTable[yawIdx] >> 2;                 /* 0x472A8A */
            int cosY = g_cosTable[yawIdx] >> 2;                 /* 0x472A92 */
            int sinU = g_sinTable[uvIdx * 4] >> 2;              /* from [esi + 0x92568C] */
            int cosU = g_cosTable[uvIdx * 4] >> 2;

            /* Build 3x3 rotation matrix (yaw × UV rotation).
             * Binary uses the Watcom imul-by-0 pattern,
             * producing a simplified 2-angle rotation matrix. */

            /* First stage: yaw rotation */
            int L00 = (cosY * sinU) >> 12;                      /* ebp-0x40 */
            int L02 = (-(sinU << 12)) >> 12;                    /* = -sinU, ebp-0x38 */
            int L10 = ((sinU << 12) + 0) >> 12;                 /* ebp-0x34 (term_0a=0) */
            int L20 = (cosY * 0) >> 12;                         /* ebp-0x2C = 0 */
            int L01 = ((sinY * cosU) >> 12);                    /* ebp-0x28 (from sinY*cosU) */
            int L21 = (sinU * sinY) >> 12;                      /* ebp-0x20 */

            /* Apply UV rotation */
            int R10 = (L00 * cosU) >> 12;
            int R11 = (L00 * sinU) >> 12;
            int R20 = (L10 * cosU) >> 12;
            int R21 = (L10 * sinU) >> 12;
            int R30 = (L21 * cosU - cosY * sinU) >> 12;
            int R31 = (sinU * L21 + cosU * cosY) >> 12;

            /* Final rotation matrix elements */
            int m00 = R10;   int m01 = R11;
            int m10 = R20;   int m11 = R21;
            int m20 = R30;   int m21 = R31;
            int m02 = L02;   int m12 = L20;   int m22 = L01;

            /* Transform vertices through rotation matrix */
            unsigned short vtxStart = *(unsigned short *)(ecx + 0x38);
            SrcVertex *vtxBase = &g_vertexArrayBase[vtxStart];
            unsigned short vtxCount = *(unsigned short *)(ecx + 0x3A);

            int pivotX = *(int *)(ecx + 0x20);
            int pivotY = *(int *)(ecx + 0x24);
            int pivotZ = *(int *)(ecx + 0x28);

            for (unsigned short vi = 0; vi < vtxCount; vi++) {
                SrcVertex *v = &vtxBase[vi];
                int lx = v->posX - pivotX;                      /* local - pivot */
                int ly = v->posY - pivotY;
                int lz = -(v->posZ) - pivotZ;                   /* negated Z */

                /* Rotate: result = M × (local - pivot) */
                int rx = (m00*lx + m10*ly + m20*lz) / 4096;     /* SDIV4096 */
                int ry = (m01*lx + m11*ly + m21*lz) / 4096;
                int rz = (m02*lx + m12*ly + m22*lz) / 4096;

                /* Store rotated + pivot */
                v->posX = rx + pivotX;                           /* 0x472D57 */
                v->posY = ry + pivotY;                           /* 0x472D62 */
                v->posZ = -(rz - pivotZ);                       /* 0x472D6B-0x472D75: negate */
            }
        }

    next_obj: (void)0;
    }
}
