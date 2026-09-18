#ifndef NET_DELTA_H
#define NET_DELTA_H

#include "player_struct.h"

#define NET_DELTA_FIELD_COUNT    44
#define NET_DELTA_MASK_BYTES     6
#define NET_DELTA_KEYFRAME_BODY  96
#define NET_DELTA_OVERFLOW_BIT   44

typedef struct {
    int prev[NET_DELTA_FIELD_COUNT];
    int keyframeCountdown;
} NetDeltaSendState;

typedef struct {
    int state[NET_DELTA_FIELD_COUNT];
    int valid;
} NetDeltaRecvState;

void net_delta_reset_send(NetDeltaSendState *s);
void net_delta_reset_recv(NetDeltaRecvState *s);

int net_delta_encode(NetDeltaSendState *s, const Player *pl,
                     int playerIdx, int is_keyframe,
                     unsigned char *buf, int maxlen);

int net_delta_decode(NetDeltaRecvState *s, const unsigned char *buf,
                     int len, int is_keyframe, Player *pl);

#endif
