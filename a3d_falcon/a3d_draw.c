// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * a3d_draw.c - the shared engine's drawing (see a3d_draw.h) on the Atari
 * Falcon030. The upstream file (amiga_games examples/amiga3d/a3d_draw.c)
 * with its two platform sections replaced by this one: the 2D, the text,
 * everything from "2D, both" on, the 3D draw list and the models are the
 * upstream code unchanged.
 *
 * The screen is the Falcon's 256-colour mode: eight bitplanes interleaved
 * a word at a time, so 16 pixels are 8 consecutive words, one per plane.
 * The games use pens 0-31 and the sky gradient pens 32-47, so only planes
 * 0-5 are ever written (planes 6 and 7 stay clear): a span is three long
 * stores per 16 pixels, or three masked ones at its ends. Rows come
 * through the canvas's rowoff, which maps the games' 256 rows to the
 * 240-line VGA screen (one row in 16 is dropped: -1).
 *
 * The text is the TOS 8 x 8 system font (line-A), in place of topaz 8.
 */
#include <string.h>
#include <stdlib.h>

#include "a3d.h"
#include "a3d_draw.h"
#include "a3d_falcon.h"
#include "falcon_sys.h"
#include "a3d_fix.h"

/* ---------- the font: the TOS 8 x 8 one (falcon_sys.c) ---------- */
#define glyph a3f_glyph
static int glyph_base = 6;

int a3_draw_init(void)
{
    fsys_font_init();
    return 1;
}

int g_clip_y0 = 0, g_clip_y1 = SCREEN_H - 1;

/* ---------- the pens: three longs of plane words each (falcon_sys.c) ---------- */
#define pat a3f_pat
static void masks(void);
int a3f_sky_pen = -1, a3f_sky_rows;
uint8_t a3f_sky_col[SCREEN_H];          /* the sky pen's pen (32-47) on each row */

void a3f_draw_pens(void)
{
    masks();
}

/* the edge masks, doubled to cover two planes' words: the pixels from x
 * (lmask) or up to x (rmask) of a group, bit 15 left */
static uint32_t lmask[16], rmask[16];

static void masks(void)
{
    int i;
    for (i = 0; i < 16; i++) {
        uint32_t l = 0xFFFFu >> i, r = (0xFFFFu << (15 - i)) & 0xFFFFu;
        lmask[i] = l << 16 | l;
        rmask[i] = r << 16 | r;
    }
}

/* the 16 pixels of group d under doubled mask mm to pen pt */
static inline void group_mask(uint32_t *d, const uint32_t *pt, uint32_t mm)
{
    d[0] = (d[0] & ~mm) | (pt[0] & mm);
    d[1] = (d[1] & ~mm) | (pt[1] & mm);
    d[2] = (d[2] & ~mm) | (pt[2] & mm);
}

/* ---------- primitives ---------- */
void g_span(Canvas *cv, int y, int xl, int xr, int col)
{
    int32_t off = cv->rowoff[y];
    int n;
    uint32_t *d, m;
    const uint32_t *pt;
    if (off < 0) return;
    if (col == a3f_sky_pen && y < a3f_sky_rows) col = a3f_sky_col[y];
    pt = pat[col < 48 ? col : col & 31];
    n = (xr >> 4) - (xl >> 4);
    d = (uint32_t *)(cv->base + off + ((xl >> 4) << 4));
    m = lmask[xl & 15];
    if (n == 0) { group_mask(d, pt, m & rmask[xr & 15]); return; }
    group_mask(d, pt, m);
    d += 4;
    if (--n > 0) {
        uint32_t a = pt[0], b = pt[1], c = pt[2];
        do { d[0] = a; d[1] = b; d[2] = c; d += 4; } while (--n);
    }
    group_mask(d, pt, rmask[xr & 15]);
}

void g_fill(Canvas *cv, int x0, int y0, int x1, int y1, int col)
{
    g_rect(cv, x0, y0, x1, y1, col);
}

void g_sync(void) {}

#define pspan g_span                    /* the drawing's pixels are the games' */


/* ---------- the tall picture: the same format, every row kept ---------- */
static Canvas pic;
static int32_t *pic_rows;
static int pic_h;

Canvas *g_pic_alloc(int h)
{
    int y;
    g_pic_free();
    pic.base = (uint8_t *)calloc((size_t)h, SCREEN_W);
    pic_rows = (int32_t *)malloc(sizeof(int32_t) * (size_t)h);
    if (!pic.base || !pic_rows) { g_pic_free(); return NULL; }
    for (y = 0; y < h; y++) pic_rows[y] = (int32_t)y * SCREEN_W;
    pic.rowoff = pic_rows;
    pic.h = h;
    pic_h = h;
    return &pic;
}

