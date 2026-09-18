/**
 * model_load.c — Character model and animation loading
 *
 * LoadCharacterModels calls LoadModelData 10 times (once per character).
 * LoadCharacterAnimations calls LoadAnimationData ~130 times.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "endian_util.h"
#include <math.h>

/* Static buffer for GRD file loading — largest is MROBOT.GRD at 44832 bytes */
static uint32_t __attribute__((aligned(32))) s_grdStorage[11208];
#include "sonicr_paths.h"

/* Forward declarations */
void LoadModelData(const char *filename);
void LoadAnimationData(const char *filename, int charId);

/* Animation frame data — accessed by RenderCharacterCredits and
 * RenderCharacterOnPodium for limb transforms.
 * Original base address: 0x0075EB18 */
#define ANIM_FRAME_MAX_ENTRIES 16384
int g_animFrameData[ANIM_FRAME_MAX_ENTRIES * 6];

/* Model data tables */

/* Per-character model section sizes (hardcoded in LoadCharacterModels) */
static const int s_charPolyCount[] = {
    0x72, 0x50, 0x6A, 0x76, 0xC2, 0x72, 0x6A, 0x69, 0xA5, 0x80
};

/* Character model filenames, indexed by charId 0-9 */
static const char *s_charModelFiles[10] = {
    PATH_MDL_SONIC,          /* 0: Sonic */
    PATH_MDL_TAILS,          /* 1: Tails */
    PATH_MDL_KNUCKLES,       /* 2: Knuckles */
    PATH_MDL_AMY,            /* 3: Amy */
    PATH_MDL_EGGMAN,         /* 4: Eggman */
    PATH_MDL_METALSONIC,     /* 5: Metal Sonic */
    PATH_MDL_TAILSDOLL,      /* 6: Tails Doll */
    PATH_MDL_METALKNUCKLES,  /* 7: Metal Knuckles */
    PATH_MDL_EGGROBO,        /* 8: Egg Robo */
    PATH_MDL_SUPERSONIC,     /* 9: Super Sonic */
};

extern int g_footShadowVerts[];  /* 0x00909320 */
extern int g_footShadowCtrl[];   /* 0x00908E20 */

/* Character bounding box dimensions table at ROM 0x501600 */
/* 10 entries × 3 ints (width, height, depth) = 12 bytes each */
static const int g_charBBoxDims[10 * 3] = {
    10, 16, 11,     /* 0: Sonic */
     8, 13, -9,     /* 1: Tails */
    12, 18, 11,     /* 2: Knuckles */
     8, 12, 32,     /* 3: Amy */
     8, 12,  0,     /* 4: Eggman */
     8, 12,  0,     /* 5 */
     8, 12,  0,     /* 6 */
     8, 12,  0,     /* 7 */
     8, 20,-21,     /* 8: Metal Sonic */
    10, 16, 11,     /* 9 */
};

/* Signed fixed-point division by 4096, rounding toward zero (matches binary pattern) */
static inline int sdiv4096(int v) { return v / 4096; }

/* Character GRD (Gouraud shading) filenames — from SONICR.EXE 0x52F792-0x52F8A4 */
static const char *s_charGrdFiles[10] = {
    DATA_DIR SEP "BIN" SEP "OBJECTS" SEP "SONIC"    SEP "SONIC_H.GRD",
    DATA_DIR SEP "BIN" SEP "OBJECTS" SEP "TAILS"    SEP "TAILS_H.GRD",
    DATA_DIR SEP "BIN" SEP "OBJECTS" SEP "KNUCKLES" SEP "KNUCK_H.GRD",
    DATA_DIR SEP "BIN" SEP "OBJECTS" SEP "AMY"      SEP "AMY_H3.GRD",
    DATA_DIR SEP "BIN" SEP "OBJECTS" SEP "ROBOTNIK" SEP "ROBOTNIK.GRD",
    DATA_DIR SEP "BIN" SEP "OBJECTS" SEP "MSONIC"   SEP "MSONIC.GRD",
    DATA_DIR SEP "BIN" SEP "OBJECTS" SEP "DTAILS"   SEP "DTAILS.GRD",
    DATA_DIR SEP "BIN" SEP "OBJECTS" SEP "MKNUCK"   SEP "MKNUCK.GRD",
    DATA_DIR SEP "BIN" SEP "OBJECTS" SEP "MROBOT"   SEP "MROBOT.GRD",
    DATA_DIR SEP "BIN" SEP "OBJECTS" SEP "SSONIC"   SEP "SSONIC.GRD",
};

/**
 * RelocateModelVertices — FUN_00421790 — 104 bytes
 *
 * Relocates model vertex data from file-relative offsets to absolute addresses.
 * Processes 4 vertex pairs per iteration at stride 0x30 (48 bytes).
 *
 * Even fields (positions): mask 19 low bits and add newBase.
 * Odd fields (pointers):   subtract oldBase and add fileBase.
 *
 * Original: EAX=vertexArray, EDX=count, EBX=newBase, ECX=oldBase,
 *           stack param=fileBase. Returns via ret 4.
 */
void RelocateModelVertices(int *arr, int count, int newBase, int oldBase, int fileBase)
{
    for (int i = 0; i < count; i++) {
        arr[0] = (arr[0] & 0x7FFFF) + newBase;
        arr[1] = arr[1] - oldBase + fileBase;
        arr[2] = (arr[2] & 0x7FFFF) + newBase;
        arr[3] = arr[3] - oldBase + fileBase;
        arr[4] = (arr[4] & 0x7FFFF) + newBase;
        arr[5] = arr[5] - oldBase + fileBase;
        arr[6] = (arr[6] & 0x7FFFF) + newBase;
        arr[7] = arr[7] - oldBase + fileBase;
        arr += 12;  /* stride 0x30 = 12 ints */
    }
}

