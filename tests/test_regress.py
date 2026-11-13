"""Port vs. original Midnight: identical inputs must give identical outputs.

These pin the readability rewrite to the behaviour of the code that passed QEMU. Deliberate
behaviour changes are listed in tests/README.md and excluded here explicitly.
"""
from __future__ import annotations

import ctypes as C
import math
import random

import pytest

import camera_model as cm
from native import (CAT, H, MAX_STARS, REFERENCE_DATA, W, OverlayOpts, Scene, SkyOptions,
                    SkySolution, Star, Target, Wcs, as_bytes, load_stars, midnight, port)

pytestmark = pytest.mark.skipif(not (REFERENCE_DATA / "SKY.DIR").exists(),
                                reason="reference data missing: run `astroml data reference`")

SKY_BUDGET_EXHAUSTED = 2  # port-only status; the original reports it as "no match" (0)


@pytest.fixture(scope="module")
def libs():
    new, old = port(), midnight()
    assert new.harness_open_sky(str(REFERENCE_DATA).encode(), b"SKY") == 0
    assert old.open_sky(str(REFERENCE_DATA).encode(), b"SKY") == 0
    assert new.harness_open_files(str(REFERENCE_DATA).encode())
    assert old.open_files(str(REFERENCE_DATA).encode())
    return new, old


@pytest.fixture(scope="module")
def stars():
    return load_stars(REFERENCE_DATA / "STARS.BIN")


def random_wcs(rng: random.Random) -> Wcs:
    focal = rng.choice(cm.FOCALS_MM)
    k = W * focal / 35.8 * math.pi / 180000 * (1 << 24)
    roll = math.radians(rng.uniform(-180, 180))
    s = -1 if rng.random() < 0.5 else 1
    w = Wcs()
    w.ra0 = rng.randrange(0, 3600000)
    w.dec0 = rng.randrange(-899000, 899000)
    w.m[0], w.m[1] = round(-s * k * math.cos(roll)), round(s * k * math.sin(roll))
    w.m[2], w.m[3] = round(-k * math.sin(roll)), round(-k * math.cos(roll))
    w.t[0], w.t[1] = W // 2 * 65536, H // 2 * 65536
    return w


def test_fixed_math_matches(libs):
    new, old = libs
    rng = random.Random(1)
    for _ in range(20000):
        angle = rng.randrange(-20000000, 20000000)
        a = [C.c_int32(), C.c_int32()]
        b = [C.c_int32(), C.c_int32()]
        new.astro_sincos(angle, C.byref(a[0]), C.byref(a[1]))
        old.astro_sincos(angle, C.byref(b[0]), C.byref(b[1]))
        assert (a[0].value, a[1].value) == (b[0].value, b[1].value), angle
        y, x = rng.randrange(-1 << 45, 1 << 45), rng.randrange(-1 << 45, 1 << 45)
        assert new.astro_atan2(y, x) == old.astro_atan2(y, x)
        v = rng.randrange(0, 1 << 63)
        assert new.astro_isqrt64(v) == old.astro_isqrt64(v)


def wcs_point_results(lib, w: Wcs, ra: int, dec: int, x_q4: int, y_q4: int):
    px, py = C.c_int32(), C.c_int32()
    ura, udec = C.c_uint32(), C.c_int32()
    north, east = (C.c_int32 * 2)(), (C.c_int32 * 2)()
    dx, dy = C.c_int32(), C.c_int32()
    return (
        lib.astro_wcs_project(C.byref(w), ra, dec, C.byref(px), C.byref(py)), px.value, py.value,
        lib.astro_wcs_unproject(C.byref(w), x_q4, y_q4, C.byref(ura), C.byref(udec)),
        ura.value, udec.value,
        lib.astro_wcs_axes(C.byref(w), ra, dec, north, east), list(north), list(east),
        lib.astro_wcs_direction(C.byref(w), w.ra0, w.dec0, ra, dec, C.byref(dx), C.byref(dy)),
        dx.value, dy.value,
        lib.astro_separation(w.ra0, w.dec0, ra, dec),
        lib.astro_wcs_fov_cdeg(C.byref(w), W, H),
    )


def test_wcs_matches(libs):
    new, old = libs
    rng = random.Random(2)
    for _ in range(3000):
        w = random_wcs(rng)
        ra = (w.ra0 + rng.randrange(-300000, 300000)) % 3600000
        dec = max(-900000, min(900000, w.dec0 + rng.randrange(-300000, 300000)))
        x_q4, y_q4 = rng.randrange(-2000, 8000), rng.randrange(-2000, 6000)
        assert wcs_point_results(new, w, ra, dec, x_q4, y_q4) == \
            wcs_point_results(old, w, ra, dec, x_q4, y_q4)
    for sensor in (100, 223, 358, 600):
        for focal in (4, 14, 50, 300, 3000):
            assert new.astro_lens_fov_cdeg(sensor, focal) == old.astro_lens_fov_cdeg(sensor, focal)
        for fov in (10, 500, 4000, 17000):
            assert new.astro_lens_focal_x10(sensor, fov) == old.astro_lens_focal_x10(sensor, fov)


