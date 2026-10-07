// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * Bullion Dash - Input abstraction
 * Reads joystick port 2 + keyboard IDCMP.
 * Joystick directions are HELD state (not edge-detected).
 * Fire and keys are edge-detected.
 */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <hardware/cia.h>
#include <hardware/custom.h>
#include <proto/exec.h>
#include <proto/intuition.h>

#include <string.h>

#include "input.h"

extern struct Custom custom;

/* Maximum raw keycode we track */
#define MAX_KEYS 128

static struct Window *input_win = NULL;

/* Current and previous frame state */
static int joy_dx_val = 0;
static int joy_dy_val = 0;
static int joy_fire_val = 0;
static int prev_fire = 0;

/* Persistent held-state table: set on press, cleared on release. */
static UBYTE keys_down[MAX_KEYS];

/* Per-frame snapshots derived from keys_down. Used for edge detection. */
static UBYTE keys_curr[MAX_KEYS];
static UBYTE keys_prev[MAX_KEYS];
static int any_key_pressed = 0;

void input_init(struct Window *win)
{
    input_win = win;
    joy_dx_val = 0;
    joy_dy_val = 0;
    joy_fire_val = 0;
    prev_fire = 0;
    any_key_pressed = 0;
    memset(keys_down, 0, sizeof(keys_down));
    memset(keys_curr, 0, sizeof(keys_curr));
    memset(keys_prev, 0, sizeof(keys_prev));
}

void input_update(void)
{
    volatile UWORD joy;
    UBYTE ciaa_pra;
    int joy_dx_stick, joy_dy_stick;
    int joy_fire_stick;

    /* Save previous frame state */
    prev_fire = joy_fire_val;
    memcpy(keys_prev, keys_curr, sizeof(keys_prev));

    /* Read joystick port 2 (JOY1DAT) - held state */
    joy = custom.joy1dat;

    joy_dx_stick = 0;
    joy_dy_stick = 0;

    /* Decode joystick directions */
    if ((joy >> 1) & 1)                  joy_dx_stick = 1;   /* right */
    if ((joy >> 9) & 1)                  joy_dx_stick = -1;  /* left */
    if (((joy >> 1) ^ joy) & 1)          joy_dy_stick = 1;   /* down */
    if (((joy >> 9) ^ (joy >> 8)) & 1)   joy_dy_stick = -1;  /* up */

    /* Fire button: CIA-A PRA bit 7, active low */
    ciaa_pra = *((volatile UBYTE *)0xBFE001);
    joy_fire_stick = (ciaa_pra & 0x80) ? 0 : 1;

    /* Process keyboard IDCMP messages - update persistent keys_down */
    if (input_win) {
        struct IntuiMessage *imsg;
        while ((imsg = (struct IntuiMessage *)GetMsg(input_win->UserPort))) {
            ULONG cl = imsg->Class;
            UWORD code = imsg->Code;
            ReplyMsg((struct Message *)imsg);

            if (cl == IDCMP_RAWKEY) {
                int keycode = code & 0x7F;
                int released = code & 0x80;

                if (keycode < MAX_KEYS) {
                    keys_down[keycode] = released ? 0 : 1;
                }
            }
        }
    }

    /* Snapshot current frame from persistent keys_down; derive
     * held direction / fire state from it too, so keys held across
     * frames without new IDCMP events are still reported. */
    memcpy(keys_curr, keys_down, sizeof(keys_curr));
    any_key_pressed = 0;
    {
        int i;
        for (i = 0; i < MAX_KEYS; i++) {
            if (keys_down[i]) { any_key_pressed = 1; break; }
        }
    }

    joy_dx_val = joy_dx_stick;
    joy_dy_val = joy_dy_stick;
    joy_fire_val = joy_fire_stick;

    if (keys_down[KEY_UP])    joy_dy_val = -1;
    if (keys_down[KEY_DOWN])  joy_dy_val = 1;
    if (keys_down[KEY_LEFT])  joy_dx_val = -1;
    if (keys_down[KEY_RIGHT]) joy_dx_val = 1;
    if (keys_down[KEY_SPACE] || keys_down[KEY_RETURN])
        joy_fire_val = 1;
}

int input_dx(void)
{
    return joy_dx_val;
}

int input_dy(void)
{
    return joy_dy_val;
}

int input_fire(void)
{
    /* Edge-detected: true only on the frame fire was pressed */
    return (joy_fire_val && !prev_fire);
}

int input_fire_held(void)
{
    return joy_fire_val;
}

int input_key(int rawkey)
{
    /* Edge-detected: true only on the frame the key was pressed */
    if (rawkey < 0 || rawkey >= MAX_KEYS) return 0;
    return (keys_curr[rawkey] && !keys_prev[rawkey]);
}

int input_any_key(void)
{
    return any_key_pressed;
}
