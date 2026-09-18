/**
 * ghost.c — Ghost replay recording and playback
 *
 * Saves/loads .gho files with lap times.
 * The actual per-frame input recording is in input.c (UpdatePerPlayerInput).
 * See GhostReplay_annotated.c for full system documentation.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "sonicr_paths.h"
#include "endian_util.h"

#define P_INT(p, off) (*(int *)((char *)(p) + (off)))
#define PATH_SEP '/'

/* String tables for building ghost file paths */
/* Path: PATH_GHOST + lapConfigName + "/" + trackName + "/" + charName + ".gho" */
static const char *s_ghostDir1 = PATH_GHOST;  /* save path prefix */
static const char *s_ghostDir2 = PATH_GHOST;  /* load path prefix */

extern const char *g_raceSubModeNames[];   /* 0x4fbf0c — "normal", "reverse" */
extern const char *g_trackDirNames[];    /* 0x4fbf10 — "island","city","ruin","factory","emerald" */
extern const char *g_charGhoNames[];     /* 0x4fbf28 — "Sonic.gho" etc. */

/* File handle for ghost operations */
static FILE *s_ghostFile;   /* DAT_00625C00 */

/* Time record tables, storage in globals_extra.c. The post-race record update
 * in main.c drives the same three binary arrays, so they are one set of
 * symbols; index is charId + (trackId-1)*10 + lapConfig*50. */
extern unsigned char g_taRecordFlags[];   /* 0x911A08 — 1 if .gho file exists */
extern int g_taBestTotalTime[];           /* 0x911A6C — best 3-lap total */
extern int g_taBestLapTime[];             /* 0x911BFC — best single lap */


/**
 * Helper: build ghost file path into buffer
 */
static void BuildGhostPath(char *buf, const char *baseDir, int charIdx)
{
    char *dst = buf;

    /* Copy base dir */
    const char *src = baseDir;
    while (*src) {
        *dst++ = *src++;
    }

    *dst++ = PATH_SEP;

#ifdef SONICR_DC
    /* Dreamcast: collapse the character axis so there is one ghost per
     * (lapConfig, track) — 2 * 5 = 10 slots — which fits the VMU. The name
     * encodes the slot directly (G<NN>.GHO); the VMU shim in save_vmu.c
     * (classify -> K_GHOST) parses that back into a slot index. Every
     * character on a track therefore shares the one ghost/record. */
    {
        int slot = g_raceSubMode * 5 + (g_trackId - 1);   /* 0..9 */
        *dst++ = 'G';
        *dst++ = (char)('0' + slot / 10);
        *dst++ = (char)('0' + slot % 10);
        *dst++ = '.'; *dst++ = 'G'; *dst++ = 'H'; *dst++ = 'O';
        *dst = '\0';
    }
    (void)charIdx;
#else
    /* PC: full per-character matrix — lapConfigName/trackName/charName.gho */
    src = g_raceSubModeNames[g_raceSubMode];
    while (*src) {
        *dst++ = *src++;
    }

    *dst++ = PATH_SEP;

    src = g_trackDirNames[g_trackId];
    while (*src) {
        *dst++ = *src++;
    }

    *dst++ = PATH_SEP;

    src = g_charGhoNames[charIdx];
    while (*src) {
        *dst++ = *src++;
    }

    *dst = '\0';
#endif
}

/**
 * SaveGhostData — 0x0042E9A4 — 517 bytes
 *
 * Saves the recorded replay to a .gho file.
 * Format: total (4), best lap (4),
 *         frameCount (4), frameData (frameCount × 2 bytes).
 *
 * The binary builds both values in the same pass over the three lap times
 * (0x42E9AF-0x42E9F6): [ebp-0x18] accumulates the sum while [ebp-0x1c] keeps
 * the running minimum, and they are written in that order at 0x42EB40 and
 * 0x42EB52. LoadAllGhostTimes relies on that order — dword 0 feeds
 * g_taBestTotalTime, dword 1 feeds g_taBestLapTime.
 */
