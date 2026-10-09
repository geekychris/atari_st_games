/*
 * The room on screen, as a list of cels (3DO headers only).
 *
 *   background   the floor and back walls, drawn once per room (vox.c)
 *   items        blocks, spikes, gates, the throne, lifts, pickups, crates
 *                and characters: pre-rendered sprites, drawn back to front
 *   arches       the front doorways, always in front
 *   flash, panel full-screen colour flash and the status panel's shade:
 *                1x1 cels whose pixel processor mixes with the frame buffer
 *
 * Draw order: an item must come after every item that is behind it and
 * overlaps it on screen. "Behind" for two boxes is any separating axis
 * that puts one further from the camera (+x, +z) or below the other.
 * Static pairs are worked out at room entry; the few moving items are
 * linked in each frame, then a depth-first walk emits the cels.
 *
 * AmigaOS 4: the cel engine is replaced by a software compositor into a
 * 320 x 240 32-bit frame (scene_fb). Every sprite is drawn 1:1 at whole
 * pixels anyway, so this is plain blitting: the room's background is
 * composed once per room and copied each frame, then the items are drawn
 * over it in the same order as before. The pixel-processor effects
 * (shadows, the panel's shade, the flash) are done per pixel.
 *
 * The frame is A R G B on AmigaOS 4, and 15-bit RGB (the sprites' own
 * format, so no conversion) on the classic 68k build (SCENE_RGB15).
 */
#include <string.h>
#include <stdlib.h>
#include "keep.h"

#define MAXITEMS  160
#define MAXEDGES  1400
#define MAXCELS   260

int scene_ox, scene_oy, scene_ncels;

#define SW 320
#define SH 240
#if defined(SCENE_RGB565)
/* Atari Falcon port: the Falcon's 16 bit true colour, RRRRRGGGGGGBBBBB,
 * composed straight into the screen; the sprites' 15-bit pixels are
 * converted as they are drawn.  Channels are kept at 5 bits (green's
 * low bit 0), so the effects below work as with SCENE_RGB15. */
typedef unsigned short Pix;
#define PIX_OF(c)    ((Pix)((((c) << 1) & 0xFFC0) | ((c) & 0x1F)))
#define PIX_HALF(v)  ((Pix)(((v) >> 1) & 0x7BDF))
#define PIX_R(v)     (((v) >> 11) & 31)
#define PIX_G(v)     (((v) >> 6) & 31)
#define PIX_B(v)     ((v) & 31)
#define PIX_RGB(r, g, b) ((Pix)(((r) << 11) | ((g) << 6) | (b)))
#define PIX_MAX      31
#elif defined(SCENE_RGB15)
typedef unsigned short Pix;
#define PIX_OF(c)    ((Pix)(c))
#define PIX_HALF(v)  ((Pix)(((v) >> 1) & 0x3DEF))
#define PIX_R(v)     (((v) >> 10) & 31)
#define PIX_G(v)     (((v) >> 5) & 31)
#define PIX_B(v)     ((v) & 31)
#define PIX_RGB(r, g, b) ((Pix)(((r) << 10) | ((g) << 5) | (b)))
#define PIX_MAX      31
#else
typedef unsigned long Pix;
#define PIX_OF(c)    rgb_of[(c) & 0x7FFF]
#define PIX_HALF(v)  (0xFF000000UL | (((v) >> 1) & 0x7F7F7FUL))
#define PIX_R(v)     (((v) >> 16) & 255)
#define PIX_G(v)     (((v) >> 8) & 255)
#define PIX_B(v)     ((v) & 255)
#define PIX_RGB(r, g, b) (0xFF000000UL | ((unsigned long)(r) << 16) | ((unsigned long)(g) << 8) | (unsigned long)(b))
#define PIX_MAX      255
static unsigned long rgb_of[32768];   /* 3DO 15-bit RGB -> A R G B */
#endif
Pix *scene_fb;                        /* the main program's frame, SW x SH */
static Pix bgbuf[SW * SH];            /* the room's floor and walls, made once per room */

