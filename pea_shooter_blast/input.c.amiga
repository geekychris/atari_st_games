// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * Pea Shooter Blast - Input: Joystick port 2 + keyboard
 *
 * Physical keys are tracked independently in phys_key_down[]; the logical
 * INPUT_* bits are (re-)derived on every up/down event so that releasing
 * one mapped key does not clear an action that another mapped key still
 * holds (e.g. releasing 'A' while cursor-left is still held).
 */
#include <hardware/custom.h>
#include <hardware/cia.h>
#include "input.h"

extern volatile struct Custom custom;
extern volatile struct CIA ciaa;

/* RAWKEY codes are 7-bit (0x00..0x7F) */
#define NUM_RAWKEYS 128

static UBYTE phys_key_down[NUM_RAWKEYS];
static UWORD key_state = 0;

/* Map a physical raw-key code to a set of logical INPUT_* bits (0 if unmapped). */
static UWORD key_to_bits(UWORD code)
{
    switch (code) {
        case 0x4F: return INPUT_LEFT;   /* cursor left */
        case 0x4E: return INPUT_RIGHT;  /* cursor right */
        case 0x4C: return INPUT_JUMP;   /* cursor up */
        case 0x4D: return INPUT_DOWN;   /* cursor down */
        case 0x20: return INPUT_LEFT;   /* A */
        case 0x22: return INPUT_RIGHT;  /* D */
        case 0x11: return INPUT_JUMP;   /* W */
        case 0x31: return INPUT_DOWN;   /* Z (labelled 'S' in original mapping) */
        case 0x60: return INPUT_FIRE;   /* left alt */
        case 0x64: return INPUT_FIRE;   /* right alt */
        case 0x40: return INPUT_FIRE;   /* space */
        case 0x45: return INPUT_ESC;    /* escape */
        default:   return 0;
    }
}

/* Rebuild key_state by OR-ing bits of every currently-held physical key. */
static void rebuild_key_state(void)
{
    UWORD i;
    UWORD bits = 0;
    for (i = 0; i < NUM_RAWKEYS; i++) {
        if (phys_key_down[i]) {
            bits |= key_to_bits(i);
        }
    }
    key_state = bits;
}

void input_key_down(UWORD code)
{
    UWORD idx = code & 0x7F;
    if (!phys_key_down[idx]) {
        phys_key_down[idx] = 1;
        /* Fast path: just OR in this key's bits */
        key_state |= key_to_bits(idx);
    }
}

void input_key_up(UWORD code)
{
    UWORD idx = code & 0x7F;
    if (phys_key_down[idx]) {
        phys_key_down[idx] = 0;
        /* Must rebuild because a released key may share bits with a still-held one */
        rebuild_key_state();
    }
}

void input_reset(void)
{
    UWORD i;
    for (i = 0; i < NUM_RAWKEYS; i++) phys_key_down[i] = 0;
    key_state = 0;
}

UWORD input_read(void)
{
    UWORD result = key_state;
    UWORD joy;

    /* Read joystick port 2 (JOY1DAT) */
    joy = custom.joy1dat;

    {
        UWORD h = joy & 3;
        UWORD v = (joy >> 8) & 3;

        if (h == 2) result |= INPUT_RIGHT;
        if (h == 1) result |= INPUT_LEFT;
        if (v == 1) result |= INPUT_JUMP;
        if (v == 2) result |= INPUT_DOWN;
    }

    /* Fire button: CIA-A PRA bit 7, active low (port 2) */
    if (!(ciaa.ciapra & 0x80))
        result |= INPUT_FIRE;

    return result;
}
