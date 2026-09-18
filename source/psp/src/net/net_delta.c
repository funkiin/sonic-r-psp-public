#include "net_delta.h"
#include <stddef.h>
#include <string.h>
#include <stdint.h>

extern void DeriveGroundState(int posX, int posZ, int *outHeight,
                              short *outNormX, short *outNormY, short *outNormZ);

#define FLAG_SIGNED  0x01
#define FLAG_DELTA   0x02

typedef struct {
    unsigned short offset;
    unsigned char  player_size;
    unsigned char  abs_width;
    unsigned char  delta_width;
    unsigned char  flags;
} FieldDef;

static const FieldDef s_fields[NET_DELTA_FIELD_COUNT] = {
    /*  0 */ { offsetof(Player, posX),             4, 4, 2, FLAG_SIGNED|FLAG_DELTA },
    /*  1 */ { offsetof(Player, posY),             4, 4, 2, FLAG_SIGNED|FLAG_DELTA },
    /*  2 */ { offsetof(Player, posZ),             4, 4, 2, FLAG_SIGNED|FLAG_DELTA },
    /*  3 */ { offsetof(Player, velX),             4, 4, 2, FLAG_SIGNED|FLAG_DELTA },
    /*  4 */ { offsetof(Player, velY),             4, 4, 2, FLAG_SIGNED|FLAG_DELTA },
    /*  5 */ { offsetof(Player, velZ),             4, 4, 2, FLAG_SIGNED|FLAG_DELTA },
    /*  6 */ { offsetof(Player, angleYaw),         4, 2, 1, FLAG_SIGNED|FLAG_DELTA },
    /*  7 */ { offsetof(Player, anglePitch),       4, 2, 1, FLAG_SIGNED|FLAG_DELTA },
    /*  8 */ { offsetof(Player, angleRoll),        4, 2, 1, FLAG_SIGNED|FLAG_DELTA },
    /*  9 */ { offsetof(Player, pitchCombo),       4, 2, 1, FLAG_SIGNED|FLAG_DELTA },
    /* 10 */ { offsetof(Player, forwardSpeed),     4, 4, 2, FLAG_SIGNED|FLAG_DELTA },
    /* 11 */ { offsetof(Player, lateralSpeed),     4, 4, 2, FLAG_SIGNED|FLAG_DELTA },
    /* 12 */ { offsetof(Player, animId),           2, 1, 0, 0 },
    /* 13 */ { offsetof(Player, groundedFlag),     2, 1, 0, 0 },
    /* 14 */ { offsetof(Player, lapCrossFlag),     2, 1, 0, 0 },
    /* 15 */ { offsetof(Player, dynamicSpeedMode), 2, 2, 0, 0 },
    /* 16 */ { offsetof(Player, moveMode),         2, 2, 0, 0 },
    /* 17 */ { offsetof(Player, collisionLayer),   4, 2, 0, 0 },
    /* 18 */ { offsetof(Player, loopMode),        2, 2, 0, 0 },
    /* 19 */ { offsetof(Player, yOffset),          4, 4, 2, FLAG_SIGNED|FLAG_DELTA },
    /* 20 */ { offsetof(Player, ringCount),        2, 1, 0, 0 },
    /* 21 */ { offsetof(Player, _unk_0x94),        2, 1, 0, 0 },
    /* 22 */ { offsetof(Player, _unk_0xBC),        4, 1, 0, 0 },
    /* 23 */ { offsetof(Player, _unk_0x78),        2, 1, 0, 0 },
    /* 24 */ { offsetof(Player, _unk_0x7A),        2, 1, 0, 0 },
    /* 25 */ { offsetof(Player, itemEffectId),     2, 1, 0, 0 },
    /* 26 */ { offsetof(Player, itemEffectState),  2, 1, 0, FLAG_SIGNED },
    /* 27 */ { offsetof(Player, itemHeightMod),    4, 4, 0, 0 },
    /* 28 */ { offsetof(Player, effectYMod),       4, 4, 0, 0 },
    /* 29 */ { offsetof(Player, invincTimer),      2, 1, 0, 0 },
    /* 30 */ { offsetof(Player, brakeCounter),     2, 2, 0, 0 },
    /* 31 */ { offsetof(Player, abilityTimer),     2, 2, 0, 0 },
    /* 32 */ { offsetof(Player, abilityState),     2, 1, 0, 0 },
    /* 33 */ { offsetof(Player, sfxTrigger),       2, 1, 0, 0 },
    /* 34 */ { offsetof(Player, _unk_0x80),        2, 1, 0, 0 },
    /* 35 */ { offsetof(Player, _unk_0xD6),        2, 1, 0, 0 },
    /* 36 */ { offsetof(Player, _unk_0x1F0),       4, 1, 0, 0 },
    /* 37 */ { offsetof(Player, lapsCompleted),    2, 1, 0, 0 },
    /* 38 */ { offsetof(Player, racePosition),     2, 1, 0, 0 },
    /* 39 */ { offsetof(Player, collisionCount),   4, 1, 0, 0 },
    /* 40 */ { offsetof(Player, lap1Time),        4, 4, 0, 0 },
    /* 41 */ { offsetof(Player, lap2Time),        4, 4, 0, 0 },
    /* 42 */ { offsetof(Player, lap3Time),        4, 4, 0, 0 },
    /* 43 */ { offsetof(Player, _unk_0x86),        2, 1, 0, 0 },
};