#ifdef SCENE_RGB565
/*
 * Atari Falcon port: the frame is one of the Falcon layer's three
 * screens, so instead of copying the whole room into it every frame,
 * each screen keeps the rectangles drawn into it (sprites here, the HUD
 * through scene_mark) and the next frame on that screen restores only
 * those from the room.  A new room, a full-screen effect or too many
 * rectangles mean a full copy, as before.
 */
#define MAXDIRTY 300
typedef struct { short x0, y0, x1, y1; } DRect;
static struct DBuf { Pix *fb; long gen; int full, nd; DRect d[MAXDIRTY]; } dbuf[3];
static long room_gen = 1;

static struct DBuf *dbuf_of(Pix *fb)
{
    int i;
    for (i = 0; i < 3; i++)
        if (dbuf[i].fb == fb) return &dbuf[i];
    for (i = 0; i < 3; i++)
        if (!dbuf[i].fb) { dbuf[i].fb = fb; dbuf[i].full = 1; return &dbuf[i]; }
    return 0;
}

/* a rectangle drawn into the current frame (scene_fb) */
void scene_mark(int x0, int y0, int x1, int y1)
{
    struct DBuf *b = dbuf_of(scene_fb);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SW - 1) x1 = SW - 1;
    if (y1 > SH - 1) y1 = SH - 1;
    if (!b || x0 > x1 || y0 > y1) return;
    if (b->nd == MAXDIRTY) { b->full = 1; return; }
    b->d[b->nd].x0 = (short)x0;
    b->d[b->nd].y0 = (short)y0;
    b->d[b->nd].x1 = (short)x1;
    b->d[b->nd].y1 = (short)y1;
    b->nd++;
}

static void copy_rows(Pix *dst, const Pix *src, long n)	/* n pixels, 8-pixel multiple */
{
    const Pix *end = src + n;
    __asm__ volatile (
        "1:	movem.l	(%0)+,%%d0-%%d3\n"
        "	movem.l	%%d0-%%d3,(%1)\n"
        "	lea	16(%1),%1\n"
        "	cmp.l	%2,%0\n"
        "	blt.s	1b\n"
        : "+a" (src), "+a" (dst) : "a" (end) : "d0", "d1", "d2", "d3", "cc", "memory");
}

/* the frame back to the room: in full, or what was drawn into it */
static void restore_frame(void)
{
    struct DBuf *b = dbuf_of(scene_fb);
    int i, y;
    if (!b || b->full || b->gen != room_gen) {
        copy_rows(scene_fb, bgbuf, (long)SW * SH);
    } else {
        for (i = 0; i < b->nd; i++) {
            const DRect *r = &b->d[i];
            int w = r->x1 - r->x0 + 1;
            for (y = r->y0; y <= r->y1; y++)
                memcpy(scene_fb + y * SW + r->x0, bgbuf + y * SW + r->x0, w * sizeof(Pix));
        }
    }
    if (b) {
        b->nd = 0;
        b->full = 0;
        b->gen = room_gen;
    }
}
#endif
static int npool;

enum { I_BLOCK, I_SPIKES, I_GATE, I_THRONE, I_ACTOR, I_LIFT, I_PICKUP };
typedef struct {
    Box b;
    short x0, y0, x1, y1;           /* screen rectangle */
    long key;                       /* depth of the centre: bigger = further */
    short kind, idx, var;
    short cx, cy, cz;               /* static items: the cell */
    short head;                     /* static edges: first */
    short dhead;                    /* this frame's edges: first */
    unsigned char mark;
} DItem;
static DItem item[MAXITEMS];
static int nstatic, nitem;
typedef struct { short from, next; } Edge;      /* `from` is drawn before the list's owner */
static Edge sedge[MAXEDGES], dedge[MAXEDGES];
static int nsedge, ndedge;

/* ---- blitting ---- */

/* the 3DO pixel-processor modes this game uses */
#define PIXC_SHADOW 1               /* fb * 4/8 under the sprite's shape */

static void forget_cel(Sprite *s) { s->ccb = 0; }

