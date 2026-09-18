/**
 * physics_small.c — Lap tracking, sector detection, AI steering, water collision
 *
 * ValidateLapCompletion    — 0x004815A4 — 318 bytes
 * UpdatePlayerLapSector           — 0x004816E4 — 715 bytes
 * AISteeringAndDrag        — 0x004D5BE4 — 700 bytes
 * ResolvePlayerCollision   — 0x004D56B8 — 832 bytes
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include <math.h>

#define IABS(x) ({ int _v = (x); int _s = _v >> 31; (_v ^ _s) - _s; })

/* Track surface check — returns 1 if on-track, 0 if off-track */
extern int IsOnTrackSurface(float x, float z);  /* 0x47B794 */

/* Character type table — maps charId to character class */
extern int g_charTypeTable[];       /* 0x50157A — stride 2 */

extern int g_bestOverallLap;            /* 0x008FB8F4 */
extern int g_checkpointB[];             /* 0x008FB8F8 — per-charId best lap / checkpoint B */


/**
 * ValidateLapCompletion — FUN_004815A4 — 318 bytes
 * Checks lap times, updates best times per character, plays lap SFX.
 * Returns the completed lap number (1-3) or 0 if no new lap.
 * EAX = player pointer.
 */
static int ValidateLapCompletion(Player *player)
{
    char *p = (char *)player;
    int charId = (int)player->charId;                       /* 0x4815A9 */
    int lap1 = player->lap1Time;                           /* 0x4815B0 */
    int completed = 0;  /* esi */
    int highestLap = 0; /* edx — tracks highest valid lap */

    /* Check each lap time against 0xFFFFFF threshold — 0x4815B7-0x4815EC */
    if (lap1 > 0xFFFFFF) {                                  /* 0x4815B7 */
        highestLap = 1;
        *(unsigned char *)(p + 0x53) = 0;                   /* 0x4815C4: clear flag byte */
    }
    if (player->lap2Time > 0xFFFFFF) {                     /* 0x4815C8 */
        highestLap = 2;
        *(unsigned char *)(p + 0x57) = 0;
    }
    if (player->lap3Time > 0xFFFFFF) {                     /* 0x4815DA */
        highestLap = 3;
        *(unsigned char *)(p + 0x5B) = 0;
    }

    /* Best time tracking per-lap, per-character — 0x4815F0-0x4816B9 */
    short lapCount = player->lapsCompleted;                     /* 0x4815F0 */

    if (lapCount >= 3) {                                    /* 0x4815F8: cmp bx, 2; jb → skip if < 2+1 */
        /* Lap 3 done — 0x48160A */
        int t3 = player->lap3Time;
        if (t3 <= g_bestOverallLap) {
            g_bestOverallLap = t3;  /* 0x481613 */
        }
        if (t3 <= g_checkpointB[charId]) {                  /* 0x481627 */
            g_checkpointB[charId] = t3;
            *(unsigned char *)(p + 0x5B) |= 1;              /* 0x481638: flag new best */
            completed = 3;
        }
    }
    if (lapCount >= 2) {                                    /* falls through or jumps to 0x481643 */
        int t2 = player->lap2Time;
        if (t2 <= g_bestOverallLap) {
            g_bestOverallLap = t2;
        }
        if (t2 <= g_checkpointB[charId]) {
            g_checkpointB[charId] = t2;
            *(unsigned char *)(p + 0x57) |= 1;
            completed = 2;
        }
    }
    if (lapCount >= 1) {                                    /* 0x48167C or 0x4816B3→0x4816B7 */
        int t1 = player->lap1Time;
        if (t1 <= g_bestOverallLap) {
            g_bestOverallLap = t1;
        }
        if (t1 <= g_checkpointB[charId]) {
            g_checkpointB[charId] = t1;
            *(unsigned char *)(p + 0x53) |= 1;
            completed = 1;
        }
    }

    /* Play lap completion SFX if new best and not already at that lap — 0x4816B9 */
    if (completed != 0 && completed != highestLap) {
        if (g_demoMode != DEMO_TITLE) {
            PlaySoundEffect(0x22, 0, 0);              /* 0x4816D5: call 0x482280 */
        }
    }

    return completed;                                       /* 0x4816DA: mov eax, esi */
}

