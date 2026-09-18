/**
 * race_setup.c — Race setup and GP progression functions
 *
 * SetupSpecialRace       — 0x00471AA0 — 957 bytes
 * AdvanceGrandPrixTrack  — 0x00471364 — 1695 bytes
 * CalculateChampionshipPoints — 0x004C43C0 — 2395 bytes
 * ShowEmeraldUnlockScreen  — 0x004C86D0 — 1625 bytes
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "sonicr_paths.h"
#include "win32_shim.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

/* Race state globals */
extern int g_raceCounter9c;

/* Per-track start config table (defined in player_init.c) */
extern const int s_trackConfigTable[][2];

/* Camera */
extern void ComputeLookAtAngles(Player *player, int *target, CamStateEntry *cam);
extern void InitPlayerSlot(Player *player, int charId);


/**
 * ClearRaceStateArrays — the four race-state clear loops Watcom expanded inline
 * into all three race-entry functions:
 *
 *   AdvanceGrandPrixTrack  0x47152C / 0x47153F / 0x471555 / 0x47156A
 *   SetupSpecialRace       0x471B82 / 0x471B94 / 0x471BA9 / 0x471BBD
 *   InitTrackCommon        0x472505 / 0x472519 / 0x47252E / 0x472542
 *
 * All three are byte-identical in stride, bound and target. Each loop clears
 * ONE field per entry — not the whole array — so live entries keep their other
 * fields; the cleared field is what marks the entry dead.
 */
void ClearRaceStateArrays(void)
{
    /* Foot-shadow billboards: visibility word of all 80 slots (5 players ×
     * 16 rotating). Binary: eax = 0x10..0x500 step 0x10, store dword at
     * [eax + 0x908E1C] — i.e. 0x908E2C..0x90931C, entry base 0x908E20 + 0xC. */
    for (int i = 0; i < 80; i++) {
        g_footShadowCtrl[i * 4 + 3] = 0;
    }

    /* Collect effects: lifetime (+0x1E) of all 64 entries. Binary: eax =
     * 0x3C..0xF00 step 0x3C, store WORD at [eax + 0x907F02], base 0x907F20. */
    for (int i = 0; i < COLLECT_EFFECT_COUNT; i++) {
        g_collectEffectBuf[i].lifetime = 0;
    }

    /* Ring chase: timer (+0x18) of all 32 entries. Binary: eax = 0x20..0x400
     * step 0x20, store dword at [eax + 0x907B18], base 0x907B20. */
    for (int i = 0; i < 32; i++) {
        g_ringChaseArray[i * 8 + 6] = 0;
    }

    /* Bounce/missile slots: type (+0x00) of all 4 entries. Binary: eax =
     * 0x0C..0x30 step 0x0C, store dword at [eax + 0x907AE4], base 0x907AF0.
     * Entries are 3 intptr_t slots (type, targetPtr, dataPtr) — index by
     * slot, not by the binary's 12-byte stride, which is 32-bit-only. */
    for (int i = 0; i < 4; i++) {
        g_bounceStateArray[i * 3] = 0;
    }
}

/**
 * SetupSpecialRace — 0x00471AA0 — 957 bytes
 * Retranslated from binary 2026-04-11.
 * Sets up a special/boss race (raceType 3). Picks the rival character
 * based on track ID, positions players at podium spawn points,
 * calls InitPlayerSlot for 3 players, computes initial camera angles,
 * and disables track-specific objects for the rival race.
 */
