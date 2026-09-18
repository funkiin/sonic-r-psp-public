#include <pspaudio.h>
#include <pspthreadman.h>
#include <psptypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "sonicr_paths.h"
#include "replay_voice.h"
#include "adx.h"
#include "adp.h"

extern void SetAllSoundVolumes(void);
extern int  g_soundActive[64];

#define SFX_MAX_SLOTS 64
#define MIX_RATE      44100
#define MIX_FRAMES    1024

typedef struct {
    int16_t *pcm;
    int      frames;
    int      channels;
    int      sampleRate;
} SfxSample;

typedef struct {
    uint64_t phase;
    uint64_t step;
    int      loop;
    int      playing;
    int      freqHz;
    int      gain256;
    int      panL256;
    int      panR256;
    int      mixL;
    int      mixR;
} SfxVoice;

static SfxSample s_sfx[SFX_MAX_SLOTS];
static SfxVoice  s_voice[SFX_MAX_SLOTS];
static int       s_sfxReady;

#define SFX_TRIM_UNITY 256
static const short s_sfxSlotTrim[SFX_MAX_SLOTS] = {
    [0x0B] = 154,
};

#define SFX_CURVE_EXP 1.5f

static float sfx_ds_linear(int dsVolume)
{
    if (dsVolume <= g_volumeBase) {
        return 0.0f;
    }
    if (dsVolume >= 0) {
        return 1.0f;
    }
    float t = (float)(dsVolume - g_volumeBase) / (float)(-g_volumeBase);
    return powf(t, SFX_CURVE_EXP);
}

static float sfx_slot_trim(int slot)
{
    int t = s_sfxSlotTrim[slot];
    if (t <= 0) {
        return 1.0f;
    }
    return (float)t / (float)SFX_TRIM_UNITY;
}

static void voice_recompute_mix(SfxVoice *v)
{
    v->mixL = (v->gain256 * v->panL256) >> 8;
    v->mixR = (v->gain256 * v->panR256) >> 8;
}

static void voice_reset(int slot)
{
    SfxVoice *v = &s_voice[slot];
    v->phase = 0;
    v->step = 0;
    v->loop = 0;
    v->playing = 0;
    v->freqHz = 0;
    v->gain256 = 256;
    v->panL256 = 256;
    v->panR256 = 256;
    voice_recompute_mix(v);
}

static int parse_wav_header(FILE *fp, int *channels, int *sampleRate, int *bits, long *dataStart, uint32_t *dataSize)
{
    char tag[4];
    uint32_t sz;

    if (fread(tag, 1, 4, fp) != 4 || memcmp(tag, "RIFF", 4) != 0) {
        return 0;
    }
    if (fread(&sz, 4, 1, fp) != 1) {
        return 0;
    }
    if (fread(tag, 1, 4, fp) != 4 || memcmp(tag, "WAVE", 4) != 0) {
        return 0;
    }

    int haveFmt = 0;
    int haveData = 0;

    while (!haveFmt || !haveData) {
        char id[4];
        uint32_t chunkSz;
        if (fread(id, 1, 4, fp) != 4) {
            break;
        }
        if (fread(&chunkSz, 4, 1, fp) != 1) {
            break;
        }

        if (memcmp(id, "fmt ", 4) == 0) {
            uint16_t fmtTag, ch, blockAlign, bps;
            uint32_t rate, byteRate;
            long chunkStart = ftell(fp);
            fread(&fmtTag, 2, 1, fp);
            fread(&ch, 2, 1, fp);
            fread(&rate, 4, 1, fp);
            fread(&byteRate, 4, 1, fp);
            fread(&blockAlign, 2, 1, fp);
            fread(&bps, 2, 1, fp);
            (void)byteRate;
            (void)blockAlign;
            (void)fmtTag;
            *channels = (int)ch;
            *sampleRate = (int)rate;
            *bits = (int)bps;
            fseek(fp, chunkStart + (long)chunkSz, SEEK_SET);
            haveFmt = 1;
        }
        else if (memcmp(id, "data", 4) == 0) {
            *dataStart = ftell(fp);
            *dataSize = chunkSz;
            fseek(fp, (long)chunkSz + (long)(chunkSz & 1), SEEK_CUR);
            haveData = 1;
        }
        else {
            fseek(fp, (long)chunkSz + (long)(chunkSz & 1), SEEK_CUR);
        }
    }

    return haveFmt && haveData;
}

