/*
 * The little of the 3DO compatibility layer (amiga3do.h) that game.c uses
 * on AmigaOS 4: the pad bits. main_os4.c maps the keyboard onto them.
 */
#ifndef AMIGA3DO_H
#define AMIGA3DO_H

#define PAD_UP     0x0001
#define PAD_DOWN   0x0002
#define PAD_LEFT   0x0004
#define PAD_RIGHT  0x0008
#define PAD_A      0x0010
#define PAD_B      0x0020
#define PAD_C      0x0040
#define PAD_P      0x0080   /* play/pause (start) */
#define PAD_X      0x0100   /* stop: quits */
#define PAD_L      0x0200
#define PAD_R      0x0400

#endif
