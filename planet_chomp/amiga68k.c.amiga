/*
 * The classic 68k Amiga under the 3DO ports: display (RTG window, RTG
 * screen or AGA screen), timer and file loading. See amiga68k.h. The same
 * file is in planet_chomp, rolling_steel and spectral_keep.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/modeid.h>
#include <graphics/displayinfo.h>
#include <devices/timer.h>
#include <hardware/custom.h>
#include <hardware/cia.h>
#include <libraries/Picasso96.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/timer.h>
#include <proto/Picasso96.h>
#include <stdio.h>
#include <string.h>
#include "amiga68k.h"

struct Library *P96Base;
UWORD *fb;
int fb_h;

static struct Screen *scr;
static struct Window *win;
static const char *title_;
static int mode_, wscale, sc, ox, oy;
static UWORD *big;                   /* RTG: the frame scaled up */
static const char *why = "";         /* why RTG wasn't used, for sys_mode_name() */
static long big_bytes;

/* ---- RTG ---- */

static int wb_is_rtg(void)
{
    struct Screen *wb;
    int rtg = 0;
    if (!P96Base) return 0;
    wb = LockPubScreen(NULL);
    if (wb) {
        rtg = p96GetBitMapAttr(wb->RastPort.BitMap, P96BMA_ISP96) != 0;
        UnlockPubScreen(NULL, wb);
    }
    return rtg;
}

static int alloc_big(int s)
{
    long need = (long)FB_W * s * fb_h * s * 2;
    sc = s;
    if (s == 1) return 1;
    if (big && big_bytes >= need) return 1;
    if (big) FreeVec(big);
    big = (UWORD *)AllocVec(need, MEMF_ANY);
    big_bytes = big ? need : 0;
    return big != 0;
}

static int open_rtg_window(void)
{
    win = OpenWindowTags(NULL,
                         WA_Title, (ULONG)title_,
                         WA_InnerWidth, FB_W * wscale, WA_InnerHeight, fb_h * wscale,
                         WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
                         WA_Activate, TRUE, WA_RMBTrap, TRUE, WA_GimmeZeroZero, TRUE,
                         WA_IDCMP, SYS_IDCMP, TAG_DONE);
    if (!win) return 0;
    ox = oy = 0;
    mode_ = SYS_WINDOW;
    return alloc_big(wscale);
}

static int open_rtg_screen(void)
{
    static const int sizes[3][2] = { { 640, 480 }, { 800, 600 }, { 320, 240 } };
    ULONG id = INVALID_ID;
    int k, w = 0, h = 0, s;
    static const int depths[4] = { 16, 15, 32, 24 };   /* BestModeID assumes 8 if not told */
    int dk;
    for (k = 0; k < 3 * 4 && id == INVALID_ID; k++) {
        w = sizes[k / 4][0]; h = sizes[k / 4][1]; dk = k % 4;
        if (w < FB_W || h < fb_h) continue;
        id = p96BestModeIDTags(P96BIDTAG_NominalWidth, w, P96BIDTAG_NominalHeight, h,
                               P96BIDTAG_Depth, depths[dk],
                               P96BIDTAG_FormatsAllowed, RGBFF_R5G6B5 | RGBFF_R5G5B5 | RGBFF_R5G6B5PC |
                               RGBFF_R5G5B5PC | RGBFF_A8R8G8B8 | RGBFF_B8G8R8A8 | RGBFF_R8G8B8A8,
                               TAG_DONE);
        if (id != INVALID_ID) {
            w = p96GetModeIDAttr(id, P96IDA_WIDTH);
            h = p96GetModeIDAttr(id, P96IDA_HEIGHT);
            if (w < FB_W || h < fb_h) id = INVALID_ID;
        }
    }
    if (id == INVALID_ID) { why = " (no RTG mode)"; return 0; }
    scr = p96OpenScreenTags(P96SA_DisplayID, id, P96SA_Width, w, P96SA_Height, h,
                            P96SA_Depth, p96GetModeIDAttr(id, P96IDA_DEPTH),
                            P96SA_Quiet, TRUE, P96SA_ShowTitle, FALSE, P96SA_Type, CUSTOMSCREEN,
                            P96SA_Title, (ULONG)title_, TAG_DONE);
    if (!scr) { why = " (RTG screen didn't open)"; return 0; }
    win = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, 0, WA_Top, 0,
                         WA_Width, w, WA_Height, h, WA_Backdrop, TRUE, WA_Borderless, TRUE,
                         WA_Activate, TRUE, WA_RMBTrap, TRUE, WA_IDCMP, SYS_IDCMP, TAG_DONE);
    if (!win) { p96CloseScreen(scr); scr = 0; return 0; }
    s = w / FB_W < h / fb_h ? w / FB_W : h / fb_h;
    if (s < 1) s = 1;
    ox = (w - FB_W * s) / 2;
    oy = (h - fb_h * s) / 2;
    SetAPen(win->RPort, 1);
    SetRast(win->RPort, 0);
    mode_ = SYS_RTG_SCREEN;
    return alloc_big(s);
}

