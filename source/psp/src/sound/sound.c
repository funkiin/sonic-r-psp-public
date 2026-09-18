/**
 * sound.c - Sound system functions
 *
 * CD audio (MCI) and DirectSound initialization/cleanup.
 * See Sound_annotated.c for full documentation.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "replay_voice.h"
#include <math.h>

/* MCI command constants. On Windows the real values come from <mciapi.h>
 * via <windows.h>; on other platforms we just need stand-ins so the names
 * resolve. Note: our MCI_STATUS=0x808 is actually MCI_STOP in real Windows
 * (real MCI_STATUS=0x0814) - latent bug, but unused at the moment. */
#ifndef _WIN32
#define MCI_OPEN    0x803
#define MCI_CLOSE   0x804
#define MCI_PLAY    0x806
#define MCI_STATUS  0x808
#define MCI_SET     0x80D
#endif

/* Sound channel state - contiguous block at 0x689760-0x689788 */
static int s_sndChanVol[3];           /* 0x689760 - per-channel volume (init 0x100) */
static int s_sndMaxVol;               /* 0x68976c - max volume for char type 1 (surface sound) */
static int s_sndMinDist;              /* 0x689770 - min distance for char type 1 */
static int s_sndCountA;               /* 0x689774 - engine sound A counter */
static int s_sndCountB;               /* 0x689778 - engine sound B counter */
static int s_sndVolC;                 /* 0x68977c - volume for char type 3 */
static int s_sndDistC;                /* 0x689780 - distance for char type 3 */
static int s_sndVolD;                 /* 0x689784 - volume for other types */
static int s_sndDistD;                /* 0x689788 - distance for other types */

extern void PlaySoundEffect(int soundCmd, int distance, int freqParam);

/* =====================================================================
 * UpdatePlayerSound3D - FUN_004828cc - 713 bytes - VALIDATED: capstone 2026-04-25
 *
 * Per-player 3D spatial audio processor.  Computes XZ distance from
 * player to camera, determines sound category by character type,
 * calculates volume and pan angle, calls PlaySound.
 *
 * Also updates per-sound-type volume/distance tracking globals at
 * 0x68976c-0x689788, which the caller (InitRaceStart) reads to
 * decide which aggregate sounds to play.
 *
 * Watcom fastcall: EAX=player, EDX=cam, ECX=soundParam, EBX=mode.
 * Returns 1 if sound was queued from the caller-provided soundParam,
 * 0 otherwise.
 * ===================================================================== */
