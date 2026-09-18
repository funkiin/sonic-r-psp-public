/**
 * game_loop.c — Game loop helper functions
 *
 * Functions called from the main game loop in WinMain.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "sonicr_paths.h"
#include "player_struct.h"
#include <stdarg.h>
#include <string.h>

extern void UpdatePalette(void);
extern void LoadTitleTextureD3D(void);
extern void SetupD3DTexturesBegin(void);
extern void FinalizeMenuTexturesD3D(void);
extern void LoadTPageRGB(int tpage, const char *filename);
extern void SetTitleTextureFile(const char *path);
extern void InitSceneryTpageA(void);
extern void InitSceneryTpageB(void);
extern void RemapCharacterTpages(void); /* FUN_00470460 */
extern void RemapBalloonTpages(void);   /* 0x00470564 */
extern void LoadCharacterGouraudTables(void);

extern void R_MarkTextureDirty(int tpage);
extern void GL_KeepPixels(int tpage);

extern void R_SetPendingRGBA(int tpage, unsigned char *rgba, int w, int h);

extern void R_MarkTextureDirty(int tpage);
extern void GL_KeepPixels(int tpage);

extern void StopCD(void);

extern int g_cdTrackTable[];
extern int g_cdTrackEmerald;

/* CD drive path and detection state */
static int s_prevCdAvailable; /* 0x006DA288 — previous g_cdAvailable value; local alias "prevState" in #if 0 block below is same address */

static int s_lastReplayIndex = 0;

/**
 * UpdateGameLogic — 0x004D0BC4 — 197 bytes
 * Manages CD audio track transitions during gameplay.
 */
void UpdateGameLogic(void)
{
    if (g_cdPlaybackState == 0 && g_postRaceCameraMode == 0 && g_introCountdown == 0x9D) {
        UpdateCDPlayback(3);
        g_cdPlaybackState = 1;
    }

    if (g_cdPlaybackState != 1) {
        goto check_state_2;
    }

    if (g_postRaceCameraMode == 0) {
check_playback:
        if (g_postRaceCameraMode != 0) {
            goto check_state_2;
        }

        if (GetLogicalCDTrack() == 3) {
            goto check_state_2;
        }
    }
    else {
        if (GetLogicalCDTrack() == 4) {
            goto check_playback;
        }
    }
    g_cdPlaybackState = 2;

check_state_2:
    if (g_cdPlaybackState == 2) {
        if (g_postRaceCameraMode == 0) {
            UpdateCDPlayback(g_cdPlaybackTarget);
        }
        else {
            StopCD();
        }

        g_cdPlaybackState = 3;
    }

    if (g_cdPlaybackState == 3 && g_postRaceCameraMode == 0) {
        if (GetLogicalCDTrack() != g_cdPlaybackTarget) {
            g_cdPlaybackState = 2;
        }
    }
}

/**
 * UpdateFrameTimers — 0x0047FDE8 — 189 bytes
 * Per-player random blink/expression timer.
 * Iterates all players, skips vehicle characters (charId 4, 6, 8).
 * For non-vehicle characters: manages expression state at player offset 0x204.
 *   Packed format: upper 16 bits = expression type, lower 16 bits = countdown.
 *   When countdown reaches 0, rolls Random() for a new expression:
 *     < 100 → type 1, duration 3 (0x10003)
 *     < 200 → type 2, duration 3 (0x20003)
 *     < 700 → type 3, duration 2 (0x30002)
 *     >= 700 → reset to 0 (no expression), skip decrement
 *   Otherwise decrements the packed timer by 1.
 */
void UpdateFrameTimers(void)
{
    Player *curPlayer = (Player *)g_playerBase;
    int i = 0;

    if (g_numPlayers <= 0) {
        return;
    }

    do {
        short charId = curPlayer->charId;

        /* Skip vehicle characters: Eggman (4), TDoll (6), EgRobo (8) */
        if (charId != CHAR_EGGMAN && charId != CHAR_TAILS_DOLL && charId != CHAR_EGG_ROBO) {
            if ((curPlayer->renderState & 0xFFFF) == 0) {
                /* Timer expired — roll for new expression */
                int rnd = Random();
                if (rnd < 200) {
                    if (rnd < 100) {
                        curPlayer->renderState = 0x10003;  /* type 1, countdown 3 */
                    } else {
                        curPlayer->renderState = 0x20003;  /* type 2, countdown 3 */
                    }
                } else {
                    if (rnd >= 700) {
                        /* No expression — reset and skip decrement */
                        if (curPlayer->renderState != 0)
                            curPlayer->renderState = 0;

                        goto next_player;
                    }
                    curPlayer->renderState = 0x30002;  /* type 3, countdown 2 */
                }
            }
            /* Decrement packed timer */
            curPlayer->renderState = curPlayer->renderState - 1;
        }
next_player:
        i++;
        curPlayer++;
    } while (i < g_numPlayers);
}

