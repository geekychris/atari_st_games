/*
 * GEM VDI showcase for the Atari ST.
 *
 * Opens a GEM window split into six panels, each demonstrating part of
 * the VDI (the Atari's "GDI"): fill patterns & hatches, primitives,
 * polylines, text effects, the color palette, and an XOR animation
 * driven by AES timer events.  Redraws go through the AES rectangle
 * list, so the window can be moved, sized, fulled and overlapped.
 *
 * Quit with the window closer, 'q' or Esc.
 *
 * With Hatari --natfeats on, progress is logged to the host, which
 * shows up in the agent API's /console endpoint.
 */
#include <gem.h>
#include <osbind.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "natfeats.h"

#define PANELS_X 3
#define PANELS_Y 2
#define SPOKES   12
#define STEPS    64	/* animation positions per revolution */

static short vh;		/* virtual workstation handle */
static short ncolors;		/* number of pens */
static short char_w, char_h;	/* small font cell size */
static short win = -1;
static GRECT work;		/* window work area */
static int angle;		/* animation step */
static short spoke_x[STEPS], spoke_y[STEPS];	/* unit circle * 1024 */
static long redraws, ticks;

static void log_host(const char *fmt, long a, long b, long c)
{
	char buf[128];
	snprintf(buf, sizeof(buf), fmt, a, b, c);
	nf_print(buf);
}

static short pen(int i)
{
	/* skip white (0); on mono everything is black */
	return ncolors > 2 ? 1 + i % (ncolors - 1) : 1;
}

/* panel rectangle (with margin) for panel number n */
static void panel_rect(int n, GRECT *r)
{
	int col = n % PANELS_X, row = n / PANELS_X;
	short pw = work.g_w / PANELS_X, ph = work.g_h / PANELS_Y;
	r->g_x = work.g_x + col * pw + 2;
	r->g_y = work.g_y + row * ph + 2;
	r->g_w = pw - 4;
	r->g_h = ph - 4;
}

static void box(short x1, short y1, short x2, short y2)
{
	short pxy[4] = { x1, y1, x2, y2 };
	v_bar(vh, pxy);
}

static void label(const GRECT *r, const char *text)
{
	short dummy;
	vst_effects(vh, 0);
	vst_color(vh, 1);
	vst_alignment(vh, 0, 5, &dummy, &dummy);	/* left, top */
	v_gtext(vh, r->g_x + 2, r->g_y + 1, text);
}

/* client area of a panel below its label */
static void panel_body(const GRECT *r, GRECT *b)
{
	b->g_x = r->g_x + 2;
	b->g_y = r->g_y + char_h + 3;
	b->g_w = r->g_w - 4;
	b->g_h = r->g_h - char_h - 5;
}

static void panel_frame(const GRECT *r, const char *title)
{
	short pxy[10];

	vsf_interior(vh, FIS_SOLID);
	vsf_color(vh, 0);
	box(r->g_x, r->g_y, r->g_x + r->g_w - 1, r->g_y + r->g_h - 1);
	vsl_color(vh, 1);
	vsl_type(vh, SOLID);
	vsl_width(vh, 1);
	pxy[0] = pxy[6] = pxy[8] = r->g_x;
	pxy[1] = pxy[3] = pxy[9] = r->g_y;
	pxy[2] = pxy[4] = r->g_x + r->g_w - 1;
	pxy[5] = pxy[7] = r->g_y + r->g_h - 1;
	v_pline(vh, 5, pxy);
	label(r, title);
}

static void draw_patterns(const GRECT *r)
{
	GRECT b;
	int cols = 6, rows = 4, i;

	panel_frame(r, "Fills");
	panel_body(r, &b);
	vsf_perimeter(vh, 1);
	for (i = 0; i < cols * rows; i++)
	{
		short cw = b.g_w / cols, ch = b.g_h / rows;
		short x = b.g_x + (i % cols) * cw, y = b.g_y + (i / cols) * ch;
		short pxy[4] = { x + 1, y + 1, x + cw - 2, y + ch - 2 };
		if (i < 18)
		{
			vsf_interior(vh, FIS_PATTERN);
			vsf_style(vh, 1 + i);
		}
		else
		{
			vsf_interior(vh, FIS_HATCH);
			vsf_style(vh, 1 + (i - 18) * 2);
		}
		vsf_color(vh, pen(i));
		v_bar(vh, pxy);
		vsl_color(vh, 1);
		pxy[2] = x + cw - 2;	/* outline */
		{
			short o[10] = { x + 1, y + 1, x + cw - 2, y + 1, x + cw - 2,
			                y + ch - 2, x + 1, y + ch - 2, x + 1, y + 1 };
			v_pline(vh, 5, o);
		}
	}
}

