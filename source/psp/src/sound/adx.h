/**
 * adx.h — minimal CRI ADX (type 0x03, standard linear-scale ADPCM) decoder.
 *
 * Self-contained, zero-dependency. Handles the unencrypted standard ADX that
 * ships with the PC soundtrack (44100 Hz stereo, 18-byte frames, 4-bit). Decodes
 * on demand to interleaved signed-16 PCM so a streaming SDL_RWops can pull from
 * it without holding the whole track in RAM. Rejects encrypted / non-standard
 * variants with a distinct error code rather than emitting garbage.
 */
#ifndef SONICR_ADX_H
#define SONICR_ADX_H

#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

/* Bulk input buffer: decode frames out of RAM so we do a few large fReads
 * instead of one tiny 18-byte fRead per channel per frame. On DC each fRead
 * takes the global io_lock, so the per-frame reads were ~512 lock/unlocks per
 * audio refill, contending with the main thread. */
#define ADX_INBUF_SIZE (16384)

typedef struct {
    FILE    *fp;
    int      channels;         /* 1 or 2 */
    int      sampleRate;
    uint32_t totalSamples;     /* per channel */
    int      frameSize;        /* bytes per channel frame (18) */
    long     dataStart;        /* file offset of the first audio frame */
    int      coef1, coef2;     /* Q12 fixed-point predictor coefficients */

    int      hist1[2], hist2[2];           /* per-channel filter history */
    uint8_t  obuf[32 * 2 * 2];             /* one dual-channel frame of PCM bytes */
    int      obytePos;                     /* consumed bytes in obuf */
    int      obyteCount;                   /* valid bytes in obuf */
    uint32_t samplesDone;                  /* per-channel samples decoded so far */

    uint8_t  inbuf[ADX_INBUF_SIZE];        /* bulk-read ADX input */
    int      inpos;                        /* consumed bytes in inbuf */
    int      inlen;                        /* valid bytes in inbuf */
} AdxDecoder;

/* Open + parse header. 0 on success; <0 on error:
 *   -1 open/read fail, -2 bad magic, -3 encrypted/unsupported encoding,
 *   -4 unexpected block/bit layout, -5 bad channel count. */
int      Adx_Open(AdxDecoder *d, const char *path);

/* Total interleaved s16 PCM byte count for the whole stream. */
int64_t  Adx_PcmBytes(const AdxDecoder *d);

/* Rewind to the first sample (used for looping): clears history + buffers. */
void     Adx_Rewind(AdxDecoder *d);

/* Decode up to nbytes of interleaved s16 PCM into out. Returns bytes produced
 * (0 at end of stream). */
size_t   Adx_Read(AdxDecoder *d, uint8_t *out, size_t nbytes);

void     Adx_Close(AdxDecoder *d);

#endif /* SONICR_ADX_H */
