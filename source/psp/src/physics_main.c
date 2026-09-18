/**
 * physics_main.c — Core player physics (binary region 0x41FFD0-0x421100+)
 *
 * CollisionResponse    — 0x0041FFD0 — 461 bytes
 * SweepPlayerCollision — 0x004201A0 — 1016 bytes
 * TrackHeightCheck     — 0x00420598 — 286 bytes
 * PlayerPhysicsMain    — 0x004206B8 — 2632 bytes
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include <math.h>

/* Absolute value (Watcom's (val ^ sign) - sign pattern) */
#define IABS(x) ({ int _v = (x); int _s = _v >> 31; (_v ^ _s) - _s; })

/* Signed fixed-point multiply: (a * b) >> 16 with rounding */
#define FIXMUL16(a, b) ((int)(((long long)(a) * (long long)(b)) >> 16))

/* Forward declarations for sub-functions */
extern void AISteeringAndDrag(Player *obj);           /* 0x004D5BE4 — EAX=player */
extern void VehiclePhysicsPreUpdate(Player *player, unsigned short abilityBits);  /* 0x00480910 — EAX=player, EDX=abilityBits */
extern void PlaySoundEffect(int soundId, int distance, int freqParam);

extern unsigned char g_charDistTable[];  /* ROM at 0x4FA1C0, 16 × 10 bytes */

/* Tag-mode AI flee distance, recomputed per player per frame in
 * PlayerPhysicsMain and read back by both turnaround branches. */
static int g_raceDistThreshold;          /* 0x0054009C */

/* =====================================================================
 * CollisionResponse — 0x0041FFD0 — 461 bytes
 *
 * Computes and applies collision response between two objects.
 * Calculates each object's XZ speed from position delta (current - previous),
 * sums them, decays existing push velocities (obj[0x2c], obj[0x34]) by 14/16,
 * then applies a push impulse proportional to speed and relative displacement.
 * Push is equal and opposite: obj2 gains, obj1 loses.
 *
 * Watcom fastcall:
 *   EAX = obj1 ptr, EDX = obj2 ptr, EBX = param3, ECX = param4,
 *   [stack1] = param5, [stack2] = param6
 *
 * param4-param3 → pushX displacement, param6-param5 → pushZ displacement.
 * Object layout: [0x00]=posX, [0x08]=posZ, [0x20]=prevX, [0x28]=prevZ,
 *                [0x2C]=pushVelX, [0x34]=pushVelZ.
 *
 * ret 0x18 cleans 6 dwords of stack (2 stack params + 4 spilled regs).
 * ===================================================================== */
void CollisionResponse(int *obj1, int *obj2, int p3, int p4, int p5, int p6)
{
    int dispX = p4 - p3;                                 /* ecx -= ebx */
    int dispZ = p6 - p5;                                 /* ebp -= edx */

    /* Speed of obj2: sqrt(deltaX^2 + deltaZ^2) from position delta */
    int dx2 = (obj2[0] - obj2[8]) / 256;                /* (obj2[0x00] - obj2[0x20]) >> 8 */
    int dz2 = (obj2[2] - obj2[10]) / 256;               /* (obj2[0x08] - obj2[0x28]) >> 8 */
    int distSq2 = dx2 * dx2 + dz2 * dz2;
    /* fsqrt — left on FPU stack as st(0) */

    /* Speed of obj1: same computation */
    int dx1 = (obj1[0] - obj1[8]) / 256;                /* (obj1[0x00] - obj1[0x20]) >> 8 */
    int dz1 = (obj1[2] - obj1[10]) / 256;               /* (obj1[0x08] - obj1[0x28]) >> 8 */
    int distSq1 = dx1 * dx1 + dz1 * dz1;
    /* fsqrt — now st(0)=speed1, st(1)=speed2 */

    /* fxch + trunc: truncate speed2 first, then speed1 */
    sr_double speed2_f = sr_sqrt((sr_double)distSq2);
    sr_double speed1_f = sr_sqrt((sr_double)distSq1);
    int speed2 = (int)speed2_f;
    int speed1 = (int)speed1_f;

    /* Sign-extend to 24-bit and sum */
    speed2 = (speed2 << 8) >> 8;                         /* shl 8; sar 8 */
    speed1 = (speed1 << 8) >> 8;
    int totalSpeed = speed2 + speed1;

    /* Decay existing push velocities by 14/16 for all 4 fields */
    obj1[0x2C / 4] = (obj1[0x2C / 4] * 14) / 16;       /* (val*8 - val)*2 / 16 = val*14/16 */
    obj1[0x34 / 4] = (obj1[0x34 / 4] * 14) / 16;
    obj2[0x2C / 4] = (obj2[0x2C / 4] * 14) / 16;
    obj2[0x34 / 4] = (obj2[0x34 / 4] * 14) / 16;

    /* Compute push impulse: displacement / 8, scaled by clamped speed / 8 */
    int pushX = (-dispX) / 8;                            /* neg ecx; sar 3 */
    int pushZ = (-dispZ) / 8;                            /* neg ebp; sar 3 */
    int speedScale = totalSpeed / 8;                     /* sar 3 */
    if (speedScale < 7) {
        speedScale = 7;                 /* clamp [7, 20] */
    }
    if (speedScale > 20) {
        speedScale = 20;
    }

    /* Binary applies the impulse with `sar 3` = ARITHMETIC FLOOR (0x420171/0x420177),
     * NOT truncate-toward-zero. Differs by 1 for negative products: e.g.
     * pushZ*speedScale = -148780 floors to -18598, but /8 truncates to -18597.
     * (pushX/pushZ and speedScale above DO use the sbb/sar truncate idiom = `/`.) */
    int impulseX = (pushX * speedScale) >> 3;            /* 0x420168 imul; 0x420171 sar 3 */
    int impulseZ = (pushZ * speedScale) >> 3;            /* 0x42016b imul; 0x420177 sar 3 */

    /* Apply equal and opposite: obj2 gains, obj1 loses */
    obj2[0x2C / 4] += impulseX;
    obj2[0x34 / 4] += impulseZ;
    obj1[0x2C / 4] -= impulseX;
    obj1[0x34 / 4] -= impulseZ;
}

