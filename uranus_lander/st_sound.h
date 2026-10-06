/* Uranus Lander - ST-only sound additions (see st_sound.c) */
#ifndef ST_SOUND_H
#define ST_SOUND_H

void sound_tick(void);
void music_enable(int on);
int music_enabled(void);

/* ST-only input setup (see st_input.c) */
void input_init(void);
void input_exit(void);

#endif
