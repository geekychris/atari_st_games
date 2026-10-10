// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * Orb Hunter - Input: Joystick port 2 + keyboard
 */
#include <hardware/custom.h>
#include <hardware/cia.h>
#include "input.h"

/* Hardware registers */
extern volatile struct Custom custom;
extern volatile struct CIA ciaa;

/* Per-physical-key state, so releasing one alternate for an action
   (e.g. A vs. cursor-left, alt vs. space) doesn't clear the action while
   the other alternate is still held. */
#define KEY_MAX 128
static UBYTE key_down[KEY_MAX];

/* Map raw code -> action bit, or 0 for unmapped. */
static UWORD code_to_action(UWORD code)
{
    switch (code) {
        case 0x4F: return INPUT_LEFT;   /* cursor left */
        case 0x4E: return INPUT_RIGHT;  /* cursor right */
        case 0x4C: return INPUT_UP;     /* cursor up */
        case 0x4D: return INPUT_DOWN;   /* cursor down */
        case 0x20: return INPUT_LEFT;   /* A */
        case 0x22: return INPUT_RIGHT;  /* D */
        case 0x11: return INPUT_UP;     /* W */
        case 0x31: return INPUT_DOWN;   /* Z, reused for down */
        case 0x60: return INPUT_FIRE;   /* left alt */
        case 0x64: return INPUT_FIRE;   /* right alt */
        case 0x40: return INPUT_FIRE;   /* space */
        case 0x45: return INPUT_ESC;    /* escape */
        case 0x44: return INPUT_START;  /* return */
    }
    return 0;
}

void input_key_down(UWORD code)
{
    if (code < KEY_MAX) key_down[code] = 1;
}

void input_key_up(UWORD code)
{
    if (code < KEY_MAX) key_down[code] = 0;
}

void input_reset(void)
{
    WORD i;
    for (i = 0; i < KEY_MAX; i++) key_down[i] = 0;
}

/* Aggregate per-key state into action bits. */
static UWORD keyboard_state(void)
{
    UWORD state = 0;
    WORD i;
    for (i = 0; i < KEY_MAX; i++) {
        if (key_down[i]) state |= code_to_action((UWORD)i);
    }
    return state;
}

UWORD input_read(void)
{
    UWORD result = keyboard_state();
    UWORD joy;

    /* Read joystick port 2 (JOY1DAT register) */
    joy = custom.joy1dat;

    /* Joystick decoding for digital joystick:
     * JOY1DAT low byte = X counter, high byte = Y counter.
     * For a digital joystick:
     *   Right: bit1=1, bit0=0  (low byte = 0x02)
     *   Left:  bit1=0, bit0=1  (low byte = 0x01)
     *   Down:  bit9=1, bit8=0  (high byte = 0x02)
     *   Up:    bit9=0, bit8=1  (high byte = 0x01)
     */
    {
        UWORD h = joy & 3;        /* horizontal bits */
        UWORD v = (joy >> 8) & 3; /* vertical bits */

        if (h == 2) result |= INPUT_RIGHT;
        if (h == 1) result |= INPUT_LEFT;
        if (v == 1) result |= INPUT_UP;
        if (v == 2) result |= INPUT_DOWN;
    }

    /* Fire button: CIA-A PRA bit 7, active low (port 2) */
    if (!(ciaa.ciapra & 0x80))
        result |= INPUT_FIRE;

    return result;
}
