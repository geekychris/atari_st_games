/* Planet Chomp sounds (from Sfx.cs): synthesised at startup, played on Paula (3DO) / AHI (OS4) */
#ifndef SFX_H
#define SFX_H
enum { SFX_WAKA_A, SFX_WAKA_B, SFX_KEY, SFX_EAT, SFX_DEATH, SFX_START, SFX_CLEAR, SFX_EXTRA,
       SFX_SIREN, SFX_FRIGHT, SFX_COUNT };
int  sfx_init(void);
void sfx_play(int id);
void sfx_waka(void);
void sfx_ambience(int which);      /* 0 silent, 1 siren, 2 frightened warble */
void sfx_update(void);             /* once a frame (AHI: loops, request bookkeeping) */
void sfx_exit(void);
int  sfx_available(void);
#endif