static void blit(Pix *dst, Sprite *s, long x, long y, int pixc)
{
    int w = s->w, h = s->h, i, j, i0 = 0, j0 = 0, i1 = w, j1 = h;
    if (!s->pix || w <= 0) return;
    if (x < 0) i0 = (int)-x;
    if (y < 0) j0 = (int)-y;
    if (x + w > SW) i1 = (int)(SW - x);
    if (y + h > SH) j1 = (int)(SH - y);
    for (j = j0; j < j1; j++) {
        const unsigned short *src = s->pix + j * w;
        Pix *row = dst + (y + j) * SW + x;
        if (pixc == PIXC_SHADOW) {
            for (i = i0; i < i1; i++)
                if (src[i]) row[i] = PIX_HALF(row[i]);
        } else {
            for (i = i0; i < i1; i++)
                if (src[i]) row[i] = PIX_OF(src[i]);
        }
    }
}

/* sprite with its model origin at screen pixel (x, y) */
static void emit(Sprite *s, long x, long y, int pixc)
{
    if (!scene_fb) return;
    blit(scene_fb, s, x - s->ax, y - s->ay, pixc);
#ifdef SCENE_RGB565
    if (s->pix && s->w > 0)
        scene_mark((int)(x - s->ax), (int)(y - s->ay), (int)(x - s->ax + s->w - 1), (int)(y - s->ay + s->h - 1));
#endif
    npool++;
}

/* darken a band to k/8 (the status panel: 3/8) */
#ifdef SCENE_RGB565
/* Atari Falcon port: the same per-channel k/8 from a table of all 65536
 * pixel values, made for the k in use */
static Pix *shade_tab;
static int shade_k = -1;

static void shade_rect(int y0, int y1, int k)
{
    Pix *p = scene_fb + y0 * SW, *e = scene_fb + (y1 + 1) * SW;
    if (k != shade_k) {
        unsigned long v;
        if (!shade_tab && !(shade_tab = (Pix *)malloc(65536L * sizeof(Pix))))
            return;
        for (v = 0; v < 65536; v++)
            shade_tab[v] = PIX_RGB(PIX_R(v) * k >> 3, PIX_G(v) * k >> 3, PIX_B(v) * k >> 3);
        shade_k = k;
    }
    for (; p < e; p++)
        *p = shade_tab[*p];
    scene_mark(0, y0, SW - 1, y1);
}
#else
static void shade_rect(int y0, int y1, int k)
{
    Pix *p = scene_fb + y0 * SW, *e = scene_fb + (y1 + 1) * SW;
    for (; p < e; p++) {
        Pix v = *p;
        *p = PIX_RGB(PIX_R(v) * k >> 3, PIX_G(v) * k >> 3, PIX_B(v) * k >> 3);
    }
}
#endif

/* fb * 7/8 + colour / 2, the colour being a third of the ink: a flash */
static void flash(unsigned short q)
{
#ifdef SCENE_RGB565
    scene_mark(0, 0, SW - 1, SH - 1);
#endif
    Pix c = PIX_OF(q);
    unsigned long cr = PIX_R(c) >> 1, cg = PIX_G(c) >> 1, cb = PIX_B(c) >> 1;
    Pix *p = scene_fb, *e = scene_fb + SW * SH;
    for (; p < e; p++) {
        Pix v = *p;
        unsigned long r, g, b;
        r = (PIX_R(v) * 7 >> 3) + cr; if (r > PIX_MAX) r = PIX_MAX;
        g = (PIX_G(v) * 7 >> 3) + cg; if (g > PIX_MAX) g = PIX_MAX;
        b = (PIX_B(v) * 7 >> 3) + cb; if (b > PIX_MAX) b = PIX_MAX;
        *p = PIX_RGB(r, g, b);
    }
}

