#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "player_struct.h"
#include <math.h>

/* Signed divide toward zero: val / 4096 */
#define SDIV4096(val) ((val) / 4096)

/* Sub-functions called by UpdateHumanPlayerPhysics */
extern void VehiclePhysicsPreUpdate(Player *player, unsigned short abilityBits);  /* 0x00480910 — EAX=player, EDX=param_2&0x620 */
extern int g_catchUpToggle;                       /* 0x008FD450 — options item 11 (0/1): 2P catch-up toggle */
extern void ApplyDragAndSteering(int *obj, int flags);  /* 0x004D59F8 — loopMode/loop input physics (vehicle_physics.c) */

extern int Random(void);          /* 0x004E1342 — returns 0..0x7FFF */

extern unsigned short *g_randomStream;
extern int g_bouncePosition2;   /* 0x901C5C */
extern int g_bounceVelocity2;   /* 0x901C60 */

/**
 * CalcJumpVelocity — 0x004853A8 — 574 bytes
 * Verified line-by-line against the binary; each statement below is tagged with
 * the VMA of the instruction(s) it corresponds to.
 *
 * player = EAX, jumpPower = EDX (Watcom fastcall).
 * Returns vertical velocity (negative = upward); also writes player->vel{X,Y,Z}.
 */
int CalcJumpVelocity(Player *player, int jumpPower)
{
    int result = jumpPower * -0x1000;             /* 0x4853B6: [ebp-14] = -jumpPower<<12 (default) */

    /* Gate: needs surface velocity (velX|velZ) AND a surface normal (normX|normZ) */
    int velXraw = player->velX >> 12;             /* 0x4853C0: [esi+2C] >> 12 */
    int velZraw = player->velZ >> 12;             /* 0x4853CA: [esi+34] >> 12 */
    int normX = player->surfNormX;                /* 0x4853D8 / 0x4853F6: [esi+B2]>>16 */
    int normZ = player->surfNormZ;                /* 0x4853E5 / 0x48541A: [esi+B6]>>16 */

    if ((velXraw != 0 || velZraw != 0) && (normX != 0 || normZ != 0)) {  /* 0x4853C6-0x4853F0: gates (je 0x4855DA) */
        int normY = player->surfNormY;            /* 0x485408: [esi+B4]>>16 */
        int normYscaled = normY * 5;              /* 0x485411: lea [eax*4]+eax */

        /* |normal| = sqrt(x² + (5y)² + z²) */
        sr_double normMagSq = (sr_double)(normX * normX + normYscaled * normYscaled + normZ * normZ);  /* 0x485402-0x485435 */
        int normMag = (int)(sr_sqrt(normMagSq));  /* 0x485438: fild/fsqrt/trunc(0x4E08CA)/fistp; shl8/sar8 */

#ifdef DEFENSIVE
        if (normMag == 0)
            normMag = 1;            /* (defensive; no binary counterpart) */
#endif

        int jumpX = (int)(((long long)(normX << 12) * (long long)jumpPower) / (long long)normMag);    /* 0x485456: (normX<<12)*jp/mag */
        result    = (int)(((long long)(normY * 0x5000) * (long long)jumpPower) / (long long)normMag); /* 0x48546C: (5normY<<12)*jp/mag → [ebp-14] */
        int jumpZ = (int)(((long long)(normZ << 12) * (long long)jumpPower) / (long long)normMag);    /* 0x485481: (normZ<<12)*jp/mag */

        int deltaX = player->prevPosX - g_prevPrevPosX;  /* 0x48548D: [esi+20] - [0x6897C8] */
        int deltaY = player->prevPosY - g_prevPrevPosY;  /* 0x485498: [esi+24] - [0x6897CC] */
        int deltaZ = player->prevPosZ - g_prevPrevPosZ;  /* 0x4854A6: [esi+28] - [0x6897D0] */
        int dx12 = -deltaX >> 12;                 /* 0x4854B7: neg; sar 12 */
        int dz12 = -deltaZ >> 12;                 /* 0x4854BD: neg; sar 12 */

        if (dx12 == 0 && dz12 == 0) {             /* 0x4854C9: (jne 0x4854E5 → else) */
            /* No horizontal movement — apply jump direction directly */
            player->velZ = jumpZ;                 /* 0x4854D4: [esi+34] */
            player->velX = jumpX;                 /* 0x4854D1/0x4854D7: [esi+2C] */
            player->velY = result;                /* 0x4854DA: [esi+30]; jmp 0x4855DA */
        }
        else {
            /* Horizontal movement — project jump onto movement direction */
            sr_double moveMagSq = (sr_double)(dx12 * dx12 + dz12 * dz12);  /* 0x4854E5 */
            int moveMag = (int)(sr_sqrt(moveMagSq));  /* 0x4854FA: fild/fsqrt/trunc/fistp; shl8/sar8 */

#ifdef DEFENSIVE
            if (moveMag == 0)
                moveMag = 1;        /* (defensive) */
#endif

            int moveNormX = (dx12 << 12) / moveMag;  /* 0x485510 */
            int moveNormZ = (dz12 << 12) / moveMag;  /* 0x485522 */

            /* dot(normal, moveDir) */
            int dot = normZ * moveNormZ + normX * moveNormX;  /* 0x485535-0x48555D */
            int dotScaled = dot / 4096;           /* 0x48555D: /4096 (trunc toward 0) */

            if (dotScaled < 1) {                  /* 0x48556D: (jg 0x48558F → reflect) */
                /* Against the surface — add jump impulse to movement */
                player->velX = deltaX + jumpX;    /* 0x485571: [esi+2C] */
                player->velY = deltaY + result;   /* 0x48557D: [esi+30] */
                deltaZ = deltaZ + jumpZ;          /* 0x485588: → [esi+34] via 0x4855D7 */
            }
            else {
                /* With the surface — reflect jump off surface */
                int nxScaled = normX * dotScaled; /* 0x48558F */
                int nzScaled = normZ * dotScaled; /* 0x4855A4 */

                player->velX =
                    moveMag * ((nxScaled / 2048) - moveNormX);   /* 0x485592-0x4855C6: [esi+2C] (sar 0xB) */
                deltaZ =
                    moveMag * ((nzScaled / 2048) - moveNormZ);   /* 0x4855AF-0x4855CC (sar 0xB) */
                player->velY = jumpPower * -0x1000;  /* 0x4855CF: [esi+30] = -jumpPower<<12 */
            }
            player->velZ = deltaZ;                /* 0x4855D7: [esi+34] */
        }
    }

    return result;                                /* 0x4855DA: return [ebp-14]; ret @0x4855E5 */
}

