/**
 * sonicr_types.h — Type definitions
 */

#ifndef SONICR_TYPES_H
#define SONICR_TYPES_H

#ifdef _WIN32
/* On Windows we only need the core Win32 + multimedia headers — windows.h
 * gives us BOOL/HWND/DWORD/LPRECT/etc., mmsystem.h provides timeGetTime.
 * We deliberately do NOT pull in <ddraw.h>/<d3d.h>/<dsound.h>/<dinput.h>:
 * the SDL port never calls those APIs, and including them collides with
 * our own typedefs. */
#include <windows.h>
#include <mmsystem.h>
#else
#include "win32_shim.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sonicr_math.h"
#include "ter_types.h"

#define SRABS(x)            (((x) ^ ((x) >> 31)) - ((x) >> 31))

/* =====================================================================
 * Game constants
 * ===================================================================== */

/* Render mode (g_renderMode at 0x6dd860).
 *
 * SetRenderMode, 0x4322c6:
 *     cmp dword ptr [0x630148], 0
 *     setne al / and eax, 0xff / inc eax / mov [0x6dd860], eax
 * so g_renderMode = 1 + ([0x630148] != 0).
 *
 * [0x630148] is the *native software* flag, not a D3D-present flag. Proof at
 * 0x4321b7:
 *     mov edi, dword ptr [0x630148]
 *     test edi, edi / edi ? "TRUE" : "FALSE"
 *     printf("INFO Native software render mode is %s.\n", ...)
 * The driver scan sets it at "Direct3D - Not using any drivers, resetting to
 * native software..." (0x520b78). So [0x630148] != 0 means Direct3D found no
 * driver and the game falls back to its own span rasteriser.
 *
 * That makes the string table at 0x4322db CORRECT, not misleading: mode 2
 * prints "native software render" (0x520ddd), mode 1 prints "Direct3d render"
 * (0x520df4).
 *
 *     g_renderMode == 1  →  Direct3D device present (hardware)
 *     g_renderMode == 2  →  native software rasteriser (the 13-bit display
 *                           list at 0x8FB354, dispatched through 0x4CAC30)
 *
 * This port takes the mode 2 branches: the colour maths we reproduce is the
 * software rasteriser's `clamp(texel + light - bias)`, not D3D's modulate.
 * The OpenGL/PVR backend is only how those branches reach the screen.
 */
enum RenderMode {
    RENDER_D3D  = 1,    /* binary mode 1: Direct3D device present */
    RENDER_SOFT = 2     /* binary mode 2: native software rasteriser */
};

/* Unlit textured-quad vertex colour.
 *
 * 0xE0E0E0 is 224/255 = 0.878, so on a MODULATE site it dims every channel by
 * 12% — which scales chroma by 12% as well. It comes from the binary's D3D
 * path (loaded at 0x46995c, the D3D env-map renderer's flat branch); the
 * software rasteriser this port targets has no equivalent and draws unlit
 * texels undimmed. Measured on the Resort Island parallax: source sky blue
 * 238, the 1998 PC renders 244, we render 211 == 238 * 224/255.
 *
 * Kept behind one name so the value is a single edit. The two forms are NOT
 * interchangeable across texenv: under MODULATE the colour is a multiplier,
 * under ADD_SIGNED it is the light term and 0xE0E0E0 means +0.378. */
#define VERTEX_WHITE_RGB  0xffffffu                       /* caller ORs alpha in */
#define VERTEX_WHITE      (0xff000000u | VERTEX_WHITE_RGB)

/* Colour-key texel test, on the game's 5-bit channels.
 *
 * The key is EXACT. Every software span routine compares the whole 16-bit
 * texel — `cmp ax, 0x7C0` at 0x4AA92C, 0x4AB635, 0x4AC2CD and siblings — and
 * tpages are stored R5<<11 | G5<<6 | B5 with bit 5 unused, so 0x07C0 is
 * precisely (r5 0, g5 31, b5 0). IsOnTrackSurface reads the 24bpp surface and
 * spells the same test out longhand at 0x47B992: `r>>3 != 0`, `g>>3 != 0x1F`,
 * `b>>3 != 0` each return on-track. LoadTPageRGB converts with a plain >>3, so
 * only source green 248..255 with no red or blue can ever reach g5 31.
 *
 * Do NOT widen this. A `g5 >= 29` tolerance used to sit at every upload site
 * and ate 560 art pixels across the game's 226 .RAW files — Amy's 2x2 shoulder
 * highlight in SCHAR00.RAW, 441 pixels of BIN/END/AMY.RAW — against 3,547,776
 * genuine key pixels, every one of them exactly g5 31. IsOnTrackSurface was
 * looser still (`r5 <= 1 && g5 >= 29 && b5 <= 1`) and called 5667 texels of
 * ISLAND03 off-track that the binary treats as solid ground. */
