/**
 * texture.c — Texture loading functions
 *
 * LoadTPageRGB, D3D_LoadPlayfieldTilesRGB,
 * and related texture page management.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "sonicr_paths.h"
#include "r_state.h"
#include "r_texture.h"
#include <string.h>

extern void R_ThawTexture(int tpage);
extern void R_MarkTextureDirty(int tpage);
extern void R_SetTpageRGBA8(int tpage, unsigned char *rgba);
extern void R_SetTpageGreen6(int tpage, int on);

/* Chroma gain baked into the character atlases at upload, 8.8 fixed point
 * (256 = 1.0x). Tuning dial for the DC Add Signed stand-in — see
 * R_SetTpageSatBoost. 358 ≈ 1.4x, derived from a 0.25 offset lift at a 0.6
 * midtone. DC-only in effect; the SDL backend stubs it out. */
#define CHAR_SAT_BOOST (358*12/10)

/* Texture page state arrays (52 tpages max) */
int g_tpageWidth[52];         /* 0x625F98 */
int g_tpageHeight[52];        /* 0x625F9A */

/* Software mode pixel buffers (one per tpage, 256×256 × bpp) */
void *g_tpagePixelBuf[52];             /* 0x8F6BA8 */

/* D3D tpage alt index for playfield tiles */

/**
 * ClaimTpage — take ownership of a tpage slot for new content.
 *
 * Sets the slot's dimensions and state, and clears the per-slot flags that
 * describe the pixels of whatever occupied it before. Those flags are read
 * by the DC upload conversion, so they belong to the content rather than to
 * the slot: a loader that fills g_tpagePixelBuf without claiming the slot
 * first bakes the previous occupant's chroma gain and green layout into the
 * new image. Loaders that want either flag set it after claiming.
 *
 * Every loader that replaces a whole tpage must call this. Loaders that
 * patch a sub-rect of existing content (LoadTextureSubRect, the tint and
 * brightness mutators) must not — they inherit the flags on purpose.
 */
void ClaimTpage(int tpage, int w, int h, int state)
{
    if (tpage < 0 || tpage >= 52) {
        return;
    }
    g_tpageWidth[tpage] = w;
    g_tpageHeight[tpage] = h;
    g_tpageStateArray[tpage] = state;
    R_SetTpageSatBoost(tpage, 0);
    R_SetTpageGreen6(tpage, 0);
}

/* =====================================================================
 * Pixel format conversion
 * ===================================================================== */

/**
 * ConvertPixelRGB — converts 3 RGB bytes to the active pixel format.
 * g_bitsPerPixel: 0x0F=RGB555, 0x10=RGB565, 8=paletted
 */
static unsigned short ConvertPixelRGB(unsigned char r, unsigned char g, unsigned char b)
{
    if (g_bitsPerPixel == 0x0F) {
        /* RGB555: XBBBBBGGGGGRRRRR — matches original Watcom output */
        return (unsigned short)((b >> 3) << 10 | (g >> 3) << 5 | (r >> 3));
    }
    else if (g_bitsPerPixel == 0x10) {
        /* RGB565: RRRRRGGGGGGBBBBB */
        return (unsigned short)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
    }
    else {
        /* 8-bit paletted: dead code, return 0 */
        return 0;
    }
}

#ifdef SONICR_DC
/* =====================================================================
 * DitherConvertImageRGB565 — DC-only Floyd-Steinberg dither
 *
 * Whole-image variant of ConvertPixelRGB's 565 path (5-bit R, 6-bit G,
 * 5-bit B; standard packing (r5<<11)|(g6<<5)|b5). Error-diffuses the
 * 8->5/6-bit quantization to break up color banding on large gradient
 * textures (full-screen wallpapers, sky/parallax). DC only — the 16bpp
 * twiddled PVR textures band hard without it; SDL/GL keeps straight
 * truncation. One-time cost at texture load; error buffers are transient.
 *
 * Error buffers hold values pre-scaled by 16 (the FS denominator), so a
 * neighbour's accumulated error is read back as (acc + 8) >> 4 (rounded).
 * ===================================================================== */