/* =====================================================================
 * SweepPlayerCollision — FUN_004201a0 — 1016 bytes
 *
 * Swept player-vs-player collision detection and response.
 * Steps both players forward from their previous-frame positions in
 * up to 16 sub-steps, checking if their trajectories bring them within
 * a distance threshold derived from race progress.  On collision:
 *   - Computes approach direction at the collision point
 *   - In race sub-type 2 with player 0, can trigger player B finish
 *   - Plays collision SFX (0x18) with 5-frame cooldown
 *   - Calls CollisionResponse to apply push impulses
 *   - Sets +0x204 collision flags on both players
 *
 * Watcom fastcall: EAX = pA, EDX = pB (player struct pointers).
 * Called for each player pair: after each human's physics update,
 * check that player against all previously-updated players.
 * ===================================================================== */
void SweepPlayerCollision(Player *pA, Player *pB)        /* 0x4201a0 */
{
    /* 0x4201b3: Both players must have +0x78 == 0 (active/eligible) */
    if (pA->_unk_0x78 != 0) {
        return;                        /* 0x4201b3 */
    }
    if (pB->_unk_0x78 != 0) {
        return;                        /* 0x4201c1 */
    }

    /* 0x4201cc: Read current world positions */
    int curXA = pA->posX;                                   /* 0x4201cf */
    int curYA = pA->posY - 0xA000;                          /* 0x4201d7 */
    int curZA = pA->posZ;                                   /* 0x4201e8 */

    int curXB = pB->posX;                                   /* 0x4201f4 */
    int curYB = pB->posY - 0xA000;                          /* 0x4201fc */
    int curZB = pB->posZ;                                   /* 0x42020a */

    /* 0x420210: Read previous-frame Z */
    int prevZA = pA->prevPosZ;                              /* 0x420213 */
    int prevZB = pB->prevPosZ;                              /* 0x42021c */

    /* 0x420222: Read race progress (+0x1C4 >> 16) from both players */
    int progressA = pA->speedCategory;                      /* 0x420225: *(int*)(p+0x1C4)>>16 = short at 0x1C6 */
    int progressB = pB->speedCategory;                      /* 0x420237 */

    /* 0x420246: Threshold = ((progressA + progressB) / 4)^2 */
    int progressSum = progressA + progressB;                /* 0x420252 */
    int quarter = progressSum / 4;                          /* 0x420256 */
    int thresholdSq = quarter * quarter;                    /* 0x420264 — EAX */

    /* 0x420267: Read A's previous XY */
    int prevXA = pA->prevPosX;                              /* 0x42026d — esi */
    int prevYA = pA->prevPosY - 0xA000;                     /* 0x420273 — ebx */

    /* 0x420270: Deltas: B_current minus A_prev */
    int dxBA = curXB - prevXA;                              /* 0x420276 */
    int dyBA = curYB - prevYA;                              /* 0x420284 */
    int dzBA = curZB - prevZA;                              /* 0x42028c */

    /* 0x420292: Coarse distance squared (>> 16) */
    int dxBA16 = dxBA >> 16;                                /* 0x420295 */
    int dyBA16 = dyBA >> 16;                                /* 0x42029e */
    int dzBA16 = dzBA >> 16;                                /* 0x4202a7 */

    int distSqBA = dxBA16 * dxBA16 + dyBA16 * dyBA16 + dzBA16 * dzBA16;

    /* 0x4202dd: Read B's previous XY */
    int prevXB = pB->prevPosX;                              /* 0x4202e3 — edi */
    int prevYB = pB->prevPosY - 0xA000;                     /* 0x4202f1 — ecx */

    /* 0x420301: If threshold <= distance, players too far apart */
    if (thresholdSq <= distSqBA) {
        return;                    /* 0x420303 */
    }

    /* 0x420319: Close enough — begin swept convergence loop */
    int progressSumSq = progressSum * progressSum;          /* 0x42031c — [ebp-0x28] */

    /* 0x42031f: Per-step motion vectors: (current - prev) / 16 */
    int stepXA = (curXA - prevXA) / 16;                     /* 0x42031f */
    int stepYA = (curYA - prevYA) / 16;                     /* 0x420334 */
    int stepZA = (curZA - prevZA) / 16;                     /* 0x42034c */

    int stepXB = (curXB - prevXB) / 16;                     /* 0x420367 */
    int stepYB = (curYB - prevYB) / 16;                     /* 0x42037c */
    int stepZB = (curZB - prevZB) / 16;                     /* 0x420391 */

    int newDX, newDY, newDZ;

    /* 0x4203ac: iterate up to 16 sub-steps */
    for (int iter = 1; ; ) {                                    /* 0x4203ac */
        /* 0x4203b1: Step both positions forward */
        prevXB += stepXB;                                   /* 0x4203b1 */
        prevYB += stepYB;                                   /* 0x4203b4 */
        prevZB += stepZB;                                   /* 0x4203ba */

        prevXA += stepXA;                                   /* 0x4203bd */
        prevYA += stepYA;                                   /* 0x4203c0 */
        prevZA += stepZA;                                   /* 0x4203c6 */

        /* 0x4203c9: Delta at current sub-step (B - A) */
        newDX = prevXB - prevXA;                            /* 0x4203cb */
        newDY = prevYB - prevYA;                            /* 0x4203d2 */
        newDZ = prevZB - prevZA;                            /* 0x4203da */

        /* 0x4203e0: Distance at 12-bit precision */
        int dx12 = newDX >> 12;                             /* 0x4203e3 */
        int dy12 = newDY >> 12;                             /* 0x4203ef */
        int dz12 = newDZ >> 12;                             /* 0x4203fb */

        int newDistSq = dx12 * dx12 + dy12 * dy12 + dz12 * dz12;

        /* 0x42044f: Collision found if distance < threshold */
        if (newDistSq < progressSumSq) {
            break;               /* 0x420452 → fall through */
        }

        /* 0x42030a: No collision this step — try next */
        iter++;                                             /* 0x42030a */
        if (iter > 0x10) {
            return;                            /* 0x42030e — 16 steps max */
        }
    }

    /* 0x420458: Collision detected — compute approach direction (/16) */
    int approachDX = newDX / 16;                            /* 0x420458 */
    int __attribute__((unused)) approachDY = newDY / 16;    /* 0x42046b */
    int approachDZ = newDZ / 16;                            /* 0x420481 */

    /* 0x420491: XZ horizontal distance via sqrt (for collision scale) */
    int xzSq = approachDX * approachDX + approachDZ * approachDZ;
    int iSqrt = (int)sr_sqrt((sr_double)xzSq);             /* 0x4204a5 — fild+fsqrt+fistp */
    int __attribute__((unused)) collisionScale = iSqrt / 256 + 1;                  /* 0x4204b8 — unused by callee */

    /* 0x4204e2: Race sub-type 2 special: if pA is player 0, trigger pB finish */
    if (g_raceSubMode == SUBMODE_TAG) {                     /* 0x4204eb */
        if (pA == (Player *)g_playerBase) {                 /* 0x4204f0 */
            int finishState_val = pB->finishState;          /* *(int*)(p+0x1BE)>>16 = short at 0x1C0 */
            if (finishState_val != 2) {                     /* 0x420505 */
                pB->finishState = 2;                        /* 0x420512 */
                PlaySoundEffect(0x27, 0, 0);                      /* 0x420520 */
                g_p1CollectionCount++;                      /* 0x420525 */
            }
        }
    }

    /* 0x42052b: Play collision SFX with cooldown */
    if (g_sfxCooldownTimer == 0) {                          /* 0x42052b */
        g_sfxCooldownTimer = 5;                             /* 0x42053c */
        pA->sfxTrigger = 0x18;                              /* 0x420542 — SFX: bump */
    }

    /* 0x42054b: Apply collision response impulses */
    CollisionResponse((int *)pA, (int *)pB, prevXB, prevXA, prevZB, prevZA);  /* 0x420570 */

    /* 0x420575: Set collision flags on both players */
    pA->renderState = 0x30001;                              /* 0x420578 */
    pB->renderState = 0x30001;                              /* 0x420585 */
}

