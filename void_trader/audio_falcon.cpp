/*
 * Atari Falcon port: the original modplay.c (Paula MOD player and track
 * generator) runs unchanged on the Paula emulation in st_port/paula:
 * it is compiled as C++ so its custom.dmacon writes reach the emulated
 * DMA control, with its entry points renamed mp_*_raw (see Makefile).
 *
 * The player's tick runs from the VBL interrupt at its design rate of
 * 50 Hz (on the Amiga it was called once per rendered frame), so music
 * tempo does not depend on the frame rate.  Calls from the game are
 * wrapped with interrupts masked, since the tick touches the same state.
 */
#include "modplay.h"
#include "paula.h"

extern "C" {
int  mp_init_raw(void);
void mp_start_raw(void);
void mp_start_song_raw(int id);
void mp_stop_raw(void);
void mp_cleanup_raw(void);
void mp_tick_raw(void);
void mp_sfx_raw(BYTE *data, UWORD len_words, UWORD period, UWORD volume);
}

int modplay_init(void)
{
    if (mp_init_raw())
        return 1;
    return paula_init(mp_tick_raw, 50);
}

void modplay_start(void)
{
    UWORD sr = paula_lock();
    mp_start_raw();
    paula_unlock(sr);
}

void modplay_start_song(int id)
{
    UWORD sr = paula_lock();
    mp_start_song_raw(id);
    paula_unlock(sr);
}

void modplay_stop(void)
{
    UWORD sr = paula_lock();
    mp_stop_raw();
    paula_unlock(sr);
}

void modplay_cleanup(void)
{
    paula_exit();
    mp_cleanup_raw();
}

/* driven from the VBL interrupt instead */
void modplay_tick(void) { }

void modplay_sfx(BYTE *data, UWORD len_words, UWORD period, UWORD volume)
{
    UWORD sr = paula_lock();
    mp_sfx_raw(data, len_words, period, volume);
    paula_unlock(sr);
}
