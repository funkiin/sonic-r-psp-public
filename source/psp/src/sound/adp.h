/**
 * adp.h — AICA (Yamaha) ADPCM decoder for headerless interleaved stereo .adp,
 * as produced by `wav2adpcm -n -i -t` (the Dreamcast music format).
 *
 * Written from the AICA decode model directly (tables verified against
 * AICA_E.pdf), NOT lifted from KOS's adpcm2pcm — that reference has a nibble
 * ordering bug. Clean recurrence, no high-pass, no diff clamp:
 *
 *   DIFF  = 1,3,5,7,9,11,13,15, -1,-3,-5,-7,-9,-11,-13,-15
 *   SCALE = 0xE6,0xE6,0xE6,0xE6, 0x133,0x199,0x200,0x266
 *   cur=0, quant=127
 *   delta = trunc_toward_zero(quant * DIFF[code] / 8)
 *   cur   = clamp(cur + delta, -32768, 32767)
 *   quant = clamp((quant * SCALE[code & 7]) >> 8, 127, 24576)
 *
 * Nibble order per channel stream: even sample = low nibble, odd = high nibble.
 * Stereo is nibble-interleaved per the `-i` layout: each byte holds one left
 * nibble (high) + one right nibble (low); the two channels keep independent
 * predictor state. Decodes on demand to interleaved s16 for streaming.
 *
 * The file is headerless, so rate + channel count come from the caller.
 */
#ifndef SONICR_ADP_H
#define SONICR_ADP_H

#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

typedef struct {
    FILE    *fp;
    int      channels;         /* 1 or 2 */
    int      sampleRate;
    int64_t  fileBytes;        /* ADP payload byte count */
    int64_t  pcmBytes;         /* total interleaved s16 output byte count */

    int      cur[2], quant[2]; /* per-channel AICA predictor state */
    uint8_t  obuf[8];          /* one decoded frame (mono 4 bytes, stereo 8) */
    int      obytePos, obyteCount;
} AdpDecoder;

/* Open. rate/channels are supplied by the caller (headerless format).
 * Returns 0 on success, <0 on error. */
int      Adp_Open(AdpDecoder *d, const char *path, int sampleRate, int channels);

/* Total interleaved s16 PCM byte count for the whole stream. */
int64_t  Adp_PcmBytes(const AdpDecoder *d);

/* Rewind to the first sample (for looping): resets predictor state + buffers. */
void     Adp_Rewind(AdpDecoder *d);

/* Decode up to nbytes of interleaved s16 PCM into out. Returns bytes produced
 * (0 at end of stream). */
size_t   Adp_Read(AdpDecoder *d, uint8_t *out, size_t nbytes);

void     Adp_Close(AdpDecoder *d);

#endif /* SONICR_ADP_H */
