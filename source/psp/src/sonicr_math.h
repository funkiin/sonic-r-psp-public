/**
 * sonicr_math.h — Precision-portable math.
 *
 * Routes double / long double math to either double or float at compile
 * time. Default = double (host x86 / SDL builds). Define SONICR_FLOAT32
 * for the Dreamcast SH4 `-m4-single-only` build, where double-precision
 * math is forbidden and `long double` libc routines (atan2l/sinl/lrintl)
 * are not provided by sh-elf newlib.
 */

#ifndef SONICR_MATH_H
#define SONICR_MATH_H

#include <math.h>

#ifdef SONICR_DC
__attribute__((always_inline)) static inline float sr_inv_sqrtf(float x)
{
    asm volatile ("fsrra %0" : "+f"(x));
    return x;
}
__attribute__((always_inline)) static inline float sr_sqrtf(float x)
{
    return (x == 0.0f) ? 0.0f : sr_inv_sqrtf(x) * x;
}
#elif defined(SONICR_PSP)
__attribute__((always_inline)) static inline float sr_sqrtf(float x)
{
    float r;
    __asm__ volatile (
        "mtv %1, S000\n"
        "vsqrt.s S000, S000\n"
        "mfv %0, S000\n"
        : "=r"(r)
        : "r"(x)
    );
    return r;
}
#else
static inline float sr_sqrtf(float x)
{
    return sqrtf(x);
}
#endif

#ifdef SONICR_FLOAT32
typedef float sr_double;

static inline sr_double sr_atan2(sr_double y, sr_double x)
{
    return atan2f(y, x);
}

static inline sr_double sr_sqrt(sr_double x)
{
#ifdef SONICR_PSP
    return sr_sqrtf(x);
#else
    return sqrtf(x);
#endif
}

static inline sr_double sr_sin(sr_double x) {
    return sinf(x);
}

static inline long sr_lrint(sr_double x) {
    return lrintf(x);
}
#else
typedef double sr_double;

static inline sr_double sr_atan2(sr_double y, sr_double x)
{
    return atan2(y, x);
}

static inline sr_double sr_sqrt(sr_double x)
{
    return sqrt(x);
}

static inline sr_double sr_sin(sr_double x)
{
    return sin(x);
}

static inline long sr_lrint(sr_double x)
{
    return lrint(x);
}
#endif

#endif /* SONICR_MATH_H */