void SetupSpecialRace(void)
{
    int rivalCharId;

    /* 0x471aab: g_raceType = 3 */
    g_raceType = 3;                                           /* EAX = 0x8fb950 */
    g_finishOrderCounter = 0;                                 /* EAX = 0x901c80 */

    /* 0x471ac4-0x471af3: rival character selection */
    /* Binary 0x471ADC `cmp trackId,3`→7, 0x471AEA `cmp 4`→8; binary
     * convention 3=Factory/4=Ruin (un-crosswired 2026-07-13):
     * Factory's rival is Metal Knuckles, Ruin's is EggRobo. */
    if (g_trackId == TRACK_RESORT_ISLAND) {
        rivalCharId = CHAR_METAL_SONIC;     /* EAX = 5 */
    }
    if (g_trackId == TRACK_RADICAL_CITY) {
        rivalCharId = CHAR_TAILS_DOLL;      /* EAX = 6 */
    }
    if (g_trackId == TRACK_REACTIVE_FACTORY) {
        rivalCharId = CHAR_METAL_KNUCKLES; /* EAX = 7 */
    }
    if (g_trackId == TRACK_REGAL_RUIN) {
        rivalCharId = CHAR_EGG_ROBO;        /* EAX = 8 */
    }

    /* 0x471af8: store rivalCharId to two locations */
    g_replayCharIds[1] = rivalCharId;                         /* 0x8fb984 — dword */
    g_playerBase[1].charId = (short)rivalCharId;             /* 0x8fdd02 — word */

    g_inputStateEC = 0;                                       /* 0x471b03 */

    /* 0x471b0c-0x471b7b: globals setup */
    g_fadeLevel = (int)0xFFFFFF00;                            /* 0x901c44 */
    g_fadeState = FADE_IN;                                    /* 0x901c48 */
    g_fadeSpeed = 0x10;                                       /* 0x901c4c */
    g_raceFinished = 0;                                       /* 0x901c88 */
    g_orbitAngle = 0;                                         /* 0x901c50 */
    g_ringCollectPos[0] = 0;                                  /* 0x901c8c */
    g_raceTimerB[0] = 0;                                      /* 0x901c98 */
    g_ringRespawnPos[0] = 0;                                  /* 0x901ca4 */
    g_effectPosItemBurst[0] = 0;                              /* 0x901cb0 */
    g_raceCounter98 = 0;                                      /* 0x902098 */
    g_raceCounterA0 = 0;                                      /* 0x9020a0 */
    g_particleIdx = 0;                                        /* 0x9020a4 */
    g_raceCounterA8 = 0;                                      /* 0x9020a8 */
    g_postRaceCameraMode = (g_raceSpeedMult << 2) - 0x10;    /* 0x901c84 */
    g_trackEventTimer = 0;                                    /* 0x9118e8 */

    ClearRaceStateArrays();                                   /* 0x471b80-0x471bcd */

    /* 0x471c50: position players from podium spawn points */
    int *podium = (int *)g_podiumCenter;
    Player *p0 = &g_playerBase[0];
    Player *p1 = &g_playerBase[1];
    Player *p2 = &g_playerBase[2];

    /* Player 0: podium spawn 6 (offset 0x48) */
    p0->posX = podium[18] << 12;                         /* 0x471c55: [eax+0x48] << 12 */
    p0->posY = -(podium[19] << 12);                      /* 0x471c61: neg [eax+0x4c] << 12 */
    p0->posZ = podium[20] << 12;                         /* 0x471c6f: [eax+0x50] << 12 */
    p0->anglePitch = 0;                                   /* 0x471c7b */
    p0->angleYaw = s_trackConfigTable[g_trackId][1];     /* 0x471c83: [trackId*8 + 0x4feb04] */
    p0->angleRoll = 0;                                    /* 0x471c96 */

    /* Player 1: podium spawn 8 (offset 0x60) */
    p1->posX = podium[24] << 12;                         /* 0x471c9c: [eax+0x60] << 12 */
    p1->posY = -(podium[25] << 12);                      /* 0x471ca8: neg [eax+0x64] << 12 */
    p1->posZ = podium[26] << 12;                         /* 0x471cb6: [eax+0x68] << 12 */
    p1->anglePitch = 0;                                   /* 0x471cc2 */
    p1->angleYaw = p0->angleYaw;                          /* 0x471cc8: copy from [0x8fd504] */
    p1->angleRoll = 0;                                    /* 0x471cd4 */

    /* Player 2: podium spawn 7 (offset 0x54) */
    p2->posX = podium[21] << 12;                         /* 0x471cda: [eax+0x54] << 12 */
    p2->posY = -(podium[22] << 12);                      /* 0x471ce6: neg [eax+0x58] << 12 */
    p2->posZ = podium[23] << 12;                         /* 0x471cf4: [eax+0x5c] << 12 */
    p2->anglePitch = 0;                                   /* 0x471cff */
    p2->angleYaw = p0->angleYaw;                          /* 0x471d05: copy from [0x8fd504] */
    p2->angleRoll = 0;                                    /* 0x471d0f */

    /* 0x471d15-0x471d43: init player slots — direct calls with player + charId */
    InitPlayerSlot(p0, (int)(short)p0->charId);           /* 0x471d21: EAX=0x8fd4f4, EDX=movsx [0x8fd5e6] */
    InitPlayerSlot(p1, (int)(short)p1->charId);           /* 0x471d32: EAX=0x8fdc10, EDX=movsx [0x8fdd02] */
    InitPlayerSlot(p2, (int)(short)p2->charId);           /* 0x471d43: EAX=0x8fe32c, EDX=movsx [0x8fe41e] */

    /* 0x471d48-0x471df9: camera orbit computation */
    g_orbitAngle = 0;                                     /* 0x471d4d */

    int yawIdx = (0x800 - p2->angleYaw) & 0xFFF;     /* 0x471d53-0x471d59 */

    /* sinOffset = (g_sinTable[yawIdx] * 600) / 65536 */
    int sinVal = g_sinTable[yawIdx];                  /* 0x471d5e */
    int sinScaled = sinVal * 600;                     /* 0x471d65-0x471d76: lea/add/shl/sub = ×600 */
    int sinSign = sinScaled >> 31;                    /* 0x471d7a: sar edx, 0x1f */
    int sinOffset = (sinScaled - (sinSign << 16) - (sinSign < 0 ? 1 : 0)) >> 16;  /* 0x471d7d-0x471d82 */

    /* cosOffset = (g_cosTable[yawIdx] * 600) / 65536 */
    int cosVal = g_cosTable[yawIdx];                  /* 0x471db0-0x471dbc */
    int cosScaled = cosVal * 600;                     /* 0x471dbf-0x471dd0 */
    int cosSign = cosScaled >> 31;                    /* 0x471dd4 */
    int cosOffset = (cosScaled - (cosSign << 16) - (cosSign < 0 ? 1 : 0)) >> 16;  /* 0x471dd7-0x471ddc */

    /* target position for camera look-at */
    int targetPos[3];
    targetPos[0] = -(podium[21]) - sinOffset;         /* 0x471d90-0x471d92 */
    targetPos[1] = podium[22] + 0x90;                 /* 0x471d95-0x471d9d */
    targetPos[2] = cosOffset - podium[23];            /* 0x471de4-0x471df1 */

    /* 0x471df9: ComputeLookAtAngles(player2, targetPos, g_camStateTable) */
    ComputeLookAtAngles(p2, targetPos, &g_camStateTable[0]);

    /* 0x471dfe-0x471e2f: copy smoothed camera to saved camera state */
    g_savedCamPitch = g_gpSmoothedCam.smoothYaw;                     /* 0x9020fc → 0x9021e0 */
    g_savedCamYaw   = g_gpSmoothedCam.smoothPitch;                 /* 0x9020fe → 0x9021e2 */
    g_savedCamX     = g_gpSmoothedCam.posX;                        /* 0x9020f0 → 0x9021e4 */
    g_savedCamY     = g_gpSmoothedCam.posY;                        /* 0x9020f4 → 0x9021e8 */
    g_savedCamZ     = g_gpSmoothedCam.posZ;                        /* 0x9020f8 → 0x9021ec */

    /* 0x471e34-0x471e55: finalize and disable track-specific rival objects */
    g_raceOrder[0] = 1;                                       /* 0x902070 */
    g_cdPlaybackState = 1;                                    /* 0x6d9a40 */

    /* Jump table at 0x471a90, indexed by (g_trackId - 1).
     * Sets rival-associated object fields to 0xFFFF in g_objectStructStorage.
     * Default (trackId not 1-4): no action. */
    switch (g_trackId) {
        case TRACK_RESORT_ISLAND:                                 /* 0x471bd5 */
            *(short *)((char *)g_objectStructStorage + 0x8474) = (short)0xFFFF;
            break;
        case TRACK_RADICAL_CITY:                                  /* 0x471bed */
            *(short *)((char *)g_objectStructStorage + 0xEE2C) = (short)0xFFFF;
            *(short *)((char *)g_objectStructStorage + 0xEE70) = (short)0xFFFF;
            break;
        /* Jump table 0x471A90: entry[2] (binary trackId 3 = FACTORY) → 0x471C2F,
        * entry[3] (binary 4 = RUIN) → 0x471C0E. */
        case TRACK_REACTIVE_FACTORY:                              /* 0x471c2f */
            *(short *)((char *)g_objectStructStorage + 0xCFA0) = (short)0xFFFF;
            *(short *)((char *)g_objectStructStorage + 0xCE90) = (short)0xFFFF;
            break;
        case TRACK_REGAL_RUIN:                                    /* 0x471c0e */
            *(short *)((char *)g_objectStructStorage + 0xD688) = (short)0xFFFF;
            *(short *)((char *)g_objectStructStorage + 0xD6CC) = (short)0xFFFF;
            break;
    }
}

extern unsigned char g_difficultyModeC;  /* 0x008FB9A0 */
extern unsigned char g_gpWonBits;        /* 0x008FB9A1 */
extern unsigned char g_gpLostBits;       /* 0x008FB9A2 */

void FUN_004d025c(void);                 /* 0x004D025C — 7 bytes, tiny helper */
void UpdateCDPlayback(int trackNum);     /* 0x004D01AC */

/* =====================================================================
 * AdvanceGrandPrixTrack — 0x00471364 — 1695 bytes
 *
 * Called after a GP race to advance to the next track. Clears race state,
 * repositions players at podium spawn points based on their finish order,
 * and reinitializes player slots for the next race.
 * ===================================================================== */
