/*
 * INTERACT: GEM test target for the Hatari agent API.
 *
 * A window with things to poke at, each reporting what happened on the
 * emulator host through NatFeats (Hatari --natfeats on -> /console):
 *
 *   buttons   Red / Green / Blue / Clear   -> "CLICK <name>"
 *   text      typed keys, Return submits     -> "KEY <ascii>", "TEXT <line>"
 *   canvas    mouse drag paints strokes      -> "PAINT <points> <x1>,<y1> <x2>,<y2>"
 *   arena     joystick port 1 moves a ball   -> "JOY <bits> BALL <x>,<y>"
 *             fire / right button            -> "FIRE color <n>"
 *
 * At start (and whenever the window moves) it reports where its
 * controls are in screen coordinates:  "LAYOUT <name> <x> <y> <w> <h>",
 * and the address of on_button() for debugger demos: "SYMBOL on_button <addr>".
 * "READY" ends the start-up report.  All lines start with "INTERACT ".
 */
#include <gem.h>
#include <osbind.h>
#include <stdio.h>
#include <string.h>
#include "natfeats.h"

enum { BTN_RED, BTN_GREEN, BTN_BLUE, BTN_CLEAR, NUM_BTNS };
static const char *btn_name[NUM_BTNS] = { "Red", "Green", "Blue", "Clear" };

#define MAX_SEGS  1500
#define MAX_TEXT  36
#define BALL_R    5

static short vh, ncolors, char_h, char_w;
static short win = -1;
static GRECT work, btn[NUM_BTNS], swatch, canvas, arena, textline, status;
static short paint_color = 1;
static long clicks;
static char text[MAX_TEXT + 1];
static int ball_x, ball_y, ball_color = 2;
static unsigned char last_joy;
volatile unsigned char joy1;	/* written by IKBD joystick handler */
static void (*old_joyvec)(void *);

static struct { short x1, y1, x2, y2, color; } segs[MAX_SEGS];
static int nsegs;

/* IKBD joystick handler: a0 = OS joystick buffer (header, joy0, joy1) */
__asm__(
	"	.text\n"
	"	.globl	joy_hook\n"
	"joy_hook:\n"
	/* the buffer always holds both states, whichever port changed */
	"	move.b	2(%a0),joy1\n"	/* buffer: header, joy0, joy1 */
	"	rts\n"
);
void joy_hook(void *);

static long install_joy(void)
{
	/* IKBD "set joystick event reporting" (0x14) turns the mouse off,
	 * "relative mouse mode" (0x08) right after it turns it back on:
	 * the classic way to get mouse + joystick 1 events together
	 */
	static char cmd[] = { 0x14, 0x08 };
	_KBDVECS *kv = Kbdvbase();
	old_joyvec = kv->joyvec;
	kv->joyvec = joy_hook;
	Ikbdws(sizeof(cmd) - 1, cmd);
	return 0;
}

static long remove_joy(void)
{
	/* "set joystick interrogation mode": stop joystick reports again */
	static char cmd[] = { 0x15 };
	_KBDVECS *kv = Kbdvbase();
	Ikbdws(sizeof(cmd) - 1, cmd);
	kv->joyvec = old_joyvec;
	return 0;
}

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#include <stdarg.h>
static void say(const char *fmt, ...)
{
	char buf[160];
	va_list ap;
	strcpy(buf, "INTERACT ");
	va_start(ap, fmt);
	vsnprintf(buf + 9, sizeof(buf) - 10, fmt, ap);
	va_end(ap);
	strcat(buf, "\n");
	nf_print(buf);
}

static short pen(int i)
{
	/* VDI pens: 1 black, 2 red, 3 green, 4 blue */
	return ncolors >= 4 ? i : 1;
}

/* ------------------------------------------------------------------ */
/* layout */

static void layout(void)
{
	short x = work.g_x + 4, y = work.g_y + 4, w = work.g_w - 8, h = work.g_h - 8;
	short bh = char_h + 6, bw = (w - 5 * 4) / 5;
	int i;

	for (i = 0; i < NUM_BTNS; i++)
	{
		btn[i].g_x = x + i * (bw + 4);
		btn[i].g_y = y;
		btn[i].g_w = bw;
		btn[i].g_h = bh;
	}
	swatch.g_x = x + NUM_BTNS * (bw + 4);
	swatch.g_y = y;
	swatch.g_w = w - (swatch.g_x - x);
	swatch.g_h = bh;

	status.g_x = x;
	status.g_h = char_h + 2;
	status.g_y = y + h - status.g_h;
	status.g_w = w;
	textline = status;
	textline.g_y -= char_h + 6;
	textline.g_h = char_h + 4;

	canvas.g_x = x;
	canvas.g_y = y + bh + 4;
	canvas.g_w = w * 3 / 5;
	canvas.g_h = textline.g_y - 4 - canvas.g_y;
	arena = canvas;
	arena.g_x = canvas.g_x + canvas.g_w + 4;
	arena.g_w = x + w - arena.g_x;
}