/**
 * TrackHeightCheck — 0x00420598 — 286 bytes
 * Checks if player is on a ramp surface and applies launch physics.
 * Returns 1 if ramp launch occurred, 0 otherwise.
 *
 * player = in_EAX (int*)
 */
int TrackHeightCheck(Player *player)
{
    /* Skip ramp-launch logic on REACTIVE FACTORY — binary 0x42059E
     * `cmp trackId,3` where binary convention 3=Factory (2026-07-13
     * un-crosswire; was wrongly skipping on Ruin, which killed human
     * ramp launches at the end of the Ruins demo). */
    if (g_trackId == TRACK_REACTIVE_FACTORY) {
        return 0;
    }

    /* Special ability launch active */
    if (player->_unk_0xFC != 0) {
        int velY = player->velY;
        velY += GRAVITY_PER_FRAME;
        player->velY = velY;
        player->posY += velY;

        /* If grounded again, cancel ability */
        if (player->groundedFlag != 0) {
            player->airTimer = 0;
            player->_unk_0xFC = 0;
        }
        return 1;
    }

    /* Check for ramp surface collision */
    if (g_trackSurfaceData == NULL) {
        return 0;
    }
    int surfaceIdx = player->hitSurfaceIdx;
    unsigned int surfaceFlags =
        (unsigned short)*(short *)((char *)g_trackSurfaceData + 12 + surfaceIdx * 16);

    if (surfaceFlags == 0) {
        return 0;
    }

    /* Only process if on special surface and grounded */
    if (player->overSurface != 1) {
        return 0;                  /* *(int*)(p+0xAE)>>16 = short at 0xB0 */
    }
    if (player->groundedFlag == 0) {
        return 0;
    }

    /* Ramp launch */
    int rampPower = surfaceFlags & 0xFFF;

    if ((surfaceFlags & 0x8000) != 0) {
        /* Speed ramp: add velocity along terrain normal */
        int normalX = player->surfNormX;                   /* *(int*)(p+0xB2)>>16 = short at 0xB4 */
        int normalZ = player->surfNormZ;                   /* *(int*)(p+0xB6)>>16 = short at 0xB8 */
        player->velX += normalX * rampPower;
        player->velZ += normalZ * rampPower;
    }
    else if ((surfaceFlags & 0x4000) != 0) {
        /* Stop ramp: kill horizontal velocity */
        player->velZ = 0;
        player->velX = 0;
    }

    /* Set airborne state */
    player->airTimer = 1;                       /* start air timer */
    player->_unk_0xFC = 1;                     /* specialAbility = launched */
    int launchVelY = rampPower * -0x1000;
    player->groundedFlag = 0;                  /* clear airborne flag */
    player->velY = launchVelY;
    player->posY += launchVelY;

    return 1;
}