/**
 * UpdatePlayerLapSector — FUN_004816E4 — 715 bytes
 * Sector detection via 3 cross-product boundary tests (3-bit sector 0-7),
 * lap counting on sector 6→7 transitions, and finished-player progress write.
 *
 * Boundary data from g_trackBoundaryWaypoints ([0x90249C]), NOT g_splineWaypoints.
 * player = in_EAX (int*)
 */
void UpdatePlayerLapSector(Player *player)
{
    int *bnd = g_trackBoundaryWaypoints;
    if (bnd == NULL) {
        return;
    }

    int posX = player->posX;
    int posZ = player->posZ;

    /* Cross-product test 1 (bit 1) — 0x4816F2-0x481742
     * Boundary: bnd[3],bnd[5] → bnd[6],bnd[8] */
    int sector;
    int x1;
    int z1;
    int x2;
    int z2;
    long long cross;
    x1 = bnd[3] << 12;
    z1 = bnd[5] << 12;         /* 0x0C, 0x14 */
    x2 = bnd[6] << 12;
    z2 = bnd[8] << 12;         /* 0x18, 0x20 */
    cross = (long long)(posX - x1) * (long long)(z2 - z1)
          - (long long)(posZ - z1) * (long long)(x2 - x1);
    sector = (cross >= 0) ? 2 : 0;                     /* 0x481742: add eax,eax → bit 1 */

    /* Cross-product test 2 (bit 0) — 0x48174A-0x4817A2
     * Boundary: bnd[0],bnd[2] → bnd[3],bnd[5] */
    x1 = bnd[0] << 12;
    z1 = bnd[2] << 12;         /* 0x00, 0x08 */
    x2 = bnd[3] << 12;
    z2 = bnd[5] << 12;         /* 0x0C, 0x14 */
    cross = (long long)(posX - x1) * (long long)(z2 - z1)
          - (long long)(posZ - z1) * (long long)(x2 - x1);
    if (cross >= 0) {
        sector |= 1;
    }

    /* Cross-product test 3 (bit 2) — 0x4817A4-0x4817F9
     * Boundary: bnd[0],bnd[2] → bnd[9],bnd[11] */
    x1 = bnd[9] << 12;
    z1 = bnd[11] << 12;        /* 0x24, 0x2C */
    x2 = bnd[0] << 12;
    z2 = bnd[2] << 12;         /* 0x00, 0x08 */
    cross = (long long)(posX - x1) * (long long)(z2 - z1)
          - (long long)(posZ - z1) * (long long)(x2 - x1);
    if (cross >= 0) {
        sector |= 4;                       /* 0x4817F6: shl eax, 2 */
    }

    /* Sector swap for time attack free practice — 0x4817F9-0x481825 */
    if (g_raceType == RACE_TIMEATTACK && g_raceSubMode == SUBMODE_REVERSE) {  /* 0x481801, 0x481806 */
        if (sector == 7) {
            sector = 6;
        }
        else if (sector == 6) {
            sector = 7;
        }
    }

    /* Track-specific height gate — 0x481825-0x481862
     * Only proceed to lap counting if player is at valid altitude for the track. */
    int trackId = g_trackId;                            /* [0x8FB8EC] */
    int passGate = 0;
    if (trackId == TRACK_RESORT_ISLAND || trackId == TRACK_REGAL_RUIN || trackId == TRACK_REACTIVE_FACTORY) { /* 0x48182B-0x481838 */
        passGate = 1;
    }
    else if (trackId == TRACK_RADICAL_CITY) {                          /* 0x48183A */
        if ((unsigned int)player->posY < 0xFFA95000U) { /* 0x481842: posY < threshold */
            passGate = 1;
        }
    }
    else if (trackId == TRACK_RADIANT_EMERALD) {                          /* 0x48184B */
        if ((unsigned int)player->posY < 0xFF900000U) { /* 0x48185B */
            passGate = 1;                               /* jae skips → passGate only if jb */
       }
    }
    /* Binary: track 5 uses jae 0x48199e (skip if >= threshold) */
    if (trackId == TRACK_RADIANT_EMERALD && (unsigned int)player->posY >= 0xFF900000U) {
        passGate = 0;
    }
    if (!passGate) {
        goto store_sector;
    }
    /* Sector 7: mark transition — 0x481868-0x48187B
     * If entering sector 7 from sector 6, set player+0x60 = 1 */
    if (sector == 7) {                                      /* 0x481868 */
        int prevSector = player->_unk_0x62;                 /* 0x481870: *(int*)(p+0x60)>>16 = short at 0x62 */
        if (prevSector == 6) {                              /* 0x481876 */
            player->lapCrossFlag = 1;                          /* 0x48187E */
        }
    }

    /* Sector 6: lap completion — 0x481884-0x4818C7 */
    if (sector != 6) {
        goto store_sector;                     /* 0x481884 */
    }
    
    int prevSector = player->_unk_0x62;                 /* 0x481890: *(int*)(p+0x60)>>16 = short at 0x62 */
    if (prevSector != 7) {
        goto store_sector;             /* 0x481899 */
    }

    /* Check if already counted this lap — 0x48189F */
    int lapCount5E = player->lapCrossFlag;                 /* 0x4818A2: *(int*)(p+0x5E)>>16 = short at 0x60 */
    /* This checks if the short at 0x60 == 1, which means "already passed 7→6". */
    if (lapCount5E == 1) {                              /* 0x4818A8 */
        player->lapCrossFlag = 0;                          /* 0x4818B0: reset flag */
        goto store_sector;                              /* 0x4818B6 */
    }

    /* Check if race position < 3 (not finished) — 0x4818BB */
    int racePos = player->lapsCompleted;                    /* 0x4818BB: *(int*)(p+0x5C)>>16 = short at 0x5E = lap count */
    if (racePos >= 3) {
        goto store_sector;                /* 0x4818C7 */
    }

    /* Lap increment — 0x4818CD */
    player->lapsCompleted++;                                /* 0x4818D0: inc word [eax+0x5E] */

    /* Call lap validation for player 0 or in GP mode — 0x4818D4 */
    if (player == (Player *)g_playerBase || g_raceType == RACE_MULTIPLAYER) { /* 0x4818D4, 0x4818DB */
        int lapResult = ValidateLapCompletion(player);  /* 0x4818EB: call 0x4815A4 */

        /* In GP mode: if lap validation returned non-zero, clear matching charIds — 0x4818F0 */
        if (g_raceType == RACE_MULTIPLAYER && lapResult != 0) {  /* 0x4818F0, 0x4818FD */
            /* Compute this player's index — 0x481905-0x481914 */
            Player *pBase = (Player *)g_playerBase;
            int thisIdx = (int)((char *)player - (char *)pBase) / PLAYER_STRIDE;
            /* Loop through all players, clear position bytes for same charId — 0x481937-0x48196F */
            for (int n = 0; n < (int)g_numPlayers; n++) {   /* 0x481920 uses [0x6E9910] */
                Player *pN = (Player *)((char *)pBase + n * PLAYER_STRIDE);
                if (n == thisIdx) {
                    goto next_player;      /* 0x481939: skip self */
                }
                if (pN->charId == player->charId) {
                    /* Same charId — clear byte flags at +0x57, +0x5B, +0x53 */
                    /* 0x8FD547 = g_playerBase+0x53, 0x8FD54B = +0x57, 0x8FD54F = +0x5B */
                    *((unsigned char *)pN + 0x57) = 0;   /* 0x48194F */
                    *((unsigned char *)pN + 0x5B) = 0;   /* 0x481955 */
                    *((unsigned char *)pN + 0x53) = 0;   /* 0x48195B */
                }
            next_player:
                /* */ ;
            }
        }
    }
    
    /* Finished player progress — 0x481971-0x48199B */
    int posVal = player->lapsCompleted;                     /* *(int*)(p+0x5C)>>16 = short at 0x5E = lap count */
    if (posVal == 3) {                                  /* 0x48197A: finished all laps */
        int counter = g_finishOrderCounter;             /* 0x481984 */
        unsigned int progress = 0xFFFFFFFF - (unsigned int)counter; /* 0x48197F, 0x481990 */
        g_finishOrderCounter = counter + 1;             /* 0x48198F, 0x481995 */
        player->trackProgress = (int)progress;          /* 0x48199B: byte 0x4C */
    }

store_sector:
    /* Store final sector — 0x48199E */
    player->_unk_0x62 = (short)sector;                      /* 0x4819A1 */
}

