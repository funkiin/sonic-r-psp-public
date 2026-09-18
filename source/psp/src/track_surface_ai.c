/**
 * track_surface_ai.c - TrackSurfaceAI + AdvanceTrackSegment + RewindTrackSegment
 *
 * These handle: AI pathfinding, ground collision, surface detection,
 * sector tracking, waypoint navigation, rubber banding.
 *
 * in_EAX = player struct pointer (Watcom fastcall).
 */

#include <math.h>
#include <stdio.h>
#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "player_struct.h"

#ifdef SONICR_DC
#include <float.h>
#endif

/* Segment offset table: 8 bytes per segment [int waypointCount, int baseOffset] */
#define SEG_COUNT(seg) (*(int *)((char *)g_segmentOffsetTable + (seg) * 8))
#define SEG_BASE(seg)  (*(int *)((char *)g_segmentOffsetTable + 4 + (seg) * 8))

/* Waypoint data: 16 bytes per waypoint. */
#define WP_ADDR(seg, wp) ((char *)g_waypointDataBase + (SEG_BASE(seg) + (wp)) * 16)

/* Forward declarations */
void ResetPlayerToWaypoint(Player *player);

/* Globals referenced by TrackSurfaceAI */
extern void *g_charJumpPowerTable;   /* per-character jump power */
extern unsigned short g_randomRingBuffer[];  /* 0x0092498C - canonical in globals_extra.c */
#define g_randTable2 ((unsigned char *)g_randomRingBuffer + 0x800)  /* 0x0092518C */
extern short g_rampSpeedTableA[];    /* 0x00540078 - BSS, zero in retail too */
extern unsigned short g_aiSpeedFactor;   /* 0x0054004A - rubber-band speed factor */

/* Per-character boost-pad speed tables (binary reads dword at base-2 + >>16,
 * so effective base = quoted address, charId-indexed shorts).
 * B: Tails=30, Knuckles=34, Metal Sonic=34 (2026-07-13: was off-by-one-short).
 * C: Tails=90, Knuckles=69. */
static const short s_rampSpeedTableB[10] = {
    0, 30, 34, 0, 0, 0, 34, 0, 0, 0   /* 0x4FA158 */
};
static const short s_rampSpeedTableC[10] = {
    0, 90, 69, 0, 0, 0, 0, 0, 0, 0    /* 0x4FA178 */
};

/* Forward declarations */
static int AdvanceTrackSegment(Player *player, int *segPtr, int *wpPtr);
static int RewindTrackSegment(Player *player, int *segPtr, int *wpPtr);

/**
 * compute_segment_distance - 0x004E08CA (29 bytes)
 *
 * This computes 2D Euclidean distance: isqrt(dx^2 + dz^2).
 */
static int compute_segment_distance(int dx, int dz)
{
    return (int)sr_sqrtf((float)((long long)dx * dx + (long long)dz * dz));
}

/**
 * TrackSurfaceAI - 0x0041E6DC - 4505 bytes
 * Main AI + surface detection function. Called once per player per frame.
 */
