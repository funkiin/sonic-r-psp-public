/*
 * ter_types.h -- Typed structs for .TER terrain file data.
 *
 * These overlay the binary data loaded from .TER files.  Field names
 * replace the raw (char *)ptr + offset patterns used throughout the
 * collision and rendering code.
 *
 * All fields are little-endian in the file.  On BE platforms, the
 * loader byte-swaps each field after reading so struct access works
 * natively.
 */
#ifndef TER_TYPES_H
#define TER_TYPES_H

#include <stdint.h>

/* -- Vertex table: 6 bytes per entry -- */
typedef struct {
    int16_t x;      /* +0 */
    int16_t y;      /* +2  (height) */
    int16_t z;      /* +4 */
} TerVertex;

/* -- Face table: 14 bytes per entry -- */
typedef struct {
    uint16_t vtxIdx[4]; /* +0  four vertex byte-offsets into vertex table */
    int16_t  normalX;   /* +8 */
    int16_t  normalY;   /* +10 */
    int16_t  normalZ;   /* +12 */
} TerFace;

/* -- Surface table: 16 bytes per entry -- */
typedef struct {
    int16_t  faceBase;   /* +0   first face index */
    int16_t  faceCount;  /* +2   number of faces */
    int16_t  centerX;    /* +4   bounding center X */
    int16_t  centerZ;    /* +6   bounding center Z */
    int32_t  radiusSq;   /* +8   bounding radius squared */
    int16_t  misc;       /* +12  low 16 of dword (unused by ground collision) */
    int16_t  layer;      /* +14  collision layer */
} TerSurface;

/* -- Item state table: 12 bytes per entry -- */
typedef struct {
    int16_t meshIdx0;     /* +0   collision mesh index (low 16) */
    int16_t meshIdx1;     /* +2   collision mesh index (high 16), -1 if none */
    int16_t storedMask0;  /* +4   mask swapped with meshIdx0's collision mask */
    int16_t storedMask1;  /* +6   mask swapped with meshIdx1's collision mask */
    int16_t activeFlag;   /* +8   1 = active, 0 = collected/triggered */
    int16_t ringCost;     /* +10  ring cost to collect */
} TerItemState;

/* -- Collision mesh (wall) entry: 16 bytes per entry -- */
typedef struct {
    int16_t  vtxBase;    /* +0   first vertex index */
    uint8_t  vtxCount;   /* +2   number of vertices */
    uint8_t  bounceMag;  /* +3   bounce magnitude */
    uint8_t  softFlag;   /* +4   soft/hard wall flag */
    uint8_t  heightGate; /* +5   height gate flag */
    int16_t  mask;       /* +6   player collision bitmask */
    int16_t  centerX;    /* +8   bounding center X */
    int16_t  centerZ;    /* +10  bounding center Z */
    int32_t  radiusSq;   /* +12  bounding radius squared */
} TerCollisionMesh;

/* -- Loop / ride-around surface entry: 22 bytes per entry. These are the track's
 *    loop surfaces (Factory loops, Regal Ruin's double-loop); they route through
 *    the loopMode physics path. Field names below are inferred from their reader
 *    functions (cited); layout is byte-exact to the binary's 11-short record. -- */
typedef struct {
    int16_t vtxBase;      /* +0    base vertex index into g_terUnknown74 (render_character_d3d, model_load, LoopSurfaceCalc) */
    int16_t field2;       /* +2 */
    int16_t rangeStart1;  /* +4    Z-range bound (LoopSurfaceCheck) */
    int16_t rangeStart2;  /* +6 */
    int16_t normX;        /* +8    surface 3-vector X (negated by NegateLookAtGravityVector) */
    int16_t normY;        /* +0xa  surface 3-vector Y */
    int16_t normZ;        /* +0xc  surface 3-vector Z */
    int16_t heightRef;    /* +0xe  loop pull-drag height reference (UpdateLoopMovement) */
    int16_t speedRef;     /* +0x10 speed threshold for AI heading tweak (AISteeringAndDrag) */
    int16_t rangeSize;    /* +0x12 */
    int16_t surfAngle;    /* +0x14 surface heading; subtracted from car yaw to steer relative to surface (ApplyDragAndSteering) */
} TerLoopEntry;

_Static_assert(sizeof(TerVertex)        == 6,  "TerVertex must be 6 bytes");
_Static_assert(sizeof(TerFace)          == 14, "TerFace must be 14 bytes");
_Static_assert(sizeof(TerSurface)       == 16, "TerSurface must be 16 bytes");
_Static_assert(sizeof(TerItemState)     == 12, "TerItemState must be 12 bytes");
_Static_assert(sizeof(TerCollisionMesh) == 16, "TerCollisionMesh must be 16 bytes");
_Static_assert(sizeof(TerLoopEntry)     == 22, "TerLoopEntry must be 22 bytes");

#endif /* TER_TYPES_H */