#define IS_COLOR_KEY_RGB5(r5, g5, b5)  ((r5) == 0 && (g5) == 31 && (b5) == 0)

/* Screen function return codes */
#define SCREEN_QUIT         (-1)
#define SCREEN_BACK         0
#define SCREEN_OK           1
#define SCREEN_TA           2
#define SCREEN_MP           3
#define SCREEN_NET          4
#define SCREEN_OPTIONS      5
#define SCREEN_LOADSAVE     10000
#define SCREEN_TIMES        10001
#define SCREEN_TITLE        12345
#define SCREEN_EXIT         99999

#define SCREEN_TA_MODE_SELECT 6
#define SCREEN_TA_NORMAL    1
#define SCREEN_TA_REVERSE   2
#define SCREEN_TA_BALLOON   3
#define SCREEN_TA_TAG       4

#define SCREEN_MP_MODE_SELECT 7
#define SCREEN_MP_NORMAL    1
#define SCREEN_MP_BALLOON   2

/* Race finish states (player+0x1BE upper 16) */
#define FINISH_RACING       0
#define FINISH_BRAKING      1
#define FINISH_DONE         2

/* Fade states (g_fadeState) */
#define FADE_VISIBLE        0
#define FADE_IN             1
#define FADE_OUT            2

#define CHALLENGE_LOST      6
#define CHALLENGE_WON       7

/* Demo mode (g_demoMode at 0x8FB8E4) — Sonic Retro wiki confirmed */
enum DemoMode {
    DEMO_NONE           = 0,    /* Normal gameplay */
    DEMO_TITLE          = 1,    /* Title screen demo (ghost playback) */
    DEMO_REPLAY         = 2     /* Replay demo */
};

/* Weather type (g_weatherType at 0x94BCF4) — Sonic Retro wiki confirmed */
enum WeatherType {
    WEATHER_CLEAR       = 0,
    WEATHER_RAIN        = 1,
    WEATHER_SNOW        = 2
};

enum WeatherConfig {
    WC_RANDOM = 0,
    WC_CLEAR = 1,
    WC_RAIN = 2,
    WC_SNOW = 3
};

/* Time of day (g_timeOfDay at 0x94BCF8) — Sonic Retro wiki confirmed */
enum TimeOfDay {
    TOD_SUNRISE         = 0,
    TOD_DAY             = 1,
    TOD_SUNSET          = 2,
    TOD_NIGHT           = 3
};

/* Difficulty */
enum Difficulty {
    DIFF_EASY = 0,
    DIFF_NORMAL = 1,
    DIFF_HARD = 2
};

/* Race result / pause screen selection (g_raceResult at 0x901C10) */
enum RaceResult {
    RACE_RESULT_ENDED   = -1,   /* Race completed or demo aborted */
    RACE_RESULT_NONE    = 0,    /* Still racing / no selection */
    RACE_RESULT_RESTART = 1,    /* Pause: restart selected */
    RACE_RESULT_QUIT    = 2     /* Pause: quit selected */
};

/* Track ID (g_trackId at 0x8FB8EC) — Sonic Retro wiki confirmed
 * Note: Ruin(3) and Factory(4) are swapped from in-game menu order */
enum TrackId {
    TRACK_NONE              = 0,
    TRACK_RESORT_ISLAND     = 1,
    TRACK_RADICAL_CITY      = 2,
    TRACK_REGAL_RUIN        = 3,
    TRACK_REACTIVE_FACTORY  = 4,
    TRACK_RADIANT_EMERALD   = 5
};

