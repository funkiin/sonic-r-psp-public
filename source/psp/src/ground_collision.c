/**
 * ground_collision.c — Track ground collision detection
 *
 * FUN_004d75f0 @ 0x004d75f0 — 844 bytes — main ground collision
 * FUN_004d67d8 @ 0x004d67d8 — 807 bytes — point-in-polygon (binary search)
 * FUN_004d6c1c @ 0x004d6c1c — 761 bytes — height interpolation
 * FUN_004d751c @ 0x004d751c — 210 bytes — loop surface check
 *
 * Called from UpdatePlayerMovement per player per frame.
 * Uses the .TER terrain data: grid-based spatial index to find which
 * track polygon the player stands on, then interpolates ground height.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include <math.h>

/* Terrain sub-table pointers (set by LoadTerrain / ParseTerrainHeader) */

/* Forward declarations */
static void LoopSurfaceCalc(Player *player);
int LoopSurfaceCheck(Player *player, int surfaceIdx, int heightThreshold);

/* Grid transform floats — same globals as g_worldBoundsA-E in track_per_level.c.
 * Set per-track during init. Were incorrectly declared static (separate zero copies). */
#define g_gridScaleX   g_worldBoundsB
#define g_gridScaleZ   g_worldBoundsE
#define g_gridOffsetX  g_worldBoundsD
#define g_gridOffsetZ  g_worldBoundsC
#define g_gridBaseScale g_worldBoundsA

/* ROM float constants */
#define GRID_RECIP  0.0078125f     /* 0x52F94C: 1/128 */
#define GRID_TILE   32.0f          /* 0x52F950: tile size */

/* Tile map — 256 tile indices covering the track grid.
 * Non-static: also used by sky_render_d3d.c for sky grid visibility lookup. */
unsigned char __attribute__((aligned(32))) g_tileMap[256 * 256];  /* 0x0068B2C0 */

/* Scratch globals written by PointInPolygon, read by GroundCollision + SampleTerrainGrid */
int s_hitSurfaceIdx;     /* 0x6da584 */
int s_hitEdgeIdx;        /* 0x6da588 */
int s_hitSurfaceLayer;   /* 0x6da58c */

int g_qtSurfIdx;         /* 0x6da590 — last matched surface (QueryTerrainHeight) */
int g_qtEdgeIdx;         /* 0x6da594 — last matched edge (QueryTerrainHeight) */


/**
 * PointInPolygon — FUN_004d67d8 — 807 bytes
 *
 * Tests if player XZ (in_EAX=playerX, param_2=playerZ) is inside the
 * polygon of surface [unaff_EBX] in g_trackSurfaceData.
 * Uses binary search on sorted edges.
 * Returns 1 if inside, 0 if outside.
 * Sets s_hitSurfaceIdx, s_hitEdgeIdx, s_hitSurfaceLayer on hit.
 */