/**
 * LoadCharacterGouraud — loads GRD file into g_charLightingTable.
 *
 * GRD format: 32 lighting directions * vertexCount vertices * 3 RGB bytes,
 * direction-major — every vertex for direction 0, then direction 1, and so on.
 * UpdateVertexLightingPhase indexes the table the same way, selecting a
 * direction row from the player's yaw, so the file is copied verbatim.
 * Matches LoadLightingTable (0x42e834), which fills the same table.
 *
 * Each byte converted to 13-bit fixed point: byte * 0x2000 + 0x1000.
 */
static void LoadCharacterGouraud(int charIdx)
{
    FILE *fp = fOpen(s_charGrdFiles[charIdx], "rb");
    if (!fp) {
        DebugLog("LoadCharacterGouraud: FAILED to open %s\n", s_charGrdFiles[charIdx]);
        return;
    }

    int vertexStart = g_modelMeta[charIdx].vertexStart;
    int vertexCount = g_modelMeta[charIdx].vertexCount;

    /* Read entire GRD file */
    unsigned char *grd = (unsigned char *)s_grdStorage;
    fRead(grd, 1, vertexCount * 24/* 96 */, fp);
    fRead(grd + (vertexCount * 24), 1, vertexCount * 24/* 96 */, fp);
    fRead(grd + (vertexCount * 48), 1, vertexCount * 24/* 96 */, fp);
    fRead(grd + (vertexCount * 72), 1, vertexCount * 24/* 96 */, fp);
    fClose(fp);

    /* Straight copy: the model's slice starts at vertexStart * 0x60 ints and
     * runs vertexCount * 32 RGB entries, in file order. */
    int *dst = g_charLightingTable + vertexStart * 0x60;
    int byteCount = vertexCount * 96;
    for (int i = 0; i < byteCount; i++) {
        dst[i] = (int)grd[i] * 0x2000 + 0x1000;
    }

    /* grd is static — no free needed */
}

/**
 * LoadCharacterModels — 0x00475D58 — 565 bytes
 * Resets model counters and calls LoadModelData 10 times.
 */
void LoadCharacterModels(void)
{
    g_modelCount = 0;
    g_modelVertexCount = 0;
    g_modelPolygonCount = 0;

    /* Load 10 character models (geometry) */
    for (int i = 0; i < 10; i++) {
        LoadModelData(s_charModelFiles[i]);
    }

    /* Write per-character polygon counts into model metadata table */
    for (int i = 0; i < 10; i++) {
        g_modelMeta[i].polyCount = s_charPolyCount[i];
    }

    /* Per-character meta table init (binary 0x475DEE-0x475F8C)
     * Sets boundRadius, lightGroup and charHeight from hardcoded ROM values.
     * These are required for culling, shadow rendering and ground-height
     * collision. */
    #define META_SET(cid, field, val) (g_modelMeta[cid].field = (val))

    /* boundRadius (+0x20): RenderCharacterD3D culls against this as a
     * bounding-sphere radius (cz - boundRadius vs far, cz < -boundRadius). */
    META_SET(0, boundRadius, 0x72);   /* 0x475DF3: Sonic */
    META_SET(1, boundRadius, 0x50);   /* 0x475DF9: Tails */
    META_SET(2, boundRadius, 0x6A);   /* 0x475E2F: Knuckles */
    META_SET(3, boundRadius, 0x76);   /* 0x475E35: Amy */
    META_SET(4, boundRadius, 0xC2);   /* 0x475E5C: Eggman */
    META_SET(5, boundRadius, 0x72);   /* 0x475E67: Metal Sonic */
    META_SET(6, boundRadius, 0x6A);   /* 0x475E9E: TDoll */
    META_SET(7, boundRadius, 0x69);   /* 0x475EAF: MKnux */
    META_SET(8, boundRadius, 0xA5);   /* 0x475ED9: Egg Robo */
    META_SET(9, boundRadius, 0x80);   /* 0x475EE2: Super */

    /* lightGroup (+0x24): shadow bounding size — controls shadow quad extent */
    META_SET(0, lightGroup, 0x16);   /* 0x475EFB: Sonic = 22 */
    META_SET(1, lightGroup, 0x10);   /* 0x475DFF: Tails = 16 */
    META_SET(2, lightGroup, 0x1A);   /* 0x475F21: Knuckles = 26 */
    META_SET(3, lightGroup, 0x21);   /* 0x475E3B: Amy = 33 */
    META_SET(4, lightGroup, 0x22);   /* 0x475F41: Eggman = 34 */
    META_SET(5, lightGroup, 0x16);   /* 0x475E6D: Metal Sonic = 22 */
    META_SET(6, lightGroup, 0x14);   /* 0x475F6F: TDoll = 20 */
    META_SET(7, lightGroup, 0x13);   /* 0x475EB8: MKnux = 19 */
    META_SET(8, lightGroup, 0x1E);   /* 0x475F7F: Egg Robo = 30 */
    META_SET(9, lightGroup, 0x18);   /* 0x475EF5: Super = 24 */

    /* charHeight (+0x28): used by ground collision and trigger checks */
    META_SET(0, charHeight, 0x23);   /* 0x475F16: Sonic = 35 */
    META_SET(1, charHeight, 0x1E);   /* 0x475F1B: Tails = 30 */
    META_SET(2, charHeight, 0x24);   /* 0x475F36: Knuckles = 36 */
    META_SET(3, charHeight, 0x23);   /* 0x475F3B: Amy = 35 */
    META_SET(4, charHeight, 0x0A);   /* 0x475F7A: Eggman = 10 */
    META_SET(5, charHeight, 0x23);   /* 0x475F47: Metal Sonic = 35 */
    META_SET(6, charHeight, 0x23);   /* 0x475F4D: TDoll = 35 */
    META_SET(7, charHeight, 0x23);   /* 0x475F53: MKnux = 35 */
    META_SET(8, charHeight, 0x23);   /* 0x475F59: Egg Robo = 35 */
    META_SET(9, charHeight, 0x23);   /* 0x475F5F: Super = 35 */

    #undef META_SET

    /* Binary 0x475DEE-0x475F01: Set per-polygon type flags and modify
     * a specific Eggman polygon's UVs and flags.
     * eax = Amy's polygon start (meta[3]+0x08), used to index g_polyTypeTable. */
    {
        int amyPolyStart = g_modelMeta[3].polyStart;  /* 0x475DEE: [0x71319C] */

        /* 0x475E05-0x475E26: Set 4 consecutive polyType bytes to 1 */
        g_polyTypeTable[amyPolyStart + 0x71] = 1;   /* 0x475E05: [eax+0x8F6499] — 0x8F6499-0x8F6428=0x71 */
        g_polyTypeTable[amyPolyStart + 0x72] = 1;   /* 0x475E10 */
        g_polyTypeTable[amyPolyStart + 0x73] = 1;   /* 0x475E1B */
        g_polyTypeTable[amyPolyStart + 0x74] = 1;   /* 0x475E26 */

        /* 0x475E2C-0x475EF5: Modify Eggman vehicle polygon UVs and flags.
         * polyIdx = amyPolyStart + 0x22 — binary: lea edx,[eax+0x22]
         * poly = g_charFaceBase + polyIdx * 0x30 */
        int polyIdx = amyPolyStart + 0x22;                                    /* 0x475E2C */
        int *poly = (int *)((char *)g_charFaceBase + polyIdx * 0x30);         /* 0x475E46-0x475E62 */

        /* Set UV values (16.16 fixed point) */
        poly[6] = 0x6FFFFF;    /* +0x18: 0x475E73 */
        poly[3] = 0x1EFFFF;    /* +0x0C: 0x475E7F */
        poly[4] = 0x400000;    /* +0x10: 0x475E8B */
        poly[7] = 0x100000;    /* +0x1C: 0x475E97 */

        /* Set double-sided flag (bit 2 of flags byte at +0x2E) */
        unsigned char *flags = (unsigned char *)poly + 0x2E;                  /* 0x475EA4 */
        *flags |= 4;                                                          /* 0x475EAA-0x475EB5 */

        /* Copy UV pairs: UV[0,1] = UV[3], UV[0b,1b] = UV[1,2] */
        poly[0] = poly[6];     /* +0x00 = +0x18: 0x475EAD */
        poly[1] = poly[3];     /* +0x04 = +0x0C: 0x475EC6 */
        poly[2] = poly[4];     /* +0x08 = +0x10: 0x475ED6 */
        poly[5] = poly[7];     /* +0x14 = +0x1C: 0x475EE8 */

        /* 0x475EF0-0x475F01: Set polyType byte for TDoll character */
        int tdollPolyStart = g_modelMeta[6].polyStart;  /* [0x71328C] */
        g_polyTypeTable[tdollPolyStart] = 1;                                  /* 0x475F01: [eax+0x8F6428] */
    }

    /* Load GRD (Gouraud shading) files into g_charLightingTable */
    for (int i = 0; i < 10; i++) {
        LoadCharacterGouraud(i);
    }
}

