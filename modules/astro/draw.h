/* SPDX-License-Identifier: GPL-2.0-only */
/* Bitmap rendering of overlay scenes, matched stars and status lines. */
#ifndef ASTRO_DRAW_H
#define ASTRO_DRAW_H

#include "core/scene.h"
#include "solve.h"

/* Image (Q4) -> screen: sx = ox + x_q4 * num / (den * 16). Clip rectangle x0..x1, y0..y1. */
typedef struct {
    int num, den;
    int ox, oy;
    int x0, y0, x1, y1;
} astro_map;

/* 360x240 analysis image -> full 720x480 Live View/playback overlay, above the HUD line. */
extern const astro_map astro_liveview_map;

/* erase != 0 repaints the identical shapes transparent (for Live View refreshes). */
void astro_draw_scene(const astro_scene *scene, const astro_map *map, int erase);
void astro_draw_matches(const astro_solve_result *result, const astro_map *map);
void astro_draw_arrow(const astro_map *map, int x_q4, int y_q4, int to_x_q4, int to_y_q4,
                      int color);

/* ASCII, plus bytes 0x01..0x18 drawn as Greek letters alpha..omega. */
int astro_label_width(const char *text);
void astro_draw_label(int x, int y, const char *text, int fg, int bg);

/* Status line at the bottom of the Live View screen. */
void astro_hud(const char *text, int color);
void astro_hud_clear(void);

#endif