static int load_wav_full(const char *path, SfxSample *out)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }

    int channels = 0, sampleRate = 0, bits = 0;
    long dataStart = 0;
    uint32_t dataSize = 0;

    if (!parse_wav_header(fp, &channels, &sampleRate, &bits, &dataStart, &dataSize) ||
        channels <= 0 || channels > 2 || sampleRate <= 0) {
        fclose(fp);
        return 0;
    }

    fseek(fp, dataStart, SEEK_SET);

    int frames;
    int16_t *pcm;

    if (bits == 16) {
        frames = (int)(dataSize / (uint32_t)(2 * channels));
        pcm = (int16_t *)malloc((size_t)frames * channels * sizeof(int16_t));
        if (!pcm) {
            fclose(fp);
            return 0;
        }
        fread(pcm, sizeof(int16_t), (size_t)frames * channels, fp);
    }
    else if (bits == 8) {
        frames = (int)(dataSize / (uint32_t)channels);
        pcm = (int16_t *)malloc((size_t)frames * channels * sizeof(int16_t));
        if (!pcm) {
            fclose(fp);
            return 0;
        }
        uint8_t *tmp = (uint8_t *)malloc((size_t)frames * channels);
        if (!tmp) {
            free(pcm);
            fclose(fp);
            return 0;
        }
        fread(tmp, 1, (size_t)frames * channels, fp);
        for (int i = 0; i < frames * channels; i++) {
            pcm[i] = (int16_t)(((int)tmp[i] - 128) << 8);
        }
        free(tmp);
    }
    else {
        fclose(fp);
        return 0;
    }

    fclose(fp);

    out->pcm = pcm;
    out->frames = frames;
    out->channels = channels;
    out->sampleRate = sampleRate;
    return 1;
}

static void free_sfx_slot(int slot)
{
    if (s_sfx[slot].pcm != NULL) {
        free(s_sfx[slot].pcm);
    }
    memset(&s_sfx[slot], 0, sizeof(SfxSample));
    voice_reset(slot);
    g_soundBuffers[slot] = NULL;
    g_soundActive[slot] = 0;
}

static int load_wav_into_slot(int slot, const char *filename)
{
    if (slot < 0 || slot >= SFX_MAX_SLOTS) {
        return 0;
    }

    if (s_sfx[slot].pcm != NULL) {
        free_sfx_slot(slot);
    }

    char path[512];
    snprintf(path, sizeof(path), DATA_DIR "/SOUND/SFX/%s", filename);

    if (!load_wav_full(path, &s_sfx[slot])) {
        DebugLog("SFX: failed to load slot 0x%02X (%s)\n", slot, filename);
        return 0;
    }

    g_soundBuffers[slot] = (void *)(intptr_t)1;
    g_soundActive[slot] = 1;
    return 1;
}

