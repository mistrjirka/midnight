/* SPDX-License-Identifier: GPL-2.0-only */
/* Scene drawing: clipped lines, 24-segment ellipses, non-overlapping labels. */
#include <dryos.h>
#include <bmp.h>
#include <string.h>
#include "draw.h"

#define HUD_Y          446
#define HUD_HEIGHT     30
#define SCREEN_W       720
#define GLYPH_W        8
#define GLYPH_H        12
#define MAX_PLACED     64
#define ELLIPSE_STEPS  24
#define Q14            16384
#define CLIP_GUARD     8
#define MIN_ELLIPSE_SQ 9 /* smaller outlines are drawn as a 3 px circle */
#define RETICLE_GAP    5
#define RETICLE_ARM    14
#define ARROW_HEAD     14

const astro_map astro_liveview_map = {2, 1, 0, 0, 0, 0, SCREEN_W - 1, HUD_Y - 1};

/* 8x12 lowercase Greek, MSB = left pixel. ML fonts are ASCII only. */
static const uint8_t greek_glyphs[24][GLYPH_H] = {
    {0x00, 0x00, 0x00, 0x00, 0x62, 0x92, 0x8a, 0x84, 0x8a, 0x71, 0x00, 0x00}, /* alpha */
    {0x00, 0x38, 0x44, 0x44, 0x58, 0x44, 0x42, 0x42, 0x62, 0x5c, 0x40, 0x40}, /* beta */
    {0x00, 0x00, 0x00, 0x00, 0x82, 0x42, 0x24, 0x14, 0x08, 0x08, 0x10, 0x10}, /* gamma */
    {0x00, 0x3c, 0x40, 0x20, 0x18, 0x44, 0x82, 0x82, 0x82, 0x7c, 0x00, 0x00}, /* delta */
    {0x00, 0x00, 0x00, 0x00, 0x3c, 0x40, 0x40, 0x38, 0x40, 0x40, 0x3c, 0x00}, /* epsilon */
    {0x00, 0x7c, 0x08, 0x10, 0x20, 0x40, 0x40, 0x40, 0x3c, 0x02, 0x04, 0x00}, /* zeta */
    {0x00, 0x00, 0x00, 0x00, 0xb8, 0xc4, 0x84, 0x84, 0x84, 0x84, 0x04, 0x04}, /* eta */
    {0x00, 0x38, 0x44, 0x82, 0x82, 0xfe, 0x82, 0x82, 0x44, 0x38, 0x00, 0x00}, /* theta */
    {0x00, 0x00, 0x00, 0x00, 0x30, 0x10, 0x10, 0x10, 0x10, 0x0c, 0x00, 0x00}, /* iota */
    {0x00, 0x00, 0x00, 0x00, 0x46, 0x48, 0x50, 0x60, 0x50, 0x4c, 0x00, 0x00}, /* kappa */
    {0x00, 0x40, 0x20, 0x20, 0x10, 0x10, 0x28, 0x28, 0x44, 0x86, 0x00, 0x00}, /* lambda */
    {0x00, 0x00, 0x00, 0x00, 0x44, 0x44, 0x44, 0x44, 0x4c, 0x76, 0x40, 0x40}, /* mu */
    {0x00, 0x00, 0x00, 0x00, 0x82, 0x82, 0x44, 0x48, 0x28, 0x10, 0x00, 0x00}, /* nu */
    {0x00, 0x7c, 0x40, 0x38, 0x40, 0x80, 0x80, 0x7c, 0x02, 0x04, 0x08, 0x00}, /* xi */
    {0x00, 0x00, 0x00, 0x00, 0x38, 0x44, 0x82, 0x82, 0x44, 0x38, 0x00, 0x00}, /* omicron */
    {0x00, 0x00, 0x00, 0x00, 0xfe, 0x44, 0x44, 0x44, 0x44, 0x46, 0x00, 0x00}, /* pi */
    {0x00, 0x00, 0x00, 0x00, 0x38, 0x44, 0x42, 0x42, 0x64, 0x58, 0x40, 0x40}, /* rho */
    {0x00, 0x00, 0x00, 0x00, 0x3f, 0x44, 0x82, 0x82, 0x44, 0x38, 0x00, 0x00}, /* sigma */
    {0x00, 0x00, 0x00, 0x00, 0xfe, 0x10, 0x10, 0x10, 0x10, 0x0c, 0x00, 0x00}, /* tau */
    {0x00, 0x00, 0x00, 0x00, 0x82, 0x42, 0x42, 0x42, 0x44, 0x38, 0x00, 0x00}, /* upsilon */
    {0x00, 0x10, 0x10, 0x38, 0x54, 0x92, 0x92, 0x54, 0x38, 0x10, 0x10, 0x00}, /* phi */
    {0x00, 0x00, 0x00, 0x00, 0x82, 0x44, 0x28, 0x10, 0x28, 0x44, 0x82, 0x00}, /* chi */
    {0x00, 0x00, 0x10, 0x92, 0x92, 0x92, 0x54, 0x38, 0x10, 0x10, 0x10, 0x00}, /* psi */
    {0x00, 0x00, 0x00, 0x00, 0x44, 0x82, 0x92, 0x92, 0x92, 0x6c, 0x00, 0x00}, /* omega */
};