void AdvanceGrandPrixTrack(void)
{
    g_finishOrderCounter = 0;                                        /* 0x901c80 */

    /* Play music if not special race */
    if (g_raceType != RACE_SPECIAL) {                                 /* 0x47137d */
        UpdateCDPlayback(4);                                         /* 0x4d01ac — play track 4 */
        DebugLog("advance gp track\n");                              /* 0x471391 */
        g_cdPlaybackState = 3;                                       /* 0x6d9a40 — skip states 1-2 so UpdateGameLogic doesn't kill the fanfare */
    }

    /* Championship track progression (raceType 4) */
    if (g_raceType == RACE_MULTIPLAYER) {                             /* 0x4713af */
        goto skip_to_init;                                           /* Multiplayer: skip flags */
    }
    if (g_raceType == 4) {                            /* 0x4713b8 */
        /* Compute bit mask for current track */
        int trackBit = 0x00;
        if (g_trackId == TRACK_RESORT_ISLAND) {
            trackBit = 0x02;                           /* 0x4713c1 */
        }
        if (g_trackId == TRACK_RADICAL_CITY) {
            trackBit = 0x04;                           /* 0x4713d1 */
        }
        /* Binary 0x4713E1 `cmp 3`→8, 0x4713F1 `cmp 4`→0x10; 3=Factory/4=Ruin
         * in binary convention. */
        if (g_trackId == TRACK_REACTIVE_FACTORY) {
            trackBit = 0x08;                     /* 0x4713e1 */
        }
        if (g_trackId == TRACK_REGAL_RUIN) {
            trackBit = 0x10;                        /* 0x4713f1 */
        }

        g_raceOrder[0] = 1;                                          /* 0x902070 */

        /* Check if player beat AI (compare placements) */
        short p0place = g_playerBase[0].racePosition;               /* 0x8fd550 */
        short p1place = g_playerBase[1].racePosition;               /* 0x8fdc6c */

        if (p0place < p1place) {                                     /* 0x471418: player0 won */
            g_playerBase[0].racePosition = 1;
            g_playerBase[1].racePosition = 5;

            /* Clear lost bit, set won bit */
            g_gpLostBits &= ~(unsigned char)trackBit;
            g_gpWonBits  |=  (unsigned char)trackBit;

            /* Mark track as won in extended status */
            int charId1 = (short)g_playerBase[1].charId;
            g_charUnlockTable[charId1] = 2;                       /* 0x8fba64 + charId*4 */
        }
        else {                                                     /* 0x471463: AI won */
            g_playerBase[0].racePosition = 5;
            g_playerBase[1].racePosition = 1;

            /* If won bit not already set, set lost bit */
            if (!(g_gpWonBits & (unsigned char)trackBit)) {
                g_gpLostBits |= (unsigned char)trackBit;
            }

            int charId1 = (short)g_playerBase[1].charId;
            g_charUnlockTable[charId1] = 1;                       /* 0x8fba64 + charId*4 */
        }
    }
    else {
        /* Non-GP, non-TA: busy-wait (original does 0x800 iterations) */
        volatile int dummy = 0;
        for (int i = 0; i < 0x800; i++) {
            dummy++;
        }
    }

skip_to_init:

    /* Clear race state */
    g_playerBase[0]._unk_0x1F8 = 0;                                /* 0x8FD6EC */
    g_fadeLevel = (int)0xFFFFFF00;                                   /* 0x901c44 */
    g_fadeState = 1;                                                 /* 0x901c48 — FADE_IN */
    g_fadeSpeed = 0x10;                                              /* 0x901c4c */
    g_raceFinished = 0;                                              /* 0x901c88 */
    g_postRaceCameraMode = 1;                                        /* 0x901c84 */
    g_orbitAngle = 0;                                                /* 0x901c50 */

    /* Clear race state counters */
    g_ringCollectPos[0] = 0;
    g_raceTimerB[0] = 0;
    g_ringRespawnPos[0] = 0;
    g_effectPosItemBurst[0] = 0;
    g_raceCounter98 = 0;
    g_raceCounter9c = 0;
    g_raceCounterA0 = 0;
    g_particleIdx = 0;
    g_raceCounterA8 = 0;
    g_trackEventTimer = 0;

    ClearRaceStateArrays();                                          /* 0x47152c-0x47157c */

    /* Position players from podium spawn table */
    int *podium = (int *)g_podiumCenter;                         /* 0x9024a8 */

    /* For each of the 5 player slots, get their placement and
     * index the podium table to set spawn position.
     * Podium table: 3 ints (X,Y,Z) per entry, indexed by (placement-1). */
    for (int p = 0; p < 5; p++) {
        Player *pl = &g_playerBase[p];
        short placement = pl->racePosition;
        int podIdx = (placement - 1) * 3;                       /* 3 ints per entry */

        pl->posX =   podium[podIdx + 0] << 12;                  /* posX */
        pl->posY = -(podium[podIdx + 1] << 12);                 /* posY (negated) */
        pl->posZ =   podium[podIdx + 2] << 12;                  /* posZ */
        pl->anglePitch = 0;                                     /* player[3] */

        /* Each player gets a different initial yaw velocity */
        int yawVel;
        if (p == 0)
            yawVel = 0x400;
        else if (p == 1)
            yawVel = 0x200;
        else if (p == 2)
            yawVel = 0x600;
        else if (p == 3)
            yawVel = 0x600;
        else
            yawVel = 0x500;

        pl->angleRoll = 0;                                       /* player[5] */
        pl->angleYaw = yawVel;                                   /* yaw */
    }

    /* Init player slots (0x4717E8) */
    for (int p = 0; p < g_numPlayers; p++) {
        Player *pl = &g_playerBase[p];
        short savedLapField = pl->lapsCompleted;                               /* cx preserved across call */
        int charId = (int)pl->charId;                                      /* edx = charId */
        InitPlayerSlot(pl, charId);                                        /* 0x471804: eax=player, edx=charId */
        pl->lapsCompleted = savedLapField;                                     /* 0x471809: restore cx */
    }

    /* Camera setup */
    g_orbitAngle = 0;                                                /* 0x901c50 */

    if (g_netSessionActive != 0) {                                    /* 0x47182f: multiplayer */
        /* Multiplayer podium camera — LOCAL player only (network clients use
         * their own g_localPlayerIndex). g_orbitAngle == 0 here (set above), so
         * the binary constant-folds the orbit to g_sinTable[0xC00] /
         * g_cosTable[0xC00] with radius 0x190. Writes to g_camStateTable[localIdx]. */
        int localIdx = (int)*(unsigned short *)&g_localPlayerIndex;  /* 0x68acdc */
        Player *pl = &g_playerBase[localIdx];

        int offsetX = (g_sinTable[0xC00] * 0x190) >> 16;   /* 0x471844-6e: [0x92868c]*0x190/65536 */
        int camX = ((-pl->posX) >> 12) - offsetX;          /* 0x471883-90 */
        int camY = ((-pl->posY) >> 12) + 0xC0;             /* 0x471893-a3 */
        int offsetZ = (g_cosTable[0xC00] * 0x190) >> 16;   /* 0x4718a6-d1 */
        int camZ = ((-pl->posZ) >> 12) + offsetZ;          /* 0x4718d4-e1 */

        int target[3] = { camX, camY, camZ };
        ComputeLookAtAngles(pl, target, &g_camStateTable[localIdx]); /* 0x47190b: EBX=&g_camStateTable[localIdx] */
    } else {
        /* Single-player: compute camera for each human player */
        for (int p = 0; p < g_numHumans; p++) {                     /* 0x47191a */
            Player *pl = &g_playerBase[p];

            /* Camera orbit angle offset from g_orbitAngle */
            int angle = (g_orbitAngle - 0x400) & 0xFFF;
            int sinIdx = angle << 2;

            /* Scale by orbit radius */
            int orbitRadius = (g_orbitAngle / 2) + 0x190;
            int sinVal = g_sinTable[sinIdx / 4];
            int offsetX = (sinVal * orbitRadius) >> 16;

            int camX = -(pl->posX >> 12) - offsetX;
            int camY = -(pl->posY >> 12) + 0xC0;

            int cosVal = g_cosTable ? g_cosTable[sinIdx / 4] : 0;
            int offsetZ = (cosVal * orbitRadius) >> 16;
            int camZ = -(pl->posZ >> 12) + offsetZ;

            int target[3] = { camX, camY, camZ };

            /* Binary EBX = &g_camStateTable[p] (edi=0x902140, +0x28/player) —
             * NOT s_cameraStruct (0x6E9924); same wrong-struct mistake noted at
             * player_init.c:710. */
            ComputeLookAtAngles(pl, target, &g_camStateTable[p]);    /* 0x4233c0 */
        }
    }
}