/**
 * LoadTextureSubRect — FUN_0042a848 — 114 bytes
 * Loads a W×H sub-rectangle of raw RGB pixels from a file into a tpage's
 * pixel buffer at position (destX, destY). The tpage buffer has a 256-pixel
 * stride. Converts from 24-bit RGB to the active 16bpp pixel format.
 *
 * Writes 16bpp into g_tpagePixelBuf for GL upload.
 *
 * Params (from x86 disasm):
 *   EAX = filename, EDX = dest buffer, ECX = height, EBX = width,
 *   [ebp+8] = destX, [ebp+0xc] = destY
 */
void LoadTextureSubRect(const char *filename, int tpage, int width, int height,
                        int destX, int destY)
{
    GL_KeepPixels(tpage);

    if (filename == NULL) {
        return;
    }

    FILE *fp = fOpen(filename, "rb");
    if (fp == NULL) {
        DebugLog("  Failed to open: %s\n", filename);
        return;
    }

    /* Ensure pixel buffer exists */
    if (g_tpagePixelBuf[tpage] == NULL) {
        g_tpagePixelBuf[tpage] = malloc(256 * 256 * 2);

        if (g_tpagePixelBuf[tpage] == NULL) {
            fClose(fp);
            return;
        }

        memset(g_tpagePixelBuf[tpage], 0, 256 * 256 * 2);
    }

    int tpageW = g_tpageWidth[tpage];
    if (tpageW <= 0) {
        tpageW = 256;
    }

    unsigned short *buf = (unsigned short *)g_tpagePixelBuf[tpage];

    size_t want = (size_t)width * (size_t)height * 3;
    unsigned char *raw = (unsigned char *)malloc(want);
    if (raw != NULL) {
        size_t got = fRead(raw, 1, want / 4, fp);
        got += fRead(raw + want / 4, 1, want / 4, fp);
        got += fRead(raw + (want / 4) + (want / 4), 1, want / 4, fp);
        got += fRead(raw + (want / 4) + (want / 4) + (want / 4), 1, want / 4, fp);

        int pixelsGot = (int)(got / 3);
        int p;
        for (int row = 0; row < height; row++) {
            for (int col = 0; col < width; col++) {
                p = row * width + col;

                if (p >= pixelsGot) {
                    free(raw);
                    goto done;
                }

                unsigned char r = raw[p*3+0], g = raw[p*3+1], b = raw[p*3+2];

                int dx = destX + col;
                int dy = destY + row;

                if (dx < tpageW && dy < 256) {
                    if (g_bitsPerPixel == 0x10) {
                        buf[dy * tpageW + dx] = (unsigned short)((int)b >> 3) |
                            (unsigned short)(((int)r >> 3) << 11) |
                            (unsigned short)(((int)g >> 3) << 6);
                    } else {
                        buf[dy * tpageW + dx] = (unsigned short)((int)b >> 3) |
                            (unsigned short)(((int)r >> 3) << 10) |
                            (unsigned short)(((int)g >> 3) << 5);
                    }
                }
            }
        }
        free(raw);
        goto done;
    }

    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            unsigned char r, g, b;
            if (fRead(&r, 1, 1, fp) != 1) {
                goto done;
            }
            fRead(&g, 1, 1, fp);
            fRead(&b, 1, 1, fp);
            int dx = destX + col;
            int dy = destY + row;
            if (dx < tpageW && dy < 256) {
                if (g_bitsPerPixel == 0x10) {
                    buf[dy * tpageW + dx] = (unsigned short)((int)b >> 3) |
                        (unsigned short)(((int)r >> 3) << 11) |
                        (unsigned short)(((int)g >> 3) << 6);
                } else {
                    buf[dy * tpageW + dx] = (unsigned short)((int)b >> 3) |
                        (unsigned short)(((int)r >> 3) << 10) |
                        (unsigned short)(((int)g >> 3) << 5);
                }
            }
        }
    }