static void present_rtg(void)
{
    struct RenderInfo ri;
    if (sc > 1) {
        const UWORD *src = fb;
        int y, x, k, bw = FB_W * sc;
        for (y = 0; y < fb_h; y++, src += FB_W) {
            UWORD *row = big + (long)y * sc * bw, *d = row;
            if (sc == 2)
                for (x = 0; x < FB_W; x++) { d[0] = d[1] = src[x]; d += 2; }
            else
                for (x = 0; x < FB_W; x++)
                    for (k = 0; k < sc; k++) *d++ = src[x];
            for (k = 1; k < sc; k++) memcpy(row + (long)k * bw, row, bw * 2);
        }
        ri.Memory = big;
        ri.BytesPerRow = bw * 2;
    } else {
        ri.Memory = fb;
        ri.BytesPerRow = FB_W * 2;
    }
    ri.pad = 0;
    ri.RGBFormat = RGBFB_R5G5B5;
    p96WritePixelArray(&ri, 0, 0, win->RPort, ox, oy, FB_W * sc, fb_h * sc);
}

/* ---- AGA: 256 colours, chunky to planar, double buffered ----
 *
 * The palette is fitted to the frame: about once a second a histogram of
 * the colours on screen is cut into 256 boxes (weighted median cut) and
 * each box's average becomes a pen. A pixel gets the nearest pen, worked
 * out the first time its colour turns up and then kept in pen_of[]. These
 * games show far fewer than 256 colours at a time (a room's stone, a few
 * sprites), so most frames come out exact, with no dithering. */

#define NPEN 255                     /* pen 255 = "not looked up yet" in pen_of[] */
static struct ScreenBuffer *sbuf[2];
static int cur;
static UWORD *hist;                  /* 32768 counts, saturating */
static UBYTE *pen_of;                /* 32768: pen, or 255 = not looked up yet */
static UWORD *ucol, *tmp;            /* the frame's colours; sort scratch */
static UBYTE pen_rgb[NPEN][3];       /* 5-bit */
static int npen, since_fit = 1000, pal_changed;
static ULONG pal32[2 + 256 * 3];
static ULONG c2p_lo[256], c2p_hi[256];

typedef struct { int lo, hi, range, ch; } PBox;   /* ucol[lo..hi) */

static int chan(UWORD c, int ch) { return ch == 0 ? (c >> 10) & 31 : ch == 1 ? (c >> 5) & 31 : c & 31; }

static void box_measure(PBox *b)
{
    int mn[3] = { 31, 31, 31 }, mx[3] = { 0, 0, 0 }, i, k;
    for (i = b->lo; i < b->hi; i++)
        for (k = 0; k < 3; k++) {
            int v = chan(ucol[i], k);
            if (v < mn[k]) mn[k] = v;
            if (v > mx[k]) mx[k] = v;
        }
    b->ch = 0;
    for (k = 1; k < 3; k++) if (mx[k] - mn[k] > mx[b->ch] - mn[b->ch]) b->ch = k;
    b->range = b->hi - b->lo > 1 ? mx[b->ch] - mn[b->ch] : -1;
}

/* split a box at the weighted median of its widest channel */
static int box_split(PBox *b, PBox *nb)
{
    int cnt[32], i, k, total = 0, half, acc = 0, cut;
    long pos[32];
    for (k = 0; k < 32; k++) cnt[k] = 0;
    for (i = b->lo; i < b->hi; i++) cnt[chan(ucol[i], b->ch)]++;
    pos[0] = b->lo;
    for (k = 1; k < 32; k++) pos[k] = pos[k - 1] + cnt[k - 1];
    for (i = b->lo; i < b->hi; i++) tmp[pos[chan(ucol[i], b->ch)]++] = ucol[i];
    for (i = b->lo; i < b->hi; i++) { ucol[i] = tmp[i]; total += hist[ucol[i]]; }
    half = total / 2;
    for (cut = b->lo; cut < b->hi - 1; cut++) {
        acc += hist[ucol[cut]];
        if (acc >= half) { cut++; break; }
    }
    if (cut <= b->lo) cut = b->lo + 1;
    if (cut >= b->hi) cut = b->hi - 1;
    nb->lo = cut; nb->hi = b->hi;
    b->hi = cut;
    box_measure(b);
    box_measure(nb);
    return 1;
}