/**
 * UpdatePlayerPhysicsA — 0x004819B0 — 124 bytes
 * Terrain height lookup. Sets player+0x40 based on whether the player
 * is over valid terrain or out of bounds.
 *
 * player = in_EAX (int*)
 */
void UpdatePlayerPhysicsA(Player *player)
{
    player->collisionResult = 0;

    if (g_trackId == TRACK_RADIANT_EMERALD) {
        /* Radiant Emerald: no terrain lookup, always on track */
        player->dynamicSpeedMode = 0;   /* 0x4819cf: mov word ptr [eax+0x40], 0 */
    }
    else {
        /* Convert player position to float for terrain lookup.
         * 0x52FB6D = 0x39800000 = 1.0f/4096.0f (fixed-point >> 12 scale). */
        float worldX = (float)player->posX * (1.0f / 4096.0f);
        float worldZ = (float)player->posZ * (1.0f / 4096.0f);

        int terrainResult = IsOnTrackSurface(worldX, worldZ);

        if (terrainResult == 0) {
            /* Off-track — 0x4819fd: cmp dword ptr [0x94bcf4], 2 / jne.
             *
             * Snow is the only exemption from drowning; every other
             * weather drowns in every mode. The function is 124 bytes
             * (0x4819B0-0x481A2B) and never reads g_raceSubMode
             * (0x8FB954), so Tag mode is not a special case here. */
            if (g_weatherType == WEATHER_SNOW) {
                /* Snow/ice: mark off-track but don't trigger drowning */
                player->collisionResult = 1;              /* 0x481a06 */
            }
            else {
                /* Everything else: mark as OOB — triggers drowning */
                player->dynamicSpeedMode = (short)0x8000; /* 0x481a12 */
                return;
            }
        }
        player->dynamicSpeedMode = 0;                     /* 0x481a1f */
    }
}