done:
    fClose(fp);
    R_MarkTextureDirty(tpage);
}

/**
 * TintBackgroundTPage — FUN_004884b4 — 261 bytes
 * Builds a 256-entry color ramp from (R,G,B) and remaps every pixel
 * in the background tpage. Uses each pixel's R channel (byte[2] in the
 * original BGR D3D surface) as the ramp index.
 *
 * Re-reads the raw file at 8-bit precision, tints, and stores the result
 * directly as RGBA in a separate buffer that GL uploads at full quality.
 *
 * Params (Watcom fastcall): EAX=R, EDX=G, EBX=B (0-255 each).
 */
void TintBackgroundTPage(int tintR, int tintG, int tintB)
{
    int tpage = g_uiTexPage;
    int w = g_tpageWidth[tpage];
    int h = g_tpageHeight[tpage];
    if (w <= 0 || h <= 0) {
        w = 256;
        h = 256;
    }
    int pixelCount = w * h;

    /* Build 256-entry color ramp in 16.16 fixed point (matches original) */
    unsigned char ramp[768];
    int rStep = (tintR << 16) / 255;
    int gStep = (tintG << 16) / 255;
    int bStep = (tintB << 16) / 255;
    int rAcc = 0x8000, gAcc = 0x8000, bAcc = 0x8000;

    for (int i = 0; i < 256; i++) {
        ramp[i * 3 + 0] = (unsigned char)(rAcc >> 16);
        ramp[i * 3 + 1] = (unsigned char)(gAcc >> 16);
        ramp[i * 3 + 2] = (unsigned char)(bAcc >> 16);
        rAcc += rStep;
        gAcc += gStep;
        bAcc += bStep;
    }

    /* Re-read raw file at full 8-bit precision and produce RGBA at full quality.
     * Handed off to R_SetPendingRGBA for lazy upload — avoids the 16bpp
     * precision loss that caused visible banding. */
    FILE *fp = fOpen(PATH_GENERAL_RAW, "rb");
    if (fp == NULL) {
        return;
    }

    unsigned char *rgba = (unsigned char *)malloc(pixelCount * 4);
    if (rgba == NULL) {
        fClose(fp);
        return;
    }

    unsigned char *raw = (unsigned char *)malloc((size_t)pixelCount * 3);
    if (raw != NULL) {
        size_t got = fRead(raw, 1, (size_t)(pixelCount * 3) / 4, fp);
        got += fRead(raw + (size_t)(pixelCount * 3) / 4, 1, (size_t)(pixelCount * 3) / 4, fp);
        got += fRead(raw + (size_t)(pixelCount * 3) / 4 + (size_t)(pixelCount * 3) / 4, 1, (size_t)(pixelCount * 3) / 4, fp);
        got += fRead(raw + (size_t)(pixelCount * 3) / 4 + (size_t)(pixelCount * 3) / 4 + (size_t)(pixelCount * 3) / 4, 1, (size_t)(pixelCount * 3) / 4, fp);
        int pixelsGot = (int)(got / 3);
        for (int i = 0; i < pixelsGot; i++) {
            unsigned char b = raw[i*3+2];
            int idx = b * 3;
            unsigned char newR = ramp[idx + 0];
            unsigned char newG = ramp[idx + 1];
            unsigned char newB = ramp[idx + 2];
            if (newR == 0 && newG > 0xF7 && newB == 0) {
                newG = 0xF7;
            }
            rgba[i * 4 + 0] = newR;
            rgba[i * 4 + 1] = newG;
            rgba[i * 4 + 2] = newB;
            rgba[i * 4 + 3] = 255;
        }
        free(raw);
    } else
    for (int i = 0; i < pixelCount; i++) {
        unsigned char r, g, b;
        if (fRead(&r, 1, 1, fp) != 1) {
            break;
        }
        fRead(&g, 1, 1, fp);
        fRead(&b, 1, 1, fp);

        int idx = b * 3;  /* byte[2] per disasm: mov dl, byte ptr [eax + 2] */
        unsigned char newR = ramp[idx + 0];
        unsigned char newG = ramp[idx + 1];
        unsigned char newB = ramp[idx + 2];

        if (newR == 0 && newG > 0xF7 && newB == 0) {
            newG = 0xF7;
        }

        rgba[i * 4 + 0] = newR;
        rgba[i * 4 + 1] = newG;
        rgba[i * 4 + 2] = newB;
        rgba[i * 4 + 3] = 255;
    }
    fClose(fp);

    /* Hand off to GL for lazy upload during next render frame */
    R_SetPendingRGBA(tpage, rgba, w, h);  /* takes ownership of rgba */
}