/* Race sub-mode (g_raceSubMode at 0x8FB954) — Sonic Retro wiki confirmed */
enum RaceSubMode {
    SUBMODE_NORMAL      = 0,    /* Standard race */
    SUBMODE_REVERSE     = 1,    /* Drive the course backwards: start from podium
                                 * index 10 with the heading turned 180 degrees.
                                 * Unrelated to g_mirrorMode (0x6E9920), which
                                 * mirrors the world by negating view-matrix X. */
    SUBMODE_TAG         = 2,    /* Tag 4 Characters */
    SUBMODE_BALLOON     = 3     /* Balloon Hunt */
};

/* Race type / game mode (g_raceType at 0x8FB950) — Sonic Retro wiki confirmed,
 * also verified empirically 2026-04-13 via DrawTimerAndStatus instrumentation.
 * Values 0..3 are the menu-selectable game modes; value 4 is an internal
 * post-upgrade state entered from raceType==3 and reset to 0 on race end. */
enum RaceType {
    RACE_TYPE_GP            = 0,    /* Grand Prix */
    RACE_TYPE_MULTIPLAYER   = 1,    /* 2P split-screen / multi-human */
    RACE_TYPE_TIME_ATTACK   = 2,    /* Time Attack (and its submodes) */
    RACE_TYPE_VS_CHALLENGE  = 3,    /* Special race vs hidden character (menu) */
    RACE_TYPE_INTERNAL_4    = 4     /* Post-VS-Challenge internal state (???) */
};

/* Character model metadata — stride 0x50 (20 ints), 26 entries at 0x7130A4.
 * The binary addresses this table as 0x713080 + charId*0x50, which is 0x24
 * bytes below this base, so a binary offset of +0x24 is field 0 here. */
typedef struct {
    int vertexStart;    /* +0x00: first vertex index into g_vertexArrayBase */
    int vertexCount;    /* +0x04: number of vertices */
    int polyStart;      /* +0x08: first polygon index into g_charFaceBase */
    int polyCount;      /* +0x0C: number of polygons */
    int limbStart;      /* +0x10: first limb index into g_limbMetaTable */
    int limbCount;      /* +0x14: number of limbs */
    int animFrameBase;  /* +0x18: animation frame base index */
    int animFrameCount; /* +0x1C: animation frames loaded for this model */
    int boundRadius;    /* +0x20: bounding-sphere radius, near/far culling */
    int lightGroup;     /* +0x24: lighting group / bounding size */
    int charHeight;     /* +0x28: character height for collision/rendering */
    int _reserved_2C[8]; /* +0x2C..+0x48: unused */
    int modelYOffset;   /* +0x4C: whole-model Y offset, added to every limb <<4 */
} ModelMeta;

/* Physics constants */
#define GRAVITY_PER_FRAME   0x3000
#define TERMINAL_VELOCITY   0x25800
#define MAX_JUMP_HEIGHT     0x60000

/* Player struct stride */
#define PLAYER_STRIDE       0x71C

/* Character IDs — index into model tables, unlock table, animation tables */
enum CharId {
    CHAR_SONIC          = 0,
    CHAR_TAILS          = 1,
    CHAR_KNUCKLES       = 2,
    CHAR_AMY            = 3,
    CHAR_EGGMAN         = 4,
    CHAR_METAL_SONIC    = 5,
    CHAR_TAILS_DOLL     = 6,
    CHAR_METAL_KNUCKLES = 7,
    CHAR_EGG_ROBO       = 8,
    CHAR_SUPER_SONIC    = 9,
    CHAR_COUNT          = 10
};

/* =====================================================================
 * D3D constants (from d3dtypes.h / d3d.h)
 * ===================================================================== */

/* D3DPRIMITIVETYPE */
#define D3DPT_TRIANGLELIST      4

/* D3DVERTEXTYPE */
#define D3DVT_TLVERTEX          3

/* D3DSHADEMODE */
#define D3DSHADE_FLAT           1
#define D3DSHADE_GOURAUD        2
#define D3DSHADE_PHONG          3

/* D3DFILLMODE */
#define D3DFILL_POINT           1
#define D3DFILL_WIREFRAME       2
#define D3DFILL_SOLID           3

