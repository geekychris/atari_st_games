/*
 * Game launcher for the Atari ST: a menu of the game ports on drive C:.
 *
 * Reads C:\GAMES.INI (written by make_drive.py from examples/games.ini):
 * one [section] per game with title, folder (under C:\GAMES), program,
 * machine (st, ste or falcon), desc and ctrl lines.  Shows the list, a
 * 160x100 thumbnail of the selected game (THUMB.DAT in its folder: 8
 * STE colours and a 4-plane image using pens 8-15), its description and
 * controls.  Return / Space / fire starts the game from its own folder
 * (so it finds its data files), and the menu comes back when it exits.
 *
 * Text goes through the TOS VT52 console in ST low resolution; the
 * thumbnail is copied straight to screen memory.  Keys and joystick 1
 * come from the ST layer's IKBD handler, installed only while the menu
 * is up.  Games the machine can't run (an STE game on a plain ST, a
 * Falcon game on an STE) are listed but can't be started.
 *
 * Switching machines: when make_drive.py --run supervises Hatari, it
 * creates C:\SWITCH.ON.  Then Return on a game this machine can't run (or
 * M for the other machine) writes the game to C:\LAUNCHER.NXT and logs
 * "LAUNCHER SWITCH falcon" (or "ste"); the supervisor reconfigures Hatari
 * through the agent API, the machine reboots into the launcher, and the
 * launcher starts the game from LAUNCHER.NXT.
 *
 * With Hatari --natfeats on, the launcher logs "LAUNCHER ..." lines.
 */
#include <osbind.h>
#include <stdio.h>
#include <string.h>
#include "st_ikbd.h"
#include "natfeats.h"

#define MAX_GAMES 24
#define MAX_LINES 6
#define LIST_ROW  2
#define LIST_ROWS 12
#define THUMB_W   160
#define THUMB_H   100
#define THUMB_X   160			/* pixels, a multiple of 16 */
#define THUMB_Y   16
#define TEXT_ROW  16
#define TEXT_ROWS 8

struct game {
	char id[20], title[24], folder[12], program[16], machine[8];
	char desc[MAX_LINES][41], ctrl[MAX_LINES][41];
	int ndesc, nctrl;
};

static struct game games[MAX_GAMES];
static int ngames, sel, top;
static long mch;			/* _MCH cookie: machine type */
static int can_switch;			/* C:\SWITCH.ON: a host supervisor */

/* menu pens 0-7 (pens 8-15 are the thumbnail's) */
enum { P_BG, P_TEXT, P_TITLE, P_SEL, P_DIM, P_HEAD, P_WARN, P_WHITE };
static const UWORD menu_pal[8] = {
	0x112, 0x666, 0x764, 0x237, 0x444, 0x5a7, 0x733, 0x777,
};
static UWORD old_pal[16];

static void log_line(const char *msg)
{
	char buf[160];
	snprintf(buf, sizeof(buf), "LAUNCHER %s\n", msg);
	nf_print(buf);
}

/* ---- machine ---- */

/* the cookie jar pointer ($5a0) is in supervisor memory: Supexec */
static long machine_cookie(void)
{
	long *jar = *(long **)0x5a0;
	if (!jar)
		return -1;
	for (; jar[0]; jar += 2)
		if (jar[0] == 0x5f4d4348)	/* '_MCH' */
			return jar[1];
	return -1;
}

static const char *machine_name(void)
{
	switch (mch >> 16) {
	case 0: return "ST";
	case 1: return (mch & 0xffff) == 0x10 ? "Mega STE" : "STE";
	case 2: return "TT";
	case 3: return "Falcon030";
	}
	return "Atari";
}

/* can this machine run the game?  (ST games: ST, STE, Mega STE) */
static int runnable(const struct game *g)
{
	int m = (int)(mch >> 16);
	if (!strcmp(g->machine, "falcon"))
		return m == 3;
	if (!strcmp(g->machine, "ste") || !strcmp(g->machine, "megaste"))
		return m == 1;
	return m == 0 || m == 1;
}

static const char *needs(const struct game *g)
{
	if (!strcmp(g->machine, "falcon"))
		return "needs a Falcon030";
	if (!strcmp(g->machine, "ste") || !strcmp(g->machine, "megaste"))
		return "needs an STE";
	return "needs an ST or STE";
}

/* ---- GAMES.INI ---- */

static void copy_str(char *d, const char *s, int n)
{
	strncpy(d, s, n - 1);
	d[n - 1] = 0;
}

