/*
 * amiga68k.c: what the classic (68k, AmigaOS 3.x) builds of the 3DO ports
 * need from the machine. The same file is in planet_chomp, rolling_steel
 * and spectral_keep.
 *
 * The game draws into a 320 x H frame of 15-bit RGB (0RRRRRGGGGGBBBBB,
 * the 3DO's own pixel format), which sys_present() puts on screen:
 *   RTG window   a window on an RTG Workbench, scaled up (Picasso96)
 *   RTG screen   a screen of its own on the RTG card
 *   AGA screen   320 x 256 x 8 planes: a 256-colour palette fitted to
 *                the frame, chunky to planar, double buffered
 */
#ifndef AMIGA68K_H
#define AMIGA68K_H

#include <exec/types.h>
#include <intuition/intuition.h>

#define FB_W 320
extern UWORD *fb;                   /* the frame: FB_W x fb_h, rows top to bottom */
extern int fb_h;

#ifdef __MINT__
/* Atari Falcon port (../falcon_3do/sys3do.c): -1 until the renderer can
 * draw the 3D at half resolution, then 0 or 1 (F / F10 toggle it); the
 * '_CPU' cookie (30, 60) */
extern int sys_halfres;
int sys_cpu(void);
#endif

enum { SYS_AUTO, SYS_WINDOW, SYS_RTG_SCREEN, SYS_AGA };

int   sys_open(const char *title, int h, int scale, int mode);   /* 0 on failure */
void  sys_close(void);
void  sys_present(void);
int   sys_toggle(void);             /* RTG window <-> RTG screen */
struct Window *sys_window(void);
const char *sys_mode_name(void);

/* IDCMP every game window asks for, and the keys that switch the display */
#define SYS_IDCMP (IDCMP_RAWKEY | IDCMP_CLOSEWINDOW | IDCMP_INACTIVEWINDOW)
#define SYS_KEY_F10 0x59
#define SYS_KEY_F   0x23

/* the joystick in port 1 as pad bits: 1 up, 2 down, 4 left, 8 right,
 * 0x10 fire (the 3DO ports' A). FS-UAE puts it on the cursor keys by
 * default, so this is also what makes the arrows work there. */
unsigned long sys_joystick(void);

int   timer_open(void);
void  timer_close(void);
unsigned long long now_us(void);

/* data/<name> beside the program, in one AllocVec block (chip RAM if asked:
 * sound for Paula) */
void *sys_load(const char *name, long *size, int chip);
void  sys_free(void *p);

#endif
