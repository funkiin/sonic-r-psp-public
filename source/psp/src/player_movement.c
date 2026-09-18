/**
 * player_sprite.c — UpdatePlayerMovement
 *
 * UpdatePlayerMovement @ 0x004d8ab8 — 172 bytes
 * Per-player dispatcher called from the race loop.
 * Handles: collectible collision, wall collision, ground collision,
 * slope physics, Y clamping, and position update.
 *
 * in_EAX = player struct pointer (Watcom fastcall).
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "player_struct.h"
#include <math.h>

/* Sub-functions called by UpdatePlayerMovement */
extern void GroundCollision(Player *player);      /* FUN_004d75f0 — 844 bytes — ground_collision.c */
extern int SampleTerrainGrid(int, int, int);          /* FUN_004d7c08 */

/* Forward declarations for wall collision system */
static void WallCollision(Player *player);
static void WallBounce(Player *player, int bounceMag, short *segStart, short *segEnd);
static void WallPushback(Player *player);
static int  WallHeightGate(Player *player, short *segStart, short *segEnd);

/* =====================================================================
 * SwapLinkedListNodes — 0x004d8b64 — 106 bytes
 * Swaps link fields between nodes in a 12-byte-stride structure array.
 * EAX = node index
 * Array base at [0x6da578], link table at [0x6da568] (16-byte stride).
 * Clears field +8, swaps field +4 with link table entry +6.
 * If upper 16 bits of field +0 != -1, also swaps field +6.
 * ===================================================================== */
void SwapLinkedListNodes(int nodeIndex)  /* EAX */
{
    TerItemState    *ist = (TerItemState *)g_itemStateTable + nodeIndex;
    TerCollisionMesh *cm = (TerCollisionMesh *)g_terCollisionMesh;

    int idx0 = ist->meshIdx0;
    ist->activeFlag = 0;

    /* Swap storedMask0 with collision mesh mask for meshIdx0 */
    int16_t linkVal = cm[idx0].mask;
    cm[idx0].mask = ist->storedMask0;
    ist->storedMask0 = linkVal;

    /* If meshIdx1 != -1, also swap storedMask1 */
    if (ist->meshIdx1 != -1) {
        int idx1 = ist->meshIdx1;
        int16_t linkVal2 = cm[idx1].mask;
        cm[idx1].mask = ist->storedMask1;
        ist->storedMask1 = linkVal2;
    }
}

/**
 * CollectibleCollision — FUN_004d8bd0 — 312 bytes
 * Per-player collectible/ring pickup collision check.
 *
 * Iterates the item state table (stride 12, count at g_terCollectibleCount).
 * Each entry references 1-2 objects in g_terCollisionMesh (stride 16).
 * For active entries (type==1), checks XZ distance² against object radius².
 * On hit: calls SwapLinkedListNodes to swap link fields, subtracts
 * the entry's value from player->ringCount.
 *
 * EAX = player pointer.
 */
static void CollectibleCollision(Player *player)
{
    unsigned char playerSlot = (unsigned char)player->collisionLayer;
    unsigned short playerBit = (unsigned short)(1 << playerSlot);

    TerItemState     *items = (TerItemState *)g_itemStateTable;
    TerCollisionMesh *cm    = (TerCollisionMesh *)g_terCollisionMesh;
    int count = g_terCollectibleCount;

    for (int i = 0; i < count; i++) {
        TerItemState *ist = &items[i];

        if (ist->activeFlag != 1) {
            continue;
        }
        if (ist->ringCost > player->ringCount) {
            continue;
        }

        int hit = 0;

        /* Check object 1 (meshIdx0) */
        TerCollisionMesh *obj1 = &cm[ist->meshIdx0];
        if (obj1->mask & playerBit) {
            int dx = (player->posX >> 12) - obj1->centerX;
            int dz = (player->posZ >> 12) - obj1->centerZ;
            if (dx * dx + dz * dz < obj1->radiusSq) {
                hit = 1;
            }
        }

        /* Check object 2 (meshIdx1) if no hit */
        if (!hit) {
            if (ist->meshIdx1 == -1) {
                continue;
            }

            TerCollisionMesh *obj2 = &cm[ist->meshIdx1];
            if (!(obj2->mask & playerBit)) {
                continue;
            }

            int dx = (player->posX >> 12) - obj2->centerX;
            int dz = (player->posZ >> 12) - obj2->centerZ;
            if (dx * dx + dz * dz >= obj2->radiusSq) {
                continue;
            }
        }

        SwapLinkedListNodes(i);
        player->ringCount -= ist->ringCost;
        return;
    }
}

/**
 * WallHeightGate — FUN_004d6184 — 371 bytes — translated from binary
 * Projects player midpoint onto wall segment in wall-aligned coordinates,
 * clamps along wall length, interpolates wall vertex Y at that position.
 * Returns the wall height (in <<12 fixed point) at the player's position.
 *
 * Watcom: EAX=player, EDX=seg0, EBX=seg1. Returns height in EAX.
 * ECX is preserved (caller passes midY in ECX and reads it back after call).
 */
static int WallHeightGate(Player *player, short *seg0, short *seg1)
{
    /* Wall edge direction */
    int dX = (int)*(short *)seg1 - (int)*(short *)seg0;                    /* 0x4D619B */
    int dZ = *(short *)((char *)seg1 + 4) -
             *(short *)((char *)seg0 + 4);                                 /* 0x4D61AF */

    /* Wall angle and rotation setup — same pattern as WallBounce */
    sr_double angle_f = sr_atan2((sr_double)dX, (sr_double)dZ);            /* 0x4D61BA: atan2(dX, dZ) */
    int wallAngle = (int)(angle_f * 4096.0 * 0.15915494327375637);
    wallAngle = ((wallAngle << 4) >> 4);
    int negAngle = (-wallAngle) & 0xFFF;                                   /* 0x4D61DC */

    int sinNeg = g_sinTable[negAngle];                                     /* 0x4D61E6 — NOT >>2 */
    int cosNeg = g_cosTable[negAngle];                                     /* 0x4D61ED — NOT >>2 */

    int dX_K = dX << 12;                                                   /* 0x4D61F4 */
    int dZ_K = dZ << 12;                                                   /* 0x4D6200 */

    int seg0X = (int)*(short *)seg0;                                       /* from 0x4D6198 */
    int seg0Z = *(short *)((char *)seg0 + 4);                               /* from 0x4D61AC */

    /* Player midpoint relative to seg0, in <<12 fixed point */
    int relX = ((player->posX + player->prevPosX) >> 1) - (seg0X << 12);            /* 0x4D6212 */
    int relZ = ((player->posZ + player->prevPosZ) >> 1) - (seg0Z << 12);           /* 0x4D622D */

    /* Rotate wall direction into wall-aligned frame — 0x4D6232 */
    /* normalComp (unused directly, but binary computes it) */
    /* tangentComp = wall length in tangent direction */
    int wallTangent = (int)(((long long)(-dX_K) * (long long)sinNeg +      /* 0x4D624F */
                             (long long)dZ_K * (long long)cosNeg) / 4096);
 
    int playerTangent = (int)(((long long)(-relX) * (long long)sinNeg +    /* 0x4D628F */
                               (long long)relZ * (long long)cosNeg) / 4096);

    /* Clamp playerTangent to [0, wallTangent] — 0x4D62A5 */
    int clamped;
    if (playerTangent < 0) {
        clamped = 0;                                                        /* 0x4D62A9 */
    }
    else if (playerTangent > wallTangent) {
        clamped = wallTangent;                                              /* 0x4D62B4 */
    }
    else {
        clamped = playerTangent;
    }

    /* Fraction along wall: clamped * 0x1000 / wallTangent — 0x4D62C0 */
    int fraction = (int)(((long long)clamped * 0x1000) / wallTangent);

    /* Read vertex Y (height) from upper 16 bits of dword at vertex — 0x4D62CA */
    int seg0Y = *(short *)((char *)seg0 + 2);                               /* upper 16 of first dword */
    int seg1Y = *(short *)((char *)seg1 + 2);
    int dY = seg1Y - seg0Y;                                                /* 0x4D62D4 */

    /* Interpolate: seg0Y*K + dY*K * fraction / K — 0x4D62E9 */
    return (seg0Y << 12) + (int)(((long long)(dY << 12) * fraction) / 0x1000); /* 0x4D62ED */
}