static char *trim(char *s)
{
	char *e;
	while (*s == ' ' || *s == '\t')
		s++;
	e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
		*--e = 0;
	return s;
}

static int load_ini(const char *path)
{
	static char buf[16384];
	long fh = Fopen(path, 0), n;
	char *line, *next;
	struct game *g = NULL;

	if (fh < 0)
		return 0;
	n = Fread((short)fh, sizeof(buf) - 1, buf);
	Fclose((short)fh);
	if (n <= 0)
		return 0;
	buf[n] = 0;
	for (line = buf; line && *line; line = next) {
		char *eq, *key, *val;
		next = strchr(line, '\n');
		if (next)
			*next++ = 0;
		line = trim(line);
		if (!*line || *line == ';' || *line == '#')
			continue;
		if (*line == '[') {
			char *end = strchr(line, ']');
			if (end)
				*end = 0;
			g = ngames < MAX_GAMES ? &games[ngames++] : NULL;
			if (g) {
				memset(g, 0, sizeof(*g));
				copy_str(g->id, line + 1, sizeof(g->id));
			}
			continue;
		}
		eq = strchr(line, '=');
		if (!g || !eq)
			continue;
		*eq = 0;
		key = trim(line);
		val = trim(eq + 1);
		if (!strcmp(key, "title"))
			copy_str(g->title, val, sizeof(g->title));
		else if (!strcmp(key, "folder"))
			copy_str(g->folder, val, sizeof(g->folder));
		else if (!strcmp(key, "program"))
			copy_str(g->program, val, sizeof(g->program));
		else if (!strcmp(key, "machine"))
			copy_str(g->machine, val, sizeof(g->machine));
		else if (!strcmp(key, "desc") && g->ndesc < MAX_LINES)
			copy_str(g->desc[g->ndesc++], val, 41);
		else if (!strcmp(key, "ctrl") && g->nctrl < MAX_LINES)
			copy_str(g->ctrl[g->nctrl++], val, 41);
	}
	return ngames;
}

/* ---- screen: VT52 text, raw thumbnail ---- */

static void out(const char *s) { Cconws(s); }

static void at(int row, int col)
{
	char e[5] = { 27, 'Y', (char)(32 + row), (char)(32 + col), 0 };
	out(e);
}

/* VT52 colour escapes take a character whose low 4 bits are the pen */
static void ink(int fg, int bg)
{
	char e[7] = { 27, 'b', (char)('0' + fg), 27, 'c', (char)('0' + bg), 0 };
	out(e);
}

/* text padded with spaces to width columns */
static void field(int row, int col, int width, const char *s, int fg, int bg)
{
	char line[42];
	int n = (int)strlen(s);
	if (n > width)
		n = width;
	memcpy(line, s, n);
	memset(line + n, ' ', width - n);
	line[width] = 0;
	at(row, col);
	ink(fg, bg);
	out(line);
}

static void clear_thumb(void)
{
	UBYTE *scr = (UBYTE *)Physbase();
	for (int y = 0; y < THUMB_H; y++)
		memset(scr + (long)(THUMB_Y + y) * 160 + THUMB_X / 2, 0, THUMB_W / 2);
}

static void show_thumb(const struct game *g)
{
	static UWORD data[8 + THUMB_H * THUMB_W / 4];
	char path[64];
	long fh, n;
	UBYTE *scr = (UBYTE *)Physbase();

	snprintf(path, sizeof(path), "C:\\GAMES\\%s\\THUMB.DAT", g->folder);
	fh = Fopen(path, 0);
	if (fh < 0) {
		clear_thumb();
		return;
	}
	n = Fread((short)fh, sizeof(data), data);
	Fclose((short)fh);
	if (n != (long)sizeof(data)) {
		clear_thumb();
		return;
	}
	Vsync();
	for (int i = 0; i < 8; i++)
		Setcolor(8 + i, data[i]);
	for (int y = 0; y < THUMB_H; y++)
		memcpy(scr + (long)(THUMB_Y + y) * 160 + THUMB_X / 2,
		       (UBYTE *)(data + 8) + (long)y * (THUMB_W / 2), THUMB_W / 2);
}

static void draw_entry(int i)
{
	char line[24];
	int row = LIST_ROW + i - top;
	if (i < top || i >= top + LIST_ROWS)
		return;
	if (i >= ngames) {
		field(row, 0, 20, "", P_TEXT, P_BG);
		return;
	}
	snprintf(line, sizeof(line), " %s", games[i].title);
	if (i == sel)
		field(row, 0, 20, line, P_WHITE, P_SEL);
	else
		field(row, 0, 20, line, runnable(&games[i]) ? P_TEXT : P_DIM, P_BG);
}