void SaveGhostData(void)
{
    unsigned int t1 = g_playerBase[0].lap1Time & 0xFFFFFF;
    unsigned int t2 = g_playerBase[0].lap2Time & 0xFFFFFF;
    unsigned int t3 = g_playerBase[0].lap3Time & 0xFFFFFF;

    unsigned int total = t1 + t2 + t3;

    unsigned int best = t1;
    if (t2 < best) {
        best = t2;
    }
    if (t3 < best) {
        best = t3;
    }

    char path[256];
    short charId = g_playerBase->charId;
    BuildGhostPath(path, s_ghostDir1, charId);

    s_ghostFile = (FILE *)fOpen(path, "wb");
    if (s_ghostFile != NULL) {
        bswap32_inplace(&total);
        bswap32_inplace(&best);
        fWrite(&total, 4, 1, s_ghostFile);                    /* 0x42EB40 */
        fWrite(&best, 4, 1, s_ghostFile);                     /* 0x42EB52 */

        /* Divergence from the binary: clamp to the buffer. Recording stops at
         * g_ghostMaxFrames (0x8000 / numViewports), which is larger than
         * g_taGhostBuffer, and main.c only ever copies GHOST_BUFFER_FRAMES
         * entries into it — so a run past that many frames would write out
         * whatever follows the buffer. The binary writes g_ghostTotalFrames
         * unchecked at 0x42EB7F. */
        int frameCount = g_ghostTotalFrames;
        if (frameCount < 0) {
            frameCount = 0;
        }
        if (frameCount > GHOST_BUFFER_FRAMES) {
            frameCount = GHOST_BUFFER_FRAMES;
        }

        int frameCountLE = frameCount;
        bswap32_inplace(&frameCountLE);
        fWrite(&frameCountLE, 4, 1, s_ghostFile);             /* 0x42EB6A */
        bswap16_arr(g_taGhostBuffer, frameCount);
        fWrite(g_taGhostBuffer, 2, frameCount, s_ghostFile);  /* 0x42EB7F */
        bswap16_arr(g_taGhostBuffer, frameCount);
        fClose(s_ghostFile);
    }
}

/**
 * LoadGhostData — 0x0042EBAC — 462 bytes
 *
 * Loads a ghost replay from a .gho file.
 * Reads frame count and replay input data into g_taGhostBuffer.
 * Sets g_ghostDataExists = 1 if file was found.
 *
 * The two header dwords are consumed and discarded — the binary reads both
 * into the same stack slot (0x42ECFF and 0x42ED11 both target [ebp-0x18]).
 * LoadAllGhostTimes is what actually keeps them.
 */
void LoadGhostData(void)
{
    char path[256];
    short charId = g_playerBase->charId;
    BuildGhostPath(path, s_ghostDir2, charId);

    FILE *fp = fOpen(path, "rb");
    int exists = (fp != NULL);

    if (exists) {
        int headerScratch;
        fRead(&headerScratch, 4, 1, fp);                       /* 0x42ECFF — total, discarded */
        fRead(&headerScratch, 4, 1, fp);                       /* 0x42ED11 — best lap, discarded */
        fRead(&g_ghostTotalFrames, 4, 1, fp);                  /* 0x42ED29 */
        bswap32_inplace(&g_ghostTotalFrames);

        /* Divergence from the binary: the frame count comes straight off disk
         * and sizes a read into g_taGhostBuffer, so a truncated or corrupt
         * .gho would overflow it. The binary reads it unchecked at 0x42ED3E.
         * Out of range means the file is not usable — reject it rather than
         * load a partial replay. */
        if (g_ghostTotalFrames < 0 || g_ghostTotalFrames > GHOST_BUFFER_FRAMES) {
            g_ghostTotalFrames = 0;
            exists = 0;
        }
        else {
            fRead(g_taGhostBuffer, 2, g_ghostTotalFrames, fp); /* 0x42ED3E */
            bswap16_arr(g_taGhostBuffer, g_ghostTotalFrames);
        }
        fClose(fp);
    }

    g_ghostDataExists = exists;
}

/* =====================================================================
 * LoadAllGhostTimes — 0x0042ED7C — 921 bytes
 *
 * Scans all ghost files across 2 lapConfigs × 5 tracks × 10 characters.
 * For each existing .gho file, reads the two header dwords into the time
 * record tables: dword 0 is the 3-lap total, dword 1 the best single lap.
 * Binary 0x42EFE8 stores the first into 0x911A6C, 0x42F005 the second into
 * 0x911BFC, matching the order SaveGhostData writes them (0x42EB40/0x42EB52,
 * where [ebp-0x18] accumulates the sum and [ebp-0x1c] keeps the running min).
 *
 * Then reconciles: for each character/track combo where a ghost exists,
 * if the ghost's time beats the current checkpoint table entry, updates it.
 *
 * Time record tables, indexed charId + (trackId-1)*10 + lapConfig*50:
 *   0x911A6C: g_taBestTotalTime[100] — best 3-lap total per char/track/config
 *   0x911BFC: g_taBestLapTime[100]   — best single lap per char/track/config
 *   0x911A08: g_taRecordFlags[100] (bytes) — 1 if ghost file exists
 *
 * Checkpoint table updates (inside g_saveBlock / g_cpTableA):
 *   Normal:  total → g_cpTableA[charIdx*41 + 11 + trackIdx]  (0x8FBCB0)
 *            lap   → g_cpTableA[charIdx*41 + 16 + trackIdx]  (0x8FBCC4)
 *   Reverse: total → g_cpTableA[charIdx*41 + 21 + trackIdx]  (0x8FBCD8)
 *            lap   → g_cpTableA[charIdx*41 + 26 + trackIdx]  (0x8FBCEC)
 *
 * Called from LoadGameState at startup (binary 0x4CDC03).
 * ===================================================================== */