static const struct { int slot; const char *filename; } s_sfxTable[] = {
    { 0x00, "PAUSE.WAV"    },
    { 0x01, "CHOOSE.WAV"   },
    { 0x02, "SELECT.WAV"   },
    { 0x03, "RUNLEFT.WAV"  },
    { 0x04, "RUNRIGHT.WAV" },
    { 0x05, "AMY.WAV"      },
    { 0x06, "JET.WAV"      },
    { 0x07, "JUMP.WAV"     },
    { 0x08, "SPIN.WAV"     },
    { 0x09, "SPINGO.WAV"   },
    { 0x0A, "SPINREV.WAV"  },
    { 0x0B, "TAILS.WAV"    },
    { 0x0D, "JUMP.WAV"     },
    { 0x0E, "FIRE.WAV"     },
    { 0x0F, "EXPLODE.WAV"  },
    { 0x10, "AMYSKID.WAV"  },
    { 0x11, "AMYWATER.WAV" },
    { 0x12, "WATERRUN.WAV" },
    { 0x13, "WATERRUN.WAV" },
    { 0x14, "BUBBLE.WAV"   },
    { 0x15, "SPLASH.WAV"   },
    { 0x16, "POP.WAV"      },
    { 0x18, "HITCHAR.WAV"  },
    { 0x1A, "BONUS.WAV"    },
    { 0x1B, "GETTOKEN.WAV" },
    { 0x1C, "GETCHAOS.WAV" },
    { 0x1D, "RING1.WAV"    },
    { 0x1E, "RING1.WAV"    },
    { 0x1F, "WARP.WAV"     },
    { 0x20, "SKID1.WAV"    },
    { 0x21, "DOOR.WAV"     },
    { 0x22, "RECORD.WAV"   },
    { 0x23, "GOTALL.WAV"   },
    { 0x24, "BONUS.WAV"    },
    { 0x27, "TAG.WAV"      },
    { 0x2D, "THUNDER.WAV"  },
    { 0x32, "SPRING.WAV"   },
    { 0x33, "BUMPER1.WAV"  },
    { 0x34, "BUMPER2.WAV"  },
    { 0x35, "READY.WAV"    },
    { 0x36, "SET.WAV"      },
    { 0x37, "GO.WAV"       },
    { -1,   NULL           }
};

static void voice_start(int slot, int gain256, int freqHz, int loop)
{
    SfxSample *smp = &s_sfx[slot];
    SfxVoice *v = &s_voice[slot];

    int effHz = freqHz > 0 ? freqHz : smp->sampleRate;
    v->step = ((uint64_t)effHz << 32) / MIX_RATE;
    v->phase = 0;
    v->loop = loop ? 1 : 0;
    v->freqHz = effHz;
    v->gain256 = gain256;
    v->panL256 = 256;
    v->panR256 = 256;
    voice_recompute_mix(v);
    v->playing = 1;
}

void SFX_Play(int slot, int loop, int freq)
{
    if (!s_sfxReady || slot < 0 || slot >= SFX_MAX_SLOTS) {
        return;
    }
    if (s_sfx[slot].pcm == NULL) {
        return;
    }

    float vol = sfx_ds_linear(g_masterVolume);
    if (IS_REPLAY_VOICE_SLOT(slot)) {
        vol = 1.0f;
    }
    int gain256 = (int)(vol * 256.0f);

    if (loop) {
        SfxVoice *v = &s_voice[slot];
        if (!v->playing || !v->loop) {
            voice_start(slot, gain256, freq, 1);
            return;
        }
        if (freq && freq != v->freqHz) {
            v->freqHz = freq;
            v->step = ((uint64_t)freq << 32) / MIX_RATE;
        }
        return;
    }

    if (slot == 0x0D) {
        freq = 22050 + 5512;
    }

    voice_start(slot, gain256, freq, 0);
}

void SFX_Stop(int slot)
{
    if (slot < 0 || slot >= SFX_MAX_SLOTS) {
        return;
    }
    s_voice[slot].playing = 0;
}

void SFX_StopAll(void)
{
    for (int i = 0; i < SFX_MAX_SLOTS; i++) {
        s_voice[i].playing = 0;
    }
}

void SFX_Tick(void)
{
}

void SFX_SetVolume(int slot, int dsVolume)
{
    if (slot < 0 || slot >= SFX_MAX_SLOTS || s_sfx[slot].pcm == NULL) {
        return;
    }
    float linear = sfx_ds_linear(dsVolume) * sfx_slot_trim(slot);
    SfxVoice *v = &s_voice[slot];
    v->gain256 = (int)(linear * 256.0f);
    voice_recompute_mix(v);
}