/**
 * FindSonicRCD — 0x4d0f4c — 218 bytes
 * Checks if the Sonic R CD is present by testing for "sonr_cpy.txt"
 * on the CD drive. If found, opens the MCI CD audio device.
 * If not found and was previously available, closes the device.
 */
void FindSonicRCD(void)
{
    DebugLog("FindSonicRCD\n");

    g_cdAvailable = 1;
    s_prevCdAvailable = g_cdAvailable;
}

/**
 * HackTPageBrightness — FUN_00424d58 — 321 bytes
 * Multiplies texture pixel data by 1.5 (×6>>2) for brightness boost.
 * Original (FUN_00424d58, 321 bytes) operates on raw 3-byte RGB in D3D surface.
 *
 * From x86 disasm of call site (0x47080A-0x47081D):
 *   EAX = g_tpagePixelBuf[1] (env map tpage surface)
 *   EDX = 0 (column offset)
 *   EBX = 0 (row offset)
 *   ECX = 0x80 (height = 128 rows)
 *   stack = 0x80 (width = 128 columns)
 *
 * Processes sub-rect of a tpage surface at (colOffset,rowOffset)
 * within 256-wide surface.
 * Our version operates on 16bpp pixel buffer.
 */
void HackTPageBrightness(int tpage, int colOffset, int rowOffset, int height, int width)
{
    GL_KeepPixels(tpage);
    unsigned short *buf = (unsigned short *)g_tpagePixelBuf[tpage];
    if (buf == NULL) {
        return;
    }

    int tpageW = g_tpageWidth[tpage];
    if (tpageW <= 0) {
        tpageW = 256;
    }

    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            int idx = (rowOffset + row) * tpageW + (colOffset + col);
            unsigned short p = buf[idx];

            /* Extract 5-bit channels (our encoding: R[15:11] G[10:6] B[4:0]) */
            unsigned char r5 = (p >> 11) & 0x1F;
            unsigned char g5 = (p >> 6) & 0x1F;
            unsigned char b5 = p & 0x1F;

            /* Expand to 8-bit */
            int r8 = (r5 << 3) | (r5 >> 2);
            int g8 = (g5 << 3) | (g5 >> 2);
            int b8 = (b5 << 3) | (b5 >> 2);

            /* Skip color-key green (exact 0,255,0 — near-green is real art) */
            if (IS_COLOR_KEY_RGB5(r5, g5, b5)) {
                continue;
            }

            /* Multiply by 1.5: val * 6 >> 2, clamp to 255 */
            r8 = (r8 * 6) >> 2;
            if (r8 > 255) {
                r8 = 255;
            }

            g8 = (g8 * 6) >> 2;
            if (g8 > 255) {
                g8 = 255;
            }

            b8 = (b8 * 6) >> 2;
            if (b8 > 255) {
                b8 = 255;
            }

            /* Write back in our 16bpp encoding */
            buf[idx] = (unsigned short)((b8 >> 3)) |
                       (unsigned short)((r8 >> 3) << 11) |
                       (unsigned short)((g8 >> 3) << 6);
        }
    }

    R_MarkTextureDirty(tpage);
}

/**
 * InitTitleScreen — 0x00470654 — 539 bytes  (DebugLog: "InitTitleStuff"; loads bin\titles\titles3.bin)
 * Sets up the title/menu screen rendering environment.
 * Loads track 0 data, sets tpage assignments for menus,
 * loads menu textures, initializes fade state.
 */
