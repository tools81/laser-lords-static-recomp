/* Sprite interpolation from the game's own sprite layer (HD edition).

   Laser Lords' planet scenes are drawn in software: the program keeps a
   clean copy of the scene window's background (320x160 CLUT pixels, the
   start of the buffer cdi_tribes keeps at -22494(a6)) and draws its
   characters into the displayed plane over it.  So for each picture the
   sprites are exactly the window pixels where the plane differs from the
   background.

   The characters' walk and climb poses are new drawings each time, so the
   block search of motion.c finds no clean motion in them.  Here the sprites
   of the picture shown now (p) and of the next one (n) are separated into
   connected shapes, shapes are paired by position and size, and each pair's
   offset is found by aligning their outlines.  An in-between picture is the
   clean background with each moving sprite drawn along its path: p's pose
   for the first half of the interval, n's pose for the second.  Nothing is
   blended, and a pose changes when it changed originally.  Shapes without a
   partner stay as they are until n is due.  Pixels where the screen shows
   something other than the plane (the right border column overlaps the
   window's last column) are never touched. */
#include "recomp.h"
#include "runtime.h"
#include "sprites.h"
#include <stdlib.h>
#include <string.h>

#define TRIBES_BASE 0xC6000u     /* cdi_tribes (game.json) */
#define BG_PTR      (-22494)     /* its pointer to the background copy */
#define WX0 32                   /* the scene window, normal resolution */
#define WY0 14
#define WW 320
#define WH 160
#define MAXC 32                  /* shapes per picture */
#define GAP 2                    /* pixels apart that still form one shape */

const uint32_t *video_plane_a_lines(const uint8_t **coding);
uint32_t video_clut_host(int idx);
uint32_t os9_child_a6(uint32_t modbase);

struct SpriteLayer {
    int valid;
    uint8_t mask[WH][WW];        /* 1 = sprite pixel */
    uint32_t bg[WH][WW];         /* the background, as it is shown */
    uint8_t bgi[WH][WW];         /* ... its CLUT indices */
    uint8_t cover[WH][WW];       /* 1 = the screen shows something else here
                                    (the border, which overlaps the window's
                                    last column): never drawn over */
};

SpriteLayer *sprites_new(void) { return (SpriteLayer *)calloc(1, sizeof(SpriteLayer)); }
void sprites_invalidate(SpriteLayer *l) { l->valid = 0; }
int sprites_layer_valid(const SpriteLayer *l) { return l->valid; }

const char *g_sprites_why = "";     /* why the last capture failed (debug log) */

int sprites_capture(SpriteLayer *l, const uint32_t *pic) {
    l->valid = 0;
    uint32_t a6 = os9_child_a6(TRIBES_BASE);
    if (!a6) { g_sprites_why = "not in cdi_tribes"; return 0; }
    uint32_t bgp = RL(a6 + BG_PTR) & 0xFFFFFF;
    if (bgp < HEAP_BASE || bgp + WW * WH >= HEAP_END) { g_sprites_why = "no background buffer"; return 0; }
    const uint8_t *cm;
    const uint32_t *lines = video_plane_a_lines(&cm);
    for (int y = 0; y < WH; y++) {
        int line = WY0 + y;
        if ((cm[line] & 15) != 1 || ((cm[line] >> 4) & 3) == 2) { g_sprites_why = "plane A not CLUT8"; return 0; }   /* CLUT8, not run-length */
    }
    /* the colour each index is shown in, from the picture itself (the CLUT
       is reloaded during the field, so the one left at its end may differ) */
    uint32_t lut[256];
    uint8_t seen[256] = {0};
    uint8_t (*bgi)[WW] = l->bgi;
    int sprite = 0, wrong = 0;
    for (int y = 0; y < WH; y++) {
        uint32_t row = lines[WY0 + y];
        const uint32_t *pr = pic + (WY0 + y) * 768 + WX0 * 2;
        for (int x = 0; x < WW; x++) {
            uint8_t v = RB(row + WX0 + x), b = RB(bgp + (uint32_t)(y * WW + x));
            bgi[y][x] = b;
            l->mask[y][x] = v != b;
            sprite += v != b;
            uint32_t c = pr[x * 2] & 0xFFFFFF;
            if (!seen[v]) { seen[v] = 1; lut[v] = c; }
            else if (lut[v] != c) wrong++;    /* the picture is not the plane as it is */
        }
    }
    if (wrong * 50 > WW * WH) { g_sprites_why = "picture differs from plane"; return 0; }
    if (sprite * 4 > WW * WH) { g_sprites_why = "too many sprite pixels"; return 0; }   /* most of the scene changed: not sprites */
    for (int y = 0; y < WH; y++) {
        const uint32_t *pr = pic + (WY0 + y) * 768 + WX0 * 2;
        uint32_t row = lines[WY0 + y];
        for (int x = 0; x < WW; x++) {
            int b = bgi[y][x];
            l->bg[y][x] = seen[b] ? lut[b] : video_clut_host(b);
            uint8_t v = RB(row + WX0 + x);
            l->cover[y][x] = (pr[x * 2] & 0xFFFFFF) != lut[v];
        }
    }
    l->valid = 1;
    return 1;
}

