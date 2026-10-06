/*
 * Jump Quest - Atari ST input (replaces the Amiga input.c): the same
 * keys (cursor keys / WASD, Space / Alt / Z / X jump, Return start, Esc)
 * and joystick port 1 with fire = jump, from the shared IKBD handler.
 */
#include "game.h"
#include "st_ikbd.h"

void input_init(void) {
    ikbd_init(IKBD_JOYSTICK);
}

void input_exit(void) {
    ikbd_exit();
}

UWORD input_read(void) {
    volatile UBYTE *k = ikbd_keys;
    UBYTE j = ikbd_joy1();
    UWORD result = 0;

    if (k[SC_LEFT]  || k[SC_A] || (j & 0x04)) result |= INP_LEFT;
    if (k[SC_RIGHT] || k[SC_D] || (j & 0x08)) result |= INP_RIGHT;
    if (k[SC_UP]    || k[SC_W] || (j & 0x01)) result |= INP_UP;
    if (k[SC_DOWN]  || k[SC_S] || (j & 0x02)) result |= INP_DOWN;
    if (k[SC_SPACE] || k[SC_ALT] || k[SC_Z] || k[SC_X] || (j & 0x80))
        result |= INP_JUMP;
    if (k[SC_RETURN]) result |= INP_START;
    return result;
}

BOOL input_check_esc(void) {
    return ikbd_key_hit(SC_ESC) ? TRUE : FALSE;
}