void TrackSurfaceAI(Player *player)
{
    if (player == NULL) {
        return;
    }
    /* binary 0x41e6e9 - when finishState != 0, zero these two fields and
     * RETURN (0x41e708: jmp 0x41f86b = function epilogue). Finished cars run none
     * of the steering/surface/RNG code - the finishState recheck before the Phase-8
     * switch (0x41eab0) is dead defensive source code, NOT evidence of fall-through.
     * Audit 2026-07-12: the prior fall-through advanced the shared item RNG +1/frame
     * per finished car, skewing the stream vs retail after the first finisher. */
    if (player->finishState != 0) {
        player->_unk_0x1D6 = 0;
        player->_unk_0x1DC = 0;
        return;
    }

    /* Animation wobble counter */
    unsigned short wobble = player->wobbleCounter;
    player->wobbleCounter = wobble + 1;
    player->aiWobbleSin = (short)g_sinTable[(wobble & 0xFF) * 16];

    /* AI steering convergence - binary 0x41e739-0x41e7b7.
     * The binary RE-READS aiSteerCurrent/aiSteerTarget from the struct after
     * each step (not cached locals), and the == branch converges aiSteerTarget
     * toward 0x7F by comparing aiSteerTarget itself (word @+0x1cc, read as
     * [+0x1ca]>>16) to 0x7F - NOT directionFlip. The prior translation cached
     * the values and compared directionFlip (+0x1ce, a 0/1 flag), so it always
     * did +1 and settled aiSteerCurrent at 0x80 instead of 0x7F - a 1-unit error
     * that propagated through factor(_unk_0x1D2) → waypoint interp → targetYaw. */
    short steerTarget = player->aiSteerTarget;                 /* 0x41e739 */
    if (player->aiSteerCurrent > steerTarget) {                /* 0x41e747: cmp; jle */
        player->aiSteerCurrent = player->aiSteerCurrent - 1;   /* 0x41e74e: dec */
    }
    if (player->aiSteerCurrent < player->aiSteerTarget) {      /* 0x41e764: reload; cmp; jge */
        player->aiSteerCurrent = player->aiSteerCurrent + 1;   /* 0x41e76b: inc */
    }
    if (player->aiSteerTarget == player->aiSteerCurrent) {     /* 0x41e77a: reload both */
        if (player->aiSteerTarget > 0x7F) {                    /* 0x41e78c: cmp aiSteerTarget,0x7f */
            player->aiSteerTarget = player->aiSteerTarget - 1;
        }
        if (player->aiSteerTarget < 0x7F) {                    /* 0x41e7a4: reload; cmp */
            player->aiSteerTarget = player->aiSteerTarget + 1;
        }
        player->aiSteerCurrent = player->aiSteerTarget;        /* 0x41e7b0 */
    }

    /* Podium mode override / AI avoidance */
    if (g_raceType == RACE_SPECIAL) {
        player->aiSteerTarget = 0x80;
    }
    else {
        /* Check nearby players for avoidance.
         * g_playerPtrTable is an array of 5 void* pointers (one per player).
         * Original used i*4 stride (32-bit pointers); we use sizeof(void*). */
        if (g_playerPtrTable != NULL) {
            for (int i = 1; i < 5; i++) {
                Player *other = ((Player **)g_playerPtrTable)[i];
                if (other == NULL || player == other) {
                    continue;
                }
                if (player->aiSegmentIdx != other->aiSegmentIdx) {
                    continue;
                }
                int delta = other->waypointIdx - player->waypointIdx;
                if (delta > 0 && delta < 5) {
                    player->aiSteerTarget = (short)(g_avoidanceBias + 0x80);
                    g_avoidanceCounter--;
                    if (g_avoidanceCounter == -1) {
                        g_avoidanceBias = -g_avoidanceBias;
                        g_avoidanceCounter = 8;
                    }
                    break;
                }
            }
        }
    }

    /* Compute AI speed target */
    player->_unk_0x1D2 = (short)(player->aiWobbleSin >> 9) + player->aiSteerCurrent;
    if (0xC0 < player->_unk_0x1D2) {
        player->_unk_0x1D2 = 0xC0;
    }
    if (player->_unk_0x1D2 < 0x40) {
        player->_unk_0x1D2 = 0x40;
    }

    /* waypoint-proximity boost - binary 0x41E8AB-0x41E99E.
     * When within sqrt(0x800) of the current waypoint target (16-bit-scaled),
     * the waypoint flag byte selects a per-character speed table; bits
     * cumulative, later bits overwrite. Factor is the rubber-band word 0x54004A.
     *
     * The flags are the HIGH byte of the short at +0xE (its low byte is the
     * alt-waypoint link index — see the `& 0xFF` in AdvanceTrackSegment). The
     * binary reads it as byte +0xF, but that only names the same byte on LE:
     * section 1 is stored pair-reversed relative to the file, and the BE build
     * reads the file straight, mirroring byte positions within each short.
     * Reading the short and taking >> 8 lands on the flag byte on both. */
    if (g_segmentOffsetTable != NULL && g_waypointDataBase != NULL) {
        int dxP = (player->posX - (player->waypointX << 12)) >> 16;
        int dzP = (player->posZ - (player->waypointZ << 12)) >> 16;
        if (dxP * dxP + dzP * dzP < 0x800) {
            unsigned char wpFlags = (unsigned char)
                (*(unsigned short *)(WP_ADDR(player->aiSegmentIdx, player->waypointIdx) + 0xE) >> 8);
            int bCharId = player->charId;
            if (bCharId >= 0 && bCharId < CHAR_COUNT) {
                if (wpFlags & 1) {
                    player->_unk_0x1D8 = g_rampSpeedTableA[bCharId];
                }
                if (wpFlags & 2) {
                    player->_unk_0x1D8 = s_rampSpeedTableB[bCharId];
                }
                if (wpFlags & 4) {
                    player->_unk_0x1D8 = s_rampSpeedTableC[bCharId];
                }
            }
            if (wpFlags & 7) {
                int bFactor = g_aiSpeedFactor;   /* 0x41e971: zero-ext word */
                player->_unk_0x1D8 =
                    (short)((player->_unk_0x1D8 * (0x100 - bFactor)) / 256);
                player->physicsFlags = 0x01;   /* 0x41e98e: word 0x0001 at 0x1DA */
                player->physicsFlags2 = 0x00;
            }
        }
    }

    /* Grid cell lookup */
    int gridX = (player->posX >> 12) - g_aiGridOriginX;
    int gridZ = (player->posZ >> 12) - g_aiGridOriginZ;
    int cellIdx = 0;
    if (g_aiGridCellWidth != 0 && g_aiGridCellHeight != 0) {
        cellIdx = (gridZ / g_aiGridCellHeight) * 0x20 + (gridX / g_aiGridCellWidth);
    }

    /* Surface type lookup */
    unsigned int surfaceRef;
    unsigned char surfaceType;
    if (player->overSurface == 1) {
        /* On a ramp surface */
        if (g_trackSurfaceData != NULL) {
            surfaceRef = ((TerSurface *)g_trackSurfaceData + player->hitSurfaceIdx)->faceBase + player->hitEdgeIdx;
            surfaceType = *(unsigned char *)((char *)(intptr_t)g_aiGridSurface + surfaceRef);
        }
        else {
            surfaceRef = 0;
            surfaceType = 0;
        }
    }
    else {
        surfaceRef = cellIdx + 0x8000;
        if (g_aiGridGround != NULL) {
            surfaceType = *(unsigned char *)((char *)g_aiGridGround + cellIdx);
        }
        else {
            surfaceType = 0;
        }
    }

    /* wall-response steer + SHARED-RNG advance - binary 0x41ea19-0x41eaaa.
     * Runs per AI car every frame. On a NEW surface (lastSurfaceRef != surfaceRef, zero-
     * extended vs the full surfaceRef, per 0x41ea19 xor;mov ax / cmp eax,esi) OR on
     * normal ground (surfaceType == 0), pick a random ±1 steer nudge and ADVANCE
     * g_randomRingIdx (0x41ea81-0x41eaa1). Otherwise ramp wallSteerDir toward the wall
     * by ±8, clamping to 0x4000 at ±0x1000 (0x41ea34-0x41ea7f; the condition reads
     * [0x1b4]>>16 = the +0x1B6 word = wallSteerDir itself).
     * This block advances the shared item RNG once per AI car per frame (the missing
     * +4/frame). It is faithful to the binary; it also feeds wall-steering. */
    if ((unsigned short)player->lastSurfaceRef != surfaceRef || surfaceType == 0) {
        player->wallSteerDir = (short)((g_randTable2[g_randomRingIdx] & 2) - 1);  /* 0x41ea86-0x41ea9a */
        g_randomRingIdx = (g_randomRingIdx + 1) & 0xFF;                            /* 0x41ea93/0x41eaa1 */
    }
    else if (player->wallSteerDir > 0) {                                         /* 0x41ea37: test; jle */
        player->wallSteerDir += 8;                                                /* 0x41ea3b */
        if (player->wallSteerDir >= 0x1000) {
            player->wallSteerDir = 0x4000;         /* 0x41ea4c/0x41ea53 */
        }
    }
    else {
        player->wallSteerDir -= 8;                                                /* 0x41ea5e */
        if (player->wallSteerDir <= -0x1000) {
            player->wallSteerDir = 0x4000;        /* 0x41ea6f/0x41ea76 */
        }
    }

    /* Binary 0x41eab0 (xor dl,dl): when finishState != 0, force surfaceType to 0 before
     * the switch so a braking/finished car sees flat ground - no boost pad, no ramp. */
    if (player->finishState != 0) {
        surfaceType = 0;
    }

    /* Surface type switch - 16 cases based on upper nibble.
     * Handles: normal ground, ramps, boosters, walls, sector transitions, respawns. */
    unsigned int cellValue = (unsigned int)surfaceType;
    unsigned char cellByte = (unsigned char)cellValue;
    player->ring.wallPresence = 0;  /* 0x164 - clear wall flag */

    switch ((cellValue & 0xFF) >> 4) {
        case 0: {
            /* Normal ground / ramps / special surfaces */
            if ((cellValue & 0xFF) == 8) {
                /* Amy-specific boost pad */
                cellValue = 0;
                if ((unsigned short)player->lastSurfaceRef != surfaceRef &&
                    0xC0 < g_randTable2[g_randomRingIdx] &&
                    player->charId == CHAR_AMY &&
                    player->directionFlip == 0)
                {
                    player->physicsFlags = 0x00;
                    player->physicsFlags2 = 0x08;
                }
                g_randomRingIdx = (g_randomRingIdx + 1) & 0xFF;
            }
            if ((cellValue & 8) != 0) {
                if ((unsigned short)player->lastSurfaceRef == surfaceRef ||
                    0x20 < g_randTable2[g_randomRingIdx])
                {
                    g_randomRingIdx = (g_randomRingIdx + 1) & 0xFF;
                    break;
                }
                g_randomRingIdx = (g_randomRingIdx + 1) & 0xFF;
            }
            /* Ramp sub-type based on lower 2 bits, direction from bit 2 */
            if (((cellValue & 0xFF) >> 2 & 1) == (unsigned)player->directionFlip) {
                unsigned char rampType = cellByte & 3;

                /* Speed tables hoisted to file scope (shared with the Phase-5.5
                * waypoint-proximity boost block). Computation everywhere:
                * speed * (0x100 - factor) / 256. */
                if (rampType == 0) {
                    /* Normal ground - do nothing (binary falls through) */
                }
                else if (rampType == 1) {
                    /* Ramp type 1 - boost from BSS table (0x540078) */
                    extern short g_rampSpeedTableA[];            /* 0x00540078 */
                    int charId = player->charId;
                    int speed = (charId >= 0 && charId < CHAR_COUNT) ? g_rampSpeedTableA[charId] : 0;
                    int factor = player->speedField;
                    player->physicsFlags = 0x01;
                    player->physicsFlags2 = 0x00;
                    player->_unk_0x1D8 = (short)((speed * (0x100 - factor)) / 256);
                }
                else if (rampType == 2) {
                    /* Ramp type 2 - boost from ROM table (0x4FA158) */
                    int charId = player->charId;
                    int speed = (charId >= 0 && charId < CHAR_COUNT) ? s_rampSpeedTableB[charId] : 0;
                    int factor = player->speedField;
                    player->physicsFlags = 0x01;
                    player->physicsFlags2 = 0x00;
                    player->_unk_0x1D8 = (short)((speed * (0x100 - factor)) / 256);
                }
                else if (rampType == 3) {
                    /* Ramp type 3 - same as type 2 + track-specific handling */
                    int charId = player->charId;
                    int speed = (charId >= 0 && charId < CHAR_COUNT) ? s_rampSpeedTableB[charId] : 0;
                    int factor = player->speedField;
                    player->physicsFlags = 0x01;
                    player->physicsFlags2 = 0x00;
                    player->_unk_0x1D8 = (short)((speed * (0x100 - factor)) / 256);
                    /* Track dispatch un-crosswired 2026-07-13: binary 0x41ecc5
                    * `cmp trackId,3` = FACTORY (binary convention 3=Factory,
                    * 4=Ruin) - vertical launch pad + the previously-untranslated
                    * else (tableC boost, 0x41ed25). Binary 0x41ed66 `cmp 4` =
                    * RUIN - horizontal launch pad. Was swapped. */
                    if (g_trackId == TRACK_REACTIVE_FACTORY) {
                        if (player->aiSegmentIdx == 0) {
                            /* Factory vertical launch pad */
                            player->airTimer = 1;
                            player->groundedFlag = 0;
                            player->physicsFlags = 0x00;
                            player->physicsFlags2 = 0x00;
                            player->velY = -0x3C000;
                            player->_unk_0x1D8 = 0;
                            player->velX = (player->directionFlip == 0) ? 0x32906 : -0x32906;
                            player->velZ = -0xC72;
                        }
                        else {
                            /* 0x41ed25: boost from tableC, OR-ing the ramp flag
                            * (byte OR, unlike the other handlers' word write) */
                            int speedC = (charId >= 0 && charId < CHAR_COUNT) ? s_rampSpeedTableC[charId] : 0;
                            player->physicsFlags |= 0x01;
                            player->_unk_0x1D8 = (short)((speedC * (0x100 - factor)) / 256);
                        }
                    }
                    if (g_trackId == TRACK_REGAL_RUIN) {
                        /* Ruin horizontal launch pad */
                        player->airTimer = 1;
                        player->groundedFlag = 0;
                        player->physicsFlags = 0x00;
                        player->physicsFlags2 = 0x10;
                        player->velY = -0x14000;
                        player->_unk_0x1D8 = 0;
                        player->velX = (player->directionFlip == 0) ? 0x46186 : -0x46186;
                        player->velZ = 0;
                    }
                }
            }
            break;
        }
        case 1:
            /* Wall - direction 0 only */
            if (player->airTimer == 0 && player->directionFlip == 0) {
                player->_unk_0x1DC = 0;
                player->_unk_0x1D6 = 0;
                player->ring.wallPresence = 6;  /* wall flag */
                player->targetYaw = (unsigned short)(cellByte & 0xF) << 8;
            }
            break;
        case 2:
            /* Wall - direction 1 only */
            if (player->airTimer == 0 && player->directionFlip == 1) {
                player->ring.wallPresence = 6;
                player->_unk_0x1DC = 0;
                player->_unk_0x1D6 = 0;
                player->targetYaw = ((unsigned char)(cellByte + 8) & 0x0F) << 8;
            }
            break;
        case 3:
            /* Wall - both directions */
            if (player->airTimer == 0) {
                player->ring.wallPresence = 6;
                player->_unk_0x1DC = 0;
                player->_unk_0x1D6 = 0;
                if (player->directionFlip == 0) {
                    player->targetYaw = (unsigned short)(cellByte & 0xF) << 8;
                }
                else {
                    player->targetYaw = ((unsigned char)(cellByte + 8) & 0x0F) << 8;
                }
            }
            break;
        case 4:
            /* Sector transition (with flag set) */
            if (player->ring._unk_0x166 == 0) {
                player->ring._unk_0x166 = 1;
                player->aiSegmentIdx = (unsigned int)(cellByte & 0xF);
            }
            /* fall through to case 5 */
        case 5:
            /* Checkpoint - save position */
            if ((unsigned short)player->lastSurfaceRef != surfaceRef) {
                player->respawnX = player->posX;
                player->respawnY = player->posY;
                player->respawnZ = player->posZ;
                player->ring._unk_0x160 = 0;
                player->ring.respawnSegment = (short)(cellByte & 0xF);
            }
            break;
        case 6:
            /* Wall - direction variant 1 */
            if (player->airTimer == 0) {
                player->ring.wallPresence = 6;
                player->_unk_0x1DC = 0;
                player->_unk_0x1D6 = 0;
                if (player->directionFlip == 0) {
                    player->targetYaw = (unsigned short)(cellByte & 0xF) << 8;
                }
                else {
                    player->targetYaw = ((unsigned char)(cellByte - 4) & 0x0F) << 8;
                }
            }
            break;
        case 7:
            /* Wall - direction variant 2 */
            if (player->airTimer == 0) {
                player->_unk_0x1DC = 0;
                player->_unk_0x1D6 = 0;
                player->ring.wallPresence = 6;
                if (player->directionFlip == 0) {
                    player->targetYaw = (unsigned short)(cellByte & 0xF) << 8;
                }
                else {
                    player->targetYaw = ((unsigned char)(cellByte + 4) & 0x0F) << 8;
                }
            }
            break;
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
        case 13:
        case 14:
            {
                /* Respawn/reset - put player back on waypoint */
                int wpIdx;
                if (player->directionFlip == 0) {
                    wpIdx = (unsigned int)(cellByte & 0x7F) * 2;
                }
                else {
                    wpIdx = (unsigned int)(cellByte & 0x7F) * 2 - 6;
                }
                player->waypointIdx = wpIdx;
                ResetPlayerToWaypoint(player);              /* 0x41f056: call 0x421100 */
                player->steerState = 0;                     /* 0x41f05b: word at 0x1D0 */
                player->modelCharId = 0;                    /* 0x41f064: word at 0x1DE */
                player->aiAccumDist = 200;                  /* 0x41f06d: dword at 0x120 = 0xC8 */
                player->ring.head = 0;                      /* 0x41f077: dword at 0x158 */
                player->ring.tail = 0;                      /* 0x41f081: dword at 0x15C */
                player->ring.segId[0] = 0xFF;               /* 0x41f08b: byte at 0x188 */
                player->aiAccel = (unsigned short)player->speedModifier; /* 0x41f092: zero-ext word at 0x1BC → dword at 0x128 */
                break;
            }
        case 15:
            /* Combined wall + sector */
            if (player->airTimer == 0) {
                player->ring.wallPresence = 6;
                player->_unk_0x1DC = 0;
                player->_unk_0x1D6 = 0;
                unsigned short wallDir;
                if (player->directionFlip == 0) {
                    wallDir = (unsigned short)(cellByte & 0xF);
                }
                else {
                    wallDir = (unsigned char)(cellByte + 8) & 0x0F;
                }
                player->targetYaw = wallDir << 8;
                if ((unsigned short)player->lastSurfaceRef != surfaceRef) {
                    player->ring._unk_0x160 = 0;
                    player->respawnX = player->posX;
                    player->respawnY = player->posY;
                    player->respawnZ = player->posZ;
                    player->ring.respawnSegment = (short)player->aiSegmentIdx;
                }
            }
            break;
    }

    /* Store surface reference for next-frame comparison */
    player->lastSurfaceRef = (short)surfaceRef;

    /* Race finish override */
    if (player->finishState == 2) {
        player->ring.wallPresence = 0;
    }

    /* Wall response - steering adjustment based on surface direction */
    if (player->ring.wallPresence != 0) {
        int steerDir = player->wallSteerDir;
        if (steerDir == 0x4000) {
            player->_unk_0x1DC = 2;
        }
        else {
            if (steerDir < 0xF1) {
                if (-0xF1 < steerDir) {
                    goto track_progress_update;
                }
                player->targetYaw = player->targetYaw + player->wallSteerDir + 0xF0;
            }
            else {
                player->targetYaw = player->targetYaw + player->wallSteerDir - 0xF0;
            }
            player->targetYaw &= 0x0FFF;
        }
    }

track_progress_update:
    /* Track progress update */
    player->modelCharId = (unsigned short)player->modelCharId + (short)((unsigned int)player->aiSpeed >> 8);

    if (player->steerState == 0) {
        player->lapCheckX = player->posX >> 12;
        player->lapCheckZ = player->posZ >> 12;
    }

    /* Distance calculations for lap detection */
    int dxWp = (player->posX >> 12) - player->waypointX;
    int dzWp = (player->posZ >> 12) - player->waypointZ;
    int distSq = dxWp * dxWp + dzWp * dzWp;
    int dzTarget = player->lapCheckZ - player->waypointZ;

    if ((short)player->_unk_0x1DC != 0) {
        player->_unk_0x1DC = player->_unk_0x1DC - 1;
    }

    /* Shortcut timer management */
    if ((short)player->_unk_0x1D6 != 0) {
        player->_unk_0x1D6 -= 1;
        if (player->distanceAccum + player->aiAccumDist < 0xC3501) {
            if ((short)player->_unk_0x1D6 == 0) {
                player->steerState = 0;
                player->aiAccel = (unsigned int)player->speedModifier;
                player->ring.tail = player->ring.head;
                player->_unk_0x1DC = 0x78;
                player->aiSegmentIdx = (unsigned int)player->ring.segId[player->ring.head & 0xF];
                int wpIdx2;
                if (player->directionFlip == 0) {
                    wpIdx2 = (unsigned int)player->ring.wpCount[player->ring.tail & 0xF] +
                             (int)player->ring.wpBase[player->ring.tail & 0xF] - 1;
                }
                else {
                    wpIdx2 = (int)player->ring.wpBase[player->ring.tail & 0xF];
                }
                player->waypointIdx = wpIdx2;
                player->distanceAccum = 0;
                player->lapCounter = 0;
                player->modelCharId = 0;
                player->aiAccumDist = 0;
            }
        }
        else {
            player->_unk_0x1D6 = 0;
        }
    }

    /* Lap crossing detection */
    {
        int targetDistSq = (player->lapCheckX - player->waypointX) * (player->lapCheckX - player->waypointX) + dzTarget * dzTarget;
        int threshold = (int)((unsigned int)(1 < player->steerState) * 0x10000 + 200);
        if (threshold < targetDistSq - distSq && distSq < 120000) {
            player->lapCounter = 0;
            player->modelCharId = 0;
            player->steerState = 0;
            player->aiAccel = (unsigned int)player->speedModifier;
        }
    }

    /* Lap counter increment - applies negative acceleration.
     * This creates a spline speed oscillation. */
    if ((unsigned int)player->speedModifier == (unsigned int)player->aiAccel &&
        player->physicsConst0x1B4 < (short)player->modelCharId)
    {
        if (player->steerState == 0) {
            player->steerState = player->steerState + 1;
            if ((short)player->_unk_0x1D6 == 0) {
                player->_unk_0x1D6 = 0x96;
                player->distanceAccum = 0;
            }
        }
        if (player->steerState == 2) {
            player->steerState = player->steerState + 1;
        }
        player->aiAccel = (unsigned int)(unsigned short)player->_unk_0x1BE;
        player->aiAccel = -(unsigned int)(unsigned short)player->_unk_0x1BE;
    }

    /* Wrong-way detection */
    if ((short)player->modelCharId < -0x1C1) {
        if (player->steerState == 1) {
            player->steerState = 2;
            player->aiAccel = (unsigned int)player->speedModifier;
        }
        if ((short)player->modelCharId < -0x2ED) {
            player->steerState = 0;
            player->aiAccel = (unsigned int)player->speedModifier;
        }
    }

    /* Speed target clamping */
    {
        int speedTarget = player->aiSpeed + player->aiAccel;
        int maxSpeed = player->maxSpeedCap;
        player->aiSpeed = speedTarget;
        if (maxSpeed < speedTarget) {
            player->aiSpeed = maxSpeed;
        }
        else if (-speedTarget != maxSpeed && speedTarget <= -maxSpeed) {
            player->aiSpeed = -maxSpeed;
        }
    }


    /* Track progress accumulation */
    {
        int rampState = player->_unk_0x78;
        player->aiAccumDist = player->aiAccumDist + player->aiSpeed;

        if (rampState > 2) {
            if (rampState < 4) {
                player->ring.head = 0;
                player->ring.tail = 0;
                player->ring.segId[0] = 0xFF;
                player->aiAccumDist = 0;
                player->aiAccel = (unsigned int)player->speedModifier;
            }
            /* Launch progress correction - binary 0x41F52A-0x41F579.
             * During ramp-launch states (_unk_0x78 > 2) the frame's aiSpeed
             * is replaced by the gate vector length
             * (dwords at g_gateWaypointPtr[state-3]/[state-2],
             * i.e. ptr+state*4-0xC/-0x8), <<8, so track progress follows the
             * launch arc instead of ground speed. 32-bit sum like binary. */
            player->aiAccumDist -= player->aiSpeed;
            if (g_gateWaypointPtr != NULL) {
                int *gw = (int *)g_gateWaypointPtr;
                int gdx = gw[rampState - 3];
                int gdz = gw[rampState - 2];
                int gDist = (int)sr_sqrtf((float)(gdx * gdx + gdz * gdz));
                player->aiAccumDist += gDist << 8;
            }
        }
    }

    /* Boost pad override */
    if (player->loopMode != 0) {
        player->aiAccumDist = player->aiAccumDist - player->aiSpeed;
        player->aiAccumDist = player->aiAccumDist + ((int)(unsigned int)player->speedModifier >> 4);
    }

    /* Waypoint navigation loop.
     * Advances along the track spline segment by segment until the
     * player's accumulated distance (player[0x48]) fits within one segment.
     * Sets player[0x4E] and player[0x50] - the expected XZ position on track.
     * This is the core track-following logic for ground collision and AI. */
    if (g_segmentOffsetTable != NULL && g_waypointDataBase != NULL) {
        int bRewound = 0;
        int bKeepGoing = 1;
        int lastDeltaX = 0;
        int lastDeltaZ = 0;
        int lastSegDist = 1;
        int _loopCount = 0;

        do {
            /* Store segment waypoint count */
            player->_unk_0x154 = SEG_COUNT(player->aiSegmentIdx);

            /* Save current state before advance.
             * player->aiSegmentIdx (0x11C), player->waypointIdx (0x1A8). */
            int savedSeg  = player->aiSegmentIdx;  /* current segment - [ebp-0x28] in binary */
            int savedTail = player->ring.tail;     /* ring buffer tail (byte 0x15C) */
            int savedWP   = player->waypointIdx;   /* current waypoint index - [ebp-0x2c] in binary */

            /* Advance to next waypoint - use LOCAL copies, not player fields.
             * Binary 0x41f5f6-0x41f60a: saves seg/wp to [ebp-0x28]/[ebp-0x2c],
             * passes those local pointers to AdvanceTrackSegment. The player
             * struct fields (player+0x11C, player+0x1A8) are NOT modified here.
             * Only the second advance call (crossing segment boundary) writes
             * to the actual player fields. */
            int advSeg = savedSeg;
            int advWP  = savedWP;
            AdvanceTrackSegment(player, &advSeg, &advWP);

            /* Restore ring buffer tail (AdvanceTrackSegment modified it during search) */
            player->ring.tail = savedTail;

            /* Binary 0x41f615-0x41f64b:
             * ORIGINAL waypoint from player fields (unchanged) -> ebx
             * ADVANCED waypoint from local copies -> esi
             * player->waypointX/waypointZ are set to ORIGINAL position.
             * Delta points from original toward advanced (forward direction). */
            int _oIdx = SEG_BASE(savedSeg) + savedWP;
            int _aIdx = SEG_BASE(advSeg) + advWP;
            if (_oIdx < 0 || _aIdx < 0 || _oIdx > 0x3FF || _aIdx > 0x3FF) {
                break;
            }
            /* Original (current) waypoint - where player IS */
            int *curWP = (int *)((char *)g_waypointDataBase + _oIdx * 16);
            /* Advanced (next) waypoint - where player is heading */
            int *advWPdata = (int *)((char *)g_waypointDataBase + _aIdx * 16);

            /* Interpolation factor: player->_unk_0x1D2
             * (byte offset 0x1D0, read as *(int*)(player+0x1D0) >> 16) */
            int factor = player->_unk_0x1D2;

            /* Interpolate ORIGINAL (current) waypoint -> waypointX/waypointZ
             * Binary 0x41f64d-0x41f6ba: reads from ebx (original waypoint),
             * stores interpolated position into player+0x138/0x13C/0x140.
             * Waypoint layout (16 bytes, little-endian):
             *   byte 0-1: short X    = (short)*wp
             *   byte 2-3: short Z    = *wp >> 16
             *   byte 4-5: short X2   = *(int*)(wp+2) >> 16
             *   byte 6-7: short Z2   = wp[1] >> 16
             *   byte 8-9: short Y    = *(int*)(wp+6) >> 16  */
            short cX = *(short *)((char *)curWP + 0);
            short cX2 = *(short *)((char *)curWP + 4);
            short cZ = *(short *)((char *)curWP + 2);
            short cZ2 = *(short *)((char *)curWP + 6);
            short cY = *(short *)((char *)curWP + 8);

            int cXinterp = (int)cX + ((int)(cX2 - cX) * factor) / 256;
            int cZinterp = ((int)(cZ2 - cZ) * factor) / 256 + (int)cZ;

            player->waypointX = cXinterp;    /* expected X - current waypoint position */
            player->_unk_0x13C = (int)cY;    /* expected Y (height). Binary reads this back at
                                              * 0x41f72a only to compute dY = advY - waypointY, which
                                              * it stores to a stack local that is never read again
                                              * (dead store) - no functional effect, so no read in our code. */
            player->waypointZ = -cZinterp;   /* expected Z - binary negates (0x41f6b8: neg edx).
                                              * Waypoint data stores Z with opposite sign to world coords;
                                              * negation converts to world Z convention for steering. */

            /* Interpolate ADVANCED (next) waypoint -> for delta */
            short aX = *(short *)((char *)advWPdata + 0);
            short aX2 = *(short *)((char *)advWPdata + 4);
            short aZ = *(short *)((char *)advWPdata + 2);
            short aZ2 = *(short *)((char *)advWPdata + 6);

            int aXinterp = (int)aX + ((int)(aX2 - aX) * factor) / 256;
            int aZinterp = ((int)(aZ2 - aZ) * factor) / 256 + (int)aZ;

            /* Delta: binary 0x41f728-0x41f73d.
             * X: aXinterp - cXinterp (both un-negated).
             * Z: (-aZinterp) - waypointZ where waypointZ is already -cZinterp.
             *    = (-aZinterp) - (-cZinterp) = cZinterp - aZinterp.
             * This flips Z sign to match world coordinate convention. */
            lastDeltaX = aXinterp - player->waypointX;   /* advX - origX */
            lastDeltaZ = -aZinterp - player->waypointZ;  /* (-advZ) - (-origZ) */

            /* Compute segment step distance via isqrt(dx^2 + dz^2)
             * (atan2_compute @ 0x004E08CA - rounds x87 FPU sqrt result) */
            lastSegDist = compute_segment_distance(lastDeltaX, lastDeltaZ);
            lastSegDist = (lastSegDist << 8) >> 8;    /* sign-extend 24 bits */
            if (lastSegDist == 0) {
                lastSegDist = 1;    /* prevent division by zero */
            }

            /* Update loop state based on accumulated distance */
            if (bRewound) {
                player->aiAccumDist += lastSegDist * 0x100;
                bRewound = 0;
            }

            if (player->aiAccumDist < 0) {
                /* Went backward: rewind one segment step */
                player->lapCounter--;
                RewindTrackSegment(player, (int *)&player->aiSegmentIdx, (int *)&player->waypointIdx);
                bRewound = 1;
            }
            else if (player->aiAccumDist >> 8 < lastSegDist) {
                /* Remaining distance fits within this segment - done */
                bKeepGoing = 0;
            }
            else {
                /* Still have distance to cover: advance and subtract */
                player->lapCounter++;
                int crossed = AdvanceTrackSegment(player, (int *)&player->aiSegmentIdx, (int *)&player->waypointIdx);
                if (crossed != 0) {
                    player->distanceAccum += lastSegDist * 0x100;
                }
                player->aiAccumDist -= lastSegDist * 0x100;
            }
            if (++_loopCount > 256) {
                break;
            }
        } while (bKeepGoing);

        /* Final sub-segment interpolation:
         * fraction = (remaining distance) / (segment distance)
         * Adjust expected position by fraction of delta. */
        player->waypointX += (lastDeltaX * (player->aiAccumDist >> 8)) / lastSegDist;
        player->waypointZ += (lastDeltaZ * (player->aiAccumDist >> 8)) / lastSegDist;
    }
}

