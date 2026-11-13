"""ctypes bindings for the host builds of the camera C code.

Two libraries are built from source with the host gcc:
  * ``port``     - AstroML's astro/core plus tests/harness/port_harness.c
  * ``midnight`` - the original tests/reference_midnight sources, used as a regression oracle

Struct layouts are identical in both, so the same ctypes classes serve both libraries.
"""
from __future__ import annotations

import ctypes as C
import functools
import math
import os
import pathlib
import struct
import subprocess

REPO = pathlib.Path(__file__).resolve().parents[1]
CORE = REPO / "modules" / "astro" / "core"
MIDNIGHT = REPO / "tests" / "reference_midnight"
BUILD = REPO / "tests" / "build"
DATA = pathlib.Path(os.environ.get("ASTRO_DATA", REPO / ".astro-data"))
REFERENCE_DATA = pathlib.Path(os.environ.get("ASTRO_REFERENCE_DATA", DATA / "reference"))

W, H = 360, 240
MAX_STARS = 32

CFLAGS = ["-shared", "-fPIC", "-std=gnu99", "-O2", "-Wall", "-Wextra", "-Wshadow", "-Werror"]
SANITIZE = ["-fsanitize=undefined", "-fno-sanitize-recover=undefined"]


class Star(C.Structure):
    _fields_ = [("x", C.c_int16), ("y", C.c_int16), ("flux", C.c_uint16)]


class Wcs(C.Structure):
    _fields_ = [("ra0", C.c_uint32), ("dec0", C.c_int32),
                ("m", C.c_int32 * 4), ("t", C.c_int32 * 2)]


class SkySolution(C.Structure):
    _fields_ = [("ra", C.c_uint32), ("dec", C.c_int32),
                ("field", C.c_uint32), ("quads", C.c_uint32),
                ("reads", C.c_uint32), ("checked", C.c_uint32),
                ("matched", C.c_int), ("mse100", C.c_int), ("fov_cdeg", C.c_int),
                ("matched_mask", C.c_uint32),
                ("names", C.c_uint16 * MAX_STARS),
                ("wcs", Wcs)]


class SkyOptions(C.Structure):
    _fields_ = [("scale_min", C.c_uint32), ("scale_max", C.c_uint32)]


class Dso(C.Structure):
    _fields_ = [("ra", C.c_uint32), ("dec", C.c_int32), ("maj", C.c_uint16),
                ("min", C.c_uint16), ("pa", C.c_int16), ("mag10", C.c_int16),
                ("type", C.c_uint8), ("cat", C.c_uint8), ("num", C.c_uint16),
                ("name", C.c_uint16), ("reserved", C.c_uint16)]


class Target(C.Structure):
    _fields_ = [("valid", C.c_int), ("obj", Dso), ("label", C.c_char * 24)]


class OverlayOpts(C.Structure):
    _fields_ = [("stars", C.c_int), ("dso", C.c_int), ("constellations", C.c_int),
                ("star_mag10", C.c_int), ("dso_mag10", C.c_int)]


class Line(C.Structure):
    _fields_ = [("x1", C.c_int16), ("y1", C.c_int16), ("x2", C.c_int16), ("y2", C.c_int16)]


class Ellipse(C.Structure):
    _fields_ = [("x", C.c_int16), ("y", C.c_int16), ("ax", C.c_int16), ("ay", C.c_int16),
                ("bx", C.c_int16), ("by", C.c_int16), ("kind", C.c_uint8), ("pad", C.c_uint8)]


class Label(C.Structure):
    _fields_ = [("x", C.c_int16), ("y", C.c_int16), ("kind", C.c_uint8),
                ("priority", C.c_uint8), ("text", C.c_char * 24)]


class Scene(C.Structure):
    _fields_ = [("width", C.c_int), ("height", C.c_int),
                ("nlines", C.c_int), ("nellipses", C.c_int), ("nlabels", C.c_int),
                ("lines", Line * 384), ("ellipses", Ellipse * 48), ("labels", Label * 64),
                ("target", C.c_int), ("target_x", C.c_int16), ("target_y", C.c_int16),
                ("target_dx", C.c_int16), ("target_dy", C.c_int16),
                ("target_sep", C.c_uint32), ("target_label", C.c_char * 24)]


CAT = {"M": 1, "NGC": 2, "IC": 3, "C": 4, "B": 5, "Sh2": 6}
LABEL_STAR, LABEL_DSO, LABEL_CONSTELLATION, LABEL_TARGET = 1, 2, 3, 4


def _compile(name: str, sources: list[pathlib.Path], includes: list[pathlib.Path],
             sanitize: bool) -> pathlib.Path:
    BUILD.mkdir(parents=True, exist_ok=True)
    out = BUILD / f"lib{name}.so"
    newest = max(p.stat().st_mtime for p in sources)
    if out.exists() and out.stat().st_mtime > newest:
        return out
    flags = CFLAGS + (SANITIZE if sanitize else [])
    cmd = ["gcc", *flags, *[f"-I{i}" for i in includes], "-o", str(out), *map(str, sources)]
    subprocess.run(cmd, check=True)
    return out