/* cos(15 degrees * i), Q14. sin(t) is cos(t - 90 degrees) = entry (i + 18) % 24. */
static const int16_t cos24[ELLIPSE_STEPS] = {
    16384,  15826,  14189,  11585,  8192,  4240,  0, -4240, -8192, -11585, -14189, -15826,
    -16384, -15826, -14189, -11585, -8192, -4240, 0, 4240,  8192,  11585,  14189,  15826,
};

/* Screen rectangles of labels already drawn in this pass. */
static struct {
    int count;
    int box[MAX_PLACED][4];
} placed;

static int char_width(void)
{
    return fontspec_width(FONT_SMALL);
}

static int char_height(void)
{
    return fontspec_height(FONT_SMALL);
}

int astro_label_width(const char *text)
{
    return (int)strlen(text) * char_width();
}

static void draw_greek(int x, int y, int letter, int fg, int bg)
{
    const uint8_t *glyph = greek_glyphs[letter - 1];
    int gx = x + (char_width() - GLYPH_W) / 2;
    int gy = y + (char_height() - GLYPH_H) / 2;

    bmp_fill(bg, x, y, char_width(), char_height());
    for (int row = 0; row < GLYPH_H; row++) {
        for (int col = 0; col < GLYPH_W; col++) {
            if (glyph[row] & (0x80 >> col)) {
                bmp_putpixel(gx + col, gy + row, (uint8_t)fg);
            }
        }
    }
}

void astro_draw_label(int x, int y, const char *text, int fg, int bg)
{
    char run[24];

    while (*text) {
        int n = 0;
        while (text[n] && (uint8_t)text[n] > 24 && n < (int)sizeof(run) - 1) {
            run[n] = text[n];
            n++;
        }
        if (n) {
            run[n] = 0;
            bmp_printf(FONT(FONT_SMALL, fg, bg), x, y, "%s", run);
            x += n * char_width();
            text += n;
            continue;
        }
        draw_greek(x, y, (uint8_t)*text, fg, bg);
        x += char_width();
        text++;
    }
}

static int to_screen_x(const astro_map *m, int x_q4)
{
    return m->ox + x_q4 * m->num / (m->den * 16);
}

static int to_screen_y(const astro_map *m, int y_q4)
{
    return m->oy + y_q4 * m->num / (m->den * 16);
}

static int outcode(const astro_map *m, int x, int y)
{
    return (x < m->x0) | ((x > m->x1) << 1) | ((y < m->y0) << 2) | ((y > m->y1) << 3);
}

/* Cohen-Sutherland clip to the map rectangle, then draw. */
static void clipped_line(const astro_map *m, int x1, int y1, int x2, int y2, int color)
{
    int c1 = outcode(m, x1, y1);
    int c2 = outcode(m, x2, y2);

    for (int guard = 0; guard < CLIP_GUARD && (c1 | c2); guard++) {
        if (c1 & c2) {
            return;
        }
        int c = c1 ? c1 : c2;
        int x, y;
        if (c & 8) {
            x = x1 + (x2 - x1) * (m->y1 - y1) / (y2 - y1);
            y = m->y1;
        } else if (c & 4) {
            x = x1 + (x2 - x1) * (m->y0 - y1) / (y2 - y1);
            y = m->y0;
        } else if (c & 2) {
            y = y1 + (y2 - y1) * (m->x1 - x1) / (x2 - x1);
            x = m->x1;
        } else {
            y = y1 + (y2 - y1) * (m->x0 - x1) / (x2 - x1);
            x = m->x0;
        }
        if (c == c1) {
            x1 = x;
            y1 = y;
            c1 = outcode(m, x1, y1);
        } else {
            x2 = x;
            y2 = y;
            c2 = outcode(m, x2, y2);
        }
    }
    if (!(c1 | c2)) {
        draw_line(x1, y1, x2, y2, color);
    }
}

