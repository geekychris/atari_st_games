/*
 * The machine layer of the 3DO-derived ports (planet_chomp, rolling_steel,
 * spectral_keep) on the Atari Falcon030: what their amiga68k.c and
 * paula68k.c provide on the Amiga (see amiga68k.h, paula.h).
 *
 *   frame     the game draws into fb, 320 x fb_h of 15-bit RGB (the 3DO's
 *             0RRRRRGGGGGBBBBB); sys_present converts it to the Falcon's
 *             16 bit true colour (RRRRRGGGGGGBBBBB), two pixels per long,
 *             into the back screen of ../falcon_port/fgfx (320 x 240 VGA,
 *             320 x 200 RGB: rows are dropped evenly when fb_h is larger).
 *             A game that draws Falcon pixels itself sets sys_rgb565: fb
 *             is then the back screen itself (it moves at every
 *             sys_present), and nothing is converted or copied
 *   keyboard  the IKBD handler's key events are queued as IDCMP_RAWKEY
 *             messages with Amiga key codes on the game window's port,
 *             so the games' Intuition loops read them unchanged
 *   joystick  joystick 1 as pad bits
 *   timer     now_us from the 200 Hz system timer
 *   data      data\<name> beside the program, in one block
 *   sound     the Paula API on the ST layer's Paula emulation
 *
 * The games' main() is built as game_main(); main() here enters
 * supervisor mode once around it (the IKBD handler, the screen and the
 * DMA sound need it).
 */
#include <osbind.h>
#include <string.h>
#include <ctype.h>
#include "fgfx.h"
#include "st_ikbd.h"
#include "natfeats.h"
#include "amiga3do_compat.h"
#include "amiga68k.h"
#include "paula.h"
#include "bridge_client.h"

/* mintlib's stack for the program (the 68k builds ask libnix for
 * 64 KB through __stack: the voxel renderer and the room composer
 * recurse); mintlib's default is far smaller */
long _stksize = 256L * 1024;

/* ---- the frame ---- */

UWORD *fb;
int fb_h;
int sys_rgb565;				/* set before sys_open: fb is the screen */
static int opened, direct;
static WORD src_row[256];		/* screen row -> frame row */

/* 15-bit 0RRRRRGGGGGBBBBB -> 16-bit RRRRRGGGGGGBBBBB, n longs (2 pixels
 * each): R and G one bit up, B stays, green's new low bit 0 */
static void conv(const ULONG *s, ULONG *d, int n)
{
	__asm__ volatile (
		"	move.l	#0xffc0ffc0,%%d3\n"
		"	move.l	#0x001f001f,%%d4\n"
		"	subq.w	#1,%2\n"
		"1:	move.l	(%0)+,%%d0\n"
		"	move.l	%%d0,%%d1\n"
		"	add.l	%%d0,%%d0\n"
		"	and.l	%%d3,%%d0\n"
		"	and.l	%%d4,%%d1\n"
		"	or.l	%%d1,%%d0\n"
		"	move.l	%%d0,(%1)+\n"
		"	dbra	%2,1b\n"
		: "+a" (s), "+a" (d), "+d" (n)
		:
		: "d0", "d1", "d3", "d4", "cc", "memory");
}

int sys_open(const char *title, int h, int scale, int mode)
{
	int y, sh;
	(void)title; (void)scale; (void)mode;
	if (opened)
		return 1;
	fb_h = h;
	if (fgfx_init())
		return 0;
	sh = fgfx_height();
	/* a game drawing Falcon pixels draws straight into the back screen
	 * when its frame fits (VGA: 240 lines); otherwise into its own
	 * frame, converted by sys_present */
	direct = sys_rgb565 && h <= sh;
	fb = direct ? fgfx_back()->base : (UWORD *)calloc((long)FB_W * h, 2);
	if (!fb)
		return 0;
	for (y = 0; y < sh; y++)
		src_row[y] = (WORD)((long)y * h / sh);
	ikbd_init(IKBD_JOYSTICK);
	opened = 1;
	return 1;
}

void sys_close(void)
{
	if (!opened)
		return;
	ikbd_exit();
	fgfx_exit();
	opened = 0;
}

void sys_present(void)
{
	UWORD *d = fgfx_back()->base;
	int y, sh = fgfx_height();
	if (direct) {
		/* the frame was the back screen: show it, draw into the next */
		fgfx_swap();
		fb = fgfx_back()->base;
		return;
	}
	for (y = 0; y < sh; y++, d += FB_W) {
		const UWORD *s = fb + (long)src_row[y] * FB_W;
		if (sys_rgb565)
			memcpy(d, s, FB_W * 2);
		else
			conv((const ULONG *)s, (ULONG *)d, FB_W / 2);
	}
	fgfx_swap();
}