/**
 * AdvanceTrackSegment - 0x0041F878 - 1552 bytes
 *
 * Advances to the next waypoint along the track spline.
 * Manages the segment history ring buffer (16 entries).
 * Handles AI rubber banding: based on difficulty, race position,
 * and random chance, the AI may be redirected to an alternate
 * segment (ramp) to catch up or slow down.
 *
 * Returns: 0 = still within same segment, 1 = crossed segment boundary
 *
 * Original calling convention (Watcom fastcall):
 *   in_EAX = player struct pointer
 *   param_2 = pointer to waypoint index (int*)
 *   unaff_EBX = pointer to segment index (uint*)
 */
static int AdvanceTrackSegment(Player *player, int *segPtr, int *wpPtr)
{
    unsigned int curSeg = (unsigned int)*segPtr;     /* local_18 - EBX in binary */
    int wpIdx = *wpPtr;                               /* iVar7 - EDX in binary */
    unsigned int rampSeg;                             /* local_1c */
    unsigned int accelChance;                         /* local_20 */

    /* Bounds check: segment and waypoint must be valid */
    if (curSeg > 255) {
        return 0;
    }
    if (wpIdx < 0 || SEG_BASE(curSeg) + wpIdx < 0) {
        return 0;
    }
    char *wpData = WP_ADDR(curSeg, wpIdx);

    /* Step waypoint: 0=forward (+1), 1=reverse (-1) */
    if (player->directionFlip == 0) {
        wpIdx++;
    }
    else {
        wpIdx--;
    }

    /* Search the segment history ring buffer (tail -> head) */
    while ((unsigned int)player->ring.tail != (unsigned int)player->ring.head) {
        unsigned int ri = (unsigned int)player->ring.tail & 0xF;
        int basePt = (int)player->ring.wpBase[ri];
        if (player->ring.segId[ri] == (unsigned char)curSeg &&
            basePt <= wpIdx &&
            wpIdx - basePt < (int)(unsigned int)player->ring.wpCount[ri])
        {
            *segPtr = (int)curSeg;
            *wpPtr = wpIdx;
            return 0;
        }
        player->ring.tail = player->ring.tail + 1;
    }

    /* Check head entry of ring buffer */
    {
        unsigned int hi = (unsigned int)player->ring.head & 0xF;
        unsigned int headSeg = (unsigned int)player->ring.segId[hi];
        int headBase = (int)player->ring.wpBase[hi];
        unsigned int headCount = (unsigned int)player->ring.wpCount[hi];
        int headEnd = headBase + (int)headCount;

        if (player->directionFlip == 0) {
            /* Forward direction */
            if (headSeg == curSeg && headBase <= wpIdx && wpIdx < headEnd - 1) {
                *segPtr = (int)headSeg;
                *wpPtr = wpIdx;
                return 0;
            }
            if (headSeg == curSeg && headBase <= wpIdx &&
                wpIdx == headBase - 1 + (int)headCount) {
                *segPtr = (int)curSeg;
                *wpPtr = wpIdx;
                return 1;  /* crossed segment boundary */
            }
        }
        else {
            /* Reverse direction */
            if (headSeg == curSeg && headBase < wpIdx && wpIdx < headEnd) {
                *segPtr = (int)curSeg;
                *wpPtr = wpIdx;
                return 0;
            }
            if (headSeg == curSeg && wpIdx == headBase) {
                *segPtr = (int)curSeg;
                *wpPtr = headBase;
                return 1;  /* crossed segment boundary */
            }
        }
    }

    /* Waypoint link lookup: determine next segment via link data */
    /* Binary uses the original wpData (from entry wpIdx), NOT recomputed. */
    rampSeg = 0xFFFFFFFF;
    {
        /* Forward segment link: short at waypoint byte 10
         * (accessed as *(int*)(wp+8) >> 16 in original) */
        int linkFwd = *(short *)(wpData + 10);
        if (linkFwd != 0) {
            if (linkFwd < 1) {
                linkFwd = -linkFwd;  /* abs() */
            }
            rampSeg = (unsigned int)(linkFwd - 1);  /* 1-based -> 0-based */
        }
    }
    {
        /* Reverse/alternate segment link: short at waypoint byte 12 */
        int linkBwd = *(short *)(wpData + 12);
        if (linkBwd != 0) {
            if (linkBwd < 1) {
                linkBwd = -linkBwd;
            }
            curSeg = (unsigned int)(linkBwd - 1);
            /* Target waypoint index: byte at waypoint byte 14 */
            wpIdx = (int)(short)(*(unsigned short *)(wpData + 0xE) & 0xFF);
        }
    }

    /* Wrap waypoint within segment bounds */
    {
        int segLen = SEG_COUNT(curSeg);
        if (segLen <= wpIdx) {
            wpIdx -= segLen;
        }
        if (wpIdx < 0) {
            wpIdx += SEG_COUNT(curSeg);
        }
    }

    /* Disable ramp/rubber-band logic for mirrored mode and podium mode */
    if (g_raceSubMode == SUBMODE_TAG) {
        rampSeg = 0xFFFFFFFF;
    }
    if (g_raceType == RACE_SPECIAL)  {
        rampSeg = 0xFFFFFFFF;
    }

    if (rampSeg == 0xFFFFFFFF || player->_unk_0x78 != 0) {
        goto update_ring;
    }

    /* Rubber banding section */
    {
        int rampOff = (int)rampSeg * 16;
        int charId = player->charId;
        /* Table index: 5 bytes per entry, indexed by (charId + rampSeg*16 - 16) */
        int tableIdx = (charId + rampOff - 16) * 5;
        char *rampTbl = (char *)g_charRampSpeedTable;

        accelChance = (unsigned int)*(unsigned char *)(rampTbl + tableIdx);       /* byte 0 */
        unsigned int decelChance = (unsigned int)*(unsigned char *)(rampTbl + 1 + tableIdx);  /* byte 1 */
        unsigned int specialFlag = (unsigned int)*(unsigned char *)(rampTbl + 4 + tableIdx);  /* byte 4 */

        /* Random value 0-99 (scaled from 0-255 byte) */
        unsigned int randVal = (unsigned int)g_randTable2[g_randomRingIdx] * 100 >> 8;
        g_randomRingIdx = (g_randomRingIdx + 1) & 0xFF;

        /* Adjust chances based on race position relative to player 1 */
        short p1Pos = ((Player *)g_playerBase)->racePosition;

        if (decelChance < 0x33) {
            /* Low decel chance: boost accel if behind player 1 */
            if (accelChance > 0x32 && g_raceCheckpoint != 0 &&
                player->racePosition < p1Pos)
            {
                accelChance += 0x28;
            }
        }
        else if (player->racePosition < p1Pos) {
            /* High decel chance + behind player 1: scale by difficulty */
            if (g_raceCheckpoint != 0) {
                decelChance /= 3;
            }
            if (g_difficultyConfig == DIFF_EASY) {
                decelChance = 0;
            }
            if (g_difficultyConfig == DIFF_NORMAL) {
                decelChance = 10;
            }
            if (g_demoMode == DEMO_TITLE) {
                decelChance = 10;
            }
        }

        /* Special item/collectible check - skip rubber banding if item is active */
        int skipRubberBand = 0;
        if (specialFlag != 0) {
            skipRubberBand = 1;  /* default: assume item active -> skip rubber banding */

            if (specialFlag < 0x40) {
                /* Item index 1-63: check item state table */
                if (g_itemStateTable == NULL) {
                    skipRubberBand = 0;
                }
                else if (g_trackId == TRACK_REGAL_RUIN || g_trackId == TRACK_REACTIVE_FACTORY) {
                    /* Ruin/Factory: check if item is active (activeFlag != 0) */
                    TerItemState *ist = &((TerItemState *)g_itemStateTable)[specialFlag - 1];
                    if (ist->activeFlag == 0)
                        skipRubberBand = 0;  /* item inactive */
                }
                else {
                    /* Other tracks: check item cost vs player rings + active state */
                    TerItemState *ist = &((TerItemState *)g_itemStateTable)[specialFlag - 1];
                    if (ist->ringCost <= player->ringCount || ist->activeFlag == 0) {
                        skipRubberBand = 0;  /* item below player or inactive */
                    }
                }
            }
            else if (specialFlag == 0x40) {
                skipRubberBand = (g_factoryEmeraldBounceTimer1 == 0) ? 1 : 0;
            }
            else if (specialFlag == 0x41) {
                skipRubberBand = (g_factoryEmeraldBounceTimer2 == 0) ? 1 : 0;
            }
            else {
                skipRubberBand = 0;  /* specialFlag > 0x41: always apply rubber banding */
            }
        }
        if (skipRubberBand) {
            goto update_ring;
        }

        /* Track position comparison for rubber banding */
        {
            int posDiff = (int)((unsigned int)player->trackProgress >> 21) -
                          (int)((unsigned int)g_playerBase->trackProgress >> 21);

            unsigned int accelThresh = (unsigned int)*(unsigned char *)(rampTbl + 2 + tableIdx);
            unsigned int decelThresh = (unsigned int)*(unsigned char *)(rampTbl + 3 + tableIdx);

            /* Within normal rubber band range: check both thresholds */
            if (g_rubberBandLowerBound < posDiff && posDiff < g_rubberBandThreshold) {
                if (accelThresh <= randVal && decelThresh <= randVal) {
                    goto update_ring;
                }
            }
            /* Outside normal range: adjust chances based on distance */
            else {
                if (posDiff > 0 && accelChance > 0x32) {
                    accelChance += (unsigned int)((posDiff * 16) / g_rubberBandDivisor);
                }
                if (posDiff < 0 && decelChance > 0x32) {
                    decelChance -= (unsigned int)((posDiff * 64) / g_rubberBandDivisor);
                }

                /* Check if adjusted chances triggered */
                if ((posDiff < 1 || (int)accelChance <= (int)randVal) &&
                    (posDiff >= 0 || (int)decelChance <= (int)randVal))
                {
                    /* Neither chance triggered: check decel threshold only
                     * (goto joined_r0x0041fd6b in original - bypasses accel check) */
                    if (decelThresh <= randVal) {
                        goto update_ring;
                    }
                }
            }

            /* Rubber banding triggered: redirect AI to ramp segment */
            wpIdx = 0;
            curSeg = rampSeg;
        }
    }

update_ring:
    *segPtr = (int)curSeg;
    *wpPtr = wpIdx;

    /* Ring buffer update: extend or create entry */
    {
        unsigned int hi = (unsigned int)player->ring.head & 0xF;
        int headBase = (int)player->ring.wpBase[hi];
        unsigned int headCount = (unsigned int)player->ring.wpCount[hi];

        /* Try to extend current head entry forward */
        if (player->ring.segId[hi] == (unsigned char)curSeg &&
            wpIdx == headBase + (int)headCount &&
            player->directionFlip == 0 &&
            headCount < 0x40)
        {
            player->ring.wpCount[hi] = player->ring.wpCount[hi] + 1;
            return 1;
        }

        /* Try to extend current head entry backward (reverse direction) */
        if (player->ring.segId[hi] == (unsigned char)curSeg &&
            wpIdx == headBase - 1 &&
            player->directionFlip == 1 &&
            headCount < 0x40)
        {
            /* Increment count, decrement base */
            unsigned int hi2 = (unsigned int)player->ring.head & 0xF;
            player->ring.wpCount[hi2] = player->ring.wpCount[hi2] + 1;
            unsigned int hi3 = (unsigned int)player->ring.head & 0xF;
            player->ring.wpBase[hi3] = player->ring.wpBase[hi3] - 1;
            return 1;
        }

        /* Create new ring buffer entry */
        {
            unsigned int newHead = (unsigned int)player->ring.head + 1;
            player->ring.head = newHead;
            player->ring.segId[newHead & 0xF] = (unsigned char)curSeg;
            player->ring.wpBase[(unsigned int)player->ring.head & 0xF] = (short)wpIdx;
            player->ring.wpCount[(unsigned int)player->ring.head & 0xF] = 1;
            return 1;
        }
    }
}