/* =====================================================================
 * CalculateChampionshipPoints — 0x004C43C0 — 2395 bytes
 *
 * Called after a championship race. Collects per-player lap times from
 * player structs, compares against per-track best times, updates standings
 * and per-character statistics. Pure computation, no rendering.
 * ===================================================================== */

extern int g_gpBestChangedAlt[];     /* 0x006D9800 */
/* g_gpTrackBestA..H, g_gpPlayerStandings and g_gpCharTrackWins are #defines
 * into g_saveBlock — see sonicr_globals.h */

#define CHARDETAIL_STRIDE   41  /* 0xA4 / 4 = 41 ints per character */

/**
 * CompareAndUpdateBestA — FUN_004c4370 — 37 bytes
 * If *current < *best, update *best and set g_gpBestChanged[playerIdx].
 */
static int CompareAndUpdateBestA(int playerIdx, int *current, int *best)
{
    if (*current < *best) {                                         /* 0x4c4377 */
        *best = *current;
        g_gpBestChanged[playerIdx] = 1;                             /* 0x6d97f0 */
        return 1;
    }
    return 0;
}

/**
 * CompareAndUpdateBestB — FUN_004c4398 — 37 bytes
 * Same as A but sets g_gpBestChangedAlt flag.
 */
static int CompareAndUpdateBestB(int playerIdx, int *current, int *best)
{
    if (*current < *best) {                                         /* 0x4c439f */
        *best = *current;
        g_gpBestChangedAlt[playerIdx] = 1;                          /* 0x6d9800 */
        return 1;
    }
    return 0;
}