/* D3DRENDERSTATETYPE — the state IDs passed to SetRenderState */
#define D3DRS_TEXTUREHANDLE     0x01
#define D3DRS_ANTIALIAS         0x02
#define D3DRS_TEXTUREADDRESS    0x03
#define D3DRS_FILLMODE          0x08
#define D3DRS_SHADEMODE         0x09
#define D3DRS_ZWRITEENABLE      0x0F
#define D3DRS_DITHERENABLE      0x11
#define D3DRS_SPECULARENABLE    0x12
#define D3DRS_SRCBLEND          0x13
#define D3DRS_TEXTUREMAPBLEND   0x15
#define D3DRS_CULLMODE          0x16
#define D3DRS_FOGENABLE         0x17
#define D3DRS_ALPHABLENDENABLE  0x1B
#define D3DRS_STIPPLEDALPHA     0x20
#define D3DRS_FOGCOLOR          0x22
#define D3DRS_FOGTABLESTART     0x23
#define D3DRS_FOGTABLEEND       0x24
#define D3DRS_COLORKEYENABLE    0x29

/* D3DTADDRESS — values for D3DRS_TEXTUREADDRESS */
#define D3DTADDRESS_CLAMP       3

/* D3DTBLEND — values for D3DRS_TEXTUREMAPBLEND */
#define D3DTBLEND_MODULATEALPHA 4

/* D3DCULL */
#define D3DCULL_NONE            1
#define D3DCULL_CW              2
#define D3DCULL_CCW             3

/* D3DBLEND */
#define D3DBLEND_BOTHSRCALPHA   0x0C

/* DDFLIP flags */
#ifndef _WIN32
#define DDFLIP_WAIT             0x01
#endif

/* D3DLIGHTSTATETYPE */
#define D3DLIGHTSTATE_MATERIAL  1

/* =====================================================================
 * COM vtable call macro (used for all DirectX interface calls)
 *
 * Usage: COM_CALL(pInterface, VTABLE_OFFSET, args...)
 * Example: COM_CALL(g_lpD3DDevice, D3DDEV2_BEGINSCENE)
 *          COM_CALL(g_lpD3DDevice, D3DDEV2_SETRENDERSTATE, stateId, value)
 * ===================================================================== */
