"""Blind solver on synthetic star lists from an independent floating-point camera model.

A wrong answer is worse than none: any wrong sky position fails. Pass rates are enforced for
the field sizes the default index is built for.
"""
from __future__ import annotations

import ctypes as C
import math
import random

import pytest

import camera_model as cm
from native import (DATA, REFERENCE_DATA, H, W, SkyOptions, SkySolution, Star, load_stars,
                    port_instance, separation)

pytestmark = pytest.mark.skipif(not (DATA / "SKY.DIR").exists(),
                                reason="data missing: run `astroml data all`")

SENSOR = cm.SENSORS_MM["6D"]
WRONG_DEG = 2.0  # a solution farther than this from the truth is wrong


@pytest.fixture(scope="module")
def stars():
    return load_stars(DATA / "STARS.BIN")


def open_index(tag: str, root) -> C.CDLL:
    lib = port_instance(tag)
    assert lib.harness_open_sky(str(root).encode(), b"SKY") == 0
    return lib


def solve(lib, det, options=None):
    arr = (Star * len(det))(*(Star(round(x), round(y), max(1, 60000 - i * 100))
                              for i, (_, x, y) in enumerate(det)))
    sol = SkySolution()
    rc = lib.harness_solve(arr, len(det), W, H, C.byref(options) if options else None,
                           C.byref(sol))
    return rc, sol


def run_frames(lib, stars, focal, count, rng, dec_range=None, hint=False):
    """Returns (solved, attempted, wrong)."""
    solved = attempted = wrong = 0
    for _ in range(count):
        cam = cm.random_camera(rng, SENSOR, focal)
        if dec_range:
            cam = cm.Camera(rng.uniform(0, 360), rng.uniform(*dec_range), rng.uniform(-180, 180),
                            rng.random() < 0.5, SENSOR, focal)
        maglim = cm.limiting_mag(focal, rng)
        det = cm.detections(cm.visible_stars(stars, cam, maglim), rng, maglim)
        if len(det) < 8:
            continue
        attempted += 1
        options = SkyOptions(*cm.scale_window(SENSOR, focal)) if hint else None
        rc, sol = solve(lib, det, options)
        if rc != 1:
            continue
        if separation(sol.ra / 1e4, sol.dec / 1e4, cam.ra, cam.dec) > WRONG_DEG:
            wrong += 1
        else:
            solved += 1
    return solved, attempted, wrong


@pytest.mark.parametrize("focal", [85, 135, 200, 300])
def test_north_pole_is_covered(stars, focal):
    """Polar alignment points at the celestial pole."""
    lib = open_index("polar", DATA)
    solved, attempted, wrong = run_frames(lib, stars, focal, 20, random.Random(focal),
                                          dec_range=(88.0, 90.0))
    assert wrong == 0
    assert attempted >= 15
    assert solved >= 0.85 * attempted, f"{solved}/{attempted} at {focal} mm near the pole"


@pytest.mark.skipif(not (REFERENCE_DATA / "SKY.DIR").exists(), reason="no reference index")
def test_polar_layout_beats_original_at_the_pole(stars):
    """The original bands stop at +84.6..85.4 degrees for narrow fields: at 300 mm near the
    north pole it solved 15/40 synthetic frames against 37/40 with the polar layout."""
    rates = {}
    for tag, root in (("midnight", REFERENCE_DATA), ("polar", DATA)):
        solved, attempted, wrong = run_frames(open_index(tag, root), stars, 300, 30,
                                              random.Random(300), dec_range=(88.0, 90.0))
        assert wrong == 0
        rates[tag] = solved / attempted
    assert rates["polar"] >= rates["midnight"] + 0.25, rates


@pytest.mark.slow
@pytest.mark.parametrize("focal", [35, 50, 85, 135, 200])
@pytest.mark.parametrize("hint", [False, True])
def test_blind_solve_rates(stars, focal, hint):
    lib = open_index("polar", DATA)
    solved, attempted, wrong = run_frames(lib, stars, focal, 12, random.Random(focal * 7 + hint),
                                          hint=hint)
    assert wrong == 0
    fov = math.degrees(2 * math.atan(SENSOR / (2 * focal)))
    if 9 <= fov <= 55:
        assert solved >= 0.85 * attempted - 1, f"{solved}/{attempted} at {focal} mm"