/**
 * WallCollision — FUN_004d848c — 1143 bytes
 * Translated from binary. Detects wall crossings using terrain grid
 * spatial lookup + 4-edge cross product point-in-quad tests.
 * Watcom: in_EAX = player.
 */
static void WallCollision(Player *player)
{
    int px = player->posX >> 12;                                /* 0x4D849A */
    int pz = player->posZ >> 12;                                /* 0x4D84C2 */

    player->_unk_0xD6 = 0;                               /* 0x4D84AF */

    /* Grid cell lookup — same pattern as GroundCollision */
    int cellX = (px - g_aiGridOriginX) / g_aiGridCellWidth;  /* 0x4D84BD */
    int cellZ = (pz - g_aiGridOriginZ) / g_aiGridCellHeight; /* 0x4D84DF */
    int cellIdx = cellZ * 0x20 + cellX;                      /* 0x4D84E4 */

    short *gridIdx = (short *)g_terGridIndex;
    short *polyList = (short *)g_terGridData + gridIdx[cellIdx]; /* 0x4D84FC */

    short entry = *polyList;                                  /* 0x4D8501 */
    if (entry == -1) {
        goto exit_no_hit;                        /* 0x4D8507 */
    }

    int playerMask = 1 << ((unsigned char)player->collisionLayer);    /* 0x4D8518 */

    /* Polygon list loop */
loop_top:
    entry = *polyList;                                        /* 0x4D852A */
    polyList++;                                               /* 0x4D8530 */

    /* Only process wall polygons (bit 0x4000 set) */
    if ((entry & 0x4000) == 0) {
        goto check_end;               /* 0x4D8534 */
    }

    int surfIdx = entry & 0xFFF;
    TerCollisionMesh *wall = &((TerCollisionMesh *)g_terCollisionMesh)[surfIdx];

    if ((playerMask & wall->mask) == 0) {
        goto check_end;
    }
    int cx = wall->centerX;
    int cz = wall->centerZ;
    if ((px - cx) * (px - cx) + (pz - cz) * (pz - cz) > wall->radiusSq) {
        goto check_end;
    }

    /* Get wall segment vertex data */
    char *seg0 = (char *)g_terEdgeList + (int)wall->vtxBase * 6;
    char *seg1 = seg0 + 6;
    int segIdx = 0;
    int segCount = (int)wall->vtxCount - 1;

    /*- Segment loop- */
    while (segIdx < segCount) {
        int seg1Z = *(short *)(seg1 + 4);
        int seg0Z = *(short *)(seg0 + 4);
        int segDZ = seg1Z - seg0Z;
        int seg1X = (int)*(short *)seg1;
        int seg0X = (int)*(short *)seg0;
        int segDX = seg1X - seg0X;

        /* Quad corners: wall segment extruded by (segDZ, -segDX) */
        int tX2 = player->prevPosX  - segDZ;              /* 0x4D861A: prevX - segDZ */
        int tZ2 = player->prevPosZ + segDX;               /* 0x4D862D: prevZ + segDX */
        int tX1 = player->posX  + segDZ;                  /* 0x4D8637: posX + segDZ */
        int tZ1 = player->posZ  - segDX;                  /* 0x4D863F: posZ - segDX */

        int v0X = seg0X * 0x1000 - segDX;                 /* 0x4D864E: [ebp-0x30] */
        int v0Z = seg0Z * 0x1000 - segDZ;                 /* 0x4D864C: [ebp-0x38] */
        int v1X = seg1X * 0x1000 + segDX;                 /* 0x4D865F: [ebp-0x2c] */
        int v1Z = seg1Z * 0x1000 + segDZ;                 /* 0x4D866D: [ebp-0x3c] */

        /* 4 cross product tests — point-in-quad check for player path vs wall */
        /* Test 1: (t1 - v0) side of edge (v0 → v1) — 0x4D8675 */
        if ((long long)(tX1 - v0X) * (long long)(v1Z - v0Z) -
            (long long)(tZ1 - v0Z) * (long long)(v1X - v0X) < 0)
        {
            goto next_seg;
        }

        /* Test 2: (t2 - v1) side of edge (v1 → v0) — 0x4D86AF */
        if ((long long)(tX2 - v1X) * (long long)(v0Z - v1Z) -
            (long long)(tZ2 - v1Z) * (long long)(v0X - v1X) < 0)
        {
            goto next_seg;
        }

        /* Test 3: (t1 - v0) side of edge (v0 → t2) — 0x4D86F1 */
        if ((long long)(tX1 - v0X) * (long long)(tZ2 - v0Z) -
            (long long)(tZ1 - v0Z) * (long long)(tX2 - v0X) < 0)
        {
            goto next_seg;
        }

        /* Test 4: (t1 - t2) side of edge (t2 → v1) — 0x4D8733 */
        if ((long long)(tX1 - tX2) * (long long)(v1Z - tZ2) -
            (long long)(tZ1 - tZ2) * (long long)(v1X - tX2) < 0)
        {
            goto next_seg;
        }

        /* Wall crossing detected */

        /* Height gate check — 0x4D8775 */
        if (wall->heightGate != 0) {
            int midY = (player->posY + player->prevPosY) >> 1;       /* 0x4D8790: (posY + prevY) / 2 */
            int gateH = WallHeightGate(player, (short *)seg0, (short *)seg1); /* 0x4D8797 */
            if (midY < gateH) {
                goto check_end;              /* 0x4D879C: wall below player → skip */
            }
        }

        /* Set wall-hit state — 0x4D87A4 */
        player->renderState = 0x30002;                    /* 0x4D87A7 */
        player->_unk_0xBA = 1;                         /* 0x4D87B7 */

        /*- Tag mode (raceType==2 && lapConfig < 2) — 0x4D87C0- */
        if (g_raceType == RACE_TIMEATTACK && g_raceSubMode < 2) {  /* 0x4D87C0,0x4D87C5 */
            if (wall->softFlag == 0) {
                goto wall_bounce;
            }

            /* Tag boundary — only player 1 gets the flag */
            if (player != g_playerBase) {
                return;
            }
            wall->mask = 0x80;
            player->_unk_0xD6 = 1;                     /* 0x4D87F5 */
            if (g_trackId == TRACK_RADICAL_CITY && player->posX > 0) {           /* 0x4D87FE,0x4D8807 */
                player->_unk_0xD6 = 2;                 /* 0x4D8810 */
            }
            return;
        }

        /*- Soft wall: velocity damping — 0x4D881E- */
        if (wall->softFlag != 0) {
            if (player->forwardSpeed > 0x18000 ||                   /* 0x4D882E: player speed > threshold */
                player->airTimer != 0)                 /* 0x4D8837: or airborne */
            {
                /* Damp velocity to 3/4 — 0x4D8845 */
                player->velX = (player->velX * 3) / 4;     /* 0x4D8848-0x4D8864 */
                player->velZ = (player->velZ * 3) / 4;     /* 0x4D8867-0x4D8883 */
                wall->mask = 0x80;                          /* 0x4D8889 */
                player->_unk_0xD6 = 1;                  /* 0x4D8895 */
                if (g_trackId == TRACK_RADICAL_CITY && player->posX > 0) {       /* 0x4D889E,0x4D88A3 */
                    player->_unk_0xD6 = 2;              /* 0x4D88A8 */
                }
                return;
            }
        }

    wall_bounce:
        /* Hard wall: bounce + pushback — 0x4D88BB */
        WallBounce(player, (int)wall->bounceMag,
                   (short *)seg0, (short *)seg1);           /* 0x4D88CC */
        WallPushback(player);                               /* 0x4D88D4 */
        return;

    next_seg:
        seg0 += 6;                                          /* 0x4D85C7 */
        seg1 += 6;                                          /* 0x4D85C3 */
        segIdx++;                                           /* 0x4D85C6 */
    }
    

check_end:
    if (entry & (short)0x8000) {
        goto exit_no_hit;               /* 0x4D88E3 */
    }
    goto loop_top;                                             /* 0x4D88E7→0x4D8527 */

exit_no_hit:
    player->_unk_0xBA = 0;                                 /* 0x4D88F0 */
}