int UpdatePlayerSound3D(Player *player, CamStateEntry *cam, /* 0x4828cc */
                        int soundParam, int mode)
{
    int retVal = 0;                                        /* [ebp-0x1c] */

    /* Distance: player XZ to camera XZ */
    int deltaX = (cam->posX >> 4) + (player->posX >> 12);  /* 0x4828e6 */
    int deltaZ = (cam->posZ >> 4) + (player->posZ >> 12); /* 0x4828f8 */
    int distSq = deltaX * deltaX + deltaZ * deltaZ;       /* 0x482909 */
    int dist = (int)sr_sqrtf((float)distSq);                  /* 0x482917: fild+fsqrt */
    dist = ((dist << 8) >> 12);                            /* 0x482927: shl 8, sar 0xc */
    if (dist >= 0x100) {
        return retVal;                      /* 0x48292d */
    }

    int ecxDist = dist;                                    /* 0x482939: saved for tracking */

    /* Mode 1: halve distance + offset */
    if (mode == 1) {                                        /* 0x48293b */
        dist = dist / 2 + 0x40;                           /* 0x48294f */
    }

    int soundId = -1;                                      /* [ebp-0x10] */
    int specialFlag = 0;                                   /* EDI */

    /* Per-character dispatch
     * Binary jump table at 0x4828ac (8 entries indexed by charId-1):
     *   case 0 (Tails       , charId 1) -> 0x48297b  surface block
     *   case 1 (Knuckles    , charId 2) -> 0x482a37  fall-through (no per-char handling)
     *   case 2 (Amy         , charId 3) -> 0x4829d0  ability block
     *   case 3 (Eggman      , charId 4) -> 0x482a16  engine block
     *   case 4 (Metal Sonic , charId 5) -> 0x482a16  engine block
     *   case 5 (Tails Doll  , charId 6) -> 0x482a37  fall-through (no per-char handling)
     *   case 6 (Metal Knux  , charId 7) -> 0x482a16  engine block
     *   case 7 (Egg Robo    , charId 8) -> 0x482a16  engine block
     */
    int charType = (unsigned short)player->charId;          /* 0x48295c */
    int ct = charType;                                 /* 0x482963 */

    if (ct <= 8) {
        switch (ct) {
            case CHAR_TAILS: /* Tails (charId 1) */                     /* 0x48297b - surface block */
                dist = dist / 2 + 0x40;                        /* 0x482986 */
                {
                    int surfType = (int)player->animId;        /* 0x482989: upper16 of int@0x96 = short@0x98 */
                    if (surfType == 0xD || surfType == 0xC) {  /* 0x482992 */
                        soundId = 0xB;                         /* 0x4829a0 */
                        specialFlag = 0x38;                    /* 0x4829a5 */
                        if (s_sndMaxVol < specialFlag) {       /* 0x4829b2 */
                            s_sndMaxVol = specialFlag;
                        }
                        if (ecxDist < s_sndMinDist) {          /* 0x4829bc */
                            s_sndMinDist = ecxDist;
                        }
                    }
                }
                break;

            case CHAR_AMY: /* Amy (charId 3) */                       /* 0x4829d0 - ability block */
                dist = dist / 2 + 0x40;                        /* 0x4829db */
                {
                    int state = (int)player->abilityState;     /* 0x4829de: upper16 of int@0xFC = short@0xFE */
                    if (state == 2) {                          /* 0x4829ea */
                        soundId = 0x11;                        /* 0x482a06 */
                        s_sndCountB++;                         /* 0x482a0f */
                    } else {
                        soundId = 5;                           /* 0x4829f1 */
                        s_sndCountA++;                         /* 0x4829fa */
                    }
                }
                break;

            case CHAR_EGGMAN: /* Eggman        (charId 4) */             /* 0x482a16 - engine block */
            case CHAR_METAL_SONIC: /* Metal Sonic   (charId 5) */
            case CHAR_METAL_KNUCKLES: /* Metal Knuckles(charId 7) */
            case CHAR_EGG_ROBO: /* Egg Robo      (charId 8) */
                if (player->yOffset == 0) {                    /* 0x482a16 */
                    dist = dist / 2 + 0x40;                    /* 0x482a27 */
                    soundId = soundParam;                      /* 0x482a2a */
                    retVal = 1;                                /* 0x482a30 */
                }
                break;

            /* case 1 (Knuckles)   - binary 0x482a37: fall-through, no per-char handling */
            /* case 5 (Tails Doll) - binary 0x482a37: fall-through, no per-char handling */
        }
    }

    /* Volume computation (skipped for surface sounds) */
    if (specialFlag == 0 && soundId != -1) {               /* 0x482a37 */
        int vol = player->forwardSpeed >> 10;               /* 0x482a49 */
        if (player->_unk_0x6E != 0) {                       /* 0x482a4c */
            vol += 0x40;                                   /* 0x482a58 */
        }

        if (player->abilityState == 3) {                    /* 0x482a5b: upper16 of int@0xFC = short@0xFE */
            vol = 0xE0;                                    /* 0x482a69 */
        }

        /* Add jitter from global random stream at [0x901cd0] */
        {
            extern unsigned short *g_randomStream;         /* 0x901cd0 */
            int jitter = (*(unsigned char *)g_randomStream) & 0x1F;  /* 0x482a6e */
            vol += jitter;                                 /* 0x482a86 */
        }

        if (vol < 0x20) {
            vol = 0x20;                        /* 0x482a88 */
        }
        if (vol > 0xFF) {
            vol = 0xFF;                        /* 0x482a92 */
        }

        /* Per-character-type volume/distance tracking */
        int ct2 = (short)player->charId;                    /* 0x482a9e */
        if (ct2 == CHAR_TAILS) {                                    /* 0x482aa5 */
            if (vol > s_sndMaxVol) {
                s_sndMaxVol = vol;      /* 0x482aaa */
            }
            if (ecxDist < s_sndMinDist) {
                s_sndMinDist = ecxDist;  /* 0x482ab7 */
            }
        }
        else if (ct2 == CHAR_AMY) {                             /* 0x482ac7 */
            if (vol > s_sndVolC) {
                s_sndVolC = vol;          /* 0x482ad3 */
            }
            if (ecxDist < s_sndDistC) {
                s_sndDistC = ecxDist;  /* 0x482ae0 */
            }
        }
        else {                                           /* 0x482af0 */
            if (vol > s_sndVolD) {
                s_sndVolD = vol;          /* 0x482af0 */
            }
            if (ecxDist < s_sndDistD) {
                s_sndDistD = ecxDist;  /* 0x482afd */
            }
        }
    }

    /* Bail if no sound and no active animation */
    if (soundId == -1) {                                   /* 0x482b0b */
        if (player->sfxTrigger == (short)0xFFFF) {         /* 0x482b11: upper16 of int@0xE8 = short@0xEA */
            return retVal;                                 /* 0x482b1d */
        }
    }

    /* Pan angle: atan2(deltaX, deltaZ) → 12-bit, minus camera yaw */
    sr_double angle = sr_atan2((sr_double)deltaX, (sr_double)deltaZ);  /* 0x482b1f */
    int iAngle = (int)(angle * 4096.0 * 0.15915494327375637);  /* 0x482b2a */
    iAngle = ((iAngle << 4) >> 4);                         /* 0x482b47: sign-extend 28-bit */
    int camYaw = cam->smoothPitch;                          /* 0x482b44: cam[0xC] >> 16 */
    int pan = (iAngle - camYaw) & 0xFFF;                   /* 0x482b50 */

    /* Play sound if animation is active */
    if (player->sfxTrigger != (short)0xFFFF) {              /* 0x482b52: upper16 of int@0xE8 = short@0xEA */
        /* Binary: EAX=sfxTrigger, EBX=dist&0xFFFF, ECX=0, EDX=pan&0xFFFF */
        (void)pan;    /* EDX - only used by no-op FUN_00496a44 */
        PlaySoundEffect((unsigned short)player->sfxTrigger,
                     dist & 0xFFFF, 0);                    /* 0x482b76 */
        player->sfxTrigger = (short)0xFFFF;                /* 0x482b82 */
    }

    return retVal;                                         /* 0x482b8b */
}