static void draw_list(void)
{
	for (int r = 0; r < LIST_ROWS; r++)
		draw_entry(top + r);
	/* scroll hints */
	field(LIST_ROW - 1, 0, 20, top > 0 ? "        ...        " : "", P_DIM, P_BG);
	field(LIST_ROW + LIST_ROWS, 0, 20, top + LIST_ROWS < ngames ? "        ...        " : "",
	      P_DIM, P_BG);
}

static void draw_info(void)
{
	const struct game *g = &games[sel];
	int row = TEXT_ROW, i;
	char line[42];

	if (runnable(g))
		snprintf(line, sizeof(line), "%s  (%s)", g->title, g->program);
	else
		snprintf(line, sizeof(line), "%s: %s", g->title, needs(g));
	field(TEXT_ROW - 1, 0, 40, line, runnable(g) ? P_HEAD : P_WARN, P_BG);
	for (i = 0; i < g->ndesc && row < TEXT_ROW + TEXT_ROWS; i++)
		field(row++, 0, 40, g->desc[i], P_TEXT, P_BG);
	for (i = 0; i < g->nctrl && row < TEXT_ROW + TEXT_ROWS; i++)
		field(row++, 0, 40, g->ctrl[i], P_TITLE, P_BG);
	while (row < TEXT_ROW + TEXT_ROWS)
		field(row++, 0, 40, "", P_TEXT, P_BG);
	show_thumb(g);
}

static void draw_all(void)
{
	char head[42];
	out("\033E\033f");			/* clear, cursor off */
	snprintf(head, sizeof(head), " GAME PORTS                  %-10s", machine_name());
	field(0, 0, 40, head, P_WHITE, P_SEL);
	field(24, 0, 39, can_switch ? " Up/Down  Return/Fire play  M machine"
	                            : " Up/Down select  Return/Fire play  Esc", P_DIM, P_BG);
	draw_list();
	draw_info();
}

static void screen_on(void)
{
	Setscreen((void *)-1, (void *)-1, 0);	/* ST low, VT52 re-initialised */
	for (int i = 0; i < 8; i++)
		Setcolor(i, menu_pal[i]);
	for (int i = 8; i < 16; i++)
		Setcolor(i, 0);
	__asm__ volatile ("dc.w 0xa00a" ::: "d0", "d1", "d2", "a0", "a1", "a2");	/* hide mouse */
}

/* ---- main loop ---- */

static void select_game(int i)
{
	if (i < 0 || i >= ngames || i == sel)
		return;
	int old = sel, old_top = top;
	sel = i;
	if (sel < top)
		top = sel;
	if (sel >= top + LIST_ROWS)
		top = sel - LIST_ROWS + 1;
	if (top != old_top)
		draw_list();
	else {
		draw_entry(old);	/* only the two rows that change */
		draw_entry(sel);
	}
	draw_info();
}

static int is_falcon(void) { return (mch >> 16) == 3; }

/* message line above the description */
static void message(const char *s, int pen)
{
	field(TEXT_ROW - 1, 0, 40, s, pen, P_BG);
}

/* ask the host supervisor to reboot as the other machine, then start
 * game g there (NULL: just the menu) */
static void request_switch(const struct game *g)
{
	const char *to = is_falcon() ? "ste" : "falcon";
	char msg[48];

	if (g && !strcmp(g->machine, "falcon"))
		to = "falcon";
	else if (g)
		to = "ste";
	if (!can_switch) {
		message(g ? needs(g) : "Switching needs make_drive.py --run", P_WARN);
		return;
	}
	Fdelete("C:\\LAUNCHER.NXT");
	if (g) {
		long fh = Fcreate("C:\\LAUNCHER.NXT", 0);
		if (fh >= 0) {
			Fwrite((short)fh, strlen(g->id), g->id);
			Fclose((short)fh);
		}
	}
	snprintf(msg, sizeof(msg), "Restarting as %s...", strcmp(to, "falcon") ? "an STE" : "a Falcon030");
	message(msg, P_HEAD);
	snprintf(msg, sizeof(msg), "SWITCH %s", to);
	log_line(msg);
}

