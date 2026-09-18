/**
 * vehicle_physics.c — VehiclePhysicsPreUpdate
 *
 * VehiclePhysicsPreUpdate @ 0x00480910 — 1124 bytes
 *
 * Vehicle-specific physics for Eggman (charId 4) and Egg Robo (charId 8).
 * Handles: proximity target scan, missile lock-on, missile launch,
 * exhaust particle spawning, lock-on sound, cooldown timer.
 *
 * Called once per frame from UpdateHumanPlayerPhysics and PlayerPhysicsMain
 * for vehicle characters.
 *
 * in_EAX = player struct pointer
 * param_2 (EDX) = ability button bits (param_2 & 0x620 from caller)
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include <math.h>

/* Player struct included via sonicr_globals.h */

extern int g_sfxPhaseCounter;        /* 0x00902094 — SFX phase counter */

/**
 * VehiclePhysicsPreUpdate — 0x00480910 — 1124 bytes
 *
 * Three phases:
 *   1. Proximity scan: find nearest opponent in front arc, set lock-on timer
 *   2. Missile launch: if ability button + locked on, spawn missile particle
 *   3. Exhaust/trail: spawn visual effects while locked on or cooling down
 */
void VehiclePhysicsPreUpdate(Player *player, unsigned short abilityBits)
{
    short charId = player->charId;

    /* Slot offset: Eggman=0, Egg Robo=1. In single GP with same character,
     * toggle to avoid particle slot collision. Binary: 0x480923-0x48095f */
    int slotOffset = (charId != CHAR_EGGMAN) ? 1 : 0;
    if (g_isMultiRace != 0 &&
        player == &g_playerBase[1] &&
        g_playerBase[1].charId == g_playerBase[0].charId)
    {
        slotOffset = (slotOffset + 1) & 1;
    }

    /* Proximity scan for lock-on target
     * Binary: 0x480963-0x480a9a
     * Requires: player boost field [0x16]>>16 >= 10 and ability timer [0x7E] == 0.
     * Scans 2 players in GP mode, 5 otherwise. */
    Player *targetPlayer = NULL;
    if (player->ringCount >= 0xA && player->abilityTimer == 0) {
        int scanCount = (g_raceType == RACE_MULTIPLAYER) ? 2 : 5;

        for (int i = 0; i < scanCount; i++) {
            Player *other = &g_playerBase[i];
            if (other == player) {
                continue;
            }

            /* Distance check (squared, in >>12 units) */
            int dx = (player->posX >> 12) - (other->posX >> 12);
            int dy = (player->posY >> 12) - (other->posY >> 12);
            int dz = (player->posZ >> 12) - (other->posZ >> 12);
            int distSq = dx * dx + dy * dy + dz * dz;
            if (distSq >= 0x300000) {
                continue;
            }

            /* Adjust delta by small heading offset (moves reference forward).
             * Binary: invYaw = 0xFFF - yaw, then dx += sin(invYaw)>>6,
             * dz -= cos(invYaw)>>6. sin(invYaw) ≈ -sin(yaw). */
            int invYaw = (0xFFF - player->angleYaw) & 0xFFF;
            int dx_adj = dx + (g_sinTable[invYaw] >> 6);
            int dz_adj = dz - (g_cosTable[invYaw] >> 6);

            /* Compute bearing to target in game units.
             * Binary 0x480a4f: fild dz_adj; fild dx_adj; call 0x4e08fd.
             * The wrapper does fxch st(1); fpatan → atan2(dx_adj, dz_adj),
             * then fmul 4096.0 (0x52fb5d), fmul 1/(2π) (0x52fb65) — POSITIVE
             * scale. Game bearing convention: heading = (sin yaw, cos yaw),
             * so bearing = atan2(dx, dz); the +0x800 below converts the
             * player−target delta into target-from-player.
             * (Was mistranslated as atan2(dz,dx) × −4096/2π — args swapped
             * AND negated, which differs by ±0x400 per quadrant and broke
             * the lock-on arc for Eggman/EggRobo.) */
            int gameAngle = (int)(sr_atan2((sr_double)dx_adj, (sr_double)dz_adj)
                                  * (4096.0 * 0.15915494327375637));
            /* Sign-extend 28-bit (shl 4, sar 4) */
            gameAngle = (gameAngle << 4) >> 4;
            /* Relative to player heading, offset by 0x800 (180°) */
            int relAngle = (gameAngle + 0x800 - player->angleYaw) & 0xFFF;

            /* Front arc check: within ~53° of forward (0x258 = 600 units) */
            if (relAngle < 0x258 || relAngle > 0xDA7) {
                player->abilityTimer = (short)0xFFFF;  /* lock-on: timer = -1 */
                targetPlayer = other;
                break;  /* first valid target wins */
            }
        }
    }

    /* Missile launch
     * Binary: 0x480aa0-0x480c59
     * Fires when ability button pressed AND timer == -1 (locked on). */
    if (abilityBits != 0 && player->abilityTimer == -1) {
        int slotIdx = g_raceCounterA8;

        /* Compute particle data pointer: base + (slotIdx + 60) */
        CollectEffect *particle = &g_collectEffectBuf[slotIdx + COLLECT_EFFECT_SPAWN_MAX];

        /* Set missile tracking table entry */
        int tblIdx = slotIdx * 3;
        g_bounceStateArray[tblIdx + 0] = 0x1C2;                     /* type: missile */
        g_bounceStateArray[tblIdx + 1] = (intptr_t)targetPlayer;      /* target ptr */
        g_bounceStateArray[tblIdx + 2] = (intptr_t)particle;          /* data ptr */

        /* Set cooldown timer and decrease speed */
        player->abilityTimer = 0x5A;  /* 90-frame cooldown */
        player->ringCount -= 0xA;     /* decrease speed boost by 10 */

        /* Spawn position depends on character */
        if (charId == CHAR_EGG_ROBO) {
            /* Egg Robo: offset spawn position to the side */
            if (player->animId == 2) {
                player->prevAnimId = (short)0xFFFF;  /* trigger sound variant */
            }
            int sideYaw = (player->angleYaw - 0x9C4) & 0xFFF;
            particle->posX = (player->posX >> 4) - g_cosTable[sideYaw];
            particle->posY = (player->posY >> 4) - 0x4600;
            particle->posZ = (player->posZ >> 4) + g_sinTable[sideYaw];
            particle->billboardSize = 0x10;
            particle->animDiv = 0x40;
        }
        else {
            /* Eggman: spawn at player position */
            particle->posX = player->posX >> 4;
            particle->posY = (player->posY >> 4) - 0x1E00;
            particle->posZ = player->posZ >> 4;
            particle->billboardSize = 0x10;
            particle->animDiv = 0x40;
        }

        /* Direction: forward along player heading */
        particle->velX = g_sinTable[player->angleYaw];     /* dirX = sin(yaw) */
        particle->velY = 0;                                /* dirY = 0 */
        particle->velZ = g_cosTable[player->angleYaw];     /* dirZ = cos(yaw) */
        particle->accelY = 0;

        /* Particle parameters */
        particle->halfW = 0;
        particle->lifetime = 0x1C2;
        particle->spriteId = 0x420;
        particle->type = 1;
        particle->timer = 0;
        particle->animEnd = 0x100;
        particle->animFrameW = 2;
        particle->animFrameH = 2;
        particle->uvBaseX = 0;
        particle->uvBaseY = 0x70;
        particle->uvSpan = 0x20;
        particle->tpage = (unsigned char)g_tpageCharBase;

        /* Advance slot counter (wraps at 4) */
        g_raceCounterA8 = slotIdx + 1;
        if (g_raceCounterA8 >= 4) {
            g_raceCounterA8 = 0;
        }

        player->sfxTrigger = 0xE;  /* missile launch sound */
    }

    /* Lock-on exhaust / trail particles
     * Binary: 0x480c59-0x480d55
     * Runs when timer == -1 (locked on, no button pressed this frame).
     * Spawns exhaust effect at the TARGET player's position. */
    if (player->abilityTimer == -1) {
        /* Lock-on sound (gated by timing counters) */
        if (g_sfxPhaseCounter == 0 && g_raceOrder[3] == 0) {
            player->sfxTrigger = 0x2F;  /* lock-on beep */
        }

        int slot = (g_raceCounterA8 + slotOffset) & 3;
        int tblIdx = slot * 3;
        g_bounceStateArray[tblIdx] = 0;  /* clear missile type (exhaust only) */

        /* Compute exhaust particle pointer */
        CollectEffect *exhaust = &g_collectEffectBuf[slot + COLLECT_EFFECT_SPAWN_MAX];

        /* Position at target player (if found this frame) */
        if (targetPlayer != NULL) {
            exhaust->posX = targetPlayer->posX >> 4;
            exhaust->posY = (targetPlayer->posY >> 4) - 0x2800;
            exhaust->posZ = targetPlayer->posZ >> 4;
        }

        /* Exhaust particle parameters */
        exhaust->halfW = 0;
        exhaust->lifetime = 2;
        exhaust->spriteId = 0x210;
        exhaust->type = 1;
        exhaust->timer = 0;
        exhaust->animEnd = 4;
        exhaust->animFrameW = 0x10;
        exhaust->animFrameH = 0x10;
        exhaust->billboardSize = 0x20;
        exhaust->animDiv = 4;
        exhaust->uvBaseX = 0x40;
        exhaust->uvBaseY = 0x50;
        exhaust->uvSpan = 0x10;
        exhaust->velX = 0;
        exhaust->velY = 0;
        exhaust->velZ = 0;
        exhaust->accelY = 0;
        exhaust->tpage = (unsigned char)g_tpageCharBase;

        player->abilityTimer = 0;  /* clear timer (was -1) */
        player->_unk_0x112 += 1;   /* increment exhaust frame counter */
    }
    else {
        /* Not locked on: reset exhaust frame counter */
        player->_unk_0x112 = 0;
    }

    /* Timer countdown: if timer > 0, decrement */
    if (player->abilityTimer > 0) {
        player->abilityTimer -= 1;
    }
}