extern void ClearPlayerSFXState(void);                    /* 0x496ac8 */
int SoundStop(int soundId);                                /* 0x4d0410 - defined at end of file */

/* ROM sound parameter table (DGROUP 0x5015F4, 30 shorts) */
static const short s_romSoundParams[] = {
    6, 44, 48, 6, 44, 0,
    10, 0, 16, 0, 11, 0,
    8, 0, 13, 0, -9, -1,
    12, 0, 18, 0, 11, 0,
    8, 0, 12, 0, 32, 0,
};

/* =====================================================================
 * UpdateRaceSFX - FUN_00482674 - 565 bytes - per-frame from UpdateLapCounter 
 * VALIDATED: capstone 2026-04-25
 *
 * Race start sequence: resets player animations, initializes sound
 * channels, calls UpdatePlayerSound3D per player to evaluate spatial
 * audio, then dispatches aggregate sounds (surface, engine, other).
 *
 * Three paths based on game mode and network state:
 *   - Single player: sound for player 0 only
 *   - Network/split multiplayer: sound for local player only
 *   - Local multiplayer: sound for all racers, ROM table advances
 *     per player based on UpdatePlayerSound3D return value
 *
 * ROM sound param table at 0x5015f4: array of shorts, one per racer.
 * ===================================================================== */