void CalculateChampionshipPoints(void)
{
    /* Clear per-player flag arrays */
    /* 0x4c43cb: loop clears g_gpBestChanged[0..3] and g_gpBestChangedAlt[0..3] */
    for (int i = 0; i < 4; i++) {
        g_gpBestChanged[i] = 0;
        g_gpBestChangedAlt[i] = 0;
    }
    g_gpBeatAllFlag = 0;                                             /* 0x6d9810 */

    /* Collect per-player lap times from player structs */
    /* 0x4c43e3: loop over g_numViewports players */
    int *lapBase = g_racePointsLaps;                               /* 0x8fb64c */
    int *totBase = g_racePointsTotal;                           /* 0x8fb67c */

    for (int i = 0; i < g_numViewports; i++) {
        Player *pl = &g_playerBase[i];
        lapBase[i * 3 + 0] = pl->lap1Time & 0xFFFFFF;         /* lap1 → racePointsR */
        lapBase[i * 3 + 1] = pl->lap2Time & 0xFFFFFF;         /* lap2 → racePointsG */
        lapBase[i * 3 + 2] = pl->lap3Time & 0xFFFFFF;         /* lap3 → racePointsB */
        totBase[i] = lapBase[i*3+0] + lapBase[i*3+1] + lapBase[i*3+2]; /* total */
    }

    /* First scoring pass: CompareAndUpdateBestA */

    if (g_raceSubMode == SUBMODE_NORMAL && (g_raceType == RACE_GP || g_raceType == RACE_MULTIPLAYER || g_raceType == 4)) {
        /* 0x4c4462: cmp raceType<2 (jl) or ==4 — raceType 0/1/4 (GP/Multiplayer/upgrade);
         * Time Attack (2) deliberately excluded so it falls to the raceType==2 branch below */
        lapBase = g_racePointsLaps;
        totBase = g_racePointsTotal;
        int trkIdx = g_trackId - 1;

        for (int i = 0; i < g_numViewports; i++) {
            int placement = (int)g_playerBase[i].lapsCompleted;

            /* If placed (>0), compare lap1 against best */
            if (placement > 0) {
                CompareAndUpdateBestA(i, &lapBase[i*3], &g_gpTrackBestB[trkIdx]); /* 0x8fbabc */
            }
            /* If placed >1, compare lap2 */
            if (placement > 1) {
                CompareAndUpdateBestA(i, &lapBase[i*3+1], &g_gpTrackBestB[trkIdx]);
            }
            /* If placed >2, compare lap3 */
            if (placement > 2) {
                CompareAndUpdateBestA(i, &lapBase[i*3+2], &g_gpTrackBestB[trkIdx]);
            }
            /* If placed ==3 (finished all laps), compare total */
            if (placement == 3) {
                CompareAndUpdateBestA(i, &totBase[i], &g_gpTrackBestA[trkIdx]); /* 0x8fbaa8 */

                /* Update per-player standings (cap 99) */
                if (g_raceType == RACE_MULTIPLAYER) {
                    if (g_gpPlayerStandings[i] < 99) {               /* 0x8fbb88 */
                        g_gpPlayerStandings[i]++;
                    }
                    /* Update per-character cumulative */
                    int charId = (short)g_playerBase[i].charId;
                    if (g_gpCharDetail[charId * CHARDETAIL_STRIDE + 40] < 99) { /* 0x8fbd28 */
                        g_gpCharDetail[charId * CHARDETAIL_STRIDE + 40]++;
                    }
                }
            }
        }
    }
    else if (g_raceSubMode == SUBMODE_BALLOON) {
        /* 0x4c45c8: lapConfig 3 scoring */
        totBase = g_racePointsTotal;
        int trkIdx = g_trackId - 1;

        for (int i = 0; i < g_numViewports; i++) {
            int placement = (int)g_playerBase[i].lapsCompleted;
            if (placement == 3) {
                CompareAndUpdateBestA(i, &totBase[i], &g_gpTrackBestG[trkIdx]); /* 0x8fbb20 */

                if (g_raceType == RACE_MULTIPLAYER) {
                    if (g_gpPlayerStandings[i] < 99) {
                        g_gpPlayerStandings[i]++;
                    }
                    int charId = (short)g_playerBase[i].charId;
                    if (g_gpCharDetail[charId * CHARDETAIL_STRIDE + 40] < 99) {
                        g_gpCharDetail[charId * CHARDETAIL_STRIDE + 40]++;
                    }
                }
            }
        }
    }
    else if (g_raceSubMode == SUBMODE_TAG) {
        /* 0x4c4684: submode 2 (Tag) — binary tests g_raceSubMode only
         * (cmp edx,2 / jne 0x4c46bb); there is no raceType qualifier, so
         * Time Attack Tag scores here too. */
        int trkIdx = g_trackId - 1;
        if (g_playerBase[0].lapsCompleted == 3) {
            CompareAndUpdateBestA(0, g_racePointsTotal, &g_gpTrackBestH[trkIdx]); /* 0x8fbb34 */
        }
    }
    else if (g_raceType == RACE_TIMEATTACK) {
        /* 0x4c46bb: raceType 2 (Time Attack), various lapConfig scoring */
        int trkIdx = g_trackId - 1;
        lapBase = g_racePointsLaps;
        totBase = g_racePointsTotal;

        if (g_raceSubMode == SUBMODE_NORMAL) {
            /* Per-lap bests */
            if (g_playerBase[0].lapsCompleted > 0)
                CompareAndUpdateBestA(0, &lapBase[0], &g_gpTrackBestE[trkIdx]); /* 0x8fbaf8 */
            if (g_playerBase[0].lapsCompleted > 1)
                CompareAndUpdateBestA(0, &lapBase[1], &g_gpTrackBestE[trkIdx]);
            if (g_playerBase[0].lapsCompleted > 2)
                CompareAndUpdateBestA(0, &lapBase[2], &g_gpTrackBestE[trkIdx]);
            if (g_playerBase[0].lapsCompleted == 3)
                CompareAndUpdateBestA(0, totBase, &g_gpTrackBestC[trkIdx]); /* 0x8fbad0 */
        }
        else if (g_raceSubMode == SUBMODE_REVERSE) {
            /* Same pattern with different tables */
            if (g_playerBase[0].lapsCompleted > 0)
                CompareAndUpdateBestA(0, &lapBase[0], &g_gpTrackBestF[trkIdx]); /* 0x8fbb0c */
            if (g_playerBase[0].lapsCompleted > 1)
                CompareAndUpdateBestA(0, &lapBase[1], &g_gpTrackBestF[trkIdx]);
            if (g_playerBase[0].lapsCompleted > 2)
                CompareAndUpdateBestA(0, &lapBase[2], &g_gpTrackBestF[trkIdx]);
            if (g_playerBase[0].lapsCompleted == 3)
                CompareAndUpdateBestA(0, totBase, &g_gpTrackBestD[trkIdx]); /* 0x8fbae4 */
        }
    }

    /* GP standings update (raceType == 0) */
    /* 0x4c4809 */
    if (g_raceType == RACE_GP && g_playerBase[0].racePosition <= 3) {
        g_gpBeatAllFlag = 1;                                         /* 0x6d9810 */

        if (g_playerBase[0].racePosition == 1) {
            int trkIdx = g_trackId;
            if (g_gpTrackStatus[trkIdx] != 2) {
                g_gpTrackStatus[trkIdx] = 2;                         /* 0x8fba4c */
                int charId = (short)g_playerBase[0].charId;
                g_gpCharTrackWins[charId * 5 + trkIdx] = 1;         /* 0x8fbb94 */
                if (g_gpCharRaceCount[charId] == 4) {                /* 0x8fbc60 */
                    g_gpCharRaceCount[charId]++;
                }
                else {
                    g_gpCharRaceCount[charId]++;
                }
            }

            /* Unlock Eggman for human play once Radiant Emerald has been won.
             * The block above wrote g_gpTrackStatus[trkIdx] = 2, and trkIdx is
             * TRACK_RADIANT_EMERALD (5) on that track, so this fires on the
             * race that completes it. 0x8FBA74 IS charUnlockTable[4]; the
             * char-select cursor needs exactly 2 there to stop skipping him
             * (binary 0x48d302). Previously spelled g_gpRelayFlag — a
             * write-only alias nothing ever reads under that name. */
            if (g_gpTrackStatus[TRACK_RADIANT_EMERALD] == 2) {       /* 0x8fba60 */
                g_charUnlockTable[CHAR_EGGMAN] = 2;                  /* 0x8fba74 */
            }
            if (g_gpTrackStatus[5] == 0) {                           /* 0x8fba60 */
                /* Check all 4 main tracks complete */
                if (g_gpTrackStatus[1] == 2 && g_gpTrackStatus[2] == 2
                    && g_gpTrackStatus[3] == 2 && g_gpTrackStatus[4] == 2)
                {
                    g_gpAllTracksFlag = 1;                           /* 0x8fba60 → actually a different field */
                }
            }
        }
    }

    /* Second scoring pass: CompareAndUpdateBestB (per-character stats) */
    /* 0x4c48e7: same structure as first pass but writing to per-character detail arrays */
    lapBase = g_racePointsLaps;
    totBase = g_racePointsTotal;
    int trkIdx = g_trackId - 1;

    for (int i = 0; i < g_numViewports; i++) {
        int placement = (int)g_playerBase[i].lapsCompleted;
        int charId = (short)g_playerBase[i].charId;
        int charBase = charId * CHARDETAIL_STRIDE;

        if (g_raceSubMode == SUBMODE_NORMAL && (g_raceType == RACE_GP || g_raceType == RACE_MULTIPLAYER || g_raceType == 4)) {
            /* 0x4c4921: cmp raceType<2 (jl) or ==4 — raceType 0/1/4 (GP/Multiplayer/upgrade);
             * Time Attack (2) deliberately excluded so it falls to the raceType==2 branch below */
            /* Per-lap to per-character detail at offset +0x14 */
            if (placement > 0) {
                CompareAndUpdateBestB(i, &lapBase[i*3], &g_gpCharDetail[charBase + 5 + trkIdx]);
            }
            if (placement > 1) {
                CompareAndUpdateBestB(i, &lapBase[i*3+1], &g_gpCharDetail[charBase + 5 + trkIdx]);
            }
            if (placement > 2) {
                CompareAndUpdateBestB(i, &lapBase[i*3+2], &g_gpCharDetail[charBase + 5 + trkIdx]);
            }
            if (placement == 3) {
                CompareAndUpdateBestB(i, &totBase[i], &g_gpCharDetail[charBase + trkIdx]); /* offset 0 */
            }
        }
        else if (g_raceSubMode == SUBMODE_BALLOON) {
            if (placement == 3) {
                CompareAndUpdateBestB(i, &totBase[i], &g_gpCharDetail[charBase + 30 + trkIdx]); /* +0x78 */
            }
        }
        else if (g_raceSubMode == SUBMODE_TAG) {
            /* 0x4c4a92: cmp edx,2 / jne 0x4c4ad3 — submode only, no raceType. */
            if (placement == 3) {
                CompareAndUpdateBestB(i, &totBase[i], &g_gpCharDetail[charBase + 35 + trkIdx]); /* +0x8C */
            }
        }
        else if (g_raceType == RACE_TIMEATTACK) {
            if (g_raceSubMode == SUBMODE_NORMAL) {
                if (placement > 0) {
                    CompareAndUpdateBestB(i, &lapBase[i*3], &g_gpCharDetail[charBase + 15 + trkIdx]); /* +0x3C */
                }
                if (placement > 1) {
                    CompareAndUpdateBestB(i, &lapBase[i*3+1], &g_gpCharDetail[charBase + 15 + trkIdx]);
                }
                if (placement > 2) {
                    CompareAndUpdateBestB(i, &lapBase[i*3+2], &g_gpCharDetail[charBase + 15 + trkIdx]);
                }
                if (placement == 3) {
                    CompareAndUpdateBestB(i, &totBase[i], &g_gpCharDetail[charBase + 10 + trkIdx]); /* +0x28 */
                }
            }
            else if (g_raceSubMode == SUBMODE_REVERSE) {
                if (placement > 0) {
                    CompareAndUpdateBestB(i, &lapBase[i*3], &g_gpCharDetail[charBase + 25 + trkIdx]); /* +0x64 */
                }
                if (placement > 1) {
                    CompareAndUpdateBestB(i, &lapBase[i*3+1], &g_gpCharDetail[charBase + 25 + trkIdx]);
                }
                if (placement > 2) {
                    CompareAndUpdateBestB(i, &lapBase[i*3+2], &g_gpCharDetail[charBase + 25 + trkIdx]);
                }
                if (placement == 3) {
                    CompareAndUpdateBestB(i, &totBase[i], &g_gpCharDetail[charBase + 20 + trkIdx]); /* +0x50 */
                }
            }
        }
    }

    /* Final: check all-complete condition */
    /* 0x4c4ccd */
    g_gpResultFlag = 0;                                              /* 0x6da618 */
    if (g_raceType == RACE_GP) {
        /* Check if all 10 extended track status entries are 2 */
        int allTracks = 1;
        for (int i = 0; i < 10; i++) {
            if (g_charUnlockTable[i] != 2) {                      /* 0x8fba64 + i*4 */
                allTracks = 0;
                break;
            }
        }
        if (allTracks) {
            /* Check if all 7 character unlock slots are 2 */
            int allChars = 1;
            for (int i = 0; i < 7; i++) {
                if (g_allCharsUnlocked != 0) {
                    break;  /* check g_charUnlockState */
                }
                if (g_charUnlockState[i] != 2) {                    /* 0x8fba8c */
                    allChars = 0;
                    break;
                }
            }
            if (allChars && allTracks) {
                g_gpResultFlag = 1;                                  /* 0x6da618 */
            }
        }
    }
}