void g_pic_blit(Canvas *cv, int src_y, int dst_y, int rows)
{
    int r, g;
    if (!pic.base || src_y < 0 || src_y + rows > pic_h || rows <= 0) return;
    for (r = 0; r < rows; r++) {
        int32_t off = cv->rowoff[dst_y + r];
        const uint32_t *s;
        uint32_t *d;
        if (off < 0) continue;
        s = (const uint32_t *)(pic.base + (src_y + r) * SCREEN_W);
        d = (uint32_t *)(cv->base + off);
        for (g = 0; g < SCREEN_W / 16; g++, s += 4, d += 4) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; }
    }
}

void g_pic_free(void)
{
    free(pic.base);
    free(pic_rows);
    pic.base = NULL;
    pic_rows = NULL;
    pic_h = 0;
}

/* pictures need full colour: AmigaOS 4 only (the games keep their own look) */
A3Image *g_image_load(const char *path) { (void)path; return NULL; }
void g_image_draw(Canvas *cv, A3Image *im, int x, int y) { (void)cv; (void)im; (void)x; (void)y; }
void g_image_free(A3Image *im) { (void)im; }
void g_image_row(Canvas *cv, A3Image *im, int sy, int x0, int x1, int dx, int dy) { (void)cv; (void)im; (void)sy; (void)x0; (void)x1; (void)dx; (void)dy; }
void g_fill_grad(Canvas *cv, int y0, int y1, uint32_t top, uint32_t bottom) { (void)cv; (void)y0; (void)y1; (void)top; (void)bottom; }
void g_image_wrap(Canvas *cv, A3Image *im, int sx, int top, int y0, int y1) { (void)cv; (void)im; (void)sx; (void)top; (void)y0; (void)y1; }
int g_image_w(const A3Image *im) { (void)im; return 0; }
int g_image_h(const A3Image *im) { (void)im; return 0; }

static void g_glyph(Canvas *cv, int x, int y, int col, int c)
{
    int r, wl = x >> 4, sh = x & 15;
    const uint32_t *pt;
    if (x < 0 || x > SCREEN_W - 8) return;
    for (r = 0; r < 8; r++) {
        int yy = y + r, cc = col;
        uint32_t m = (uint32_t)glyph[c & 255][r] << (24 - sh);
        uint32_t *d;
        int32_t off;
        if (!m || yy < g_clip_y0 || yy > g_clip_y1) continue;
        /* a row the 240-line screen drops shares its screen row with the
         * next: drawn there too, so 8-row text keeps all its strokes (a
         * 0 without its bottom row reads as an A) */
        if ((off = cv->rowoff[yy]) < 0) off = cv->rowoff[yy + 1];
        if (cc == a3f_sky_pen && yy < a3f_sky_rows) cc = a3f_sky_col[yy];
        pt = pat[cc < 48 ? cc : cc & 31];
        d = (uint32_t *)(cv->base + off + wl * 16);
        group_mask(d, pt, (m & 0xFFFF0000UL) | m >> 16);
        if (m & 0xFFFF) group_mask(d + 4, pt, (m & 0xFFFF) << 16 | (m & 0xFFFF));
    }
}

/* ---------- 2D, both ---------- */
const uint8_t *g_glyph_bits(int c) { return glyph[c & 255]; }

void g_rect(Canvas *cv, int x0, int y0, int x1, int y1, int col)
{
    int y;
    if (x0 < 0) x0 = 0;
    if (y0 < g_clip_y0) y0 = g_clip_y0;
    if (x1 > SCREEN_W - 1) x1 = SCREEN_W - 1;
    if (y1 > g_clip_y1) y1 = g_clip_y1;
    if (x0 > x1) return;
#ifdef A3_FALCON
    /* a few rows through g_span, more through a3d_rows.S (not the sky
     * pen's rows: they change pen) */
    if (y1 - y0 >= 3 && !(col == a3f_sky_pen && y0 < a3f_sky_rows)) {
        static int16_t rl[SPAN_MAX], rr[SPAN_MAX];
        static int lx0 = -1, lx1 = -1, ln;
        int n = y1 - y0 + 1;
        if (x0 != lx0 || x1 != lx1 || n > ln) {
            for (y = 0; y < n; y++) { rl[y] = (int16_t)x0; rr[y] = (int16_t)x1; }
            lx0 = x0; lx1 = x1; ln = n;
        }
        a3f_rows(rl, rr, n, cv->rowoff + y0, cv->base, pat[col < 48 ? col : col & 31]);
        return;
    }
#endif
    for (y = y0; y <= y1; y++) g_span(cv, y, x0, x1, col);
}