static void draw_ellipse(const astro_map *m, const astro_ellipse *e, int color)
{
    int cx = to_screen_x(m, e->x);
    int cy = to_screen_y(m, e->y);
    int scale = m->den * 16;
    int ax = e->ax * m->num / scale;
    int ay = e->ay * m->num / scale;
    int bx = e->bx * m->num / scale;
    int by = e->by * m->num / scale;

    if (ax * ax + ay * ay < MIN_ELLIPSE_SQ && bx * bx + by * by < MIN_ELLIPSE_SQ) {
        if (cx >= m->x0 + 4 && cx <= m->x1 - 4 && cy >= m->y0 + 4 && cy <= m->y1 - 4) {
            draw_circle(cx, cy, 3, color);
        }
        return;
    }
    int px = 0;
    int py = 0;
    for (int i = 0; i <= ELLIPSE_STEPS; i++) {
        int c = cos24[i % ELLIPSE_STEPS];
        int s = cos24[(i + 18) % ELLIPSE_STEPS];
        int x = cx + (ax * c + bx * s) / Q14;
        int y = cy + (ay * c + by * s) / Q14;
        if (i) {
            clipped_line(m, px, py, x, y, color);
        }
        px = x;
        py = y;
    }
}

/* Tries the four corners around an anchor; returns 0 if every spot overlaps. */
static int place_label(const astro_map *m, int x, int y, int w, int h, int gap, int *tx, int *ty)
{
    for (int choice = 0; choice < 4; choice++) {
        *tx = (choice & 1) ? x - w - gap : x + gap;
        *ty = (choice & 2) ? y - h - gap / 2 : y + gap / 2;
        if (*tx < m->x0 || *tx + w > m->x1 || *ty < m->y0 || *ty + h > m->y1) {
            continue;
        }
        int overlap = 0;
        for (int j = 0; j < placed.count && !overlap; j++) {
            const int *b = placed.box[j];
            overlap = *tx < b[2] + 3 && *tx + w > b[0] - 3 && *ty < b[3] + 2 && *ty + h > b[1] - 2;
        }
        if (!overlap) {
            return 1;
        }
    }
    return 0;
}

static void remember_label(int x, int y, int w, int h)
{
    if (placed.count < MAX_PLACED) {
        int *b = placed.box[placed.count++];
        b[0] = x;
        b[1] = y;
        b[2] = x + w;
        b[3] = y + h;
    }
}

static void put_label(const astro_map *m, int x, int y, int gap, const char *text, int color,
                      int erase)
{
    int w = astro_label_width(text);
    int h = char_height();
    int tx, ty;

    if (!place_label(m, x, y, w, h, gap, &tx, &ty)) {
        return;
    }
    if (erase) {
        bmp_fill(COLOR_EMPTY, tx, ty, w, h);
    } else {
        astro_draw_label(tx, ty, text, color, COLOR_BLACK);
    }
    remember_label(tx, ty, w, h);
}

static int label_color(int kind)
{
    switch (kind) {
        case ASTRO_LABEL_TARGET:
            return COLOR_RED;
        case ASTRO_LABEL_DSO:
            return COLOR_GREEN1;
        case ASTRO_LABEL_CONSTELLATION:
            return COLOR_LIGHT_BLUE;
        default:
            return COLOR_YELLOW;
    }
}

static void draw_reticle(const astro_map *m, int x, int y, int color)
{
    clipped_line(m, x - RETICLE_ARM, y, x - RETICLE_GAP, y, color);
    clipped_line(m, x + RETICLE_GAP, y, x + RETICLE_ARM, y, color);
    clipped_line(m, x, y - RETICLE_ARM, x, y - RETICLE_GAP, color);
    clipped_line(m, x, y + RETICLE_GAP, x, y + RETICLE_ARM, color);
}