def _bind(lib: C.CDLL) -> C.CDLL:
    """Argument types for the functions shared by both libraries."""
    P = C.POINTER
    lib.astro_wcs_project.argtypes = [P(Wcs), C.c_uint32, C.c_int32, P(C.c_int32), P(C.c_int32)]
    lib.astro_wcs_unproject.argtypes = [P(Wcs), C.c_int32, C.c_int32, P(C.c_uint32), P(C.c_int32)]
    lib.astro_wcs_axes.argtypes = [P(Wcs), C.c_uint32, C.c_int32, C.c_int32 * 2, C.c_int32 * 2]
    lib.astro_separation.argtypes = [C.c_uint32, C.c_int32, C.c_uint32, C.c_int32]
    lib.astro_separation.restype = C.c_uint32
    lib.astro_wcs_direction.argtypes = [P(Wcs), C.c_uint32, C.c_int32, C.c_uint32, C.c_int32,
                                        P(C.c_int32), P(C.c_int32)]
    lib.astro_wcs_fov_cdeg.argtypes = [P(Wcs), C.c_int, C.c_int]
    lib.astro_lens_fov_cdeg.argtypes = [C.c_int, C.c_int]
    lib.astro_lens_focal_x10.argtypes = [C.c_int, C.c_int]
    lib.astro_sincos.argtypes = [C.c_int32, P(C.c_int32), P(C.c_int32)]
    lib.astro_atan2.argtypes = [C.c_int64, C.c_int64]
    lib.astro_atan2.restype = C.c_int32
    lib.astro_isqrt64.argtypes = [C.c_uint64]
    lib.astro_isqrt64.restype = C.c_uint32
    lib.astro_mask_streak.argtypes = [C.c_char_p, C.c_int, C.c_int, C.c_int]
    lib.astro_focus_metric.argtypes = [C.c_char_p, C.c_int, C.c_int, C.c_int,
                                       P(C.c_int), P(C.c_int)]
    lib.astro_focus_metric_locked.argtypes = [C.c_char_p, C.c_int, C.c_int, C.c_int,
                                              P(C.c_int), P(C.c_int), C.c_int]
    lib.astro_designation.argtypes = [C.c_int, C.c_int, C.c_char_p, C.c_int]
    return lib


@functools.cache
def port(sanitize: bool = True) -> C.CDLL:
    sources = sorted(CORE.glob("*.c")) + [REPO / "tests" / "harness" / "port_harness.c"]
    lib = _bind(C.CDLL(str(_compile("port_san" if sanitize else "port", sources, [CORE],
                                    sanitize))))
    P = C.POINTER
    lib.harness_open_sky.argtypes = [C.c_char_p, C.c_char_p]
    lib.harness_solve.argtypes = [P(Star), C.c_int, C.c_int, C.c_int, P(SkyOptions),
                                  P(SkySolution)]
    lib.harness_open_files.argtypes = [C.c_char_p]
    lib.harness_lookup.argtypes = [C.c_int, C.c_int, P(Target)]
    lib.harness_common_name.argtypes = [P(Dso), C.c_char_p, C.c_int]
    lib.harness_scene.argtypes = [P(Wcs), C.c_int, C.c_int, P(OverlayOpts), P(Target), P(Scene)]
    lib.harness_detect.argtypes = [C.c_char_p, C.c_int, C.c_int, C.c_int, P(Star), C.c_int]
    lib.astro_find_focus_stars.argtypes = [C.c_char_p, C.c_int, C.c_int, C.c_int, P(Star),
                                           C.c_int]
    return lib


def port_instance(tag: str) -> C.CDLL:
    """A separate copy of the port library: its harness holds one open index per copy."""
    base = pathlib.Path(port()._name)
    copy = BUILD / f"libport_{tag}.so"
    if not copy.exists() or copy.stat().st_mtime < base.stat().st_mtime:
        copy.write_bytes(base.read_bytes())
    lib = C.CDLL(str(copy))
    lib.harness_open_sky.argtypes = port().harness_open_sky.argtypes
    lib.harness_solve.argtypes = port().harness_solve.argtypes
    return lib


@functools.cache
def midnight() -> C.CDLL:
    names = ["astro_sky.c", "astro_wcs.c", "astro_overlay.c", "astro_psf.c", "astro_streak.c",
             "astro_core.c", "tests/sky_native_runner.c", "tests/overlay_native_runner.c"]
    lib = _bind(C.CDLL(str(_compile("midnight", [MIDNIGHT / n for n in names], [MIDNIGHT],
                                    sanitize=False))))
    P = C.POINTER
    lib.open_sky.argtypes = [C.c_char_p, C.c_char_p]
    lib.run_sky.argtypes = [P(Star), C.c_int, C.c_int, C.c_int, P(SkyOptions), P(SkySolution)]
    lib.open_files.argtypes = [C.c_char_p]
    lib.lookup.argtypes = [C.c_int, C.c_int, P(Target)]
    lib.common_name.argtypes = [P(Dso), C.c_char_p, C.c_int]
    lib.scene.argtypes = [P(Wcs), C.c_int, C.c_int, P(OverlayOpts), P(Target), P(Scene)]
    lib.astro_find_stars_psf.argtypes = [C.c_char_p, C.c_int, C.c_int, C.c_int, P(Star), C.c_int]
    lib.astro_find_stars.argtypes = [C.c_char_p, C.c_int, C.c_int, C.c_int, P(Star), C.c_int]
    return lib


