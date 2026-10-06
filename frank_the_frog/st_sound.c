/*
 * Frank the Frog - Atari ST sound effects (replaces the Amiga sound.c,
 * which builds Paula samples): synthesised on YM channel C through the
 * music player, which pauses its drums meanwhile (as on the Amiga).
 */
#include "sound.h"
#include "st_modplay.h"

int sound_init(void) { return 0; }
void sound_cleanup(void) { }
void sound_hop(void)      { st_sfx(SFX_HOP, 5); }
void sound_splat(void)    { st_sfx(SFX_SPLAT, 14); }
void sound_splash(void)   { st_sfx(SFX_SPLASH, 24); }
void sound_home(void)     { st_sfx(SFX_HOME, 12); }
void sound_levelup(void)  { st_sfx(SFX_LEVELUP, 20); }
void sound_gameover(void) { st_sfx(SFX_GAMEOVER, 30); }