#define ANIM(dir, prefix, num) PATH_OBJECTS dir SEP prefix #num ".BIN"

static void LoadCharAnims(int charId, const char **files, int count) {
    /* Set META(0x18) = entry start index for this character
     * VALIDATED: binary reads [0x713074] (g_modelFrameCount) */
    g_modelMeta[charId].animFrameBase = g_modelFrameCount;
    for (int i = 0; i < count; i++) {
        LoadAnimationData(files[i], charId);
    }
}

/**
 * LoadCharacterAnimations — 0x00475F90 — 1983 bytes — VALIDATED
 * Calls LoadAnimationData 128 times, grouped by character.
 * After each character's animations are loaded, saves the cumulative
 * frame count.
 *
 * The ~130 individual calls are NOT a loop — they're hardcoded sequential
 * calls in the original binary, one per animation sequence.
 */
void LoadCharacterAnimations(void)
{
    /* Per-character animation file tables.
    * Each character has N animation files loaded sequentially. */
    g_modelFrameCount = 0;

    /* VALIDATED: binary sets [0x713074]=0 and meta[0].offset_0x18=0 (0x7130BC=0)
     * The meta write for char 0 happens in LoadCharAnims below. */

    /* Character 0 (Sonic): 14 animations */
    static const char *s_sonicAnims[] = {
        ANIM("SONIC","SAN",1), ANIM("SONIC","SAN",2), ANIM("SONIC","SAN",3),
        ANIM("SONIC","SAN",4), ANIM("SONIC","SAN",5), ANIM("SONIC","SAN",6),
        ANIM("SONIC","SAN",7), ANIM("SONIC","SAN",8), ANIM("SONIC","SAN",9),
        ANIM("SONIC","SAN",10), ANIM("SONIC","SAN",11), ANIM("SONIC","SAN",12),
        ANIM("SONIC","SAN",13), ANIM("SONIC","SAN",14),
    };
    LoadCharAnims(0, s_sonicAnims, 14);

    /* Character 1 (Tails): 15 animations */
    static const char *s_tailsAnims[] = {
        ANIM("TAILS","TAN",1), ANIM("TAILS","TAN",2), ANIM("TAILS","TAN",3),
        ANIM("TAILS","TAN",4), ANIM("TAILS","TAN",5), ANIM("TAILS","TAN",6),
        ANIM("TAILS","TAN",7), ANIM("TAILS","TAN",8), ANIM("TAILS","TAN",9),
        ANIM("TAILS","TAN",10), ANIM("TAILS","TAN",11), ANIM("TAILS","TAN",12),
        ANIM("TAILS","TAN",13), ANIM("TAILS","TAN",14), ANIM("TAILS","TAN",15),
    };
    LoadCharAnims(1, s_tailsAnims, 15);

    /* Character 2 (Knuckles): 15 animations (KAN1-15, not 16) */
    static const char *s_knuxAnims[] = {
        ANIM("KNUCKLES","KAN",1), ANIM("KNUCKLES","KAN",2), ANIM("KNUCKLES","KAN",3),
        ANIM("KNUCKLES","KAN",4), ANIM("KNUCKLES","KAN",5), ANIM("KNUCKLES","KAN",6),
        ANIM("KNUCKLES","KAN",7), ANIM("KNUCKLES","KAN",8), ANIM("KNUCKLES","KAN",9),
        ANIM("KNUCKLES","KAN",10), ANIM("KNUCKLES","KAN",11), ANIM("KNUCKLES","KAN",12),
        ANIM("KNUCKLES","KAN",13), ANIM("KNUCKLES","KAN",14), ANIM("KNUCKLES","KAN",15),
    };
    LoadCharAnims(2, s_knuxAnims, 15);

    /* Character 3 (Amy): 11 animations */
    static const char *s_amyAnims[] = {
        ANIM("AMY","AAN",1), ANIM("AMY","AAN",2), ANIM("AMY","AAN",3),
        ANIM("AMY","AAN",4), ANIM("AMY","AAN",5), ANIM("AMY","AAN",6),
        ANIM("AMY","AAN",7), ANIM("AMY","AAN",8), ANIM("AMY","AAN",9),
        ANIM("AMY","AAN",10), ANIM("AMY","AAN",11),
    };
    LoadCharAnims(3, s_amyAnims, 11);

    /* Character 4 (Eggman): 9 animations */
    static const char *s_eggAnims[] = {
        ANIM("ROBOTNIK","RAN",1), ANIM("ROBOTNIK","RAN",2), ANIM("ROBOTNIK","RAN",3),
        ANIM("ROBOTNIK","RAN",4), ANIM("ROBOTNIK","RAN",5), ANIM("ROBOTNIK","RAN",6),
        ANIM("ROBOTNIK","RAN",7), ANIM("ROBOTNIK","RAN",8), ANIM("ROBOTNIK","RAN",9),
    };
    LoadCharAnims(4, s_eggAnims, 9);

    /* Character 5 (Metal Sonic): 13 animations */
    static const char *s_msonicAnims[] = {
        ANIM("MSONIC","MSAN",1), ANIM("MSONIC","MSAN",2), ANIM("MSONIC","MSAN",3),
        ANIM("MSONIC","MSAN",4), ANIM("MSONIC","MSAN",5), ANIM("MSONIC","MSAN",6),
        ANIM("MSONIC","MSAN",7), ANIM("MSONIC","MSAN",8), ANIM("MSONIC","MSAN",9),
        ANIM("MSONIC","MSAN",10), ANIM("MSONIC","MSAN",11), ANIM("MSONIC","MSAN",12),
        ANIM("MSONIC","MSAN",13),
    };
    LoadCharAnims(5, s_msonicAnims, 13);

    /* Character 6 (Tails Doll): 10 animations */
    static const char *s_dtailsAnims[] = {
        ANIM("DTAILS","DTAN",1), ANIM("DTAILS","DTAN",2), ANIM("DTAILS","DTAN",3),
        ANIM("DTAILS","DTAN",4), ANIM("DTAILS","DTAN",5), ANIM("DTAILS","DTAN",6),
        ANIM("DTAILS","DTAN",7), ANIM("DTAILS","DTAN",8), ANIM("DTAILS","DTAN",9),
        ANIM("DTAILS","DTAN",10),
    };
    LoadCharAnims(6, s_dtailsAnims, 10);

    /* Character 7 (Metal Knuckles): 13 animations */
    static const char *s_mknuxAnims[] = {
        ANIM("MKNUCK","MKAN",1), ANIM("MKNUCK","MKAN",2), ANIM("MKNUCK","MKAN",3),
        ANIM("MKNUCK","MKAN",4), ANIM("MKNUCK","MKAN",5), ANIM("MKNUCK","MKAN",6),
        ANIM("MKNUCK","MKAN",7), ANIM("MKNUCK","MKAN",8), ANIM("MKNUCK","MKAN",9),
        ANIM("MKNUCK","MKAN",10), ANIM("MKNUCK","MKAN",11), ANIM("MKNUCK","MKAN",12),
        ANIM("MKNUCK","MKAN",13),
    };
    LoadCharAnims(7, s_mknuxAnims, 13);

    /* Character 8 (Egg Robo): 14 animations */
    static const char *s_mrobotAnims[] = {
        ANIM("MROBOT","MRAN",1), ANIM("MROBOT","MRAN",2), ANIM("MROBOT","MRAN",3),
        ANIM("MROBOT","MRAN",4), ANIM("MROBOT","MRAN",5), ANIM("MROBOT","MRAN",6),
        ANIM("MROBOT","MRAN",7), ANIM("MROBOT","MRAN",8), ANIM("MROBOT","MRAN",9),
        ANIM("MROBOT","MRAN",10), ANIM("MROBOT","MRAN",11), ANIM("MROBOT","MRAN",12),
        ANIM("MROBOT","MRAN",13), ANIM("MROBOT","MRAN",14),
    };
    LoadCharAnims(8, s_mrobotAnims, 14);

    /* Character 9 (Super Sonic): 14 animations */
    static const char *s_ssonicAnims[] = {
        ANIM("SSONIC","SSAN",1), ANIM("SSONIC","SSAN",2), ANIM("SSONIC","SSAN",3),
        ANIM("SSONIC","SSAN",4), ANIM("SSONIC","SSAN",5), ANIM("SSONIC","SSAN",6),
        ANIM("SSONIC","SSAN",7), ANIM("SSONIC","SSAN",8), ANIM("SSONIC","SSAN",9),
        ANIM("SSONIC","SSAN",10), ANIM("SSONIC","SSAN",11), ANIM("SSONIC","SSAN",12),
        ANIM("SSONIC","SSAN",13), ANIM("SSONIC","SSAN",14),
    };
    LoadCharAnims(9, s_ssonicAnims, 14);

    DebugLog("LoadCharacterAnimations: %d entries in anim array\n",
             g_modelFrameCount);
}