#define COM_CALL(obj, offset, ...) \
    ((int (*)(void*, ...))(*((void***)(obj)))[(offset)/4])((obj), ##__VA_ARGS__)

/* IDirect3DDevice2 vtable offsets (d3d.h) */
#define D3DDEV2_RELEASE             0x08
#define D3DDEV2_ADDVIEWPORT         0x18
#define D3DDEV2_BEGINSCENE          0x28
#define D3DDEV2_ENDSCENE            0x2C
#define D3DDEV2_SETCURRENTVIEWPORT  0x34
#define D3DDEV2_SETRENDERTARGET     0x3C
#define D3DDEV2_GETRENDERSTATE      0x58
#define D3DDEV2_SETRENDERSTATE      0x5C
#define D3DDEV2_GETLIGHTSTATE       0x60
#define D3DDEV2_SETLIGHTSTATE       0x64
#define D3DDEV2_SETTRANSFORM        0x68
#define D3DDEV2_DRAWPRIMITIVE       0x74
#define D3DDEV2_DRAWINDEXEDPRIM     0x78
#define D3DDEV2_SETCLIPSTATUS       0x7C
#define D3DDEV2_GETCLIPSTATUS       0x80

/* IDirectDraw vtable offsets (ddraw.h) */
#define DD_RELEASE                  0x08
#define DD_CREATEPALETTE            0x14
#define DD_CREATESURFACE            0x18
#define DD_SETCOOPERATIVELEVEL      0x50
#define DD_SETDISPLAYMODE           0x54

/* IDirectDrawSurface vtable offsets (ddraw.h) */
#define DDSURF_RELEASE              0x08
#define DDSURF_BLT                  0x14
#define DDSURF_BLTFAST              0x1C
#define DDSURF_FLIP                 0x2C
#define DDSURF_GETATTACHEDSURFACE   0x30
#define DDSURF_GETDC                0x44
#define DDSURF_ISLOST               0x60
#define DDSURF_LOCK                 0x64
#define DDSURF_RELEASEDC            0x68
#define DDSURF_RESTORE              0x6C
#define DDSURF_SETPALETTE           0x7C
#define DDSURF_UNLOCK               0x80

/* IDirectSound vtable offsets (dsound.h) */
#define DS_RELEASE                  0x08
#define DS_CREATESOUNDBUFFER        0x0C
#define DS_SETCOOPERATIVELEVEL      0x18

/* IDirectSoundBuffer vtable offsets (dsound.h) */
#define DSBUF_RELEASE               0x08
#define DSBUF_GETCURRENTPOS         0x10
#define DSBUF_GETVOLUME             0x18
#define DSBUF_LOCK                  0x2C
#define DSBUF_PLAY                  0x30
#define DSBUF_SETPOS                0x34    /* SetCurrentPosition */
#define DSBUF_SETVOLUME             0x3C
#define DSBUF_SETPAN                0x40
#define DSBUF_SETFREQUENCY          0x44
#define DSBUF_STOP                  0x48
#define DSBUF_UNLOCK                0x4C

/* IDirectInputA vtable offsets (dinput.h) */
#define DI_RELEASE                  0x08
#define DI_CREATEDEVICE             0x0C
#define DI_INITIALIZE               0x1C

/* IDirectInputDeviceA vtable offsets (dinput.h) */
#define DIDEV_RELEASE               0x08
#define DIDEV_SETPROPERTY           0x18
#define DIDEV_ACQUIRE               0x1C
#define DIDEV_UNACQUIRE             0x20
#define DIDEV_GETDEVICESTATE        0x24
#define DIDEV_SETDATAFORMAT         0x2C
#define DIDEV_SETCOOPERATIVELEVEL   0x34

/* IDirectPlay2/3/4 vtable offsets (dplay.h) */
#define DP_RELEASE                  0x08
#define DP_CLOSE                    0x10
#define DP_CREATEPLAYER             0x18
#define DP_DESTROYPLAYER            0x24
#define DP_RECEIVE                  0x64
#define DP_SEND                     0x68
#define MAX_PLAYERS         5

/* =====================================================================
 * Weather particle — 64 entries at 0x00673DB0, stride 0x44 (68 bytes)
 * ===================================================================== */
typedef struct {
    int posX;               /* +0x00 */
    int posY;               /* +0x04 */
    int posZ;               /* +0x08 */
    int targetX;            /* +0x0C */
    int targetY;            /* +0x10 */
    int targetZ;            /* +0x14 */
    int velX;               /* +0x18 */
    int velY;               /* +0x1C */
    int velZ;               /* +0x20 */
    int gravity;            /* +0x24 */
    unsigned short flags;   /* +0x28 */
    short lifetime;         /* +0x2A */
    short width;            /* +0x2C */
    short height;           /* +0x2E */
    short animFrame;        /* +0x30 */
    short animMax;          /* +0x32 */
    short frameTimer;       /* +0x34 */
    short frameDelay;       /* +0x36 */
    unsigned char alpha;    /* +0x38 */
    unsigned char uvBaseY;  /* +0x39 */
    unsigned char uvX;      /* +0x3A */
    unsigned char uvY;      /* +0x3B */
    unsigned char tpage;    /* +0x3C */
    unsigned char _pad3D;   /* +0x3D */
    short uvStep;           /* +0x3E */
    short active;           /* +0x40 */
    short _pad42;           /* +0x42 */
} OtherParticle;          /* 0x44 = 68 bytes */

/* =====================================================================
 * Snow/rain particle — 128 entries at 0x0094BD24, stride 0x38 (56 bytes)
 * ===================================================================== */
typedef struct {
    int posX;               /* +0x00 */
    int posY;               /* +0x04 */
    int posZ;               /* +0x08 */
    int targetX;            /* +0x0C */
    int targetY;            /* +0x10 */
    int targetZ;            /* +0x14 */
    int velX;               /* +0x18 */
    int velY;               /* +0x1C */
    int velZ;               /* +0x20 */
    short flags;            /* +0x24 */
    short age;              /* +0x26 */
    short sizeW;            /* +0x28 */
    short sizeH;            /* +0x2A */
    unsigned char uvBaseX;  /* +0x2C */
    unsigned char uvBaseY;  /* +0x2D */
    unsigned char tpage;    /* +0x2E */
    unsigned char _pad2F;   /* +0x2F */
    short frameW;           /* +0x30 */
    short frameH;           /* +0x32 */
    short state;            /* +0x34 */
    short _pad36;           /* +0x36 */
} PrecipParticle;             /* 0x38 = 56 bytes */

/* =====================================================================
 * Ground particle — 16 slots at 0x008F6C60, stride 0x30 (48 bytes)
 * 4 corners of a ground-level quad (leaf/confetti), Y assumed 0.
 * ===================================================================== */
typedef struct {
    int x, y, z;
} GroundParticleVert;

typedef struct {
    GroundParticleVert corners[4]; /* 4 × 12 = 48 = 0x30 */
} GroundParticle;

/* =====================================================================
 * RenderCamera — per-viewport output camera (g_viewportConfigArray)
 *
 * 0xC8 bytes per viewport. Built by BuildCameraView / UpdateFlyoverCamera.
 * Contains world positions, angles, float camera position, and rotation matrices.
 * ===================================================================== */
typedef struct {
    int worldX;         /* +0x00  posX * -256 */
    int worldY;         /* +0x04  posY * 256 (clamped min 0x19000) */
    int worldZ;         /* +0x08  posZ * -256 */
    int intX;           /* +0x0C  worldX >> 12 */
    int intY;           /* +0x10  worldY >> 12 */
    int intZ;           /* +0x14  worldZ >> 12 */
    int yaw;            /* +0x18  yaw angle (12-bit) */
    int pitch;          /* +0x1C  pitch angle (12-bit) */
    int _pad20;         /* +0x20 */
    int basisY;         /* +0x24  camera basis Y (ComputeCameraBasis) */
    int basisX;         /* +0x28  camera basis X (ComputeCameraBasis) */
    int basisZ;         /* +0x2C  camera basis Z (ComputeCameraBasis) */
    float camFloatX;    /* +0x30  worldX / 4096 (float; copied to g_camFloatX) */
    float camFloatY;    /* +0x34  worldY / 4096 (float; copied to g_camFloatY) */
    float camFloatZ;    /* +0x38  worldZ / 4096 (float; copied to g_camFloatZ) */
    float yawFloat;     /* +0x3C  yaw in radians (for BuildD3DViewMatrix) */
    float pitchFloat;   /* +0x40  pitch in radians (for BuildD3DViewMatrix) */
    int _tail[29];      /* +0x44..+0xB7 — int/float rotation matrices */
} RenderCamera;         /* 0xB8 = 184 bytes (padded to 0xC8 per viewport by array stride) */

/* =====================================================================
 * CamStateEntry — per-viewport camera state (g_camStateTable)
 * 4 viewports × 40 bytes at 0x00902140.
 *
 * Binary layout packs 12-bit angles as shorts inside int slots [3]-[5].
 * Struct fields replace pointer-cast aliasing for endian safety.
 * ===================================================================== */
typedef struct {
    int posX;           /* 0x00 — smoothed camera X (×16 fixed-point) */
    int posY;           /* 0x04 — smoothed camera Y (×16) */
    int posZ;           /* 0x08 — smoothed camera Z (×16) */
    short smoothYaw;    /* 0x0C — smoothed yaw (12-bit, wraps) → renderCam->yaw */
    short smoothPitch;  /* 0x0E — smoothed pitch (12-bit, wraps) → renderCam->pitch (negated) */
    short _unk_0x10;    /* 0x10 — unknown */
    short targetYaw;    /* 0x12 — target yaw (re-computed each frame) */
    short targetPitch;  /* 0x14 — target pitch (re-computed each frame) */
    short fovDetail;    /* 0x16 — smoothing divisor (init 0x10 or 0x20) */
    int camDist;        /* 0x18 — camera distance from s_camDistTable */
    int camHeight;      /* 0x1C — camera height from s_camHeightTable */
    int stateCounter;   /* 0x20 — ramp state (0→1→2, cycles) */
    int initFlag;       /* 0x24 — initialized flag (0 then 1) */
} CamStateEntry;        /* 0x28 = 40 bytes */

/* =====================================================================
 * Forward declarations for function pointer compatibility
 * ===================================================================== */

typedef void (*VoidFunc)(void);
typedef int  (*IntFunc)(void);
typedef void (*CodePtr)(void);

#endif /* SONICR_TYPES_H */