/**
 * WallBounce — FUN_004d7f2c — 587 bytes — translated from binary
 * Bounces player off a wall segment. Computes wall normal via atan2,
 * decomposes velocity into normal/tangent, reflects normal component,
 * then repositions player at prevPos + reflected velocity.
 *
 * Watcom: EAX=player, EDX=seg0, EBX=seg1, ECX=bounceMag
 */
static void WallBounce(Player *player, int bounceMag, short *seg0, short *seg1)
{
    /* Wall edge direction */
    int dX = (int)*(short *)seg1 - (int)*(short *)seg0;                  /* 0x4D7F3F */
    int dZ = *(short *)((char *)seg1 + 4) -
             *(short *)((char *)seg0 + 4);                               /* 0x4D7F4D */

    /* Wall angle: atan2(dX, dZ) → 12-bit game angle */
    sr_double angle_f = sr_atan2((sr_double)dX, (sr_double)dZ);          /* 0x4D7F64: fild dZ, fild dX → atan2(dX, dZ) */
    int wallAngle = (int)(angle_f * 4096.0 * 0.15915494327375637);       /* 0x4D7F69: *4096/(2π) */
    wallAngle = ((wallAngle << 4) >> 4) & 0xFFF;                         /* 0x4D7F80 */

    /* Player displacement from previous position */
    int deltaZ = player->posZ - player->prevPosZ;                                  /* 0x4D7F91: posZ - prevZ */
    int deltaX = player->posX - player->prevPosX;                                   /* 0x4D7F9C: posX - prevX */

    /* Rotate into wall-aligned frame using -wallAngle */
    int negAngle = (-wallAngle) & 0xFFF;                                  /* 0x4D7F9E */
    int sinNeg = g_sinTable[negAngle] >> 2;                               /* 0x4D7FAD */
    int cosNeg = g_cosTable[negAngle] >> 2;                               /* 0x4D7FBA */

    /* Normal component (perpendicular to wall) */
    int normalComp = (int)(((long long)deltaX * cosNeg +                  /* 0x4D7FD0 */
                            (long long)deltaZ * sinNeg) / 4096);

    /* Tangent component (along wall) */
    int tangentComp = (int)(((long long)(-deltaX) * sinNeg +              /* 0x4D7FEB */
                             (long long)deltaZ * cosNeg) / 4096);

    /* Reflect: clamp min to 1, negate, divide by 3, cap magnitude at 0x3000 */
    if (normalComp < 1) normalComp = 1;                                   /* 0x4D800A */
    normalComp = -normalComp / 3;                                         /* 0x4D8014 */
    if (normalComp > -0x3000) {
        normalComp = -0x3000;                       /* 0x4D8026 */
    }

    /* Rotate back to world frame using +wallAngle */
    int sinPos = g_sinTable[wallAngle] >> 2;                              /* 0x4D8035 */
    int cosPos = g_cosTable[wallAngle] >> 2;                              /* 0x4D8042 */

    int bounceVelX = (int)(((long long)normalComp * cosPos +              /* 0x4D8056 */
                            (long long)tangentComp * sinPos) / 4096);
    int bounceVelZ = (int)(((long long)(-normalComp) * sinPos +           /* 0x4D8073 */
                            (long long)tangentComp * cosPos) / 4096);

    player->velZ = bounceVelZ;                                            /* 0x4D8092 */
    player->velX = bounceVelX;                                            /* 0x4D8098 */

    /* If bounceMag != 0: add directional push along wall normal */
    if (bounceMag != 0) {                                                  /* 0x4D809B */
        int edgeDX = (int)*(short *)seg0 - (int)*(short *)seg1;           /* 0x4D80AF */
        int edgeDZ = *(short *)((char *)seg0 + 4) -
                     *(short *)((char *)seg1 + 4);                         /* 0x4D80C6 */
        int negEdgeDX = -edgeDX;                                           /* 0x4D80D0 */

        int distSq = edgeDZ * edgeDZ + negEdgeDX * negEdgeDX;             /* 0x4D80DA */
        int dist = (int)sr_sqrt((sr_double)distSq);                       /* 0x4D80E2: fild+fsqrt = 80-bit */
        dist = ((dist << 8) >> 8);                                         /* 0x4D80EF: 24-bit sign-extend */

        int normZ = (edgeDZ << 12) / dist;                                /* 0x4D8106 */
        int normX = (negEdgeDX << 12) / dist;                             /* 0x4D8117 */

        player->velX += normZ * bounceMag;                                 /* 0x4D812A */
        player->velZ += normX * bounceMag;                                 /* 0x4D812C */

        /* Track 2: random wall-hit SFX */
        if (g_trackId == TRACK_RADICAL_CITY) {                                              /* 0x4D8131 */
            unsigned short rv = *g_ringSpawnReadPtr;
            g_ringSpawnReadPtr++;
            player->sfxTrigger = (rv & 1) + 0x33; /* 0x4D8157 */
        }
    }

    /* Reposition: prevPos + bounce velocity */
    player->posX = player->prevPosX + bounceVelX;                                    /* 0x4D815E */
    player->posZ = player->prevPosZ + bounceVelZ;                                   /* 0x4D816D */
}
/**
 * WallPushback — FUN_004d8178 — 785 bytes — translated from binary
 * Secondary wall check after WallBounce: if post-bounce position still
 * intersects a wall quad, snaps player back to previous position.
 * Same grid/polygon/segment/quad pattern as WallCollision.
 *
 * Watcom: EAX = player
 */
static void WallPushback(Player *player)
{
    int px = player->posX >> 12;                                             /* 0x4D8186 */
    int pz = player->posZ >> 12;                                             /* 0x4D81A5 */

    /* Grid cell lookup */
    int cellX = (px - g_aiGridOriginX) / g_aiGridCellWidth;               /* 0x4D81A0 */
    int cellZ = (pz - g_aiGridOriginZ) / g_aiGridCellHeight;              /* 0x4D81C2 */
    int cellIdx = cellZ * 0x20 + cellX;                                    /* 0x4D81C4 */

    short *gridIdx = (short *)g_terGridIndex;
    short *polyList = (short *)g_terGridData + gridIdx[cellIdx];

    short entry = *polyList;                                               /* 0x4D81DD */
    if (entry == -1) return;                                               /* 0x4D81E6 */

    int playerMask = 1 << ((unsigned char)player->collisionLayer);                 /* 0x4D81FD */

    /* Polygon list loop */
loop_top:
    entry = *polyList;                                                     /* 0x4D8209 */
    polyList++;                                                            /* 0x4D820F */

    if ((entry & 0x4000) == 0) {
        goto check_end;                            /* 0x4D8213 */
    }

    int surfIdx = entry & 0xFFF;
    TerCollisionMesh *wall = &((TerCollisionMesh *)g_terCollisionMesh)[surfIdx]; /* 0x4D822E */

    if ((playerMask & wall->mask) == 0) {
        goto check_end;               /* 0x4D823F */
    }

    int cx = wall->centerX;                                            /* 0x4D824A */
    int cz = wall->centerZ;                                            /* 0x4D825D */
    if ((px - cx) * (px - cx) + (pz - cz) * (pz - cz) >
        wall->radiusSq)
    {
        goto check_end;                                /* 0x4D8272 */
    }

    char *seg0 = (char *)g_terEdgeList + (int)wall->vtxBase * 6;      /* 0x4D827B */
    char *seg1 = seg0 + 6;                                             /* 0x4D828E */
    int segIdx = 0;
    int segCount = (int)wall->vtxCount - 1;                            /* 0x4D82B7 */

    /*- Segment loop- */
    while (segIdx < segCount) {                                         /* 0x4D82BE */
        int seg1X = (int)*(short *)seg1;                                /* 0x4D82D2 */
        int seg1Z = *(short *)(seg1 + 4);                              /* 0x4D82D5 */
        int seg0Z = *(short *)(seg0 + 4);                              /* 0x4D82D8 */
        int seg0X = (int)*(short *)seg0;                                /* 0x4D82F4 */
        int segDZ = seg1Z - seg0Z;                                      /* 0x4D82F2 */
        int segDX = seg1X - seg0X;                                      /* 0x4D830C */

        int tX2 = player->prevPosX  - segDZ;                                   /* 0x4D8301 */
        int tZ2 = player->prevPosZ + segDX;                                   /* 0x4D8311 */
        int tX1 = player->posX  + segDZ;                                   /* 0x4D831B */
        int tZ1 = player->posZ  - segDX;                                   /* 0x4D8323 */

        int v0X = seg0X * 0x1000 - segDX;                               /* 0x4D832D */
        int v0Z = seg0Z * 0x1000 - segDZ;                               /* 0x4D833B */
        int v1X = seg1X * 0x1000 + segDX;                               /* 0x4D8351 */
        int v1Z = seg1Z * 0x1000 + segDZ;                               /* 0x4D833B */

        /* 4 cross product tests */
        if ((long long)(tX1 - v0X) * (long long)(v1Z - v0Z) -
            (long long)(tZ1 - v0Z) * (long long)(v1X - v0X) < 0)
        {
            goto next_seg;
        }

        if ((long long)(tX2 - v1X) * (long long)(v0Z - v1Z) -
            (long long)(tZ2 - v1Z) * (long long)(v0X - v1X) < 0)
        {
            goto next_seg;
        }

        if ((long long)(tX1 - v0X) * (long long)(tZ2 - v0Z) -
            (long long)(tZ1 - v0Z) * (long long)(tX2 - v0X) < 0)
        {
            goto next_seg;
        }

        if ((long long)(tX1 - tX2) * (long long)(v1Z - tZ2) -
            (long long)(tZ1 - tZ2) * (long long)(v1X - tX2) < 0)
        {
            goto next_seg;
        }

        /* Wall hit — snap to previous position */                      /* 0x4D845A */
        player->posX = player->prevPosX;
        player->posZ = player->prevPosZ;
        return;

    next_seg:
        seg0 += 6;                                                      /* 0x4D82A6 */
        seg1 += 6;                                                      /* 0x4D82A2 */
        segIdx++;                                                       /* 0x4D82A5 */
    }

check_end:
    if (entry & (short)0x8000) {
        return;                                      /* 0x4D8475 */
    }
    goto loop_top;                                                          /* 0x4D8479→0x4D8206 */
}