void g_hline(Canvas *cv, int x0, int x1, int y, int col)
{
    if (y < g_clip_y0 || y > g_clip_y1) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (x0 < 0) x0 = 0;
    if (x1 > SCREEN_W - 1) x1 = SCREEN_W - 1;
    if (x0 <= x1) g_span(cv, y, x0, x1, col);
}

void g_vline(Canvas *cv, int x, int y0, int y1, int col)
{
    int y;
    if (x < 0 || x >= SCREEN_W) return;
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (y0 < g_clip_y0) y0 = g_clip_y0;
    if (y1 > g_clip_y1) y1 = g_clip_y1;
    for (y = y0; y <= y1; y++) g_span(cv, y, x, x, col);
}

static int16_t span_l[SPAN_MAX], span_r[SPAN_MAX];

/* fill a polygon in the drawing's pixels between rows py0..py1; widen
 * (0 or 1): stretch each span and the last row to cover whole pixels of
 * the games' coordinates, as the classic Amiga does */
static void raster(Canvas *cv, const int16_t *xs, const int16_t *ys, int n, int col, int py0, int py1, int widen)
{
#ifdef A3_FALCON
    int ymin, i, rows = a3f_poly_spans(xs, ys, n, span_l, span_r, &ymin), w = PHYS_W - 1;
    if (rows < 0) rows = poly_spans(xs, ys, n, span_l, span_r, &ymin);
#else
    int ymin, i, rows = poly_spans(xs, ys, n, span_l, span_r, &ymin), w = PHYS_W - 1;
#endif
#ifdef __PPC__
    /* the colour once, and each row filled here: a call a row costs too
     * much under emulation */
    uint32_t c = native(a3_lut[col & 31]);
    int sky = col == a3_sky_pen;
    for (i = 0; i < rows + widen; i++) {
        int k = i < rows ? i : rows - 1, xl = span_l[k], xr = span_r[k] + widen, y = ymin + i, n2;
        if (xl > xr || y < py0 || y > py1) continue;
        if (xl < 0) xl = 0;
        if (xr > w) xr = w;
        n2 = xr - xl + 1;
        if (n2 <= 0) continue;
        if (sky && y < a3_sky_rows) { pspan(cv, y, xl, xr, col); continue; }
        if (cv->bpp == 4) {
            uint32_t *d = (uint32_t *)cv->pix + y * cv->w + xl;
            if (n2 < 4) { while (n2-- > 0) *d++ = c; continue; }
            if ((uintptr_t)d & 4) { *d++ = c; n2--; }
            fill_words(d, c, n2);
        } else {
            uint16_t *d = (uint16_t *)cv->pix + y * cv->w + xl;
            if (n2 < 8) { while (n2-- > 0) *d++ = (uint16_t)c; continue; }
            while (((uintptr_t)d & 7) && n2 > 0) { *d++ = (uint16_t)c; n2--; }
            d = (uint16_t *)fill_words((uint32_t *)d, c, n2 >> 1);
            if (n2 & 1) *d = (uint16_t)c;
        }
    }
#elif defined(A3_FALCON)
    /* the rows in a3d_rows.S (a call per row, and C's loop, don't fit the
     * 68030's 256-byte instruction cache); the sky pen's rows here */
    int i0 = py0 - ymin, i1 = py1 - ymin;
    (void)w; (void)widen;               /* widen is A3_SUB - 1: 0 */
    if (i0 < 0) i0 = 0;
    if (i1 > rows - 1) i1 = rows - 1;
    if (col == a3f_sky_pen) {
        for (i = i0; i <= i1 && ymin + i < a3f_sky_rows; i++) {
            int xl = span_l[i], xr = span_r[i];
            if (xl < 0) xl = 0;
            if (xr > SCREEN_W - 1) xr = SCREEN_W - 1;
            if (xl <= xr) g_span(cv, ymin + i, xl, xr, col);
        }
        i0 = i;
    }
    if (i0 <= i1)
        a3f_rows(span_l + i0, span_r + i0, i1 - i0 + 1, cv->rowoff + ymin + i0, cv->base, pat[col < 48 ? col : col & 31]);
#else
    for (i = 0; i < rows + widen; i++) {
        int k = i < rows ? i : rows - 1, xl = span_l[k], xr = span_r[k] + widen, y = ymin + i;
        if (xl > xr || y < py0 || y > py1) continue;
        if (xl < 0) xl = 0;
        if (xr > w) xr = w;
        if (xl <= xr) pspan(cv, y, xl, xr, col);
    }
#endif
}