static void draw_shapes(const GRECT *r)
{
	GRECT b;
	short cx, cy, rad, pxy[4];

	panel_frame(r, "Shapes");
	panel_body(r, &b);
	rad = (b.g_h < b.g_w / 2 ? b.g_h : b.g_w / 2) / 2 - 2;
	cy = b.g_y + b.g_h / 2;

	vsf_interior(vh, FIS_PATTERN);
	vsf_style(vh, 4);
	vsf_color(vh, pen(1));
	cx = b.g_x + rad + 2;
	v_circle(vh, cx, cy, rad);

	vsf_interior(vh, FIS_SOLID);
	vsf_color(vh, pen(3));
	v_pieslice(vh, cx, cy, rad - 3, 300, 1500);	/* tenths of degrees */

	vsf_interior(vh, FIS_HATCH);
	vsf_style(vh, 3);
	vsf_color(vh, pen(5));
	v_ellipse(vh, b.g_x + b.g_w * 3 / 4, b.g_y + b.g_h / 4 + 1, b.g_w / 4 - 2, b.g_h / 4 - 1);

	vsf_interior(vh, FIS_SOLID);
	vsf_color(vh, pen(7));
	pxy[0] = b.g_x + b.g_w / 2 + 2;
	pxy[1] = b.g_y + b.g_h / 2 + 2;
	pxy[2] = b.g_x + b.g_w - 2;
	pxy[3] = b.g_y + b.g_h - 2;
	v_rfbox(vh, pxy);
}

static void draw_lines(const GRECT *r)
{
	GRECT b;
	short pxy[4];
	int i, n = 16;

	panel_frame(r, "Lines");
	panel_body(r, &b);
	vsl_width(vh, 1);
	/* fan from bottom-left corner */
	for (i = 0; i <= n; i++)
	{
		vsl_color(vh, pen(i));
		vsl_type(vh, 1 + i % 6);	/* solid, long dash, dot, ... */
		pxy[0] = b.g_x;
		pxy[1] = b.g_y + b.g_h - 1;
		if (i <= n / 2)
		{
			pxy[2] = b.g_x + (b.g_w - 1) * i / (n / 2);
			pxy[3] = b.g_y;
		}
		else
		{
			pxy[2] = b.g_x + b.g_w - 1;
			pxy[3] = b.g_y + (b.g_h - 1) * (i - n / 2) / (n / 2);
		}
		v_pline(vh, 2, pxy);
	}
	/* thick line with rounded ends */
	vsl_type(vh, SOLID);
	vsl_color(vh, 1);
	vsl_width(vh, 5);
	vsl_ends(vh, ROUND, ROUND);
	pxy[0] = b.g_x + b.g_w / 2;
	pxy[1] = b.g_y + b.g_h - 6;
	pxy[2] = b.g_x + b.g_w - 6;
	pxy[3] = b.g_y + b.g_h / 2;
	v_pline(vh, 2, pxy);
	vsl_width(vh, 1);
	vsl_ends(vh, SQUARE, SQUARE);
}

static void draw_text(const GRECT *r)
{
	static const struct { short fx; const char *name; } fx[] = {
		{ TXT_THICKENED, "Bold" }, { TXT_LIGHT, "Light" },
		{ TXT_SKEWED, "Italic" }, { TXT_UNDERLINED, "Underline" },
		{ TXT_OUTLINED, "Outline" }, { TXT_SHADOWED, "Shadow" },
	};
	GRECT b;
	short dummy;
	unsigned i;

	panel_frame(r, "Text");
	panel_body(r, &b);
	vst_alignment(vh, 0, 5, &dummy, &dummy);
	for (i = 0; i < sizeof(fx) / sizeof(fx[0]); i++)
	{
		short y = b.g_y + i * (char_h + 2);
		if (y + char_h > b.g_y + b.g_h)
			break;
		/* readable pens: black, black, red, blue, magenta, black */
		static const short text_pens[] = { 1, 1, 2, 4, 7, 1 };
		vst_color(vh, ncolors >= 16 ? text_pens[i] : 1);
		vst_effects(vh, fx[i].fx);
		v_gtext(vh, b.g_x + 2, y, fx[i].name);
	}
	vst_effects(vh, 0);
}