/**
 * ProcessGroundContact — 0x004855E8 — 343 bytes
 * Checks if the player is on a ramp surface and applies launch physics.
 * Returns the ground height (player[0xE]) — in the original, this
 * was returned via the ECX register as a side effect.
 *
 * p_player = player pointer. param_2 unused.
 */
static int ProcessGroundContact(Player *player)
{
    if (g_trackSurfaceData == NULL) {
        return player->groundHeight;
    }

    int surfIdx = player->hitSurfaceIdx;
    TerSurface *surf = (TerSurface *)g_trackSurfaceData + surfIdx;
    unsigned int surfFlags = (unsigned short)surf->misc;

    if (surfFlags != 0 &&
        player->overSurface == 1 &&
        player->groundedFlag != 0)
    {
        unsigned int rampPower = surfFlags & 0xFFF;

        if ((surfFlags & 0x8000) != 0) {
            /* Speed boost ramp: add velocity along surface normal */
            player->velX += (player->surfNormX) * (int)rampPower;
            player->velZ += (int)rampPower * (player->surfNormZ);
            if (g_trackId == TRACK_RADICAL_CITY) {
                /* City track: alternating ramp sound effect */
                unsigned short rv = *g_randomStream;
                g_randomStream++;
                player->sfxTrigger = (rv & 1) + 0x33;
            }
        }
        else if ((surfFlags & 0x4000) != 0) {
            /* Stop ramp: kill horizontal velocity */
            player->velZ = 0;
            player->velX = 0;
            if (g_trackId == TRACK_RADICAL_CITY || g_trackId == TRACK_REACTIVE_FACTORY || g_trackId == TRACK_REGAL_RUIN) {
                player->sfxTrigger = 0x32;
            }
        }
        else {
            /* Minor surface effect — binary 0x4856C5. */
            if (g_sfxCooldownTimer == 0 && g_trackId == TRACK_REACTIVE_FACTORY) {
                player->sfxTrigger = 0x32;
                g_sfxCooldownTimer = 5;
            }
        }

        /* Set airborne + compute launch velocity */
        player->airTimer = 1;        /* start air timer */
        player->groundedFlag = 0;        /* clear grounded flag */
        player->velY = (int)rampPower * -0xD05;  /* launch velocity */
        if (player->charId < CHAR_AMY || player->charId > CHAR_EGGMAN) {
            player->animId = 0x10;
        }
        return player->groundHeight;  /* ground height */
    }

    return player->groundHeight;  /* no contact — return current ground height */
}

/**
 * UpdateHumanPlayerPhysics — 0x485768 — 4011 bytes
 * Validated register-by-register against binary disassembly.
 *
 * Binary register/local → C variable mapping:
 *   ESI       = maxSpeed     = stats[0]  MaxSpeed
 *   [ebp-0x28]= accel        = stats[1]*3/2  Accel
 *   ECX       = turnRate     = stats[2]  TurnRate
 *   [ebp-0x1c]= friction     = stats[3]  Friction (lateral damping)
 *   [ebp-0x24]= fwdDrag      = stats[4]  Drag (forward drag, x2 per frame)
 *   [ebp-0x40]= jumpParam    = stats[5]/2  JumpPower (pre-halved)
 *   [ebp-0x2c]= abilSpeed    = stats[6]  AbilSpeed (Tails/Knuckles/Amy)
 *   [ebp-0x38]= landingSpeed = stats[7]  LandingSpeed
 *   EDI       = waterSpeed   = stats[8]  WaterSpeed
 *   [ebp-0x48]= uwMaxSpeed   = stats[9]  UWMaxSpeed
 *   [ebp-0x44]= justLanded   — set in ground state, prevents jump same frame
 *   [ebp-0x50]= inWater      — set in water section, checked in steering/clamping
 */