/* the game LAUNCHER.NXT asks for after a machine switch, or -1 */
static int next_game(void)
{
	char id[24];
	long fh = Fopen("C:\\LAUNCHER.NXT", 0), n;
	if (fh < 0)
		return -1;
	n = Fread((short)fh, sizeof(id) - 1, id);
	Fclose((short)fh);
	Fdelete("C:\\LAUNCHER.NXT");
	if (n <= 0)
		return -1;
	id[n] = 0;
	for (int i = 0; i < ngames; i++)
		if (!strcmp(games[i].id, trim(id)))
			return i;
	return -1;
}

/* the menu; returns the game to start, or -1 to quit */
static int menu(void)
{
	long ssp;
	int choice = -2, repeat = 0;

	screen_on();
	draw_all();
	ssp = Super(0L);
	ikbd_init(IKBD_JOYSTICK);
	while (choice == -2) {
		UBYTE joy, hits;
		int dir = 0;
		Vsync();
		joy = ikbd_joy1();
		hits = ikbd_joy1_hits();
		if (ikbd_key_hit(SC_UP) || (hits & 1))
			dir = -1;
		if (ikbd_key_hit(SC_DOWN) || (hits & 2))
			dir = 1;
		/* held: repeat after 20 frames, then every 5 */
		if (!dir && (ikbd_keys[SC_UP] || ikbd_keys[SC_DOWN] || (joy & 3))) {
			if (++repeat > 20 && !(repeat % 5))
				dir = (ikbd_keys[SC_UP] || (joy & 1)) ? -1 : 1;
		} else if (!dir)
			repeat = 0;
		if (dir) {
			Super((void *)ssp);
			select_game(sel + dir);
			ssp = Super(0L);
		}
		if (ikbd_key_hit(SC_RETURN) || ikbd_key_hit(SC_SPACE) || (hits & 0x80)) {
			if (runnable(&games[sel]))
				choice = sel;
			else {
				Super((void *)ssp);
				request_switch(&games[sel]);
				ssp = Super(0L);
			}
		}
		if (ikbd_key_hit(SC_M)) {
			Super((void *)ssp);
			request_switch(NULL);
			ssp = Super(0L);
		}
		if (ikbd_key_hit(SC_ESC))
			choice = -1;
	}
	/* wait for the keys to be released, so the game doesn't see them */
	while (ikbd_keys[SC_RETURN] || ikbd_keys[SC_SPACE] || ikbd_keys[SC_ESC] ||
	       (ikbd_joy1() & 0x80))
		Vsync();
	ikbd_exit();
	Super((void *)ssp);
	return choice;
}

static void play(const struct game *g)
{
	char dir[40], msg[128];
	long r;

	out("\033E");
	snprintf(dir, sizeof(dir), "\\GAMES\\%s", g->folder);
	Dsetdrv(2);
	Dsetpath(dir);
	snprintf(msg, sizeof(msg), "PLAY %s C:%s\\%s", g->id, dir, g->program);
	log_line(msg);
	r = Pexec(0, g->program, "\0", NULL);
	Dsetpath("\\");
	snprintf(msg, sizeof(msg), "BACK %s exit=%ld", g->id, r);
	log_line(msg);
}

int main(void)
{
	int old_rez = Getrez();
	char msg[64];

	nf_init();
	for (int i = 0; i < 16; i++)
		old_pal[i] = (UWORD)Setcolor(i, -1);
	mch = Supexec(machine_cookie);
	if (mch < 0)
		mch = 0;
	if (!load_ini("C:\\GAMES.INI")) {
		Cconws("C:\\GAMES.INI not found or empty\r\n");
		Cconin();
		return 1;
	}
	/* start on the first game this machine can run */
	for (sel = 0; sel < ngames - 1 && !runnable(&games[sel]); sel++)
		;
	{
		long fh = Fopen("C:\\SWITCH.ON", 0);
		can_switch = fh >= 0;
		if (fh >= 0)
			Fclose((short)fh);
	}
	int next = next_game();			/* after a machine switch */
	if (next >= 0)
		sel = next;
	top = sel >= LIST_ROWS ? sel - LIST_ROWS + 1 : 0;
	snprintf(msg, sizeof(msg), "START games=%d machine=%s switch=%d", ngames, machine_name(),
		 can_switch);
	log_line(msg);
	if (next >= 0 && runnable(&games[next]))
		play(&games[next]);

	for (;;) {
		int i = menu();
		if (i < 0)
			break;
		play(&games[i]);
	}

	log_line("EXIT");
	out("\033E\033e");
	Setscreen((void *)-1, (void *)-1, old_rez);
	Setpalette(old_pal);
	__asm__ volatile ("dc.w 0xa009" ::: "d0", "d1", "d2", "a0", "a1", "a2");	/* show mouse */
	return 0;
}