/**
 * RewindTrackSegment - 0x0041FE88 - 325 bytes
 *
 * Moves backward along the track spline (undoes one AdvanceTrackSegment step).
 * Used when the player's accumulated distance goes negative,
 * indicating they moved backward relative to the track.
 *
 * Returns: always 0.
 *
 * Original calling convention (Watcom fastcall):
 *   in_EAX = player struct pointer
 *   param_2 = pointer to waypoint index (int*)
 *   unaff_EBX = pointer to segment index (uint*)
 */
static int RewindTrackSegment(Player *player, int *segPtr, int *wpPtr)
{
    /* Read tail entry of ring buffer */
    unsigned int ti = (unsigned int)player->ring.tail & 0xF;
    unsigned int tailSeg = (unsigned int)player->ring.segId[ti];
    int tailBase = (int)player->ring.wpBase[ti];
    unsigned int tailCount = (unsigned int)player->ring.wpCount[ti];

    unsigned int curSeg = (unsigned int)*segPtr;
    int wpIdx = *wpPtr;

    if (player->directionFlip == 0) {
        /* Forward direction: rewind = step backward */
        if (curSeg == tailSeg && tailBase < wpIdx) {
            *wpPtr = wpIdx - 1;
            *segPtr = (int)tailSeg;
            return 0;
        }
        if (curSeg == tailSeg) {
            /* Pop tail entry to go to previous segment */
            unsigned int newTail = (unsigned int)player->ring.tail - 1;
            player->ring.tail = newTail;
            if (newTail != 0) {
                unsigned int ni = newTail & 0xF;
                if (ni != ((unsigned int)player->ring.head & 0xF)) {
                    *wpPtr = (int)(unsigned int)player->ring.wpCount[ni] +
                             (int)player->ring.wpBase[ni] - 1;
                    *segPtr = (int)(unsigned int)player->ring.segId[ni];
                    return 0;
                }
            }
        }
    }
    else {
        /* Reverse direction: rewind = step forward */
        if (curSeg == tailSeg && wpIdx < (int)(tailBase + tailCount - 1)) {
            wpIdx++;
            /* fall through to final store */
        }
        else if (curSeg == tailSeg) {
            /* Pop tail entry */
            unsigned int newTail = (unsigned int)player->ring.tail - 1;
            player->ring.tail = newTail;
            if (newTail != 0) {
                unsigned int ni = newTail & 0xF;
                if (ni != ((unsigned int)player->ring.head & 0xF)) {
                    *wpPtr = (int)player->ring.wpBase[ni];
                    *segPtr = (int)(unsigned int)player->ring.segId[ni];
                    return 0;
                }
            }
        }
    }

    *wpPtr = wpIdx;
    *segPtr = (int)curSeg;
    return 0;
}