int PointInPolygon(int playerX, int playerZ, int surfIdx)
{
/* Helper: read vertex XZ by face vertex index (byte offset into vertex table) */
#define FACE_VTX(faces, fi, slot) ((TerVertex *)((char *)g_terVertexTable + (faces)[(fi)].vtxIdx[(slot)]))

    TerFace *faces = (TerFace *)g_terFaceTable;
    TerSurface *surf  = (TerSurface *)g_trackSurfaceData + surfIdx;

    int faceBase  = surf->faceBase;
    int faceCount = surf->faceCount;

    /* Check first edge: cross product against edge (v0 → v1) */
    TerVertex *v0 = FACE_VTX(faces, faceBase, 0);
    TerVertex *v1 = FACE_VTX(faces, faceBase, 1);

    long long cross = (long long)(playerZ + v0->z * -0x1000) * (long long)-(v1->x * 0x1000 + v0->x * -0x1000) +
                      (long long)(playerX + v0->x * -0x1000) * (long long)(v1->z * 0x1000 + v0->z * -0x1000);
    if ((cross & 0x8000000000000000ULL) != 0){
        return 0;
    }

    /* Check last edge */
    int lastFace = faceBase + faceCount - 1;
    TerVertex *vL2 = FACE_VTX(faces, lastFace, 2);
    TerVertex *vL3 = FACE_VTX(faces, lastFace, 3);

    cross = (long long)(playerZ + vL2->z * -0x1000) * (long long)-(vL3->x * 0x1000 + vL2->x * -0x1000) +
            (long long)(playerX + vL2->x * -0x1000) * (long long)(vL3->z * 0x1000 + vL2->z * -0x1000);
    if ((cross & 0x8000000000000000ULL) != 0) {
        return 0;
    }

    /* Binary search for the edge the player crosses */
    int lo = 0;
    int hi = faceCount;
    if (hi > 1) {
        do {
            int mid = lo + (hi - lo) / 2;
            TerVertex *vm0 = FACE_VTX(faces, faceBase + mid, 0);
            TerVertex *vm1 = FACE_VTX(faces, faceBase + mid, 1);

            cross = (long long)(playerZ + vm0->z * -0x1000) * (long long)-(vm1->x * 0x1000 + vm0->x * -0x1000) +
                    (long long)(playerX + vm0->x * -0x1000) * (long long)(vm1->z * 0x1000 + vm0->z * -0x1000);
            if ((cross & 0x8000000000000000ULL) != 0) {
                hi = mid;
            }
            else {
                lo = mid;
            }
        } while (hi - lo > 1);
    }

    /* Final check: verify player is inside the quad formed by edges lo..lo+1 */
    int fi = faceBase + lo;
    TerVertex *fv1 = FACE_VTX(faces, fi, 1);
    TerVertex *fv2 = FACE_VTX(faces, fi, 2);

    cross = (long long)(playerZ + fv1->z * -0x1000) * (long long)-(fv2->x * 0x1000 + fv1->x * -0x1000) +
            (long long)(playerX + fv1->x * -0x1000) * (long long)(fv2->z * 0x1000 + fv1->z * -0x1000);
    if ((cross & 0x8000000000000000ULL) != 0) {
        return 0;
    }

    TerVertex *fv3 = FACE_VTX(faces, fi, 3);
    TerVertex *fv0 = FACE_VTX(faces, fi, 0);

    cross = (long long)(playerZ + fv3->z * -0x1000) * (long long)-(fv0->x * 0x1000 + fv3->x * -0x1000) +
            (long long)(playerX + fv3->x * -0x1000) * (long long)(fv0->z * 0x1000 + fv3->z * -0x1000);
    if ((cross & 0x8000000000000000ULL) != 0) {
        return 0;
    }

    /* Hit! Store results */
    s_hitEdgeIdx = lo;
    s_hitSurfaceLayer = surf->layer;
    s_hitSurfaceIdx = surfIdx;
    return 1;
}

/**
 * InterpolateGroundHeight — FUN_004d6c1c — 761 bytes
 *
 * Given the player is inside polygon [surfIdx] at edge [edgeIdx],
 * interpolate the ground Y height at the player's exact XZ position.
 *
 * Vertex layout in .TER (6 bytes per vertex):
 *   byte 0-1: short X
 *   byte 2-3: short Y (height)
 *   byte 4-5: short Z
 *
 * Vertex format: 6 bytes per vertex. X = lower 16 bits of int at byte 0,
 * Y = upper 16 bits of int at byte 0, Z = upper 16 bits of int at byte 2.
 *
 * Algorithm: picks correct triangle half via cross product, sorts 3 vertices
 * by Z, interpolates along edges at player Z, then interpolates Y at player X.
 *
 * Original: in_EAX = playerX, unaff_EBX = surfIdx, param_1 = edgeIdx, param_2 = playerZ.
 */
