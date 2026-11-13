"""Full camera pipeline on rendered frames: trail mask -> DoG detection -> blind solve."""
from __future__ import annotations

import ctypes as C

import pytest

from native import DATA, MAX_STARS, H, W, SkySolution, Star, port_instance, separation
from tools.fixtures import render_star_field

pytestmark = pytest.mark.skipif(not (DATA / "SKY.DIR").exists(),
                                reason="data missing: run `astroml data all`")

FRAMES = [  # ra, dec, roll, focal, trail
    (83.82, -5.39, 12.0, 85, False),    # Orion
    (279.23, 38.78, -40.0, 50, True),   # Vega, with a satellite trail
    (37.95, 89.26, 70.0, 200, False),   # Polaris: polar alignment territory
    (10.68, 41.27, 160.0, 135, False),  # Andromeda
]


@pytest.fixture(scope="module")
def lib():
    lib = port_instance("pipeline")
    assert lib.harness_open_sky(str(DATA).encode(), b"SKY") == 0
    lib.astro_mask_streak.argtypes = [C.c_char_p, C.c_int, C.c_int, C.c_int]
    lib.harness_detect.argtypes = [C.c_char_p, C.c_int, C.c_int, C.c_int, C.POINTER(Star),
                                   C.c_int]
    return lib


@pytest.mark.parametrize("ra,dec,roll,focal,trail", FRAMES)
def test_rendered_frame_solves(lib, ra, dec, roll, focal, trail):
    image = render_star_field(DATA / "STARS.BIN", ra, dec, roll, focal, maglim=6.5, trail=trail)
    buffer = C.create_string_buffer(image.tobytes(), W * H)
    masked = lib.astro_mask_streak(buffer, W, H, W)
    assert masked == trail
    stars = (Star * MAX_STARS)()
    count = lib.harness_detect(buffer, W, H, W, stars, MAX_STARS)
    assert count >= 8
    solution = SkySolution()
    assert lib.harness_solve(stars, count, W, H, None, C.byref(solution)) == 1
    assert separation(solution.ra / 1e4, solution.dec / 1e4, ra, dec) < 0.05
    assert solution.matched >= 8


def test_trail_masker_detects_trails_and_ignores_dense_fields(lib):
    """Over 48 random frames the masker flags every trail and no clean frame. The original
    flagged 4 of 60 clean frames: glow and dense star fields passed for a trail."""
    import random

    rng = random.Random(7)
    wrong = []
    for i in range(48):
        trail = i % 2 == 0
        focal = rng.choice([24, 35, 50, 85, 135, 200])
        image = render_star_field(DATA / "STARS.BIN", rng.uniform(0, 360), rng.uniform(-60, 80),
                                  rng.uniform(-180, 180), focal, maglim=rng.choice([5.5, 6.5, 7.5]),
                                  trail=trail)
        buffer = C.create_string_buffer(image.tobytes(), W * H)
        if bool(lib.astro_mask_streak(buffer, W, H, W)) != trail:
            wrong.append((i, trail, focal))
    assert not wrong, wrong