/* Data tables extracted from binary (DGROUP section) */

/* Per-track unlock character indices: [trackId] → {charA, charB}, -1 = none */
/* 0x503d8c, stride 8 bytes */
static const int s_unlockPerTrack[][2] = {
    { 1600000, 1600000 }, /* trackId 0: unused padding */
    {  0, -1 },           /* trackId 1 (Resort Island): Amy */
    {  2,  1 },           /* trackId 2 (Radical City): Metal Sonic, Robotnik */
    {  3,  4 },           /* trackId 3 (Regal Ruin) — swap-family 0x503d8c, was binary slot [4] */
    {  5,  6 },           /* trackId 4 (Reactive Factory) — swap-family 0x503d8c, was binary slot [3] */
    {  0,  0 },           /* trackId 5: unused */
};

/* Character text screen positions: [charIdx] → {x, y, z} */
/* 0x503c54, stride 12 bytes */
static const int s_emeraldSlotPos[][3] = {
    { -111, -25, 675 },
    {  -37, -25, 675 },
    {   37, -25, 675 },
    {  111, -25, 675 },
    {  -75, -75, 700 },
    {    0, -75, 700 },
    {   75, -75, 700 },
};

/* Per-emerald RGB color tint: [emeraldIdx] → {R, G, B}.
 * 7 chaos emeralds, each with a distinct hue. Values are 13-bit fixed-point
 * (>>13 yields 0-255). 0x503d40, stride 12 bytes. */
static const int s_emeraldColor[][3] = {
    {       0, 1600000, 1600000 },  /* cyan   */
    {       0, 1600000,       0 },  /* green  */
    { 1600000,       0, 1600000 },  /* magenta*/
    { 1600000, 1600000,       0 },  /* yellow */
    { 1600000,  800000,       0 },  /* orange */
    { 1600000,       0,       0 },  /* red    */
    { 1600000, 1600000, 1600000 },  /* white  */
};

/* Small-emerald sprite UV frames in EM00.RAW: [frame 0-7] → {texU, texV}.
 * Right half of EM00, 4 rows × 2 cols of 64×48 cells. 0x503cc0, stride 8. */
static const int s_emeraldFrameSmall[][2] = {
    { 124,   0 }, { 124,  48 }, { 124,  96 }, { 124, 144 },
    { 188,   0 }, { 188,  48 }, { 188,  96 }, { 188, 144 },
};

/* Large-emerald sprite UV frames in EM01.RAW: [frame 0-7] → {texU, texV}.
 * Full EM01, 4 rows × 2 cols of 80×64 cells. 0x503d00, stride 8. */
static const int s_emeraldFrameLarge[][2] = {
    {  0,   0 }, {  0,  64 }, {  0, 128 }, {  0, 192 },
    { 80,   0 }, { 80,  64 }, { 80, 128 }, { 80, 192 },
};

/* Single values from binary data section */
#define EMERALD_LARGE_POS1_X   182   /* 0x503ca8 — screen X of first large-emerald slot (top row) */
#define EMERALD_LARGE_POS2_X   272   /* 0x503cb4 — screen X of second large-emerald slot */
#define EMERALD_DROP_INITIAL_Y 40    /* 0x503cac — initial Y for drop-in animation of large emeralds */

/* Forward declarations (not in sonicr_functions.h) */

void platform_pump_events(void);
void platform_sleep_ms(int ms);
void ColorizeTpageHiColor(int r, int g, int b);   /* 0x488310 */
void SubmitSpriteQuad(int screenX, int screenY, int depthBucket, int width,
                       int height, int flags, int texU, int texV,
                       int texW, int texH, int tpage, int vertColor,
                       int vertAlpha, int vertDepth);   /* 0x44c220 */

/* Externs for unlock screen */
/* g_charUnlockState is a #define into g_saveBlock via sonicr_globals.h */
/* g_racePlacement is a macro from sonicr_globals.h — aliases player[0].racePosition */

/* Screen-local globals repurposed per-screen */
static int s_emeraldTpageSmall;           /* 0x006D9814 — EM00.RAW: text + small emerald grid */
static int s_emeraldTpageLarge;           /* 0x006D9818 — EM01.RAW: large emerald grid */
#define s_emeraldDropPos1  g_stateBlock92528C[1]   /* 0x925290 */
#define s_emeraldDropVel1  g_stateBlock92528C[4]   /* 0x92529C */
#define s_emeraldDropPos2  g_stateBlock92528C[7]   /* 0x9252A8 */
#define s_emeraldDropVel2  g_stateBlock92528C[10]   /* 0x9252B4 */
#define s_savedDisplayConfig g_splashPrevState

