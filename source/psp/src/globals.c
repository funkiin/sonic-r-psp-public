/**
 * globals.c — Global variable definitions
 *
 * All extern globals declared in sonicr_globals.h are defined here.
 * In the original binary these live at fixed addresses in the .bss
 * and .data sections. For recompilation they're regular globals.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"

/* Window / Instance */
HINSTANCE    g_hInstance;
HINSTANCE    g_hInstance_dinput;
HINSTANCE    g_hInstance_dplay;
HINSTANCE    g_hPrevInstance;
LPSTR        g_lpCmdLine;
int          g_nCmdShow;
HWND         g_hWnd;
HWND         g_hWndGame;
HACCEL       g_hAccel;

/* Render Mode & Display */
int          g_renderMode;
int          g_bitsPerPixel;
int          g_screenWidth;
int          g_screenHeight;
int          g_screenWidthFull;
int          g_screenHeightFull;
int          g_screenCenterX;
int          g_screenCenterY;
int          g_projScaleX;
int          g_projScaleY;
int          g_projScaleXCurrent;
int          g_clipLeft;
int          g_clipRight;
int          g_clipTop;
int          g_clipBottom;
int          g_screenScale;
int          g_renderEnabled;
int          g_renderPass;
int          g_surfaceLost;
int          g_surfaceStride;

/* Frame Timing */
DWORD        g_currentTime;
int          g_currentFPS;
int          g_totalFrames;
int          g_totalFrames2;
int          g_frameSkip;
int          g_skipThisFrame;

int          g_maxPolyCount;
int          g_polyCount;

/* Game State */
int          g_demoMode;
int          g_trackId;
int          g_raceType;             /* 0x008FB950 — Game Mode (Sonic Retro wiki):
                                       *   0=GP, 1=Multiplayer, 2=Time Attack, 3=VS Challenge
                                       *   4=internal post-upgrade state from raceType 3 (purpose unknown) */
int          g_raceSubMode;
int          g_netSessionActive;
int          g_isNetworkGame;
int          g_numPlayers;
int          g_numHumans;
int          g_numViewports;         /* 0x6E9910 */
int          g_mirrorMode;

/* Fade System */
int          g_fadeState;
int          g_fadeLevel;
int          g_fadeSpeed;
unsigned char g_bgTintR;
unsigned char g_bgTintG;
unsigned char g_bgTintB;

/* Race State */
int          g_raceResult;
int          g_raceFinished;
int          g_raceCheckpoint;
int          g_postRaceCameraMode;
int          g_introCountdown;
int          g_orbitAngle;
int          g_isMultiRace;
int          g_viewportIndex;
int          g_exitRaceFlag;
int          g_isPaused;
int          g_pauseLatch;

/* Screen / Menu — g_screenResult, g_modelRotation and the menu cursor fields
 * all live in g_stateBlock92528C; see sonicr_globals.h. */
unsigned char g_inputBits;
unsigned short g_combinedInputState;

/* Math Tables — cosTable is sinTable + 1024 entries (90° offset),
 * matching the binary where 0x51e074 points to sinTable + 0x1000 bytes.
 * sinTable has 5120 entries to allow cosTable indexing up to 4095. */
int          g_sinTable[5120] =
#include "sin_table_data.h"
;
int         *g_cosTable = &g_sinTable[1024];

/* Model / Track Data */
int          g_modelCount;
int          g_modelLimbCount;
int          g_modelVertexCount;
int          g_modelPolygonCount;
int          g_modelFrameCount;
void        *g_objectStructArray;
SrcVertex   *g_vertexArrayBase;
/* g_polygonArrayBase — points to s_polygonStorage in globals_extra.c */
void        *g_polygonArrayBase;
int          g_totalObjects;
int          g_trackVertexCount;
int          g_ringCount;
int          g_weatherType;

/* Character Data */
/* Character stats table — extracted from SONICR.EXE at VA 0x005016B4.
 * 10 characters × 10 ints (stride 0x28 = 40 bytes).
 * Fields: MaxSpeed, TurnRate, Friction, Drag, JumpPower,
 *         AbilSpeed, WaterDrag, WaterSpeed, UWMaxSpeed, Unk9 */