/**
 * UpdatePlayerPhysicsB — 0x00481A2C — 543 bytes
 * Water detection, jump state management, and gravity enable/disable.
 *
 * player = in_EAX (int*)
 */
void UpdatePlayerPhysicsB(Player *player)
{
    short charId = player->charId;
    int charType = g_charTypeTable[charId];  /* 0x0050157A + charId * 2, upper 16 */

    /* Water detection: set flag if conditions met */
    if (player->groundedFlag == 1 &&            /* running */
        g_postRaceCameraMode == 0 &&
        player->posY > -0x30000 &&              /* above water level */
        (player->overSurface == 0 || player->prevOverSurface == 0) &&
        player->dynamicSpeedMode != 0 &&        /* underwater flag set */
        player->loopMode == 0) {               /* not on a loop surface */
        player->dynamicSpeedMode = 1;
    }
    else {
        player->dynamicSpeedMode = 0;
    }

    if (charType == 3) {
        /* VEHICLE: different physics — handled separately */
        player->prevOverSurface = (short)player->overSurface;
        return;
    }

    if (charType == 1) {
        /* RUNNER WITH HOVER (Tails) */
        if (player->dynamicSpeedMode == 0) {
            if (player->abilityState == 2) {
                player->abilityState = 0;
            }
        }
        else {
            player->abilityState = 2;
        }
    }
    else {
        /* RUNNER — standard type or speed type */
        int isUnderwater = player->dynamicSpeedMode;

        if (charType == 2) {
            if (player->abilityState == 2) {
                player->abilityState = 0;
            }
            /* Speed threshold check for water skim */
            int absVelX = IABS(player->forwardSpeed);
            int absVelZ = IABS(player->lateralSpeed);
            if ((absVelX > 0x4000 || absVelZ > 0x4000) &&
                player->yOffset == 0) {
                if (isUnderwater == 1) {
                    player->abilityState = 2;
                }
                isUnderwater = 0;
            }
        }

        /* Jump/gravity state machine */
        if (isUnderwater == 1 && player->_unk_0x42 == 0) {
            /* In water, not on surface */
            if (player->itemEffectState == -1) {
                player->itemEffectState = (short)0xFFFE;
            }
            if (player->itemEffectState >= 0) {
                if (player->yOffset == 0) {
                    player->sfxTrigger = 0x15;  /* splash sound */
                }
                if (player->itemEffectState > 0) {
                    player->itemEffectState = 0;
                }
                player->yOffset += 0x10000;    /* raise Y offset by one unit */
                player->_unk_0x86 = 0;

                /* Clamp vertical height */
                if (player->yOffset > MAX_JUMP_HEIGHT) {
                    player->yOffset = MAX_JUMP_HEIGHT;
                    player->prevOverSurface = (short)player->overSurface;
                    return;
                }
            }
        }
        else {
            /* Not in water or on surface — gravity descent */
            if (player->itemEffectState == -2) {
                player->itemEffectState = 0;
            }
            if (player->yOffset == MAX_JUMP_HEIGHT) {
                player->sfxTrigger = 0x15;  /* splash on max height */
                player->_unk_0x42 = 0x14;
            }
            int newHeight = player->yOffset - 0x10000;
            player->yOffset = newHeight;
            if (newHeight < 0) {
                player->yOffset = 0;
            }
        }
    }

    player->prevOverSurface = (short)player->overSurface;
}