void SFX_SetPan(int slot, int dsPan)
{
    if (slot < 0 || slot >= SFX_MAX_SLOTS || s_sfx[slot].pcm == NULL) {
        return;
    }
    float pan = (float)dsPan / 10000.0f;
    if (pan < -1.0f) pan = -1.0f;
    if (pan > 1.0f) pan = 1.0f;

    SfxVoice *v = &s_voice[slot];
    v->panL256 = (int)(256.0f * (1.0f - pan) / 2.0f);
    v->panR256 = (int)(256.0f * (1.0f + pan) / 2.0f);
    voice_recompute_mix(v);
}

void SFX_SetPosition(int slot, int pos)
{
    (void)slot;
    (void)pos;
}

int SFX_ClipDurationMs(int slot)
{
    if (slot < 0 || slot >= SFX_MAX_SLOTS || s_sfx[slot].pcm == NULL) {
        return 0;
    }
    return (int)(((int64_t)s_sfx[slot].frames * 1000) / s_sfx[slot].sampleRate);
}

void LoadSoundEffect(const char *filename, int slot)
{
    load_wav_into_slot(slot, filename);
}

static int PlaySoundSimple(int slot)
{
    if (slot < 0 || slot >= SFX_MAX_SLOTS) {
        return 0;
    }
    if (g_soundActive[slot] == 0) {
        return 0;
    }
    if (g_optSfxVolume == 0) {
        return 1;
    }

    SFX_Play(slot, 0, 0);
    return 1;
}

static int PlaySoundWithParams(int slot, int distance, int freqParam)
{
    if (slot < 0 || slot >= SFX_MAX_SLOTS) {
        return 0;
    }
    if (g_soundActive[slot] == 0) {
        return 0;
    }
    if (g_optSfxVolume == 0) {
        return 1;
    }

    if (distance != 0x100) {
        int d = distance;
        if (d < 0x40) {
            d = 0xFF;
        }
        else {
            d = 0xFF - d;
        }

        int volDiff = g_masterVolume - g_volumeBase;
        if (volDiff < 0) {
            volDiff = -volDiff;
        }

        int divisor = (g_demoMode == 2) ? 0x12c : 0xff;

        int attenVol = (d * volDiff) / divisor + g_volumeBase;

        SFX_SetVolume(slot, attenVol);
    }

    if (freqParam != 0) {
        freqParam = (freqParam * 99900) / 255 + 100;
    }

    SFX_Play(slot, 1, freqParam);
    return 1;
}

static int g_ringAlternate;

void PlaySoundEffect(int soundCmd, int distance, int freqParam)
{
    int slot = soundCmd & 0xFFFF;

    if (soundCmd & 0xFFFF0000) {
        PlaySoundWithParams(slot, distance, freqParam);
    }
    else {
        PlaySoundSimple(slot);
    }

    if (g_demoMode == DEMO_REPLAY) {
        return;
    }

    if ((soundCmd & 0xFFFF) == 0x1D) {
        slot = 0x1D + g_ringAlternate;
        g_ringAlternate = (g_ringAlternate + 1) & 1;
    }
}

typedef enum { MUSIC_NONE, MUSIC_RAW, MUSIC_ADX, MUSIC_ADP } MusicKind;

typedef struct {
    MusicKind  kind;
    FILE      *fp;
    long       dataStart;
    uint32_t   dataSize;
    uint32_t   bytesLeft;
    int        channels;
    AdxDecoder adx;
    AdpDecoder adp;
    int        loop;
    int        eof;
} MusicStream;

static void ring_reset(void);

static MusicStream s_music;
static int   s_musicReady;
static int   s_musicPaused;
static int   s_currentTrack;
static int   s_currentTrackIsFanfare;
static int   s_logicalTrack;
static DWORD s_trackStartMs;
static int   s_musicLevel = 8;
static int   s_musicDucked;