/* ---------------------------------------------------------------- shapes */
typedef struct { int x0, y0, x1, y1, n; } Shape;

static int16_t lab_p[WH][WW], lab_n[WH][WW];
static Shape sh_p[MAXC], sh_n[MAXC];
static int nsp, nsn;
static int match_of[MAXC];           /* p shape -> n shape, -1 none */
static int mdx[MAXC], mdy[MAXC];     /* n = p moved by (mdx, mdy) */

static int label(const SpriteLayer *l, int16_t lab[WH][WW], Shape *sh) {
    static uint8_t dil[WH][WW];
    static int16_t dl[WH][WW];
    static int stack[WW * WH];
    memset(dil, 0, sizeof dil);
    for (int y = 0; y < WH; y++)
        for (int x = 0; x < WW; x++) {
            if (!l->mask[y][x]) continue;
            for (int dy = -GAP; dy <= GAP; dy++)
                for (int dx = -GAP; dx <= GAP; dx++) {
                    int xx = x + dx, yy = y + dy;
                    if (xx >= 0 && xx < WW && yy >= 0 && yy < WH) dil[yy][xx] = 1;
                }
        }
    for (int y = 0; y < WH; y++) for (int x = 0; x < WW; x++) dl[y][x] = -1;
    int ns = 0;
    for (int sy = 0; sy < WH; sy++)
        for (int sx = 0; sx < WW; sx++) {
            if (!dil[sy][sx] || dl[sy][sx] >= 0) continue;
            int id = ns < MAXC ? ns : MAXC - 1;
            if (ns < MAXC) ns++;
            int sp = 0;
            dl[sy][sx] = (int16_t)id;
            stack[sp++] = sy * WW + sx;
            while (sp) {
                int b = stack[--sp], x = b % WW, y = b / WW;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int xx = x + dx, yy = y + dy;
                        if (xx < 0 || xx >= WW || yy < 0 || yy >= WH || !dil[yy][xx] || dl[yy][xx] >= 0) continue;
                        dl[yy][xx] = (int16_t)id;
                        stack[sp++] = yy * WW + xx;
                    }
            }
        }
    for (int i = 0; i < ns; i++) { sh[i].x0 = WW; sh[i].y0 = WH; sh[i].x1 = -1; sh[i].y1 = -1; sh[i].n = 0; }
    for (int y = 0; y < WH; y++)
        for (int x = 0; x < WW; x++) {
            lab[y][x] = -1;
            if (!l->mask[y][x]) continue;
            int id = dl[y][x];
            lab[y][x] = (int16_t)id;
            Shape *s = &sh[id];
            if (x < s->x0) s->x0 = x;
            if (x > s->x1) s->x1 = x;
            if (y < s->y0) s->y0 = y;
            if (y > s->y1) s->y1 = y;
            s->n++;
        }
    return ns;
}

/* pixels of n shape j that p shape i covers when moved by (dx, dy) */
static int overlap(int i, int j, int dx, int dy) {
    const Shape *b = &sh_n[j];
    int c = 0;
    for (int y = b->y0; y <= b->y1; y++) {
        int py = y - dy;
        if (py < 0 || py >= WH) continue;
        for (int x = b->x0; x <= b->x1; x++) {
            if (lab_n[y][x] != j) continue;
            int px = x - dx;
            if (px >= 0 && px < WW && lab_p[py][px] == i) c++;
        }
    }
    return c;
}