void g_poly(Canvas *cv, const int16_t *xs, const int16_t *ys, int n, int col, int y0, int y1)
{
#if A3_SUB > 1
    int16_t px[MAX_PV * 2 + 8], py[MAX_PV * 2 + 8];
    int i;
    if (n > MAX_PV * 2 + 8) n = MAX_PV * 2 + 8;
    for (i = 0; i < n; i++) { px[i] = (int16_t)(xs[i] * A3_SUB); py[i] = (int16_t)(ys[i] * A3_SUB); }
    if (y0 < g_clip_y0) y0 = g_clip_y0;
    if (y1 > g_clip_y1) y1 = g_clip_y1;
    raster(cv, px, py, n, col, y0 * A3_SUB, y1 * A3_SUB + A3_SUB - 1, A3_SUB - 1);
#else
    if (y0 < g_clip_y0) y0 = g_clip_y0;
    if (y1 > g_clip_y1) y1 = g_clip_y1;
    raster(cv, xs, ys, n, col, y0, y1, 0);
#endif
}

void g_span_sub(Canvas *cv, int py, int xl, int xr, int col)
{
    if (py < g_clip_y0 * A3_SUB || py > g_clip_y1 * A3_SUB + A3_SUB - 1) return;
    if (xl < 0) xl = 0;
    if (xr > PHYS_W - 1) xr = PHYS_W - 1;
    if (xl <= xr) pspan(cv, py, xl, xr, col);
}

void g_blend_sub(Canvas *cv, int py, int xl, int xr, int col, int alpha)
{
    int x;
    if (py < g_clip_y0 * A3_SUB || py > g_clip_y1 * A3_SUB + A3_SUB - 1) return;
    if (xl < 0) xl = 0;
    if (xr > PHYS_W - 1) xr = PHYS_W - 1;
    if (xl > xr || alpha <= 0) return;
    if (alpha >= 256) { pspan(cv, py, xl, xr, col); return; }
#ifdef __PPC__
    {
        uint32_t c = (col == a3_sky_pen && py < a3_sky_rows) ? a3_sky_col[py] : a3_lut[col & 31];
        int cr = (c >> 16) & 255, cg = (c >> 8) & 255, cb = c & 255, k = 256 - alpha;
        for (x = xl; x <= xr; x++) {
            uint32_t v, o;
            if (cv->bpp == 4) v = to_argb(((uint32_t *)cv->pix)[py * cv->w + x], a3_order);
            else v = to_argb(((uint16_t *)cv->pix)[py * cv->w + x], a3_order);
            o = 0xFF000000UL | (uint32_t)((((v >> 16) & 255) * k + cr * alpha) >> 8) << 16 |
                (uint32_t)((((v >> 8) & 255) * k + cg * alpha) >> 8) << 8 | (uint32_t)(((v & 255) * k + cb * alpha) >> 8);
            o = native(o);
            if (cv->bpp == 4) ((uint32_t *)cv->pix)[py * cv->w + x] = o; else ((uint16_t *)cv->pix)[py * cv->w + x] = (uint16_t)o;
        }
    }
#else
    if (alpha < 64) return;
    if (alpha >= 192) { pspan(cv, py, xl, xr, col); return; }
    for (x = xl + ((xl + py) & 1); x <= xr; x += 2) pspan(cv, py, x, x, col);
#endif
}

void g_poly_sub(Canvas *cv, const int16_t *xs, const int16_t *ys, int n, int col)
{
    raster(cv, xs, ys, n, col, g_clip_y0 * A3_SUB, g_clip_y1 * A3_SUB + A3_SUB - 1, 0);
}