int InterpolateGroundHeight(int playerX, int playerZ, int surfIdx, int edgeIdx)
{
    TerFace *faces = (TerFace *)g_terFaceTable;
    TerSurface *surf  = (TerSurface *)g_trackSurfaceData + surfIdx;
    int faceBase = surf->faceBase;

    TerFace *edge = &faces[faceBase + edgeIdx];

    TerVertex *tv0 = (TerVertex *)((char *)g_terVertexTable + edge->vtxIdx[0]);
    TerVertex *tv2 = (TerVertex *)((char *)g_terVertexTable + edge->vtxIdx[2]);

    int v0x = tv0->x;
    int v0z = tv0->z;
    int v2x = tv2->x;
    int v2z = tv2->z;

    long long cross = (long long)(playerZ + v0z * -0x1000) * (long long)-(v2x * 0x1000 + v0x * -0x1000) +
                       (long long)(playerX + v0x * -0x1000) * (long long)(v2z * 0x1000 + v0z * -0x1000);

    TerVertex *tvB;
    if ((cross & 0x8000000000000000ULL) == 0) {
        tvB = (TerVertex *)((char *)g_terVertexTable + edge->vtxIdx[3]);
    }
    else {
        tv2 = (TerVertex *)((char *)g_terVertexTable + edge->vtxIdx[1]);
        tvB = (TerVertex *)((char *)g_terVertexTable + edge->vtxIdx[2]);
    }

    TerVertex *vLo = tv0, *vMid = tvB, *vHi = tv2;

    if (vMid->z < vLo->z)
    {
        TerVertex *tmp = vLo;
        vLo = vMid;
        vMid = tmp;
    }
    if (vHi->z < vMid->z) 
    {
        TerVertex *tmp = vMid;
        vMid = vHi;
        vHi = tmp;
    }
    if (vMid->z < vLo->z) 
    {
        TerVertex *tmp = vLo;
        vLo = vMid;
        vMid = tmp;
    }

    TerVertex *vA, *vC, *vD;
    if ((playerZ >> 12) < vMid->z || vHi->z == vMid->z)
    {
        vA = vLo;
        vC = vMid;
        vD = vHi;
    }
    else {
        vA = vHi;
        vC = vMid;
        vD = vLo;
    }

    int azAbs = (playerZ >> 12) - vA->z;
    if (azAbs < 0) {
        azAbs = -azAbs;
    }

    int aX = vA->x * 0x1000;
    int aY = vA->y * 0x1000;

    int interpX1 = aX, interpY1 = aY;
    if (azAbs != 0) {
        int czDist = vC->z - vA->z;
        if (czDist < 0) {
            czDist = -czDist;
        }
        if (czDist != 0) {
            interpX1 = aX + ((vC->x * 0x1000 - aX) / czDist) * azAbs;
            interpY1 = aY + ((vC->y * 0x1000 - aY) / czDist) * azAbs;
        }

        int dzDist = vD->z - vA->z;
        if (dzDist < 0) {
            dzDist = -dzDist;
        }
        if (dzDist != 0) {
            aX = aX + ((vD->x * 0x1000 - vA->x * 0x1000) / dzDist) * azAbs;
            aY = aY + ((vD->y * 0x1000 - vA->y * 0x1000) / dzDist) * azAbs;
        }
    }

    int x1 = interpX1 >> 12;
    int x2 = aX >> 12;

    if (x1 == x2) {
        return (interpY1 + aY) / 2;
    }

    int pxWorld = playerX >> 12;
    int xDist = pxWorld - ((x1 < x2) ? x1 : x2);
    if (xDist < 0) {
        xDist = -xDist;
    }
    if (xDist == 0) {
        return (x1 < x2) ? interpY1 : aY;
    }

    if (x1 < x2) {
        return interpY1 + ((aY - interpY1) / (x2 - x1)) * xDist;
    }
    else {
        return aY + ((interpY1 - aY) / (x1 - x2)) * xDist;
    }
}

/**
 * GroundCollision — FUN_004d75f0 — 844 bytes
 *
 * Main ground collision detection. Called once per player per frame.
 * Finds which track polygon the player stands on using grid-based
 * spatial indexing, interpolates ground height, sets grounded flag.
 */