/* =====================================================================
 * AISteeringAndDrag — 0x004D5BE4 — 700 bytes
 *
 * AI-controlled version of ApplyDragAndSteering. Steers the object's
 * heading (obj[0x10]) toward a target angle read from the object data
 * table, with a turn rate of 0x30 per frame (taking the shortest path
 * around the 12-bit angle circle). Decomposes world velocity into local
 * forward/lateral, applies drag, adds a constant lateral boost (0x2710),
 * and clamps with asymmetric limits (+0x60000 / -0x18000).
 *
 * Also adjusts the target heading based on:
 *   - Race sub-type 2 with player flag → +0x800 (reverse direction)
 *   - Speed vs table threshold → ±0x40 (tighter turns at low speed)
 *
 * EAX = object struct pointer (Watcom fastcall).
 *
 * Object layout: same as ApplyDragAndSteering.
 *   [0x00..0x08]=pos, [0x10]=heading, [0x20..0x28]=prevPos,
 *   [0x92]=animIdx(>>16), [0xC0]=speed, [0xCC]=velX, [0xD0]=velZ,
 *   [0x1CC]=player flags.
 * ===================================================================== */
void AISteeringAndDrag(int *obj)
{
    /* Save previous position */
    obj[8] = obj[0];                                     /* obj[0x20] = obj[0x00] */
    obj[9] = obj[1];                                     /* obj[0x24] = obj[0x04] */
    obj[10] = obj[2];                                    /* obj[0x28] = obj[0x08] */

    /* Constants */
    int maxLateral  = 0x60000;                           /* [ebp-0x1c] */
    int latBoost    = 0x2710;                            /* [ebp-0x2c] = 10000 */
    int fwdDrag     = 0x0E00;                            /* [ebp-0x20] */
    int turnRate    = 0x30;                              /* edx */

    /* This loop surface entry (indexed by obj+0x94) */
    int loopIdx = *(short *)((char *)obj + 0x94);        /* [esi+0x92] >> 16 */
    TerLoopEntry *we = &((TerLoopEntry *)g_terLoopTable)[loopIdx];

    /* Read target heading from table */
    int targetAngle = we->surfAngle;                      /* [eax+0x12]>>16 = surfAngle (+0x14) */

    /* Race mode heading adjustment */
    if (g_raceSubMode == SUBMODE_TAG) {                  /* cmp [0x8fb954], 2 */
        int playerFlag = *(short *)((char *)obj + 0x1CE);          /* [esi+0x1cc] >> 16 */
        if (playerFlag == 1) {
            targetAngle += 0x800;                        /* add 0x800 (half circle) */
        }
    }

    /* Speed-based heading tweak */
    int speedVal = we->speedRef;                          /* [eax+0xe]>>16 = speedRef (+0x10) */
    int playerSpeed = *(int *)((char *)obj + 0xC0);     /* [esi+0xc0] */
    if ((speedVal << 10) > playerSpeed) {                /* slow: turn more */
        targetAngle += 0x40;
    }
    else if (((speedVal << 12) - (speedVal << 10)) < playerSpeed) {  /* fast: turn less */
        targetAngle -= 0x40;                             /* speedVal*3072 < playerSpeed */
    }

    targetAngle &= 0xFFF;                               /* wrap to 12 bits */

    /* Heading interpolation */
    int currentAngle = obj[4];                           /* obj[0x10] */
    int diff = targetAngle - currentAngle;
    int wrapThresh = 0x1000 - turnRate;                  /* 0xFD0 */

    if (diff > 0) {
        if (diff < turnRate) {
            obj[4] = targetAngle;                        /* snap: very close */
        }
        else if (diff < 0x800) {
            obj[4] += turnRate;                          /* short path: add */
        }
        else if (diff > wrapThresh) {
            obj[4] = targetAngle;                        /* close via wrap: snap */
        }
        else {
            obj[4] -= turnRate;                          /* long path: subtract */
        }
    }
    else if (diff < 0) {
        if (diff > -turnRate) {
            obj[4] = targetAngle;                        /* snap: very close */
        }
        else if (diff > -0x800) {
            obj[4] -= turnRate;                          /* short path: subtract */
        }
        else if (diff < -wrapThresh) {
            obj[4] = targetAngle;                        /* close via wrap: snap */
        }
        else {
            obj[4] += turnRate;                          /* long path: add */
        }
    }
    /* diff == 0: no change */

    obj[4] &= 0xFFF;                                    /* wrap heading to 12 bits */

    /* Velocity decomposition (same as ApplyDragAndSteering) */
    /* Heading reference = surfAngle of the same entry (binary re-reads obj+0x94) */
    int headingOff = we->surfAngle;
    int rotAngle = (obj[4] - headingOff) & 0xFFF;

    /* Trig lookups */
    int cosA  = g_cosTable[rotAngle];
    int sinA  = g_sinTable[rotAngle];
    int negAngle = (-rotAngle) & 0xFFF;
    int negSinA = g_sinTable[negAngle];
    int cosA2   = g_cosTable[negAngle];

    /* Read world velocity */
    int vX = *(int *)((char *)obj + 0xCC);
    int vZ = *(int *)((char *)obj + 0xD0);

    /* Rotate world → local.
     * Binary divides in TWO steps: idiv 0x1000 (trunc toward zero) THEN sar 2
     * (arithmetic floor). This is NOT the same as a single /0x4000 for negative
     * sums — e.g. S=-20480 gives (-5)>>2=-2 here vs -1 for /16384. Matching the
     * two-step rounding is required for AI-car velocity to stay bit-exact. */
    int forward = (int)(((long long)vX * cosA2 + (long long)vZ * negSinA) / 4096) >> 2;      /* idiv 0x1000; sar 2 */
    int lateral = (int)(((long long)(-vX) * negSinA + (long long)vZ * cosA2) / 4096) >> 2;   /* idiv 0x1000; sar 2 */

    /* Constant lateral boost */
    lateral += latBoost;                                 /* += 0x2710 */

    /* Forward drag (toward 0 by fwdDrag=0x0E00) */
    if (forward < 0) {
        forward += fwdDrag;
        if (forward > 0) {
            forward = 0;
        }
    }
    else if (forward > 0) {
        forward -= fwdDrag;
        if (forward < 0) {
            forward = 0;
        }
    }

    /* Lateral drag (toward 0 by fwdDrag/3) */
    int latDrag = fwdDrag / 3;                           /* 0xe00 / 3 = 0x4AB */
    if (lateral < 0) {
        lateral += latDrag;
        if (lateral > 0) {
            lateral = 0;
        }
    }
    else if (lateral > 0) {
        lateral -= latDrag;
        if (lateral < 0) {
            lateral = 0;
        }
    }

    /* Asymmetric lateral clamp */
    if (lateral > maxLateral) {                          /* > 0x60000 */
        lateral -= maxLateral / 16;                      /* reduce by 0x6000 */
    }
    if (lateral < -(maxLateral / 4)) {                   /* < -0x18000 */
        lateral += maxLateral / 16;                      /* boost by 0x6000 */
    }

    /* Rotate local → world (same two-step idiv 0x1000 / sar 2 rounding) */
    int newVX = (int)(((long long)forward * cosA + (long long)lateral * sinA) / 4096) >> 2;
    int newVZ = (int)(((long long)(-forward) * sinA + (long long)lateral * cosA) / 4096) >> 2;

    *(int *)((char *)obj + 0xCC) = newVX;
    *(int *)((char *)obj + 0xD0) = newVZ;
}