/**
 * FindNearestWaypoint - 0x00496544 - 123 bytes
 * Searches waypoint array (3 ints per entry: X, Y, Z) for the nearest
 * to the given search position. Returns the index of the nearest.
 *
 * Original Watcom fastcall:
 *   in_EAX = waypoint array, param_2 = searchX, unaff_EBX = searchY,
 *   param_1 = searchZ, param_3 = count
 */
int FindNearestWaypoint(int *waypoints, int searchX, int searchY, int searchZ, int count)
{
    if (waypoints == NULL) {
        return 0;
    }

#ifdef SONICR_DC
    float bestDistf = FLT_MAX;
    int bestIdx = 0;

    for (int i = 0; i < count; i++) {
        vec3f_t dv;
        dv.x = (float)(waypoints[0] - searchX);
        dv.y = (float)(waypoints[1] - searchY);
        dv.z = (float)(waypoints[2] - searchZ);

        float distf = vec_fipr(dv);

        if (distf < bestDistf) {
            bestDistf = distf;
            bestIdx = i;
        }
        waypoints += 3;
    }
    return bestIdx;

#else
    unsigned int bestDist = 0xFFFFFFFF;
    int bestIdx = 0;

    for (int i = 0; i < count; i++) {
        int dx = waypoints[0] - searchX;
        int dy = waypoints[1] - searchY;
        int dz = waypoints[2] - searchZ;
        unsigned int dist = (unsigned int)(dx * dx + dy * dy + dz * dz);
        if (dist < bestDist) {
            bestDist = dist;
            bestIdx = i;
        }
        waypoints += 3;
    }
    return bestIdx;
#endif
}