static void report_layout(void)
{
	int i;
	for (i = 0; i < NUM_BTNS; i++)
		say("LAYOUT %s %d %d %d %d", btn_name[i], btn[i].g_x, btn[i].g_y, btn[i].g_w, btn[i].g_h);
	say("LAYOUT canvas %d %d %d %d", canvas.g_x, canvas.g_y, canvas.g_w, canvas.g_h);
	say("LAYOUT arena %d %d %d %d", arena.g_x, arena.g_y, arena.g_w, arena.g_h);
	say("LAYOUT text %d %d %d %d", textline.g_x, textline.g_y, textline.g_w, textline.g_h);
}

static int inside(const GRECT *r, short x, short y)
{
	return x >= r->g_x && y >= r->g_y && x < r->g_x + r->g_w && y < r->g_y + r->g_h;
}

/* ------------------------------------------------------------------ */
/* drawing primitives (callers set clipping) */

static void fill(const GRECT *r, short color, short interior, short style)
{
	short pxy[4] = { r->g_x, r->g_y, r->g_x + r->g_w - 1, r->g_y + r->g_h - 1 };
	vsf_interior(vh, interior);
	vsf_style(vh, style);
	vsf_color(vh, color);
	vsf_perimeter(vh, 0);
	v_bar(vh, pxy);
}

static void frame(const GRECT *r)
{
	short pxy[10] = { r->g_x, r->g_y, r->g_x + r->g_w - 1, r->g_y,
	                  r->g_x + r->g_w - 1, r->g_y + r->g_h - 1,
	                  r->g_x, r->g_y + r->g_h - 1, r->g_x, r->g_y };
	vsl_color(vh, 1);
	vsl_width(vh, 1);
	v_pline(vh, 5, pxy);
}

static void text_at(short x, short y, const char *s)
{
	short d;
	vst_alignment(vh, 0, 5, &d, &d);
	vst_color(vh, 1);
	v_gtext(vh, x, y, s);
}

static void draw_button(int i)
{
	short tw = strlen(btn_name[i]) * char_w;
	fill(&btn[i], 0, FIS_SOLID, 0);
	frame(&btn[i]);
	text_at(btn[i].g_x + (btn[i].g_w - tw) / 2, btn[i].g_y + 3, btn_name[i]);
}

static void draw_swatch(void)
{
	fill(&swatch, pen(paint_color), FIS_SOLID, 0);
	frame(&swatch);
}

static void draw_segments(void)
{
	int i;
	vsl_width(vh, 1);
	for (i = 0; i < nsegs; i++)
	{
		short pxy[4] = { segs[i].x1, segs[i].y1, segs[i].x2, segs[i].y2 };
		vsl_color(vh, segs[i].color);
		v_pline(vh, 2, pxy);
	}
}

static void draw_canvas(void)
{
	fill(&canvas, 0, FIS_SOLID, 0);
	frame(&canvas);
	draw_segments();
}

static void draw_ball(int erase)
{
	vsf_interior(vh, FIS_SOLID);
	vsf_color(vh, erase ? 0 : pen(ball_color));
	vsf_perimeter(vh, 0);
	v_circle(vh, arena.g_x + ball_x, arena.g_y + ball_y, BALL_R);
}

static void draw_arena(void)
{
	fill(&arena, 1, FIS_PATTERN, 1);	/* light dotted background */
	fill(&(GRECT){ arena.g_x + 1, arena.g_y + 1, arena.g_w - 2, arena.g_h - 2 }, 0, FIS_SOLID, 0);
	frame(&arena);
	text_at(arena.g_x + 3, arena.g_y + 2, "Joystick");
	draw_ball(0);
}

static void draw_textline(void)
{
	char buf[MAX_TEXT + 8];
	snprintf(buf, sizeof(buf), "Text: %s_", text);
	fill(&textline, 0, FIS_SOLID, 0);
	frame(&textline);
	text_at(textline.g_x + 3, textline.g_y + 2, buf);
}

static void draw_status(short mx, short my)
{
	char buf[64];
	snprintf(buf, sizeof(buf), "Clicks:%ld Segs:%d Joy:%02x Mouse:%d,%d",
	         clicks, nsegs, last_joy, mx, my);
	fill(&status, 0, FIS_SOLID, 0);
	text_at(status.g_x, status.g_y + 1, buf);
}

static void draw_all(void)
{
	int i;
	fill(&work, 0, FIS_SOLID, 0);
	for (i = 0; i < NUM_BTNS; i++)
		draw_button(i);
	draw_swatch();
	draw_canvas();
	draw_arena();
	draw_textline();
	draw_status(-1, -1);
}