/* F / F10 (the Amiga's window <-> screen switch) toggles half
 * resolution for a renderer that supports it (it sets sys_halfres to 0
 * or 1, see rolling_steel's glcels_soft.c); otherwise nothing to switch */
int sys_halfres = -1;

int sys_toggle(void)
{
	if (sys_halfres >= 0)
		sys_halfres ^= 1;
	return 1;
}

const char *sys_mode_name(void)
{
	return sys_halfres == 1 ? "Falcon true colour, 3D at half resolution"
				: "Falcon true colour";
}

/* the CPU from the '_CPU' cookie: 30 for a 68030, 60 for a 68060 */
int sys_cpu(void)
{
	long *jar = *(long **)0x5a0;
	for (; jar && jar[0]; jar += 2)
		if ((ULONG)jar[0] == 0x5f435055)	/* '_CPU' */
			return (int)jar[1];
	return 0;
}

/* ---- the window's IDCMP port: key events as RAWKEY messages ---- */

static struct MsgPort port;
static struct Window window = { &port };
static struct IntuiMessage queue[64];
static int q_head, q_tail;
static UBYTE was_down[128];

/* ST scancode -> Amiga raw key code (0xff: none) */
static UBYTE amiga_code(int sc)
{
	static const UBYTE kp[12] = {		/* ST 0x67..0x72 */
		0x3d, 0x3e, 0x3f, 0x2d, 0x2e, 0x2f, 0x1d, 0x1e, 0x1f, 0x0f, 0x3c, 0x43 };
	if (sc >= 0x02 && sc <= 0x0d) return (UBYTE)(sc - 1);		/* 1..= */
	if (sc >= 0x10 && sc <= 0x1b) return (UBYTE)sc;			/* q..] */
	if (sc >= 0x1e && sc <= 0x28) return (UBYTE)(sc + 2);		/* a..' */
	if (sc >= 0x2c && sc <= 0x35) return (UBYTE)(sc + 5);		/* z../ */
	if (sc >= 0x3b && sc <= 0x44) return (UBYTE)(sc + 0x15);	/* F1..F10 */
	if (sc >= 0x67 && sc <= 0x72) return kp[sc - 0x67];
	switch (sc) {
	case 0x01: return 0x45;	/* Esc */
	case 0x0e: return 0x41;	/* Backspace */
	case 0x0f: return 0x42;	/* Tab */
	case 0x1c: return 0x44;	/* Return */
	case 0x1d: return 0x63;	/* Control */
	case 0x29: return 0x00;	/* ` */
	case 0x2a: return 0x60;	/* left Shift */
	case 0x2b: return 0x0d;	/* \ */
	case 0x36: return 0x61;	/* right Shift */
	case 0x38: return 0x64;	/* Alt */
	case 0x39: return 0x40;	/* Space */
	case 0x48: return 0x4c;	/* up */
	case 0x50: return 0x4d;	/* down */
	case 0x4b: return 0x4f;	/* left */
	case 0x4d: return 0x4e;	/* right */
	case 0x4a: return 0x4a;	/* keypad - */
	case 0x4e: return 0x5e;	/* keypad + */
	case 0x53: return 0x46;	/* Delete */
	case 0x62: return 0x5f;	/* Help */
	}
	return 0xff;
}

static void post(UWORD code)
{
	int next = (q_tail + 1) & 63;
	if (next == q_head)
		return;
	queue[q_tail].Class = IDCMP_RAWKEY;
	queue[q_tail].Code = code;
	queue[q_tail].Qualifier = 0;
	q_tail = next;
}

/* new key events since the last look; a press and release between two
 * looks still gives both */
static void collect(void)
{
	int sc;
	for (sc = 1; sc < 0x73; sc++) {
		UBYTE a = amiga_code(sc), down;
		int hits;
		if (a == 0xff)
			continue;
		hits = ikbd_key_hit(sc);
		down = ikbd_keys[sc];
		if (hits && (was_down[sc] || !down)) {
			if (was_down[sc])
				post(a | IECODE_UP_PREFIX);
			post(a);
			was_down[sc] = 1;
		} else if (down && !was_down[sc]) {
			post(a);
			was_down[sc] = 1;
		}
		if (!down && was_down[sc]) {
			post(a | IECODE_UP_PREFIX);
			was_down[sc] = 0;
		}
	}
}