void GroundCollision(Player *player)
{
    if (g_trackSurfaceData == NULL || g_terGridIndex == NULL || g_terGridData == NULL) {
        return;
    }

    /* Clear contact flag */
    player->overSurface = 0;

    int bestHeight = 0;
    int bestSurfLayer = 0;

    /* Clamp player position to grid bounds */
    int gridMinX = g_aiGridOriginX * 0x1000;
    if (player->posX < gridMinX) {
        player->velX = 0;
        player->posX = gridMinX;
    }

    int gridMinZ = g_aiGridOriginZ * 0x1000;
    if (player->posZ < gridMinZ) {
        player->velZ = 0;
        player->posZ = gridMinZ;
    }

    int gridMaxX = (g_aiGridCellWidth * 0x20 + g_aiGridOriginX - 1) * 0x1000;
    if (player->posX > gridMaxX) {
        player->velX = 0;
        player->posX = gridMaxX;
    }

    int gridMaxZ = (g_aiGridCellHeight * 0x20 + g_aiGridOriginZ - 1) * 0x1000;
    if (player->posZ > gridMaxZ) {
        player->velZ = 0;
        player->posZ = gridMaxZ;
    }

    int px = player->posX;
    int pz = player->posZ;

    /* Look up grid cell */
    int cellX = ((px >> 12) - g_aiGridOriginX) / g_aiGridCellWidth;
    int cellZ = ((pz >> 12) - g_aiGridOriginZ) / g_aiGridCellHeight;
    int cellIdx = cellZ * 0x20 + cellX;

    /* Get polygon list for this cell */
    short *gridIdx = (short *)g_terGridIndex;
    short *gridData = (short *)g_terGridData;
    short *polyList = gridData + gridIdx[cellIdx];

    if (*polyList == (short)0xFFFF) {
        goto done;
    }

    /* Iterate surface polygons in this cell */
    do {
        unsigned short entry = *polyList;
        unsigned int surfIdx = (unsigned int)(short)entry;

        /* Skip wall polygons (bit 14 set) */
        if ((surfIdx & 0x4000) != 0) {
            polyList++;
            continue;
        }

        /* Track 3 (Regal Ruin): skip pyramid ground surfaces when opened.
         * VALIDATED: binary 0x4d7701-0x4d7763 */
        if (g_trackId == TRACK_REGAL_RUIN) {
            int si = surfIdx & 0xFFF;
            TerItemState *ist = (TerItemState *)g_itemStateTable;
            if (ist[0].activeFlag == 0) {
                if (si > 0x188 && si < 0x18E) {
                    polyList++;
                    continue;
                }
            }
            if (ist[1].activeFlag == 0) {
                if (si == 0x3F || si == 0x103) {
                    polyList++;
                    continue;
                }
                if (si > 0x12E && si < 0x132) {
                    polyList++;
                    continue;
                }
            }
        }

        /* Distance check: is player within polygon's bounding radius? */
        TerSurface *surf = (TerSurface *)g_trackSurfaceData + (surfIdx & 0xFFF);
        int sx = (px >> 12) - surf->centerX;
        int sz = (pz >> 12) - surf->centerZ;
        int distSq = sx * sx + sz * sz;
        if (distSq >= surf->radiusSq) {
            polyList++;
            continue;
        }

        /* Point-in-polygon test */
        if (!PointInPolygon(px, pz, surfIdx & 0xFFF)) {
            polyList++;
            continue;
        }

        /* Interpolate ground height at player position */
        int groundY = InterpolateGroundHeight(px, pz, surfIdx & 0xFFF, s_hitEdgeIdx);

        /* Binary 0x4d77e0: loop-surface check inside the ground-collision loop, before ground logic.
         * EDX = surfIdx & 0xFFF (surface index, NOT player Z position).
         * EBX = groundY from interpolation. */
        if (LoopSurfaceCheck(player, surfIdx & 0xFFF, groundY)) {
            return;
        }

        /* Accept this ground contact */
        if (player->collisionLayer < s_hitSurfaceLayer) {
            /* Hit surface is on a higher collision layer */
            if ((player->airTimer == 0 || player->posY <= groundY ||
                 player->posY - groundY > 0x7FFFF) || player->velY >= 0) {
                /* Track 2 (Radical City): skip surfaces 0x1F5-0x1F8.
                 * VALIDATED: binary 0x4d782d-0x4d7849 */
                if (g_trackId == TRACK_RADICAL_CITY) {
                    int si = surfIdx & 0xFFF;
                    if (si >= 0x1F5 && si <= 0x1F8) {
                        polyList++; continue;
                    }
                }
                if (player->posY - groundY < 0x40000) {
                    player->overSurface = 1;
                    if (groundY <= bestHeight) {
                        bestSurfLayer = s_hitSurfaceLayer;
                        player->hitSurfaceIdx = s_hitSurfaceIdx;
                        bestHeight = groundY;
                        player->hitEdgeIdx = s_hitEdgeIdx;
                    }
                }
            } else {
                player->velY = 0;  /* zero vertical velocity */
            }
        } else {
            player->overSurface = 1;
            if (groundY <= bestHeight) {
                bestSurfLayer = s_hitSurfaceLayer;
                player->hitSurfaceIdx = s_hitSurfaceIdx;
                bestHeight = groundY;
                player->hitEdgeIdx = s_hitEdgeIdx;
            }
        }

        polyList++;
    } while ((*(polyList - 1) & 0x8000) == 0);

done:
    player->groundHeight = bestHeight;
    player->loopMode = 0;


    if (bestSurfLayer < player->collisionLayer) {
        /* No surface found on current layer — became airborne */
        if (player->groundedFlag != 0) {
            player->velY = player->savedVelocity;  /* restore saved velocity */
        }
        player->groundedFlag = 0;  /* clear grounded flag */
        if (player->airTimer == 0) {
            player->airTimer = 1;  /* start air timer */
        }
    }

    /* Ground contact: clamp Y position */
    if (bestHeight <= player->posY) {
        player->groundedFlag = 1;  /* set grounded flag */
    }
    if (player->groundedFlag != 0) {
        player->airTimer = 0;  /* grounded */
    }
    player->collisionLayer = bestSurfLayer;
}

/* =====================================================================
 * QueryTerrainHeight — FUN_004d7db4 — 374 bytes
 *
 * Returns the terrain height at world position (x, z).
 * Looks up the grid cell, iterates surface entries, does
 * point-in-polygon + height interpolation for each candidate.
 * Returns the highest ground surface below `minY`, or 0 if none.
 *
 * Original Watcom fastcall: EAX=x, EBX=z, EDX=minY threshold.
 * Sets globals _DAT_006da590/594 on match (surface/edge indices).
 * ===================================================================== */