/* Overflow sub-mask bit → field index mapping.
 * Bits 0-11 map to fields 0-11, bit 12 maps to field 19 (yOffset). */
static const int s_delta_field_idx[13] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 19
};
#define NUM_DELTA_FIELDS 13

static int read_field(const Player *pl, const FieldDef *f)
{
    const char *base = (const char *)pl;
    if (f->player_size == 4)
        return *(const int *)(base + f->offset);
    return (int)*(const short *)(base + f->offset);
}

static void write_field(Player *pl, const FieldDef *f, int val)
{
    char *base = (char *)pl;
    if (f->player_size == 4)
        *(int *)(base + f->offset) = val;
    else
        *(short *)(base + f->offset) = (short)val;
}

static void write_le(unsigned char *buf, int val, int n)
{
    int i;
    for (i = 0; i < n; i++)
        buf[i] = (unsigned char)(val >> (i * 8));
}

static int read_le_unsigned(const unsigned char *buf, int n)
{
    int val = 0, i;
    for (i = 0; i < n; i++)
        val |= (int)buf[i] << (i * 8);
    return val;
}

static int read_le_signed(const unsigned char *buf, int n)
{
    int val = read_le_unsigned(buf, n);
    int shift = (4 - n) * 8;
    return (val << shift) >> shift;
}

static int delta_fits(int delta, int width)
{
    if (width == 1)
        return delta >= -128 && delta <= 127;
    return delta >= -32768 && delta <= 32767;
}

static int delta_submask_bit(int field_idx)
{
    int i;
    for (i = 0; i < NUM_DELTA_FIELDS; i++) {
        if (s_delta_field_idx[i] == field_idx)
            return i;
    }
    return -1;
}

void net_delta_reset_send(NetDeltaSendState *s)
{
    memset(s->prev, 0, sizeof(s->prev));
    s->keyframeCountdown = 0;
}

void net_delta_reset_recv(NetDeltaRecvState *s)
{
    memset(s->state, 0, sizeof(s->state));
    s->valid = 0;
}

int net_delta_encode(NetDeltaSendState *s, const Player *pl,
                     int playerIdx, int is_keyframe,
                     unsigned char *buf, int maxlen)
{
    int current[NET_DELTA_FIELD_COUNT];
    int i, cursor;

    for (i = 0; i < NET_DELTA_FIELD_COUNT; i++)
        current[i] = read_field(pl, &s_fields[i]);

    buf[0] = (unsigned char)playerIdx;
    cursor = 1;

    if (is_keyframe) {
        if (maxlen < 1 + NET_DELTA_KEYFRAME_BODY)
            return 0;
        for (i = 0; i < NET_DELTA_FIELD_COUNT; i++) {
            const FieldDef *f = &s_fields[i];
            write_le(buf + cursor, current[i], f->abs_width);
            cursor += f->abs_width;
        }
        memcpy(s->prev, current, sizeof(current));
        return cursor;
    }

    /* Delta encode */
    {
        unsigned char mask[NET_DELTA_MASK_BYTES];
        unsigned char submask[2] = {0, 0};
        unsigned char fielddata[256];
        int fdlen = 0;
        int has_overflow = 0;

        memset(mask, 0, sizeof(mask));

        for (i = 0; i < NET_DELTA_FIELD_COUNT; i++) {
            const FieldDef *f = &s_fields[i];
            if (current[i] == s->prev[i])
                continue;

            mask[i / 8] |= (1 << (i % 8));

            if (f->flags & FLAG_DELTA) {
                int delta = current[i] - s->prev[i];
                if (delta_fits(delta, f->delta_width)) {
                    write_le(fielddata + fdlen, delta, f->delta_width);
                    fdlen += f->delta_width;
                } else {
                    int sbit = delta_submask_bit(i);
                    if (sbit >= 0)
                        submask[sbit / 8] |= (1 << (sbit % 8));
                    has_overflow = 1;
                    write_le(fielddata + fdlen, current[i], f->abs_width);
                    fdlen += f->abs_width;
                }
            } else {
                write_le(fielddata + fdlen, current[i], f->abs_width);
                fdlen += f->abs_width;
            }
        }

        if (has_overflow)
            mask[NET_DELTA_OVERFLOW_BIT / 8] |= (1 << (NET_DELTA_OVERFLOW_BIT % 8));

        {
            int total = 1 + NET_DELTA_MASK_BYTES + (has_overflow ? 2 : 0) + fdlen;
            if (maxlen < total)
                return 0;
        }

        memcpy(buf + cursor, mask, NET_DELTA_MASK_BYTES);
        cursor += NET_DELTA_MASK_BYTES;

        if (has_overflow) {
            buf[cursor++] = submask[0];
            buf[cursor++] = submask[1];
        }

        memcpy(buf + cursor, fielddata, fdlen);
        cursor += fdlen;

        memcpy(s->prev, current, sizeof(current));
        return cursor;
    }
}

