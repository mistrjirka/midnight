/* SPDX-License-Identifier: GPL-2.0-only */
/* Result screen: the captured frame contrast-stretched to 600x400, overlays on top. */
#include <dryos.h>
#include <bmp.h>
#include <mem.h>
#include <menu.h>
#include <powersave.h>
#include <string.h>
#include "draw.h"
#include "overlay.h"
#include "result_view.h"
#include "state.h"

#define VIEW_X        60
#define VIEW_Y        30
#define VIEW_W        600
#define VIEW_H        400
#define LOW_PERMILLE  10  /* black point: 1 % of samples darker */
#define HIGH_PERMILLE 998 /* white point: 0.2 % brighter */
#define MIN_RANGE     10
#define MENU_CLOSE_MS 350

/* Image -> screen scale 5/3 (360 -> 600). */
static const astro_map view_map = {
    5, 3, VIEW_X, VIEW_Y, VIEW_X, VIEW_Y, VIEW_X + VIEW_W - 1, VIEW_Y + VIEW_H - 1};

static struct {
    uint8_t *frame;
    uint8_t lut[256]; /* display stretch only; the solver never sees it */
    astro_solve_result result;
    astro_scene scene;
    volatile int visible;
} view;

int astro_result_keep_frame(const uint8_t *image)
{
    if (!view.frame) {
        view.frame = malloc(ASTRO_IMAGE_BYTES);
    }
    if (view.frame) {
        memcpy(view.frame, image, ASTRO_IMAGE_BYTES);
    }
    return view.frame != NULL;
}

static int percentile(const uint16_t *histogram, int threshold)
{
    int count = 0;
    for (int level = 0; level < 256; level++) {
        count += histogram[level];
        if (count > threshold) {
            return level;
        }
    }
    return 255;
}

static int clamp_percent(int value)
{
    if (value < 0) {
        return 0;
    }
    return value > 100 ? 100 : value;
}

/* Percentile stretch so faint stars are visible on the LCD. */
static void build_lut(void)
{
    uint16_t histogram[256] = {0};
    int samples = 0;

    for (int i = 0; i < ASTRO_IMAGE_BYTES; i += 4) {
        histogram[view.frame[i]]++;
        samples++;
    }
    int black = percentile(histogram, samples * LOW_PERMILLE / 1000);
    int white = percentile(histogram, samples * HIGH_PERMILLE / 1000);
    if (white <= black + MIN_RANGE) {
        white = black + MIN_RANGE;
    }
    for (int level = 0; level < 256; level++) {
        int value = clamp_percent((level - black) * 100 / (white - black));
        view.lut[level] = (uint8_t)COLOR_GRAY(value);
    }
}

void astro_result_show(const astro_solve_result *result, const astro_scene *scene)
{
    if (!view.frame) {
        return;
    }
    view.result = *result;
    view.scene = *scene;
    build_lut();
    view.visible = 1;
    gui_stop_menu();
    msleep(MENU_CLOSE_MS);
    display_on();
    bmp_on();
    clrscr();
    astro_result_redraw();
}

int astro_result_visible(void)
{
    return view.visible;
}

void astro_result_hide(void)
{
    view.visible = 0;
    clrscr();
}

static void blit_frame(uint8_t *vram)
{
    for (int y = 0; y < VIEW_H; y++) {
        const uint8_t *row = view.frame + (y * 3 / 5) * ASTRO_IMAGE_WIDTH;
        uint8_t *out = vram + (y + VIEW_Y) * BMPPITCH + VIEW_X;
        for (int x = 0; x < VIEW_W; x++) {
            out[x] = view.lut[row[x * 3 / 5]];
        }
    }
}

static void draw_text(void)
{
    const astro_solve_result *r = &view.result;
    char target[64];

    astro_target_status(&view.scene, target, sizeof(target));
    bmp_printf(FONT(FONT_MED, COLOR_YELLOW, COLOR_BLACK), 12, 2, "SKY FOUND   %d stars",
               r->sky.matched);
    bmp_printf(FONT(FONT_MED, COLOR_WHITE, COLOR_BLACK), 12, 432, "RA %s  Dec %s", r->ra_text,
               r->dec_text);
    if (target[0]) {
        bmp_printf(FONT(FONT_SMALL, COLOR_RED, COLOR_BLACK), 430, 438, "%s", target);
    }
    bmp_printf(FONT(FONT_SMALL, COLOR_WHITE, COLOR_BLACK), 12, 461,
               "%s %d.%02d s  lens ~%s  Q: back", r->hinted ? "Hinted" : "Blind",
               r->elapsed_ms / 1000, r->elapsed_ms % 1000 / 10, r->lens_text);
}

/* Whole screen: frame, overlays, matched stars and text. Call under BMP_LOCK. */
static void paint(void)
{
    uint8_t *vram = bmp_vram();

    if (!vram) {
        return;
    }
    bmp_fill(COLOR_BLACK, 0, 0, 720, 480);
    blit_frame(vram);
    astro_draw_scene(&view.scene, &view_map, 0);
    astro_draw_matches(&view.result, &view_map);
    draw_text();
}

void astro_result_redraw(void)
{
    if (!view.visible || !view.frame || gui_menu_shown()) {
        return;
    }
    display_on();
    bmp_on();
    BMP_LOCK(paint());
}