int QueryTerrainHeight(int worldX, int worldZ, int minY)
{
    int bestHeight = 0;
    int cellX = ((worldX >> 12) - g_aiGridOriginX) / g_aiGridCellWidth;
    int cellZ = ((worldZ >> 12) - g_aiGridOriginZ) / g_aiGridCellHeight;

    if (cellX < 0 || cellZ < 0 || cellX > 0x1F || cellZ > 0x1F) {
        return 0;
    }

    short *gridIndex = (short *)g_terGridIndex;
    unsigned short *polyList = (unsigned short *)((char *)g_terGridData +
                               gridIndex[cellX + cellZ * 0x20] * 2);
    if (*polyList == 0xFFFF) {
        return 0;
    }

    do {
        unsigned short entry = *polyList++;
        if ((entry & 0x4000) != 0) {
            continue;
        }

        int surfIdx = (short)entry & 0xFFF;
        TerSurface *surf = (TerSurface *)g_trackSurfaceData + surfIdx;
        int sx = (worldX >> 12) - surf->centerX;
        int sz = (worldZ >> 12) - surf->centerZ;

        if (sx * sx + sz * sz >= surf->radiusSq) {
            continue;
        }

        /* Both callees take 12-bit fixed point — their edge tests read
         * `playerZ + v0->z * -0x1000`, which balances at that scale.
         * 0x4d7ece / 0x4d7eeb pass esi/edi, the raw args. The shifted-down
         * copies are used only for the cell index and radius test above. */
        if (!PointInPolygon(worldX, worldZ, surfIdx)) {
            continue;
        }

        int height = InterpolateGroundHeight(worldX, worldZ, surfIdx, s_hitEdgeIdx);

        if (height > minY && height < bestHeight) {
            g_qtSurfIdx = s_hitSurfaceIdx;
            g_qtEdgeIdx = s_hitEdgeIdx;
            bestHeight = height;
        }
    } while ((*(polyList - 1) & 0x8000) == 0);

    return bestHeight;
}

/* =====================================================================
 * ComputeDirectionalDistance — 0x004d6f18 — 131 bytes
 * Computes signed distance using atan2 → 12-bit angle → sin/cos projection.
 * atan2(dz, dx) * (1/2π) * 4096 → angle index into sin/cos tables.
 * EAX=x1, EDX=z1, EBX=x2, ECX=z2, stack[0]=x3, stack[1]=z3
 * Returns: signed projected distance / 4096
 * Uses stdcall (ret 8 — caller pushes 2 extra params).
 * ===================================================================== */
int ComputeDirectionalDistance(int x1, int z1, int x2, int z2,
                               int x3, int z3)
{
    int dx = x3 - x2;
    int dz = z3 - z2;

    /* Binary: fild dz, fild dx, call 0x4e08fd (wrapper does fxch before fpatan)
     * fpatan(ST1=dz after swap, ST0=dx after swap) — NO:
     * fild dz→ST0=dz; fild dx→ST0=dx,ST1=dz; fxch→ST0=dz,ST1=dx;
     * fpatan=atan2(ST1=dx, ST0=dz) = atan2(dx, dz) */
    sr_double angle = sr_atan2((sr_double)dx, (sr_double)dz);
    int iAngle = (int)(angle * 4096.0 * 0.15915494327375637);
    iAngle = ((iAngle << 4) >> 4);   /* sign-extend 28→32 bit */
    iAngle = (-iAngle) & 0xFFF;

    int sinVal = g_sinTable[iAngle] >> 2;
    int cosVal = g_cosTable[iAngle] >> 2;

    int result = (z1 - z2) * sinVal + (x1 - x2) * cosVal;
    return result / 4096;
}

/* =====================================================================
 * ComputeScaledOffset — 0x004d5ea0 — 101 bytes
 * Computes fixed-point scaled offsets from a source geometry struct.
 * ratio = (src[0xc] >> 16 << 12) / (src[0] >> 16)  (12-bit fixed point)
 * dst[0xc4] = dst[0xbc] * ratio + (p4 * ratio) / 4096
 * dst[0xc0] = p3 * (src[0xe] >> 16)
 * EAX=dst, EDX=src, EBX=p3, ECX=p4
 * ===================================================================== */
void ComputeScaledOffset(int *dst, int *src, int p3, int p4)  /* EAX, EDX, EBX, ECX */
{
    int numerator = (int)*(short *)((char *)src + 0x0e) << 12;  /* high short of dword at +0xC */
    int denominator = *(short *)((char *)src + 0x02);          /* high short of dword at +0x0 */
    int ratio = numerator / denominator;            /* 12-bit fixed point ratio */

    int baseVal = *(int *)((char *)dst + 0xbc);    /* dst field at +0xbc */
    int scaled = baseVal * ratio;
    int extra = (int)((long long)p4 * ratio / 4096);
    *(int *)((char *)dst + 0xc4) = scaled + extra;

    *(int *)((char *)dst + 0xc0) = p3 * *(short *)((char *)src + 0x10);
}

