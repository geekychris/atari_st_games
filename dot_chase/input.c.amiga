// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * DOT_CHASE - Input: Keyboard + Joystick port 2
 */
#include <hardware/custom.h>
#include <hardware/cia.h>
#include "input.h"

extern volatile struct Custom custom;
extern volatile struct CIA ciaa;

/* Keyboard state bits */
#define KEY_UP     1
#define KEY_DOWN   2
#define KEY_LEFT   4
#define KEY_RIGHT  8
#define KEY_FIRE   16
#define KEY_ESC    32

/* Per-physical-key held-state. Indexed by raw keycode. Releasing one alias
 * (e.g. W) must not clear the action if another (e.g. cursor-up) is still
 * held. So we track each physical key independently and OR them together
 * in input_read(). */
#define MAX_RAWKEYS 128
static UBYTE key_down[MAX_RAWKEYS];

/* Map raw keycode -> action bit (0 if unmapped). */
static UWORD keycode_to_action(UWORD code)
{
    switch (code) {
        case 0x4C: return KEY_UP;     /* cursor up */
        case 0x4D: return KEY_DOWN;   /* cursor down */
        case 0x4F: return KEY_LEFT;   /* cursor left */
        case 0x4E: return KEY_RIGHT;  /* cursor right */
        case 0x11: return KEY_UP;     /* W */
        case 0x21: return KEY_DOWN;   /* S */
        case 0x20: return KEY_LEFT;   /* A */
        case 0x22: return KEY_RIGHT;  /* D */
        case 0x40: return KEY_FIRE;   /* space */
        case 0x44: return KEY_FIRE;   /* return */
        case 0x50: return KEY_FIRE;   /* F1 */
        case 0x45: return KEY_ESC;    /* escape */
    }
    return 0;
}

void input_key_down(UWORD code)
{
    if (code < MAX_RAWKEYS && keycode_to_action(code))
        key_down[code] = 1;
}

void input_key_up(UWORD code)
{
    if (code < MAX_RAWKEYS)
        key_down[code] = 0;
}

void input_reset(void)
{
    WORD i;
    for (i = 0; i < MAX_RAWKEYS; i++) key_down[i] = 0;
}

void input_read(InputState *input)
{
    UWORD joy;
    UWORD combined = 0;
    WORD i;

    /* Combine every currently-held key. Releasing one alias mustn't
     * cancel an action still held by another. */
    for (i = 0; i < MAX_RAWKEYS; i++) {
        if (key_down[i]) combined |= keycode_to_action((UWORD)i);
    }

    /* Joystick port 2 (JOY1DAT) */
    joy = custom.joy1dat;
    {
        UWORD h = joy & 3;
        UWORD v = (joy >> 8) & 3;
        if (h == 2) combined |= KEY_RIGHT;
        if (h == 1) combined |= KEY_LEFT;
        if (v == 2) combined |= KEY_DOWN;
        if (v == 1) combined |= KEY_UP;
    }

    /* Fire button: CIA-A PRA bit 7, active low (port 2) */
    if (!(ciaa.ciapra & 0x80))
        combined |= KEY_FIRE;

    /* Map to game input */
    input->dir = DIR_NONE;
    if (combined & KEY_UP)    input->dir = DIR_UP;
    if (combined & KEY_DOWN)  input->dir = DIR_DOWN;
    if (combined & KEY_LEFT)  input->dir = DIR_LEFT;
    if (combined & KEY_RIGHT) input->dir = DIR_RIGHT;

    input->start = (combined & KEY_FIRE) ? 1 : 0;
    input->quit = (combined & KEY_ESC) ? 1 : 0;
}
