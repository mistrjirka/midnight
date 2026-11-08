#include "astro_shortcut.h"
int astro_shortcut_decide(int camera, int raw_key, int enabled,
                          int liveview_photo, int menu_open, int busy,
                          int electronic_af_available)
{
    if(!enabled || !liveview_photo || menu_open || busy)return ASTRO_SHORTCUT_PASS;
    /* 600D BGMT_PRESS_DISP = 8, 6D BGMT_LIGHT = 0x20 */
    int native_key=(camera==ASTRO_CAMERA_600D)?8:
                   ((camera==ASTRO_CAMERA_6D)?0x20:-1);
    if(raw_key!=native_key)return ASTRO_SHORTCUT_PASS;
    return electronic_af_available?
           ASTRO_SHORTCUT_AUTOFOCUS:ASTRO_SHORTCUT_TOGGLE_READOUT;
}