void UpdateRaceSFX(void)                                   /* 0x482674 */
{
    ClearPlayerSFXState();                                 /* 0x48267f */

    /* Init channel volumes to 0x100 */
    s_sndChanVol[0] = 0x100;                               /* 0x689760 */
    s_sndChanVol[1] = 0x100;                               /* 0x689764 */
    s_sndChanVol[2] = 0x100;                               /* 0x689768 */

    /* Init distance tracking to max (0x100), counters/volumes to 0 */
    s_sndMinDist = 0x100;                                  /* 0x689770 */
    s_sndDistC   = 0x100;                                  /* 0x689780 */
    s_sndDistD   = 0x100;                                  /* 0x689788 */
    s_sndCountA  = 0;                                      /* 0x689774 */
    s_sndCountB  = 0;                                      /* 0x689778 */
    s_sndVolC    = 0;                                      /* 0x68977c */
    s_sndVolD    = 0;                                      /* 0x689784 */
    s_sndMaxVol  = 0;                                      /* 0x68976c */

    const short *romTable = s_romSoundParams;              /* 0x5015f4 */
    int raceType = g_raceType;                             /* [0x8fb950] */

    /* Single player / demo */                             /* 0x4826de */
    if (raceType != RACE_MULTIPLAYER) {
        int soundParam = s_romSoundParams[0];              /* binary: *(int*)0x5015f2 >> 16 = first table entry */
        UpdatePlayerSound3D(g_playerBase,
                            &g_camStateTable[0],
                            soundParam, 0);                /* 0x4826f1 */
        /* Original has an empty loop 1..g_numPlayers (no-op) */
    }
    /* Network or split-screen multiplayer */              /* 0x48272f */
    else if (g_netSessionActive != 0 || g_isNetworkGame != 0) {
        int lp = (int)(unsigned short)g_localPlayerIndex;  /* 0x68acdc */
        CamStateEntry *cam = &g_camStateTable[lp];
        Player *player = g_playerBase + lp;
        int soundParam = romTable[0];                      /* *(short*)romTable */

        UpdatePlayerSound3D(player, cam, soundParam, 0);   /* 0x48276f */
        /* Original has an empty loop 0..g_numViewports (no-op) */
    }
    /* Local multiplayer (no network, no split) */         /* 0x482794 */
    else {
        CamStateEntry *cam = &g_camStateTable[0];          /* 0x902140 */
        Player *player = g_playerBase;                     /* 0x8fd4f4 */

        for (int i = 0; i < g_numViewports; i++) {         /* 0x4827e0 */
            int soundParam = *romTable;                    /* 0x4827b4 */
            int used = UpdatePlayerSound3D(player, cam,
                                           soundParam, 0); /* 0x4827b7 */
            player++;                                      /* 0x4827bc: += PLAYER_STRIDE */
            romTable += used;                              /* 0x4827ca: advance if sound used */
            cam++;                                         /* 0x4827dd: +0x28 = next entry */
        }
    }

    /* Sound dispatch: play or stop aggregate sounds */

    /* Surface sound (char type 1) - EBX=minDist, ECX=maxVol */
    if (s_sndMaxVol != 0) {                                /* 0x4827e4 */
        PlaySoundEffect(0x1000B, s_sndMinDist, s_sndMaxVol);     /* 0x4827fc */
    }
    else {
        SoundStop(0xB);                                    /* 0x482808 */
    }

    /* Engine sound A (char type 3, non-hover) - EBX=distC, ECX=volC */
    if (s_sndCountA != 0) {                                /* 0x48280d */
        PlaySoundEffect(0x10005, s_sndDistC, s_sndVolC);         /* 0x482829 */
        if (s_sndCountB == 0) {                            /* 0x48282e */
            SoundStop(0x11);                               /* 0x48283c */
        }
    }

    /* Engine sound B (char type 3, hover) - EBX=distC, ECX=volC */
    if (s_sndCountB != 0) {                                /* 0x482841 */
        PlaySoundEffect(0x10011, s_sndDistC, s_sndVolC);         /* 0x48285d */
        if (s_sndCountA == 0) {                            /* 0x482862 */
            SoundStop(5);                                  /* 0x482870 */
        }
    }

    /* Other character sound - EBX=distD, ECX=volD */
    if (s_sndVolD != 0) {                                  /* 0x482875 */
        PlaySoundEffect(0x10006, s_sndDistD, s_sndVolD);         /* 0x48888e */
    }
    else {
        SoundStop(6);                                      /* 0x48289a */
    }
}

/* =====================================================================
 * InitSoundOffsetTables - 0x004D1040 - 398 bytes
 *
 * Fills two byte-offset stride tables used by the DirectSound buffer
 * management system. Each entry is index × 8.
 *
 *   g_sndBufOffsets[26] at 0x6DA4A4: offsets 0,8,16,...,200 (26 sound slots)
 *   g_sndChanOffsets[10] at 0x6DA3E0: offsets 0,8,16,...,72  (10 channels)
 *
 * Called from LoadGameState (binary 0x4CD974).
 * ===================================================================== */
static int g_sndBufOffsets[26];     /* 0x6DA4A4 */
static int g_sndChanOffsets[10];    /* 0x6DA3E0 */

void InitSoundOffsetTables(void)
{
    for (int i = 0; i < 26; i++) {
        g_sndBufOffsets[i] = i * 8;
    }
    for (int i = 0; i < 10; i++) {
        g_sndChanOffsets[i] = i * 8;
    }
}