void InitTitleScreen(void)
{
    DebugLog("InitTitleStuff\n");

    g_mirrorMode = 0;
    g_trackId = TRACK_NONE;
    /* Original: LoadTrack3 gets filename from EAX (Watcom register convention).
     * For track 0, EAX contains the TITLES3.BIN path (title screen 3D letters).
     * We set g_nextLoadFilename explicitly since our LoadTrack3 reads from it. */
    g_nextLoadFilename = PATH_TITLES3_BIN;
    LoadTrack3();

    /* Initialize object struct array entries */
    int i = 0;
    if (g_totalObjects > 0) { /* 0x6ead30 */
        int off = 0;
        do
        {
            char *obj = (char *)(intptr_t)g_objectStructArray + off;
            *(short *)(obj + 0x2E) = 2;
            *(int *)(obj + 0x20) = 0;
            *(int *)(obj + 0x24) = 0;
            *(int *)(obj + 0x28) = 0x300;
            *(short *)(obj + 0x18) = 0;
            *(short *)(obj + 0x1A) = 0;
            *(short *)(obj + 0x1C) = 0;
            *(int *)(obj + 0x00) = *(int *)(obj + 0x20);
            *(int *)(obj + 0x04) = *(int *)(obj + 0x24);
            *(int *)(obj + 0x08) = *(int *)(obj + 0x28);
            i++;
            off += 0x44;
        } while (i < g_totalObjects);
    }

    g_titleModelAngle = 0;
    /* Binary: EAX=EBX=EDX=0x80 before call. ECX=2 is preserved across call.
     * (0x80,0x80,0x80) → (0x80-0x80)/8=0 per channel, same result as (0,0,0)
     * in D3D mode (+0x60 added regardless), but matches binary exactly. */
    TintTrackGourauds(0x80, 0x80, 0x80);
    /* ECX=2 was set at 0x47070c and preserved through TintTrackGourauds
     * (push ecx / pop ecx). Stored to g_tpageCharacters at 0x470735. */
    g_tpageCharacters = 2;
    g_tpagePlayfield1 = 3;
    g_uiTexPage = 4;
    g_tpageParallax2 = 1;
    g_tpageParallax1 = 1;

    /* Mutators (LoadTextureSubRect overlay, HackTPageBrightness, results-
     * screen env-map patch) read existing pixel state from
     * g_tpagePixelBuf[g_tpageParallax2] after first upload. Mark keep
     * BEFORE the imminent ICON01 load so the buffer survives the eager-
     * upload free path. */
    GL_KeepPixels(g_tpageParallax2);

#ifdef SONICR_DC
    if (RunningFromSlowMedia()) PauseCD();   /* slow media: hold music so the load can't starve the stream */
#endif
    SetupD3DTexturesBegin();
    /* From x86 disasm of InitTitleScreen (0x470654), D3D path at 0x4707b7:
     * 4 LoadTPageRGB calls set up tpage surfaces, then LoadTextureSubRect
     * loads the 128×128 env map into tpage 1's surface at (0,0).
     * String addresses verified from PE: 0x52c6ea..0x52c753. */
    LoadTPageRGB(0, PATH_TITLES00_RAW);                 /* 0x4707c3: tpage 0 = title sprites */
    LoadTPageRGB(1, PATH_ICON01_RAW);                   /* 0x4707d2: tpage 1 = icon01 (env map base) */
    LoadTPageRGB(g_tpageCharacters, PATH_PLAYER00_RAW); /* 0x4707de: tpage 2 = player textures */
    LoadTPageRGB(3, PATH_PLAYER01_RAW);                 /* 0x4707ea: tpage 3 = player textures */
    /* Env map: overwrite top-left 128×128 of tpage 1 with sonicr.raw */
    LoadTextureSubRect(PATH_EMAP_SONICR, g_tpageParallax2,
                       128, 128, 0, 0);              /* 0x470805: 128×128 env map at (0,0) */
    HackTPageBrightness(g_tpageParallax2, 0, 0, 0x80, 0x80); /* 0x47081d: EAX=[0x625cc4], EDX=0, EBX=0, ECX=0x80, push 0x80 */
    RemapBalloonTpages();                                /* 0x470836 */
    SetTitleTextureFile(PATH_TITLES_RAW);
    LoadTitleTextureD3D();
    ProcessTpageStates();
    FinalizeMenuTexturesD3D();
#ifdef SONICR_DC
    ResumeCD();
#endif
    InitSceneryTpageA();
    InitSceneryTpageB();
    g_colorTintEnable = 0;
    g_fadeLevel = (int)0xFFFFFF00;
    g_fadeSpeed = 8;
    g_fadeState = FADE_IN;
}

/**
 * InitCD — 0x00471E8C
 * Initializes CD audio for a new screen/race.
 */
void InitCD(void)
{
    g_cdPlaybackState = 0;
    g_cdPlaybackTarget = 0;
    /* Reset CD playback state for the new context */
}