void scene_init(void)
{
#if !defined(SCENE_RGB15) && !defined(SCENE_RGB565)
    int i;
    for (i = 0; i < 32768; i++) {
        int r = (i >> 10) & 31, g = (i >> 5) & 31, b = i & 31;
        rgb_of[i] = 0xFF000000UL | ((unsigned long)((r << 3) | (r >> 2)) << 16) |
                    ((unsigned long)((g << 3) | (g >> 2)) << 8) | (unsigned long)((b << 3) | (b >> 2));
    }
#endif
}

/* ---- projection ---- */

static long sx_of(fix x, fix z) { return scene_ox + (((x - z) * ISO_X) >> 12); }
static long sy_of(fix x, fix y, fix z) { return scene_oy - ((y * ISO_Y + (x + z) * ISO_Z) >> 12); }

static void set_box(DItem *it, fix x0, fix y0, fix z0, fix x1, fix y1, fix z1)
{
    it->b.x0 = x0; it->b.y0 = y0; it->b.z0 = z0;
    it->b.x1 = x1; it->b.y1 = y1; it->b.z1 = z1;
    it->x0 = (short)(sx_of(x0, z1) - 2);
    it->x1 = (short)(sx_of(x1, z0) + 2);
    it->y0 = (short)(sy_of(x1, y1, z1) - 2);
    it->y1 = (short)(sy_of(x0, y0, z0) + 2);
    it->key = ((x0 + x1 + z0 + z1) * 17 - (y0 + y1) * 14) >> 1;
}

static int rects_meet(const DItem *a, const DItem *b)
{
    return a->x0 <= b->x1 && b->x0 <= a->x1 && a->y0 <= b->y1 && b->y0 <= a->y1;
}

/* 1: a before b, -1: b before a, 0: either */
static int order(const DItem *a, const DItem *b)
{
    const fix e = 4;
    int ab = a->b.x0 >= b->b.x1 - e || a->b.z0 >= b->b.z1 - e || a->b.y1 <= b->b.y0 + e;
    int ba = b->b.x0 >= a->b.x1 - e || b->b.z0 >= a->b.z1 - e || b->b.y1 <= a->b.y0 + e;
    if (ab && !ba) return 1;
    if (ba && !ab) return -1;
    if (a->key != b->key) return a->key > b->key ? 1 : -1;
    return 0;
}

static void add_edge(Edge *pool_, int *n, short *head, int from)
{
    if (*n >= MAXEDGES) return;
    pool_[*n].from = (short)from;
    pool_[*n].next = *head;
    *head = (short)(*n)++;
}

/* ---- a new room ---- */

static void bg_add(Sprite *s, long x, long y) { blit(bgbuf, s, x - s->ax, y - s->ay, 0); }

#define BX(x, z) (scene_ox + ISO_X * ((x) - (z)))
#define BY(x, y, z) (scene_oy - ISO_Y * (y) - ISO_Z * ((x) + (z)))

/* the floor and back walls, back to front: walls by distance (x + z) then
 * height, the corner post first; then the door steps; then the floor */
static void background(void)
{
    const KRoom *r = R.room;
    int x, y, z, k, door[2], lo[2], hi[2];
    Pix bgc = PIX_OF(((18 >> 3) << 10) | ((19 >> 3) << 5) | (26 >> 3));   /* the camera's (0.07, 0.075, 0.1) */
    for (k = 0; k < SW * SH; k++) bgbuf[k] = bgc;
    door[0] = lv_has_door(R.level, R.ridx, SIDE_N);
    door[1] = lv_has_door(R.level, R.ridx, SIDE_E);
    door_span(r->w, &lo[0], &hi[0]);
    door_span(r->d, &lo[1], &hi[1]);
    for (k = r->w + r->d; k >= 0; k--)
        for (y = 0; y < 3; y++) {
            if (k == r->w + r->d) bg_add(&spr_brick[VARIANTS / 2], BX(r->w, r->d), BY(r->w, y, r->d));
            x = k - r->d;                                   /* north wall: (x, y, d) */
            if (x >= 0 && x < r->w && !(door[0] && x >= lo[0] && x < hi[0] && y < 2))
                bg_add(&spr_brick[brick_variant(x, y, 0)], BX(x, r->d), BY(x, y, r->d));
            z = k - r->w;                                   /* east wall: (w, y, z) */
            if (z >= 0 && z < r->d && !(door[1] && z >= lo[1] && z < hi[1] && y < 2))
                bg_add(&spr_brick[brick_variant(z, y, 1)], BX(r->w, z), BY(r->w, y, z));
        }
    if (door[0]) bg_add(&spr_step[0], scene_ox, scene_oy);
    if (door[1]) bg_add(&spr_step[1], scene_ox, scene_oy);
    for (k = r->w + r->d - 2; k >= 0; k--)
        for (x = 0; x < r->w; x++) {
            z = k - x;
            if (z >= 0 && z < r->d) bg_add(&spr_tile[tile_variant(x, z)], BX(x, z), BY(x, 0, z));
        }
}