int net_delta_decode(NetDeltaRecvState *s, const unsigned char *buf,
                     int len, int is_keyframe, Player *pl)
{
    int i, cursor;

    if (len < 1) return 0;
    /* buf[0] is playerIdx — caller reads it, we skip past */
    cursor = 1;

    if (is_keyframe) {
        if (len < 1 + NET_DELTA_KEYFRAME_BODY)
            return 0;
        for (i = 0; i < NET_DELTA_FIELD_COUNT; i++) {
            const FieldDef *f = &s_fields[i];
            if (f->flags & FLAG_SIGNED)
                s->state[i] = read_le_signed(buf + cursor, f->abs_width);
            else
                s->state[i] = read_le_unsigned(buf + cursor, f->abs_width);
            cursor += f->abs_width;
        }
        s->valid = 1;
    } else {
        unsigned char mask[NET_DELTA_MASK_BYTES];
        int has_overflow;
        unsigned char submask[2] = {0, 0};

        if (!s->valid)
            return 0;
        if (len < 1 + NET_DELTA_MASK_BYTES)
            return 0;

        memcpy(mask, buf + cursor, NET_DELTA_MASK_BYTES);
        cursor += NET_DELTA_MASK_BYTES;

        has_overflow = (mask[NET_DELTA_OVERFLOW_BIT / 8] >> (NET_DELTA_OVERFLOW_BIT % 8)) & 1;

        if (has_overflow) {
            if (cursor + 2 > len) return 0;
            submask[0] = buf[cursor++];
            submask[1] = buf[cursor++];
        }

        for (i = 0; i < NET_DELTA_FIELD_COUNT; i++) {
            const FieldDef *f = &s_fields[i];
            int present = (mask[i / 8] >> (i % 8)) & 1;
            if (!present)
                continue;

            if ((f->flags & FLAG_DELTA) && has_overflow) {
                int sbit = delta_submask_bit(i);
                if (sbit >= 0 && ((submask[sbit / 8] >> (sbit % 8)) & 1)) {
                    if (cursor + f->abs_width > len) return 0;
                    s->state[i] = (f->flags & FLAG_SIGNED)
                        ? read_le_signed(buf + cursor, f->abs_width)
                        : read_le_unsigned(buf + cursor, f->abs_width);
                    cursor += f->abs_width;
                    continue;
                }
            }

            if (f->flags & FLAG_DELTA) {
                if (cursor + f->delta_width > len) return 0;
                int delta = read_le_signed(buf + cursor, f->delta_width);
                s->state[i] += delta;
                cursor += f->delta_width;
            } else {
                if (cursor + f->abs_width > len) return 0;
                if (f->flags & FLAG_SIGNED)
                    s->state[i] = read_le_signed(buf + cursor, f->abs_width);
                else
                    s->state[i] = read_le_unsigned(buf + cursor, f->abs_width);
                cursor += f->abs_width;
            }
        }
    }

    for (i = 0; i < NET_DELTA_FIELD_COUNT; i++)
        write_field(pl, &s_fields[i], s->state[i]);

    return cursor;
}