def synthetic_images(stars, count, seed):
    rng = random.Random(seed)
    for i in range(count):
        cam = cm.random_camera(rng, cm.SENSORS_MM["6D"], rng.choice((35, 50, 85, 135)))
        visible = cm.visible_stars(stars, cam, 6.5)[:60]
        trail = None
        if i % 3 == 0:
            trail = ((rng.uniform(0, W), 0), (rng.uniform(0, W), H - 1))
        yield cm.render(visible, rng, trail=trail)


def test_streak_mask_and_detection_match(libs, stars):
    new, old = libs
    for image in synthetic_images(stars, 12, seed=3):
        a, b = C.create_string_buffer(image, W * H), C.create_string_buffer(image, W * H)
        assert new.astro_mask_streak(a, W, H, W) == old.astro_mask_streak(b, W, H, W)
        assert a.raw == b.raw
        sa, sb = (Star * MAX_STARS)(), (Star * MAX_STARS)()
        na = new.harness_detect(a, W, H, W, sa, MAX_STARS)
        nb = old.astro_find_stars_psf(b, W, H, W, sb, MAX_STARS)
        assert na == nb
        assert bytes(sa)[:na * 6] == bytes(sb)[:nb * 6]


def test_focus_metric_matches(libs, stars):
    new, old = libs
    for image in synthetic_images(stars, 8, seed=4):
        results = []
        for lib in (new, old):
            x, y = C.c_int(W // 2), C.c_int(H // 2)
            score = lib.astro_focus_metric(image, W, H, W, C.byref(x), C.byref(y))
            locked_x, locked_y = C.c_int(x.value), C.c_int(y.value)
            locked = (lib.astro_focus_metric_locked(image, W, H, W, C.byref(locked_x),
                                                    C.byref(locked_y), 7) if score >= 0 else -1)
            results.append((score, x.value, y.value, locked, locked_x.value, locked_y.value))
        assert results[0] == results[1]


def solve_both(libs, det, options=None):
    new, old = libs
    arr = (Star * len(det))(*(Star(round(x), round(y), max(1, 60000 - i * 100))
                              for i, (_, x, y) in enumerate(det)))
    opt = C.byref(options) if options else None
    sn, so = SkySolution(), SkySolution()
    rn = new.harness_solve(arr, len(det), W, H, opt, C.byref(sn))
    ro = old.run_sky(arr, len(det), W, H, opt, C.byref(so))
    return rn, sn, ro, so


@pytest.mark.parametrize("focal", [35, 50, 85, 135, 200])  # 6D: 24 mm is ~73 deg, beyond SKY.*
def test_sky_solver_matches(libs, stars, focal):
    rng = random.Random(1000 + focal)
    solved = 0
    for _ in range(6):
        cam = cm.random_camera(rng, cm.SENSORS_MM["6D"], focal)
        maglim = cm.limiting_mag(focal, rng)
        det = cm.detections(cm.visible_stars(stars, cam, maglim), rng, maglim)
        if len(det) < 8:
            continue
        for options in (None, SkyOptions(*cm.scale_window(cm.SENSORS_MM["6D"], focal))):
            rn, sn, ro, so = solve_both(libs, det, options)
            assert (0 if rn == SKY_BUDGET_EXHAUSTED else rn) == ro
            assert as_bytes(sn) == as_bytes(so)
            solved += rn == 1
    assert libs[0].harness_sky_oversized_reads() == 0
    assert solved > 0


def test_scene_matches_and_respects_read_limit(libs, stars):
    new, old = libs
    rng = random.Random(5)
    opts = OverlayOpts(1, 1, 1, 45, 90)
    for i in range(40):
        w = random_wcs(rng)
        target = Target()
        if i % 2:
            assert new.harness_lookup(CAT["M"], rng.randrange(1, 111), C.byref(target)) in (0, 1)
        sn, so = Scene(), Scene()
        rn = new.harness_scene(C.byref(w), W, H, C.byref(opts), C.byref(target), C.byref(sn))
        ro = old.scene(C.byref(w), W, H, C.byref(opts), C.byref(target), C.byref(so))
        assert rn == ro == 0
        assert as_bytes(sn) == as_bytes(so)
    # The original read 10372 bytes in one call here, which the camera reader rejected.
    assert new.harness_overlay_oversized_reads() == 0


def test_dso_lookup_matches(libs):
    new, old = libs
    for cat, top in ((1, 110), (2, 7840), (3, 5386), (4, 109), (5, 370), (6, 313)):
        for num in range(1, top + 1, max(1, top // 60)):
            tn, to = Target(), Target()
            assert new.harness_lookup(cat, num, C.byref(tn)) == old.lookup(cat, num, C.byref(to))
            assert as_bytes(tn) == as_bytes(to)
            if tn.valid:
                a, b = C.create_string_buffer(24), C.create_string_buffer(24)
                new.harness_common_name(C.byref(tn.obj), a, 24)
                old.common_name(C.byref(to.obj), b, 24)
                assert a.value == b.value