/* Pack three 13-bit fixed-point chaos-emerald color components into one
 * ARGB vertex color (full alpha). Components clamp to 0xFF. */
static inline unsigned int emerald_argb(int r13, int g13, int b13)
{
    int r = r13 >> 13;
    if (r > 0xFF) {
        r = 0xFF;
    }
    if (r < 0) {
        r = 0;
    }
    int g = g13 >> 13;
    if (g > 0xFF) {
        g = 0xFF;
    }
    if (g < 0) {
        g = 0;
    }
    int b = b13 >> 13;
    if (b > 0xFF) {
        b = 0xFF;
    }
    if (b < 0) {
        b = 0;
    }
    return 0xFF000000u | ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b;
}

/**
 * RenderEmeraldSmall — FUN_004c85c8 — 140 bytes
 * Renders one small emerald sprite at slot `emeraldIdx`. Used for the
 * bottom row (all-time chaos-emerald collection).
 */
static void RenderEmeraldSmall(int emeraldIdx)
{
    /* Screen position from per-slot table */
    int rawX = s_emeraldSlotPos[emeraldIdx][0];                     /* 0x503c54 */
    int screenX = (rawX - 0x20) * 2 + 0x140;                        /* (x-32)*2 + 320 */

    int rawY = s_emeraldSlotPos[emeraldIdx][1];                     /* 0x503c58 */
    int screenY = 0xF0 - (rawY + 0x18) * 2;                         /* 240 - (y+24)*2 */

    /* Animation frame from frame counter (8-frame sparkle cycle) */
    int frameIdx = (g_totalFrames >> 1) & 7;                        /* 0x8fb68c */
    int texU = s_emeraldFrameSmall[frameIdx][0];
    int texV = s_emeraldFrameSmall[frameIdx][1];

    unsigned int color = emerald_argb(s_emeraldColor[emeraldIdx][0],
                                      s_emeraldColor[emeraldIdx][1],
                                      s_emeraldColor[emeraldIdx][2]);

    /* Depth 100.0 (nearer than the 200.0 text layer) so the emerald renders
     * in front of the result text — mirrors the binary, where emeralds submit
     * via SubmitSpriteQuad (depthBucket 1) and text via Blit2DSprite (2D layer).
     * Our port flattens both to DrawTexturedQuad, so we encode the ordering as Z. */
    DrawTexturedQuad(screenX, screenY, 0x42C80000, 0x80,
                     0x60, s_emeraldTpageSmall,
                     texU, texV, 0x40, 0x30, color);
}

/**
 * RenderEmeraldLarge — FUN_004c8654 — 123 bytes
 * Renders one large emerald sprite at (posX, posY). Used for the top row
 * ("got this race"), with drop-in animation positioning by caller.
 */
static void RenderEmeraldLarge(int posX, int posY, int emeraldIdx)
{
    int screenX = posX * 2 - 0x50;                                  /* lea edi, [eax*2 - 0x50] */
    int screenY = posY * 2 - 0x40;                                  /* lea esi, [edx*2 - 0x40] */

    /* Animation frame (7-frame cycle on the 8-cell sheet) */
    int frameIdx = (g_totalFrames >> 1) % 7;                        /* idiv ecx=7 */
    int texU = s_emeraldFrameLarge[frameIdx][0];
    int texV = s_emeraldFrameLarge[frameIdx][1];

    unsigned int color = emerald_argb(s_emeraldColor[emeraldIdx][0],
                                      s_emeraldColor[emeraldIdx][1],
                                      s_emeraldColor[emeraldIdx][2]);

    /* Depth 100.0 (nearer than the 200.0 text layer) so the emerald renders
     * over the result text (e.g. "NO") — see RenderEmeraldSmall for the full
     * rationale (binary depthBucket 1 vs Blit2DSprite 2D layer). */
    DrawTexturedQuad(screenX, screenY, 0x42C80000, 0xA0,
                     0x80, s_emeraldTpageLarge,
                     texU, texV, 0x50, 0x40, color);
}

/**
 * ShowEmeraldUnlockScreen — 0x004C86D0 — 1625 bytes
 *
 * Called after a race to determine character unlocks and display a
 * notification screen. Checks g_raceCheckpoint bits to find which
 * characters should be unlocked based on track, applies them to
 * g_charUnlockSource[], then runs a visual presentation loop.
 */