/**
 * SurfaceNormalPhysics — FUN_004d793c — 715 bytes — VALIDATED
 *
 * Determines if the player can remain grounded on the current surface.
 * Reads the face normal from terrain data (or uses default up-vector if
 * no surface contact), then computes a slope steepness value using a
 * rotation matrix built from atan2 of the normal's XZ components.
 *
 * Key logic:
 *   - posY >= groundHeight → always grounded
 *   - Near-flat surface (normalY < -4080) with large gap → airborne
 *   - Steep slope (steepness ≤ -1060) → airborne
 *   - Saves current normal to player+0xB4/B6/B8 for next-frame comparison
 *
 * EAX = player pointer
 */
static void SurfaceNormalPhysics(Player *player)
{
    int normalX, normalY, normalZ;

    /* Read surface normal from terrain face data */

    /* On a terrain polygon: look up face normal */
    if (player->overSurface == 1) {
        int surfIdx = player->hitSurfaceIdx;                          /* player[0x2A] */
        TerSurface *surf = (TerSurface *)g_trackSurfaceData + surfIdx;
        int faceIdx = player->hitEdgeIdx + surf->faceBase;
        TerFace *face = &((TerFace *)g_terFaceTable)[faceIdx];
        normalX = face->normalX;                                      /* face bytes 8-9 */
        normalY = face->normalY;                                      /* face bytes 10-11 */
        normalZ = face->normalZ;                                      /* face bytes 12-13 */
    }
    /* No surface contact: default = straight up */
    else {
        normalX = 0;
        normalY = -0x1000;                                           /* -4096 */
        normalZ = 0;
    }

    /* Quick exit if airborne (airTimer != 0) */
    if (player->airTimer != 0) {
        goto save_normals;
    }

    /* If player is at or below ground → grounded */
    if (player->posY >= player->groundHeight) {                                 /* posY >= groundHeight */
        player->groundedFlag = 1;
        goto save_normals;
    }

    /* Player is above ground. Check slope steepness. */

    /* On near-flat surfaces with both current and previous normal near-vertical,
     * if the gap between ground and player exceeds threshold → airborne */
    if (normalY < (int)0xFFFFF010) {                                 /* normalY < -4080 */
        if (player->surfNormY < (int)0xFFFFF010) {        /* prevNormalY < -4080 */
            int gap = player->groundHeight - player->posY;                      /* groundHeight - posY */
            if (gap > 0x19000) {
                /* Too far above flat ground → airborne */
                player->airTimer = 1;                          /* start air timer */
                player->velY = 0;                             /* zero velY */
                player->groundedFlag = 0;                          /* clear grounded */
                goto save_normals;
            }
        }
    }

    /* Compute slope steepness value */
    int prevNX = player->surfNormX;                         /* short at +0xB4 */
    int prevNY = player->surfNormY;                         /* short at +0xB6 */
    int prevNZ = player->surfNormZ;                         /* short at +0xB8 */

    int slopeVal;

    /* If normal unchanged AND normal is purely vertical → default flat */
    if ((prevNX == normalX && prevNY == normalY && prevNZ == normalZ) ||
        (normalX == 0 && normalZ == 0))
    {
        slopeVal = 0x1000;                                           /* 4096 = flat ground */
    }
    else {
        /* Compute azimuth angle of slope direction:
         * atan2(normalX, normalZ) → game angle (4096 = full circle)
         * VALIDATED: binary multiplies by 4096.0 and 1/(2π) */
        sr_double angle = sr_atan2((sr_double)normalX, (sr_double)normalZ);
        int gameAngle = (int)(angle * 4096.0 * 0.15915494327375637);
        gameAngle = (gameAngle << 4) >> 4;                           /* sign-extend 28-bit */
        gameAngle &= 0xFFF;

        int negAngle = (-gameAngle) & 0xFFF;

        /* Sin/cos lookups (table values are fixed-point, >>2 for scaling) */
        int sinNeg = g_sinTable[negAngle] >> 2;
        int cosNeg = g_cosTable[negAngle] >> 2;
        int sinPos = g_sinTable[gameAngle] >> 2;
        int cosPos = g_cosTable[gameAngle] >> 2;

        /* Rotate normal XZ by -angle (align slope direction to axis) */
        int rot1 = (normalZ * sinNeg + normalX * cosNeg) / 4096;    /* SDIV */
        int rot2 = (normalZ * cosNeg - normalX * sinNeg) / 4096;    /* SDIV */

        /* Second rotation: combine with normalY to get slope components */
        int negNY = -normalY;
        int rot3 = (negNY * sinPos + rot1 * cosPos) / 4096;         /* SDIV */
        int rot4 = (negNY * cosPos - rot1 * sinPos) / 4096;         /* SDIV */

        /* Dot product with previous normal → slope steepness measure.
         * Binary: neg eax (prevNY), imul eax, [esp] (negRot2) → (-prevNY)*(-rot2) = prevNY*rot2 */
        int negRot2 = -rot2;
        int negPrevNY = -prevNY;
        slopeVal = (rot3 * prevNX + negPrevNY * negRot2 + prevNZ * rot4) / 4096;  /* SDIV */
    }

    /* Apply grounded/airborne based on steepness */
    if (slopeVal > (int)0xFFFFFBDC) {                                /* > -1060 */
        player->groundedFlag = 1;                                  /* grounded */
    }
    else {
        player->groundedFlag = 0;                                  /* airborne */
        if (player->airTimer == 0) {
            player->airTimer = 1;                              /* start air timer */
        }
    }

save_normals:
    /* Store current normal for next frame comparison */
    player->surfNormX = (short)normalX;
    player->surfNormY = (short)normalY;
    player->surfNormZ = (short)normalZ;
}

/**
 * SlopeMovement — FUN_004d6b00 — 245 bytes
 * Adjusts player XZ position based on slope of ground surface.
 * Uses delta between current position (player[0,2]) and previous
 * position (player[8,10]) to determine movement direction on the surface.
 * Scales movement by the ratio of horizontal distance to 3D distance
 * (including vertical component), keeping the player on the surface.
 */
