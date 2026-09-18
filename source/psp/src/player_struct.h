/**
 * player_struct.h — Player struct definition
 *
 * The player struct is 0x71C (1820) bytes per player.
 * 5 players are allocated contiguously in memory.
 *
 * Field naming: where semantics are known from binary analysis,
 * descriptive names are used. Unknown fields use _unk_0xNNN format
 * (byte offset in hex). All offsets in comments are BYTE offsets.
 *
 * Two addressing modes existed in the original code:
 *   player[N]              — int at byte offset N*4
 *   *(short*)(base + 0xNN) — short at byte offset 0xNN
 *
 * Key little-endian pattern:
 *   player[N] >> 16   = upper 16 bits = short at byte N*4+2
 *   (short)player[N]  = lower 16 bits = short at byte N*4
 */

#ifndef PLAYER_STRUCT_H
#define PLAYER_STRUCT_H

#include <stddef.h>   /* offsetof */

/* =====================================================================
 * Sub-struct: 3x3 rotation matrix with stride-4 int layout
 *
 * Column-major with 1 padding int between columns:
 *   Col 0: [0],[1],[2], padding [3]
 *   Col 1: [4],[5],[6], padding [7]
 *   Col 2: [8],[9],[10]
 *
 * Access: mat.m[col * 4 + row]  (col=0..2, row=0..2)
 * ===================================================================== */
typedef struct RotMatrix3x3 {
    int m[11];  /* 3 cols × (3 + 1 padding) - last col has no padding = 11 ints */
} RotMatrix3x3;

/* =====================================================================
 * Sub-struct: AI waypoint ring buffer (16 entries)
 * ===================================================================== */
typedef struct WaypointRing {
    int head;                       /* 0x00 = byte 0x158 — ring head index */
    int tail;                       /* 0x04 = byte 0x15C — ring tail index */
    short _unk_0x160;               /* 0x08 = byte 0x160 */
    short respawnSegment;           /* 0x0A = byte 0x162 — AI segment idx saved with respawn pos, restored on respawn */
    short wallPresence;             /* 0x0C = byte 0x164 — wall flag / drag mult */
    short _unk_0x166;               /* 0x0E = byte 0x166 */
    short wpBase[16];               /* 0x10 = byte 0x168 — waypoint base per slot */
    unsigned char segId[16];        /* 0x30 = byte 0x188 — segment ID per slot */
    unsigned char wpCount[16];      /* 0x40 = byte 0x198 — waypoint count per slot */
} WaypointRing;

/* =====================================================================
 * The Player struct — exactly 0x71C (1820) bytes
 * ===================================================================== */