int sprites_prepare(const SpriteLayer *lp, const SpriteLayer *ln) {
    if (!lp->valid || !ln->valid) return 0;
    if (memcmp(lp->bgi, ln->bgi, sizeof lp->bgi)) {   /* the scene itself changed */
        if (getenv("LL_INTERP_LOG")) {
            int c = 0, x0 = WW, x1 = -1, y0 = WH, y1 = -1;
            for (int y = 0; y < WH; y++) for (int x = 0; x < WW; x++) if (lp->bgi[y][x] != ln->bgi[y][x]) {
                c++; if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
            LOG("  background differs: %d pixels, x %d-%d y %d-%d", c, x0, x1, y0, y1);
        }
        return 0;
    }
    nsp = label(lp, lab_p, sh_p);
    nsn = label(ln, lab_n, sh_n);
    typedef struct { int i, j, dx, dy; float score; } Cand;
    Cand cand[MAXC * MAXC];
    int nc = 0;
    for (int i = 0; i < nsp; i++) {
        match_of[i] = -1;
        if (sh_p[i].n < 12) continue;
        for (int j = 0; j < nsn; j++) {
            if (sh_n[j].n < 12) continue;
            int r = sh_p[i].n > sh_n[j].n ? sh_p[i].n * 10 / sh_n[j].n : sh_n[j].n * 10 / sh_p[i].n;
            if (r > 25) continue;
            int cdx = (sh_n[j].x0 + sh_n[j].x1 - sh_p[i].x0 - sh_p[i].x1) / 2;
            int cdy = (sh_n[j].y0 + sh_n[j].y1 - sh_p[i].y0 - sh_p[i].y1) / 2;
            if (abs(cdx) > 40 || abs(cdy) > 32) continue;
            int best = -1, bx = 0, by = 0;
            for (int dy = cdy - 6; dy <= cdy + 6; dy++)
                for (int dx = cdx - 6; dx <= cdx + 6; dx++) {
                    int o = overlap(i, j, dx, dy);
                    if (o > best || (o == best && abs(dx) + abs(dy) < abs(bx) + abs(by))) { best = o; bx = dx; by = dy; }
                }
            int m = sh_p[i].n > sh_n[j].n ? sh_p[i].n : sh_n[j].n;
            float score = (float)best / (float)m;
            if (score < 0.25f) continue;
            cand[nc++] = (Cand){i, j, bx, by, score};
        }
    }
    /* best pairs first, each shape in one pair */
    int used_n[MAXC] = {0};
    int moving = 0;
    for (;;) {
        int b = -1;
        for (int k = 0; k < nc; k++)
            if (cand[k].score >= 0 && match_of[cand[k].i] < 0 && !used_n[cand[k].j] && (b < 0 || cand[k].score > cand[b].score)) b = k;
        if (b < 0) break;
        Cand *c = &cand[b];
        c->score = -1;
        used_n[c->j] = 1;
        if (!c->dx && !c->dy) { match_of[c->i] = -2; continue; }   /* paired, not moving: held */
        match_of[c->i] = c->j;
        mdx[c->i] = c->dx;
        mdy[c->i] = c->dy;
        moving++;
    }
    return moving > 0;
}

/* draw shape id of a picture (labels lab) moved by (dx, dy), inside the
   window and not where the screen shows something else (cover) */
static void draw_shape(uint32_t *out, const uint32_t *pic, int16_t lab[WH][WW], const Shape *s, int id, int dx, int dy,
                       const SpriteLayer *cv) {
    for (int y = s->y0; y <= s->y1; y++) {
        int ty = y + dy;
        if (ty < 0 || ty >= WH) continue;
        const uint32_t *src = pic + (WY0 + y) * 768 + WX0 * 2;
        uint32_t *dst = out + (WY0 + ty) * 768 + WX0 * 2;
        for (int x = s->x0; x <= s->x1; x++) {
            if (lab[y][x] != id) continue;
            int tx = x + dx;
            if (tx < 0 || tx >= WW || cv->cover[ty][tx]) continue;
            dst[tx * 2] = src[x * 2];
            dst[tx * 2 + 1] = src[x * 2 + 1];
        }
    }
}

void sprites_render(uint32_t *out, const uint32_t *p, const uint32_t *n,
                    const SpriteLayer *lp, const SpriteLayer *ln, float alpha) {
    (void)ln;
    /* p with its sprites taken away: only the pixels that show the scene */
    memcpy(out, p, 768 * 240 * 4);
    for (int y = 0; y < WH; y++) {
        uint32_t *dst = out + (WY0 + y) * 768 + WX0 * 2;
        for (int x = 0; x < WW; x++)
            if (lp->mask[y][x] && !lp->cover[y][x]) dst[x * 2] = dst[x * 2 + 1] = lp->bg[y][x];
    }
    /* back to front by the shapes' feet */
    int order[MAXC], no = 0;
    for (int i = 0; i < nsp; i++) order[no++] = i;
    for (int a = 1; a < no; a++)
        for (int b = a; b > 0 && sh_p[order[b]].y1 < sh_p[order[b - 1]].y1; b--) {
            int t = order[b]; order[b] = order[b - 1]; order[b - 1] = t;
        }
    for (int k = 0; k < no; k++) {
        int i = order[k], j = match_of[i];
        if (j < 0) { draw_shape(out, p, lab_p, &sh_p[i], i, 0, 0, lp); continue; }
        if (alpha < 0.5f) {
            int ox = (int)(mdx[i] * alpha + (mdx[i] > 0 ? 0.5f : -0.5f));
            int oy = (int)(mdy[i] * alpha + (mdy[i] > 0 ? 0.5f : -0.5f));
            draw_shape(out, p, lab_p, &sh_p[i], i, ox, oy, lp);
        } else {
            int ox = (int)(-mdx[i] * (1.f - alpha) + (mdx[i] > 0 ? -0.5f : 0.5f));
            int oy = (int)(-mdy[i] * (1.f - alpha) + (mdy[i] > 0 ? -0.5f : 0.5f));
            draw_shape(out, n, lab_n, &sh_n[j], j, ox, oy, lp);
        }
    }
}