/* =====================================================================
 * ApplyDragAndSteering — FUN_004d59f8 — 492 bytes
 *
 * Updates an object's velocity with drag and steering. Decomposes world
 * velocity into local forward/lateral via 2D rotation by heading angle,
 * applies drag, optional lateral boost, clamps, rotates back to world.
 * EAX = object struct pointer, EDX = flags (Watcom fastcall).
 * ===================================================================== */
void ApplyDragAndSteering(int *obj, int flags)
{
    obj[8] = obj[0];
    obj[9] = obj[1];
    obj[10] = obj[2];

    unsigned char fb = (unsigned char)(flags >> 8);
    if (fb & 0x40) {
        obj[4] -= 0x14;
    }
    if (fb & 0x80) {
        obj[4] += 0x14;
    }

    int loopIdx = *(short *)((char *)obj + 0x94);
    int angle = obj[4] & 0xFFF;
    obj[4] = angle;

    /* surfAngle (+0x14) of this loop surface entry. Binary loads dword@+0x12
     * and keeps the high 16 bits; the field read is the aligned, bit-exact
     * equivalent of (dword>>16), and avoids the misaligned 32-bit load (UB/DC fault). */
    int angleOff = ((TerLoopEntry *)g_terLoopTable)[loopIdx].surfAngle;
    int rotAngle = (angle - angleOff) & 0xFFF;

    int cosA = g_cosTable[rotAngle];
    int sinA = g_sinTable[rotAngle];
    int negAngle = (-rotAngle) & 0xFFF;
    int negSinA = g_sinTable[negAngle];
    int cosA2 = g_cosTable[negAngle];

    int vX = *(int *)((char *)obj + 0xCC);
    int vZ = *(int *)((char *)obj + 0xD0);

    /* Two-step rounding: idiv 0x1000 (trunc toward zero) then sar 2 (floor).
     * NOT equivalent to a single /0x4000 for negative sums — must match the
     * binary's rounding to stay bit-exact (see AISteeringAndDrag). */
    int forward = (int)(((long long)vX * cosA2 + (long long)vZ * negSinA) / 4096) >> 2;
    int lateral = (int)(((long long)(-vX) * negSinA + (long long)vZ * cosA2) / 4096) >> 2;

    /* Binary tests fb&1 (flags bit 8), not flags bit 0 — 0x4d5b14 test bl,1
     * where bl=[ebp-0x13]=fb. All flag bits used here live in the high byte. */
    if ((fb & 0x10) || (fb & 1)) {
        lateral += 0x1200;
    }

    if (forward < 0) {
        forward += 0x1000;

        if (forward > 0) {
            forward = 0;
        }
    }
    else if (forward > 0) {
        forward -= 0x1000;

        if (forward < 0) {
            forward = 0;
        }
    }

    if (lateral < 0) {
        lateral += 0x800;

        if (lateral > 0) {
            lateral = 0;
        }
    }
    else if (lateral > 0) {
        lateral -= 0x800;

        if (lateral < 0) {
            lateral = 0;
        }
    }

    if (lateral > 0x46000) {
        lateral -= 0x1000;
    }
    if (lateral < -0x46000) {
        lateral += 0x1000;
    }

    int newVX = (int)(((long long)forward * cosA + (long long)lateral * sinA) / 4096) >> 2;
    int newVZ = (int)(((long long)(-forward) * sinA + (long long)lateral * cosA) / 4096) >> 2;

    *(int *)((char *)obj + 0xCC) = newVX;
    *(int *)((char *)obj + 0xD0) = newVZ;
}
