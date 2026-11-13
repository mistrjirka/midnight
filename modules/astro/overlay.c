/* SPDX-License-Identifier: GPL-2.0-only */
/* Target lookup and overlay scene construction on the worker task. */
#include <dryos.h>
#include <string.h>
#include "overlay.h"
#include "sdcard.h"
#include "state.h"
#include "worker.h"

#define STAR_LABEL_MAG10 45 /* name stars up to magnitude 4.5 */

static struct {
    volatile int catalogue; /* what `target` holds; 0 = nothing looked up yet */
    volatile int number;
    astro_target target;
    char info[48];
} loaded;

int astro_target_current(void)
{
    if (loaded.catalogue == astro_cfg_target_catalogue &&
        loaded.number == astro_cfg_target_number) {
        return 1;
    }
    astro_request(ASTRO_ACTION_LOOKUP_TARGET);
    return 0;
}

const astro_target *astro_target_get(void)
{
    return &loaded.target;
}

const char *astro_target_info(void)
{
    return loaded.info;
}

static void describe(const astro_sky_files *files, int status)
{
    const astro_dso *o = &loaded.target.object;
    char common[ASTRO_NAME_BYTES];

    if (status != 1) {
        snprintf(loaded.info, sizeof(loaded.info), "%s",
                 status == 0 ? "Not in catalogue" : "DSO.BIN/KEY missing");
        return;
    }
    astro_dso_common_name(files, o, common, sizeof(common));
    if (common[0]) {
        snprintf(loaded.info, sizeof(loaded.info), "%s", common);
    } else if (o->mag10 != ASTRO_MAG_UNKNOWN) {
        int mag = o->mag10 < 0 ? -o->mag10 : o->mag10;
        snprintf(loaded.info, sizeof(loaded.info), "%s, mag %s%d.%d", astro_dso_type_name(o->type),
                 o->mag10 < 0 ? "-" : "", mag / 10, mag % 10);
    } else {
        snprintf(loaded.info, sizeof(loaded.info), "%s", astro_dso_type_name(o->type));
    }
}

void astro_target_lookup(void)
{
    astro_catalogue_files c;
    int catalogue = astro_cfg_target_catalogue;
    int number = astro_cfg_target_number;

    if (loaded.catalogue == catalogue && loaded.number == number) {
        return;
    }
    astro_catalogue_open(&c);
    int status = astro_dso_lookup(&c.files, catalogue, number, &loaded.target);
    describe(&c.files, status);
    astro_catalogue_close(&c);
    loaded.catalogue = catalogue;
    loaded.number = number;
}

void astro_build_overlay(const astro_solve_result *result, astro_scene *scene)
{
    astro_overlay_opts opts = {astro_cfg_overlay_stars, astro_cfg_overlay_dso,
                               astro_cfg_overlay_constellations, STAR_LABEL_MAG10,
                               astro_cfg_overlay_dso_mag * 10};
    astro_catalogue_files c;

    astro_target_lookup();
    const astro_target *target =
        astro_cfg_mark_target && loaded.target.valid ? &loaded.target : NULL;
    astro_catalogue_open(&c);
    if (astro_build_scene(&result->sky.wcs, ASTRO_IMAGE_WIDTH, ASTRO_IMAGE_HEIGHT, &opts, target,
                          &c.files, scene) < 0) {
        DryosDebugMsg(0, 15, "ASTRO overlay data file corrupt");
    }
    astro_catalogue_close(&c);
}

void astro_target_status(const astro_scene *scene, char *out, int size)
{
    unsigned whole = scene->target_separation / 10000;
    unsigned tenth = scene->target_separation / 1000 % 10;

    out[0] = 0;
    if (scene->target == ASTRO_TARGET_IN_FRAME) {
        snprintf(out, size, "%s in view, %d.%d deg from centre", scene->target_label, whole, tenth);
    } else if (scene->target == ASTRO_TARGET_OUTSIDE) {
        snprintf(out, size, "%s %d.%d deg away: follow the arrow", scene->target_label, whole,
                 tenth);
    }
}