/* =====================================================================
 * LoopSurfaceCalc — 0x004d6f9c — 688 bytes
 * Computes player orientation on a loop surface polygon.
 * Reads loop surface index from player[+0x92]>>16, looks up 4 vertices,
 * computes edge midpoints, projects player position onto surface axes
 * via ComputeDirectionalDistance, interpolates with ComputeScaledOffset,
 * and computes orientation via sin/cos tables.
 * EAX = player struct pointer
 * ===================================================================== */
static void LoopSurfaceCalc(Player *player)
{
    /* Loop surface index from player[+0x92] >> 16 = short at 0x94 */
    int loopIdx = player->_unk_0x94;

    /* Loop entry in surface table: stride 22 (0x16) */

    /* Player offset on surface */
    int playerOff = player->_unk_0xBC;

    TerLoopEntry *we = &((TerLoopEntry *)g_terLoopTable)[loopIdx];
    int vtxBase = we->vtxBase + playerOff;

    /* Look up 4 vertices from g_terUnknown74, stride 0x22 (34 bytes per vertex) */
    char *vtxData = (char *)g_terUnknown74;
    char *v = vtxData + vtxBase * 34;  /* vtxBase * 0x22 = vtxBase * (16+1) * 2 */

    /* Read 4 vertex positions (each vertex: short x, dword y (>>16), ...) at offsets 0, 6, 0xc, 0x12 */
    int v0x = (short)(*(short *)(v + 0));
    int v0y = *(short *)(v + 4);
    int v1x = (short)(*(short *)(v + 6));
    int v1y = *(short *)(v + 0xa);
    int v2x = (short)(*(short *)(v + 0xc));
    int v2y = *(short *)(v + 0x10);
    int v3x = (short)(*(short *)(v + 0x12));
    int v3y = *(short *)(v + 0x16);

    /* Compute 4 midpoints between adjacent vertices */
    int m01x = (v0x + v1x) >> 1;
    int m01y = (v0y + v1y) >> 1;
    int m12x = (v1x + v2x) >> 1;
    int m12y = (v1y + v2y) >> 1;
    int m23x = (v2x + v3x) >> 1;
    int m23y = (v2y + v3y) >> 1;
    int m30x = (v3x + v0x) >> 1;
    int m30y = (v3y + v0y) >> 1;

    /* Compute distance between opposite midpoint pairs */
    /* Axis 1: m01 to m23 */
    int dx1 = m01x - m23x;
    int dy1 = m01y - m23y;
    int dist1 = (int)sr_sqrtf((float)(dx1 * dx1 + dy1 * dy1));
    dist1 = ((dist1 << 8) >> 8);  /* sign-extend 24-bit */
    if (dist1 < 1) {
        dist1 = 1;
    }

    /* Axis 2: m12 to m30 */
    int dx2 = m12x - m30x;
    int dy2 = m12y - m30y;
    int dist2 = (int)sr_sqrtf((float)(dx2 * dx2 + dy2 * dy2));
    dist2 = ((dist2 << 8) >> 8);
    if (dist2 < 1) {
        dist2 = 1;
    }

    /* Project player position onto both axes via atan2-based distance */
    int playerX = player->posX >> 12;
    int playerZ = player->posZ >> 12;

    int proj1 = ComputeDirectionalDistance(playerX, playerZ, m23x, m23y, m01x, m01y);
    int proj2 = ComputeDirectionalDistance(playerX, playerZ, m30x, m30y, m12x, m12y);

    /* Compute interpolation factor for axis 1: clamp to 0..0x1000 */
    int half1 = (dist1 + ((unsigned)dist1 >> 31)) >> 1;  /* abs(dist1)/2 */
    int t1 = ((proj2 + half1) << 12) / dist1;
    if (t1 < 0) {
        t1 = 0;  /* clamp negative to zero (binary: xor edi,eax at 0x4d7159) */
    }
    else if (t1 > 0x1000) {
        t1 = 0x1000;
    }

    /* Compute interpolation factor for axis 2: clamp to 0..0x1000 */
    int half2 = (dist2 + ((unsigned)dist2 >> 31)) >> 1;
    int t2 = ((proj1 + half2) << 12) / dist2;
    if (t2 < 0) {
        t2 = 0;
    }
    else if (t2 > 0x1000) {
        t2 = 0x1000;
    }

    /* Apply scaled offset computation */
    ComputeScaledOffset((int *)player, (int *)we, t1, t2);

    /* Compute orientation from loop surface normal */
    int surfAngle = we->surfAngle;
    surfAngle = (-surfAngle) & 0xFFF;

    int sinVal = g_sinTable[surfAngle];
    int cosVal = g_cosTable[surfAngle];

    /* Rotate player's forward/side vectors by surface angle */
    int fwd = player->velX;   /* player forward (0x2C) */
    int side = player->velZ;   /* player side (0x34) */

    /* Cross product: fwd*cos + side*sin */
    long long cross1 = (long long)fwd * cosVal + (long long)side * sinVal;
    player->loopVelX = (int)(cross1 / 0x1000) >> 2;

    /* Cross product: -fwd*sin + side*cos */
    long long cross2 = (long long)(-fwd) * sinVal + (long long)side * cosVal;
    player->_unk_0xF8 = 0x400;
    player->pitchCombo = 0;
    player->loopVelZ = (int)(cross2 / 0x1000) >> 2;
}

