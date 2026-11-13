/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The 6D writes its GPS fix into each photo's EXIF, but ML does not expose the receiver.
 * We therefore read the newest photo. GPS UTC time against the camera's local
 * DateTimeOriginal also yields the clock's UTC offset, needed for sidereal time.
 */
#include <dryos.h>
#include <fio-ml.h>
#include <mem.h>
#include <notify_box.h>
#include <string.h>
#include "core/astro_time.h"
#include "core/exif.h"
#include "location.h"
#include "sdcard.h"
#include "state.h"

#define PATH_BYTES      64
#define OFFSET_STEP_MIN 15 /* time zones are whole quarter hours */
#define MAX_OFFSET_MIN  (14 * 60)
#define NOTIFY_MS       6000

static const char *const prefixes[] = {"IMG_", "_MG_"}; /* sRGB / Adobe RGB file names */
static const char *const extensions[] = {"CR2", "JPG"};

static int64_t local_now_seconds(void)
{
    struct tm now;
    astro_datetime t;

    LoadCalendarFromRTC(&now);
    t.year = now.tm_year + 1900;
    t.month = now.tm_mon + 1;
    t.day = now.tm_mday;
    t.hour = now.tm_hour;
    t.minute = now.tm_min;
    t.second = now.tm_sec;
    return astro_seconds_since_j2000(&t);
}

int64_t astro_now_utc(void)
{
    return local_now_seconds() - (int64_t)astro_cfg_utc_offset * 60;
}

int astro_location_site(astro_site *site)
{
    if (!astro_cfg_location_valid) {
        return 0;
    }
    site->latitude = astro_cfg_latitude;
    site->longitude = astro_cfg_longitude;
    return 1;
}

int astro_location_age_minutes(void)
{
    if (!astro_cfg_location_valid) {
        return -1;
    }
    return (int)((astro_now_utc() - astro_cfg_location_time) / 60);
}

void astro_location_source(char *out, int size)
{
    astro_datetime local;

    if (!astro_cfg_location_valid) {
        snprintf(out, size, "none: take a photo with GPS on");
        return;
    }
    astro_datetime_from_seconds((int64_t)astro_cfg_location_time + astro_cfg_utc_offset * 60,
                                &local);
    int age = astro_location_age_minutes();
    if (age < 0) {
        snprintf(out, size, "#%03d-%04d %02d-%02d %02d:%02d (bad clock)", astro_cfg_location_folder,
                 astro_cfg_location_file, local.month, local.day, local.hour, local.minute);
        return;
    }
    snprintf(out, size, "#%03d-%04d %02d-%02d %02d:%02d (%dh%02dm ago)", astro_cfg_location_folder,
             astro_cfg_location_file, local.month, local.day, local.hour, local.minute, age / 60,
             age % 60);
}

/* Fast path: the current file number in the current DCIM folder. */
static int current_number_path(char *path, int size)
{
    struct card_info *card = get_shooting_card();
    uint32_t bytes;

    for (unsigned p = 0; p < sizeof(prefixes) / sizeof(prefixes[0]); p++) {
        for (unsigned e = 0; e < sizeof(extensions) / sizeof(extensions[0]); e++) {
            snprintf(path, size, "%s/%s%04d.%s", get_dcim_dir(), prefixes[p], card->file_number,
                     extensions[e]);
            if (FIO_GetFileSize(path, &bytes) == 0 && bytes > 0) {
                return 1;
            }
        }
    }
    return 0;
}

static int is_photo(const char *name)
{
    int length = (int)strlen(name);
    if (length < 4 || name[length - 4] != '.') {
        return 0;
    }
    for (unsigned e = 0; e < sizeof(extensions) / sizeof(extensions[0]); e++) {
        if (!strcmp(name + length - 3, extensions[e])) {
            return 1;
        }
    }
    return 0;
}

/* Fallback: the JPG/CR2 with the latest timestamp in the current DCIM folder. */
static int newest_by_timestamp(char *path, int size)
{
    struct fio_file *entry = alloc_fio_file();
    uint64_t newest = 0;
    int found = 0;

    if (!entry) {
        return 0;
    }
    struct fio_dirent *dir = FIO_FindFirstEx(get_dcim_dir(), entry);
    if (!IS_ERROR(dir)) {
        do {
            struct file_info info = convert_fio_file_info(entry);
            if (is_photo(info.name) && info.size && (!found || info.timestamp >= newest)) {
                newest = info.timestamp;
                snprintf(path, size, "%s/%s", get_dcim_dir(), info.name);
                found = 1;
            }
        } while (FIO_FindNextEx(dir, entry) == 0);
        FIO_FindClose(dir);
    }
    free(entry);
    return found;
}