void ShowEmeraldUnlockScreen(void)
{
    int gotEmerald1 = -1;  /* [ebp - 0x18] */
    int gotEmerald2;       /* esi */

    ResetInputState();                                               /* 0x47059c */

    /* Determine unlock characters from checkpoint flags */
    gotEmerald1 = -1;
    gotEmerald2 = -1;

    if (g_raceCheckpoint & 1) {                                      /* bit 0: main race won */
        gotEmerald1 = s_unlockPerTrack[g_trackId][0];                /* 0x503d8c */
    }
    if (g_raceCheckpoint & 2) {                                      /* bit 1: secondary objective */
        gotEmerald2 = s_unlockPerTrack[g_trackId][1];                /* 0x503d90 */
    }

    /* Copy current unlock state to working set */
    for (int i = 0; i < 7; i++) {
        g_charUnlockSource[i] = g_charUnlockState[i];               /* 0x8fba8c → 0x6d981c */
    }

    /* Apply unlocks if player got 1st place */
    if (g_playerBase[0].racePosition == 1) {                               /* 0x8fd54e >> 16 == 1 */
        if (gotEmerald1 != -1) {
            g_charUnlockSource[gotEmerald1] = 2;
        }
        if (gotEmerald2 != -1) {
            g_charUnlockSource[gotEmerald2] = 2;
        }
    }

    /* Set up rendering state */
    g_bgTintR = 0x7F;                                                /* 0x625c9c */
    g_bgTintG = 0xFF;                                                /* 0x625ca0 */
    g_bgTintB = 0xFF;                                                /* 0x625ca4 */

    s_emeraldTpageSmall = g_uiTexPage + 1;                               /* 0x6d9814 = 0x8f6c48 + 1 */
    s_emeraldTpageLarge = g_uiTexPage + 2;                               /* 0x6d9818 = 0x8f6c48 + 2 */

#ifdef SONICR_DC
    if (RunningFromSlowMedia()) PauseCD();   /* slow media: hold music so the load can't starve the stream */
#endif
    /* Load 3 tpages. Real paths extracted from the binary
     * (0x42b044 calls at 0x4c87cb, 0x4c87f0, 0x4c87ff with edx pointing
     * to string addresses 0x531541, 0x531554, 0x531568 respectively). */
    LoadTPageRGB(g_uiTexPage, DATA_DIR SEP "GENERAL" SEP "SONICR.RAW");      /* 0x4c87c1: mov edx, 0x531541 */
    ColorizeTpageHiColor(g_bgTintR, g_bgTintG, g_bgTintB);      /* 0x488310 */
    LoadTPageRGB(s_emeraldTpageSmall, DATA_DIR SEP "BIN" SEP "RESULT" SEP "EM00.RAW"); /* 0x4c87e6: mov edx, 0x531554 */
    LoadTPageRGB(s_emeraldTpageLarge, DATA_DIR SEP "BIN" SEP "RESULT" SEP "EM01.RAW"); /* 0x4c87f5: mov edx, 0x531568 */
#ifdef SONICR_DC
    if (RunningFromSlowMedia()) ResumeCD();
#endif

    /* Initialize fade and animation state */
    g_fadeLevel = (int)0xFFFFFF00;                                   /* 0x901c44 */
    g_fadeState = 1;                                                 /* 0x901c48 — FADE_IN */
    g_fadeSpeed = 0x0C;                                              /* 0x901c4c */
    g_totalFrames = 0;                                               /* 0x8fb68c */
    g_renderEnabled = 1;                                             /* 0x8fb81c */

    s_emeraldDropVel1 = 0;                                            /* 0x92529c */
    s_emeraldDropPos2 = EMERALD_DROP_INITIAL_Y << 8;                        /* 0x9252a8 */
    s_emeraldDropPos1 = EMERALD_DROP_INITIAL_Y << 8;                        /* 0x925290 */
    s_emeraldDropVel2 = 0;                                            /* 0x9252b4 */

    /* Determine presentation mode based on placement */
    int presentMode;                                                 /* [ebp - 0x1c] */
    if (g_playerBase[0].racePosition == 1) {
        presentMode = 0x14;  /* 20: unlock screen with chars */
    }
    else {
        presentMode = 0x13;  /* 19: no unlock, just fade */
    }

    /* Step 8: Main display loop */
    /* 0x95058c */
    DWORD timerStart = timeGetTime() / 1000;                    /* [ebp - 0x20] */

    g_currentFPS = 30;  /* initialize */

    for (;;) {
        platform_pump_events();

        DWORD now = timeGetTime();
        g_currentTime = now;
        DWORD elapsed = now / 1000 - timerStart;
        int absElapsed = (int)(elapsed >= 0 ? elapsed : -elapsed);

        /* Check CD playback */
        UpdateCDPlayback(presentMode);                           /* 0x4d01ac */
        if (GetLogicalCDTrack() == presentMode) {
            /* CD track reached target */
        }

        /* Update fade */
        if (g_fadeState != 0) {
            UpdateFade();                                        /* 0x4305d4 */
        }

        /* Check for exit: fade complete to black */
        if (g_fadeLevel == (int)0xFFFFFF00) {                    /* fully faded out */
            StopCD();                                            /* 0x4d0264 */
            return;                                              /* originally returns 1 */
        }

        /* Check for auto-advance or user input */
        if (g_fadeState == 0 && absElapsed > 6) {                /* 6 seconds timeout */
            g_fadeState = 2;  /* start fade out */
        }

        /* Save display config and read input */
        s_savedDisplayConfig = g_softDoubleBuf;                  /* 0x68af6c = 0x8fd478 */
        ReadInput();                                             /* 0x477228 */

        /* If placement > 1 AND elapsed > 2 sec: advance animations */
        if (g_playerBase[0].racePosition > 1 && absElapsed > 2) {
            s_emeraldDropVel1 += 0x40;
            s_emeraldDropPos1 += s_emeraldDropVel1;
            if (absElapsed > 3) {
                s_emeraldDropVel2 += 0x40;
                s_emeraldDropPos2 += s_emeraldDropVel2;
            }
        }

        /* This screen owns its own track: the per-frame
         * UpdateCDPlayback(presentMode) above drives the fanfare, and the
         * music backend won't replay a finished one-shot (fanfare guard in
         * play_track), so it plays once and stays silent. */

        /* Render frame */
        /* GL clear + state setup. Without ProcessTpageStates the depth
         * buffer holds prior race-scene Z and the 2D quads here
         * fail the depth test → blank framebuffer. Mirrors every
         * other 2D screen in the port (LoadSaveScreen et al). */
        ProcessTpageStates();
        BeginFrame();

        /* Wobbly Sonic R background — drawn first so foreground
         * sprites (emeralds + text) layer on top. */
        RenderWavingMenuBackground();                              /* 0x004C68B8 — software twin */

        /* Render all collected emeralds */
        for (int i = 0; i < 7; i++) {
            if (g_charUnlockSource[i] == 2) {
                RenderEmeraldSmall(i);                     /* 0x4c85c8 */
            }
        }

        /* Render newly collected emerald 1 */
        if (gotEmerald1 != -1) {
            RenderEmeraldLarge(
                EMERALD_LARGE_POS1_X, s_emeraldDropPos1 >> 8,
                gotEmerald1);                                /* 0x4c8654 */
        }

        /* Render newly collected emerald 2 */
        if (gotEmerald2 != -1) {
            int posX, posY;
            if (gotEmerald1 == -1) {
                posX = EMERALD_LARGE_POS1_X;
                posY = s_emeraldDropPos1 >> 8;
            } else {
                posX = EMERALD_LARGE_POS2_X;
                posY = s_emeraldDropPos2 >> 8;
            }
            RenderEmeraldLarge(posX, posY, gotEmerald2); /* 0x4c8654 */
        }

        /* Text sprites from EM00.RAW. Each quad samples a different
         * row of the tpage; binary push order at 0x4c8a69-0x4c8b24
         * has uvY values {0, 0x20, 0x60, 0xC0}, not uvX as a prior
         * translation pass had it. */
        DrawTexturedQuad(0x20, 0x28, 0x43480000, 0x74,
                         0x38, s_emeraldTpageSmall, 0, 0,
                         0x3A, 0x1C, VERTEX_WHITE);

        DrawTexturedQuad(0xA4, 0x28, 0x43480000, 0x74,
                         0x38, s_emeraldTpageSmall, 0, 0x20,
                         0x3A, 0x1C, VERTEX_WHITE);

        /* Third quad only when racePlacement > 1 
         * renders the "NO" that appears when the emeralds drop */
        if (g_playerBase[0].racePosition > 1) {
            DrawTexturedQuad(0x140, 0x28, 0x43480000, 0x60,
                             0x38, s_emeraldTpageSmall, 0, 0x60,
                             0x30, 0x1C, VERTEX_WHITE);
        }

        /* "Chaos Emerald(s)" — bottom band of EM00 */
        DrawTexturedQuad(0x40, 0x80, 0x43480000, 0x200,
                         0x40, s_emeraldTpageSmall, 0, 0xC0,
                         0x100, 0x20, VERTEX_WHITE);

        EndFrame();
        if (g_fadeLevel < 0) {
            RenderFadeOverlay();
        }
        FlipD3D();                                           /* 0x4356bc */

        /* Update frame counters */
        g_totalFrames2++;                                        /* 0x8fb690 */
        g_totalFrames++;                                         /* 0x8fb68c */

        /* Frame rate limiter */
        WaitForFrameCap();
    }
}
