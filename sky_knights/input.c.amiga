// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * SKY KNIGHTS - Input: Keyboard (P1) + Joystick port 2 (P2)
 */
#include <hardware/custom.h>
#include <hardware/cia.h>
#include "input.h"

extern volatile struct Custom custom;
extern volatile struct CIA ciaa;

/* Track each physical key independently, so releasing one alias doesn't
 * clear an action bit still owned by another held alias (e.g. cursor-left
 * and A both map to INP_LEFT — releasing A must not cancel cursor-left). */
#define NUM_MAPPED_KEYS 12
static UBYTE key_down[NUM_MAPPED_KEYS];  /* 1 = held, 0 = up */

static const UBYTE key_codes[NUM_MAPPED_KEYS] = {
    0x4F, /*  0: cursor left  -> LEFT   */
    0x4E, /*  1: cursor right -> RIGHT  */
    0x4C, /*  2: cursor up    -> FLAP   */
    0x20, /*  3: A            -> LEFT   */
    0x22, /*  4: D            -> RIGHT  */
    0x11, /*  5: W            -> FLAP   */
    0x40, /*  6: space        -> FLAP   */
    0x45, /*  7: escape       -> ESC    */
    0x50, /*  8: F1           -> START1 */
    0x51, /*  9: F2           -> START2 */
    0x01, /* 10: 1            -> START1 */
    0x02  /* 11: 2            -> START2 */
};

/* Action produced by each slot; sys keys (ESC/START*) return 0 here and
 * are folded into sys_state instead of the per-player key_state. */
static const UWORD key_actions[NUM_MAPPED_KEYS] = {
    INP_LEFT,   INP_RIGHT,  INP_FLAP,
    INP_LEFT,   INP_RIGHT,  INP_FLAP,
    INP_FLAP,
    0, 0, 0, 0, 0
};

static const UWORD key_sys_actions[NUM_MAPPED_KEYS] = {
    0, 0, 0, 0, 0, 0, 0,
    INP_ESC, INP_START1, INP_START2, INP_START1, INP_START2
};

static UWORD derive_key_state(void)
{
    UWORD s = 0;
    WORD i;
    for (i = 0; i < NUM_MAPPED_KEYS; i++)
        if (key_down[i]) s |= key_actions[i];
    return s;
}

static UWORD derive_sys_state(void)
{
    UWORD s = 0;
    WORD i;
    for (i = 0; i < NUM_MAPPED_KEYS; i++)
        if (key_down[i]) s |= key_sys_actions[i];
    return s;
}

void input_key_down(UWORD code)
{
    WORD i;
    for (i = 0; i < NUM_MAPPED_KEYS; i++)
        if (key_codes[i] == (UBYTE)code) key_down[i] = 1;
}

void input_key_up(UWORD code)
{
    WORD i;
    for (i = 0; i < NUM_MAPPED_KEYS; i++)
        if (key_codes[i] == (UBYTE)code) key_down[i] = 0;
}

void input_reset(void)
{
    WORD i;
    for (i = 0; i < NUM_MAPPED_KEYS; i++) key_down[i] = 0;
}

void input_read(InputState *input)
{
    UWORD joy;

    /* Player 1: keyboard — derive fresh action mask from per-key state. */
    input->p1 = derive_key_state() & (INP_LEFT | INP_RIGHT | INP_FLAP);

    /* System keys */
    input->sys = derive_sys_state();

    /* Player 2: Joystick port 2 (JOY1DAT).
     * Amiga joystick decoding uses direct bit tests:
     *   RIGHT = bit 1
     *   LEFT  = bit 9
     *   UP    = bit 9 XOR bit 8   (also mapped to FLAP)
     * (DOWN  = bit 1 XOR bit 0, unused here)
     */
    input->p2 = 0;
    joy = custom.joy1dat;

    if (joy & 0x0002) input->p2 |= INP_RIGHT;         /* bit 1 */
    if (joy & 0x0200) input->p2 |= INP_LEFT;          /* bit 9 */
    if (((joy >> 9) ^ (joy >> 8)) & 1) input->p2 |= INP_FLAP; /* up = flap */

    /* Fire button: CIA-A PRA bit 7, active low (port 2) */
    if (!(ciaa.ciapra & 0x80))
        input->p2 |= INP_FLAP;  /* fire also flaps */
}