static void add_static(int kind, int x, int y, int z, int var)
{
    DItem *it;
    fix X = (fix)x << 12, Y = (fix)y << 12, Z = (fix)z << 12;
    if (nstatic >= MAXITEMS - MAXACT - MAXLIFT - MAXPICK) return;
    it = &item[nstatic++];
    it->kind = (short)kind;
    it->var = (short)var;
    it->idx = 0;
    switch (kind) {
    case I_SPIKES: set_box(it, X, Y, Z, X + FX, Y + 1843, Z + FX); break;
    case I_GATE:   set_box(it, X, Y, Z, X + FX, Y + 2 * FX, Z + FX); break;
    case I_THRONE: set_box(it, X + 410, Y, Z + 614, X + 3686, Y + 6800, Z + 3564); break;
    default:       set_box(it, X, Y, Z, X + FX, Y + FX, Z + FX); break;
    }
    it->cx = (short)x;
    it->cy = (short)y;
    it->cz = (short)z;
}

void scene_room(void)
{
    const KRoom *r = R.room;
    int x, y, z, i, j;
#ifdef SCENE_RGB565
    room_gen++;                     /* every screen gets the new room in full */
#endif
    /* frame the room above the panel (Game.FrameRoom) */
    scene_ox = 160 + ISO_X * (r->d - r->w) / 2;
    scene_oy = 100 + (3 * ISO_Y + ISO_Z * (r->w + r->d + 2)) / 2;
    for (i = 0; i < 2; i++)
        for (j = 0; j < BLOCK_VARIANTS; j++) forget_cel(&spr_block[i][j]);
    forget_cel(&spr_arch[0]);
    forget_cel(&spr_arch[1]);
    for (i = 0; i < VARIANTS; i++) { forget_cel(&spr_tile[i]); forget_cel(&spr_brick[i]); }
    forget_cel(&spr_step[0]);
    forget_cel(&spr_step[1]);
    vox_room(r);
    background();

    nstatic = 0;
    for (y = 0; y < MAXH; y++)
        for (z = 0; z < r->d; z++)
            for (x = 0; x < r->w; x++)
                switch (CELL(r, x, y, z)) {
                case '#': add_static(I_BLOCK, x, y, z, block_variant(x, y, z)); break;
                case 'B': add_static(I_BLOCK, x, y, z, BLOCK_VARIANTS + block_variant(x, y, z)); break;
                case '^': add_static(I_SPIKES, x, y, z, 0); break;
                case 'G': add_static(I_GATE, x, y, z, x == 0 || x == r->w - 1); break;
                case 'T': add_static(I_THRONE, x, y, z, 0); break;
                }
    /* far ones first, so ties come out in a sensible order */
    for (i = 1; i < nstatic; i++) {
        DItem t = item[i];
        for (j = i; j > 0 && item[j - 1].key < t.key; j--) item[j] = item[j - 1];
        item[j] = t;
    }
    nsedge = 0;
    for (i = 0; i < nstatic; i++) item[i].head = -1;
    for (i = 0; i < nstatic; i++)
        for (j = i + 1; j < nstatic; j++) {
            int o;
            if (!rects_meet(&item[i], &item[j])) continue;
            o = order(&item[i], &item[j]);
            if (o > 0) add_edge(sedge, &nsedge, &item[j].head, i);
            else if (o < 0) add_edge(sedge, &nsedge, &item[i].head, j);
        }
}