static void draw_palette(const GRECT *r)
{
	GRECT b;
	short i, n = ncolors > 16 ? 16 : ncolors;
	short pxy[4];

	panel_frame(r, "Colors");
	panel_body(r, &b);
	vsf_interior(vh, FIS_SOLID);
	vsf_perimeter(vh, 1);
	for (i = 0; i < n; i++)
	{
		short cols = n > 4 ? n / 2 : n;
		short cw = b.g_w / cols, ch = b.g_h / (n > 4 ? 2 : 1);
		pxy[0] = b.g_x + (i % cols) * cw;
		pxy[1] = b.g_y + (i / cols) * ch;
		pxy[2] = pxy[0] + cw - 2;
		pxy[3] = pxy[1] + ch - 2;
		vsf_color(vh, i);
		v_bar(vh, pxy);
	}
	/* polymarkers on top */
	vsm_type(vh, 6);	/* diagonal cross */
	vsm_color(vh, 1);
	for (i = 0; i < 4; i++)
	{
		pxy[0] = b.g_x + b.g_w * (2 * i + 1) / 8;
		pxy[1] = b.g_y + b.g_h / 2;
		v_pmarker(vh, 1, pxy);
	}
}

/* rotating star, always drawn in XOR mode so it can be erased */
static void draw_star(const GRECT *b, int step)
{
	short cx = b->g_x + b->g_w / 2, cy = b->g_y + b->g_h / 2;
	short rad = (b->g_w < b->g_h ? b->g_w : b->g_h) / 2 - 2;
	short pxy[4];
	int i;

	vswr_mode(vh, MD_XOR);
	vsl_color(vh, 1);
	vsl_type(vh, SOLID);
	for (i = 0; i < SPOKES; i++)
	{
		int s = (step + i * STEPS / SPOKES) % STEPS;
		int t = (step * 3 + i * STEPS / SPOKES + STEPS / 2) % STEPS;
		pxy[0] = cx + (long)spoke_x[s] * rad / 1024;
		pxy[1] = cy + (long)spoke_y[s] * rad / 1024;
		pxy[2] = cx + (long)spoke_x[t] * rad / 2048;
		pxy[3] = cy + (long)spoke_y[t] * rad / 2048;
		v_pline(vh, 2, pxy);
	}
	vswr_mode(vh, MD_REPLACE);
}

static void draw_anim(const GRECT *r)
{
	GRECT b;
	panel_frame(r, "XOR anim");
	panel_body(r, &b);
	draw_star(&b, angle);
}

static void (*const panels[PANELS_X * PANELS_Y])(const GRECT *) = {
	draw_patterns, draw_shapes, draw_lines,
	draw_text, draw_palette, draw_anim,
};

static void set_clip(const GRECT *c)
{
	short pxy[4] = { c->g_x, c->g_y, c->g_x + c->g_w - 1, c->g_y + c->g_h - 1 };
	vs_clip(vh, 1, pxy);
}

/* redraw the parts of 'area' that are visible */
static void redraw(const GRECT *area)
{
	GRECT r, a = *area;
	int i;

	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, NULL);
	wind_get_grect(win, WF_FIRSTXYWH, &r);
	while (r.g_w && r.g_h)
	{
		if (rc_intersect(&a, &r))
		{
			set_clip(&r);
			vsf_interior(vh, FIS_SOLID);
			vsf_color(vh, 0);
			box(r.g_x, r.g_y, r.g_x + r.g_w - 1, r.g_y + r.g_h - 1);
			for (i = 0; i < PANELS_X * PANELS_Y; i++)
			{
				GRECT p;
				panel_rect(i, &p);
				if (rc_intersect(&r, &p))
				{
					panel_rect(i, &p);
					panels[i](&p);
				}
			}
		}
		wind_get_grect(win, WF_NEXTXYWH, &r);
	}
	vs_clip(vh, 0, NULL);
	graf_mouse(M_ON, NULL);
	wind_update(END_UPDATE);
	redraws++;
}