static void fit_palette(void)
{
    static PBox box[NPEN];
    long n = (long)FB_W * fb_h, i;
    int nu = 0, nb, k;
    for (i = 0; i < 32768; i++) hist[i] = 0;
    for (i = 0; i < n; i++) { UWORD c = fb[i] & 0x7FFF; if (hist[c] < 65535) hist[c]++; }
    for (i = 0; i < 32768; i++) if (hist[i]) ucol[nu++] = (UWORD)i;
    if (!nu) return;
    box[0].lo = 0; box[0].hi = nu;
    box_measure(&box[0]);
    nb = 1;
    while (nb < NPEN) {
        int best = -1;
        for (k = 0; k < nb; k++) if (box[k].range > 0 && (best < 0 || box[k].range > box[best].range)) best = k;
        if (best < 0) break;                 /* every box is one colour: exact */
        box_split(&box[best], &box[nb]);
        nb++;
    }
    for (k = 0; k < nb; k++) {
        long w = 0, r = 0, g = 0, b = 0;
        for (i = box[k].lo; i < box[k].hi; i++) {
            UWORD c = ucol[i];
            long h = hist[c];
            w += h; r += h * ((c >> 10) & 31); g += h * ((c >> 5) & 31); b += h * (c & 31);
        }
        if (!w) w = 1;
        pen_rgb[k][0] = (UBYTE)((r + w / 2) / w);
        pen_rgb[k][1] = (UBYTE)((g + w / 2) / w);
        pen_rgb[k][2] = (UBYTE)((b + w / 2) / w);
    }
    {
        /* the same palette as before (a room standing still): keep the
         * looked-up pens and don't reload the colours */
        static UBYTE last[NPEN][3];
        static int last_n = -1;
        if (nb == last_n && !memcmp(last, pen_rgb, (size_t)nb * 3)) return;
        memcpy(last, pen_rgb, sizeof(last));
        last_n = nb;
    }
    npen = nb;
    for (i = 0; i < 32768; i++) pen_of[i] = 255;
    pal32[0] = 256UL << 16;
    for (k = 0; k < 256; k++) {
        int c, v;
        for (c = 0; c < 3; c++) {
            v = k < npen ? (pen_rgb[k][c] << 3) | (pen_rgb[k][c] >> 2) : 0;
            pal32[1 + k * 3 + c] = (ULONG)v * 0x01010101UL;
        }
    }
    pal32[1 + 256 * 3] = 0;
    pal_changed = 1;
}

static UBYTE nearest(UWORD c)
{
    int r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31, k, best = 0;
    long bd = 0x7FFFFFFFL;
    for (k = 0; k < npen; k++) {
        int dr = r - pen_rgb[k][0], dg = g - pen_rgb[k][1], db = b - pen_rgb[k][2];
        long d = (long)dr * dr * 3 + (long)dg * dg * 4 + (long)db * db * 2;   /* green counts most */
        if (d < bd) { bd = d; best = k; }
    }
    return (UBYTE)best;
}

static int aga_tables(void)
{
    int c;
    hist = (UWORD *)AllocVec(3 * 32768 * 2 + 32768, MEMF_ANY);
    if (!hist) return 0;
    ucol = hist + 32768;
    tmp = ucol + 32768;
    pen_of = (UBYTE *)(tmp + 32768);
    for (c = 0; c < 256; c++) {
        ULONG lo = 0, hi = 0;
        int k;
        for (k = 0; k < 4; k++) {
            if (c & (1 << k)) lo |= 0x80UL << (24 - 8 * k);
            if (c & (16 << k)) hi |= 0x80UL << (24 - 8 * k);
        }
        c2p_lo[c] = lo;                  /* byte k (from the top): plane k's bit for pixel 0 */
        c2p_hi[c] = hi;
    }
    return 1;
}