/* Thick arrow from the frame centre towards an off-frame target, with its distance. */
static void draw_target_arrow(const astro_scene *s, const astro_map *m, int erase)
{
    int cx = (m->x0 + m->x1) / 2;
    int cy = (m->y0 + m->y1) / 2;
    int shorter = (m->y1 - m->y0) < (m->x1 - m->x0) ? (m->y1 - m->y0) : (m->x1 - m->x0);
    int length = shorter * 2 / 5;
    int ex = cx + s->target_dx * length / Q14;
    int ey = cy + s->target_dy * length / Q14;
    int color = erase ? COLOR_EMPTY : COLOR_RED;
    char text[40];

    for (int t = -1; t <= 1; t++) {
        clipped_line(m, cx + t, cy, ex + t, ey, color);
        clipped_line(m, cx, cy + t, ex, ey + t, color);
    }
    int bx = -s->target_dx * ARROW_HEAD / Q14;
    int by = -s->target_dy * ARROW_HEAD / Q14;
    clipped_line(m, ex, ey, ex + bx - by / 2, ey + by + bx / 2, color);
    clipped_line(m, ex, ey, ex + bx + by / 2, ey + by - bx / 2, color);
    snprintf(text, sizeof(text), "%s %d.%d deg", s->target_label,
             (int)(s->target_separation / 10000), (int)(s->target_separation / 1000 % 10));
    put_label(m, ex, ey, 10, text, COLOR_RED, erase);
}

void astro_draw_scene(const astro_scene *s, const astro_map *m, int erase)
{
    for (int i = 0; i < s->nlines; i++) {
        const astro_line *l = &s->lines[i];
        clipped_line(m, to_screen_x(m, l->x1), to_screen_y(m, l->y1), to_screen_x(m, l->x2),
                     to_screen_y(m, l->y2), erase ? COLOR_EMPTY : COLOR_LIGHT_BLUE);
    }
    for (int i = 0; i < s->nellipses; i++) {
        draw_ellipse(m, &s->ellipses[i], erase ? COLOR_EMPTY : label_color(s->ellipses[i].kind));
    }

    placed.count = 0;
    if (s->target == ASTRO_TARGET_IN_FRAME) {
        draw_reticle(m, to_screen_x(m, s->target_x), to_screen_y(m, s->target_y),
                     erase ? COLOR_EMPTY : COLOR_RED);
    } else if (s->target == ASTRO_TARGET_OUTSIDE && (s->target_dx || s->target_dy)) {
        draw_target_arrow(s, m, erase);
    }
    for (int i = 0; i < s->nlabels; i++) {
        const astro_label *l = &s->labels[i];
        int x = to_screen_x(m, l->x);
        int y = to_screen_y(m, l->y);
        int gap = 8;
        if (l->kind == ASTRO_LABEL_STAR && x > m->x0 + 5 && x < m->x1 - 5 && y > m->y0 + 5 &&
            y < m->y1 - 5) {
            draw_circle(x, y, 4, erase ? COLOR_EMPTY : COLOR_YELLOW);
        }
        if (l->kind == ASTRO_LABEL_CONSTELLATION) {
            /* Centred on the figure rather than beside a point. */
            x -= astro_label_width(l->text) / 2;
            y -= char_height() / 2;
            gap = 0;
        }
        put_label(m, x, y, gap, l->text, label_color(l->kind), erase);
    }
}

void astro_draw_matches(const astro_solve_result *r, const astro_map *m)
{
    for (int i = 0; i < r->star_count; i++) {
        if (!(r->sky.matched_mask & (1u << i))) {
            continue;
        }
        int x = to_screen_x(m, r->stars[i].x * 16);
        int y = to_screen_y(m, r->stars[i].y * 16);
        bmp_draw_rect(COLOR_BLACK, x - 6, y - 6, 12, 12);
        bmp_draw_rect(COLOR_YELLOW, x - 5, y - 5, 10, 10);
    }
}

void astro_draw_arrow(const astro_map *m, int x_q4, int y_q4, int to_x_q4, int to_y_q4, int color)
{
    int x1 = to_screen_x(m, x_q4);
    int y1 = to_screen_y(m, y_q4);
    int x2 = to_screen_x(m, to_x_q4);
    int y2 = to_screen_y(m, to_y_q4);

    for (int t = -1; t <= 1; t++) {
        clipped_line(m, x1 + t, y1, x2 + t, y2, color);
    }
    draw_reticle(m, x1, y1, color);
    draw_circle(x2, y2, 6, color);
}

static void paint_hud(const char *text, int color)
{
    bmp_fill(COLOR_BLACK, 0, HUD_Y, SCREEN_W, HUD_HEIGHT);
    bmp_printf(FONT(FONT_MED, color, COLOR_BLACK), 10, HUD_Y + 3, "%s", text);
}

void astro_hud(const char *text, int color)
{
    BMP_LOCK(paint_hud(text, color));
}

void astro_hud_clear(void)
{
    BMP_LOCK(bmp_fill(COLOR_EMPTY, 0, HUD_Y, SCREEN_W, HUD_HEIGHT));
}