void g_circle(Canvas *cv, int cx, int cy, int r, int col)
{
    int dy, R = r * A3_SUB + (A3_SUB - 1) / 2, PX = cx * A3_SUB + A3_SUB / 2, PY = cy * A3_SUB + A3_SUB / 2;
    int py0 = g_clip_y0 * A3_SUB, py1 = g_clip_y1 * A3_SUB + A3_SUB - 1;
    for (dy = -R; dy <= R; dy++) {
        int w = (int)a3_isqrt((uint32_t)(R * R - dy * dy) + (uint32_t)R / 2), xl = PX - w, xr = PX + w, y = PY + dy;
        if (y < py0 || y > py1) continue;
        if (xl < 0) xl = 0;
        if (xr > PHYS_W - 1) xr = PHYS_W - 1;
        if (xl <= xr) pspan(cv, y, xl, xr, col);
    }
}

void g_text(Canvas *cv, int x, int y, int col, const char *s)
{
    for (; *s; s++, x += 8) g_glyph(cv, x, y - glyph_base, col, (unsigned char)*s);
}

void g_text_shadow(Canvas *cv, int x, int y, int col, const char *s)
{
    g_text(cv, x + 1, y + 1, 0, s);
    g_text(cv, x, y, col, s);
}

void g_text_centre(Canvas *cv, int y, int col, const char *s)
{
    g_text_shadow(cv, (SCREEN_W - (int)strlen(s) * 8) / 2, y, col, s);
}

void g_text_big(Canvas *cv, int x, int y, int col, const char *s, int sc)
{
    int r, b;
#ifdef __PPC__
    {
        /* the 16 x 16 font, each of its pixels sc x sc */
        const char *p;
        int xx = x, all = 1;
        for (p = s; *p; p++) if ((unsigned char)*p < 32 || (unsigned char)*p > 126) all = 0;
        if (all) {
            for (p = s; *p; p++, xx += 8 * sc) glyph16(cv, xx, y, col, (unsigned char)*p, sc);
            return;
        }
    }
#endif
    for (; *s; s++, x += 8 * sc)
        for (r = 0; r < 8; r++) {
            uint8_t bits = glyph[(unsigned char)*s][r];
            for (b = 0; b < 8; b++) {
                int e = b;
                if (!(bits & (0x80 >> b))) continue;
                while (e < 7 && (bits & (0x80 >> (e + 1)))) e++;      /* a run of set pixels */
                g_rect(cv, x + b * sc, y + r * sc, x + e * sc + sc - 1, y + r * sc + sc - 1, col);
                b = e;
            }
        }
}

void g_box(Canvas *cv, int x0, int y0, int x1, int y1, int fill, int edge)
{
    g_rect(cv, x0, y0, x1, y1, fill);
    g_rect(cv, x0, y0, x1, y0, edge);
    g_rect(cv, x0, y1, x1, y1, edge);
    g_rect(cv, x0, y0, x0, y1, edge);
    g_rect(cv, x1, y0, x1, y1, edge);
}

/* ---------- the 3D draw list ---------- */
typedef struct {
    int16_t next;
    uint8_t n, col;
    int16_t x[MAX_PV], y[MAX_PV];
} DPoly;

#if A3_SUB > 1
#define MAX_DPOLY 4000                  /* AmigaOS 4 draws further */
#else
#define MAX_DPOLY 1400
#endif
#define NBUCKET   512
static DPoly pool[MAX_DPOLY];
static int16_t bhead[NBUCKET], btail[NBUCKET];
static int32_t far_z = 4096;
int32_t a3_npolys, a3_dropped;

void a3_begin(int32_t fz)
{
    int i;
    a3_npolys = 0;
    far_z = fz > 0 ? fz : 1;
    for (i = 0; i < NBUCKET; i++) bhead[i] = btail[i] = -1;
}

static int bucket_of(int32_t depth)
{
#ifdef A3_FALCON
    /* 32 bits when it fits: libgcc's 64-bit divide is slow on a 68030 */
    int32_t b = depth < 0 ? 0 : depth >= far_z ? NBUCKET - 1 :
                depth < 0x400000 ? (depth * NBUCKET) / far_z : (int32_t)(((int64_t)depth * NBUCKET) / far_z);
#else
    int32_t b = depth < 0 ? 0 : (depth >= far_z ? NBUCKET - 1 : (int32_t)(((int64_t)depth * NBUCKET) / far_z));
#endif
    if (b < 0) b = 0;
    if (b >= NBUCKET) b = NBUCKET - 1;
    return (int)b;
}