/* =====================================================================
 * SoundStop - 0x004d0410 - 70 bytes
 * DirectSound: Stop sound buffer and reset position.
 * EAX = buffer index
 * Calls Stop() then SetCurrentPosition(0).
 * Returns 1 on success, 0 if buffer inactive.
 * ===================================================================== */
extern int g_soundActive[64];                              /* 0x006DA080 */

int SoundStop(int bufIndex)  /* EAX */
{
    if (g_soundActive[bufIndex] == 0) {
        return 0;
    }
    SFX_Stop(bufIndex);                                    /* vtable+0x48: Stop */
    g_surfaceLost = 0;                                     /* 0x4d0437: HRESULT (success) */
    return 1;
}

/* =====================================================================
 * Replay commentary music duck — enhancement, no binary counterpart.
 *
 * The announcer clip in slot 0x38 is exempt from the g_masterVolume
 * attenuation (0x4d07a6) so it already sits above the sound effects, which
 * the demo path drops to 3/4 on entry. Halving the music for the length of
 * the clip puts the voice clearly on top of the mix as well.
 *
 * Restore is deadline-based rather than "is the channel still playing":
 * SDL_mixer and KOS snd_sfx report one-shot completion differently, and a
 * deadline keeps both platforms on identical logic. The clip length comes
 * from the WAV itself via SFX_ClipDurationMs, so it tracks whichever of the
 * six REPLAYn files was loaded (they run 3.4s to 7.4s).
 * ===================================================================== */
#define MUSIC_DUCK_TAIL_MS 250   /* let the tail of the clip breathe before restoring */

static unsigned int s_musicDuckUntil;    /* timeGetTime() deadline */
static int          s_musicDucked;

void SFX_DuckMusic(int durationMs)
{
    if (durationMs <= 0) {
        return;
    }
    s_musicDuckUntil = timeGetTime() + (unsigned int)durationMs + MUSIC_DUCK_TAIL_MS;
    if (!s_musicDucked) {
        Music_SetDucked(1);
        s_musicDucked = 1;
    }
}

void SFX_DuckTick(void)
{
    if (!s_musicDucked) {
        return;
    }
    unsigned int now = timeGetTime();
    /* Signed difference so the comparison survives timeGetTime() wrapping.
     * Leaving the replay early restores immediately. */
    if ((int)(now - s_musicDuckUntil) >= 0 || g_demoMode != DEMO_REPLAY) {
        SFX_DuckStop();
    }
}

/* Force the music back to normal. SFX_DuckTick is the normal route and fires
 * when the clip's deadline passes; StopCD also calls this so a replay left
 * before the clip finishes can't strand the music at the ducked level — the
 * per-frame tick only runs inside the race loop. */
void SFX_DuckStop(void)
{
    if (!s_musicDucked) {
        return;
    }
    Music_SetDucked(0);
    s_musicDucked = 0;
    s_musicDuckUntil = 0;
}

/* =====================================================================
 * SetAllSoundVolumes - 0x004d0760 - 115 bytes
 * Iterates all 64 sound buffers and sets volume on each.
 * Volume = masterVol + (-masterVol / 8) * g_optSfxVolume
 * Skips buffer 0x38 if flag at 0x8fb8e4 is set.
 * ===================================================================== */
/* g_soundSystemActive is same memory as g_initFeatureB (0x006D9AE8) */
#define g_soundSystemActive g_initFeatureB
/* g_skipSoundFlag is same memory as g_demoMode (0x008FB8E4) */
#define g_skipSoundFlag g_demoMode

void SetAllSoundVolumes(void)
{
    if (g_soundSystemActive == 0) {
        return;
    }
    int vol = -g_volumeBase;
    int scaled = (vol / 8) * g_optSfxVolume;
    g_masterVolume = scaled + g_volumeBase;                /* 0x4d0796 */

    for (int i = 0; i < 64; i++) {                         /* 0x4d079d */
        if (g_skipSoundFlag != 0 && IS_REPLAY_VOICE_SLOT(i)) { /* 0x4d07a6 */
            continue;
        }
        if (g_soundBuffers[i] == NULL) {                   /* 0x4d07b4 */
            continue;
        }
        SFX_SetVolume(i, g_masterVolume);                  /* vtable+0x3c: SetVolume */
    }
}