struct Message *GetMsg(struct MsgPort *p)
{
	struct IntuiMessage *m;
	(void)p;
	if (q_head == q_tail)
		collect();
	if (q_head == q_tail)
		return NULL;
	m = &queue[q_head];
	q_head = (q_head + 1) & 63;
	return &m->ExecMessage;
}

void ReplyMsg(struct Message *msg) { (void)msg; }
struct Window *sys_window(void) { return &window; }
void WaitTOF(void) { Vsync(); }

unsigned long sys_joystick(void)
{
	UBYTE j = ikbd_joy1();
	return (j & 0x0f) | ((j & 0x80) ? 0x10 : 0);
}

/* ---- time ---- */

int timer_open(void) { return 1; }
void timer_close(void) { }

unsigned long long now_us(void)
{
	return (unsigned long long)*(volatile ULONG *)0x4ba * 5000;	/* _hz_200 */
}

/* ---- data files ---- */

void *sys_load(const char *name, long *size, int chip)
{
	char path[24], *p;
	const char *dot = strrchr(name, '.');
	long fh, len;
	void *buf;
	int n;
	(void)chip;
	/* DATA\NAME.EXT, the name shortened to GEMDOS 8.3 (as f3do.mk copies
	 * the files): "gatehouse.raw" -> DATA\GATEHOUS.RAW */
	strcpy(path, "DATA\\");
	p = path + 5;
	for (n = 0; name[n] && name + n != dot && n < 8; n++)
		*p++ = toupper((unsigned char)name[n]);
	if (dot)
		for (*p++ = '.', n = 1; dot[n] && n <= 3; n++)
			*p++ = toupper((unsigned char)dot[n]);
	*p = 0;
	fh = Fopen(path, 0);
	if (fh < 0)
		return NULL;
	len = Fseek(0, (short)fh, 2);
	Fseek(0, (short)fh, 0);
	buf = len > 0 ? malloc(len) : NULL;
	if (buf && Fread((short)fh, len, buf) != len) {
		free(buf);
		buf = NULL;
	}
	Fclose((short)fh);
	if (buf && size)
		*size = len;
	return buf;
}

void sys_free(void *p) { free(p); }

/* ---- fonts ---- */

struct TextFont *OpenFont(struct TextAttr *ta)
{
	static struct TextFont tf;
	static ULONG loc[257];
	register void *fonts __asm__("a1");
	const UBYTE *f8;
	int i, first;
	(void)ta;
	if (tf.tf_CharData)
		return &tf;
	/* the Line-A font headers: [1] is the 8x8 font, one strike with
	 * every character 8 pixels wide */
	__asm__ volatile (".dc.w 0xa000" : "=r" (fonts) : : "d0", "d1", "d2", "a0", "a2", "memory");
	f8 = ((const UBYTE **)fonts)[1];
	first = *(const UWORD *)(f8 + 36);
	tf.tf_YSize = 8;
	tf.tf_XSize = 8;
	tf.tf_Baseline = 6;
	tf.tf_Modulo = *(const UWORD *)(f8 + 80);
	tf.tf_LoChar = (UBYTE)first;
	tf.tf_HiChar = 255;
	for (i = 0; i <= 256 - first; i++)
		loc[i] = ((ULONG)i * 8 << 16) | 8;
	tf.tf_CharLoc = loc;
	tf.tf_CharData = *(APTR *)(f8 + 76);
	return &tf;
}

/* ---- sound ---- */

static void (*tick_fn)(void);
static int sound_open;

int paula_open(void)
{
	if (!sound_open)
		sound_open = paula_init(NULL, 50) == 0;
	return sound_open;
}

void paula_close(void)
{
	if (sound_open)
		paula_exit();
	sound_open = 0;
}

void paula_set_tick(void (*tick)(void)) { tick_fn = tick; }

void paula_tick(void)
{
	if (tick_fn && sound_open) {
		UWORD sr = paula_lock();	/* the mixer runs in the VBL */
		tick_fn();
		paula_unlock(sr);
	}
}

/* ---- main ---- */

#undef main
int game_main(int argc, char **argv);

int main(int argc, char **argv)
{
	long old_ssp;
	int rc;
	nf_init();
	ab_init(F3DO_NAME);	/* log prefix, unless the game sets its own */
	old_ssp = Super(0L);
	rc = game_main(argc, argv);
	paula_close();
	sys_close();
	Super((void *)old_ssp);
	return rc;
}