_Static_assert(offsetof(Player, posX)            == 0x000, "net_delta posX");
_Static_assert(offsetof(Player, posY)            == 0x004, "net_delta posY");
_Static_assert(offsetof(Player, posZ)            == 0x008, "net_delta posZ");
_Static_assert(offsetof(Player, velX)            == 0x02C, "net_delta velX");
_Static_assert(offsetof(Player, velY)            == 0x030, "net_delta velY");
_Static_assert(offsetof(Player, velZ)            == 0x034, "net_delta velZ");
_Static_assert(offsetof(Player, angleYaw)        == 0x010, "net_delta angleYaw");
_Static_assert(offsetof(Player, anglePitch)      == 0x00C, "net_delta anglePitch");
_Static_assert(offsetof(Player, angleRoll)       == 0x014, "net_delta angleRoll");
_Static_assert(offsetof(Player, pitchCombo)      == 0x0C8, "net_delta pitchCombo");
_Static_assert(offsetof(Player, forwardSpeed)    == 0x044, "net_delta forwardSpeed");
_Static_assert(offsetof(Player, lateralSpeed)    == 0x048, "net_delta lateralSpeed");
_Static_assert(offsetof(Player, animId)          == 0x098, "net_delta animId");
_Static_assert(offsetof(Player, groundedFlag)    == 0x072, "net_delta groundedFlag");
_Static_assert(offsetof(Player, lapCrossFlag)    == 0x060, "net_delta lapCrossFlag");
_Static_assert(offsetof(Player, dynamicSpeedMode)== 0x040, "net_delta dynamicSpeedMode");
_Static_assert(offsetof(Player, moveMode)        == 0x070, "net_delta moveMode");
_Static_assert(offsetof(Player, collisionLayer)  == 0x0A4, "net_delta collisionLayer");
_Static_assert(offsetof(Player, loopMode)       == 0x0A0, "net_delta loopMode");
_Static_assert(offsetof(Player, yOffset)         == 0x068, "net_delta yOffset");
_Static_assert(offsetof(Player, ringCount)       == 0x018, "net_delta ringCount");
_Static_assert(offsetof(Player, _unk_0x94)       == 0x094, "net_delta unk94");
_Static_assert(offsetof(Player, _unk_0xBC)       == 0x0BC, "net_delta unkBC");
_Static_assert(offsetof(Player, _unk_0x78)       == 0x078, "net_delta unk78");
_Static_assert(offsetof(Player, _unk_0x7A)       == 0x07A, "net_delta unk7A");
_Static_assert(offsetof(Player, itemEffectId)    == 0x064, "net_delta itemEffectId");
_Static_assert(offsetof(Player, itemEffectState) == 0x082, "net_delta itemEffectState");
_Static_assert(offsetof(Player, itemHeightMod)   == 0x100, "net_delta itemHeightMod");
_Static_assert(offsetof(Player, effectYMod)      == 0x104, "net_delta effectYMod");
_Static_assert(offsetof(Player, invincTimer)     == 0x088, "net_delta invincTimer");
_Static_assert(offsetof(Player, brakeCounter)    == 0x06C, "net_delta brakeCounter");
_Static_assert(offsetof(Player, abilityTimer)    == 0x07E, "net_delta abilityTimer");
_Static_assert(offsetof(Player, abilityState)    == 0x0FE, "net_delta abilityState");
_Static_assert(offsetof(Player, sfxTrigger)      == 0x0EA, "net_delta sfxTrigger");
_Static_assert(offsetof(Player, _unk_0x80)       == 0x080, "net_delta unk80");
_Static_assert(offsetof(Player, _unk_0xD6)       == 0x0D6, "net_delta unkD6");
_Static_assert(offsetof(Player, _unk_0x1F0)      == 0x1F0, "net_delta unk1F0");
_Static_assert(offsetof(Player, lapsCompleted)   == 0x05E, "net_delta lapsCompleted");
_Static_assert(offsetof(Player, racePosition)    == 0x05C, "net_delta racePosition");
_Static_assert(offsetof(Player, collisionCount)  == 0x1F4, "net_delta collisionCount");
_Static_assert(offsetof(Player, lap1Time)       == 0x050, "net_delta lap1Time");
_Static_assert(offsetof(Player, lap2Time)       == 0x054, "net_delta lap2Time");
_Static_assert(offsetof(Player, lap3Time)       == 0x058, "net_delta lap3Time");
_Static_assert(offsetof(Player, _unk_0x86)       == 0x086, "net_delta unk86");
