#ifndef COLLECT_EFFECT_H
#define COLLECT_EFFECT_H

#include <stddef.h>

typedef struct {
    int posX;              /* 0x00 — world position (<<8 fixed-point) */
    int posY;              /* 0x04 — world position (<<8, negated convention) */
    int posZ;              /* 0x08 — world position (<<8 fixed-point) */
    int velX;              /* 0x0C */
    int velY;              /* 0x10 */
    int velZ;              /* 0x14 */
    int accelY;            /* 0x18 — gravity / vertical acceleration */
    short halfW;           /* 0x1C — billboard half-width, low byte has alpha flags */
    short lifetime;        /* 0x1E — countdown timer; active when > 0 */
    short spriteId;        /* 0x20 */
    short type;            /* 0x22 */
    int timer;             /* 0x24 — animation frame accumulator */
    int animEnd;           /* 0x28 — frame wrap limit (timer resets when == animEnd) */
    short animFrameW;      /* 0x2C */
    short animFrameH;      /* 0x2E — also decremented as alt timer */
    short billboardSize;   /* 0x30 — projection half-size */
    short animDiv;         /* 0x32 — frame timer divisor */
    unsigned char uvBaseX; /* 0x34 */
    unsigned char uvBaseY; /* 0x35 */
    unsigned char tpage;   /* 0x36 */
    unsigned char _pad37;  /* 0x37 */
    short uvSpan;          /* 0x38 */
    short _pad3A;          /* 0x3A */
} CollectEffect;           /* 60 bytes = 0x3C */

_Static_assert(sizeof(CollectEffect) == 60, "CollectEffect must be 60 bytes");
_Static_assert(offsetof(CollectEffect, posX) == 0x00, "");
_Static_assert(offsetof(CollectEffect, posY) == 0x04, "");
_Static_assert(offsetof(CollectEffect, posZ) == 0x08, "");
_Static_assert(offsetof(CollectEffect, velX) == 0x0C, "");
_Static_assert(offsetof(CollectEffect, velY) == 0x10, "");
_Static_assert(offsetof(CollectEffect, velZ) == 0x14, "");
_Static_assert(offsetof(CollectEffect, accelY) == 0x18, "");
_Static_assert(offsetof(CollectEffect, halfW) == 0x1C, "");
_Static_assert(offsetof(CollectEffect, lifetime) == 0x1E, "");
_Static_assert(offsetof(CollectEffect, spriteId) == 0x20, "");
_Static_assert(offsetof(CollectEffect, type) == 0x22, "");
_Static_assert(offsetof(CollectEffect, timer) == 0x24, "");
_Static_assert(offsetof(CollectEffect, animEnd) == 0x28, "");
_Static_assert(offsetof(CollectEffect, animFrameW) == 0x2C, "");
_Static_assert(offsetof(CollectEffect, animFrameH) == 0x2E, "");
_Static_assert(offsetof(CollectEffect, billboardSize) == 0x30, "");
_Static_assert(offsetof(CollectEffect, animDiv) == 0x32, "");
_Static_assert(offsetof(CollectEffect, uvBaseX) == 0x34, "");
_Static_assert(offsetof(CollectEffect, uvBaseY) == 0x35, "");
_Static_assert(offsetof(CollectEffect, tpage) == 0x36, "");
_Static_assert(offsetof(CollectEffect, uvSpan) == 0x38, "");

#define COLLECT_EFFECT_COUNT 64
#define COLLECT_EFFECT_SPAWN_MAX 60

#endif /* COLLECT_EFFECT_H */