/* VALIDATED: capstone 2026-03-25 - multiplication chain (base*0x1194), field offsets, slot check */
/* =====================================================================
 * ResetPlayerToWaypoint - FUN_00421100 - 366 bytes
 * Recalculates AI speed parameters from the rubber band speed factor.
 * Updates player fields +0x1B0..+0x1BE with scaled speed values.
 *
 * Original: in_EAX = player pointer.
 * ===================================================================== */
void ResetPlayerToWaypoint(Player *player)
{
    unsigned short speedFactor = g_aiSpeedFactor;

    if (speedFactor != player->speedField &&
        player->lapsCompleted < 3)
    {
        int base = (int)speedFactor + 0x80;
        player->speedField = speedFactor;

        /* Speed factor fields 0x1B0-0x1BE - binary 0x421134-0x4211BA */
        int v1 = base * 31 * 4;    /* base * 124 */
        v1 += base;                 /* base * 125 */
        v1 *= 4;                    /* base * 500 */
        int v2 = v1;
        v1 <<= 3;                  /* base * 4000 */
        v1 += v2;                   /* base * 4500 = base * 0x1194 */
        player->speedModifier = (short)(((v1) + ((v1) >> 31)) >> 8);
        player->_unk_0x1BE = (short)((((int)(base * 0x2EE)) + (((int)(base * 0x2EE)) >> 31)) >> 8);
        player->physicsConst0x1B4 = (short)((((int)(base * 0x226)) + (((int)(base * 0x226)) >> 31)) >> 8);
        int angVel = (base * 3) << 13;  /* (base*4 - base) << 13 = base*3*8192 */
        player->maxSpeedCap = ((angVel) + ((angVel) >> 31)) >> 8;

        /* Character stats scaling - binary 0x4211C0-0x421263 */
        short charId = player->charId;
        int *stats = g_charStatsTable + (int)charId * 10;

        /* maxSpeed = stats[0] * 9 * 4 / 32 * base / 256 */
        int spdRaw = stats[0] * 9 * 4;
        int spdScaled = ((spdRaw) + ((spdRaw) >> 31)) >> 5;  /* /32 Watcom */
        int maxSpd = spdScaled * base;
        player->paramMaxSpeed = ((maxSpd) + ((maxSpd) >> 31)) >> 8;  /* /256 */

        /* accel = stats[1] * base / 256 */
        int accRaw = stats[1] * base;
        player->paramAccel = ((accRaw) + ((accRaw) >> 31)) >> 8;

        /* drag = stats[3] * 4 * base / 256 */
        int dragRaw = stats[3] * 4 * base;
        player->paramDrag = ((dragRaw) + ((dragRaw) >> 31)) >> 8;
    }
}

