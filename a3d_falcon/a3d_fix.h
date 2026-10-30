// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * a3d_fix.h - 16.16 fixed point for the games' simulations: a 68020 has no
 * FPU. FMUL and FDIV use 64-bit intermediates (built with -m68020-60, so
 * no instruction the 68060 lacks); square roots and lengths stay in 32 bits.
 */
#ifndef A3D_FIX_H
#define A3D_FIX_H

#include <stdint.h>

typedef int32_t fix;
#define FONE        65536
#define FX(x)       ((fix)((x) * 65536.0))
#ifdef A3_FALCON
/* The Falcon build is 68020-68060 code (a 68060 lacks the 64-bit multiply),
 * where GCC makes the 64-bit product a libgcc call: Lander 3D spent more
 * than half of its 11 s level start in it on a 68030. So the 68020-68040's
 * one muls.l does it when the CPU has it (a3f_mul64, from the _CPU cookie:
 * a3d_fix.c), the same product either way. */
extern int a3f_mul64;
static inline fix a3f_fmul(fix a, fix b)
{
    if (a3f_mul64) {
        int32_t hi, lo = a;
        __asm__("muls.l %2,%1:%0" : "+d"(lo), "=d"(hi) : "d"(b));
        return (fix)((uint32_t)hi << 16 | (uint32_t)lo >> 16);
    }
    return (fix)(((int64_t)a * b) >> 16);
}
#define FMUL(a, b)  a3f_fmul((a), (b))

/* a / b in 16.16: the 68020-68040's 64 / 32-bit divs.l, unless the
 * quotient overflows 32 bits (then the C, which keeps its low 32 bits) */
static inline fix a3f_fdiv(fix a, fix b)
{
    if (a3f_mul64 && b) {
        int32_t hi = a >> 16, lo = (int32_t)((uint32_t)a << 16);
        uint8_t ovf;
        __asm__("divs.l %3,%1:%0\n\tsvs %2" : "+d"(lo), "+d"(hi), "=d"(ovf) : "d"(b) : "cc");
        if (!ovf) return lo;
    }
    return (fix)((((int64_t)a) * 65536) / b);
}

/* the whole 64-bit product, the same way */
static inline int64_t a3f_lmul(int32_t a, int32_t b)
{
    if (a3f_mul64) {
        int32_t hi, lo = a;
        __asm__("muls.l %2,%1:%0" : "+d"(lo), "=d"(hi) : "d"(b));
        return (int64_t)((uint64_t)(uint32_t)hi << 32 | (uint32_t)lo);
    }
    return (int64_t)a * b;
}
#else
#define FMUL(a, b)  ((fix)(((int64_t)(a) * (b)) >> 16))
#endif
#ifdef A3_FALCON
#define FDIV(a, b)  a3f_fdiv((a), (b))
#else
#define FDIV(a, b)  ((fix)((((int64_t)(a)) * 65536) / (b)))
#endif
#define FINT(a)     ((a) >> 16)
#define FROMI(i)    ((fix)(i) * 65536)

fix fsqrt(fix a);
fix fhypot(fix a, fix b);
fix fhypot3(fix a, fix b, fix c);
fix fsin(fix rad);
fix fcos(fix rad);
fix fatan2(fix y, fix x);               /* radians, -pi..pi, as Math.atan2 */
fix fasin(fix x);
fix fexp_neg(fix x);                    /* e^-x for x >= 0 */
int a3_angle(fix rad);                  /* radians -> the engine's 16-bit angle */

#endif
