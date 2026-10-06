/* Frank the Frog - ST additions to the music player (st_modplay.c) */
#ifndef ST_MODPLAY_H
#define ST_MODPLAY_H

enum { SFX_HOP, SFX_SPLAT, SFX_SPLASH, SFX_HOME, SFX_LEVELUP, SFX_GAMEOVER };
void st_sfx(int kind, int frames);	/* take over YM channel C */
void modplay_toggle(void);

#endif