/**
 * InitOptionStuff — 0x0047096C — 818 bytes
 * Sets up the 3D environment for the option/menu screens.
 * Loads track 0 geometry from OPTION3.BIN, initializes all objects
 * at position (0, 0, 0x300) with mode 2, loads 17 texture pages
 * covering all track textures + player textures + option sprites,
 * and sets tpage assignments for the menu rendering pipeline.
 */
void InitOptionStuff(void)
{
    DebugLog("InitOptionStuff\n");

    StopCD();

    /* Load option screen 3D models from OPTION3.BIN */
    g_trackId = TRACK_NONE;
    g_nextLoadFilename = PATH_OPTION3_BIN;
    LoadTrack3();

    /* Initialize all objects: position=(0,0,0x300), mode=2, rotation=0 */
    int i = 0;
    int off = 0;
    while (i < g_totalObjects) {
        char *obj = (char *)g_objectStructArray + off;
        *(unsigned short *)(obj + 0x2E) = 2;         /* mode = 2 */
        *(int *)(obj + 0x20) = 0;                    /* target X = 0 */
        *(int *)(obj + 0x24) = 0;                    /* target Y = 0 */
        *(int *)(obj + 0x28) = 0x300;                /* target Z = 768 */
        *(unsigned short *)(obj + 0x18) = 0;         /* rotation X = 0 */
        *(unsigned short *)(obj + 0x1A) = 0;         /* rotation Y = 0 */
        *(unsigned short *)(obj + 0x1C) = 0;         /* rotation Z = 0 */
        *(int *)(obj + 0x00) = *(int *)(obj + 0x20); /* pos X = target X */
        *(int *)(obj + 0x04) = *(int *)(obj + 0x24); /* pos Y = target Y */
        *(int *)(obj + 0x08) = *(int *)(obj + 0x28); /* pos Z = target Z */
        i++;
        off += 0x44;
    }

    /* Load 17 texture pages — from x86 disasm of 0x470B22 */
#ifdef SONICR_DC
    if (RunningFromSlowMedia()) PauseCD();   /* slow media: hold music so the load can't starve the stream */
#endif
    SetupD3DTexturesBegin();
    LoadTPageRGB(0,  PATH_RUIN00_RAW);
    LoadTPageRGB(1,  PATH_RUIN03_RAW);
    LoadTPageRGB(2,  PATH_RUIN01_RAW);
    LoadTPageRGB(3,  PATH_FACT00_RAW);
    LoadTPageRGB(4,  PATH_FACT01_RAW);
    LoadTPageRGB(5,  PATH_FACT02_RAW);
    LoadTPageRGB(6,  PATH_ISLAND01_RAW);
    LoadTPageRGB(7,  PATH_ISLAND04_RAW);
    LoadTPageRGB(8,  PATH_ISLAND03_RAW);
    LoadTPageRGB(9,  PATH_ISLAND02_RAW);
    LoadTPageRGB(10, PATH_CITY02_RAW);
    LoadTPageRGB(11, PATH_CITY00_RAW);
    LoadTPageRGB(12, PATH_CAS00_RAW);
    LoadTPageRGB(13, PATH_MENU_OPTION00);
    LoadTPageRGB(14, PATH_PLAYER00_RAW);
    LoadTPageRGB(15, PATH_PLAYER01_RAW);
    LoadTPageRGB(16, PATH_ICON01_RAW);
#ifdef SONICR_DC
    ResumeCD();
#endif

    /* Tpage assignments for option/menu screens */
    g_tpageCharacters = 14;                              /* 0x0E */
    g_tpagePlayfield1 = 15;                              /* 0x0F */
    g_tpageParallax1 = 16;                               /* 0x10 */
    g_uiTexPage = 17;                                    /* 0x11 */
    g_tpageExtra = 8;
    g_tpageCount = 17;                                   /* must be above menu tpages 0-16 */

    InitSceneryTpageA();
    InitSceneryTpageB();
    RemapBalloonTpages();                                /* 0x470c5e */
    RemapCharacterTpages();                              /* remaps character face tpage bytes to
                                                          * g_tpageCharacters / g_tpagePlayfield1 */
    g_menuState = -1;
    g_colorTintEnable = 0;
    LoadCharacterGouraudTables();
    TintCharacterGouraudTables(0, 0, 0);
    /* Binary: EAX=EDX=EBX=0x60 at 0x470c8a-0x470c93 */
    TintTrackGourauds(0x60, 0x60, 0x60);                 /* line 31615 */
}