/* File I/O helpers — little-endian reads */
static int ReadInt32(FILE *fp) {
    unsigned char b[4];
    fRead(b, 1, 4, fp);
    return (int)(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
}

static short ReadShort(FILE *fp) {
    unsigned char lo, hi;
    fRead(&lo, 1, 1, fp);
    fRead(&hi, 1, 1, fp);
    return (short)((hi << 8) | lo);
}

static unsigned char ReadByte(FILE *fp) {
    unsigned char b;
    fRead(&b, 1, 1, fp);
    return b;
}

/* Convert byte UV to 16.16 fixed point (odd values round up) */
static int UVByteToFixed(unsigned char b) {
    int val = (unsigned int)b << 16;
    if (b & 1) {
        val += 0xFFFF;
    }
    return val;
}

/**
 * LoadModelData — 0x0042E034 — 1880 bytes
 * Reads a single character's 3D model from a binary file.
 * Called 10 times from LoadCharacterModels (once per character).
 *
 * File format per model (from InvisibleUp's srm2obj.py):
 *   Repeat per limb (until EOF):
 *     int32 vertexCount
 *     Per vertex (vertexCount × 0x10 bytes):
 *       3 × short: posX, posY, posZ
 *       3 × short: normalX, normalY, normalZ
 *       3 × byte:  R, G, B
 *       1 × byte:  pad
 *     int32 triCount
 *     Per triangle (triCount × 0x10 bytes):
 *       3 × ushort: vertex indices (relative to limb)
 *       6 × byte:   UV (u0,v0, u1,v1, u2,v2)
 *       1 × byte:   texture page
 *       1 × byte:   pad
 *       1 × short:  render flags
 *     int32 quadCount
 *     Per quad (quadCount × 0x14 bytes):
 *       4 × short:  vertex indices (relative to limb)
 *       8 × byte:   UV (u0,v0, u1,v1, u2,v2, u3,v3)
 *       1 × byte:   texture page
 *       1 × byte:   pad
 *       1 × short:  render flags
 *
 * Original uses Watcom fastcall: filename in EAX.
 */
void LoadModelData(const char *filename)
{
    FILE *fp = fOpen(filename, "rb");

    if (fp == NULL) {
        DebugLog("LoadModelData: FAILED to open %s\n", filename);
        g_modelCount++;
        return;
    }
    int *limbPtr = g_limbMetaTable + g_modelLimbCount * 6;
    int *polyPtr = (int *)((char *)g_charFaceBase + g_modelPolygonCount * 0x30);
    SrcVertex *vtxPtr = &g_vertexArrayBase[g_modelVertexCount];
    unsigned char *ptypePtr = g_polyTypeTable + g_modelPolygonCount;

    /* Initialize model metadata entry */
    ModelMeta *meta = &g_modelMeta[g_modelCount];
    meta->vertexStart = g_modelVertexCount;
    meta->vertexCount = 0;
    meta->polyStart   = g_modelPolygonCount;
    meta->polyCount   = 0;
    meta->limbStart   = g_modelLimbCount;
    meta->limbCount   = 0;

    /* Read limbs until EOF */
    for (;;) {
        /* Count fields are int32 (4 bytes). Read vertex count. */
        int vertCount = ReadInt32(fp);
        if (feof(fp) || vertCount <= 0) {
            break;
        }
        /* Record limb metadata */
        limbPtr[0] = g_modelVertexCount;      /* vertex start for this limb */
        limbPtr[1] = vertCount;                /* vertex count */
        limbPtr[2] = g_modelPolygonCount;      /* polygon start for this limb */

        meta->vertexCount += vertCount;
        meta->limbCount += 1;

        /* Read vertices (0x10 bytes each) */
        for (int v = 0; v < vertCount; v++) {
            /* 3 shorts: position */
            vtxPtr->posX = (int)ReadShort(fp);
            vtxPtr->posY = (int)ReadShort(fp);
            vtxPtr->posZ = (int)ReadShort(fp);
            /* 3 shorts: normal */
            vtxPtr->normalX = (int)ReadShort(fp);
            vtxPtr->normalY = (int)ReadShort(fp);
            vtxPtr->normalZ = (int)ReadShort(fp);
            /* 3 bytes: R, G, B → 13-bit fixed point */
            vtxPtr->colorR = (unsigned int)ReadByte(fp) * 0x2000 + 0x1000;
            vtxPtr->colorG = (unsigned int)ReadByte(fp) * 0x2000 + 0x1000;
            vtxPtr->colorB = (unsigned int)ReadByte(fp) * 0x2000 + 0x1000;
            /* 1 byte: pad */
            ReadByte(fp);

            vtxPtr++;
        }

        /* Read triangle count (int32) */
        int triCount = ReadInt32(fp);
        if (triCount < 0) {
            triCount = 0;
        }
        limbPtr[3] = triCount;   /* triangle count */
        limbPtr[4] = triCount;   /* total poly count (updated after quads) */
        meta->polyCount += triCount;

        /* Read triangles (0x10 bytes each) */
        int limbVertBase = limbPtr[0];  /* global vertex start for this limb */
        for (int t = 0; t < triCount; t++) {
            /* 3 ushort indices (relative to limb, add base) */
            unsigned short idxA = (unsigned short)ReadShort(fp);
            unsigned short idxB = (unsigned short)ReadShort(fp);
            unsigned short idxC = (unsigned short)ReadShort(fp);

            *(short *)((char *)polyPtr + 0x20) = (short)(limbVertBase + idxC);
            *(short *)((char *)polyPtr + 0x22) = (short)(limbVertBase + idxB);
            *(short *)((char *)polyPtr + 0x24) = (short)(limbVertBase + idxA);
            *(short *)((char *)polyPtr + 0x26) = (short)(limbVertBase + idxA);

            /* 6 UV bytes (3 pairs) — stored into polygon UV slots */
            unsigned char u0 = ReadByte(fp);
            unsigned char v0 = ReadByte(fp);
            unsigned char u1 = ReadByte(fp);
            unsigned char v1 = ReadByte(fp);
            unsigned char u2 = ReadByte(fp);
            unsigned char v2 = ReadByte(fp);

            polyPtr[0] = UVByteToFixed(u2); 
            polyPtr[1] = UVByteToFixed(v2);
            polyPtr[2] = UVByteToFixed(u1); 
            polyPtr[3] = UVByteToFixed(v1);
            polyPtr[4] = UVByteToFixed(u0);
            polyPtr[5] = UVByteToFixed(v0);
            polyPtr[6] = polyPtr[4];
            polyPtr[7] = polyPtr[5];

            /* Texture page + pad + flags */
            unsigned char tpage = ReadByte(fp);
            ReadByte(fp);  /* pad */
            short flags = ReadShort(fp);

            *(unsigned char *)((char *)polyPtr + 0x28) = tpage;
            *ptypePtr++ = tpage;
            *(unsigned char *)((char *)polyPtr + 0x2E) = (unsigned char)(flags * 2);

            polyPtr += 12;  /* stride 0x30 / 4 = 12 ints */
        }

        /* Read quad count (int32) */
        int quadCount = ReadInt32(fp);
        if (quadCount < 0) {
            quadCount = 0;
        }
        limbPtr[3] += quadCount;   /* update total poly count */
        meta->polyCount += quadCount;

        /* Read quads (0x14 bytes each) */
        for (int q = 0; q < quadCount; q++) {
            /* 4 short indices (relative to limb) */
            short qA = ReadShort(fp);
            short qB = ReadShort(fp);
            short qC = ReadShort(fp);
            short qD = ReadShort(fp);

            *(short *)((char *)polyPtr + 0x20) = (short)(limbVertBase + qD);
            *(short *)((char *)polyPtr + 0x22) = (short)(limbVertBase + qC);
            *(short *)((char *)polyPtr + 0x24) = (short)(limbVertBase + qB);
            *(short *)((char *)polyPtr + 0x26) = (short)(limbVertBase + qA);

            /* 8 UV bytes (4 pairs) */
            unsigned char u0 = ReadByte(fp), v0 = ReadByte(fp);
            unsigned char u1 = ReadByte(fp), v1 = ReadByte(fp);
            unsigned char u2 = ReadByte(fp), v2 = ReadByte(fp);
            unsigned char u3 = ReadByte(fp), v3 = ReadByte(fp);

            polyPtr[0] = UVByteToFixed(u3);  polyPtr[1] = UVByteToFixed(v3);
            polyPtr[2] = UVByteToFixed(u2);  polyPtr[3] = UVByteToFixed(v2);
            polyPtr[4] = UVByteToFixed(u1);  polyPtr[5] = UVByteToFixed(v1);
            polyPtr[6] = UVByteToFixed(u0);  polyPtr[7] = UVByteToFixed(v0);

            /* Texture page + pad + flags */
            unsigned char tpage = ReadByte(fp);
            ReadByte(fp);  /* pad */
            short flags = ReadShort(fp);

            *(unsigned char *)((char *)polyPtr + 0x28) = tpage;
            *ptypePtr++ = tpage;
            *(unsigned char *)((char *)polyPtr + 0x2E) = (unsigned char)((flags * 2) | 1);

            polyPtr += 12;
        }

        g_modelVertexCount += vertCount;
        g_modelPolygonCount += triCount + quadCount;
        limbPtr += 6;  /* stride 0x18 / 4 = 6 ints */
        g_modelLimbCount++;
    }

    g_modelCount++;
    fClose(fp);
}

/**
 * Animation frame data array — flat array of 6-int entries.
 * Each entry: posX, posY, posZ, rotX, rotY, rotZ per limb per frame.
 * Indexed as: (frameStartForChar + frame * limbCount + limbIdx) * 6
 * Stride 0x18 (24 bytes = 6 ints) per entry.
 * Arrays declared at file top (before LoadCharAnims uses them).
 */
int *GetAnimFrameDataBase(void) {
    return g_animFrameData;
}

/**
 * LoadAnimationData — 0x0042E78C — 168 bytes — VALIDATED
 * Reads animation frame data from a binary file.
 * File format: int32 header (limbCount, read but unused as loop bound),
 *              then flat sequence of (6 × int32) entries until EOF.
 *
 * Binary has a flat loop: reads 6-int entries directly into g_animFrameData,
 * incrementing g_modelFrameCount per entry (not per "frame").
 *
 * Original Watcom fastcall: filename in EAX, charId in EDX.
 */
void LoadAnimationData(const char *filename, int charId)
{
    FILE *fp = fOpen(filename, "rb");
    if (fp == NULL) {
        return;
    }

    ModelMeta *meta = &g_modelMeta[charId];

    /* Compute write pointer: g_animFrameData + g_modelFrameCount * 24 bytes
     * VALIDATED: binary does [0x713074]*4 - [0x713074] = *3, then <<3 = *24 */
    int *writePtr = g_animFrameData + g_modelFrameCount * 6;

    /* Read 1-int header (limbCount) — binary reads it but never uses value */
    int header = ReadInt32(fp);                                  /* EAX=[ebp-0x18] */
    (void)header;

    /* Flat loop: read 6 ints at a time directly into frame data until EOF */
    while (fRead(writePtr, 4, 6, fp) == 6) {                    /* EAX=esi */
#if SONICR_BIG_ENDIAN
        for (int j = 0; j < 6; j++) {
            bswap32_inplace(&writePtr[j]);
        }
#endif
        writePtr += 6;                                           /* add esi, 0x18 */
        g_modelFrameCount++;                                     /* inc [0x713074] */
        /* binary: inc [eax+0x40], i.e. meta +0x1C */
        meta->animFrameCount += 1;
    }

    fClose(fp);
}

/* =====================================================================
 * Vertex lighting and tinting
 * ===================================================================== */

/* Helper: clamp int to [0, 0x1FFFFF] (13-bit fixed point color range) */
static int ClampColor13(int val) {
    if (val < 0) {
        return 0;
    }
    if (val > 0x1FFFFF) {
        return 0x1FFFFF;
    }
    return val;
}

/**
 * TintTrackGourauds — 0x00430A0C — 463 bytes
 * Adds an RGB tint to all track vertex colors.
 * Colors are 13-bit fixed point (value * 0x2000 + 0x1000).
 * tintR/G/B: 0 = dark, 0x80 = neutral, 0xFF = bright.
 * D3D mode adds an extra +0x60 per channel.
 */
void TintTrackGourauds(int tintR, int tintG, int tintB)
{
    DebugLog("TintTrackGourauds\n");

    int rScaled, gScaled, bScaled;
    if (tintR == 0 && tintG == 0 && tintB == 0) {
        rScaled = 0;
        gScaled = 0;
        bScaled = 0;
    } else {
        rScaled = (tintR - 0x80) / 8;
        gScaled = (tintG - 0x80) / 8;
        bScaled = (tintB - 0x80) / 8;
    }

//#ifdef SONICR_DC
  //  rScaled += 0x60/2;
    //gScaled += 0x60/2;
    //bScaled += 0x60/2;
//#else
    rScaled += 0x1D;
    gScaled += 0x1D;
    bScaled += 0x1D;
//#endif

    int tintFixedR = rScaled * 0x2000 + 0x1000;
    int tintFixedG = gScaled * 0x2000 + 0x1000;
    int tintFixedB = bScaled * 0x2000 + 0x1000;

    SrcVertex *vtx = g_vertexArrayBase;
    int count = g_vertexIndexRunning;
    for (int i = 0; i < count; i++) {
        vtx->colorR = ClampColor13(vtx->colorR + tintFixedR);
        vtx->colorG = ClampColor13(vtx->colorG + tintFixedG);
        vtx->colorB = ClampColor13(vtx->colorB + tintFixedB);
        vtx++;
    }
}

/**
 * TintCharacterGouraudTables — 0x00430BDC — 455 bytes
 * Same pattern but operates on the character lighting table.
 * The table has g_modelVertexCount * 32 entries × 3 ints (R, G, B).
 */

void TintCharacterGouraudTables(int tintR, int tintG, int tintB)
{
    DebugLog("TintCharacterGouraudTables\n");

    int rScaled, gScaled, bScaled;
    if (tintR == 0 && tintG == 0 && tintB == 0) {
        rScaled = 0;
        gScaled = 0;
        bScaled = 0;
    } else {
        rScaled = (tintR / 8) - 0x10;
        gScaled = (tintG / 8) - 0x10;
        bScaled = (tintB / 8) - 0x10;
    }

//#ifdef SONICR_DC
  //  rScaled += 0x54;//0x60/2;
   // gScaled += 0x54;//0x60/2;
    //bScaled += 0x54;//0x60/2;
//#else
    rScaled += 0x1D;
    gScaled += 0x1D;
    bScaled += 0x1D;
//#endif

    int tintFixedR = rScaled * 0x2000 + 0x1000;
    int tintFixedG = gScaled * 0x2000 + 0x1000;
    int tintFixedB = bScaled * 0x2000 + 0x1000;

    int *p = g_charLightingTable;
    int count = g_modelVertexCount << 5;
    for (int i = 0; i < count; i++) {
        p[0] = ClampColor13(p[0] + tintFixedR);
        p[1] = ClampColor13(p[1] + tintFixedG);
        p[2] = ClampColor13(p[2] + tintFixedB);
        p += 3;
    }
}

/**
 * UpdateVertexLighting — 0x00430638 — 132 bytes
 * Copies pre-baked lighting from g_charLightingTable into vertex colors.
 * Selects brightness level from player+0x10 >> 7 (0-31).
 */
void UpdateVertexLighting(Player *player)
{
    int charId = player->charId;                    /* 0xF2 (signed short) */

    int vertexCount = g_modelMeta[charId].vertexCount;
    int vertexStart = g_modelMeta[charId].vertexStart;

    /* Direction row from facing angle: yaw (0-0xFFF) >> 7 gives 0-31, inverted
     * so the row tracks which way the character is turned. Unclamped, matching
     * 0x430645: the binary is sar/sub straight into the index with no bounds
     * check, so a yaw outside 0..0xFFF reads past this model's rows. */
    int lightLevel = 0x1F - (player->angleYaw >> 7);

    int *src = g_charLightingTable + (vertexCount * lightLevel * 3) + (vertexStart * 0x60);
    SrcVertex *dst = &g_vertexArrayBase[vertexStart];

    for (int i = 0; i < vertexCount; i++) {
        dst->colorR = src[0];
        dst->colorG = src[1];
        dst->colorB = src[2];
        src += 3;
        dst++;
    }
}

/**
 * BuildFootShadowQuad — 0x00482BFC — 1539 bytes — VALIDATED
 *
 * Drops one ground-conformed footstep shadow quad behind a character.
 * EAX = playerIndex, EDX = mode (1 negates bboxD — left vs right foot).
 *
 * Reads character orientation + the per-character extents at ROM 0x501600,
 * builds a rotation matrix from sin/cos tables and two atan2-derived angles,
 * then writes 4 rotated corner world positions into g_footShadowVerts
 * and a control entry (center + visibility=100) into g_footShadowCtrl.
 *
 * Each player has 16 rotating slots (player+0x1E8 counter).
 */
void BuildFootShadowQuad(int playerIndex, int mode)
{
    Player *pl = &g_playerBase[playerIndex];

    /* Vertex output slot: 0x909320 + (playerIndex*16 + slot) * 256 */
    int slot = pl->_unk_0x1E8;
    int entryIdx = playerIndex * 16 + slot;
    int *vout = &g_footShadowVerts[entryIdx * 64]; /* 256 bytes per entry */

    /* Read model dimensions */
    int dimX, dimY, dimZ;
    /* Animated path: model frame data */ 
    if (pl->loopMode != 0) {                                /* 0xA0 */    
        int frameVal = (int)pl->_unk_0x94;
        int animBase = pl->_unk_0xBC;
        /* Loop surface entry → vtxBase (+0). Binary indexes a short* by
         * frameVal*11 = the first short of entry frameVal = vtxBase. */
        int modelIdx = (int)((TerLoopEntry *)g_terLoopTable)[frameVal].vtxBase + animBase;
        /* Model struct stride: modelIdx*16 + modelIdx = modelIdx*17, *2 = modelIdx*34 */
        char *modelData = (char *)g_terUnknown74
                          + ((modelIdx << 4) + modelIdx) * 2;

        dimX = *(short *)(modelData + 0x18);
        dimY = -(*(short *)(modelData + 0x1A));
        dimZ = *(short *)(modelData + 0x1C);
    }
    /* Fallback: player struct fields */
    else {
        dimX = (int)pl->surfNormX;                           /* 0xB4 */
        dimY = -(int)pl->surfNormY;                          /* 0xB6 */
        dimZ = (int)pl->surfNormZ;                           /* 0xB8 */
    }

    /* First atan2: pitch angle from (-dimY, dimZ) */
    /* atan2(dimZ, -dimY) * 4096 / (2π) → 12-bit angle index */
    sr_double angle1Rad = sr_atan2((sr_double)dimZ, (sr_double)dimY);
    int angle1Raw = (int)(angle1Rad * 4096.0 * 0.159154943273756);
    int angle1Idx = ((angle1Raw << 4) >> 4) & 0xFFF;
    int complement1 = 0xFFF - angle1Idx;

    /* Cross product for second angle input:
     * crossVal = (cosTable[complement1]>>2 * (-dimY) - sinTable[complement1]>>2 * dimZ) / 4096 */
    int sinComp1 = g_sinTable[complement1] >> 2;
    int cosComp1 = g_cosTable[complement1] >> 2;
    int crossVal = sdiv4096(cosComp1 * dimY - sinComp1 * dimZ);

    /* Second atan2: yaw from (dimX, crossVal) */
    sr_double angle2Rad = sr_atan2((sr_double)dimX, (sr_double)crossVal);
    int angle2Raw = (int)(angle2Rad * 4096.0 * 0.159154943273756);
    int angle2Idx = ((angle2Raw << 4) >> 4) & 0xFFF;
    int complement2 = 0xFFF - angle2Idx;

    /* Orientation from player struct */
    int orientIdx = (pl->angleYaw + pl->pitchCombo) & 0xFFF;

    /* Trig lookups (all >> 2 for 10-bit fixed point) */
    int sinOrient = g_sinTable[orientIdx] >> 2;
    int cosOrient = g_cosTable[orientIdx] >> 2;
    int sinAngle1 = g_sinTable[angle1Idx] >> 2;
    int cosAngle1 = g_cosTable[angle1Idx] >> 2;
    int sinComp2  = g_sinTable[complement2] >> 2;
    int cosComp2  = g_cosTable[complement2] >> 2;

    /* Rotation sub-components (lines 131-170 of disasm) */
    /* Binary computes: imul sinComp2, 0 and imul cosComp2, 0 in certain terms,
     * so those products are explicitly zero. Kept for fidelity. */
    int R_A = (cosOrient * cosComp2) >> 12;
    int R_B = (cosOrient * sinComp2) >> 12;
    int R_C = (sinOrient * cosComp2) >> 12;
    int R_D = (sinOrient * sinComp2) >> 12;
    int negSinOrient = -sinOrient;

    /* Full rotation matrix elements (lines 184-217 of disasm) */
    int M00 = (R_B * cosAngle1 - negSinOrient * sinAngle1) >> 12;
    int M01 = (negSinOrient * cosAngle1 + R_B * sinAngle1) >> 12;
    int M10 = (R_D * cosAngle1 - cosOrient * sinAngle1) >> 12;
    int M11 = (sinAngle1 * R_D + cosAngle1 * cosOrient) >> 12;

    /* Bounding box dimensions from ROM table */
    short charId = pl->charId;
    int bboxW = g_charBBoxDims[charId * 3 + 0];  /* 0x501600 + charId*12 */
    int bboxH = g_charBBoxDims[charId * 3 + 1];  /* 0x501604 + charId*12 */
    int bboxD = g_charBBoxDims[charId * 3 + 2];  /* 0x501608 + charId*12 */
    if (mode == 1) {
        bboxD = -bboxD;
    }

    int dMinus = bboxD - bboxW;
    int dPlus  = bboxD + bboxW;
    int negH   = -bboxH;

    /* Player world position (>>12 from raw fixed-point) */
    int pX = pl->posX >> 12;
    int pY = pl->groundHeight >> 12;              /* Y at +0x38, subtracted in formula */
    int pZ = pl->posZ >> 12;

    /* Vertex 0: (dMinus, bboxH) */
    vout[5] = pX + sdiv4096(dMinus * R_A + bboxH * R_C);
    vout[6] = sdiv4096(dMinus * M00 + bboxH * M10) - pY;
    vout[7] = pZ + sdiv4096(dMinus * M01 + bboxH * M11);

    /* Vertex 1: (dPlus, bboxH) */
    vout[21] = pX + sdiv4096(dPlus * R_A + bboxH * R_C);
    vout[22] = sdiv4096(dPlus * M00 + bboxH * M10) - pY;
    vout[23] = pZ + sdiv4096(dPlus * M01 + bboxH * M11);

    /* Vertex 2: (dPlus, negH) */
    vout[37] = pX + sdiv4096(dPlus * R_A + negH * R_C);
    vout[38] = sdiv4096(dPlus * M00 + negH * M10) - pY;
    vout[39] = pZ + sdiv4096(dPlus * M01 + negH * M11);

    /* Vertex 3: (dMinus, negH) */
    vout[53] = pX + sdiv4096(dMinus * R_A + negH * R_C);
    vout[54] = sdiv4096(dMinus * M00 + negH * M10) - pY;
    vout[55] = pZ + sdiv4096(dMinus * M01 + negH * M11);

    /* Write control entry */
    int ctrlOffset = (slot + playerIndex * 16) * 4;  /* 4 ints per entry */
    g_footShadowCtrl[ctrlOffset + 0] = pl->posX >> 12;
    g_footShadowCtrl[ctrlOffset + 1] = -(pl->posY) >> 12;
    g_footShadowCtrl[ctrlOffset + 2] = pl->posZ >> 12;
    g_footShadowCtrl[ctrlOffset + 3] = 100;  /* visibility = 0x64 */

    /* Advance slot counter (wraps at 16) */
    slot++;
    if (slot == 16) {
        slot = 0;
    }
    pl->_unk_0x1E8 = slot;
}