static int open_aga(void)
{
    if (!hist && !aga_tables()) return 0;
    scr = OpenScreenTags(NULL, SA_Width, 320, SA_Height, 256, SA_Depth, 8,
                         SA_DisplayID, PAL_MONITOR_ID | LORES_KEY, SA_Quiet, TRUE,
                         SA_ShowTitle, FALSE, SA_Type, CUSTOMSCREEN, SA_Title, (ULONG)title_, TAG_DONE);
    if (!scr) return 0;
    win = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, 0, WA_Top, 0,
                         WA_Width, 320, WA_Height, 256, WA_Backdrop, TRUE, WA_Borderless, TRUE,
                         WA_Activate, TRUE, WA_RMBTrap, TRUE, WA_IDCMP, SYS_IDCMP, TAG_DONE);
    if (!win) { CloseScreen(scr); scr = 0; return 0; }
    sbuf[0] = AllocScreenBuffer(scr, NULL, SB_SCREEN_BITMAP);
    sbuf[1] = AllocScreenBuffer(scr, NULL, 0);
    if (!sbuf[0] || !sbuf[1]) return 0;
    cur = 1;
    oy = (256 - fb_h) / 2;
    since_fit = 1000;                        /* fit a palette to the first frame */
    mode_ = SYS_AGA;
    return 1;
}

#define REFIT_FRAMES 100            /* 2-4 s */

static void present_aga(void)
{
    struct BitMap *bm = sbuf[cur]->sb_BitMap;
    int y, x, bpr = bm->BytesPerRow, tries = 0;
    if (++since_fit >= REFIT_FRAMES) { fit_palette(); since_fit = 0; }
    for (y = 0; y < fb_h; y++) {
        const UWORD *src = fb + (long)y * FB_W;
        long off = (long)(y + oy) * bpr;
        for (x = 0; x < FB_W; x += 8) {
            ULONG lo = 0, hi = 0;
            int i;
            for (i = 0; i < 8; i++) {
                UWORD c = src[x + i] & 0x7FFF;
                UBYTE p = pen_of[c];
                if (p == 255) p = pen_of[c] = nearest(c);
                lo |= c2p_lo[p] >> i;
                hi |= c2p_hi[p] >> i;
            }
            bm->Planes[0][off] = (UBYTE)(lo >> 24); bm->Planes[1][off] = (UBYTE)(lo >> 16);
            bm->Planes[2][off] = (UBYTE)(lo >> 8);  bm->Planes[3][off] = (UBYTE)lo;
            bm->Planes[4][off] = (UBYTE)(hi >> 24); bm->Planes[5][off] = (UBYTE)(hi >> 16);
            bm->Planes[6][off] = (UBYTE)(hi >> 8);  bm->Planes[7][off] = (UBYTE)hi;
            off++;
        }
    }
    while (!ChangeScreenBuffer(scr, sbuf[cur]) && ++tries < 5) WaitTOF();
    if (pal_changed) { LoadRGB32(&scr->ViewPort, pal32); pal_changed = 0; }
    cur ^= 1;
}

/* ---- the rest ---- */

static void close_display(void)
{
    if (win) {
        struct IntuiMessage *m;
        while ((m = (struct IntuiMessage *)GetMsg(win->UserPort)) != 0) ReplyMsg((struct Message *)m);
        CloseWindow(win);
    }
    win = 0;
    if (mode_ == SYS_AGA && scr) {
        int tries = 0;
        if (sbuf[0]) while (!ChangeScreenBuffer(scr, sbuf[0]) && ++tries < 5) WaitTOF();
        WaitTOF();
        if (sbuf[1]) FreeScreenBuffer(scr, sbuf[1]);
        if (sbuf[0]) FreeScreenBuffer(scr, sbuf[0]);
        sbuf[0] = sbuf[1] = 0;
        CloseScreen(scr);
    } else if (scr)
        p96CloseScreen(scr);
    scr = 0;
}