static const int s_trackDurationMs[23] = {
         0,      0,  43210,   6993,  10170,  55580, 305305, 283904,
    270474, 237737, 295113, 241929, 240949, 164269, 208959, 210448,
    202700, 210360, 238142,   3921,   6451,   5910, 5448657
};

static void music_close(void)
{
    if (s_music.kind == MUSIC_ADX) {
        Adx_Close(&s_music.adx);
    }
    else if (s_music.kind == MUSIC_ADP) {
        Adp_Close(&s_music.adp);
    }
    else if (s_music.fp != NULL) {
        fclose(s_music.fp);
    }
    memset(&s_music, 0, sizeof(s_music));
    s_music.kind = MUSIC_NONE;
    ring_reset();
}

static int music_open_raw(const char *path, long dataStart, uint32_t dataSize, int channels)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    fseek(fp, dataStart, SEEK_SET);
    s_music.fp = fp;
    s_music.dataStart = dataStart;
    s_music.dataSize = dataSize;
    s_music.bytesLeft = dataSize;
    s_music.channels = channels;
    s_music.kind = MUSIC_RAW;
    return 1;
}

static AdxDecoder s_musicAdxProbe;
static AdpDecoder s_musicAdpProbe;

static int music_try_open(int trackNum)
{
    char path[512];

    snprintf(path, sizeof(path), DATA_DIR "/MUSIC/track%d.son", trackNum);
    {
        FILE *fp = fopen(path, "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long sz = ftell(fp);
            fclose(fp);
            if (sz > 0 && music_open_raw(path, 0, (uint32_t)sz, 2)) {
                return 1;
            }
        }
    }

    snprintf(path, sizeof(path), DATA_DIR "/MUSIC/track%d.adx", trackNum);
    if (Adx_Open(&s_musicAdxProbe, path) == 0) {
        memset(&s_music, 0, sizeof(s_music));
        s_music.adx = s_musicAdxProbe;
        s_music.channels = s_musicAdxProbe.channels;
        s_music.kind = MUSIC_ADX;
        return 1;
    }

    snprintf(path, sizeof(path), DATA_DIR "/MUSIC/track%d.adp", trackNum);
    if (Adp_Open(&s_musicAdpProbe, path, MIX_RATE, 2) == 0) {
        memset(&s_music, 0, sizeof(s_music));
        s_music.adp = s_musicAdpProbe;
        s_music.channels = 2;
        s_music.kind = MUSIC_ADP;
        return 1;
    }

    snprintf(path, sizeof(path), DATA_DIR "/MUSIC/track%d.wav", trackNum);
    {
        FILE *fp = fopen(path, "rb");
        if (fp) {
            int channels = 0, sampleRate = 0, bits = 0;
            long dataStart = 0;
            uint32_t dataSize = 0;
            int ok = parse_wav_header(fp, &channels, &sampleRate, &bits, &dataStart, &dataSize) &&
                     channels == 2 && sampleRate == MIX_RATE && bits == 16;
            fclose(fp);
            if (ok && music_open_raw(path, dataStart, dataSize, 2)) {
                return 1;
            }
            if (!ok) {
                DebugLog("PlayCD: track%d.wav is not 44100Hz/16-bit/stereo, skipping\n", trackNum);
            }
        }
    }

    return 0;
}

#define MUSIC_RING_FRAMES 16384

static int16_t          s_musicRing[MUSIC_RING_FRAMES * 2];
static volatile uint32_t s_ringWrite;
static volatile uint32_t s_ringRead;

static SceUID        s_musicIoThread = -1;
static volatile int  s_musicIoRun;
static int16_t        s_musicIoScratch[1024 * 2];

static void ring_reset(void)
{
    s_ringWrite = 0;
    s_ringRead = 0;
}

static void ring_push(const int16_t *src, int frames)
{
    uint32_t writeIdx = s_ringWrite;
    for (int i = 0; i < frames; i++) {
        uint32_t slot = (writeIdx + (uint32_t)i) & (MUSIC_RING_FRAMES - 1);
        s_musicRing[2 * slot]     = src[2 * i];
        s_musicRing[2 * slot + 1] = src[2 * i + 1];
    }
    s_ringWrite = writeIdx + (uint32_t)frames;
}