static void SlopeMovement(Player *player)
{
    /* Only when grounded (short at +0x72 != 0) and not in special state (+0xB8 upper 16 == 0) */
    if (player->groundedFlag == 0) {
        return;
    }
    if (player->_unk_0xBA != 0) {
        return;
    }

    /* Delta from previous position (player[8,9,10] = prev XYZ at +0x20, +0x24, +0x28) */
    int dx = (player->posX - player->prevPosX) >> 6;
    int dz = (player->posZ - player->prevPosZ) >> 6;

    /* Vertical delta: player[0x0E] - player[9] (ground height - prevY)
     * Binary reads [eax + 0x38] (= player[0x0E] = ground height from GroundCollision),
     * NOT [eax + 0x04] (= player[1] = current Y position).
     * Using player[1] caused a feedback loop where Y drift amplified itself. */
    int dy = (player->groundHeight - player->prevPosY) >> 6;

    /* Horizontal distance = sqrt(dx² + dz²)
     * Binary uses imul (wrapping 32-bit multiply) then fild+fsqrt.
     * Cast to unsigned for the multiply to avoid signed overflow UB,
     * then back to int for the fild (matching binary's signed load). */
    int hDistSq = (int)((unsigned)dx * (unsigned)dx + (unsigned)dz * (unsigned)dz);
    sr_double hDist_f = sr_sqrt((sr_double)hDistSq);
    int hDist = (hDist_f >= 0.0 && hDist_f < 2.147e9) ? (int)hDist_f : 0;

    /* 3D distance = sqrt(dx² + dy² + dz²) */
    int fullDistSq = (int)((unsigned)dx * (unsigned)dx + (unsigned)dy * (unsigned)dy + (unsigned)dz * (unsigned)dz);
    sr_double fullDist_f = sr_sqrt((sr_double)fullDistSq);
    int fullDist = (fullDist_f >= 0.0 && fullDist_f < 2.147e9) ? (int)fullDist_f : 0;

    if (hDist > 0 && fullDist > 0) {
        /* Scale position by hDist/fullDist — projects 3D movement onto horizontal plane.
         * Multiplied by 0x40 (64) to convert from >>6 delta back to position scale.
         * Binary has no ratio clamp — removed the fullDist>hDist*2 guard now that
         * terrain scale is fixed (was 1/65536, now correct 1/4096). */
        player->posX         = player->prevPosX  + (int)((((long long)hDist * dx) / fullDist) * 0x40);
        player->groundHeight = player->prevPosY  + (int)((((long long)hDist * dy) / fullDist) * 0x40);
        player->posZ         = player->prevPosZ + (int)((((long long)hDist * dz) / fullDist) * 0x40);
    }
}

/**
 * LoopGridInterpolate — FUN_004D5F08
 * Bilinearly interpolates the player's world position across the loop
 * surface grid. From the loop-local position it derives a Z cell index and
 * fractional offsets, looks up the four surrounding grid vertices (stride
 * 0x22 in g_terUnknown74), interpolates along the row (zFrac) then the
 * column (xFrac), and writes the result to posX/posY/posZ (<<12 fixed point).
 *
 * Watcom: EAX = player, EDX = loopEntry.
 */
static void LoopGridInterpolate(Player *player, void *loopEntry)
{
    char *ep = (char *)loopEntry;

    /* Cell stride from the entry header (binary 0x4D5F14): both fields are the
     * high word of their dword. zCellIdx/remainder come from loopLocalZ; on
     * clamp the remainder is forced to cellStride - 1 (below). */
    int heightField = *(short *)(ep + 0xE);
    int firstField = *(short *)(ep + 2);  /* high word of first dword */
    if (firstField == 0) {
        return;          /* guard: binary divides unconditionally */
    }
    int cellStride = (heightField << 12) / firstField;
    if (cellStride == 0) {
        return;          /* guard: binary divides unconditionally */
    }

    /* Entry width field */
    int widthField = *(short *)(ep + 0x10);
    int widthScaled = widthField << 12;

    /* Compute Z cell index and remainder from loop position Z */
    int loopZ = player->loopLocalZ;
    int zCellIdx = loopZ / cellStride;
    int zRemainder;

    /* Clamp to valid range — binary forces remainder to cellStride-1 */
    if (firstField <= zCellIdx) {
        zCellIdx = firstField - 1;
        zRemainder = cellStride - 1;
    } else {
        zRemainder = loopZ % cellStride;
    }

    /* Store cell index to player+0xBC */
    player->_unk_0xBC = zCellIdx;

    /* Compute fractional positions: (remainder * 0x1000) / cellStride */
    int loopX = player->loopLocalX;
    int zFrac = (int)((long long)zRemainder * 0x1000 / cellStride);
    int xFrac = (int)((long long)loopX * 0x1000 / widthScaled);

    /* Look up vertex data from g_terUnknown74.
     * Vertex index = entry[0] (short) + zCellIdx
     * Stride = 0x22 bytes per vertex (val*16 + val)*2 = val*34 = val*0x22 */
    short entryBase = *(short *)ep;
    int vtxIdx = (int)entryBase + zCellIdx;
    char *vtx = (char *)g_terUnknown74 + vtxIdx * 0x22;

    /* Read vertex fields for current and next vertices */
    /* vtx+0x00: short = base X position
     * vtx+0x06: short = next row X position
     * vtx+0x00 (dword) >> 16 = base Y (high word of first dword)
     * vtx+0x06 (dword) >> 16 = next row Y
     * etc. for Z fields at +0x02 */

    char *vtxA = vtx;        /* current row */
    char *vtxB = vtx + 6;    /* next row (byte +6) */
    char *vtxC = vtx + 0x12; /* third point (byte +0x12) */

    short baseXS = *(short *)vtxA;
    short nextXS = *(short *)vtxB;
    int dxRow = (int)nextXS - (int)baseXS;

    int baseYA = *(short *)(vtxB + 2);        /* high word at +6 */
    int baseYB = *(short *)(vtxA + 2);       /* high word at +0 */
    int dyRow = baseYA - baseYB;

    int baseZA = *(short *)(vtxB + 4);        /* high word at +8 */
    int baseZB = *(short *)(vtxA + 4);       /* high word at +2 */
    int dzRow = baseZA - baseZB;

    /* Entry+0xC (short at byte 0xC of vertex) */
    short entryFieldC = *(short *)(vtx + 0xC);

    /* Third point fields */
    short thirdXS = *(short *)vtxC;
    int dxCol = (int)entryFieldC - (int)thirdXS;

    int thirdYA = *(short *)(vtx + 0xE);
    int thirdYB = *(short *)(vtxC + 2);
    int dyCol = thirdYA - thirdYB;

    int thirdZA = *(short *)(vtx + 0x10);
    int thirdZB = *(short *)(vtxC + 4);
    int dzCol = thirdZA - thirdZB;

    /* Bilinear interpolation using zFrac (along row) and xFrac (along column) */
    /* Row interpolation: base + delta * zFrac / 4096 */
    int interpX = (int)baseXS + (dxRow * zFrac) / 4096;
    int interpY = baseYB + (dyRow * zFrac) / 4096;
    int interpZ = baseZB + (dzRow * zFrac) / 4096;

    /* Column deltas interpolated by zFrac */
    int colDX = (int)thirdXS + (dxCol * zFrac) / 4096;
    int colDY = thirdYB + (dyCol * zFrac) / 4096;
    int colDZ = thirdZB + (dzCol * zFrac) / 4096;

    /* Compute differences */
    int finalDX = colDY - interpY;
    int finalDY = colDX - interpX;
    int finalDZ = colDZ - interpZ;

    /* Apply xFrac interpolation */
    interpX += (finalDY * xFrac) / 4096;
    interpY += (finalDX * xFrac) / 4096;
    interpZ += (finalDZ * xFrac) / 4096;

    /* Store to player position (scaled to fixed-point) */
    player->posX = interpX << 12;
    player->posY = interpY << 12;
    player->posZ = interpZ << 12;
}

/**
 * UpdateLoopMovement — FUN_004d724c — 719 bytes
 * Updates loop/ride surface physics: integrates velocity, applies loop pull-drag,
 * handles edge bouncing, detects falling off loop surface.
 *
 * EAX = player pointer
 */