void DitherConvertImageRGB565(const unsigned char *raw,
                              unsigned short *dst, int w, int h)
{
    int count = (w + 2) * 3;
    int *errCurr = (int *)calloc((size_t)count, sizeof(int));
    int *errNext = (int *)calloc((size_t)count, sizeof(int));

    if (errCurr == NULL || errNext == NULL) {
        /* Allocation failed — fall back to plain truncation so we still
         * produce correct (un-dithered) output. */
        free(errCurr);
        free(errNext);
        for (int i = 0; i < w * h; i++) {
            int r = raw[i*3+0], g = raw[i*3+1], b = raw[i*3+2];
            dst[i] = (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
        return;
    }

    for (int y = 0; y < h; y++) {
        memset(errNext, 0, (size_t)count * sizeof(int));

        for (int x = 0; x < w; x++) {
            int ei = (x + 1) * 3;
            const unsigned char *s = raw + ((size_t)y * w + x) * 3;

            int r = (int)s[0] + ((errCurr[ei + 0] + 8) >> 4);
            int g = (int)s[1] + ((errCurr[ei + 1] + 8) >> 4);
            int b = (int)s[2] + ((errCurr[ei + 2] + 8) >> 4);

            if (r < 0) {
                r = 0;
            }
            else if (r > 255) {
                r = 255;
            }
            if (g < 0) {
                g = 0;
            }
            else if (g > 255) {
                g = 255;
            }
            if (b < 0) {
                b = 0;
            }
            else if (b > 255) {
                b = 255;
            }

            /* Rounded quantization: R/B -> 0..31, G -> 0..63. */
            int r5 = (r * 31 + 127) / 255;
            int g6 = (g * 63 + 127) / 255;
            int b5 = (b * 31 + 127) / 255;

            dst[(size_t)y * w + x] =
                (unsigned short)((r5 << 11) | (g6 << 5) | b5);

            /* Expand the quantized value back to 8-bit for the error term. */
            int qr = (r5 << 3) | (r5 >> 2);
            int qg = (g6 << 2) | (g6 >> 4);
            int qb = (b5 << 3) | (b5 >> 2);

            int er = r - qr;
            int eg = g - qg;
            int eb = b - qb;

            /* Distribute error (stored pre-scaled by 16): 7 right, 3 down-left,
             * 5 down, 1 down-right. */
            errCurr[ei + 3 + 0] += er * 7;
            errCurr[ei + 3 + 1] += eg * 7;
            errCurr[ei + 3 + 2] += eb * 7;
            errNext[ei - 3 + 0] += er * 3;
            errNext[ei - 3 + 1] += eg * 3;
            errNext[ei - 3 + 2] += eb * 3;
            errNext[ei + 0] += er * 5;
            errNext[ei + 1] += eg * 5;
            errNext[ei + 2] += eb * 5;
            errNext[ei + 3 + 0] += er;
            errNext[ei + 3 + 1] += eg;
            errNext[ei + 3 + 2] += eb;
        }

        int *tmp = errCurr;
        errCurr = errNext;
        errNext = tmp;
    }

    free(errCurr);
    free(errNext);
}
#endif /* SONICR_DC */

/**
 * LoadTPageRGB — FUN_0042b044 — 676 bytes
 * Loads a 256×256 RAW texture (3 bytes/pixel RGB) into a tpage slot.
 * Converts to 16bpp (RGB565 for 16bpp, RGB555 for 15bpp).
 *
 * Original: in_EAX = tpage index, filename from auto-increment file system.
 * Our version: tpage and filename passed explicitly by callers.
 */
void LoadTPageRGB(int tpage, const char *filename)
{
    if (filename == NULL) {
        return;
    }
    if (tpage < 0 || tpage >= 52) {
        return;
    }

    /* Clear any prior per-tpage filter override — the slot may have held a
     * different texture (e.g. PLAYER00 on the previous track) that pinned
     * this tpage to NEAREST. The hook at function exit re-applies the pin
     * if the new filename still warrants it. */
    R_ClearTpageFilter(tpage);
    R_ClearNoColorKey(tpage);

    FILE *fp = fOpen(filename, "rb");
    if (fp == NULL) {
        DebugLog("  Failed to open: %s\n", filename);
        return;
    }

    /* Detect file size to determine dimensions */
    fSeek(fp, 0, SEEK_END);
    long fileSize = fTell(fp);
    fSeek(fp, 0, SEEK_SET);

    int pixelCount = (int)(fileSize / 3);
    int imgW;
    int imgH;
    /* env map textures */
    if (pixelCount == 128 * 128) {
        imgW = 128;
        imgH = 128;
    }
    /* standard tpage */
    else {
        imgW = 256;
        imgH = 256;             
        pixelCount = 256 * 256;
    }

    /* Allocate pixel buffer if needed */
    if (g_tpagePixelBuf[tpage] != NULL &&
        (g_tpageWidth[tpage] != imgW || g_tpageHeight[tpage] != imgH))
    {
        free(g_tpagePixelBuf[tpage]);
        g_tpagePixelBuf[tpage] = NULL;
    }
    if (g_tpagePixelBuf[tpage] == NULL) {
        g_tpagePixelBuf[tpage] = malloc(pixelCount * 2);
        if (g_tpagePixelBuf[tpage] == NULL) {
            fClose(fp);
            return;
        }
    }

    /* Read pixels, 3 bytes each (R,G,B), convert to 16bpp.
     * DC: try one bulk fRead first — each per-byte fRead locks io_lock
     * against the music streaming thread. Falls back to byte-by-byte if
     * malloc fails. */
    unsigned short *dst = (unsigned short *)g_tpagePixelBuf[tpage];

    unsigned char *raw = (unsigned char *)malloc((size_t)pixelCount * 3);
    if (raw != NULL) {
        fRead(raw, 1, ((size_t)pixelCount * 3)/4, fp);
        fRead(raw + ((size_t)pixelCount * 3)/4, 1, ((size_t)pixelCount * 3)/4, fp);
        fRead(raw + ((size_t)pixelCount * 3)/4 + ((size_t)pixelCount * 3)/4, 1, ((size_t)pixelCount * 3)/4, fp);
        fRead(raw + ((size_t)pixelCount * 3)/4 + ((size_t)pixelCount * 3)/4 + ((size_t)pixelCount * 3)/4, 1, ((size_t)pixelCount * 3)/4, fp);

        if (g_bitsPerPixel == 0x10) {
            for (int i = 0; i < pixelCount; i++) {
                unsigned char r = raw[i*3+0], g = raw[i*3+1], b = raw[i*3+2];
                dst[i] = (unsigned short)((int)b >> 3) |
                         (unsigned short)(((int)r >> 3) << 11) |
                         (unsigned short)(((int)g >> 3) << 6);
            }
        }
        else {
            for (int i = 0; i < pixelCount; i++) {
                unsigned char r = raw[i*3+0], g = raw[i*3+1], b = raw[i*3+2];
                dst[i] = (unsigned short)((int)b >> 3) |
                         (unsigned short)(((int)r >> 3) << 10) |
                         (unsigned short)(((int)g >> 3) << 5);
            }
        }
        free(raw);
    }
    else if (g_bitsPerPixel == 0x10) {
        for (int i = 0; i < pixelCount; i++) {
            unsigned char r, g, b;
            fRead(&r, 1, 1, fp);
            fRead(&g, 1, 1, fp);
            fRead(&b, 1, 1, fp);
            dst[i] = (unsigned short)((int)b >> 3) |
                     (unsigned short)(((int)r >> 3) << 11) |
                     (unsigned short)(((int)g >> 3) << 6);
        }
    }
    else {
        for (int i = 0; i < pixelCount; i++) {
            unsigned char r, g, b;
            fRead(&r, 1, 1, fp);
            fRead(&g, 1, 1, fp);
            fRead(&b, 1, 1, fp);
            dst[i] = (unsigned short)((int)b >> 3) |
                     (unsigned short)(((int)r >> 3) << 10) |
                     (unsigned short)(((int)g >> 3) << 5);
        }
    }

    fClose(fp);

    ClaimTpage(tpage, imgW, imgH, 1);
    g_tpageStateArray[tpage] = 4;  /* texture ready */

    /* Character atlases get a chroma boost baked in at upload. On DC the
     * additive offset colour that stands in for Add Signed desaturates by
     * roughly max/(max+offset); pre-boosting chroma lands the characters back
     * at the intended saturation. Must be set before the MarkTextureDirty
     * below, which is what drives the upload. */
    R_SetTpageSatBoost(tpage,
                       /* (strstr(filename, "PLAYER0") != NULL) ? */ CHAR_SAT_BOOST
                                                             /* : 0 */);
    /* A full re-upload supersedes any prior freeze. Without thawing, the
     * DC PVR backend's PVR_MarkTpageDirty short-circuits when the slot
     * is frozen, leaving new pixel data stuck in the CPU buffer while
     * VRAM still holds the previous track's parallax content. */
    R_ThawTexture(tpage);
    R_MarkTextureDirty(tpage);
    /* This tpage is being repurposed with fresh 16bpp content — drop any
     * persistent 32-bit override (e.g. a previous track's folded sky) so
     * it doesn't get uploaded over the new texture (e.g. menu models after
     * retiring mid-race). The sky re-registers its own override on reload. */
    R_SetTpageRGBA8(tpage, NULL);

    /* Auto-pin atlas tpages to nearest. Filtering across sub-region edges
     * produces visible cruft on these. DC backend honors this; SDL stub is
     * no-op.
     *
     *   PLAYER00/01 — character body atlas (per-limb sub-regions).
     *   ICON00/01   — HUD atlas (timers, digits, buttons; minimap, env-map,
     *                 weather strip overlays).
     *   MISC00/01   — particle/sparkle atlas (invincibility sparkles, ring-
     *                 collect, item burst, shadow sprites).
     *
     * NOTE: pinning ICON01 also unfilters the env-map trophy/title "R" model
     * since the env map lives at (0,0..127,127) of that tpage. Acceptable
     * trade-off for now; if env-map quality matters more later, split it
     * to its own tpage and drop the ICON01 match here. */
    if (strstr(filename, "PLAYER0") != NULL ||
        strstr(filename, "ICON0")   != NULL ||
        strstr(filename, "MISC0")   != NULL ||
        strstr(filename, "TITLES0") != NULL ||
        strstr(filename, "NET01")   != NULL)
    {
        R_SetTpageFilter(tpage, R_FILTER_NEAREST);
    }
}

/**
 * LoadPlopSprites — binary 0x42A5EC call sites in the per-track D3D inits
 * (Island at 0x47369C; City/Ruin/Factory have matching calls; Emerald has
 * none). Loads GENERAL/PLOP2.RAW — a 32x224 24bpp strip of seven 32x32
 * expanding water-ring "plop" frames (FLAKE2.RAW snowflake stamps in snow
 * weather) — into the parallax tpage's top-right column at (224,0).
 *
 * DrawGroundParticlesD3D (0x4626B0) samples exactly this region: U
 * 0.875..1.0, V = (age>>1)*32/256, for the 16 ambient water-ripple decals
 * that SpawnOtherParticle scatters on off-track (water) positions.
 *
 * The strip's near-black background is converted to color-key green so the
 * ring pixels cut out cleanly through our modulate/color-key draw path.
 */
void LoadPlopSprites(void)
{
    int tpage = g_tpageParallax1;
    const char *path = (g_weatherType == WEATHER_SNOW)
        ? DATA_DIR SEP "GENERAL" SEP "FLAKE2.RAW"
        : DATA_DIR SEP "GENERAL" SEP "PLOP2.RAW";

    if (tpage < 0 || tpage >= 52) {
        return;
    }

    FILE *fp = fOpen(path, "rb");
    if (fp == NULL) {
        DebugLog("  Failed to open: %s\n", path);
        return;
    }

    if (g_tpagePixelBuf[tpage] == NULL) {
        g_tpagePixelBuf[tpage] = malloc(256 * 256 * 2);
        if (g_tpagePixelBuf[tpage] == NULL) {
            fClose(fp);
            return;
        }
        memset(g_tpagePixelBuf[tpage], 0, 256 * 256 * 2);
    }

    unsigned short *buf = (unsigned short *)g_tpagePixelBuf[tpage];

    unsigned char *plopRaw = (unsigned char *)malloc((size_t)224 * 32 * 3);
    if (plopRaw != NULL) {
        size_t got = fRead(plopRaw, 1, (size_t)224 * 32 * 3, fp);
        int gotPixels = (int)(got / 3);
        for (int i = 0; i < gotPixels; i++) {
            int row = i / 32;
            int col = i % 32;
            unsigned char r = plopRaw[i*3+0];
            unsigned char g = plopRaw[i*3+1];
            unsigned char b = plopRaw[i*3+2];

            unsigned short px;
            if (r < 0x18 && g < 0x18 && b < 0x18) {
                px = (g_bitsPerPixel == 0x10)
                    ? (unsigned short)(0x1F << 6)
                    : (unsigned short)(0x1F << 5);
            }
            else if (g_bitsPerPixel == 0x10) {
                px = (unsigned short)((int)b >> 3) |
                     (unsigned short)(((int)r >> 3) << 11) |
                     (unsigned short)(((int)g >> 3) << 6);
            }
            else {
                px = (unsigned short)((int)b >> 3) |
                     (unsigned short)(((int)r >> 3) << 10) |
                     (unsigned short)(((int)g >> 3) << 5);
            }
            buf[row * 256 + 224 + col] = px;
        }
        free(plopRaw);
    }
    else {
        for (int row = 0; row < 224; row++) {
            for (int col = 0; col < 32; col++) {
                unsigned char r;
                unsigned char g;
                unsigned char b;

                if (fRead(&r, 1, 1, fp) != 1) {
                    row = 224;
                    break;
                }
                fRead(&g, 1, 1, fp);
                fRead(&b, 1, 1, fp);

                unsigned short px;
                if (r < 0x18 && g < 0x18 && b < 0x18) {
                    px = (g_bitsPerPixel == 0x10)
                        ? (unsigned short)(0x1F << 6)
                        : (unsigned short)(0x1F << 5);
                }
                else if (g_bitsPerPixel == 0x10) {
                    px = (unsigned short)((int)b >> 3) |
                         (unsigned short)(((int)r >> 3) << 11) |
                         (unsigned short)(((int)g >> 3) << 6);
                }
                else {
                    px = (unsigned short)((int)b >> 3) |
                         (unsigned short)(((int)r >> 3) << 10) |
                         (unsigned short)(((int)g >> 3) << 5);
                }
                buf[row * 256 + 224 + col] = px;
            }
        }
    }
    fClose(fp);

    R_MarkTextureDirty(tpage);
}

#define TILE_SIZE         32

/* =====================================================================
 * D3D_LoadPlayfieldTilesRGB — 0x0042D418 — 459 bytes
 *
 * D3D mode: allocates two 256×256 texture page surfaces for playfield tiles.
 * Reads tile data from file into a temp buffer, then copies tiles into
 * the tpage surfaces in the correct grid layout.
 * ===================================================================== */
void D3D_LoadPlayfieldTilesRGB(const char *filename)
{
    extern void GL_KeepPixels(int tpage);
    int tpage1 = g_tpageUIAlt;
    int tpage2 = g_tpageUIAlt + 1;
    DebugLog("D3D_LoadPlayfieldTilesRGB: tpage1=%d tpage2=%d\n", tpage1, tpage2);

    /* Ground-collision (ground_collision.c IsTileOnTrack) samples these
     * tpages from system RAM at runtime to check the color-key magenta
     * pixels that mark off-track / water tiles. Mark them keep-pixels so
     * the eager-upload-then-free path doesn't strip them. */
    GL_KeepPixels(tpage1);
    GL_KeepPixels(tpage2);

    ClaimTpage(tpage1, 256, 256, 1);
    ClaimTpage(tpage2, 256, 256, 1);

#ifdef BLURRY

//SONICR_DC
    /* Bilinear the playfield tiles. Per-tpage rather than the global filter:
     * select_header_pair() gives the override priority over s_desired.filter,
     * it survives ClaimTpage, and it costs no per-primitive header re-emit.
     *
     * These pages are ATLASES (10x12 tiles of 32x32 in 256x256), so filtering
     * samples across tile boundaries at the edges — expect visible seams until
     * the UVs are inset by half a texel or the tiles get a duplicated border. */
    R_SetTpageFilter(tpage1, R_FILTER_LINEAR);
    R_SetTpageFilter(tpage2, R_FILTER_LINEAR);
#endif

    /* Open tile file — binary receives filename in EAX, calls fopen internally */
    FILE *fp = fOpen(filename, "rb");
    if (fp == NULL) {
        return;
    }

    /* Tile data: PLY file is a 320×384 bitmap (10 tiles × 12 tiles, each 32×32).
     * Verified against binary 0x42d4cc-0x42d5cf: source stride = 0x3C0 (960 = 320×3).
     * Read entire bitmap into temp buffer, then extract tiles by grid position. */
    #define PLY_COLS 10
    #define PLY_ROWS 12
    #define PLY_WIDTH (PLY_COLS * TILE_SIZE)  /* 320 */
    int totalPixels = PLY_WIDTH * (PLY_ROWS * TILE_SIZE);  /* 320 × 384 */
    unsigned short *tempBuf = (unsigned short *)malloc(totalPixels * 2);
    if (tempBuf == NULL) {
        fClose(fp);
        return;
    }

    unsigned char *rawTiles = (unsigned char *)malloc((size_t)totalPixels * 3);
    if (rawTiles != NULL) {
        fRead(rawTiles, 1, (size_t)totalPixels * 3, fp);
        for (int i = 0; i < totalPixels; i++) {
            tempBuf[i] = ConvertPixelRGB(rawTiles[i*3+0], rawTiles[i*3+1], rawTiles[i*3+2]);
        }
        free(rawTiles);
    }
    else {
        for (int i = 0; i < totalPixels; i++) {
            unsigned char r, g, b;
            fRead(&r, 1, 1, fp);
            fRead(&g, 1, 1, fp);
            fRead(&b, 1, 1, fp);
            tempBuf[i] = ConvertPixelRGB(r, g, b);
        }
    }
    fClose(fp);

    /* Allocate pixel buffers for GL upload */
    if (g_tpagePixelBuf[tpage1] == NULL) {
        g_tpagePixelBuf[tpage1] = calloc(256 * 256, 2);
    }
    if (g_tpagePixelBuf[tpage2] == NULL) {
        g_tpagePixelBuf[tpage2] = calloc(256 * 256, 2);
    }

    /* Extract tiles from the 320×384 bitmap into tpage 8×8 grid layout.
     * Binary iterates: outer = 12 tile rows, inner = 10 tile columns.
     * Tile index 0-63 → tpage1, 64-119 → tpage2. */
    int tileIdx = 0;
    for (int bmpTileRow = 0; bmpTileRow < PLY_ROWS; bmpTileRow++) {
        for (int bmpTileCol = 0; bmpTileCol < PLY_COLS; bmpTileCol++, tileIdx++) {
            int tp = (tileIdx >= 64) ? tpage2 : tpage1;
            unsigned short *dst = (unsigned short *)g_tpagePixelBuf[tp];
            if (dst == NULL) {
                continue;
            }
            int localTile = tileIdx - ((tileIdx >= 64) ? 64 : 0);
            int dstCol = localTile & 7;      /* 0-7: column in 8×8 tpage grid */
            int dstRow = localTile >> 3;      /* 0-7: row in 8×8 tpage grid */

            for (int ty = 0; ty < TILE_SIZE; ty++) {
                for (int tx = 0; tx < TILE_SIZE; tx++) {
                    /* Source: bitmap position (bmpTileCol*32 + tx, bmpTileRow*32 + ty) */
                    int srcX = bmpTileCol * TILE_SIZE + tx;
                    int srcY = bmpTileRow * TILE_SIZE + ty;
                    /* Dest: tpage grid position (dstCol*32 + tx, dstRow*32 + ty) */
                    int dstX = dstCol * TILE_SIZE + tx;
                    int dstY = dstRow * TILE_SIZE + ty;
                    dst[dstY * 256 + dstX] = tempBuf[srcY * PLY_WIDTH + srcX];
                }
            }
        }
    }
    #undef PLY_COLS
    #undef PLY_ROWS
    #undef PLY_WIDTH

    free(tempBuf);

    /* Mark tpages as ready for rendering, then trigger eager upload to VRAM.
     * Without these MarkDirty calls the freshly-written tile pixels stay in
     * system RAM only — under the eager-upload regime nothing else pushes
     * them across, and the GPU keeps sampling whatever was last in those
     * slots (e.g., the previous menu's tinted wallpaper). */
    g_tpageStateArray[tpage1] = 4;
    R_MarkTextureDirty(tpage1);
    if (g_tpagePixelBuf[tpage2] != NULL) {
        g_tpageStateArray[tpage2] = 4;
        R_MarkTextureDirty(tpage2);
    }
}