/**
 * PlayerPhysicsMain — 0x004206B8 — 2632 bytes
 *
 * Core physics: gravity, acceleration, steering, friction, speed clamping,
 * rubber banding, velocity transform, and position integration.
 * Called once per player per frame.
 *
 * player = in_EAX (int*, points to 0x71C-byte PlayerStruct)
 */
void PlayerPhysicsMain(Player *player)
{
    short charId = player->charId;

    /* Timers */
    if (player->invincTimer != 0) {
        player->invincTimer -= 1;    /* invincibility timer */
    }

    /* Save previous position */
    player->prevPosX = player->posX;
    player->prevPosY = player->posY;
    player->prevPosZ = player->posZ;

    /* AI steering override — when player+0xA0 is set, use simplified AI
     * steering/drag instead of full physics (binary: call 0x4D5BE4, jmp end) */
    if (player->loopMode != 0) {
        AISteeringAndDrag(player);
        return;
    }

    /* Vehicle pre-physics */
    if (charId == CHAR_EGGMAN || charId == CHAR_EGG_ROBO) {
        /* Binary 0x420736-0x420775: compute ability bits from heading vs threshold.
         * threshold = (3 - val) * 8 + 4, where val = g_demoMode if ==DEMO_TITLE, else g_difficultyConfig.
         * If player+0x110 >> 16 >= threshold, abilityBits = 1, else 0. */
        int val = (g_demoMode == DEMO_TITLE) ? DIFF_NORMAL : g_difficultyConfig;
        int threshold = (3 - val) * 8 + 4;
        unsigned short abilityBits = (player->_unk_0x112 >= threshold) ? 1 : 0;
        VehiclePhysicsPreUpdate(player, abilityBits);
    }

    /* Track surface interaction */
    short surfaceResult = TrackHeightCheck(player);

    if (surfaceResult == 0) {
        /* Airborne — handle ramp launch and character abilities */
        if ((player->physicsFlags & 1) == 1) {
            player->airTimer = 1;          /* start air timer */
            player->groundedFlag = 0;
            player->physicsFlags &= 0xFE;
            int jumpPower = g_charStatsTable[charId * 10 + 5]; /* +0x14 in stride 0x28 */
            player->velY = -(jumpPower * 0x1000) / 2;
        }

        if (player->groundedFlag == 0 && player->_unk_0x1D8 != 0) {
            player->_unk_0x1D8 -= 1;
            if (player->velY > 0) {
                if (charId == CHAR_TAILS) {          /* Tails: hover */
                    player->abilityState = 1;
                    player->velY -= GRAVITY_PER_FRAME;
                }
                else if (charId == CHAR_KNUCKLES || charId == CHAR_METAL_KNUCKLES) {  /* Knuckles: glide */
                    player->abilityState = 4;
                    player->velY -= 0x2E00;
                }
                else if (charId == CHAR_SONIC) {     /* Sonic: double jump */
                    int jp = g_charStatsTable[charId * 10 + 5];
                    player->jumpCounter = 0;
                    player->velY -= (jp << 12) / 2;
                }
            }
        }
        else {
            player->_unk_0x1D8 = 0;
            player->abilityState = 0;
            player->physicsFlags &= 0xFE;
        }
    }

    if (player->groundedFlag != 0) {
        player->physicsFlags = 0;
        player->physicsFlags2 = 0;   /* binary 0x4208cb: mov word[0x1da],0 zeros BOTH bytes */
    }

    /* Gravity */
    int velY = player->velY;
    player->velY = velY + GRAVITY_PER_FRAME;
    if (velY + GRAVITY_PER_FRAME > TERMINAL_VELOCITY) {
        player->velY = TERMINAL_VELOCITY;
    }
    player->posY += player->velY;      /* posY += velocityY */

    /* Read and modify physics parameters */
    int drag;
    int friction;
    int accel;
    int maxSpeed;

    if (g_raceType == RACE_SPECIAL) {
        drag     = player->paramDrag * 3;
        friction = player->paramFriction;
        accel    = player->paramAccel;
        maxSpeed = player->paramMaxSpeed;
    }
    else {
        int progressDelta = ((unsigned int)player->trackProgress >> 21) -
                            ((unsigned int)g_playerBase->trackProgress >> 21);

        /* rubber banding: double drag */
        if (progressDelta > g_rubberBandThreshold || progressDelta < 0) {
            drag = player->paramDrag * 2;
        }
        else {
            drag = player->paramDrag;
        }

        if (player->ring.wallPresence != 0) {
            drag = player->paramDrag * player->ring.wallPresence;
        }

        /* Binary 0x420987-0x4209AC: gradual speed decay when player+0x5C >> 16 > 2
         * and base maxSpeed exceeds 68000. Reduces stored maxSpeed by 1200/frame. */
        if (player->lapsCompleted > 2 && player->paramMaxSpeed > 0x109A0) {
            player->paramMaxSpeed -= 0x4B0;
        }

        friction = player->paramFriction;
        accel    = player->paramAccel;
        maxSpeed = player->paramMaxSpeed;

        /* Speed lock */
        if ((player->physicsFlags2 & 0x10) != 0) {
            maxSpeed = 300000;
        }

        /* Rubber band speed/accel adjustment.
         * Binary: accel term is the sbb/sar 0xa TRUNCATE idiom (0x420a08/0x420a49)
         * = `/1024`; maxSpeed term is `shr 0xa` = UNSIGNED shift (0x420a1b/0x420a61).
         * Both were plain signed `>>10` here (coincidentally equal for the positive
         * values seen, but not bit-faithful). */
        int rubberFactor;
        if (progressDelta < 0) {
            rubberFactor = (progressDelta * -256) / g_posDataCount4;
            if (rubberFactor > 256) {
                rubberFactor = 256;
            }
            accel    += (accel * rubberFactor) / 1024;                              /* sbb/sar 0xa = truncate */
            maxSpeed += (int)((unsigned int)(rubberFactor * maxSpeed) >> 10);       /* shr 0xa = unsigned */
        } else {
            rubberFactor = (progressDelta * 256) / g_posDataCount4;
            if (rubberFactor > 256) {
                rubberFactor = 256;
            }
            accel    -= (accel * rubberFactor) / 1024;                              /* sbb/sar 0xa = truncate */
            maxSpeed -= (int)((unsigned int)(rubberFactor * maxSpeed) >> 10);       /* shr 0xa = unsigned */
        }
    }

    /* Amy boost */
    if ((player->physicsFlags2 & 8) != 0 && charId == CHAR_AMY &&
        player->abilityTimer == 0)
    {
        player->abilityTimer = 0x78;      /* 120 frames of boost */
        player->physicsFlags2 &= 0xF7;
    }

    if (player->abilityTimer > 0 && charId == CHAR_AMY) {
        if (player->abilityTimer > 0x3C) {
            maxSpeed *= 2;
            accel *= 4;
        }
        player->abilityTimer -= 1;
    }

    if (g_autoSteerFlag != 0) {
        accel = g_charStatsTable[charId * 10 + 1];   /* +0x04 in stride 0x28 */
    }

    /* Steering angle */
    int turnRateMax = player->turnRateLimit;
    /* Binary 0x420afb-0x420b6f: compute direction from current position to
     * waypoint target (player[0x4E], player[0x50]), then atan2 to get yaw.
     * The <<12 converts waypoint coords to position space, >>12 normalizes. */
    int dxWp = ((player->waypointX << 12) - player->posX) >> 12;
    int dzWp = ((player->waypointZ << 12) - player->posZ) >> 12;
    if (player->ring.wallPresence == 0) {  /* no wall */
        /* Binary: fild dzWp; fild dxWp; call 0x4e08fd (wrapper swaps FPU stack)
         * Effective: atan2(dxWp, dzWp) — because yaw=0 means forward=+Z,
         * so the angle is measured from the +Z axis toward +X. */
        sr_double angle = sr_atan2((sr_double)dxWp, (sr_double)dzWp);
        int angle12 = (int)(angle * 4096.0 / (2.0 * 3.14159265358979323846));
        angle12 = (angle12 << 4) >> 4;  /* sign-extend 28 bits (binary: shl 4, sar 4) */
        player->targetYaw = (short)(angle12 & 0xFFF);
        if (player->targetYaw < 0) {
            ((char *)&player->targetYaw)[1] += 0x10;
        }
    }

    /* Velocity decomposition (world → local) */
    int yawIdx = player->angleYaw;
    int invYawIdx = (0x1000 - yawIdx) & 0xFFF;

    /* Velocity decomposition — verified against binary 0x420ba6-0x420bee.
     * Binary table mapping: 0x92668c = g_cosTable = cos, 0x92568c = g_sinTable = sin
     * [ebp-0x4c] = g_cosTable[inv] (cos), edi = g_sinTable[inv] (sin)
     * FIRST  = velX × cos(invYaw) + velZ × sin(invYaw)
     * SECOND = -velX × sin(invYaw) + velZ × cos(invYaw)
     * FIRST → [ebp-0x18] → player[0x12], SECOND → edi → player[0x11] */
    int forwardSpeed = (int)(
        ((long long)player->velX * (long long)(g_cosTable[invYawIdx] >> 2) +
         (long long)player->velZ * (long long)(g_sinTable[invYawIdx] >> 2)) / 0x1000);

    int lateralSpeed = (int)(
        ((long long)player->velZ * (long long)(g_cosTable[invYawIdx] >> 2) +
         (long long)(-player->velX) * (long long)(g_sinTable[invYawIdx] >> 2)) / 0x1000);

    /* Dynamic maxSpeed override — binary 0x420BF0-0x420C66
     * When player+0x3E >> 16 == 1 and charId is NOT 3..8 (vehicles),
     * recompute maxSpeed from charStats[8] scaled by player+0x1B8. */
    if (player->dynamicSpeedMode == 1 && (charId < CHAR_AMY || charId > CHAR_EGG_ROBO)) {
        int statsVal = g_charStatsTable[charId * 10 + 8];
        int temp = (statsVal * 9 * 4) / 32;                     /* 0x420c38 sbb/sar 5 = truncate */
        int speedField = (int)(unsigned short)player->speedField + 0x80;  /* 0x420c45 xor;mov ax = zero-extend */
        int dynMax = (temp * speedField) / 256;                /* 0x420c58 sbb/sar 8 = truncate */
        maxSpeed = dynMax;
    }

    /* Speed clamping — binary uses unsigned compare (jbe) */
    if ((unsigned int)lateralSpeed > (unsigned int)maxSpeed) {
        lateralSpeed = maxSpeed;
    }

    /* Steering / yaw update */
    if (player->finishState != FINISH_DONE) {
        int yawDelta = (player->targetYaw - player->angleYaw) & 0xFFF;
        if (yawDelta > 0x7FF) {
            yawDelta = -(0x1000 - yawDelta);
        }

        if (player->ring.wallPresence != 0) {
            turnRateMax *= 2;
        }

        if ((int)turnRateMax < yawDelta) {
            yawDelta = turnRateMax;
        }
        if (yawDelta < -(int)turnRateMax) {
            yawDelta = -turnRateMax;
        }

        player->yawDelta = (short)yawDelta * 16;
        player->angleYaw = (player->angleYaw + yawDelta) & 0xFFF;
        player->yawDelta = (short)yawDelta * -16;
    }

    /* AI race distance computation — binary 0x420D0C-0x420DC6
     * Only when g_raceSubMode == SUBMODE_TAG (tag/special mode). Computes distance from
     * player to target, wraps around track length, looks up a per-character
     * threshold from ROM table, and stores scaled result to g_raceDistThreshold. */
    int absDist = 0;  /* [ebp-0x34] — used below */
    int wrappedDist = 0;  /* [ebp-0x24] */
    if (g_raceSubMode == SUBMODE_TAG) {
        int rawDist = (int)g_playerBase->trackProgress - player->trackProgress;  /* [ebp-0x24] */
        wrappedDist = rawDist;
        absDist = IABS(rawDist);                             /* [ebp-0x34] */
        int halfTrack = IABS(g_posDataCount4) / 2;
        if (absDist > halfTrack) {
            absDist = g_posDataCount4 - absDist;
        }
        /* Wrap negative distance */
        if (rawDist < 0) {
            wrappedDist = rawDist + g_posDataCount4;
        }
        /* Lookup per-character threshold: table[p0_charId * 16 + this_charId] */
        short p0CharId = g_playerBase->charId;
        int tableVal = (int)g_charDistTable[(int)p0CharId * 16 + (int)charId];
        g_raceDistThreshold = tableVal;
        /* Track 2: scale by 3/4 */
        if (g_trackId == TRACK_RADICAL_CITY) {
            g_raceDistThreshold = (tableVal * 3 + ((tableVal * 3) >> 31)) >> 2;
        }
        /* Scale by g_posDataCount4 / 128 */
        g_raceDistThreshold = (g_raceDistThreshold * g_posDataCount4 +
            ((g_raceDistThreshold * g_posDataCount4) >> 31)) >> 7;
    }

    /* Apply acceleration / braking / friction */
    int finishState = player->finishState;

    /* Binary 0x420dc7-0x420e1c: RACING adds accel to lateralSpeed.
     * Also has a turn-timer mechanism (player+0x1C2) for hard deceleration.
     * Case 1 (BRAKING): subtracts accel.
     * Default (FINISHED): strong damping. */
    if (finishState == FINISH_RACING) {
        int newLat = accel + lateralSpeed;
        short turnTimer = player->turnTimer;
        if (turnTimer != 0) {
            player->turnTimer = turnTimer - 1;
            if (player->turnTimer > 0xF0) {
                /* Hard decel: subtract double accel */
                lateralSpeed -= accel * 2;
                if (lateralSpeed < 0) {
                    lateralSpeed = 0;
                }
            }
            else {
                lateralSpeed = newLat;
            }
        }
        else {
            lateralSpeed = newLat;
        }
        /* binary 0x420E15-0x420F18: AI turnaround in tag mode (RACING) */
        if (g_raceSubMode == SUBMODE_TAG) {
            if (absDist > g_raceDistThreshold) {
                player->finishState = 1;  /* signal hard decel */
            }
            if (player->turnTimer == 0) {
                int dir = player->directionFlip;
                int halfTrack = IABS(g_posDataCount4) / 2;
                /* Turn around when the player ends up on the other side of the
                 * track, so the AI keeps running away rather than toward you.
                 * wrappedDist is (player0.progress - ai.progress) normalised
                 * into [0, trackLen): small = AI behind, large = AI ahead.
                 *
                 * 0x420E5F: cmp halfTrack, wrapped / jg  flip   (dir 0)
                 * 0x420E84: cmp halfTrack, wrapped / jge exit   (dir 1)
                 * A dir-0 test that fails falls into the dir-1 check, which
                 * exits immediately — so the two cases are exclusive. */
                int needFlip = 0;
                if (dir == 0) {
                    needFlip = (halfTrack > wrappedDist);
                }
                else if (dir == 1) {
                    needFlip = (halfTrack < wrappedDist);
                }
                if (needFlip) {
                    /* Full waypoint/state reset and direction flip */
                    player->ring.tail = 0;
                    player->ring.head = 0;
                    player->aiAccumDist = 0;
                    player->distanceAccum = 0;
                    player->steerState = 0;
                    player->lapCounter = 0;
                    player->modelCharId = 0;
                    player->_unk_0x1D6 = 0;
                    player->_unk_0x1DC = 0;
                    player->turnTimer = 0xFF;  /* turn timer */
                    player->directionFlip ^= 1;     /* flip direction */
                    player->aiAccel = (unsigned int)player->speedModifier;
                    player->aiSpeed = (unsigned int)player->speedModifier;
                }
            }
        }
    }
    else if (finishState == FINISH_BRAKING) {
        lateralSpeed = lateralSpeed - accel;
        if (lateralSpeed < 0) {
            lateralSpeed = 0;
        }
        /* binary 0x420F2F-0x420FE2: AI turnaround in tag mode (BRAKING) */
        if (g_raceSubMode == SUBMODE_TAG) {
            if (absDist < g_raceDistThreshold) {
                int halfTrack = IABS(g_posDataCount4) / 2;
                player->directionFlip = 0;
                player->finishState = 0;
                if (halfTrack > wrappedDist) {
                    player->directionFlip = 1;
                }
                /* Full waypoint/state reset */
                player->ring.head = 0;
                player->aiAccumDist = 0;
                player->distanceAccum = 0;
                player->steerState = 0;
                player->lapCounter = 0;
                player->modelCharId = 0;
                player->_unk_0x1D6 = 0;
                player->_unk_0x1DC = 0;
                player->ring.tail = 0;
                player->aiAccel = (unsigned int)player->speedModifier;
                player->aiSpeed = (unsigned int)player->speedModifier;
            }
        }
    }
    else {
        /* FINISHED: strong damping */
        drag = player->paramDrag;
        friction = player->paramFriction;
        forwardSpeed -= forwardSpeed / 8;
        lateralSpeed -= lateralSpeed / 8;
        player->paramAccel = 0;
        if (IABS(forwardSpeed) < 8) {
            forwardSpeed = 0;
        }
        if (IABS(lateralSpeed) < 8) {
            lateralSpeed = 0;
        }
    }

    /* drag on forwardSpeed, friction on lateralSpeed */
    if (forwardSpeed < 0) {
        forwardSpeed += drag;
        if (forwardSpeed > 0) {
            forwardSpeed = 0;
        }
    } else if (forwardSpeed > 0) {
        forwardSpeed -= drag;
        if (forwardSpeed < 0) {
            forwardSpeed = 0;
        }
    }

    if (lateralSpeed < 0) {
        lateralSpeed += friction;
        if (lateralSpeed > 0) {
            lateralSpeed = 0;
        }
    } else if (lateralSpeed > 0) {
        lateralSpeed -= friction;
        if (lateralSpeed < 0) {
            lateralSpeed = 0;
        }
    }

    /* Transform back to world and integrate */
    /* Reconstruction: worldVelX = forward*sin + lateral*cos
     *                 worldVelZ = forward*cos - lateral*sin */
    int sinYaw = g_sinTable[yawIdx];
    int cosYaw = g_cosTable[yawIdx];

    /* Reconstruction — verified against binary 0x42108f-0x4210d4.
     * [ebp-0x38] = g_cosTable[yaw] (cos), [ebp-0x3c] = g_sinTable[yaw] (sin)
     * worldVelX = FIRST × cos(yaw) + SECOND × sin(yaw)
     * worldVelZ = SECOND × cos(yaw) - FIRST × sin(yaw) */
    int worldVelX = (int)(
        ((long long)forwardSpeed * (long long)(cosYaw >> 2) +
         (long long)lateralSpeed * (long long)(sinYaw >> 2)) / 0x1000);

    int worldVelZ = (int)(
        ((long long)lateralSpeed * (long long)(cosYaw >> 2) +
         (long long)(-forwardSpeed) * (long long)(sinYaw >> 2)) / 0x1000);

    player->velZ = worldVelZ;
    player->velX = worldVelX;
    player->forwardSpeed = lateralSpeed;
    player->lateralSpeed = forwardSpeed;

    /* Position integration. */
    player->posX += worldVelX;
    player->posZ += worldVelZ;

    /* Store yaw in animation frame field */
    player->moveMode = (unsigned short)player->angleYaw & 0xFFF;
}