void a3_poly(const Cam *c, int32_t depth, int col, V3 *cv, int n)
{
    V3 cl[MAX_PV];
    int32_t xs[MAX_PV], ys[MAX_PV];
    DPoly *p;
    int i, b;

    n = clip_near(cv, n, cl);
    if (n < 3) return;
    for (i = 0; i < n; i++) {
#if A3_SUB > 1
        /* x * fov fits 32 bits (as cam_project has it); the finer step comes
         * from the remainder (a 64-bit divide is a slow library call here) */
        int32_t qx = cl[i].x * c->fov, qy = cl[i].y * c->fov, z = cl[i].z;
        xs[i] = c->cx * A3_SUB + (qx / z) * A3_SUB + ((qx % z) * A3_SUB) / z;
        ys[i] = c->cyc * A3_SUB - ((qy / z) * A3_SUB + ((qy % z) * A3_SUB) / z);
#else
        cam_project(c, &cl[i], &xs[i], &ys[i]);
#endif
    }
    n = clip_rect(xs, ys, n, 0, 0, c->vw * A3_SUB - 1, c->vh * A3_SUB - 1);
    if (n < 3) return;
    if (a3_npolys >= MAX_DPOLY) { a3_dropped++; return; }
    p = &pool[a3_npolys];
    p->n = (uint8_t)n;
    p->col = (uint8_t)col;
    p->next = -1;
    for (i = 0; i < n; i++) { p->x[i] = (int16_t)xs[i]; p->y[i] = (int16_t)ys[i]; }
    b = bucket_of(depth);
    if (btail[b] >= 0) pool[btail[b]].next = (int16_t)a3_npolys; else bhead[b] = (int16_t)a3_npolys;
    btail[b] = (int16_t)a3_npolys;
    a3_npolys++;
}

void a3_quad(const Cam *c, int32_t depth, int col, V3 a, V3 b, V3 d, V3 e)
{
    V3 q[4];
    if (a.z < NEAR_Z && b.z < NEAR_Z && d.z < NEAR_Z && e.z < NEAR_Z) return;
    if (a.x > a.z && b.x > b.z && d.x > d.z && e.x > e.z) return;
    if (a.x < -a.z && b.x < -b.z && d.x < -d.z && e.x < -e.z) return;
    q[0] = a; q[1] = b; q[2] = d; q[3] = e;
    a3_poly(c, depth, col, q, 4);
}

void a3_flush(Canvas *cv)
{
    int b, k;
    for (b = NBUCKET - 1; b >= 0; b--)
        for (k = bhead[b]; k >= 0; k = pool[k].next) {
            DPoly *p = &pool[k];
            raster(cv, p->x, p->y, p->n, p->col, 0, PHYS_H - 1, 0);
        }
}

/* ---------- models ---------- */
void mdl_begin(Model *m) { m->nv = 0; m->nf = 0; }

int mdl_vtx(Model *m, int x, int y, int z)
{
    int i = m->nv;
    if (i >= MDL_MAXV) return MDL_MAXV - 1;
    m->nv++;
    m->v[i][0] = (int16_t)x;
    m->v[i][1] = (int16_t)y;
    m->v[i][2] = (int16_t)z;
    return i;
}

/* Add a face; flip it if it faces the interior point (ix, iy, iz); shade
 * it by a fixed sun high in the north-west (-1, 2, 1). */
void mdl_face(Model *m, int lit, int shade, int n, int a, int b, int c, int d, int ix, int iy, int iz)
{
    Face *f;
    int16_t *A = m->v[a], *B = m->v[b], *C = m->v[c];
    int32_t ux = B[0] - A[0], uy = B[1] - A[1], uz = B[2] - A[2];
    int32_t vx = C[0] - A[0], vy = C[1] - A[1], vz = C[2] - A[2];
    int32_t nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    int32_t light, mag;
    if (m->nf >= MDL_MAXF) return;
    f = &m->f[m->nf++];
    f->n = (uint8_t)n;
    if (nx * (A[0] - ix) + ny * (A[1] - iy) + nz * (A[2] - iz) >= 0) {
        f->idx[0] = (uint8_t)a; f->idx[1] = (uint8_t)b; f->idx[2] = (uint8_t)c; f->idx[3] = (uint8_t)d;
    } else {
        if (n == 4) { f->idx[0] = (uint8_t)d; f->idx[1] = (uint8_t)c; f->idx[2] = (uint8_t)b; f->idx[3] = (uint8_t)a; }
        else        { f->idx[0] = (uint8_t)c; f->idx[1] = (uint8_t)b; f->idx[2] = (uint8_t)a; f->idx[3] = 0; }
        nx = -nx; ny = -ny; nz = -nz;
    }
    light = -nx + 2 * ny + nz;
    mag = (nx < 0 ? -nx : nx) + (ny < 0 ? -ny : ny) + (nz < 0 ? -nz : nz);
    f->col = (uint8_t)((mag && light * 4 > mag * 3) ? lit : shade);
}