static int s_charStatsData[100] = {
    /* Sonic    */ 208896, 3584, 35, 4608, 1024, 49, 0, 221184, 98304, 147456,
    /* Tails    */ 184320, 4096, 45, 2048, 512, 56, 147456, 196608, 98304, 110592,
    /* Knuckles */ 208896, 4608, 35, 3584, 2048, 56, 196608, 196608, 98304, 122880,
    /* Amy      */ 147456, 5120, 35, 3584, 2560, 0, 221184, 0, 0, 135168,
    /* Eggman   */ 172032, 3584, 40, 3072, 512, 0, 0, 0, 0, 172032,
    /* Metal    */ 221184, 4096, 35, 2560, 1536, 70, 0, 0, 122880, 0,
    /* TailsDoll*/ 184320, 3072, 25, 4096, 0, 0, 0, 0, 0, 147456,
    /* MKnux    */ 221184, 5120, 30, 4096, 1536, 70, 208896, 0, 110592, 0,
    /* EggRobo  */ 184320, 5120, 35, 4608, 1536, 0, 0, 0, 98304, 0,
    /* Super    */ 245760, 5120, 40, 4608, 512, 56, 0, 245760, 98304, 172032,
};
int         *g_charStatsTable = s_charStatsData;
void        *g_charAnimTables;
/* g_charUnlockTable and g_allCharsUnlocked are now #defines into g_saveBlock (save.c) */

/* Ghost Replay */
int          g_ghostDataExists;
int          g_ghostCharId;
int          g_ghostWriteIndex;
int          g_ghostReadIndex;
int          g_ghostMaxFrames;

/* Sound */
void        *g_lpDirectSound;
int          g_masterVolume;
int          g_mciDeviceId;

/* Network */
int          g_localPlayerIndex;
int          g_netGameStarted;
int          g_netPlayerCount;

/* Player — 10 slots × 0x71C bytes each in BSS.
 * 5 for racing, up to 10 for unlock screen character models. */
static Player s_playerData[10];
Player      *g_playerBase = s_playerData;

/* Texture Pages */
int          g_uiTexPage;
int          g_tpagePlayfield1;
int          g_tpagePlayfield2;
int          g_tpageObjects;
int          g_tpageCharacters;
int          g_tpageCharBase;
int          g_tpageParallax1;
int          g_tpageExtra;
int          g_tpageBase;

/* Save Data — g_saveBlock is in save.c, g_saveDataBlock removed */
int          g_shutdownStarted;

/* AI / Track */
void        *g_trackSurfaceData;
void        *g_splineWaypoints;
void        *g_waypointDataBase;
void        *g_aiGridGround;
int          g_aiGridOriginX;
int          g_aiGridOriginZ;
int          g_aiGridCellWidth;
int          g_aiGridCellHeight;

/* Camera float scales — doubles in original .data section (0x51FCA0, 0x51FCA8) */
sr_double    g_fixedToFloat   = 0.000244140625;      /* 1.0 / 4096.0 */
sr_double    g_angleToRadians = 0.0015339807878960027; /* 2*PI / 4096.0 */

/* View matrix — copied from render camera by SetViewportClipRect.
 * Binary layout: stride-4 (4 ints per row, 3 used + 1 padding), 16 ints total.
 * Addresses: 0x6E9C44 row0, 0x6E9C54 row1, 0x6E9C64 row2, 0x6E9C74 unused. */
int          g_viewMtx[16]; /* [00,01,02,pad, 10,11,12,pad, 20,21,22,pad, ...] */
#ifdef SONICR_DC
/* DC parallel float view matrix — populated by BuildViewMatrix after the int
 * writes. Same stride-4 layout as g_viewMtx[]. Pre-divided by 4096 so per-vertex
 * transforms can drop the trailing >>12 / /4096 entirely. Only DC builds pay
 * the per-frame conversion; SDL keeps using the int matrix as before. */
float        g_viewMtxF[16];
#endif
int          g_camIntX, g_camIntY, g_camIntZ;

/* Input Mapping — 20 contiguous ints at 0x676084, 10 keys × 2 players */
int          g_keyMappingData[20];
unsigned char g_diKeyboardState[256];
unsigned short g_p1ButtonState;
unsigned short g_p2ButtonState;
unsigned short g_joySlotState[5];    /* 0x00675898..0x006758A0 — per-joystick button states */

/* Network events */
HANDLE       g_netRenderEvent;
HANDLE       g_netRecvEvent;
HANDLE       g_netSendEvent;