/* ---- each frame ---- */

extern long isin256(int a);

static void actor_item(int a)
{
    DItem *it = &item[nitem];
    Box b;
    body_box(R.act[a].body, &b);
    it->kind = I_ACTOR;
    it->idx = (short)a;
    set_box(it, b.x0, b.y0, b.z0, b.x1, b.y1, b.z1);
    nitem++;
}

static void draw_item(const DItem *it, int show_player)
{
    static const unsigned char walk_frame[4] = { 0, 1, 0, 2 };
    switch (it->kind) {
    case I_BLOCK: {
        int x = it->cx, y = it->cy, z = it->cz;
        Sprite *s = it->var >= BLOCK_VARIANTS ? &spr_block[1][it->var - BLOCK_VARIANTS] : &spr_block[0][it->var];
        emit(s, sx_of((fix)x << 12, (fix)z << 12), sy_of((fix)x << 12, (fix)y << 12, (fix)z << 12), 0);
        break;
    }
    case I_SPIKES:
    case I_GATE:
    case I_THRONE: {
        fix x = ((fix)it->cx << 12) + FX / 2, y = (fix)it->cy << 12, z = ((fix)it->cz << 12) + FX / 2;
        Sprite *s = it->kind == I_SPIKES ? &spr_spikes : it->kind == I_GATE ? &spr_gate[it->var] : &spr_throne;
        if (it->kind == I_GATE && R.ngate == 0) break;                /* opened */
        emit(s, sx_of(x, z), sy_of(x, y, z), 0);
        break;
    }
    case I_LIFT: {
        const Lift *l = &R.lift[it->idx];
        const Body *b = l->body;
        int k = (int)((b->py - l->base + FX / 4 - 1) / (FX / 4));
        if (k > 8) k = 8;
        if (k > 0) emit(&spr_piston[k], sx_of(b->px, b->pz), sy_of(b->px, b->py, b->pz), 0);
        emit(&spr_lift, sx_of(b->px, b->pz), sy_of(b->px, b->py, b->pz), 0);
        break;
    }
    case I_PICKUP: {
        const Pickup *p = &R.pk[it->idx];
        fix x = ((fix)p->x << 12) + FX / 2, y = (fix)p->y << 12, z = ((fix)p->z << 12) + FX / 2;
        /* Pickup.Animate: phase = x * 1.7 + z (centre of the cell); bob sin(3t + phase) * 0.08,
         * spin 140 t + 30 phase degrees. In 1/256 turns and 22.5-degree frames: */
        long ph = 693L * p->x / 10 + 407L * p->z / 10 + 55;
        long bob = (isin256((int)((R.clock * 2444 / 1000 + ph) & 255)) * 22) >> 14;   /* px * 16 */
        int f = (int)((R.clock * 1244 / 10000 + 227L * p->x / 100 + 133L * p->z / 100 + 2) & 15);
        Sprite *s = p->kind == 'R' ? &spr_relic[f] : p->kind == 'K' ? &spr_key[f] : &spr_potion;
        emit(s, sx_of(x, z), sy_of(x, y, z) - bob / 16, 0);
        break;
    }
    case I_ACTOR: {
        const Actor *a = &R.act[it->idx];
        const Body *b = a->body;
        long x = sx_of(b->px, b->pz), y = sy_of(b->px, b->py, b->pz);
        int fr = walk_frame[((a->phase >> 8) + 32) >> 6 & 3];
        Sprite *s = 0;
        if (a->kind == K_PLAYER && (!show_player || !a->visible)) break;
        if (a->kind != K_CRATE) {
            fix g = w_ground_below(b->px, b->py, b->pz);
            emit(&spr_shadow[1], sx_of(b->px, b->pz), sy_of(b->px, g, b->pz), PIXC_SHADOW);
        }
        switch (a->kind) {
        case K_PLAYER:  s = &spr_walker[0][a->face][fr]; break;
        case K_GUARD:   s = &spr_walker[1][a->face][fr]; break;
        case K_HOUND:   s = &spr_walker[2][a->face][fr]; break;
        case K_SAGE:    s = &spr_sage[a->face]; break;
        case K_GHOST:   s = &spr_ghost[a->face]; break;
        case K_BOUNCER: s = &spr_bouncer[a->roll & 7]; break;
        case K_CRATE:   s = &spr_crate; break;
        }
        if (s) emit(s, x, y, 0);
        break;
    }
    }
}

