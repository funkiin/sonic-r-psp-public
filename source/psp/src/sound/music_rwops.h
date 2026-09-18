/**
 * music_rwops.h — SDL_RWops shims that present raw .SON PCM and .ADX ADPCM to
 * SDL_mixer as a virtual streaming WAV.
 *
 * Both build a 44-byte WAV header in memory, then serve the audio body on
 * demand: .SON passes the on-disk PCM straight through; .ADX decodes frames
 * lazily. Either way SDL_mixer's WAV backend streams it, so no whole-track RAM
 * buffer and all existing Mix_PlayMusic machinery (loop / volume / fade) works
 * unchanged. Return NULL if the file is missing or not a supported variant.
 *
 * Hand the result to Mix_LoadMUS_RW(rw, 1) — freesrc=1 lets SDL_mixer close and
 * free everything (RWops, file handle, decoder) on Mix_FreeMusic.
 */
#ifndef SONICR_MUSIC_RWOPS_H
#define SONICR_MUSIC_RWOPS_H

#include <SDL.h>
#include <stdint.h>

/* Raw headerless PCM (.SON). Caller supplies the format the file lacks. */
SDL_RWops *MusicRW_OpenSon(const char *path, uint32_t rate,
                           uint16_t channels, uint16_t bits);

/* CRI ADX (.adx), standard unencrypted type 0x03. */
SDL_RWops *MusicRW_OpenAdx(const char *path);

/* AICA/Yamaha ADPCM (.adp), headerless interleaved (wav2adpcm -n -i -t).
 * Caller supplies the format the headerless file lacks. */
SDL_RWops *MusicRW_OpenAdp(const char *path, uint32_t rate, uint16_t channels);

#endif /* SONICR_MUSIC_RWOPS_H */