static void UpdateLoopMovement(Player *player)
{
    /* Velocity integration */
    int loopX = player->loopLocalX + player->loopVelX;
    int loopZ = player->loopLocalZ + player->loopVelZ;

    /* Compute loop entry pointer from surface index */
    int surfIdx = player->_unk_0x94;
    /* Stride = (idx*4 - idx)*4 - idx = idx*11; *2 = idx*22 = idx*0x16 */
    int entryOff = ((surfIdx * 4 - surfIdx) * 4 - surfIdx) * 2;
    char *loopEntry = (char *)g_terLoopTable + entryOff;

    player->loopLocalX = loopX;
    player->loopLocalZ = loopZ;

    /* Loop pull-drag — only for player 1 (or player 2 in certain modes) */
    int applyDrag = 0;
    if (player == g_playerBase) {
        applyDrag = 1;
    }
    else if (g_isMultiRace != 0 || (g_raceType == RACE_TIMEATTACK && g_raceSubMode < SUBMODE_TAG)) {
        if (player == &g_playerBase[1]) {  /* player 2 */
            applyDrag = 1;
        }
    }

    if (applyDrag) {
        int velZAbs = player->loopVelZ;
        if (velZAbs < 0) {
            velZAbs = -velZAbs;
        }
        if (velZAbs < 0x10000) {
            int entryH = *(short *)(loopEntry + 0xE);
            int pullConst = 0x1770;
            int dragLow = 0;
            int dragHigh = entryH << 11;

            /* Regal Ruin double-loop bounds — binary 0x4D730E `cmp trackId,4`;
             * binary convention 4=Ruin → our TRACK_REGAL_RUIN (3). Was
             * mistranslated as FACTORY (missed 3/4 natural-order swap),
             * applying Ruin's double-loop pull-shaping to Factory's loops. */
            if (g_trackId == TRACK_REGAL_RUIN) {
                int wZ = player->loopLocalZ;
                if (dragHigh > wZ) {
                    dragHigh = entryH << 10;
                }
                else {
                    dragLow = dragHigh;
                    dragHigh = (entryH << 12) - (entryH << 10);
                }
                /* Check if in special double-loop zone — binary 0x4D7338-0x4D735E */
                int zoneLo = (entryH * 3) << 9;   /* lower bound: entryH * 1536 */
                int zoneHi = (entryH * 5) << 9;   /* upper bound: entryH * 2560 */
                wZ = player->loopLocalZ;
                if (zoneLo < wZ && zoneHi > wZ) {
                    pullConst = 0x400;
                }
            }

            /* Compute drag force */
            int dist = dragHigh - player->loopLocalZ;
            if (dist < 0) dist = -dist;
            int range = dragHigh - dragLow;
            if (range > 0) {
                int normalizedDist = (int)((long long)dist * 0x1000 / range);
                int invDist = 0x1000 - normalizedDist;
                int force = ((invDist * invDist) / 1024) + pullConst;

                if (dragHigh > player->loopLocalZ) {
                    player->loopVelZ -= force;
                }
                else {
                    player->loopVelZ += force;
                }
            }
        }
    }

    /* X-axis bounce: clamp to 0, reverse velocity */
    if (player->loopLocalX < 0) {
        int velX = player->loopVelX;
        player->loopLocalX = 0;
        player->loopVelX = (-velX >> 1) + 0x3000;
    }

    /* X-axis ceiling: clamp to entry width */
    int maxX = *(short *)(loopEntry + 0x10) << 12;
    if (maxX < player->loopLocalX) {
        player->loopLocalX = maxX;
        int velX = player->loopVelX;
        player->loopVelX = (-velX >> 1) - 0x3000;
    }

    /* Z-axis bounds check: if out of bounds → fall off loop */
    loopZ = player->loopLocalZ;
    int maxZ = *(short *)(loopEntry + 0xE) << 12;
    if (loopZ < 0 || loopZ > maxZ) {
        /* Fall off loop surface */
        int velX = player->loopVelX;
        int velZ = player->loopVelZ;

        /* Compute rotation from surface angle */
        int surfAngle = *(short *)(loopEntry + 0x14);
        int sinA = g_sinTable[surfAngle] >> 2;
        int cosA = g_cosTable[surfAngle] >> 2;

        /* Rotate velocity into world space (64-bit multiply + divide by 0x1000) */
        long long fwd64 = (long long)velX * cosA + (long long)velZ * sinA;
        player->velX = (int)(fwd64 / 0x1000);

        long long side64 = (long long)(-velX) * sinA + (long long)velZ * cosA;
        player->velZ = (int)(side64 / 0x1000);

        player->loopMode = 0;     /* clear loop flag */
        player->pitchCombo = 0;

        LoopGridInterpolate(player, loopEntry);
        return;
    }

    /* Still on loop surface — update vertex lookup */
    /* Recompute entry from surface index */
    surfIdx = player->_unk_0x94;
    entryOff = ((surfIdx * 4 - surfIdx) * 4 - surfIdx) * 2;
    loopEntry = (char *)g_terLoopTable + entryOff;

    int playerOff = player->_unk_0xBC;
    short entryBase = *(short *)loopEntry;
    int vtxIdx = (int)entryBase + playerOff;
    char *vtxData = (char *)g_terUnknown74 + vtxIdx * 0x22;

    player->pitchCombo = *(short *)(vtxData + 0x20);

    LoopGridInterpolate(player, loopEntry);
}

/**
 * Circular angle convergence helper — smoothly moves 'current' toward 'target'
 * on a 12-bit (0-4095) circular range with half-step convergence.
 */
static int ConvergeAngle12(int current, int target)
{
    int diff = target - current;
    int absDiff = diff < 0 ? -diff : diff;
    int step = absDiff >> 1;  /* binary: sar eax, 1 — floor division */
    int maxStep = 0x1000 - step;

    if (diff > 0) {
        if (diff < step) {
            return target;                    /* snap */
        }
        if (diff < 0x800) {
            return current + step;           /* add step */
        }
        if (diff <= maxStep) {
            int v = current - step;
            return v < 0 ? v + 0x1000 : v;                /* subtract with wrap */
        }
        return target;                                      /* snap */
    }
    else if (diff < 0) {
        if (diff > -step) {
            return target;                   /* snap */
        }
        if (diff > -0x800) {
            return current - step;          /* subtract step */
        }
        if (diff >= -maxStep) {
            int v = current + step;
            return v > 0xFFF ? v - 0x1000 : v;            /* add with wrap */
        }
        return target;                                      /* snap */
    }
    return current;  /* diff == 0 */
}

/**
 * UpdateLoopOrientation — FUN_004d62f8 — 791 bytes
 * Smoothly rotates the player's orientation angles (yaw at +0x0C, roll at +0x14)
 * and loop height factor at +0xC8 to match the loop surface normal.
 *
 * EAX = player pointer
 */