/* =====================================================================
 * ResolvePlayerCollision — FUN_004d56b8 — 832 bytes
 *
 * Player-vs-player 2D elastic collision response in XZ plane.
 * Both players must be collision-eligible ([+0xa0] != 0) and on the
 * same surface ([+0x94] match).  Computes XZ distance from world
 * positions [+0xc0]/[+0xc4] (20.12 fixed); bails if >= 0x60.
 *
 * Builds normalized separation vector, projects both players'
 * velocities ([+0xcc]/[+0xd0]) onto it, computes bounce factor,
 * applies equal-and-opposite impulse (/8), then damps all velocities
 * by 14/16 (87.5%).  Sets [+0x204] = 0x30001 collision flag on both.
 *
 * Watcom fastcall: EAX = playerA, EDX = playerB.
 * ===================================================================== */
void ResolvePlayerCollision(Player *pA, Player *pB)    /* 0x4d56b8 */
{
    /* Both must be collision-eligible */
    if (pA->loopMode == 0) {
        return;                        /* 0x4d56c6 */
    }
    if (pB->loopMode == 0) {
        return;                        /* 0x4d56d4 */
    }

    /* Must be on same surface/layer */
    if (pB->_unk_0x94 != pA->_unk_0x94) {
        return;           /* 0x4d56e2 */
    }

    /* Distance check between world positions */
    int dxPos = pB->loopLocalX - pA->loopLocalX;            /* 0x4d56f6 */
    int dzPos = pB->loopLocalZ - pA->loopLocalZ;            /* 0x4d5702 */
    int dxS = dxPos >> 12;                                 /* 0x4d5712 */
    int dzS = dzPos >> 12;                                 /* 0x4d571c */

    int distSq = dxS * dxS + dzS * dzS;                   /* 0x4d5719 */
    int dist = (int)sr_sqrtf((float)distSq);                  /* 0x4d572f: fild+fsqrt */
    dist = ((dist << 8) >> 8) + 1;                         /* 0x4d573f: sign-extend 24-bit + 1 */

    if (dist >= 0x60) {
        return;                              /* 0x4d5749 */
    }

    /* Normalized separation direction (8.8 fixed) */
    int absDx = dxS < 0 ? -dxS : dxS;                     /* 0x4d5752: cdq;xor;sub = abs */
    int absDz = dzS < 0 ? -dzS : dzS;                     /* 0x4d5769 */
    int sepX = (absDx << 8) / dist;                        /* 0x4d575c */
    int sepZ = (absDz << 8) / dist;                        /* 0x4d5773 */

    /* Normalized velocity direction for playerB */
    int bVelX = pB->loopVelX >> 8;                        /* 0x4d577a */
    int bVelZ = pB->loopVelZ >> 8;                        /* 0x4d578b */

    int bVelSq = bVelX * bVelX + bVelZ * bVelZ;           /* 0x4d5788 */
    int bVelMag = (int)sr_sqrtf((float)bVelSq);              /* 0x4d57a4: fild+fsqrt */
    bVelMag = ((bVelMag << 8) >> 8) + 1;                  /* 0x4d57b4 */

    int absBVX = bVelX < 0 ? -bVelX : bVelX;              /* 0x4d57be */
    int absBVZ = bVelZ < 0 ? -bVelZ : bVelZ;              /* 0x4d57d4 */
    int bDirX = (absBVX << 8) / bVelMag;                  /* 0x4d57c8 */
    int bDirZ = (absBVZ << 8) / bVelMag;                  /* 0x4d57de */

    /* Dot product: project B's velocity onto separation axis */
    int dot = sepZ * bDirZ + sepX * bDirX;                /* 0x4d57ed */
    dot = (dot >> 8) + 1;                                  /* 0x4d57fe */

    /* Normalized velocity direction for playerA */
    int aVelX = pA->loopVelX >> 8;                        /* 0x4d581e */
    int aVelZ = (-pA->loopVelZ) >> 8;                     /* 0x4d582a: neg then sar */

    int aVelSq = aVelX * aVelX + aVelZ * aVelZ;           /* 0x4d583b */
    int aVelMag = (int)sr_sqrtf((float)aVelSq);              /* 0x4d584f: fild+fsqrt */
    aVelMag = ((aVelMag << 8) >> 8) + 1;                  /* 0x4d585f */

    int absAVX = aVelX < 0 ? -aVelX : aVelX;              /* 0x4d5869 */
    int absAVZ = aVelZ < 0 ? -aVelZ : aVelZ;              /* 0x4d587f */
    int aDirX = (absAVX << 8) / aVelMag;                  /* 0x4d5874 */
    int aDirZ = (absAVZ << 8) / aVelMag;                  /* 0x4d588a */

    /* Combine: project A's velocity, scale by separation */
    int dotA = sepX * aDirX + sepZ * aDirZ;               /* 0x4d5898 */
    /* Scale position delta by combined projection */
    int scaleA = (dotA >> 8) + 1;                          /* 0x4d58c5 */
    int deltaX = pA->loopLocalX - pB->loopLocalX;           /* 0x4d58ad */
    int deltaZ = pA->loopLocalZ - pB->loopLocalZ;           /* 0x4d58cf */
    int impulseX = (deltaX * scaleA) >> 8;                 /* 0x4d58ea */
    int impulseZ = (deltaZ * scaleA) >> 8;                 /* 0x4d58f3 */

    /* Apply collision: scale by dot, then /8 */
    int pushX = (dot * dxPos) >> 8;                        /* 0x4d5808 */
    int pushZ = (dot * dzPos) >> 8;                        /* 0x4d581b */

    int finalX = pushX - impulseX;                         /* 0x4d5903 */
    int finalZ = pushZ - impulseZ;                         /* 0x4d5905 */
    finalX >>= 3;                                          /* 0x4d5907: sar 3 */
    finalZ >>= 3;                                          /* 0x4d590a: sar 3 */

    /* Equal and opposite: B gains, A loses */
    pB->loopVelX += finalX;                               /* 0x4d5919 */
    pB->loopVelZ += finalZ;                               /* 0x4d591b */
    pA->loopVelX -= finalX;                               /* 0x4d592f */
    pA->loopVelZ -= finalZ;                               /* 0x4d593d */

    /* Damp all velocities by 14/16 (87.5%) */
    /* Pattern: val = val * 14 / 16                         */
    /* Binary:  lea [eax*8]; sub eax; add eax = *14, sar 4  */
    /* Uses signed rounding: (v*14 + (v*14>>31)&15) >> 4    */
#define DAMP14_16(v) do {                                   \
        int _t = (v) * 14;                                  \
        (v) = (_t + ((_t >> 31) & 15)) >> 4;                \
    } while (0)

    DAMP14_16(pA->loopVelX);                              /* 0x4d594b */
    DAMP14_16(pA->loopVelZ);                              /* 0x4d5969 */
    DAMP14_16(pB->loopVelX);                              /* 0x4d598f */
    DAMP14_16(pB->loopVelZ);                              /* 0x4d59b5 */

#undef DAMP14_16

    /* Set collision flag on both players */
    pA->renderState = 0x30001;                             /* 0x4d59db */
    pB->renderState = 0x30001;                             /* 0x4d59e5 */
}