/* =====================================================================
 * LoopSurfaceCheck — 0x004d751c — 210 bytes
 * Checks if a player is within a loop surface polygon.
 * Iterates loop surface entries (stride 22 from g_terLoopTable,
 * count from g_terLoopCount). Checks Z coordinate range against
 * two axis-aligned bounds per surface.
 * If match found: sets loop flags, stores height offset, calls
 * LoopSurfaceCalc for orientation computation.
 * EAX = player struct, EDX = surface index (surfIdx & 0xFFF), EBX = height threshold
 * Returns: 1 if on loop surface, 0 if not
 * ===================================================================== */
int LoopSurfaceCheck(Player *player, int surfaceIdx, int heightThreshold)  /* EAX, EDX, EBX */
{
    /* Quick reject: if height threshold > player posY, not on a loop surface */
    if (heightThreshold > player->posY) {
        return 0;
    }

    int loopCount = g_terLoopCount;
    TerLoopEntry *wentries = (TerLoopEntry *)g_terLoopTable;
    int i;

    for (i = 0; i < loopCount; i++) {
        TerLoopEntry *we = &wentries[i];

        if (surfaceIdx >= we->rangeStart1) {
            if (surfaceIdx < we->rangeStart1 + we->rangeSize) {
                player->_unk_0xBC = surfaceIdx - we->rangeStart1;
                goto found;
            }
        }

        if (surfaceIdx <= we->rangeStart2) {
            if (surfaceIdx > we->rangeStart2 - we->rangeSize) {
                player->_unk_0xBC = we->field2 - 1 - (we->rangeStart2 - surfaceIdx);
                goto found;
            }
        }
    }

    /* No loop surface found */
    return 0;

found:
    /* Set loop surface flags on player */
    player->loopMode = 1;       /* on loop surface */
    player->airTimer = 0;         /* grounded (on loop) */
    player->groundedFlag = 1;    /* grounded on loop */
    player->overSurface = 1;      /* surface found */
    player->posY = heightThreshold;  /* store loop surface height */
    player->_unk_0x94 = (short)i;    /* loop surface index */

    /* Compute loop surface orientation */
    LoopSurfaceCalc(player);
    return 1;
}

/* =====================================================================
 * IsOnTrackSurface — FUN_0047b794 — 562 bytes
 *
 * Tests if a world (X, Z) position is over valid track surface.
 * Transforms world XZ → tile grid (0-4095) via float globals,
 * looks up the tile map, samples the pixel from the playfield
 * texture, and checks for the transparent green color.
 *
 * Returns 1 if on-track, 0 if off-track / out of bounds.
 * Stack params (Watcom): float x at [esp+0x24], float z at [esp+0x28].
 * Uses ret 8 (callee-cleans 2 float params).
 * ===================================================================== */