static void UpdateLoopOrientation(Player *player)
{
    /* Compute loop entry and vertex pointer */
    int surfIdx = player->_unk_0x94;
    int entryOff = ((surfIdx * 4 - surfIdx) * 4 - surfIdx) * 2;
    char *loopEntry = (char *)g_terLoopTable + entryOff;

    int playerOff = player->_unk_0xBC;
    short entryBase = *(short *)loopEntry;
    int vtxIdx = (int)entryBase + playerOff;
    char *vtxData = (char *)g_terUnknown74 + vtxIdx * 0x22;

    /* Read surface normal from vertex data at +0x18 */
    char *norm = vtxData + 0x18;
    int normalY = *(short *)(norm + 2);          /* +0x18 high word */
    short normalXS = *(short *)(norm + 4); /* fild word ptr [ecx+4] → signed 16-bit at +0x1C */
    int normalZ = *(short *)(norm + 4);          /* +0x1A high word */

    /* Compute target yaw: atan2(normalX, -normalY) → 12-bit game angle */
    sr_double yawAngle = sr_atan2((sr_double)normalXS, (sr_double)(-normalY));
    int targetYaw = (int)(yawAngle * 4096.0 * 0.15915494327375637);
    targetYaw = (targetYaw << 4) >> 4;  /* sign-extend 28-bit */
    targetYaw &= 0xFFF;

    /* Compute roll target from cross product */
    int negAngle = (-targetYaw) & 0xFFF;
    int sinA = g_sinTable[negAngle];   /* NOT shifted by 2 here */
    int cosA = g_cosTable[negAngle];
    /* (cos * (-normalY) - sin * normalZ) / 4096 */
    int rollProjected = (cosA * (-normalY) - sinA * normalZ) / 4096;

    /* Compute target roll — binary: atan2(*(short*)norm, rollProjected) */
    sr_double rollAngle = sr_atan2((sr_double)*(short *)norm, (sr_double)rollProjected);
    /* The binary multiplies by the two FP constants already loaded on the FPU stack
     * from the yaw computation (fmulp st(2), fmulp st(1)), reusing them. */
    int targetRoll = (int)(rollAngle * 4096.0 * 0.15915494327375637);
    targetRoll = (targetRoll << 4) >> 4;
    targetRoll &= 0xFFF;
    targetRoll = (-targetRoll) & 0xFFF;

    /* Read loop height target */
    int targetHeight = *(short *)(vtxData + 0x20);

    /* Converge yaw (player+0x0C = player[3]) */
    int currentYaw = player->anglePitch;
    player->anglePitch = ConvergeAngle12(currentYaw, targetYaw);

    /* Converge roll (player+0x14 = player[5]) */
    int currentRoll = player->angleRoll;
    player->angleRoll = ConvergeAngle12(currentRoll, targetRoll);

    /* Converge loop height (player+0xC8) — same half-step logic */
    int currentH = player->pitchCombo;
    int hDiff = targetHeight - currentH;
    int hAbsDiff = hDiff < 0 ? -hDiff : hDiff;
    int hStep = hAbsDiff >> 1;
    int hMaxStep = 0x1000 - hStep;

    if (hDiff > 0) {
        if (hDiff < hStep) {
            player->pitchCombo = targetHeight;
            return;
        }
        if (hDiff < 0x800) {
            player->pitchCombo += hStep;
            return;
        }
        if (hDiff > hMaxStep) {
            player->pitchCombo = targetHeight;
            return;
        }
        int v = currentH - hStep;
        player->pitchCombo = v < 0 ? v + 0x1000 : v;
    }
    else if (hDiff < 0) {
        if (hDiff > -hStep) {
            player->pitchCombo = targetHeight;
            return; 
        }
        if (hDiff > -0x800) {
            player->pitchCombo -= hStep;
            return; 
        }
        if (hDiff < -hMaxStep) {
            player->pitchCombo = targetHeight;
            return; 
        }
        int v = currentH + hStep;
        player->pitchCombo = v > 0xFFF ? v - 0x1000 : v;
    }
}

/**
 * PositionUpdate — FUN_004d5220 — 1175 bytes
 * Updates player surface orientation angles: player[3] (+0x0C) and player[5] (+0x14).
 * These represent the character's pitch and roll on the track surface.
 * Uses velocity components, ground normal, and atan2 to compute target angles,
 * then smoothly converges current angles toward targets with clamped step size.
 *
 * When player+0x72 (grounded flag) is 0, angles converge toward 0 (level).
 *
 * Watcom fastcall: EAX = player struct pointer.
 * Register map: ebx=player, ecx=step(0x20), esi=targetA, edi=targetB.
 */