/* advance the animation: erase old star, draw new, in every visible rect */
static void animate(void)
{
	GRECT r, p, b;

	panel_rect(5, &p);
	panel_body(&p, &b);
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, NULL);
	wind_get_grect(win, WF_FIRSTXYWH, &r);
	while (r.g_w && r.g_h)
	{
		GRECT c = b;
		if (rc_intersect(&r, &c))
		{
			set_clip(&c);
			draw_star(&b, angle);
			draw_star(&b, (angle + 1) % STEPS);
		}
		wind_get_grect(win, WF_NEXTXYWH, &r);
	}
	vs_clip(vh, 0, NULL);
	graf_mouse(M_ON, NULL);
	wind_update(END_UPDATE);
	angle = (angle + 1) % STEPS;
	ticks++;
}

static void open_vdi(void)
{
	short work_in[11] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2 };
	short work_out[57];
	short wchar, hchar, wbox, hbox, dummy;

	vh = graf_handle(&wchar, &hchar, &wbox, &hbox);
	v_opnvwk(work_in, &vh, work_out);
	ncolors = work_out[13];
	/* small font: 6x6 system font on ST low res, 8x8 otherwise */
	vst_height(vh, hchar > 8 ? 6 : 4, &dummy, &dummy, &char_w, &char_h);
	log_host("GEMDEMO: screen %ldx%ld, %ld colors\n", work_out[0] + 1, work_out[1] + 1, ncolors);
}

static void update_work(void)
{
	wind_get_grect(win, WF_WORKXYWH, &work);
}

int main(void)
{
	short msg[8], ev, mx, my, mb, ks, key, clicks;
	GRECT desk, full;
	int i, quit = 0;

	if (appl_init() < 0)
		return 1;
	if (nf_init())
		nf_print("GEMDEMO: started (NatFeats available)\n");
	open_vdi();
	for (i = 0; i < STEPS; i++)
	{
		double a = 2 * M_PI * i / STEPS;
		spoke_x[i] = (short)(cos(a) * 1024);
		spoke_y[i] = (short)(sin(a) * 1024);
	}

	wind_get_grect(0, WF_WORKXYWH, &desk);
	win = wind_create_grect(NAME | CLOSER | FULLER | MOVER | SIZER, &desk);
	if (win < 0)
	{
		form_alert(1, "[3][No more windows!][ OK ]");
		v_clsvwk(vh);
		appl_exit();
		return 1;
	}
	wind_set_str(win, WF_NAME, " VDI showcase - Q to quit ");
	full = desk;
	full.g_x += 4;
	full.g_y += 4;
	full.g_w -= 8;
	full.g_h -= 8;
	graf_mouse(ARROW, NULL);
	wind_open_grect(win, &full);
	update_work();

	while (!quit)
	{
		ev = evnt_multi(MU_MESAG | MU_KEYBD | MU_TIMER, 0, 0, 0,
		                0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		                msg, 40, &mx, &my, &mb, &ks, &key, &clicks);
		if (ev & MU_MESAG)
		{
			GRECT r = { msg[4], msg[5], msg[6], msg[7] };
			switch (msg[0])
			{
			case WM_REDRAW:
				redraw(&r);
				break;
			case WM_TOPPED:
				wind_set(win, WF_TOP, win, 0, 0, 0);
				break;
			case WM_CLOSED:
				quit = 1;
				break;
			case WM_FULLED:
			{
				GRECT cur, prev;
				wind_get_grect(win, WF_CURRXYWH, &cur);
				wind_get_grect(win, WF_FULLXYWH, &full);
				if (cur.g_w == full.g_w && cur.g_h == full.g_h)
				{
					wind_get_grect(win, WF_PREVXYWH, &prev);
					wind_set_grect(win, WF_CURRXYWH, &prev);
				}
				else
					wind_set_grect(win, WF_CURRXYWH, &full);
				update_work();
				break;
			}
			case WM_SIZED:
				if (r.g_w < 120)
					r.g_w = 120;
				if (r.g_h < 80)
					r.g_h = 80;
				/* fall through */
			case WM_MOVED:
				wind_set_grect(win, WF_CURRXYWH, &r);
				update_work();
				if (msg[0] == WM_SIZED)
					redraw(&work);	/* panels are relative to size */
				break;
			}
		}
		if (ev & MU_KEYBD)
		{
			char c = key & 0xff;
			if (c == 'q' || c == 'Q' || c == 0x1b)
				quit = 1;
		}
		if ((ev & MU_TIMER) && !quit)
			animate();
	}

	log_host("GEMDEMO: exit after %ld redraws, %ld animation frames%s\n", redraws, ticks, (long)"");
	wind_close(win);
	wind_delete(win);
	v_clsvwk(vh);
	appl_exit();
	return 0;
}
