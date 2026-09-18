/**
 * track_anim.c — Track animation shared functions
 *
 * Shared angle ticker and collectible pickup detection.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"

/* Track-object angle counters live in the shared block at 0x925290, which
 * menu screens also use for their own state — see sonicr_globals.h for the
 * full tenant list. Indexed directly so the slot is visible here. */
#define s_animAngle3      g_stateBlock92528C[5]    /* 0x9252A0 */
#define s_animAngle3Speed g_stateBlock92528C[6]    /* 0x9252A4 */
#define s_animAngle5      g_stateBlock92528C[7]    /* 0x9252A8 */
#define s_animAngle4      g_stateBlock92528C[8]    /* 0x9252AC */
#define s_animAngle6      g_stateBlock92528C[10]    /* 0x9252B4 */

/**
 * TickObjectAngleCounters — 0x0047ED70 — 202 bytes
 * Increments 6 global animation angles each frame.
 * These drive cyclical animations (water shimmer, light flicker, etc.)
 */
void TickObjectAngleCounters(void)
{
    g_menuExtraY = (g_menuExtraY + g_menuScrollX) & 0xFFF;
    g_menuScrollTarget = (g_menuScrollTarget + g_menuMaxScroll) & 0xFFF;
    s_animAngle3 = (s_animAngle3 + s_animAngle3Speed) & 0xFFF;
    s_animAngle4 = (s_animAngle4 + 0x60) & 0xFFF;
    s_animAngle5 = (s_animAngle5 + 0x80) & 0xFFF;
    s_animAngle6 = (s_animAngle6 + 0x80) & 0x1FFF;
}

/* Collectible array: 17 entries, stride 0x2C (44 bytes = 11 ints) */
#define MAX_COLLECTIBLES    17
#define COLLECTIBLE_STRIDE  11      /* in ints */
#define COLLECT_RADIUS_SQ   0x4000  /* 128² */

/* Collect effect globals */

/**
 * CollectiblePickupCheck — 0x00479398 — 254 bytes
 * Checks all 17 collectible objects against all players.
 * Radius² < 0x4000 (128 world units) = collected.
 */
void CollectiblePickupCheck(void)
{
    int *collectibles = (int *)g_balloonArray;

    Player *player = g_playerBase;
    for (int collectIdx = 0; collectIdx < MAX_COLLECTIBLES; collectIdx++) {
        int *coll = collectibles + collectIdx * COLLECTIBLE_STRIDE;

        if (coll[6] == -1) {
            /* Already collected — skip */
            continue;
        }

        player = g_playerBase;
        for (int playerIdx = 0; playerIdx < g_numPlayers; playerIdx++) {
            int dx = (player->posX >> 12) - coll[0];
            int dy = -(player->posY >> 12) - coll[1];  /* Y negated */
            int dz = (player->posZ >> 12) - coll[2];

            int distSq = dx * dx + dy * dy + dz * dz;

            if ((unsigned int)distSq < COLLECT_RADIUS_SQ) {
                /* Collect! — 0x68153C is per-player collected item index list,
                 * 5 slots per player. DrawResultsBalloonsForPlayer renders these on podium. */
                int collCount = player->collisionCount;
                if (collCount < 5) {
                    g_resultModelIndices[playerIdx * 5 + collCount] = collectIdx;
                }
                player->collisionCount = collCount + 1;

                /* Set visual effect position */
                g_effectPosItemBurst[0] = coll[0] << 8;
                g_effectPosItemBurst[1] = coll[1] * -256;
                g_effectPosItemBurst[2] = coll[2] << 8;

                /* Mark as collected */
                coll[6] = -1;

                /* Set player pickup animation flag */
                player->renderState = 0x30001;

                /* Play collect sound */
                player->sfxTrigger = 0x16;
            }

            player++;
        }
    }
}