/* run 'fn' clipped to every visible window rectangle within 'area' */
static void for_visible(const GRECT *area, void (*fn)(void))
{
	GRECT r;
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, NULL);
	wind_get_grect(win, WF_FIRSTXYWH, &r);
	while (r.g_w && r.g_h)
	{
		GRECT c = *area;
		if (rc_intersect(&r, &c))
		{
			short pxy[4] = { c.g_x, c.g_y, c.g_x + c.g_w - 1, c.g_y + c.g_h - 1 };
			vs_clip(vh, 1, pxy);
			fn();
		}
		wind_get_grect(win, WF_NEXTXYWH, &r);
	}
	vs_clip(vh, 0, NULL);
	graf_mouse(M_ON, NULL);
	wind_update(END_UPDATE);
}

static short st_mx, st_my;
static void fn_all(void) { draw_all(); }
static void fn_swatch(void) { draw_swatch(); }
static void fn_canvas(void) { draw_canvas(); }
static void fn_text(void) { draw_textline(); }
static void fn_status(void) { draw_status(st_mx, st_my); }
static void fn_ball_erase(void) { draw_ball(1); }
static void fn_ball(void) { draw_ball(0); }

/* ------------------------------------------------------------------ */
/* interaction */

/* separate, non-inlined function so it's easy to break on */
__attribute__((noinline)) void on_button(int id)
{
	clicks++;
	say("CLICK %s", btn_name[id]);
	switch (id)
	{
	case BTN_RED:   paint_color = 2; break;
	case BTN_GREEN: paint_color = 3; break;
	case BTN_BLUE:  paint_color = 4; break;
	case BTN_CLEAR:
		nsegs = 0;
		paint_color = 1;
		for_visible(&canvas, fn_canvas);
		break;
	}
	say("COLOR %d", paint_color);
	for_visible(&swatch, fn_swatch);
}

/* track mouse while button held, drawing line segments */
static void paint(short mx, short my)
{
	short x = mx, y = my, nx, ny, mb, ks, points = 1;
	short sx = mx, sy = my;
	short clip[4] = { canvas.g_x + 1, canvas.g_y + 1,
	                  canvas.g_x + canvas.g_w - 2, canvas.g_y + canvas.g_h - 2 };

	wind_update(BEG_MCTRL);
	graf_mouse(M_OFF, NULL);
	vs_clip(vh, 1, clip);
	vsl_color(vh, pen(paint_color));
	vsl_width(vh, 1);
	do
	{
		graf_mkstate(&nx, &ny, &mb, &ks);
		if ((nx != x || ny != y) && nsegs < MAX_SEGS)
		{
			short pxy[4] = { x, y, nx, ny };
			v_pline(vh, 2, pxy);
			segs[nsegs].x1 = x; segs[nsegs].y1 = y;
			segs[nsegs].x2 = nx; segs[nsegs].y2 = ny;
			segs[nsegs].color = pen(paint_color);
			nsegs++;
			points++;
			x = nx;
			y = ny;
		}
	} while (mb & 1);
	vs_clip(vh, 0, NULL);
	graf_mouse(M_ON, NULL);
	wind_update(END_MCTRL);
	say("PAINT %d %d,%d %d,%d", points, sx, sy, x, y);
}

static void key(short k)
{
	char c = k & 0xff;
	size_t len = strlen(text);

	if (c == '\r' || c == '\n')
	{
		say("TEXT %s", text);
		text[0] = '\0';
	}
	else if (c == '\b')
	{
		if (len)
			text[len - 1] = '\0';
	}
	else if (c >= ' ' && c < 0x7f && len < MAX_TEXT)
	{
		text[len] = c;
		text[len + 1] = '\0';
		say("KEY %c", c);
	}
	for_visible(&textline, fn_text);
}

/* joystick fire: with the mouse enabled, the ST reports joystick 1's
 * fire button as the right mouse button (same hardware line)
 */
static void fire(void)
{
	GRECT a = { arena.g_x + 1, arena.g_y + 1, arena.g_w - 2, arena.g_h - 2 };
	ball_color = ball_color % (ncolors >= 8 ? 7 : 1) + 1;
	for_visible(&a, fn_ball);
	say("FIRE color %d", ball_color);
}

