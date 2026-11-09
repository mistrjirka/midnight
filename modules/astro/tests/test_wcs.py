#!/usr/bin/env python3
"""Integer/CORDIC sky projection (astro_wcs.c) versus floating-point maths.

Covers poles, RA wrap-around, mirror parity, 14-300 mm scales, inverse
projection, great-circle separation, arrow direction for far targets,
local north/east axes and lens field of view.
"""
import ctypes as C
import math
import random

from native import W, H, wcs_api, camera_wcs, separation, local_direction
from test_sky_solver import Camera

api = wcs_api()
rng = random.Random(4242)


def i32():
    return C.c_int32()


worst_px = worst_inv = worst_sep = worst_dir = worst_axis = 0.0
for trial in range(400):
    ra0 = rng.uniform(0, 360)
    dec0 = rng.choice([rng.uniform(-90, 90), rng.uniform(85, 89.9), rng.uniform(-89.9, -85)])
    focal = rng.choice([14, 24, 50, 135, 300])
    cam = Camera(ra0, dec0, rng.uniform(-180, 180), rng.random() < 0.5, 22.3, focal)
    w = camera_wcs(ra0, dec0, math.degrees(math.atan2(cam.sr, cam.cr)), cam.mirror, cam.k)
    reach = min(60.0, cam.fov)
    for _ in range(10):
        # Random point within the field (and across RA = 0 when near it).
        ra = (ra0 + rng.uniform(-1, 1) * reach / max(0.05, math.cos(math.radians(dec0)))) % 360
        dec = max(-89.99, min(89.99, dec0 + rng.uniform(-reach, reach) / 2))
        if separation(ra0, dec0, ra, dec) > 60:
            continue
        ref = cam.project(ra, dec)
        x, y = i32(), i32()
        assert api.astro_wcs_project(C.byref(w), round(ra * 1e4) % 3600000, round(dec * 1e4),
                                     C.byref(x), C.byref(y))
        err = math.hypot(x.value / 16 - ref[0], y.value / 16 - ref[1])
        # Error relative to the 1e-4 degree input quantisation, in pixels.
        quantum = cam.k * math.radians(1e-4) / math.cos(math.radians(separation(ra0, dec0, ra, dec))) ** 2
        worst_px = max(worst_px, err / max(1.0, 3 * quantum))
        if abs(x.value) < 30000 * 16 and abs(y.value) < 30000 * 16:
            r2, d2 = C.c_uint32(), i32()
            assert api.astro_wcs_unproject(C.byref(w), x, y, C.byref(r2), C.byref(d2))
            worst_inv = max(worst_inv, separation(r2.value / 1e4, d2.value / 1e4, ra, dec) * cam.k * math.pi / 180)
        sep = api.astro_separation(w.ra0, w.dec0, round(ra * 1e4) % 3600000, round(dec * 1e4)) / 1e4
        worst_sep = max(worst_sep, abs(sep - separation(w.ra0 / 1e4, w.dec0 / 1e4, ra, dec)))
    # Arrow towards an arbitrary, possibly far or antipodal-ish, target.
    ra, dec = rng.uniform(0, 360), math.degrees(math.asin(rng.uniform(-1, 1)))
    if 0.5 < separation(ra0, dec0, ra, dec) < 175:
        dx, dy = i32(), i32()
        assert api.astro_wcs_direction(C.byref(w), w.ra0, w.dec0, round(ra * 1e4) % 3600000,
                                       round(dec * 1e4), C.byref(dx), C.byref(dy))
        rx, ry = local_direction(cam, w.ra0 / 1e4, w.dec0 / 1e4, ra, dec)
        angle = math.degrees(math.acos(max(-1, min(1, (dx.value * rx + dy.value * ry) / 16384))))
        worst_dir = max(worst_dir, angle)
    # North axis: 1 degree north of the centre (or south at the pole).
    north, east = (C.c_int32 * 2)(), (C.c_int32 * 2)()
    assert api.astro_wcs_axes(C.byref(w), w.ra0, w.dec0, north, east)
    step = 1 if dec0 < 89 else -1
    p = cam.project(w.ra0 / 1e4, w.dec0 / 1e4 + step)
    ref_n = ((p[0] - W / 2) * step, (p[1] - H / 2) * step)
    worst_axis = max(worst_axis, math.hypot(north[0] / 16 - ref_n[0], north[1] / 16 - ref_n[1]) /
                     max(1, math.hypot(*ref_n)))
    assert abs(math.hypot(east[0], east[1]) - math.hypot(north[0], north[1])) <= 2
    assert east[0] * north[0] + east[1] * north[1] == 0

print(f"projection error <= {worst_px:.2f} x input quantum, inverse {worst_inv:.3f} px, "
      f"separation {worst_sep*3600:.2f} arcsec, arrow {worst_dir:.3f} deg, north axis {worst_axis*100:.2f} %")
# Axes are Q4 pixels per degree: at 14 mm (4.7 px/deg) rounding alone is ~1 %,
# which also bounds the arrow direction. Ample for guidance and outlines.
assert worst_px < 1.0 and worst_inv < 0.15 and worst_sep < 2 / 3600 and worst_dir < 1.5 and worst_axis < 0.025

for sensor in (223, 358):
    for focal in (8, 14, 24, 50, 135, 300, 800):
        fov = api.astro_lens_fov_cdeg(sensor, focal) / 100
        ref = 2 * math.degrees(math.atan(sensor / 10 / (2 * focal)))
        assert abs(fov - ref) < 0.011, (sensor, focal, fov, ref)
        back = api.astro_lens_focal_x10(sensor, round(ref * 100)) / 10
        assert abs(back - focal) < max(0.15, focal * 0.003), (sensor, focal, back)
print("PASS: integer WCS agrees with floating point; lens FOV and focal inversion exact")