typedef struct Player {
    /* --- 0x000-0x017: Position and angles (6 ints + 2 shorts) --- */
    int posX;                   /* 0x000 — player[0]  — world X (fixed-point) */
    int posY;                   /* 0x004 — player[1]  — world Y */
    int posZ;                   /* 0x008 — player[2]  — world Z */
    int anglePitch;             /* 0x00C — player[3]  — pitch (surface tilt) */
    int angleYaw;               /* 0x010 — player[4]  — yaw heading (12-bit, 0-0xFFF) */
    int angleRoll;              /* 0x014 — player[5]  — roll angle */

    /* --- 0x018-0x01F: Unknown + padding --- */
    short ringCount;            /* 0x018 — accumulated ring count */
    short _unk_0x1A;            /* 0x01A */
    int _unk_0x1C;              /* 0x01C — player[7] */

    /* --- 0x020-0x037: Previous position, velocity, ground height --- */
    int prevPosX;               /* 0x020 — player[8]  — saved at frame start */
    int prevPosY;               /* 0x024 — player[9] */
    int prevPosZ;               /* 0x028 — player[10] */
    int velX;                   /* 0x02C — player[0xB] — world velocity X */
    int velY;                   /* 0x030 — player[0xC] — vertical velocity */
    int velZ;                   /* 0x034 — player[0xD] — world velocity Z */

    /* --- 0x038-0x04F: Ground, local speeds, track progress --- */
    int groundHeight;           /* 0x038 — player[0xE] — from GroundCollision */
    short _unk_0x3C;            /* 0x03C — lower 16 of lapCount init slot */
    short lapCountInit;         /* 0x03E — g_configLapCount (upper 16 of init) */
    short dynamicSpeedMode;     /* 0x040 — flag checked == 1 for speed override */
    short _unk_0x42;            /* 0x042 */
    int forwardSpeed;           /* 0x044 — player[0x11] — local-frame forward */
    int lateralSpeed;           /* 0x048 — player[0x12] — local-frame lateral */
    int trackProgress;          /* 0x04C — player[0x13] — upper bits = progress */

    /* --- 0x050-0x06B: Race / state region --- */
    int lap1Time;               /* 0x050 — player[0x14] — lap 1 split time (24-bit + flag byte) */
    int lap2Time;               /* 0x054 — player[0x15] — lap 2 split time */
    int lap3Time;               /* 0x058 — player[0x16] — lap 3 split time */
    short racePosition;         /* 0x05C — placement (1-5) */
    short lapsCompleted;        /* 0x05E — number of laps finished (0-3) */
    short lapCrossFlag;         /* 0x060 — 1=crossed finish zone (sector 6→7), reset on lap count */
    short _unk_0x62;            /* 0x062 */
    short itemEffectId;         /* 0x064 — item effect type (1=ring,2-4=bonus,5=bounce,6=boost) */
    short itemResponseTimer;    /* 0x066 — set to 0x5A on pickup, counts down each frame */
    int yOffset;                /* 0x068 — player[0x1A] — Y position offset */

    /* --- 0x06C-0x09F: Animation / movement state (shorts) --- */
    short brakeCounter;         /* 0x06C */
    short _unk_0x6E;            /* 0x06E — player yaw for camera */
    short moveMode;             /* 0x070 — copy of yaw, move mode */
    short groundedFlag;         /* 0x072 — 1=grounded, 0=airborne */
    short _unk_0x74;            /* 0x074 */
    short jumpCounter;          /* 0x076 — jump ability uses left */
    short _unk_0x78;            /* 0x078 */
    short _unk_0x7A;            /* 0x07A */
    short _unk_0x7C;            /* 0x07C */
    short abilityTimer;         /* 0x07E — countdown (120 frames for boost) */
    short _unk_0x80;            /* 0x080 — jump/contact state */
    short itemEffectState;      /* 0x082 — -1 water shield active, -2 water shield clearing, >0 speed boost timer, 0 idle */
    short _unk_0x84;            /* 0x084 — init 0xFFFF = jump phase */
    short _unk_0x86;            /* 0x086 — speed lock flag */
    short invincTimer;          /* 0x088 — invincibility countdown */
    short _unk_0x8A;            /* 0x08A */
    int _unk_0x8C;              /* 0x08C — player[0x23] */
    int _unk_0x90;              /* 0x090 — player[0x24] */
    short _unk_0x94;            /* 0x094 */
    short airTimer;              /* 0x096 — 0=grounded, 1+=airborne frame counter */
    short animId;               /* 0x098 — current animation ID */
    short _unk_0x9A;            /* 0x09A */
    int _unk_0x9C;              /* 0x09C — binary: anim data cursor (4-byte ptr, see g_animDataPtrs for 64-bit) */

    /* --- 0x0A0-0x0CF: Surface / terrain / camera --- */
    short loopMode;            /* 0x0A0 — 1 = on a loop/ride surface */
    short _unk_0xA2;            /* 0x0A2 */
    int collisionLayer;         /* 0x0A4 — player[0x29] — collision surface layer index */
    int hitSurfaceIdx;          /* 0x0A8 — player[0x2A] */
    int hitEdgeIdx;             /* 0x0AC — player[0x2B] */
    short overSurface;          /* 0x0B0 — 1=ground/loop surface found this frame */
    short prevOverSurface;      /* 0x0B2 — previous frame's overSurface */
    short surfNormX;            /* 0x0B4 — surface normal X */
    short surfNormY;            /* 0x0B6 — surface normal Y */
    short surfNormZ;            /* 0x0B8 — surface normal Z */
    short _unk_0xBA;            /* 0x0BA */
    int _unk_0xBC;              /* 0x0BC — player[0x2F] — surface boundary */
    int loopLocalX;            /* 0x0C0 — player[0x30] — local X on loop surface */
    int loopLocalZ;            /* 0x0C4 — player[0x31] — local Z on loop surface */
    int pitchCombo;             /* 0x0C8 — player[0x32] — pitch + heading */
    int loopVelX;              /* 0x0CC — player[0x33] — local X velocity on loop surface */
    int loopVelZ;              /* 0x0D0 — player[0x34] — local Z velocity on loop surface */

    /* --- 0x0D4-0x0FF: Camera / display / items --- */
    short yawDelta;             /* 0x0D4 — yaw rotation delta */
    short _unk_0xD6;            /* 0x0D6 — activity flag */
    int _unk_0xD8;              /* 0x0D8 — player[0x36] */
    short _unk_0xDC;            /* 0x0DC */
    short _unk_0xDE;            /* 0x0DE */
    int progressHighWater;      /* 0x0E0 — player[0x38] — furthest track progress reached; wrong-way detection reference (vs current trackProgress @0x4C) */
    int savedVelocity;          /* 0x0E4 — player[0x39] — saved velY */
    short prevAnimId;           /* 0x0E8 — previous animId for change detection, init 0xFFFF */
    short sfxTrigger;           /* 0x0EA — sound effect ID */
    int _unk_0xEC;              /* 0x0EC — player[0x3B] */
    short renderEnabled;        /* 0x0F0 — per-player render-enable flag, set to 1 each frame */
    short charId;               /* 0x0F2 — character ID (0=Sonic..9=SuperSonic) */
    int _unk_0xF4;              /* 0x0F4 — player[0x3D] */
    int _unk_0xF8;              /* 0x0F8 — player[0x3E] */
    short _unk_0xFC;            /* 0x0FC — special ability (Tails) */
    short abilityState;         /* 0x0FE — character ability state */

    /* --- 0x100-0x11B: Item / collision / physics extended --- */
    int itemHeightMod;          /* 0x100 — player[0x40] — item height modifier */
    int effectYMod;             /* 0x104 — player[0x41] — Y / shape modifier for RenderPlayerItemEffect (water shield bob, etc.); 0x0FFFFFFF = don't render */
    int paramMaxSpeed;          /* 0x108 — player[0x42] — max speed from stats */
    int paramAccel;             /* 0x10C — player[0x43] — acceleration */
    short targetYaw;            /* 0x110 — target yaw from velocity direction */
    short _unk_0x112;           /* 0x112 */
    int paramFriction;          /* 0x114 — player[0x45] — friction (stats.Drag/2) */
    int paramDrag;              /* 0x118 — player[0x46] — drag (stats.Friction*4) */

    /* --- 0x11C-0x157: AI navigation --- */
    int aiSegmentIdx;           /* 0x11C — player[0x47] — current track segment */
    int aiAccumDist;            /* 0x120 — player[0x48] — accumulated distance */
    int aiSpeed;                /* 0x124 — player[0x49] */
    int aiAccel;                /* 0x128 — player[0x4A] */
    int respawnX;               /* 0x12C — player[0x4B] — pos saved at last checkpoint/sector crossing, teleported back on AI stuck-respawn */
    int respawnY;               /* 0x130 — player[0x4C] */
    int respawnZ;               /* 0x134 — player[0x4D] */
    int waypointX;              /* 0x138 — player[0x4E] — AI target X */
    int _unk_0x13C;             /* 0x13C — player[0x4F] */
    int waypointZ;              /* 0x140 — player[0x50] — AI target Z */
    int lapCheckX;              /* 0x144 — player[0x51] — posX>>12 snapshot (taken when steerState==0); vs waypoint for lap-cross detection */
    int selectCharId;           /* 0x148 — player[0x52] — copy of charId set at character-select (stored as short); not AI-related */
    int lapCheckZ;              /* 0x14C — player[0x53] — posZ>>12 snapshot; pairs with lapCheckX */
    int lapCounter;             /* 0x150 — player[0x54] */
    int _unk_0x154;             /* 0x154 — player[0x55] */

    /* --- 0x158-0x1A7: AI waypoint ring buffer --- */
    WaypointRing ring;          /* 0x158 — 16-entry ring buffer (80 bytes) */

    /* --- 0x1A8-0x1EF: Extended AI / race / steering --- */
    int waypointIdx;            /* 0x1A8 — player[0x6A] — current waypoint */
    int distanceAccum;          /* 0x1AC — player[0x6B] — distance accumulator */
    int maxSpeedCap;            /* 0x1B0 — player[0x6C] */
    short physicsConst0x1B4;    /* 0x1B4 — player[0x6D] low: base*0x226 physics constant */
    short wallSteerDir;         /* 0x1B6 — player[0x6D] high: wall-response steer direction */
    unsigned short speedField;  /* 0x1B8 — dynamic speed input */
    short lastSurfaceRef;       /* 0x1BA — last surface ref; guards "crossed into a new checkpoint/sector cell" */
    unsigned short speedModifier; /* 0x1BC — speed scaling value */
    short _unk_0x1BE;           /* 0x1BE */
    short finishState;          /* 0x1C0 — 0=racing, 1=braking, 2=finished */
    short turnTimer;            /* 0x1C2 — turn rate timer */
    short turnRateLimit;        /* 0x1C4 — from stats (Friction * 3 / 2) */
    short speedCategory;        /* 0x1C6 — per-character speed lookup */
    short aiSteerCurrent;       /* 0x1C8 — current AI steer value */
    short aiWobbleSin;          /* 0x1CA — sin table wobble value */
    short aiSteerTarget;        /* 0x1CC — target AI steer value */
    unsigned char directionFlip;/* 0x1CE — 0 or 1, XOR'd to flip */
    unsigned char _pad_0x1CF;   /* 0x1CF — padding */
    short steerState;           /* 0x1D0 */
    short _unk_0x1D2;           /* 0x1D2 */
    unsigned short wobbleCounter;/* 0x1D4 — animation wobble phase */
    unsigned short _unk_0x1D6;  /* 0x1D6 */
    short _unk_0x1D8;           /* 0x1D8 — lower 16 of player[0x76] */
    unsigned char physicsFlags; /* 0x1DA — bit 0x01=ramp launch */
    unsigned char physicsFlags2;/* 0x1DB — bit 0x08=boost, 0x10=speed lock */
    unsigned short _unk_0x1DC;  /* 0x1DC */
    unsigned short modelCharId; /* 0x1DE — character ID for rendering */
    short _unk_0x1E0;          /* 0x1E0 — copy of charId for model */
    short _unk_0x1E2;          /* 0x1E2 */
    short _unk_0x1E4;          /* 0x1E4 — animation frame history */
    short _unk_0x1E6;          /* 0x1E6 */
    int _unk_0x1E8;             /* 0x1E8 — player[0x7A] */
    int animFrameIdx;           /* 0x1EC — player[0x7B] — current anim frame */

    /* --- 0x1F0-0x20B: Collision / render extended --- */
    int _unk_0x1F0;             /* 0x1F0 — player[0x7C] */
    int collisionCount;         /* 0x1F4 — player[0x7D] — ring collect count */
    int _unk_0x1F8;             /* 0x1F8 — player[0x7E] */
    int _unk_0x1FC;             /* 0x1FC — player[0x7F] */
    int collisionResult;        /* 0x200 — player[0x80] — ground collision */
    int renderState;            /* 0x204 — player[0x81] — 0x30001 etc. */
    int boneCacheInt;           /* 0x208 — player[0x82] — bone matrix cache */

    /* --- 0x20C-0x234: Character rotation matrix (11 ints) --- */
    RotMatrix3x3 charRot;       /* 0x20C — player[0x83]-[0x8D] */

    /* --- 0x238-0x24B: Gap between char and bone rotation --- */
    int _unk_0x238;             /* 0x238 — player[0x8E] */
    int _unk_0x23C;             /* 0x23C — player[0x8F] */
    int _unk_0x240;             /* 0x240 — player[0x90] */
    int _unk_0x244;             /* 0x244 — player[0x91] */
    int _unk_0x248;             /* 0x248 — player[0x92] */

    /* --- 0x24C-0x274: Bone rotation matrix (11 ints) --- */
    RotMatrix3x3 boneRot;       /* 0x24C — player[0x93]-[0x9D] */

    /* --- 0x278-0x6CB: Large unknown region (mostly unused/unidentified)
     * 0x278 = int[0x9E], extends to int[0x1B2] = byte 0x6C8
     * This region likely contains:
     *   - Extended animation data
     *   - Per-limb bone state
     *   - AI path history
     *   - Misc game state
     * Total: (0x6CC - 0x278) = 0x454 bytes = 276.25 ints → 277 ints */
    int _unk_region_0x278[(0x6CC - 0x278) / 4];  /* 0x278-0x6CB */

    /* --- 0x6CC-0x71B: Per-limb bone cache + tail padding --- */
    int limbBoneCache;          /* 0x6CC — player[0x1B3] — per-limb rebuild flag */
    int _unk_tail_a[(0x714 - 0x6D0) / 4]; /* 0x6D0-0x713 — 17 ints */
    int netActive;              /* 0x714 — 1 if player slot is active in network game */
    int netConnected;           /* 0x718 — 1 if player is connected to session */

} Player;

