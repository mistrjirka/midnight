/* Camera bitmap rendering of an astro_scene. GPL-2.0-or-later.
 * Magic Lantern only (bmp.h); all geometry comes from astro_overlay.c. */
#ifndef ASTRO_DRAW_H
#define ASTRO_DRAW_H
#include "astro_overlay.h"
/* Image (Q4) -> screen: sx = ox + x16*num/(den*16). Clip rectangle x0..x1, y0..y1. */
typedef struct { int num, den, ox, oy, x0, y0, x1, y1; } astro_map;
/* erase=1 repaints the identical shapes transparent (Live View refresh). */
void astro_draw_scene(const astro_scene *scene, const astro_map *map, int erase);
/* ASCII text where bytes 0x01..0x18 are Greek letters alpha..omega. */
int astro_label_width(const char *text);
void astro_draw_label(int x, int y, const char *text, int fg, int bg);
#endif