int sys_open(const char *title, int h, int scale, int mode)
{
    title_ = title;
    fb_h = h;
    wscale = scale < 1 ? 1 : scale;
    fb = (UWORD *)AllocVec((long)FB_W * h * 2, MEMF_ANY | MEMF_CLEAR);
    if (!fb) return 0;
    P96Base = OpenLibrary((CONST_STRPTR)"Picasso96API.library", 2);
    if (!P96Base) why = " (no Picasso96API.library)";
    if (mode == SYS_AUTO) {
        /* an RTG Workbench: a window on it; else an RTG screen if there's a
         * card (no palette to fit); else AGA */
        if (wb_is_rtg()) mode = SYS_WINDOW;
        else if (P96Base && open_rtg_screen()) return 1;
        else mode = SYS_AGA;
    }
    if (mode == SYS_RTG_SCREEN && P96Base && open_rtg_screen()) return 1;
    if (mode == SYS_WINDOW && wb_is_rtg() && open_rtg_window()) return 1;
    if (mode == SYS_RTG_SCREEN || mode == SYS_WINDOW) {          /* what was asked for failed */
        close_display();
        if (P96Base && wb_is_rtg() && open_rtg_window()) return 1;
        close_display();
    }
    return open_aga();
}

void sys_close(void)
{
    close_display();
    if (big) FreeVec(big);
    if (hist) FreeVec(hist);
    if (fb) FreeVec(fb);
    big = 0; hist = 0; fb = 0;
    if (P96Base) CloseLibrary(P96Base);
    P96Base = 0;
}

void sys_present(void)
{
    if (!win) return;
    if (mode_ == SYS_AGA) present_aga();
    else present_rtg();
}

int sys_toggle(void)
{
    int was = mode_;
    if (was == SYS_AGA) return 1;            /* nothing to switch to */
    if (was == SYS_RTG_SCREEN && !wb_is_rtg()) return 1;   /* no RTG Workbench for a window */
    close_display();
    if (was == SYS_WINDOW ? open_rtg_screen() : open_rtg_window()) return 1;
    close_display();
    return was == SYS_WINDOW ? open_rtg_window() : open_rtg_screen();
}

struct Window *sys_window(void) { return win; }

const char *sys_mode_name(void)
{
    static char buf[64];
    if (mode_ != SYS_AGA) return mode_ == SYS_RTG_SCREEN ? "RTG screen" : "RTG window";
    strcpy(buf, "AGA screen");
    strcat(buf, why);
    return buf;
}

/* ---- joystick (port 1), read from the chips ---- */

unsigned long sys_joystick(void)
{
    UWORD d = ((volatile struct Custom *)0xDFF000)->joy1dat;
    UBYTE pra = ((volatile struct CIA *)0xBFE001)->ciapra;
    unsigned long b = 0;
    if (d & 0x0002) b |= 8;                              /* right */
    if (d & 0x0200) b |= 4;                              /* left */
    if ((d ^ (d >> 1)) & 0x0001) b |= 2;                 /* down */
    if ((d ^ (d >> 1)) & 0x0100) b |= 1;                 /* up */
    if (!(pra & 0x80)) b |= 0x10;                        /* fire (active low) */
    return b;
}

/* ---- timer ---- */

static struct MsgPort *tport;
static struct timerequest *treq;
struct Device *TimerBase;

int timer_open(void)
{
    tport = CreateMsgPort();
    if (!tport) return 0;
    treq = (struct timerequest *)CreateIORequest(tport, sizeof(struct timerequest));
    if (!treq) return 0;
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)treq, 0) != 0) {
        DeleteIORequest((struct IORequest *)treq);
        treq = 0;
        return 0;
    }
    TimerBase = treq->tr_node.io_Device;
    return 1;
}

void timer_close(void)
{
    if (TimerBase) CloseDevice((struct IORequest *)treq);
    TimerBase = 0;
    if (treq) DeleteIORequest((struct IORequest *)treq);
    if (tport) DeleteMsgPort(tport);
    treq = 0; tport = 0;
}

unsigned long long now_us(void)
{
    struct timeval tv;
    GetSysTime(&tv);
    return (unsigned long long)tv.tv_secs * 1000000ULL + tv.tv_micro;
}

/* ---- files ---- */

void *sys_load(const char *name, long *size, int chip)
{
    char path[96];
    BPTR f;
    long len;
    void *p = 0;
    *size = 0;
    snprintf(path, sizeof(path), "PROGDIR:data/%s", name);
    f = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (!f) return 0;
    Seek(f, 0, OFFSET_END);
    len = Seek(f, 0, OFFSET_BEGINNING);
    if (len > 0 && (p = AllocVec(len, (chip ? MEMF_CHIP : MEMF_ANY) | MEMF_PUBLIC)) != 0) {
        if (Read(f, p, len) != len) { FreeVec(p); p = 0; }
        else *size = len;
    }
    Close(f);
    return p;
}

void sys_free(void *p) { if (p) FreeVec(p); }