/* =====================================================================
 * Size assertion — struct MUST be exactly 0x71C bytes
 * ===================================================================== */
_Static_assert(sizeof(Player) == 0x71C,
    "Player struct must be exactly 0x71C (1820) bytes");

/* =====================================================================
 * Offset assertions — verify layout matches binary
 * ===================================================================== */
_Static_assert(offsetof(Player, posX)         == 0x000, "posX");
_Static_assert(offsetof(Player, posY)         == 0x004, "posY");
_Static_assert(offsetof(Player, posZ)         == 0x008, "posZ");
_Static_assert(offsetof(Player, anglePitch)   == 0x00C, "anglePitch");
_Static_assert(offsetof(Player, angleYaw)     == 0x010, "angleYaw");
_Static_assert(offsetof(Player, angleRoll)    == 0x014, "angleRoll");
_Static_assert(offsetof(Player, prevPosX)     == 0x020, "prevPosX");
_Static_assert(offsetof(Player, prevPosY)     == 0x024, "prevPosY");
_Static_assert(offsetof(Player, prevPosZ)     == 0x028, "prevPosZ");
_Static_assert(offsetof(Player, velX)         == 0x02C, "velX");
_Static_assert(offsetof(Player, velY)         == 0x030, "velY");
_Static_assert(offsetof(Player, velZ)         == 0x034, "velZ");
_Static_assert(offsetof(Player, groundHeight) == 0x038, "groundHeight");
_Static_assert(offsetof(Player, forwardSpeed) == 0x044, "forwardSpeed");
_Static_assert(offsetof(Player, lateralSpeed) == 0x048, "lateralSpeed");
_Static_assert(offsetof(Player, trackProgress)== 0x04C, "trackProgress");
_Static_assert(offsetof(Player, racePosition) == 0x05C, "racePosition");
_Static_assert(offsetof(Player, yOffset)      == 0x068, "yOffset");
_Static_assert(offsetof(Player, moveMode)     == 0x070, "moveMode");
_Static_assert(offsetof(Player, groundedFlag) == 0x072, "groundedFlag");
_Static_assert(offsetof(Player, abilityTimer) == 0x07E, "abilityTimer");
_Static_assert(offsetof(Player, invincTimer)  == 0x088, "invincTimer");
_Static_assert(offsetof(Player, airTimer)      == 0x096, "airTimer");
_Static_assert(offsetof(Player, loopMode)    == 0x0A0, "loopMode");
_Static_assert(offsetof(Player, collisionLayer)== 0x0A4, "collisionLayer");
_Static_assert(offsetof(Player, pitchCombo)   == 0x0C8, "pitchCombo");
_Static_assert(offsetof(Player, charId)       == 0x0F2, "charId");
_Static_assert(offsetof(Player, abilityState) == 0x0FE, "abilityState");
_Static_assert(offsetof(Player, paramMaxSpeed)== 0x108, "paramMaxSpeed");
_Static_assert(offsetof(Player, paramAccel)   == 0x10C, "paramAccel");
_Static_assert(offsetof(Player, targetYaw)    == 0x110, "targetYaw");
_Static_assert(offsetof(Player, paramFriction)== 0x114, "paramFriction");
_Static_assert(offsetof(Player, paramDrag)    == 0x118, "paramDrag");
_Static_assert(offsetof(Player, aiSegmentIdx) == 0x11C, "aiSegmentIdx");
_Static_assert(offsetof(Player, waypointX)    == 0x138, "waypointX");
_Static_assert(offsetof(Player, waypointZ)    == 0x140, "waypointZ");
_Static_assert(offsetof(Player, lapCounter)   == 0x150, "lapCounter");
_Static_assert(offsetof(Player, ring)         == 0x158, "ring");
_Static_assert(offsetof(Player, waypointIdx)  == 0x1A8, "waypointIdx");
_Static_assert(offsetof(Player, finishState)  == 0x1C0, "finishState");
_Static_assert(offsetof(Player, turnTimer)    == 0x1C2, "turnTimer");
_Static_assert(offsetof(Player, charRot)      == 0x20C, "charRot");
_Static_assert(offsetof(Player, boneRot)      == 0x24C, "boneRot");
_Static_assert(offsetof(Player, animFrameIdx) == 0x1EC, "animFrameIdx");
_Static_assert(offsetof(Player, collisionResult) == 0x200, "collisionResult");
_Static_assert(offsetof(Player, limbBoneCache)== 0x6CC, "limbBoneCache");

/* =====================================================================
 * Ring buffer sub-struct offset assertions
 * ===================================================================== */
_Static_assert(offsetof(Player, ring) + offsetof(WaypointRing, head)    == 0x158, "ring.head");
_Static_assert(offsetof(Player, ring) + offsetof(WaypointRing, tail)    == 0x15C, "ring.tail");
_Static_assert(offsetof(Player, ring) + offsetof(WaypointRing, wpBase)  == 0x168, "ring.wpBase");
_Static_assert(offsetof(Player, ring) + offsetof(WaypointRing, segId)   == 0x188, "ring.segId");
_Static_assert(offsetof(Player, ring) + offsetof(WaypointRing, wpCount) == 0x198, "ring.wpCount");

/* Character IDs and finish states are defined in sonicr_types.h */

#endif /* PLAYER_STRUCT_H */
