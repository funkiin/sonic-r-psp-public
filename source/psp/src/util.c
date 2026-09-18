/**
 * util.c — Utility functions
 *
 * DebugLog, Random, InitSineTable.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include <stdarg.h>
#include <math.h>
#ifdef SONICR_PSP
#include <pspdebug.h>
#endif

/**
 * DebugLog — 0x00431140 — 13 bytes
 * vsprintf + OutputDebugStringA wrapper.
 * In the original binary this is essentially a no-op in release builds
 * (the function body is just a RET). Gated on SONICR_DEBUG_LOG, defined by
 * dev builds and omitted by the release build so this compiles back to a RET.
 */
void DebugLog(char *fmt, ...)
{
#ifdef SONICR_DEBUG_LOG
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
#ifdef SONICR_PSP
    pspDebugScreenPrintf("%s", buf);
#endif
    fputs(buf, stderr);
#else
    (void)fmt;
#endif
}

/* Watcom CRT rand() reimplementation — bit-exact match to SONICR.EXE 0x4D2142.
 *   imul edx, [seed], 0x41C64E6D
 *   add  edx, 0x3039
 *   shr  eax, 0x10
 *   and  eax, 0x7FFF
 * Default seed is 1 (Watcom's _Holdrand initial value). */
static unsigned int s_randomSeed = 1;

int Random(void)
{
    s_randomSeed = s_randomSeed * 0x41C64E6Du + 0x3039u;
    return (int)((s_randomSeed >> 16) & 0x7FFF);
}

void Srand(unsigned int seed)
{
    s_randomSeed = seed;
}

/**
 * InitSineTable — 0x004E1366 -> IS THIS WRONG?
 * Builds the 4096-entry sine lookup table.
 * Each entry is sin(angle * 2π / 4096) * 16384 as a signed int.
 * Scale = 0x4000 (16384). Physics uses sinTable[i] >> 2 / 0x1000,
 * so effective multiplier is 16384 / 4 / 4096 = 1.0 at full magnitude.
 * The cosine table is the same data offset by 1024 entries (90°).
 */
void InitSineTable(void)
{
    /* g_sinTable is now baked as static data (see globals.c / sin_table_data.h),
     * reproducing the original binary's x87 table (0x4ca598) bit-for-bit. Computing
     * it here with runtime libm produced 5 entries (incl. the cosine peak/trough at
     * index 1024/3072) off by +-1 versus the original, which desynced attract demos.
     * The table is static now, so this runs no computation; g_cosTable stays
     * &g_sinTable[1024]. */
}