def load_stars(path: pathlib.Path | None = None):
    """(ra_deg, dec_deg, mag, label_id) for every STARS.BIN record."""
    raw = (path or DATA / "STARS.BIN").read_bytes()
    magic, version, _, size = struct.unpack_from("<4sIII", raw)
    assert (magic, version, size) == (b"ASTR", 1, 12)
    return [(ra / 1e4, dec / 1e4, m10 / 10 - 5, label)
            for ra, dec, m10, label in struct.iter_unpack("<IiHH", raw[16:])]


def unit_vector(ra: float, dec: float):
    r, d = math.radians(ra), math.radians(dec)
    return (math.cos(d) * math.cos(r), math.cos(d) * math.sin(r), math.sin(d))


def separation(ra1, dec1, ra2, dec2) -> float:
    a, b = unit_vector(ra1, dec1), unit_vector(ra2, dec2)
    dot = max(-1.0, min(1.0, sum(x * y for x, y in zip(a, b))))
    cross = math.sqrt(sum(c * c for c in (a[1] * b[2] - a[2] * b[1],
                                           a[2] * b[0] - a[0] * b[2],
                                           a[0] * b[1] - a[1] * b[0])))
    return math.degrees(math.atan2(cross, dot))


def as_bytes(structure: C.Structure) -> bytes:
    return C.string_at(C.addressof(structure), C.sizeof(structure))


# ---- time, alt/az, EXIF, polar alignment (port only) ----
class DateTime(C.Structure):
    _fields_ = [("year", C.c_int), ("month", C.c_int), ("day", C.c_int),
                ("hour", C.c_int), ("minute", C.c_int), ("second", C.c_int)]


class Site(C.Structure):
    _fields_ = [("latitude", C.c_int32), ("longitude", C.c_int32)]


class Horizontal(C.Structure):
    _fields_ = [("altitude", C.c_int32), ("azimuth", C.c_uint32), ("visibility", C.c_int),
                ("minutes_to_change", C.c_int32)]


class Exif(C.Structure):
    _fields_ = [("has_capture_time", C.c_int), ("capture_local", DateTime),
                ("has_gps", C.c_int), ("latitude", C.c_int32), ("longitude", C.c_int32),
                ("altitude_m", C.c_int32), ("has_gps_time", C.c_int), ("gps_utc", DateTime)]


class PolarAxis(C.Structure):
    _fields_ = [("axis_ra", C.c_uint32), ("axis_dec", C.c_int32), ("rotation", C.c_int32),
                ("axis_x_q4", C.c_int32), ("axis_y_q4", C.c_int32)]


class PolarError(C.Structure):
    _fields_ = [("separation", C.c_uint32), ("altitude_error", C.c_int32),
                ("azimuth_error", C.c_int32), ("pole_x_q4", C.c_int32),
                ("pole_y_q4", C.c_int32), ("pole_projectable", C.c_int)]


VISIBILITY = ("up", "down", "circumpolar", "never rises")


@functools.cache
def sky_math() -> C.CDLL:
    lib = port()
    P = C.POINTER
    lib.astro_seconds_since_j2000.argtypes = [P(DateTime)]
    lib.astro_seconds_since_j2000.restype = C.c_int64
    lib.astro_datetime_from_seconds.argtypes = [C.c_int64, P(DateTime)]
    lib.astro_gmst.argtypes = [C.c_int64]
    lib.astro_gmst.restype = C.c_uint32
    lib.astro_precess_to_date.argtypes = [C.c_int64, C.c_uint32, C.c_int32, P(C.c_uint32),
                                          P(C.c_int32)]
    lib.astro_precess_to_j2000.argtypes = lib.astro_precess_to_date.argtypes
    lib.astro_pole_of_date.argtypes = [C.c_int64, C.c_int, P(C.c_uint32), P(C.c_int32)]
    lib.astro_altaz.argtypes = [P(Site), C.c_int64, C.c_uint32, C.c_int32, P(Horizontal)]
    lib.astro_exif_parse.argtypes = [C.c_char_p, C.c_uint32, P(Exif)]
    lib.astro_polar_find_axis.argtypes = [P(Wcs), P(Wcs), C.c_int, C.c_int, P(PolarAxis)]
    lib.astro_polar_measure.argtypes = [P(Wcs), C.c_int32, C.c_int32, P(Site), C.c_int64,
                                        P(PolarError)]
    return lib