/* =====================================================================
 * FindNearestSegment - FUN_004964e0 - 109 bytes
 * Searches a segment table for the nearest entry to a given world
 * position using 2D distance (X² + Z²). Each table entry is 16 bytes
 * with X in lower 16 bits (signed) and Z in upper 16 bits.
 *
 * Original: in_EAX = table pointer, EDX = packedXY (>>16 for targetX),
 *   EBX = worldZ (negated internally), ECX = count (loop bound).
 * ===================================================================== */
int FindNearestSegment(int *table, int count, int worldX, int worldZ)
{
    unsigned int bestDist = 0x7FFFFFFF;
    int bestIdx = -1;
    int i;
    int targetX = worldX >> 16;
    int targetZ = -worldZ;

    for (i = 0; i < count; i++) {
        int entryX = *(short *)((char *)table + i * 16 + 0);           /* short at byte 0 */
        int entryZ = *(short *)((char *)table + i * 16 + 2);          /* short at byte 2 */
        int dx = entryX - targetX;
        int dz = entryZ - targetZ;
        unsigned int dist = (unsigned int)(dx * dx + dz * dz);
        if (dist < bestDist) {
            bestDist = dist;
            bestIdx = i;
        }
    }
    return bestIdx;
}

/* ROM data globals for rubber banding - all resolved as aliases:
 * g_rbTrackLen = g_aiTurnThreshold (0x540060), g_rbBaseSpeed = g_baseSpeedFactor (0x540048),
 * g_rbThreshold = g_rubberBandLowerBound (0x540050) - all #defined in sonicr_globals.h */