void UpdateHumanPlayerPhysics(Player *p_player, unsigned short param_2)
{
    if (p_player == NULL) {
        return;
    }

    int justLanded = 0;  /* [ebp-0x44] — set in charId<3 ground state landing */
    short charId = p_player->charId;
    int *stats = g_charStatsTable + charId * 10;

    /* Load character stats — validated against binary at 0x485785-0x485891 */
    int maxSpeed = stats[0];            /* ESI */
    int accel = (stats[1] * 3) / 2;    /* [ebp-0x28] */
    int turnRate = stats[2];            /* ECX — TurnRate (steering) */
    int friction = stats[3];            /* [ebp-0x1c] — Friction (lateral damping) */
    int fwdDrag = stats[4];             /* [ebp-0x24] — Drag (forward) */
    int jumpParam = stats[5] / 2;       /* [ebp-0x40] — pre-halved for CalcJumpVelocity */
    int abilSpeed = stats[6];            /* [ebp-0x2c] — AbilSpeed */
    int landingSpeed = stats[7];         /* [ebp-0x38] — LandingSpeed */
    int waterSpeed = stats[8];           /* EDI — WaterSpeed */
    int uwMaxSpeed = stats[9];           /* [ebp-0x48] — UWMaxSpeed */

    /* 2P catch-up: trailing player gets maxSpeed*5/4 (clamped). Binary 0x4858a8
     * reads [0x8FD450] = g_catchUpToggle (options item 11, 0/1) — NOT the GP
     * difficulty at 0x6DD834. */
    if (g_raceType == RACE_MULTIPLAYER && g_raceSubMode == SUBMODE_NORMAL && g_catchUpToggle == 1) {
        if (p_player->racePosition > 1) {
            int sign = maxSpeed * 5 >> 31;
            maxSpeed = (int)((maxSpeed * 5 + sign * -4) - (unsigned int)((sign << 1) < 0)) >> 2;
            if (maxSpeed > 0x3C000) {
                maxSpeed = 0x3C000;
            }
        }
    }

    /* Weather effects on friction/drag (binary: modifies [ebp-0x1c] and [ebp-0x24]) */
    if ((g_raceType != 2 || g_raceSubMode > 1) &&
        (charId < CHAR_EGGMAN || charId > CHAR_METAL_KNUCKLES) &&
        (charId != CHAR_EGG_ROBO || p_player->animId != 0xC) &&
        (charId != CHAR_SUPER_SONIC || p_player->animId != 0xC) &&
        p_player->groundedFlag != 0 && g_trackId != TRACK_RADIANT_EMERALD)
    {
        if (g_weatherType == WEATHER_RAIN) {
            friction = (friction * 5) / 6;
            fwdDrag = (fwdDrag * 5) / 6;
        }
        else if (g_weatherType == WEATHER_SNOW) {
            if (p_player->collisionResult == 0) {
                friction = (friction << 2) / 6;
                fwdDrag = (fwdDrag << 2) / 6;
            }
            else {
                friction = (friction * 3) / 6;
                fwdDrag = (fwdDrag * 3) / 6;
            }
        }
    }

    /* Save previous position */
    g_prevPrevPosX = p_player->prevPosX;
    g_prevPrevPosY = p_player->prevPosY;
    g_prevPrevPosZ = p_player->prevPosZ;
    p_player->savedVelocity = p_player->posY - p_player->prevPosY;  /* delta Y from last frame */
    p_player->prevPosX = p_player->posX;
    p_player->prevPosY = p_player->posY;
    p_player->prevPosZ = p_player->posZ;

    /* Character-specific ability handling (large switch on charId) */
    switch (charId) {
        case CHAR_SONIC:
            if (p_player->airTimer == 10) {
                if (p_player->_unk_0x74 == 2) {
                    p_player->sfxTrigger = 0xD;
                    p_player->velY = -0x20000;
                    p_player->airTimer = 1;
                }
            }
            else if (p_player->airTimer > 1) {
                p_player->airTimer += 1;
            }
            break;
        case CHAR_TAILS:
            if (p_player->airTimer == 2 && p_player->_unk_0x74 == 2) {
                p_player->abilityState = 1;
            }
            if (p_player->abilityState == 1) {
                if (p_player->_unk_0x74 != 2 || p_player->loopMode != 0 ||
                    p_player->groundedFlag != 0 || p_player->airTimer > 0x3C) {
                    p_player->_unk_0x74 = (short)0xFFFF;
                    p_player->abilityState = 0;
                }
                p_player->airTimer += 1;
                maxSpeed = abilSpeed;       /* binary: ESI = [ebp-0x2c] = stats[6] */
            }
            break;
        case CHAR_KNUCKLES:
        case CHAR_METAL_KNUCKLES:
            if (p_player->airTimer == 2 && p_player->_unk_0x74 == 2) {
                p_player->abilityState = 4;
            }
            if (p_player->abilityState == 4) {
                if (p_player->_unk_0x74 != 2 || p_player->loopMode != 0 ||
                    p_player->groundedFlag != 0) {
                    p_player->_unk_0x74 = (short)0xFFFF;
                    p_player->abilityState = 0;
                }
                p_player->airTimer = 3;
                maxSpeed = abilSpeed;       /* binary: ESI = [ebp-0x2c] = stats[6] */
            }
            break;
        case CHAR_AMY:
            p_player->_unk_0x80 = 0;
            if ((param_2 & 0x620) != 0 && p_player->abilityState != 2) {
                if (p_player->abilityTimer == 0) {
                    p_player->abilityTimer = 0x186;
                }
                if (p_player->abilityTimer < 300) {
                    p_player->_unk_0x80 = 1;
                }
            }
            if (((unsigned short)p_player->abilityTimer & 0x7FFF) < 0x12D) {
                if (p_player->abilityState == 3) {
                    p_player->abilityState = 0;
                }
                p_player->_unk_0x42 = 0;
            } else {
                if (p_player->abilityState != 2) {
                    p_player->abilityState = 3;
                }
                turnRate /= 4;              /* binary: ECX /= 4 at 0x485c3f-0x485c4e */
                param_2 |= 0x100;
                accel <<= 2;
                p_player->_unk_0x42 = 2;
                maxSpeed = abilSpeed;       /* binary: ESI = [ebp-0x2c] = stats[6] */
            }
            if (p_player->abilityTimer > 0) {
                p_player->abilityTimer -= 1;
            }
            break;
        case CHAR_EGGMAN:
        case CHAR_EGG_ROBO:
            /* Binary: EDX = param_2 & 0x620 (ability bits) at 0x485c9c-0x485cab */
            VehiclePhysicsPreUpdate(p_player, param_2 & 0x620);
            break;
        /* TAILS DOLL, not Metal Sonic. The charId jump table at 0x485740
         * (dispatched by `jmp dword cs:[eax*4+0x485740]` at 0x485a85) sends
         * charId 6 here to 0x485cb5; charId 5 goes straight to 0x485e20, the
         * switch's common exit — Metal Sonic has NO ability case and must fall
         * to `default`. This body was mislabelled CHAR_METAL_SONIC, which gave
         * Metal Sonic the hover and its liftoff jump and left Tails Doll with
         * nothing. The body itself always matched the binary; only the case
         * label was wrong, which is why a body-only audit missed it. */
        case CHAR_TAILS_DOLL:
            if ((param_2 & 0x620) == 0 || p_player->loopMode != 0) {
                if (p_player->abilityState == 10) p_player->abilityState = 0;
            } else {
                p_player->abilityState = 10;
                if (p_player->groundedFlag == 1) {
                    p_player->velY = -0x4000;
                    p_player->airTimer = 1;
                    p_player->groundedFlag = 0;
                }
                if (p_player->groundHeight - 0x60000 < p_player->posY && p_player->velY > -0x8000) {
                    p_player->velY -= 0x3000;
                }
                if (p_player->posY < p_player->groundHeight + 0x60000 && p_player->velY < 0x8000) {
                    p_player->velY -= 0x1000;
                }
            }
            break;
        case CHAR_SUPER_SONIC:
            if (p_player->airTimer == 10) {
                if (p_player->_unk_0x74 == 2) {
                    p_player->sfxTrigger = 0xD;
                    p_player->velY = -0x20000;
                    p_player->airTimer = 1;
                }
            }
            else if (p_player->airTimer > 1) {
                p_player->airTimer += 1;
            }
            break;
        default:
            break;
    }

    /* Slope velocity push (when grounded on a slope) */
    if (p_player->groundedFlag != 0 && p_player->surfNormY > -0xEC8 &&
        p_player->_unk_0x42 == 0)
    {
        if ((param_2 & 0x1100) == 0 || p_player->_unk_0x86 > 0 ||
            ((param_2 & 8) != 0 && (param_2 & 0x80) != 0))
        {
            p_player->velX += (p_player->surfNormX) * 2;
            int velZ = p_player->velZ;
            int slopeZ = (p_player->surfNormZ) * 2;
            p_player->velZ = velZ + slopeZ;
        } else {
            p_player->velX += p_player->surfNormX >> 1;
            int velZ = p_player->velZ;
            int slopeZ = p_player->surfNormZ >> 1;
            p_player->velZ = velZ + slopeZ;
        }
    }

    /* charId < 3 ground state machine (binary: 0x485ec2-0x4860b9)
     * IMPORTANT: In the binary, this runs BEFORE respawn check, ProcessGroundContact,
     * and jump/gravity. It uses turnRate (ECX) and maxSpeed (ESI).
     * Manages byte 0x86 (upper 16 bits of player[0x21]):
     *   0 = on ground, normal
     *   1 = just landed (maxSpeed set to landingSpeed, turnRate /= 6)
     *   2 = stationary
     *   >2 = airborne/jumping (velocity section skipped via early return) */
    if (charId < CHAR_AMY) {
        int jumpState = p_player->_unk_0x86;
        if (jumpState < 1) {
            /* Airborne or just jumped (state < 1) */
            if (((param_2 & 0x2000) == 0) || (p_player->groundedFlag != 1) ||
                (p_player->yOffset != 0 || ((param_2 & 0x4000) != 0 || (param_2 & 0x8000) != 0)))
            {
                goto ground_state_done;
            }
            if (p_player->forwardSpeed == 0 && p_player->lateralSpeed == 0 &&
                p_player->velX == 0 && p_player->velZ == 0)
            {
                /* Fully stopped: set state to 2 (stationary) */
                p_player->_unk_0x86 = 2;
            }
            else {
                /* Decrement state (falling) */
                p_player->_unk_0x86 -= 1;
                if (p_player->_unk_0x86 < -4) {
                    p_player->sfxTrigger = 10;  /* fall sound */
                    p_player->_unk_0x86 = 1;
                }
            }
        }
        else if (jumpState < 2 || (param_2 & 0x2000) == 0) {
            /* State 1 = just landed, or no jump button */
            if (p_player->_unk_0x86 != 2 && g_introCountdown == 0) {
                /* Binary: ESI = [ebp-0x38] = stats[7] (landingSpeed) at 0x485fe7 */
                maxSpeed = landingSpeed;
                if (p_player->_unk_0x86 > 2) {
                    /* Landing from height: apply velocity based on landingSpeed */
                    p_player->_unk_0x86 = p_player->_unk_0x86 & 0x7F;
                    p_player->velX = (p_player->_unk_0x86 - 2) *
                                  g_sinTable[p_player->angleYaw] * (landingSpeed >> 12) >> 4;
                    p_player->velZ = (p_player->_unk_0x86 - 2) *
                                  g_cosTable[p_player->angleYaw] * (landingSpeed >> 12) >> 4;
                    p_player->sfxTrigger = 9;  /* land sound */
                    justLanded = 1;  /* [ebp-0x44] = 1 — prevents jump this frame */
                }
                /* Binary: ECX /= 6 at 0x486078-0x48608c — reduce turn rate on landing */
                turnRate /= 6;
                /* Set state to 1 (landed) */
                p_player->_unk_0x86 = 1;
                if (p_player->forwardSpeed != 0 || p_player->lateralSpeed != 0 ||
                    p_player->velX != 0 || p_player->velZ != 0)
                {
                    goto ground_state_skip_zero;
                }
            }
ground_state_done:
            /* On ground, moving: state = 0 */
            p_player->_unk_0x86 = 0;
        }
        else if ((param_2 & 0x100) == 0) {
            /* Jump button released */
            p_player->_unk_0x86 = p_player->_unk_0x86 & 0x7F;
        }
        else if (jumpState < 0x80) {
            /* Charging jump */
            p_player->_unk_0x86 += 0x81;
            if (p_player->_unk_0x86 > 0x86) {
                p_player->_unk_0x86 = 0x86;
            }
            p_player->sfxTrigger = 8;  /* charge sound */
        }
    }
ground_state_skip_zero:

    /* Loop path — binary 0x4860b9: loopMode != 0 → call 0x4d59f8
     * (ApplyDragAndSteering), then return (skips normal ground physics +
     * position integration). Loops in Sonic R run on this loopMode path:
     * recorded/live inputs steer (±0x14 yaw) and accelerate (+0x1200 lateral
     * per frame while accel held) in the surface-local frame.
     */
    if (p_player->loopMode != 0) {
        ApplyDragAndSteering((int *)p_player, param_2);
        return;
    }

    /* Ground contact — ramp surface handling. */
    ProcessGroundContact(p_player); /* 0x4860d5-0x4860dc */

    /* Jump/gravity state machine (binary: 0x4860dc-0x48623d) */
    if (p_player->_unk_0x86 < 2 && !justLanded) {
        int vertVel = p_player->velY;   /* current vertical velocity */

        /* Binary: test [ebp-0x40] (stats[5]/2 = jumpParam) at 0x4860f8 */
        if (jumpParam == 0) {
            /* No jump capability (stats[5] is 0 or 1) */
            goto jump_gravity_aircheck;
        }
        else {
            /* Has jump capability */
            if (((param_2 & 0x620) == 0) || (p_player->yOffset != 0) ||
                (p_player->_unk_0x78 != 0))
            {
                /* Not pressing ability button, or in Y-offset mode, or jump counter active */
                p_player->_unk_0x74 = 0;  /* clear jump state at byte 0x74 */
            }
            else {
                /* Ability button pressed, on ground, no Y offset */
                int groundedState = p_player->_unk_0x74;
                if (groundedState == 0 || groundedState == 2) {
                    if (p_player->groundedFlag == 0) {
                        p_player->_unk_0x74 = 2;   /* jump ready (airborne) */
                    }
                    else {
                        p_player->_unk_0x74 = 1;   /* jump ready (grounded) */
                    }
                }
                else {
                    p_player->_unk_0x74 = (short)0xFFFF;  /* can't jump in this state */
                }
            }

            int groundedVal = p_player->_unk_0x74;
            if (groundedVal == 0) {
                goto jump_gravity_aircheck;
            }
            if (p_player->groundedFlag != 0) {
                /* Grounded and move mode active */
                if (groundedVal < 1) {
                    goto jump_gravity_aircheck;
                }

                /* Initiate jump! */
                p_player->jumpCounter = 1;   /* set jump counter */
                p_player->groundedFlag = 0;   /* clear grounded flag */

                /* Bounce camera effect — binary 0x486195 compares against
                 * [0x8FD4E8] = g_trailSrcB (= &g_playerBase[1], track_init).
                 * 2026-07-13 alias fix: was comparing never-assigned
                 * g_player1PtrAlt (always NULL → slot-1 jumps wrongly reset
                 * the slot-0 pair) and writing orphan bounce2 variables. */
                if (g_isMultiRace == 1 && p_player == g_trailSrcB) {
                    g_bounceVelocity2 = 0;
                    g_bouncePosition2 = 0x180;
                } else {
                    g_bounceVelocity = 0;
                    g_bouncePosition = 0x180;
                }

                p_player->_unk_0x86 = 0;   /* reset jump state */
                p_player->animId = 1;   /* set anim to jumping */
                p_player->airTimer = 2;   /* airborne */
                p_player->sfxTrigger = 7;   /* jump sound */

                /* CalcJumpVelocity — 0x004853A8 — 574 bytes.
                 * Binary passes [ebp-0x40] = stats[5]/2 as EDX at 0x4861d5. */
                vertVel = CalcJumpVelocity(p_player, jumpParam);
            }
        }

        /* 0x4861f3: Apply gravity */
        vertVel += 0x2000;
        if (vertVel > 0x25800) {
            vertVel = 0x25800;  /* terminal velocity */
        }
        if (p_player->groundedFlag != 0) {
            vertVel = 0;  /* grounded: zero vertical velocity */
        }

        goto jump_gravity_past;

jump_gravity_aircheck:
        /* 0x48629a: Airborne checks */
        if (p_player->jumpCounter == 0 || vertVel >= 0) {
            goto jump_gravity_apply;
        }
        /* Rising with jump held — dampen upward velocity */
        if (vertVel < -0xC800) {
            vertVel += 0x6000;  /* strong boost if very fast upward */
        }
        vertVel = vertVel / 2;  /* halve upward velocity */
        p_player->jumpCounter = 0;  /* clear jump counter */
        goto jump_gravity_skip;

jump_gravity_apply:
        vertVel += 0x2000;
        if (vertVel > 0x25800) {
            vertVel = 0x25800;
        }
        if (p_player->groundedFlag != 0) {
            vertVel = 0;
        }

jump_gravity_skip:
jump_gravity_past:
        /* Character-specific overrides */
        if (p_player->abilityState == 1) {
            vertVel = 0;       /* Tails flying */
        }
        if (p_player->abilityState == 4) {
            vertVel = 0x6000;  /* Knuckles gliding fall */
        }

        p_player->velY = vertVel;
        p_player->posY += vertVel;  /* integrate Y position */
    }

    /* Speed boost from invincibility */
    if (p_player->invincTimer != 0) {
        maxSpeed = maxSpeed + maxSpeed / 5;
        p_player->invincTimer -= 1;
    }

    /* Compute effective max speed based on surface and character */
    int inWater = 0;
    int effectiveMax;
    if (p_player->yOffset == 0) {
        effectiveMax = maxSpeed + (p_player->ringCount) * maxSpeed / 1000;
        if (p_player->dynamicSpeedMode == 1) {
            /* Binary: ESI = [ebp-0x48] = stats[9] at 0x4862ee */
            effectiveMax = uwMaxSpeed;
        }
    }
    else {
        inWater = 1;
        if (p_player->yOffset == 0x60000) {
            /* Binary: ESI = EDI = stats[8] at 0x486296 */
            effectiveMax = waterSpeed;
        }
        else {
            /* Binary: ESI = EDI/2 = stats[8]/2 at 0x4862d4-0x4862df */
            effectiveMax = waterSpeed / 2;
        }
    }

    /* Steering / turn rate setup.
     *
     * yawDelta is the drift accumulator: zeroed each frame, modulated by the
     * drift modifier bits, then read back as modYaw (the steering boost
     * applied on top of base L/R turning in the core steering loop below).
     *
     * Bit 0x0008 (DriftL) and bit 0x0080 (DriftR) are symmetric: the binary
     * writes one half of yawDelta as a byte op (Watcom optimisation for
     * += 0x100 / += 0x200 on the high half of the 16-bit word at 0xD4).
     * See PollAllInputDevices @ 0x004769B0 for the bit layout. */
    p_player->yawDelta = 0;
    int modYaw = 0;
    if (charId == CHAR_AMY || charId == CHAR_EGGMAN) {
        /* Amy / Eggman: D-pad gives visual steering, drift bits boost it */
        if (param_2 & 0x4000) {
            p_player->yawDelta += 0x80;     /* Left  */
        }
        if (param_2 & 0x8000) {
            p_player->yawDelta -= 0x80;     /* Right */
        }
        if (param_2 & 0x0008) {
            p_player->yawDelta += 0x100;    /* DriftL */
        }
        if (param_2 & 0x0080) {
            p_player->yawDelta -= 0x100;    /* DriftR */
        }
        /* abs via cdq idiom — sign mask must be 0/-1 (binary 0x48635a
         * cdq; xor; sub). Was `(unsigned)>>31` = 0/+1, which broke abs for
         * negative yawDelta: right-drift never got the ×2 steer boost. */
        int yawDeltaV = p_player->yawDelta;
        int sign = yawDeltaV >> 31;
        if (((yawDeltaV ^ sign) - sign) > 0xFF) {
            modYaw = p_player->yawDelta * 2;
        }
    } else if (!inWater) {
        /* Sonic / Tails / Knuckles: only drift bits feed yawDelta;
         * base L/R steering is applied in the core steering loop. */
        if (param_2 & 0x0008) {
            p_player->yawDelta += 0x200;    /* DriftL */
        }
        if (param_2 & 0x0080) {
            p_player->yawDelta -= 0x200;    /* DriftR */
        }
        modYaw = p_player->yawDelta;
    }

    /* Sonic airborne: halve turn rate and modYaw */
    if (charId == CHAR_SONIC && p_player->groundedFlag == 0) {
        modYaw >>= 1;
        turnRate >>= 1;    /* binary: sar ecx, 1 at 0x4863ba */
    }
    /* Stopped: halve turn rate and modYaw */
    if (p_player->forwardSpeed == 0 && p_player->lateralSpeed == 0) {
        turnRate /= 2;     /* binary: ECX /= 2 at 0x4863cb-0x4863d6 */
        modYaw /= 2;
    }
    /* Sliding: halve friction, double accel */
    if (p_player->_unk_0x6E != 0) {
        friction /= 2;     /* binary: [ebp-0x1c] /= 2 at 0x4863f1-0x486402 */
        accel *= 2;
    }

    /* Core steering (binary: 0x486408-0x486497) */
    if (((param_2 & 8) == 0 || (param_2 & 0x80) == 0) || p_player->forwardSpeed != 0) {
        /* binary: imul edx, ecx → (modYaw * turnRate) / 1024 at 0x486422 */
        int steerAdj = (modYaw * turnRate) / 1024;
        /* Left */
        if ((param_2 & 0x4000) != 0) {    
            if (modYaw != 0) {
                turnRate = turnRate + steerAdj;
                if (p_player->forwardSpeed < p_player->lateralSpeed && 
                    p_player->forwardSpeed > 0x8000)
                {
                    p_player->_unk_0x6E = 1;
                }
            }
            p_player->angleYaw -= turnRate;
        }
        /* Right */
        else if ((param_2 & 0x8000) != 0) {
            if (modYaw != 0) {
                turnRate = turnRate - steerAdj;
                int negFwd = -p_player->forwardSpeed;
                if (-p_player->lateralSpeed != p_player->forwardSpeed && 
                     p_player->lateralSpeed <= negFwd && negFwd < -0x8000)
                {
                    p_player->_unk_0x6E = (short)0xFFFF;
                }
            }
            p_player->angleYaw += turnRate;
        }
        /* No input: auto-center */
        else {
            p_player->angleYaw -= modYaw >> 5;
        }
    }

    /* Sliding direction (binary: 0x486497-0x4864cd) */
    if (p_player->_unk_0x6E == 1 && p_player->lateralSpeed <= p_player->forwardSpeed) {
        p_player->_unk_0x6E = 0;
    }
    if (p_player->_unk_0x6E == -1 &&
        (-p_player->lateralSpeed == p_player->forwardSpeed || 
         -p_player->forwardSpeed < p_player->lateralSpeed))
    {
        p_player->_unk_0x6E = 0;
    }

    /* Surface slope steering */
    if (p_player->groundedFlag != 0) {
        p_player->angleYaw += p_player->_unk_0xDE;
    }

    /* Camera angle computation (binary: 0x4864e5-0x486533) */
    unsigned int yawDelta = (p_player->moveMode) - (p_player->angleYaw & 0xFFFu) & 0xFFF;
    p_player->angleYaw &= 0xFFF;
    if (yawDelta > 0x7FF) {
        yawDelta = -(0x1000 - yawDelta);
    }
    p_player->moveMode = (short)(p_player->moveMode) - (short)((int)yawDelta >> 3);
    p_player->moveMode &= 0x0FFF;

    if (p_player->_unk_0x86 > 1) {
        return;
    }

    /* Velocity decomposition (world -> local)
     * Binary uses 32-bit imul (low 32 bits only) and 32-bit add — products
     * can wrap. Match that with unsigned multiply + cast back to int. */
    unsigned int invYaw = (0x1000 - p_player->angleYaw) & 0xFFF;
    int cosInv = g_cosTable[invYaw] >> 2;
    int sinInv = g_sinTable[invYaw] >> 2;
    int latVel = (int)((unsigned)p_player->velZ * (unsigned)sinInv
                     + (unsigned)p_player->velX * (unsigned)cosInv) / 0x1000;
    int fwdVel = (int)((unsigned)p_player->velZ * (unsigned)cosInv
                     - (unsigned)p_player->velX * (unsigned)sinInv) / 0x1000;

    /* Friction/drag damping (binary: 0x4865a3-0x4865e5)
     * latVel (EDI) gets friction damping using [ebp-0x1c] = friction (stats[3]).
     * fwdVel (ECX) gets drag damping using [ebp-0x24]*2 = fwdDrag*2 (stats[4]*2). */
    int fricAdj = friction * modYaw >> 10;
    if (latVel < 0) {
        latVel += friction - fricAdj;
        if (latVel > 0) {
            latVel = 0;
        }
    }
    else if (latVel > 0) {
        latVel -= fricAdj + friction;
        if (latVel < 0) {
            latVel = 0;
        }
    }

    if (fwdVel < 0) {
        fwdVel += fwdDrag * 2;
        if (fwdVel > 0) {
            fwdVel = 0;
        }
    }
    else if (fwdVel > 0) {
        fwdVel -= fwdDrag * 2;
        if (fwdVel < 0) {
            fwdVel = 0;
        }
    }

    /* Acceleration / braking (binary: 0x4865e5-0x486654) */
    if (((param_2 & 8) == 0 || (param_2 & 0x80) == 0) || p_player->groundedFlag == 0) {
        if ((param_2 & 0x1100) != 0 && g_introCountdown == 0) {
            /* Accelerate */
            p_player->brakeCounter = (short)0xFFFF;
            if (fwdVel < effectiveMax) {
                fwdVel += accel;
            }
        }
        else {
            /* Binary 0x486654: accel branch but not accelerating (param_2 &
             * 0x1100 == 0), or intro countdown still active → clear
             * brakeCounter to 0. */
            p_player->brakeCounter = 0;
        }
    }
    else if (fwdVel > 0) {
        /* Braking with both buttons */
        unsigned int absFwd = (unsigned int)(p_player->forwardSpeed >> 31);
        unsigned int absLat = (unsigned int)(p_player->lateralSpeed >> 31);
        if ((int)((p_player->lateralSpeed ^ absLat) - absLat) < (int)((p_player->forwardSpeed ^ absFwd) - absFwd) &&
            p_player->forwardSpeed > 0x1000)
        {
            p_player->brakeCounter += 1;
        }
        else {
            p_player->brakeCounter = 0;
        }
        fwdVel -= accel;
        if (fwdVel < 0) {
            fwdVel = 0;
        }
    }
    else {
        p_player->brakeCounter = 0;
    }

    /* Speed clamping (binary: 0x48665a-0x486698) */
    if (latVel > effectiveMax) {
        latVel = effectiveMax;
    }
    if (latVel < -effectiveMax) {
        latVel = -effectiveMax;
    }

    if (inWater && fwdVel > effectiveMax) {
        fwdVel -= 0x1000;
    }

    if (fwdVel < -effectiveMax) {
        fwdVel += 0x4000;
    }

    if (fwdVel > 0x44000) {
        fwdVel = 0x44000;
    }
    if (fwdVel < -0x44000) {
        fwdVel = -0x44000;
    }

    /* Store local speeds */
    p_player->lateralSpeed = latVel;
    p_player->forwardSpeed = fwdVel;

    /* Velocity reconstruction (local -> world)
     * Binary uses 32-bit imul + 32-bit add (truncating); match that. */
    int cosYaw = g_cosTable[p_player->angleYaw] >> 2;
    int sinYaw = g_sinTable[p_player->angleYaw] >> 2;
    p_player->velX = (int)((unsigned)fwdVel * (unsigned)sinYaw
                       + (unsigned)cosYaw * (unsigned)latVel) / 4096;
    p_player->velZ = (int)((unsigned)fwdVel * (unsigned)cosYaw
                       - (unsigned)latVel * (unsigned)sinYaw) / 4096;

    /* Position integration */
    p_player->posX += p_player->velX;
    p_player->posZ += p_player->velZ;
}