int IsOnTrackSurface(float x, float z)
{
    /* Transform world X → grid X
     * Binary FPU trace (0x47B79D-0x47B7D5):
     *   gx = (x - scaleX*baseScale) * 128 / ((offsetX - scaleX) * baseScale)
     * scaleX=boundsB, offsetX=boundsD, baseScale=boundsA */
    float bx = g_gridBaseScale;
    float sxA = g_gridScaleX * bx;                           /* scaleX * baseScale */
    float dxA128 = (g_gridOffsetX - g_gridScaleX) * bx * GRID_RECIP; /* (offsetX-scaleX)*baseScale/128 */
    float gx = (x - sxA) / dxA128;

    /* Transform world Z → grid Z (same pattern, 0x47B7D9-0x47B7FF) */
    float szA = g_gridScaleZ * bx;
    float dzA128 = (g_gridOffsetZ - g_gridScaleZ) * bx * GRID_RECIP;
    float gz = (z - szA) / dzA128;

    /* Scale to 0-4095 and convert to int */
    gx *= GRID_TILE;
    gz *= GRID_TILE;
    int gridX = (int)gx;
    int gridZ = (int)gz;

    /* Bounds check */
    if (gridX < 0 || gridZ < 0 || gridX > 0xFFF || gridZ > 0xFFF) {
        return 0;
    }

    /* Tile lookup: divide by 32 to get tile index */
    int tileZ = (0xFFF - gridZ) / 32;  /* binary: 0xFFF - gridZ, then /32 */
    int tileX = gridX / 32;
    unsigned char tileIdx = g_tileMap[tileZ * 256 + tileX];

    /* Sub-tile pixel offset within the 32×32 tile */
    int subZ = (0xFFF - gridZ) & 0x1F;
    int subX = gridX & 0x1F;

    /* D3D mode: decode tile byte into tpage index + sub-page offset */
    int tpageRow = (tileIdx & 0x3F) / 8;

    /* Binary reads [0x8F6C54] = g_tpageUIAlt as the base tpage for
     * playfield tile pixel lookup. NOT g_tpagePlayfield2 (0x8F6C2C). */
    int baseTpage = g_tpageUIAlt + (tileIdx >> 6);

    int pixelY = tpageRow * 32 + subZ;            /* tile row in 8×8 grid */
    int pixelX = (tileIdx & 7) * 32 + subX;   /* tile col in 8×8 grid */

    /* Sample pixel from tpage 16bpp buffer (our GL port stores these).
     * Binary reads from locked D3D surface at 24bpp; we read from the
     * 16bpp pixel buffer used for GL texture upload.
     * Layout: 256×256 pixels, game's R5G5_0B5 format.
     * 0x47B992 spells the key out longhand on the 24bpp bytes: r>>3 != 0,
     * g>>3 != 0x1F, b>>3 != 0 each return on-track, so only exact key green
     * is off-track. */
    unsigned short *pixels = (unsigned short *)g_tpagePixelBuf[baseTpage];
    if (pixels == NULL) {
        return 1;  /* no texture → assume on-track */
    }

    int pixelIdx = pixelY * 256 + pixelX;
    unsigned short pixel = pixels[pixelIdx];
    unsigned char r5 = (pixel >> 11) & 0x1F;
    unsigned char g5 = (pixel >> 6) & 0x1F;
    unsigned char b5 = pixel & 0x1F;
    if (IS_COLOR_KEY_RGB5(r5, g5, b5)) {
        return 0;  /* green = off-track */
    }

    return 1;  /* on-track */
}

void DeriveGroundState(int posX, int posZ, int *outHeight,
                       short *outNormX, short *outNormY, short *outNormZ)
{
    int bestHeight = 0;
    int bestFaceIdx = -1;

    *outHeight = 0;
    *outNormX = 0;
    *outNormY = -0x1000;
    *outNormZ = 0;

    if (g_trackSurfaceData == NULL || g_terGridIndex == NULL || g_terGridData == NULL) {
        return;
    }

    int cellX = ((posX >> 12) - g_aiGridOriginX) / g_aiGridCellWidth;
    int cellZ = ((posZ >> 12) - g_aiGridOriginZ) / g_aiGridCellHeight;
    if (cellX < 0 || cellZ < 0 || cellX > 0x1F || cellZ > 0x1F) {
        return;
    }

    short *gridIdx = (short *)g_terGridIndex;
    short *gridData = (short *)g_terGridData;
    short *polyList = gridData + gridIdx[cellZ * 0x20 + cellX];

    if (*polyList == (short)0xFFFF) {
        return;
    }

    do {
        unsigned short entry = *polyList++;
        if ((entry & 0x4000) != 0) {
            continue;
        }

        int surfIdx = (short)entry & 0xFFF;
        TerSurface *surf = (TerSurface *)g_trackSurfaceData + surfIdx;
        int sx = (posX >> 12) - surf->centerX;
        int sz = (posZ >> 12) - surf->centerZ;
        if (sx * sx + sz * sz >= surf->radiusSq) {
            continue;
        }
        if (!PointInPolygon(posX >> 12, posZ >> 12, surfIdx)) {
            continue;
        }
        int h = InterpolateGroundHeight(posX >> 12, posZ >> 12, surfIdx, s_hitEdgeIdx);
        if (h <= bestHeight) {
            bestHeight = h;
            bestFaceIdx = s_hitEdgeIdx + surf->faceBase;
        }
    } while ((*(polyList - 1) & 0x8000) == 0);

    *outHeight = bestHeight;

    if (bestFaceIdx >= 0) {
        TerFace *face = &((TerFace *)g_terFaceTable)[bestFaceIdx];
        *outNormX = face->normalX;
        *outNormY = face->normalY;
        *outNormZ = face->normalZ;
    }
}