extern unsigned short g_aiSpeedFactor;   /* 0x0054004A - computed speed (write target) */

/* Computed rubber band values */
int g_rbAIProgress;             /* DAT_005400A0 - current AI track progress */
int g_rbPlayerProgress;         /* DAT_005400A4 - current player track progress */

/* =====================================================================
 * AdjustRubberBandAI - FUN_00421270 - 869 bytes
 * Per-frame: computes rubber band speed factor for AI players based on
 * their track progress relative to the human player. Also handles
 * stuck-player respawn logic.
 *
 * Rubber band zones (distance = AI progress - player progress):
 *   > trackLen/2:     speed = base/2 (far ahead → slow down a lot)
 *   > trackLen/4:     speed = base*7/8 (ahead → slow down)
 *   < -trackLen:      speed = base*5/4 (far behind → speed up, cap 200)
 *   < -trackLen/2:    speed = base*3/2 (behind → speed up, cap 200)
 *   > threshold*2:    speed = base (slightly ahead → normal)
 * ===================================================================== */
void AdjustRubberBandAI(void)
{
    int numSlots = (g_raceType == RACE_SPECIAL) ? 2 : 5;
    Player *p0 = (Player *)g_playerBase;

    /* Find the active AI player and compute rubber band speed */
    if (numSlots > 1) {
        for (int slot = 1; slot < numSlots; slot++) {
            Player *aiPlayer = ((Player **)g_playerPtrTable)[slot];
            if (aiPlayer == NULL) {
                continue;
            }

            int aiRaceState = aiPlayer->racePosition;
            int p0State = p0->racePosition;
            int expectedState = (p0State == 1) ? 2 : 1;

            if (aiRaceState == expectedState || g_raceType == RACE_SPECIAL) {
                g_rbAIProgress = (unsigned int)aiPlayer->trackProgress >> 21;
                g_rbPlayerProgress = (unsigned int)g_playerBase->trackProgress >> 21;
                int dist = g_rbAIProgress - g_rbPlayerProgress;

                if (dist > g_rbTrackLen / 2) {
                    /* AI far ahead -> halve speed */
                    g_aiSpeedFactor = g_rbBaseSpeed / 2;
                    if (g_aiSpeedFactor < 0x20) {
                        g_aiSpeedFactor = 0x20;
                    }
                }
                else if (dist > g_rbTrackLen / 4) {
                    /* AI ahead -> 7/8 speed */
                    g_aiSpeedFactor = (int)((unsigned int)g_rbBaseSpeed * 7) >> 3;
                    if (g_aiSpeedFactor < 0x20) {
                        g_aiSpeedFactor = 0x20;
                    }
                }
                else if (dist < -g_rbTrackLen) {
                    /* AI far behind -> 5/4 speed */
                    g_aiSpeedFactor = (int)((unsigned int)g_rbBaseSpeed * 5) >> 2;
                    if (g_aiSpeedFactor > 200) {
                        g_aiSpeedFactor = 200;
                    }
                }
                else if (dist < -(g_rbTrackLen / 2)) {
                    /* AI behind -> 3/2 speed */
                    g_aiSpeedFactor = ((unsigned int)g_rbBaseSpeed * 3) / 2;
                    if (g_aiSpeedFactor > 200) {
                        g_aiSpeedFactor = 200;
                    }
                }
                else if (dist > g_rbThreshold * 2) {
                    /* AI slightly ahead -> normal speed */
                    g_aiSpeedFactor = g_rbBaseSpeed;
                }
                break;
            }
        }
    }

    /* Stuck player respawn logic */
    if (g_raceOrder[5] != 0) {
        Player *sp = ((Player **)g_playerPtrTable)[g_raceOrder[5]];
        if (sp == NULL) {
            return;
        }

        /* Compute respawn countdown based on heading change */
        int countdown = 0x3C0 - (int)sp->speedField * 2;
        if (g_raceSubMode == SUBMODE_TAG) {
            countdown *= 2;
        }

        /* Increment stuck timer */
        if (sp->finishState == 0) {
            if (g_trackId == TRACK_REGAL_RUIN) {                /* binary trackId 4 = Ruin */
                sp->ring._unk_0x160 += 4;
            }
            else {
                sp->ring._unk_0x160 += 5;
            }
        }

        /* Check if timer exceeded - teleport to waypoint */
        if ((int)(unsigned short)sp->ring._unk_0x160 >= countdown) {
            sp->ring._unk_0x160 = 0;
            sp->ring._unk_0x166 = 1;
            /* Teleport to saved checkpoint position */
            sp->posX = sp->respawnX;
            sp->posY = sp->respawnY;
            sp->posZ = sp->respawnZ;
            sp->aiSegmentIdx = (unsigned int)(unsigned short)sp->ring.respawnSegment;
        }

        sp->ring._unk_0x166 += 1;

        /* If respawn state == 2, do full waypoint reset */
        if (sp->ring._unk_0x166 == 2) {
            ResetPlayerToWaypoint((Player *)sp);
            /* Binary 0x421510-0x421552: computes waypoint data pointer and
             * packs player position for FindNearestSegment.
             * EAX = g_waypointDataBase + SEG_BASE(seg)*16
             * EDX = SEG_COUNT(seg)  [= number of waypoints]
             * EBX = (posX>>12)<<16 + (posY>>12)  [= packed X|Y]
             * ECX = posZ>>12 */
            int seg47 = sp->aiSegmentIdx;
            int *segEntry = (int *)((char *)g_segmentOffsetTable + seg47 * 8);
            int segCount = segEntry[0];
            int segBase = segEntry[1];
            int *wpData = (int *)((char *)g_waypointDataBase + segBase * 16);
            int packedXY = ((sp->posX >> 12) << 16) + (sp->posY >> 12);
            int posZ = sp->posZ >> 12;
            int segIdx = FindNearestSegment(wpData, segCount, packedXY, posZ);
            sp->steerState = 0;
            sp->modelCharId = 0;
            sp->aiAccumDist = 2000;
            sp->ring.head = 0;
            sp->ring.tail = 0;
            sp->ring.segId[0] = 0xFF;
            sp->waypointIdx = segIdx;
            sp->aiAccel = (unsigned int)sp->speedModifier;
        }

        /* Clear respawn flag if state is 1 or 4 */
        if (sp->ring._unk_0x166 == 1 || sp->ring._unk_0x166 == 4) {
            sp->ring._unk_0x166 = 0;
        }
    }
}
