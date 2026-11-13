"""Two real 6D frames (60 s, ISO 800/1600, manual lens) through the playback pipeline.

RAW2 shows Orion with a satellite trail; its solution is checked against the sky itself:
catalogue positions projected through the WCS must land on bright pixels. RAW1 is a dense
Milky Way field in Perseus where most of the brightest detections are noise.
The PGMs are what the module reads in playback (see `astroml fixtures raw`); the CR2
originals stay out of git because their EXIF carries a serial number and owner name.
"""
from __future__ import annotations

import ctypes as C

import numpy as np
import pytest

from native import (DATA, MAX_STARS, REFERENCE_DATA, H, W, SkySolution, Star, port_instance,
                    separation)
from tools.fixtures import cr2_preview_jpeg, playback_luma

FRAMES = DATA.parent / "tests" / "data"
RAW1, RAW2 = FRAMES / "RAW1.PGM", FRAMES / "RAW2.PGM"
SOLVED, BUDGET_EXHAUSTED = 1, 2
MIDNIGHT_RAW1 = (58.668, 34.25)  # RA, Dec from the original index; 11 stars, one check
CONTRAST = 45  # peak over local sky; 99% of random 7x7 patches of RAW2 stay below 38

# Catalogue positions (J2000, degrees) that are visible in RAW2.
ORION = {"Mintaka": (83.0017, -0.2991), "Alnilam": (84.0534, -1.2019),
         "Alnitak": (85.1897, -1.9426), "M42": (83.8221, -5.3911)}

pytestmark = pytest.mark.skipif(not (DATA / "SKY.DIR").exists(),
                                reason="data missing: run `astroml data all`")


def pixels(path) -> bytes:
    return path.read_bytes()[-W * H:]


def blind_solve(index_root, tag: str, frame: bytes):
    lib = port_instance(tag)
    assert lib.harness_open_sky(str(index_root).encode(), b"SKY") == 0
    lib.harness_detect.argtypes = [C.c_char_p, C.c_int, C.c_int, C.c_int, C.POINTER(Star),
                                   C.c_int]
    buffer = C.create_string_buffer(frame, W * H)
    stars = (Star * MAX_STARS)()
    count = lib.harness_detect(buffer, W, H, W, stars, MAX_STARS)
    solution = SkySolution()
    status = lib.harness_solve(stars, count, W, H, None, C.byref(solution))
    return lib, status, solution


def test_raw2_solves_blind_and_lands_on_orion():
    frame = pixels(RAW2)
    lib, status, sol = blind_solve(DATA, "real_polar", frame)
    assert status == SOLVED
    assert separation(sol.ra / 1e4, sol.dec / 1e4, 85.237, -1.162) < 0.1
    assert sol.matched >= 12
    assert 14.5 <= sol.fov_cdeg / 100 <= 17  # about 130 mm on the 6D

    image = np.frombuffer(frame, np.uint8).reshape(H, W)
    for name, (ra, dec) in ORION.items():
        x, y = C.c_int32(), C.c_int32()
        assert lib.astro_wcs_project(C.byref(sol.wcs), int(ra * 1e4), int(dec * 1e4), C.byref(x),
                                     C.byref(y)) == 1, name
        column, row = round(x.value / 16), round(y.value / 16)
        assert 3 <= column < W - 3 and 3 <= row < H - 3, f"{name} projects outside the frame"
        window = image[row - 3:row + 4, column - 3:column + 4]
        sky = np.median(image[row - 12:row + 13, column - 12:column + 13])
        assert window.max() - sky >= CONTRAST, f"{name}: nothing bright at ({column}, {row})"


@pytest.mark.skipif(not (REFERENCE_DATA / "SKY.DIR").exists(), reason="no reference index")
def test_raw2_agrees_with_the_original_index():
    _, status, sol = blind_solve(REFERENCE_DATA, "real_midnight", pixels(RAW2))
    assert status == SOLVED
    assert separation(sol.ra / 1e4, sol.dec / 1e4, 85.237, -1.162) < 0.1


@pytest.mark.skipif(not (REFERENCE_DATA / "SKY.DIR").exists(), reason="no reference index")
def test_raw1_solves_with_the_original_index():
    _, status, sol = blind_solve(REFERENCE_DATA, "real_midnight", pixels(RAW1))
    assert status == SOLVED
    assert separation(sol.ra / 1e4, sol.dec / 1e4, *MIDNIGHT_RAW1) < 0.1


def test_raw1_never_gives_a_wrong_answer():
    """A noisy frame may go unsolved, but a position it reports must be the right one."""
    _, status, sol = blind_solve(DATA, "real_polar", pixels(RAW1))
    assert status in (SOLVED, BUDGET_EXHAUSTED)
    if status == SOLVED:
        assert separation(sol.ra / 1e4, sol.dec / 1e4, *MIDNIGHT_RAW1) < 0.5


def test_raw1_solves_with_the_default_index():
    """Dense Milky Way field at about 135 mm; averaging 2x2 blocks keeps the noise out."""
    _, status, sol = blind_solve(DATA, "real_polar", pixels(RAW1))
    assert status == SOLVED
    assert separation(sol.ra / 1e4, sol.dec / 1e4, *MIDNIGHT_RAW1) < 0.1
    assert sol.matched >= 10
    assert 13.5 <= sol.fov_cdeg / 100 <= 16  # a 135 mm lens on the 6D


def test_trail_masker_leaves_raw1_alone():
    """Its glow and dense Milky Way once passed for a trail and blanked 1332 px of stars."""
    lib = port_instance("real_mask")
    lib.astro_mask_streak.argtypes = [C.c_char_p, C.c_int, C.c_int, C.c_int]
    buffer = C.create_string_buffer(pixels(RAW1), W * H)
    assert lib.astro_mask_streak(buffer, W, H, W) == 0


@pytest.mark.parametrize("name", ["raw1", "raw2"])
def test_pgm_is_what_the_cr2_gives(name):
    """The committed PGM must stay in step with the conversion code."""
    original = FRAMES / "raw" / f"{name}.CR2"
    if not original.exists():
        pytest.skip("CR2 original not present (kept out of git)")
    luma = playback_luma(cr2_preview_jpeg(original))
    assert pixels(FRAMES / f"{name.upper()}.PGM") == luma.tobytes()