/* LoadTrackSinglePlayer — 0x004CD794 — 213 bytes
 * Cycles time-of-day variant, then selects weather (random or forced).
 * Binary: edx=weatherType, ebx=g_demoMode, ecx=g_weatherConfig */
void LoadTrackSinglePlayer(void)
{
    if (g_demoMode != DEMO_NONE) {
        return;                                     /* 0x4CD7A5: jne save */
    }

    /* Cycle time-of-day variant 0, 1, 2, 3, 0 each race */
    g_timeOfDay = (g_timeOfDay + 1) & 3;                           /* 0x4CD7B0-BA */

    /* Force clear weather for Radiant Emerald, certain race configs, or g_weatherConfig==WC_CLEAR */
    if (g_trackId == TRACK_RADIANT_EMERALD || g_weatherConfig == WC_CLEAR ||
        (g_raceType == RACE_TIMEATTACK && g_raceSubMode < SUBMODE_TAG)) {          /* 0x4CD7BF-E2 */
        g_weatherType = WEATHER_CLEAR;
    }
    /* Check weather override from options */
    else if (g_weatherConfig != WC_RANDOM) {
        /* Override: 2 → rain(1), anything else → snow(2) */
        if (g_weatherConfig == WC_RAIN) {                                     /* 0x4CD83A */
            g_weatherType = WEATHER_RAIN;
        }
        else {
            g_weatherType = WEATHER_SNOW;                                       /* 0x4CD854 */
        }
    }
    /* Random weather selection */
    else if (Random() > 0x2AAA) {                                    /* 0x4CD7FB */
        g_weatherType = WEATHER_CLEAR;  /* clear */
    }
    else if (Random() > 0x2AAA) {                                    /* 0x4CD819 */
        g_weatherType = WEATHER_RAIN;  /* rain */
    }
    else {
        g_weatherType = WEATHER_SNOW;  /* snow */
    }

    g_saveStateFlag++;
}

/**
 * SelectCDTrack — FUN_004d0b18 — 172 bytes
 * Selects which CD music track to play based on game mode, track ID,
 * and character selection. Sets g_cdPlaybackState and g_cdPlaybackTarget.
 *
 * 0x8fd498 = g_vocalsEnabled
 * 0x4fbed8 = g_cdTrackTable (per-track CD base index)
 * 0x4fbef0 = g_cdTrackEmerald (emerald/super sonic track)
 * 0x8fd5e6 = player1 charId (at g_playerBase + 0xF2)
 */
void SelectCDTrack(void)
{
    /* Music toggle selects one of two 6-track sets: config 0 -> offset 6,
     * config 1 -> offset 0. Replay demos use the opposite set. */
    int musicSetOffset = g_vocalsEnabled * -6 + 6;
    if (g_demoMode == DEMO_REPLAY) {
        musicSetOffset = (musicSetOffset == 0) ? 6 : 0;
    }

    /* Base track for the current course. On Radiant Emerald, if any active
     * player is Super Sonic, use the special emerald track instead. */
    int baseTrack = g_cdTrackTable[g_trackId];
    if (g_trackId == TRACK_RADIANT_EMERALD && g_numViewports > 0) {
        Player *players = (Player *)g_playerBase;
        for (int pi = 0; pi < g_numViewports; pi++) {
            if (players[pi].charId == CHAR_SUPER_SONIC) {
                baseTrack = g_cdTrackEmerald;
                break;
            }
        }
    }

    if (g_demoMode != DEMO_NONE) {
        g_cdPlaybackState = 2;
    }
    g_cdPlaybackTarget = musicSetOffset + baseTrack;
}

/**
 * SetupReplayData — 0x004D06F8 — 104 bytes
 * Picks a random replay sound effect (different from the last one), and loads it.
 *
 * The binary loaded one whole "sound\sfx\replayN.wav" into SFX slot 0x38 from
 * a ROM pointer table at 0x504204. The clips are now shipped pre-split so each
 * piece fits the DC sound driver's sample cap, so the loader takes the clip
 * index and fills one slot per piece — see sound/replay_voice.c. Random()
 * masks to 0x7fff, so 0x7fff/0x1556 == 5 bounds the index to 0..5.
 */
void SetupReplayData(void)
{
    int replayIndex = 0;
    do {
        replayIndex = Random();
    } while (replayIndex / 0x1556 == s_lastReplayIndex);
    s_lastReplayIndex = replayIndex / 0x1556;
    ReplayVoice_Load(s_lastReplayIndex);
}