static void move_ball(void)
{
	unsigned char j = joy1;
	int nx = ball_x, ny = ball_y;

	if (j & 0x01) ny -= 3;
	if (j & 0x02) ny += 3;
	if (j & 0x04) nx -= 3;
	if (j & 0x08) nx += 3;
	if (nx < BALL_R + 2) nx = BALL_R + 2;
	if (ny < BALL_R + char_h + 4) ny = BALL_R + char_h + 4;
	if (nx > arena.g_w - BALL_R - 3) nx = arena.g_w - BALL_R - 3;
	if (ny > arena.g_h - BALL_R - 3) ny = arena.g_h - BALL_R - 3;
	if ((j & 0x80) && !(last_joy & 0x80))
		fire();

	if (nx != ball_x || ny != ball_y || j != last_joy)
	{
		GRECT a = { arena.g_x + 1, arena.g_y + 1, arena.g_w - 2, arena.g_h - 2 };
		for_visible(&a, fn_ball_erase);
		ball_x = nx;
		ball_y = ny;
		for_visible(&a, fn_ball);
	}
	if (j != last_joy)
	{
		say("JOY %02x BALL %d,%d", j, ball_x, ball_y);
		last_joy = j;
	}
}

static void open_vdi(void)
{
	short work_in[11] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2 };
	short work_out[57];
	short wbox, hbox;

	vh = graf_handle(&char_w, &char_h, &wbox, &hbox);
	v_opnvwk(work_in, &vh, work_out);
	ncolors = work_out[13];
	say("SCREEN %d %d %d", work_out[0] + 1, work_out[1] + 1, ncolors);
}

int main(void)
{
	short msg[8], ev, mx, my, mb, ks, k, nclicks, last_mx = -1, last_my = -1;
	GRECT desk;
	int quit = 0;

	if (appl_init() < 0)
		return 1;
	nf_init();
	open_vdi();
	Supexec(install_joy);

	wind_get_grect(0, WF_WORKXYWH, &desk);
	win = wind_create_grect(NAME | CLOSER | MOVER, &desk);
	if (win < 0)
	{
		form_alert(1, "[3][No more windows!][ OK ]");
		return 1;
	}
	wind_set_str(win, WF_NAME, " Interact - agent API test target ");
	graf_mouse(ARROW, NULL);
	wind_open_grect(win, &desk);
	wind_get_grect(win, WF_WORKXYWH, &work);
	layout();
	ball_x = arena.g_w / 2;
	ball_y = arena.g_h / 2;
	report_layout();
	say("SYMBOL on_button 0x%06lx", (unsigned long)on_button);
	say("SYMBOL joy_hook 0x%06lx", (unsigned long)joy_hook);
	say("READY");

	while (!quit)
	{
		/* clicks 0x101: negated state, i.e. any of buttons 1|2 down */
		ev = evnt_multi(MU_MESAG | MU_KEYBD | MU_BUTTON | MU_TIMER,
		                0x101, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		                msg, 20, &mx, &my, &mb, &ks, &k, &nclicks);
		if (ev & MU_MESAG)
		{
			GRECT r = { msg[4], msg[5], msg[6], msg[7] };
			switch (msg[0])
			{
			case WM_REDRAW:
				for_visible(&r, fn_all);
				break;
			case WM_TOPPED:
				wind_set(win, WF_TOP, win, 0, 0, 0);
				break;
			case WM_CLOSED:
				quit = 1;
				break;
			case WM_MOVED:
			{
				short dx, dy;
				int i;
				wind_set_grect(win, WF_CURRXYWH, &r);
				dx = -work.g_x;
				dy = -work.g_y;
				wind_get_grect(win, WF_WORKXYWH, &work);
				dx += work.g_x;
				dy += work.g_y;
				for (i = 0; i < nsegs; i++)
				{
					segs[i].x1 += dx; segs[i].x2 += dx;
					segs[i].y1 += dy; segs[i].y2 += dy;
				}
				layout();
				report_layout();
				break;
			}
			}
		}
		if ((ev & MU_BUTTON) && (mb & 2))
		{
			fire();
			/* wait for release, so one press is one shot */
			do graf_mkstate(&mx, &my, &mb, &ks); while (mb & 2);
		}
		else if (ev & MU_BUTTON)
		{
			int i, hit = 0;
			for (i = 0; i < NUM_BTNS; i++)
			{
				if (inside(&btn[i], mx, my))
				{
					on_button(i);
					hit = 1;
				}
			}
			if (!hit && inside(&canvas, mx, my))
				paint(mx, my);
		}
		if (ev & MU_KEYBD)
		{
			if ((k & 0xff) == 0x1b)
				quit = 1;
			else
				key(k);
		}
		if (ev & MU_TIMER)
		{
			move_ball();
			graf_mkstate(&mx, &my, &mb, &ks);
			if (mx != last_mx || my != last_my)
			{
				st_mx = last_mx = mx;
				st_my = last_my = my;
				for_visible(&status, fn_status);
			}
		}
	}

	say("EXIT clicks=%ld segs=%d", clicks, nsegs);
	Supexec(remove_joy);
	wind_close(win);
	wind_delete(win);
	v_clsvwk(vh);
	appl_exit();
	return 0;
}