static int music_read(int16_t *out, int frames)
{
    uint32_t avail = s_ringWrite - s_ringRead;
    if (avail > MUSIC_RING_FRAMES) {
        avail = 0;
    }

    int can = (int)avail;
    if (can > frames) {
        can = frames;
    }

    uint32_t readIdx = s_ringRead;
    for (int i = 0; i < can; i++) {
        uint32_t slot = (readIdx + (uint32_t)i) & (MUSIC_RING_FRAMES - 1);
        out[2 * i]     = s_musicRing[2 * slot];
        out[2 * i + 1] = s_musicRing[2 * slot + 1];
    }
    s_ringRead = readIdx + (uint32_t)can;

    for (int i = can; i < frames; i++) {
        out[2 * i] = 0;
        out[2 * i + 1] = 0;
    }

    return frames;
}

static int music_io_thread(SceSize args, void *argp)
{
    (void)args;
    (void)argp;

    while (s_musicIoRun) {
        if (s_music.kind == MUSIC_NONE || s_music.eof) {
            sceKernelDelayThread(5000);
            continue;
        }

        uint32_t used = s_ringWrite - s_ringRead;
        if (used > MUSIC_RING_FRAMES) {
            used = 0;
        }
        uint32_t freeFrames = MUSIC_RING_FRAMES - used;

        if (freeFrames < 1024) {
            sceKernelDelayThread(5000);
            continue;
        }

        int want = 1024;
        size_t producedBytes = 0;

        if (s_music.kind == MUSIC_RAW) {
            size_t wantBytes = (size_t)want * 4;
            size_t chunk = s_music.bytesLeft < wantBytes ? s_music.bytesLeft : wantBytes;
            if (chunk > 0) {
                producedBytes = fread(s_musicIoScratch, 1, chunk, s_music.fp);
                s_music.bytesLeft -= (uint32_t)producedBytes;
            }
        }
        else if (s_music.kind == MUSIC_ADX) {
            producedBytes = Adx_Read(&s_music.adx, (uint8_t *)s_musicIoScratch, (size_t)want * 4);
        }
        else if (s_music.kind == MUSIC_ADP) {
            producedBytes = Adp_Read(&s_music.adp, (uint8_t *)s_musicIoScratch, (size_t)want * 4);
        }

        int producedFrames = (int)(producedBytes / 4);
        if (producedFrames > 0) {
            ring_push(s_musicIoScratch, producedFrames);
        }

        if (producedFrames < want) {
            if (!s_music.loop) {
                s_music.eof = 1;
            }
            else if (s_music.kind == MUSIC_RAW) {
                fseek(s_music.fp, s_music.dataStart, SEEK_SET);
                s_music.bytesLeft = s_music.dataSize;
            }
            else if (s_music.kind == MUSIC_ADX) {
                Adx_Rewind(&s_music.adx);
            }
            else if (s_music.kind == MUSIC_ADP) {
                Adp_Rewind(&s_music.adp);
            }
        }
    }

    sceKernelExitDeleteThread(0);
    return 0;
}

static int music_effective_gain256(void)
{
    int base = s_musicDucked ? 128 : 256;
    return base * s_musicLevel / 8;
}

void Music_SetVolume(int level)
{
    if (level < 0) level = 0;
    if (level > 8) level = 8;
    s_musicLevel = level;
}

void Music_SetDucked(int ducked)
{
    s_musicDucked = ducked ? 1 : 0;
}

void StopCD(void)
{
    music_close();
    s_currentTrack = 0;
    s_currentTrackIsFanfare = 0;
    s_logicalTrack = 0;
    s_trackStartMs = 0;
    s_musicPaused = 0;
    SFX_DuckStop();
}