void LoadAllGhostTimes(void)
{
    char pathBuf[256];

    /* Read all ghost files into scratch tables */
    for (int lapCfg = 0; lapCfg < 2; lapCfg++) {
        for (int trk = 0; trk < 5; trk++) {
            for (int chr = 0; chr < 10; chr++) {
                /* Build path: GHOST/<lapConfigName>/<trackDirName>/<charGhoName> */
                char *dst = pathBuf;
                const char *src;

                src = PATH_GHOST;
                while (*src) {
                    *dst++ = *src++;
                }
                *dst++ = '/';

#ifdef SONICR_DC
                /* DC: collapse to one ghost per (lapConfig, track) — see
                 * BuildGhostPath. All 10 characters map to the same slot, so
                 * every character's record for this track is filled from the
                 * one shared G<NN>.GHO. */
                {
                    int slot = lapCfg * 5 + trk;   /* 0..9 */
                    *dst++ = 'G';
                    *dst++ = (char)('0' + slot / 10);
                    *dst++ = (char)('0' + slot % 10);
                    *dst++ = '.'; *dst++ = 'G'; *dst++ = 'H'; *dst++ = 'O';
                    *dst = '\0';
                }
#else
                src = g_raceSubModeNames[lapCfg];
                while (*src) {
                    *dst++ = *src++;
                }
                *dst++ = '/';

                src = g_trackDirNames[trk + 1]; /* tracks 1-5 */
                while (*src) {
                    *dst++ = *src++;
                }
                *dst++ = '/';

                src = g_charGhoNames[chr];
                while (*src) {
                    *dst++ = *src++;
                }
                *dst = '\0';
#endif

                int taIdx = lapCfg * 50 + trk * 10 + chr;

                FILE *fp = fOpen(pathBuf, "rb");
                if (fp != NULL) {
                    fRead(&g_taBestTotalTime[taIdx], 4, 1, fp);
                    bswap32_inplace(&g_taBestTotalTime[taIdx]);
                    fRead(&g_taBestLapTime[taIdx], 4, 1, fp);
                    bswap32_inplace(&g_taBestLapTime[taIdx]);
                    fClose(fp);
                    g_taRecordFlags[taIdx] = 1;
                } else {
                    g_taRecordFlags[taIdx] = 0;
                    g_taBestTotalTime[taIdx] = 0;
                }
            }
        }
    }

    /* Reconcile normal lapConfig (index 0) best times
     * For each track × char with a ghost, update checkpoint table
     * entries at offsets 0x8FBCB0 (best total) and 0x8FBCC4 (best lap)
     * if the ghost time is better (smaller). */

    /* Column offsets within cpTableA rows (stride 41 ints = 0xA4 bytes per row) */
    int colTotal = (0x8FBCB0 - 0x8FBC84) / 4;  /* 11 */
    int colLap   = (0x8FBCC4 - 0x8FBC84) / 4;  /* 16 */

    for (int trk = 0; trk < 5; trk++) {
        for (int chr = 0; chr < 10; chr++) {
            int taIdx = trk * 10 + chr;

            if (!g_taRecordFlags[taIdx]) {
                continue;
            }

            int rowOff = chr * 41 + trk;  /* row = char, col = track */
            int ghostTotal = g_taBestTotalTime[taIdx];
            int ghostLap = g_taBestLapTime[taIdx];

            if (ghostTotal < g_cpTableA[rowOff + colTotal]) {
                g_cpTableA[rowOff + colTotal] = ghostTotal;
            }
            if (ghostLap < g_cpTableA[rowOff + colLap]) {
                g_cpTableA[rowOff + colLap] = ghostLap;
            }
        }
    }
    

    /* Phase 3: Reconcile reverse lapConfig (index 1) best times
     * Same logic, different checkpoint table columns and flag/time offsets. */
    colTotal = (0x8FBCD8 - 0x8FBC84) / 4; /* 21 */
    colLap   = (0x8FBCEC - 0x8FBC84) / 4; /* 26 */

    for (int trk = 0; trk < 5; trk++) {
        for (int chr = 0; chr < 10; chr++) {
            int taIdx = 50 + trk * 10 + chr;

            if (!g_taRecordFlags[taIdx]) {
                continue;
            }

            int rowOff = chr * 41 + trk;
            int ghostTotal = g_taBestTotalTime[taIdx];
            int ghostLap = g_taBestLapTime[taIdx];

            if (ghostTotal < g_cpTableA[rowOff + colTotal]) {
                g_cpTableA[rowOff + colTotal] = ghostTotal;
            }
            if (ghostLap < g_cpTableA[rowOff + colLap]) {
                g_cpTableA[rowOff + colLap] = ghostLap;
            }
        }
    }
}