void mdl_box(Model *m, int lit, int shade, int x0, int x1, int y0, int y1, int z0, int z1)
{
    int b = m->nv, cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, cz = (z0 + z1) / 2;
    mdl_vtx(m, x0, y0, z0); mdl_vtx(m, x1, y0, z0); mdl_vtx(m, x1, y1, z0); mdl_vtx(m, x0, y1, z0);
    mdl_vtx(m, x0, y0, z1); mdl_vtx(m, x1, y0, z1); mdl_vtx(m, x1, y1, z1); mdl_vtx(m, x0, y1, z1);
    mdl_face(m, lit, shade, 4, b + 0, b + 3, b + 2, b + 1, cx, cy, cz);
    mdl_face(m, lit, shade, 4, b + 5, b + 6, b + 7, b + 4, cx, cy, cz);
    mdl_face(m, lit, shade, 4, b + 4, b + 7, b + 3, b + 0, cx, cy, cz);
    mdl_face(m, lit, shade, 4, b + 1, b + 2, b + 6, b + 5, cx, cy, cz);
    mdl_face(m, lit, shade, 4, b + 3, b + 7, b + 6, b + 2, cx, cy, cz);
    mdl_face(m, lit, shade, 4, b + 0, b + 1, b + 5, b + 4, cx, cy, cz);
}

void mdl_pyramid(Model *m, int lit, int shade, int x0, int x1, int y0, int z0, int z1, int apex)
{
    int b = m->nv, t, cx = (x0 + x1) / 2, cz = (z0 + z1) / 2, cy = y0 + (apex - y0) / 4;
    mdl_vtx(m, x0, y0, z0); mdl_vtx(m, x1, y0, z0); mdl_vtx(m, x1, y0, z1); mdl_vtx(m, x0, y0, z1);
    t = mdl_vtx(m, cx, apex, cz);
    mdl_face(m, lit, shade, 3, b + 0, b + 1, t, 0, cx, cy, cz);
    mdl_face(m, lit, shade, 3, b + 1, b + 2, t, 0, cx, cy, cz);
    mdl_face(m, lit, shade, 3, b + 2, b + 3, t, 0, cx, cy, cz);
    mdl_face(m, lit, shade, 3, b + 3, b + 0, t, 0, cx, cy, cz);
}

void mdl_prism(Model *m, int lit, int shade, int sides, int r, int y0, int y1)
{
    int b = m->nv, k, cy = (y0 + y1) / 2;
    for (k = 0; k < sides; k++) {
        int a = (k * 65536) / sides;
        int x = (int)((a3_sin(a) * r) >> 16), z = (int)((a3_cos(a) * r) >> 16);
        mdl_vtx(m, x, y0, z);
        mdl_vtx(m, x, y1, z);
    }
    for (k = 0; k < sides; k++) {
        int j = (k + 1) % sides;
        mdl_face(m, lit, shade, 4, b + 2 * k, b + 2 * k + 1, b + 2 * j + 1, b + 2 * j, 0, cy, 0);
    }
    /* the top, as a fan of quads/triangles round the middle */
    {
        int c = mdl_vtx(m, 0, y1, 0);
        for (k = 0; k < sides; k++) {
            int j = (k + 1) % sides;
            mdl_face(m, lit, lit, 3, b + 2 * k + 1, b + 2 * j + 1, c, 0, 0, y0, 0);
        }
    }
}

void mdl_shadow(Model *m, int col, int x0, int x1, int z0, int z1)
{
    int b = m->nv;
    mdl_vtx(m, x0, 1, z0); mdl_vtx(m, x1, 1, z0); mdl_vtx(m, x1, 1, z1); mdl_vtx(m, x0, 1, z1);
    mdl_face(m, col, col, 4, b + 0, b + 1, b + 2, b + 3, 0, -10, 0);
}

void a3_rot_yaw(int32_t m[9], int yaw)
{
    int32_t c = a3_cos(yaw), s = a3_sin(yaw);
    m[0] = c;  m[1] = 0;     m[2] = s;
    m[3] = 0;  m[4] = 65536; m[5] = 0;
    m[6] = -s; m[7] = 0;     m[8] = c;
}

