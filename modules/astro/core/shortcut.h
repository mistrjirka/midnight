/* SPDX-License-Identifier: GPL-2.0-only */
/* What a raw button press should do. Pure logic, testable on the host. */
#ifndef ASTRO_SHORTCUT_H
#define ASTRO_SHORTCUT_H

/* Special shortcut: 600D DISP or 6D LCD-light. No 550D equivalent. */
#define ASTRO_KEY_600D_DISP 0x08
#define ASTRO_KEY_6D_LIGHT  0x20

typedef enum {
    ASTRO_SHORTCUT_PASS = 0, /* let Canon handle the key */
    ASTRO_SHORTCUT_AUTOFOCUS = 1,
    ASTRO_SHORTCUT_TOGGLE_READOUT = 2 /* manual lens: toggle the star-size readout */
} astro_shortcut_action;

typedef struct {
    int enabled;
    int photo_liveview; /* photo-mode Live View with an idle display */
    int menu_open;
    int busy;
    int electronic_af; /* an AF lens that accepts focus commands */
} astro_shortcut_state;

astro_shortcut_action astro_shortcut_decide(int raw_key, const astro_shortcut_state *state);

#endif
