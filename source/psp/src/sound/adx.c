/**
 * adx.c — CRI ADX type 0x03 (standard linear-scale ADPCM) decoder.
 * See adx.h. Descends from the classic BERO reverse-engineering: fixed 18-byte
 * frames per channel (2-byte scale + 16 bytes of 4-bit nibbles = 32 samples),
 * big-endian, reconstructed by a 2-coefficient fixed-point predictor whose
 * coefficients come from the header's highpass cutoff and sample rate.
 */
#include "adx.h"
#include "fileio.h"   /* fOpen/fRead/... -> stdio on SDL, VMU-aware sr_f* on DC */

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI     3.14159265358979323846
#endif
#ifndef M_SQRT2
#define M_SQRT2  1.41421356237309504880
#endif

static uint16_t rd16be(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}
static uint32_t rd32be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

int Adx_Open(AdxDecoder *d, const char *path)
{
    memset(d, 0, sizeof(*d));
    d->fp = fOpen(path, "rb");
    if (!d->fp) {
        return -1;
    }

    uint8_t h[20];
    if (fRead(h, 1, sizeof(h), d->fp) != sizeof(h)) {
        Adx_Close(d);
        return -1;
    }

    if (rd16be(h) != 0x8000) {
        Adx_Close(d);
        return -2;
    }   /* magic */

    uint16_t dataOff = rd16be(h + 2);
    int encoding = h[4];
    int blockSz = h[5];
    int bitDepth = h[6];
    int channels = h[7];
    uint32_t rate = rd32be(h + 8);
    uint32_t total = rd32be(h + 12);
    uint16_t cutoff = rd16be(h + 16);
    /* h[18] = version, h[19] = flags; a non-zero encryption flag shows up as a
     * non-0x03 encoding on these files, which the check below rejects. */

    if (encoding != 0x03) {
        Adx_Close(d);
        return -3;
    } /* only standard ADPCM */
    if (blockSz != 18 || bitDepth != 4) {
        Adx_Close(d);
        return -4;
    }
    if (channels < 1 || channels > 2) {
        Adx_Close(d);
        return -5;
    }

    d->channels = channels;
    d->sampleRate = (int)rate;
    d->totalSamples = total;
    d->frameSize = blockSz;
    d->dataStart = (long)dataOff + 4; /* audio begins just past the (c)CRI tag */

    /* Predictor coefficients (Q12), standard BERO derivation from the cutoff.
     *
     * This is the ONLY floating point in the whole ADX path (the decode itself
     * is pure integer). The Dreamcast toolchain builds with -m4-single-only
     * plus -ffast-math -mfsca -mfsrra, so runtime cos()/sqrt() there become
     * low-precision hardware approximations (fsca/fsrra) — enough to skew these
     * coefficients and audibly distort the recursive decode, even though the
     * integer decode is bit-exact. Every real ADX uses the standard 500 Hz
     * highpass at 44100 Hz, so pin its exact coefficients (matching the x86
     * double result, keeps SDL bit-exact) and keep the float derivation only as
     * a fallback for non-standard params. */
    if (rate == 44100 && cutoff == 500) {
        d->coef1 = 7334;
        d->coef2 = -3283;
    }
    else {
        double a = M_SQRT2 - cos(2.0 * M_PI * (double)cutoff / (double)rate);
        double b = M_SQRT2 - 1.0;
        double c = (a - sqrt((a + b) * (a - b))) / b;
        d->coef1 = (int)(c * 2.0 * 4096.0);
        d->coef2 = (int)(-(c * c) * 4096.0);
    }

    Adx_Rewind(d);
    return 0;
}

int64_t Adx_PcmBytes(const AdxDecoder *d)
{
    return (int64_t)d->totalSamples * d->channels * 2;
}

void Adx_Rewind(AdxDecoder *d)
{
    if (d->fp) {
        fSeek(d->fp, d->dataStart, SEEK_SET);
    }
    d->hist1[0] = d->hist1[1] = 0;
    d->hist2[0] = d->hist2[1] = 0;
    d->obytePos = d->obyteCount = 0;
    d->samplesDone = 0;
    d->inpos = d->inlen = 0;   /* drop buffered input; refill from dataStart */
}

/* Ensure at least `need` bytes are available at inbuf+inpos, doing one bulk
 * fRead to top up (keeping the io_lock cost to a handful of calls per refill
 * instead of one per 18-byte frame). Returns 1 if `need` bytes are ready. */
static int adx_fill_input(AdxDecoder *d, int need)
{
    int avail = d->inlen - d->inpos;
    if (avail >= need) {
        return 1;
    }

    if (avail > 0) {
        memmove(d->inbuf, d->inbuf + d->inpos, (size_t)avail);
    }
    d->inpos = 0;
    d->inlen = avail;

    size_t got = fRead(d->inbuf + d->inlen, 1,
                       (size_t)(ADX_INBUF_SIZE - d->inlen), d->fp);
    d->inlen += (int)got;
    return (d->inlen - d->inpos) >= need;
}

static int clamp16(int v)
{
    if (v > 32767) {
        return 32767;
    }
    if (v < -32768) {
        return -32768;
    }
    return v;
}

/* Decode one frame per channel into obuf (interleaved s16). Returns byte count
 * produced, 0 at end of stream. */
static int adx_decode_frame(AdxDecoder *d)
{
    if (d->samplesDone >= d->totalSamples) {
        return 0;
    }

    int16_t chsamp[2][32];
    int ch;
    for (ch = 0; ch < d->channels; ch++) {
        if (!adx_fill_input(d, 18)) {
            return 0;
        }
        const uint8_t *fr = d->inbuf + d->inpos;
        d->inpos += 18;

        int scale = (int16_t)rd16be(fr);
        int h1 = d->hist1[ch], h2 = d->hist2[ch];

        int i;
        for (i = 0; i < 32; i++) {
            int byte = fr[2 + (i >> 1)];
            int nib  = (i & 1) ? (byte & 0x0F) : (byte >> 4);
            if (nib & 0x08) {
                nib -= 16;                 /* sign-extend 4-bit */
            }
            int pred = (d->coef1 * h1 + d->coef2 * h2) >> 12;
            int s = clamp16(nib * scale + pred);
            chsamp[ch][i] = (int16_t)s;
            h2 = h1; h1 = s;
        }
        d->hist1[ch] = h1; d->hist2[ch] = h2;
    }

    int n = 32;
    uint32_t remain = d->totalSamples - d->samplesDone;
    if ((uint32_t)n > remain) {
        n = (int)remain;
    }

    int16_t *out = (int16_t *)d->obuf;
    int i, o = 0;
    for (i = 0; i < n; i++) {
        for (ch = 0; ch < d->channels; ch++) {
            out[o++] = chsamp[ch][i];
        }
    }

    d->obytePos = 0;
    d->obyteCount = o * 2;
    d->samplesDone += n;
    return d->obyteCount;
}

size_t Adx_Read(AdxDecoder *d, uint8_t *out, size_t nbytes)
{
    size_t done = 0;
    while (done < nbytes) {
        if (d->obytePos >= d->obyteCount) {
            if (adx_decode_frame(d) == 0) {
                break;       /* EOF */
            }
        }
        size_t avail = (size_t)(d->obyteCount - d->obytePos);
        size_t want = nbytes - done;
        size_t copy = (want < avail) ? want : avail;
        memcpy(out + done, d->obuf + d->obytePos, copy);
        done += copy;
        d->obytePos += (int)copy;
    }
    return done;
}

void Adx_Close(AdxDecoder *d)
{
    if (d->fp) {
        fClose(d->fp);
        d->fp = NULL;
    }
}