/* roll about z, then pitch about x, then yaw about y */
void a3_rot_ypr(int32_t m[9], int yaw, int pitch, int roll)
{
    int32_t cy = a3_cos(yaw) >> 4, sy = a3_sin(yaw) >> 4, cp = a3_cos(pitch) >> 4, sp = a3_sin(pitch) >> 4;
    int32_t cr = a3_cos(roll) >> 4, sr = a3_sin(roll) >> 4;
    /* R = Ry * Rx * Rz, each term 12.12 -> 16.16 */
    int32_t a00 = cr, a01 = -sr, a10 = (cp * sr) >> 12, a11 = (cp * cr) >> 12, a12 = -sp;
    int32_t a20 = (sp * sr) >> 12, a21 = (sp * cr) >> 12, a22 = cp;
    m[0] = (cy * a00 + sy * a20) >> 8;  m[1] = (cy * a01 + sy * a21) >> 8;  m[2] = (sy * a22) >> 8;
    m[3] = a10 << 4;                     m[4] = a11 << 4;                     m[5] = a12 << 4;
    m[6] = (-sy * a00 + cy * a20) >> 8; m[7] = (-sy * a01 + cy * a21) >> 8; m[8] = (cy * a22) >> 8;
}

void a3_model(const Cam *c, const Model *m, int32_t wx, int32_t wy, int32_t wz, const int32_t *rot,
              const uint8_t *body, int flash, int32_t depth_bias)
{
    V3 cv[MDL_MAXV], ctr;
    int32_t vis_depth[MDL_MAXF], depth;
    uint8_t vis_face[MDL_MAXF];
    int i, k, nvis = 0;

    cam_xform(c, wx, wy, wz, &ctr);
    if (ctr.z < -800 || ctr.x > ctr.z + 800 || ctr.x < -ctr.z - 800) return;
    for (i = 0; i < m->nv; i++) {
        int32_t x = m->v[i][0], y = m->v[i][1], z = m->v[i][2];
        if (rot) {
            int32_t rx = (x * rot[0] + y * rot[1] + z * rot[2]) >> 16;
            int32_t ry = (x * rot[3] + y * rot[4] + z * rot[5]) >> 16;
            int32_t rz = (x * rot[6] + y * rot[7] + z * rot[8]) >> 16;
            x = rx; y = ry; z = rz;
        }
        cam_xform(c, wx + x, wy + y, wz + z, &cv[i]);
    }
    /* the faces that face the camera, farthest first (a model made of
     * several parts isn't convex, so culling alone doesn't order them) */
    for (k = 0; k < m->nf; k++) {
        const Face *f = &m->f[k];
        const V3 *a = &cv[f->idx[0]], *b = &cv[f->idx[1]], *d = &cv[f->idx[2]];
        int32_t ux = b->x - a->x, uy = b->y - a->y, uz = b->z - a->z;
        int32_t vx = d->x - a->x, vy = d->y - a->y, vz = d->z - a->z;
        int32_t nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
#ifdef A3_FALCON
        if (a3f_lmul(nx, a->x) + a3f_lmul(ny, a->y) + a3f_lmul(nz, a->z) >= 0) continue;
#else
        if ((int64_t)nx * a->x + (int64_t)ny * a->y + (int64_t)nz * a->z >= 0) continue;
#endif
        depth = a->z + b->z + d->z + (f->n == 4 ? cv[f->idx[3]].z : d->z);
        if (m->v[f->idx[0]][1] == 1 && m->v[f->idx[1]][1] == 1 && m->v[f->idx[2]][1] == 1)
            depth = 0x7FFFFFFF;                 /* a shadow on the ground goes first */
        for (i = nvis; i > 0 && vis_depth[i - 1] < depth; i--) {
            vis_face[i] = vis_face[i - 1];
            vis_depth[i] = vis_depth[i - 1];
        }
        vis_face[i] = (uint8_t)k;
        vis_depth[i] = depth;
        nvis++;
    }
    depth = ctr.z - depth_bias;
    for (i = 0; i < nvis; i++) {
        const Face *f = &m->f[vis_face[i]];
        V3 q[4];
        int col = f->col;
        q[0] = cv[f->idx[0]]; q[1] = cv[f->idx[1]]; q[2] = cv[f->idx[2]];
        if (f->n == 4) q[3] = cv[f->idx[3]];
        if (col >= COL_FLASH) col = flash;
        else if (col >= COL_BODY) col = body ? body[col - COL_BODY] : 3;
        a3_poly(c, depth, col, q, f->n);
    }
}