void PlayCD(int trackNum)
{
    if (trackNum < 2 || trackNum > 21) {
        return;
    }
    if (g_musicEnabled == 0) {
        StopCD();
        return;
    }

    if (s_currentTrack == trackNum && s_music.kind != MUSIC_NONE) {
        if (!s_music.eof) {
            s_logicalTrack = trackNum;
            s_trackStartMs = timeGetTime();
            return;
        }
        if (s_currentTrackIsFanfare) {
            return;
        }
    }

    music_close();
    s_currentTrack = 0;

    if (!music_try_open(trackNum)) {
        DebugLog("PlayCD(%d): no loadable MUSIC/track%d.* found\n", trackNum, trackNum);
        return;
    }

    int isFanfare = (trackNum == 2 || trackNum == 3 || trackNum == 4 ||
                      trackNum == 0x13 || trackNum == 0x14 || trackNum == 0x15);
    s_music.loop = !isFanfare;
    s_music.eof = 0;
    s_musicPaused = 0;

    s_currentTrack = trackNum;
    s_currentTrackIsFanfare = isFanfare;
    s_logicalTrack = trackNum;
    s_trackStartMs = timeGetTime();
}

void PauseCD(void)
{
    s_musicPaused = 1;
}

void ResumeCD(void)
{
    s_musicPaused = 0;
}

int GetLogicalCDTrack(void)
{
    if (!s_musicReady || s_logicalTrack == 0) {
        return 0;
    }

    int elapsed = (int)(timeGetTime() - s_trackStartMs);
    if (elapsed < 0) {
        elapsed = -elapsed;
    }

    if (s_logicalTrack > 0 && s_logicalTrack < 23 &&
        elapsed > s_trackDurationMs[s_logicalTrack]) {
        s_logicalTrack++;
    }

    return s_logicalTrack;
}

void UpdateCDPlayback(int trackNum)
{
    if (g_musicEnabled == 0) {
        return;
    }
    PlayCD(trackNum);
}

int OpenCDDevice(void)
{
    s_musicReady = 1;
    g_mciDeviceId = 1;
    return 1;
}

void CloseCDDevice(void)
{
    music_close();
    s_currentTrack = 0;
    s_musicReady = 0;
    g_mciDeviceId = 0;
}

static int          s_audioChannel = -1;
static SceUID        s_audioThread = -1;
static volatile int  s_audioThreadRun;
static int16_t       s_pcmOut[MIX_FRAMES * 2];
static int16_t       s_musicScratch[MIX_FRAMES * 2];
static int32_t       s_mixAccum[MIX_FRAMES * 2];

static void mix_buffer(int16_t *out, int frames)
{
    memset(s_mixAccum, 0, sizeof(int32_t) * frames * 2);

    for (int s = 0; s < SFX_MAX_SLOTS; s++) {
        SfxVoice *v = &s_voice[s];
        if (!v->playing) {
            continue;
        }
        SfxSample *smp = &s_sfx[s];
        if (smp->pcm == NULL || smp->frames <= 0) {
            v->playing = 0;
            continue;
        }

        uint64_t end = (uint64_t)smp->frames << 32;
        uint64_t phase = v->phase;
        int channels = smp->channels;

        for (int i = 0; i < frames; i++) {
            if (phase >= end) {
                if (v->loop) {
                    phase %= end;
                }
                else {
                    v->playing = 0;
                    break;
                }
            }

            uint32_t idx = (uint32_t)(phase >> 32);
            uint32_t frac = (uint32_t)(phase & 0xFFFFFFFFu);
            uint32_t idx1 = idx + 1;
            if (idx1 >= (uint32_t)smp->frames) {
                idx1 = v->loop ? 0u : idx;
            }

            int32_t l0, l1, r0, r1;
            if (channels == 2) {
                l0 = smp->pcm[2 * idx];
                l1 = smp->pcm[2 * idx1];
                r0 = smp->pcm[2 * idx + 1];
                r1 = smp->pcm[2 * idx1 + 1];
            }
            else {
                l0 = r0 = smp->pcm[idx];
                l1 = r1 = smp->pcm[idx1];
            }

            int32_t l = l0 + (int32_t)(((int64_t)(l1 - l0) * (int32_t)frac) >> 32);
            int32_t r = r0 + (int32_t)(((int64_t)(r1 - r0) * (int32_t)frac) >> 32);

            s_mixAccum[2 * i]     += (l * v->mixL) >> 8;
            s_mixAccum[2 * i + 1] += (r * v->mixR) >> 8;

            phase += v->step;
        }

        v->phase = phase;
    }

    if (!s_musicPaused && s_music.kind != MUSIC_NONE) {
        int got = music_read(s_musicScratch, frames);
        int gain = music_effective_gain256();
        for (int i = 0; i < got; i++) {
            s_mixAccum[2 * i]     += (s_musicScratch[2 * i]     * gain) >> 8;
            s_mixAccum[2 * i + 1] += (s_musicScratch[2 * i + 1] * gain) >> 8;
        }
    }

    for (int i = 0; i < frames * 2; i++) {
        int32_t v = s_mixAccum[i];
        if (v > 32767) v = 32767;
        else if (v < -32768) v = -32768;
        out[i] = (int16_t)v;
    }
}