static int newest_photo_path(char *path, int size)
{
    return current_number_path(path, size) || newest_by_timestamp(path, size);
}

static int read_head(const char *path, uint8_t *buffer, uint32_t *length)
{
    uint32_t size = 0;

    if (FIO_GetFileSize(path, &size) != 0 || !size) {
        return 0;
    }
    *length = size < ASTRO_EXIF_HEAD_BYTES ? size : ASTRO_EXIF_HEAD_BYTES;
    FILE *f = FIO_OpenFile(path, O_RDONLY | O_SYNC);
    if (!f) {
        return 0;
    }
    int ok = astro_read_exact(f, buffer, *length);
    FIO_CloseFile(f);
    return ok;
}

/* Camera clock minus UTC, rounded to a quarter hour. Returns 0 if unknown. */
static int learn_utc_offset(const astro_exif *exif, int *minutes)
{
    if (!exif->has_capture_time || !exif->has_gps_time) {
        return 0;
    }
    int64_t difference =
        astro_seconds_since_j2000(&exif->capture_local) - astro_seconds_since_j2000(&exif->gps_utc);
    int64_t quarters = (difference + (difference >= 0 ? 450 : -450)) / (OFFSET_STEP_MIN * 60);
    *minutes = (int)(quarters * OFFSET_STEP_MIN);
    return *minutes >= -MAX_OFFSET_MIN && *minutes <= MAX_OFFSET_MIN;
}

/* Reads a decimal number of up to `digits` digits. */
static int parse_digits(const char *text, int digits)
{
    int value = 0;

    for (int i = 0; i < digits && text[i] >= '0' && text[i] <= '9'; i++) {
        value = value * 10 + (text[i] - '0');
    }
    return value;
}

/* "B:/DCIM/100CANON/IMG_0042.JPG" -> folder 100, file 42. */
static void parse_photo_id(const char *path, int *folder, int *file)
{
    const char *dcim = strstr(path, "DCIM/");
    const char *name = strrchr(path, '/');

    *folder = dcim ? parse_digits(dcim + 5, 3) : 0;
    *file = name && strlen(name) > 5 ? parse_digits(name + 5, 4) : 0;
}

static void store_fix(const astro_exif *exif, const char *path)
{
    int offset;

    if (learn_utc_offset(exif, &offset)) {
        astro_cfg_utc_offset = offset;
    }
    astro_cfg_latitude = exif->latitude;
    astro_cfg_longitude = exif->longitude;
    astro_cfg_location_time =
        (int)(exif->has_gps_time ? astro_seconds_since_j2000(&exif->gps_utc)
                                 : astro_seconds_since_j2000(&exif->capture_local) -
                                       (int64_t)astro_cfg_utc_offset * 60);
    parse_photo_id(path, &astro_cfg_location_folder, &astro_cfg_location_file);
    astro_cfg_location_valid = 1;
}

static void announce(void)
{
    char source[PATH_BYTES];
    int lat = astro_cfg_latitude;
    int lon = astro_cfg_longitude;
    int stale = astro_location_age_minutes() < 0 ||
                astro_location_age_minutes() > ASTRO_LOCATION_STALE_HOURS * 60;

    astro_location_source(source, sizeof(source));
    astro_set_status("%d.%02d%s %d.%02d%s", (lat < 0 ? -lat : lat) / 10000,
                     (lat < 0 ? -lat : lat) / 100 % 100, lat < 0 ? "S" : "N",
                     (lon < 0 ? -lon : lon) / 10000, (lon < 0 ? -lon : lon) / 100 % 100,
                     lon < 0 ? "W" : "E");
    NotifyBox(NOTIFY_MS, "Location %s:\n%s\n%s", stale ? "may be OLD" : "set", astro_status(),
              source);
}

void astro_location_read_last_photo(void)
{
    char path[PATH_BYTES];
    uint32_t length = 0;
    astro_exif exif;

    if (!newest_photo_path(path, sizeof(path))) {
        astro_set_status("No photo found in %s", get_dcim_dir());
        NotifyBox(NOTIFY_MS, "Astro: %s", astro_status());
        return;
    }
    uint8_t *head = malloc(ASTRO_EXIF_HEAD_BYTES);
    int ok = head && read_head(path, head, &length) && astro_exif_parse(head, length, &exif);
    free(head);
    if (!ok || !exif.has_gps) {
        astro_set_status("%s: %s", path + strlen(path) - 12,
                         ok ? "no GPS fix (turn GPS on)" : "unreadable EXIF");
        NotifyBox(NOTIFY_MS, "Astro: %s", astro_status());
        return;
    }
    store_fix(&exif, path);
    announce();
}
