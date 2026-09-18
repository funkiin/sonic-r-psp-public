/*
 * endian_util.h -- Little-endian accessors for binary data buffers.
 *
 * All game data files (.TER, .BIN, etc.) and packed structures from the
 * original x86 binary are stored in little-endian byte order.  On LE
 * platforms (x86, ARM64, SH4) these compile to plain pointer dereferences.
 * On BE platforms (PowerPC GameCube/Wii) they byte-swap.
 */
#ifndef ENDIAN_UTIL_H
#define ENDIAN_UTIL_H

#include <stdint.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define SONICR_BIG_ENDIAN 1
#else
#define SONICR_BIG_ENDIAN 0
#endif

#if SONICR_BIG_ENDIAN

static inline int16_t  rl16s(const void *p) { const uint8_t *b = (const uint8_t *)p; return (int16_t)(b[0] | (b[1] << 8)); }
static inline uint16_t rl16u(const void *p) { const uint8_t *b = (const uint8_t *)p; return (uint16_t)(b[0] | (b[1] << 8)); }
static inline int32_t  rl32s(const void *p) { const uint8_t *b = (const uint8_t *)p; return (int32_t)(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24)); }
static inline uint32_t rl32u(const void *p) { const uint8_t *b = (const uint8_t *)p; return (uint32_t)(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24)); }

static inline void wl16(void *p, uint16_t v) { uint8_t *b = (uint8_t *)p; b[0] = v; b[1] = v >> 8; }
static inline void wl32(void *p, uint32_t v) { uint8_t *b = (uint8_t *)p; b[0] = v; b[1] = v >> 8; b[2] = v >> 16; b[3] = v >> 24; }

#else

static inline int16_t  rl16s(const void *p) { return *(const int16_t *)p; }
static inline uint16_t rl16u(const void *p) { return *(const uint16_t *)p; }
static inline int32_t  rl32s(const void *p) { return *(const int32_t *)p; }
static inline uint32_t rl32u(const void *p) { return *(const uint32_t *)p; }

static inline void wl16(void *p, uint16_t v) { *(uint16_t *)p = v; }
static inline void wl32(void *p, uint32_t v) { *(uint32_t *)p = v; }

#endif

/* In-place byte-swap helpers for post-load fixup of LE binary data on BE hosts */
#if SONICR_BIG_ENDIAN
static inline void bswap16_inplace(void *p) {
    uint8_t *b = (uint8_t *)p;
    uint8_t t = b[0]; b[0] = b[1]; b[1] = t;
}
static inline void bswap32_inplace(void *p) {
    uint8_t *b = (uint8_t *)p;
    uint8_t t;
    t = b[0]; b[0] = b[3]; b[3] = t;
    t = b[1]; b[1] = b[2]; b[2] = t;
}
static inline void bswap16_arr(void *p, int count) {
    uint16_t *s = (uint16_t *)p;
    for (int i = 0; i < count; i++) {
        bswap16_inplace(&s[i]);
    }
}
static inline void bswap32_arr(void *p, int count) {
    uint32_t *s = (uint32_t *)p;
    for (int i = 0; i < count; i++) {
        bswap32_inplace(&s[i]);
    }
}
#else
static inline void bswap16_inplace(void *p) { (void)p; }
static inline void bswap32_inplace(void *p) { (void)p; }
static inline void bswap16_arr(void *p, int count) { (void)p; (void)count; }
static inline void bswap32_arr(void *p, int count) { (void)p; (void)count; }
#endif

/* Convenience: read from base + byte offset */
#define RL16S(base, off) rl16s((const char *)(base) + (off))
#define RL16U(base, off) rl16u((const char *)(base) + (off))
#define RL32S(base, off) rl32s((const char *)(base) + (off))
#define RL32U(base, off) rl32u((const char *)(base) + (off))
#define WL16(base, off, v) wl16((char *)(base) + (off), (v))
#define WL32(base, off, v) wl32((char *)(base) + (off), (v))

#endif /* ENDIAN_UTIL_H */