static int audio_thread_entry(SceSize args, void *argp)
{
    (void)args;
    (void)argp;

    while (s_audioThreadRun) {
        mix_buffer(s_pcmOut, MIX_FRAMES);
        sceAudioOutputBlocking(s_audioChannel, PSP_AUDIO_VOLUME_MAX, s_pcmOut);
    }

    sceAudioChRelease(s_audioChannel);
    s_audioChannel = -1;
    sceKernelExitDeleteThread(0);
    return 0;
}

void InitDirectSound(void)
{
    DebugLog("InitDirectSound (PSP native audio)\n");

    for (int i = 0; i < SFX_MAX_SLOTS; i++) {
        voice_reset(i);
    }

    s_audioChannel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, MIX_FRAMES, PSP_AUDIO_FORMAT_STEREO);
    if (s_audioChannel < 0) {
        DebugLog("InitDirectSound: sceAudioChReserve failed\n");
        return;
    }

    s_audioThreadRun = 1;
    s_audioThread = sceKernelCreateThread("sr_audio", audio_thread_entry, 0x12, 0x4000, 0, NULL);
    if (s_audioThread >= 0) {
        sceKernelStartThread(s_audioThread, 0, NULL);
    }

    ring_reset();
    s_musicIoRun = 1;
    s_musicIoThread = sceKernelCreateThread("sr_music_io", music_io_thread, 0x14, 0x4000, 0, NULL);
    if (s_musicIoThread >= 0) {
        sceKernelStartThread(s_musicIoThread, 0, NULL);
    }

    s_sfxReady = 1;
    g_lpDirectSound = (void *)(intptr_t)1;
    g_initFeatureB = 1;

    for (int i = 0; s_sfxTable[i].slot >= 0; i++) {
        load_wav_into_slot(s_sfxTable[i].slot, s_sfxTable[i].filename);
    }

    SetAllSoundVolumes();
}

void CloseDirectSound(void)
{
    s_sfxReady = 0;

    if (s_audioThreadRun) {
        s_audioThreadRun = 0;
        if (s_audioThread >= 0) {
            sceKernelWaitThreadEnd(s_audioThread, NULL);
            s_audioThread = -1;
        }
    }

    if (s_musicIoRun) {
        s_musicIoRun = 0;
        if (s_musicIoThread >= 0) {
            sceKernelWaitThreadEnd(s_musicIoThread, NULL);
            s_musicIoThread = -1;
        }
    }

    for (int i = 0; i < SFX_MAX_SLOTS; i++) {
        free_sfx_slot(i);
    }

    music_close();
    g_lpDirectSound = NULL;
    DebugLog("SFX: closed\n");
}