static void PositionUpdate(Player *player)
{
    /* 0x4d522e..0x4d523b — init registers */
    int step = 0x20;          /* ecx = 0x20 — convergence step, never modified */
    int targetB = 0;          /* edi = 0 */
    int targetA = 0;          /* esi = 0 */

    /* 0x4d5235..0x4d5240 — test grounded flag */
    if (player->groundedFlag != 0) {
        /* 0x4d5246..0x4d526f — read velocity components and speed factor */
        int velX      = player->surfNormX;   /* [esp+0x24] */
        int velFwd    = player->surfNormY;   /* [esp+0x20] */
        int velZ      = player->surfNormZ;   /* [esp+0x28] */
        int speedFact = player->forwardSpeed >> 12;    /* edi = player+0x44 >> 12 */

        /* 0x4d5273..0x4d5280 — clamp speedFact to [0, 0x28] */
        if (speedFact < 0) {
            speedFact = 0;
        }
        else if (speedFact > 0x28) {
            speedFact = 0x28;
        }

        /* 0x4d5285..0x4d52a1 — scale forward velocity: velFwd*(0x30-speedFact)/8 */
        int scaledFwd = (velFwd * (0x30 - speedFact)) / 8;  /* [esp+0x20] */

        /* 0x4d52a4..0x4d52bb — magnitude squared = velX^2 + scaledFwd^2 + velZ^2 */
        int magSq = velX * velX + scaledFwd * scaledFwd + velZ * velZ;

        /* 0x4d52c1..0x4d52d7 — mag = (int)sqrt((double)magSq) */
        int mag = (int)sr_sqrt((sr_double)magSq);
        /* 0x4d52dd..0x4d52e3 — shl 8; sar 8 sign-extends from 24 bits (no-op for typical values) */

        /* 0x4d52db..0x4d530a — normalize: normX/Fwd/Z = (component << 12) / mag */
        int normX   = (velX << 12) / mag;       /* [esp+0x24] overwritten */
        int normFwd = (scaledFwd << 12) / mag;  /* [esp+0x20] overwritten */
        int normZ   = (velZ << 12) / mag;       /* eax, stored to [esp+0x28] */

        /* 0x4d530c..0x4d532c — yaw rotation setup (negated yaw) */
        unsigned int yawIdx = (-player->angleYaw) & 0xFFF;
        int cosY = g_cosTable[yawIdx] >> 2;  /* 0x92668c — [esp+8] */
        int sinY = g_sinTable[yawIdx] >> 2;     /* 0x92568c — esi */

        /* 0x4d5330..0x4d534f — projFwd = (normX*cosY + normZ*sinY) / 4096 */
        int projFwd = (normX * cosY + normZ * sinY) / 4096;  /* [esp+0x0C] */

        /* 0x4d5356..0x4d5384 — projLat = -normFwd; negProjX = (-normX*sinY + normZ*cosY)/4096 */
        int projLat  = -normFwd;                                      /* [esp+0x10] */
        int negProjX = (-normX * sinY + normZ * cosY) / 4096;        /* [esp+0x14] */

        /* 0x4d5387..0x4d5393 — first atan2: fild projLat, fild projFwd, call wrapper
         * wrapper: fxch; fpatan → atan2(projFwd, projLat) = atan2(projFwd, -normFwd) */
        sr_double angle1 = sr_atan2((sr_double)projFwd, (sr_double)projLat);

        /* 0x4d5398..0x4d53a1 — compute slope modifier (in edx, parallel with FP ops) */
        int slopeVal = player->yawDelta * speedFact;  /* edx */

        /* 0x4d53a4..0x4d53b9 — convert atan2 to 12-bit angle */
        int angle1_12 = (int)(angle1 * (4096.0 / (2.0 * 3.14159265358979323846)));

        /* 0x4d53bd..0x4d53cf — surface pitch effect: ((angle12+0x10)*speedFact^2) >> 15 */
        int angle1_se = (angle1_12 << 4) >> 4;  /* sign-extend from 28 bits */
        int surfEffect = ((angle1_se + 0x10) * speedFact * speedFact) >> 15;
        player->_unk_0xDE = (short)surfEffect;  /* 0x4d53cf */

        /* 0x4d53d6..0x4d53e5 — drift index = slopeVal / 64, masked to 12 bits */
        int driftIdx = (slopeVal / 64) & 0xFFF;

        /* 0x4d53eb..0x4d5406 — lookup sin/cos for drift rotation */
        int dSin = g_sinTable[driftIdx] >> 2;      /* 0x92568c — esi */
        int dCos = g_cosTable[driftIdx] >> 2;    /* 0x92668c — [esp+8] */

        /* 0x4d53f9..0x4d541f — rotFwd = (projFwd*dCos - projLat*dSin) / 4096 */
        int rotFwd = (projFwd * dCos - projLat * dSin) / 4096;  /* edi */

        /* 0x4d5424..0x4d543e — rotLat = (projLat*dCos + projFwd*dSin) / 4096 */
        int rotLat = (projLat * dCos + projFwd * dSin) / 4096;  /* eax, stored [esp] */

        /* 0x4d5441..0x4d5462 — third rotation: yaw (NOT negated this time) */
        unsigned int yaw = (unsigned int)player->angleYaw;
        int pSin = g_sinTable[yaw] >> 2;           /* 0x92568c — esi */
        int pCos = g_cosTable[yaw] >> 2;         /* 0x92668c — [esp+8] */

        /* 0x4d5455..0x4d5478 — worldFwd = (negProjX*pSin + rotFwd*pCos) / 4096 */
        int worldFwd = (negProjX * pSin + rotFwd * pCos) / 4096;  /* [esp+0x24] */

        /* 0x4d547c..0x4d5487 — negRotLat = -rotLat */
        int negRotLat = -rotLat;  /* [esp+0x20] */

        /* 0x4d548b..0x4d54aa — worldLat = (-rotFwd*pSin + negProjX*pCos) / 4096 */
        int worldLat = (-rotFwd * pSin + negProjX * pCos) / 4096;  /* [esp+0x28] */

        /* 0x4d54ae..0x4d54b6 — second atan2: fild negRotLat, fild worldLat, call wrapper
         * wrapper: fxch; fpatan → atan2(worldLat, negRotLat) */
        sr_double angle2 = sr_atan2((sr_double)worldLat, (sr_double)negRotLat);

        /* 0x4d54bb..0x4d54d8 — convert second atan2 to 12-bit angle */
        int angle2_12 = (int)(angle2 * (4096.0 / (2.0 * 3.14159265358979323846)));
        angle2_12 = (angle2_12 << 4) >> 4;   /* sign-extend from 28 bits */
        angle2_12 &= 0xFFF;

        /* 0x4d54e7..0x4d54f2 — targetA = 0x1000 - (angle2_12 + 0x800) */
        targetA = 0x1000 - (angle2_12 + 0x800);  /* esi */

        /* 0x4d54f4..0x4d5509 — table lookup at index (0x1000 - angle2_12) & 0xFFF */
        int tblIdx = (0x1000 - angle2_12) & 0xFFF;
        int tabSin = g_sinTable[tblIdx] >> 2;      /* 0x92568c — edx, then sar'd */
        int tabCos = g_cosTable[tblIdx] >> 2;    /* 0x92668c — [esp+8] */

        /* 0x4d5517..0x4d5537 — temp3 = (negRotLat*tabCos - worldLat*tabSin) / 4096 */
        int temp3 = (negRotLat * tabCos - worldLat * tabSin) / 4096;  /* [esp+4] */

        /* 0x4d553b..0x4d5543 — third atan2: fild temp3, fild worldFwd, call wrapper
         * wrapper: fxch; fpatan → atan2(worldFwd, temp3) */
        sr_double angle3 = sr_atan2((sr_double)worldFwd, (sr_double)temp3);

        /* 0x4d5548..0x4d555f — convert third atan2 to 12-bit angle, multiply by leftover constants */
        int angle3_12 = (int)(angle3 * (4096.0 / (2.0 * 3.14159265358979323846)));
        angle3_12 = (angle3_12 << 4) >> 4;   /* sign-extend from 28 bits */
        angle3_12 &= 0xFFF;

        /* 0x4d5564..0x4d557a — finalize targetA and targetB */
        targetA &= 0xFFF;                      /* 0x4d5564: and esi, 0xFFF */
        if (angle3_12 < 0) {
            angle3_12 += 0x1000;  /* 0x4d556a..0x4d556e (dead after &0xFFF) */
        }
        targetB = (0x1000 - angle3_12) & 0xFFF;  /* edi */
    }

    /* ================================================================
     * 0x4d5580..0x4d560f — Converge player[3] (+0x0C) toward targetA
     * ================================================================ */
    int diff = targetA - player->anglePitch; /* 0x4d5580..0x4d5582 */
    int antiStep = 0x1000 - step;            /* 0x4d5585..0x4d558c */

    if (diff > 0) { /* 0x4d5590..0x4d5592 */
        if (diff < step) {                                 /* 0x4d5594 */
            player->anglePitch = targetA; /* snap */
        }
        else if (diff < 0x800) {                               /* 0x4d55a0 */
            player->anglePitch += step; /* step forward */
        }
        else if (diff > antiStep) {                                 /* 0x4d55af */
            player->anglePitch = targetA; /* snap (close on other side) */
        }
        else {
            /* 0x4d55b8..0x4d55ca — step backward, wrap if negative */
            int val = player->anglePitch - step;
            player->anglePitch = val;
            if (val < 0) {
                player->anglePitch = val + 0x1000;
            }
        }
    }
    else if (diff < 0) { /* 0x4d55cc */
        if (diff > -step) {                                 /* 0x4d55ce..0x4d55d4 */
            player->anglePitch = targetA; /* snap */
        }
        else if (diff > -0x800) {                               /* 0x4d55db */
            player->anglePitch -= step; /* step backward */
        }
        else if (diff < -antiStep) {                                 /* 0x4d55e7..0x4d55ef */
            player->anglePitch = targetA; /* snap (close on other side) */
        }
        else {
            /* 0x4d55f6..0x4d5606 — step forward, wrap if > 0xFFF */
            int val = player->anglePitch + step;
            player->anglePitch = val;
            if (val > 0xFFF) {
                player->anglePitch = val - 0x1000;
            }
        }
    }
    /* diff == 0: no change */

    /* ================================================================
     * 0x4d560f..0x4d56b6 — Converge player[5] (+0x14) toward targetB
     * ================================================================ */
    diff = targetB - player->angleRoll; /* 0x4d560f..0x4d5611 */
    antiStep = 0x1000 - step;           /* 0x4d5614..0x4d5619 */

    if (diff > 0) { /* 0x4d561b..0x4d561d */
        if (diff < step) { /* 0x4d561f */
            player->angleRoll = targetB;
        }
        else if (diff < 0x800) { /* 0x4d562b */
            player->angleRoll += step;
        }
        else if (diff > antiStep) { /* 0x4d563a */
            player->angleRoll = targetB;
        }
        else {
            /* 0x4d5646..0x4d5658 */
            int val = player->angleRoll - step;
            player->angleRoll = val;
            if (val < 0) {
                player->angleRoll = val + 0x1000;
            }
        }
    }
    else if (diff < 0) { /* 0x4d565a */
        if (diff > -step) { /* 0x4d565c..0x4d5662 */
            player->angleRoll = targetB;
        }
        else if (diff > -0x800) { /* 0x4d5670 */
            player->angleRoll -= step;
        }
        else if (diff < -antiStep) { /* 0x4d5683..0x4d5687 */
            player->angleRoll = targetB;
        }
        else {
            /* 0x4d5695..0x4d56a5 */
            int val = player->angleRoll + step;
            player->angleRoll = val;
            if (val > 0xFFF) {
                player->angleRoll = val - 0x1000;
            }
        }
    }
    /* diff == 0: no change */
}

/**
 * FUN_004d6bf8 — 36 bytes — Apply grounded Y position.
 * When grounded: zero vertical velocity, set Y = ground height.
 * Also clamp Y to non-positive (can't go above ground plane).
 */
static void ApplyGroundedY(Player *player)
{
    if (player->groundedFlag != 0) {
        player->velY = 0;    /* zero vertical velocity */
        player->posY = player->groundHeight;           /* Y = stored ground height */
    }
    if (player->posY > 0) {
        player->posY = 0;
    }
}

/**
 * UpdatePlayerMovement — 0x004d8ab8 — 172 bytes
 * Player pointer from EAX.
 */
void UpdatePlayerMovement(Player *player)
{
    if (player == NULL) {
        return;
    }

    CollectibleCollision(player);

    /* Normal (not on a loop) */
    if (player->loopMode == 0) {

        if (player->_unk_0x1DC == 0) {
            WallCollision(player);
        }

        GroundCollision(player);

        if (player->loopMode == 0) {
            SurfaceNormalPhysics(player);

            if (player->_unk_0x78 == 0) {
                SlopeMovement(player);
            }

            ApplyGroundedY(player);

            PositionUpdate(player);
        }
    }
    /* On a loop surface (loopMode / player byte 0xA0 != 0) */
    else {        
        UpdateLoopMovement(player);

        if (player->loopMode == 0) {
            /* Exited loop this frame — sample terrain at player XZ */
            int height = SampleTerrainGrid(player->posX, player->posZ, 7); /* EBX=7 in binary */
            player->groundHeight = height;  /* store as ground height */
            ApplyGroundedY(player);
        }
        else {
            /* Still on loop */
            UpdateLoopOrientation(player);                       /* FUN_004d62f8 */
            player->groundHeight = player->posY;  /* ground height = current Y */
        }
    }

    /* Timer countdown at player+0x42 (byte offset) */
    if (player->_unk_0x42 != 0) {
        player->_unk_0x42 -= 1;
    }
}