static int show_player_flag;

static void visit(int i)
{
    int e;
    item[i].mark = 1;
    if (i < nstatic)
        for (e = item[i].head; e >= 0; e = sedge[e].next)
            if (!item[sedge[e].from].mark) visit(sedge[e].from);
    for (e = item[i].dhead; e >= 0; e = dedge[e].next)
        if (!item[dedge[e].from].mark) visit(dedge[e].from);
    draw_item(&item[i], show_player_flag);
}

void *scene_frame(int show_player, int panel, int flash_ink)
{
    int i, j;
    npool = 0;
    if (!scene_fb) return 0;
    {
        /* a plain loop: newlib's memcpy() left most of this 300 KB copy
         * undone under QEMU (aligned, not overlapping; smaller copies work) */
        const Pix *src = bgbuf;
        Pix *dst = scene_fb, *end = scene_fb + SW * SH;
#ifdef SCENE_RGB565
        (void)src; (void)dst; (void)end;
        restore_frame();
#else
        while (dst < end) *dst++ = *src++;
#endif
    }
    show_player_flag = show_player;

    /* the moving things */
    nitem = nstatic;
    for (i = 0; i < R.nlift; i++) {
        const Lift *l = &R.lift[i];
        Box b;
        body_box(l->body, &b);
        item[nitem].kind = I_LIFT;
        item[nitem].idx = (short)i;
        set_box(&item[nitem], b.x0, l->base, b.z0, b.x1, b.y1, b.z1);
        nitem++;
    }
    for (i = 0; i < R.npk; i++) {
        item[nitem].kind = I_PICKUP;
        item[nitem].idx = (short)i;
        set_box(&item[nitem], R.pk[i].box.x0, R.pk[i].box.y0, R.pk[i].box.z0,
                R.pk[i].box.x1, R.pk[i].box.y1, R.pk[i].box.z1);
        nitem++;
    }
    for (i = 0; i < R.nact; i++) actor_item(i);

    ndedge = 0;
    for (i = 0; i < nitem; i++) {
        item[i].dhead = -1;
        item[i].mark = 0;
    }
    for (i = nstatic; i < nitem; i++)
        for (j = 0; j < nitem; j++) {
            int o;
            if (j == i || (j >= nstatic && j < i) || !rects_meet(&item[i], &item[j])) continue;
            o = order(&item[i], &item[j]);
            if (o > 0) add_edge(dedge, &ndedge, &item[j].dhead, i);
            else if (o < 0) add_edge(dedge, &ndedge, &item[i].dhead, j);
        }
    for (i = 0; i < nitem; i++)
        if (!item[i].mark) visit(i);

    if (spr_arch[0].pix && lv_has_door(R.level, R.ridx, SIDE_S)) emit(&spr_arch[0], scene_ox, scene_oy, 0);
    if (spr_arch[1].pix && lv_has_door(R.level, R.ridx, SIDE_W)) emit(&spr_arch[1], scene_ox, scene_oy, 0);
    if (flash_ink >= 0) {
        unsigned short c = ink_rgb15(flash_ink, 1);
        /* the cel adds colour / 2: a third of the ink for ~16% */
        flash((unsigned short)((((c >> 10) & 31) / 3 << 10) | (((c >> 5) & 31) / 3 << 5) | ((c & 31) / 3)));
    }
    if (panel) shade_rect(200, SH - 1, 3);
    scene_ncels = npool;
    return scene_fb;
}
