/* SPDX-License-Identifier: GPL-2.0-only */
#include <dryos.h>
#include <bmp.h>
#include <menu.h>
#include <stdarg.h>
#include <string.h>
#include <timer.h>
#include "progress.h"
#include "worker.h"

#define WINDOW_X      24
#define WINDOW_Y      30
#define WINDOW_W      672
#define TITLE_H       38
#define LINE_H        26
#define LINES         10
#define FOOTER_H      32
#define WINDOW_H      (TITLE_H + LINES * LINE_H + FOOTER_H)
#define TEXT_X        (WINDOW_X + 14)
#define LINE_BYTES    48 /* the sans font is proportional; this many characters always fit */
#define TITLE_BYTES   32
#define REPAINT_MS    150 /* counters change faster than the screen needs to */
#define HOLD_STEP_MS  100
#define MENU_CLOSE_MS 350

static struct {
    int active;
    int menu_was_open;
    int last_is_progress;
    int count; /* lines in use, at most LINES */
    uint32_t started_ms;
    uint32_t painted_ms;
    char title[TITLE_BYTES];
    char lines[LINES][LINE_BYTES];
    const char *footer;
} win;

static uint32_t elapsed_ms(void)
{
    return (uint32_t)get_ms_clock() - win.started_ms;
}

static void paint_window(void)
{
    bmp_fill(COLOR_BLACK, WINDOW_X, WINDOW_Y, WINDOW_W, WINDOW_H);
    bmp_draw_rect(COLOR_WHITE, WINDOW_X, WINDOW_Y, WINDOW_W, WINDOW_H);
    bmp_printf(FONT(FONT_MED, COLOR_YELLOW, COLOR_BLACK), TEXT_X, WINDOW_Y + 6, "%s", win.title);
    for (int i = 0; i < win.count; i++) {
        bmp_printf(FONT(FONT_MED, COLOR_WHITE, COLOR_BLACK), TEXT_X,
                   WINDOW_Y + TITLE_H + i * LINE_H, "%s", win.lines[i]);
    }
    bmp_printf(FONT(FONT_MED, COLOR_GRAY(60), COLOR_BLACK), TEXT_X,
               WINDOW_Y + WINDOW_H - FOOTER_H + 2, "%s", win.footer);
}

static void paint(void)
{
    win.painted_ms = (uint32_t)get_ms_clock();
    BMP_LOCK(paint_window());
}

/* Formats "  3.2 s  text" into the last line, scrolling when the window is full. */
static void put_line(int new_line, const char *format, va_list args)
{
    char text[LINE_BYTES];
    uint32_t ms = elapsed_ms();

    if (new_line) {
        if (win.count == LINES) {
            memmove(win.lines[0], win.lines[1], sizeof(win.lines) - sizeof(win.lines[0]));
            win.count--;
        }
        win.count++;
    }
    vsnprintf(text, sizeof(text), format, args);
    snprintf(win.lines[win.count - 1], LINE_BYTES, "%2d.%d s  %s", ms / 1000, ms % 1000 / 100,
             text);
}

void astro_log_begin(const char *title)
{
    memset(&win, 0, sizeof(win));
    snprintf(win.title, sizeof(win.title), "%s", title);
    win.footer = "Q: cancel";
    win.menu_was_open = gui_menu_shown();
    if (win.menu_was_open) {
        gui_stop_menu();
        msleep(MENU_CLOSE_MS);
    }
    bmp_on();
    win.started_ms = (uint32_t)get_ms_clock();
    win.active = 1;
    paint();
}

void astro_log(const char *format, ...)
{
    if (!win.active) {
        return;
    }
    va_list args;
    va_start(args, format);
    put_line(1, format, args);
    va_end(args);
    win.last_is_progress = 0;
    paint();
}

void astro_log_progress(const char *format, ...)
{
    if (!win.active) {
        return;
    }
    va_list args;
    va_start(args, format);
    put_line(!win.last_is_progress, format, args);
    va_end(args);
    win.last_is_progress = 1;
    if ((uint32_t)get_ms_clock() - win.painted_ms >= REPAINT_MS) {
        paint();
    }
}

void astro_log_handoff(void)
{
    win.active = 0;
}

void astro_log_end(int hold_ms)
{
    if (!win.active) {
        return;
    }
    if (hold_ms > 0 && !astro_cancel_requested()) {
        win.footer = "Q: close";
        paint();
        for (int waited = 0; waited < hold_ms && !astro_cancel_requested();
             waited += HOLD_STEP_MS) {
            msleep(HOLD_STEP_MS);
        }
    }
    win.active = 0;
    BMP_LOCK(bmp_fill(COLOR_EMPTY, WINDOW_X, WINDOW_Y, WINDOW_W, WINDOW_H));
    if (win.menu_was_open) {
        gui_open_menu();
    }
}
