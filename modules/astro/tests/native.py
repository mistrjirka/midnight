"""Shared ctypes bindings for the camera's portable C code (desktop build).

The same integer C sources that run on the ARM camera are compiled with the
host gcc. Nothing here is a reimplementation of the camera algorithms.
"""
from __future__ import annotations
import ctypes as C
import math
import pathlib
import struct
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
DATA = ROOT.parents[1] / ".astro-data"
BUILD = ROOT / "build"
W, H = 360, 240
MAX_STARS = 32


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


def build(name: str, sources: list[str], extra: list[str] | None = None) -> C.CDLL:
    BUILD.mkdir(exist_ok=True)
    out = BUILD / f"lib{name}.so"
    subprocess.run(["gcc", "-shared", "-fPIC", "-std=c99", "-O2", "-Wall", "-Wextra",
                    "-Werror", "-o", str(out), *[str(ROOT / s) for s in sources],
                    *(extra or [])], check=True)
    return C.CDLL(str(out))


def wcs_api() -> C.CDLL:
    api = build("astro_wcs", ["astro_wcs.c"])
    P = C.POINTER
    api.astro_wcs_project.argtypes = [P(Wcs), C.c_uint32, C.c_int32, P(C.c_int32), P(C.c_int32)]
    api.astro_wcs_unproject.argtypes = [P(Wcs), C.c_int32, C.c_int32, P(C.c_uint32), P(C.c_int32)]
    api.astro_wcs_axes.argtypes = [P(Wcs), C.c_uint32, C.c_int32, C.c_int32 * 2, C.c_int32 * 2]
    api.astro_separation.argtypes = [C.c_uint32, C.c_int32, C.c_uint32, C.c_int32]
    api.astro_separation.restype = C.c_uint32
    api.astro_wcs_direction.argtypes = [P(Wcs), C.c_uint32, C.c_int32, C.c_uint32, C.c_int32,
                                        P(C.c_int32), P(C.c_int32)]
    api.astro_wcs_fov_cdeg.argtypes = [P(Wcs), C.c_int, C.c_int]
    api.astro_lens_fov_cdeg.argtypes = [C.c_int, C.c_int]
    api.astro_lens_focal_x10.argtypes = [C.c_int, C.c_int]
    api.astro_sincos.argtypes = [C.c_int32, P(C.c_int32), P(C.c_int32)]
    api.astro_atan2.argtypes = [C.c_int64, C.c_int64]
    api.astro_atan2.restype = C.c_int32
    return api


def sky_api(root: pathlib.Path = DATA, prefix: str = "SKY") -> C.CDLL:
    """One solver instance per index (the library keeps one open index)."""
    api = build(f"astro_{prefix.lower()}", ["astro_sky.c", "astro_wcs.c", "tests/sky_native_runner.c"])
    api.open_sky.argtypes = [C.c_char_p, C.c_char_p]
    api.run_sky.argtypes = [C.POINTER(Star), C.c_int, C.c_int, C.c_int,
                            C.POINTER(SkyOptions), C.POINTER(SkySolution)]
    rc = api.open_sky(str(root).encode(), prefix.encode())
    assert rc == 0, f"sky index missing under {root} ({rc}); run make_sky_index.py"
    return api


def load_stars(path: pathlib.Path = DATA / "STARS.BIN"):
    """(ra_deg, dec_deg, mag, label_id) for every catalogue record."""
    raw = path.read_bytes()
    magic, version, num, size = struct.unpack_from("<4sIII", raw)
    assert (magic, version, size) == (b"ASTR", 1, 12)
    return [(ra / 1e4, dec / 1e4, m10 / 10 - 5, lab)
            for ra, dec, m10, lab in struct.iter_unpack("<IiHH", raw[16:])]


def unit(ra: float, dec: float):
    r, d = math.radians(ra), math.radians(dec)
    return (math.cos(d) * math.cos(r), math.cos(d) * math.sin(r), math.sin(d))


def separation(ra1, dec1, ra2, dec2) -> float:
    a, b = unit(ra1, dec1), unit(ra2, dec2)
    dot = max(-1.0, min(1.0, sum(x * y for x, y in zip(a, b))))
    cross = math.sqrt(sum(c * c for c in (a[1] * b[2] - a[2] * b[1],
                                           a[2] * b[0] - a[0] * b[2],
                                           a[0] * b[1] - a[1] * b[0])))
    return math.degrees(math.atan2(cross, dot))


# ---- overlay scene (astro_overlay.c) ----
class Dso(C.Structure):
    _fields_ = [("ra", C.c_uint32), ("dec", C.c_int32), ("maj", C.c_uint16), ("min", C.c_uint16),
                ("pa", C.c_int16), ("mag10", C.c_int16), ("type", C.c_uint8), ("cat", C.c_uint8),
                ("num", C.c_uint16), ("name", C.c_uint16), ("reserved", C.c_uint16)]


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
    _fields_ = [("x", C.c_int16), ("y", C.c_int16), ("kind", C.c_uint8), ("priority", C.c_uint8),
                ("text", C.c_char * 24)]


class Scene(C.Structure):
    _fields_ = [("width", C.c_int), ("height", C.c_int),
                ("nlines", C.c_int), ("nellipses", C.c_int), ("nlabels", C.c_int),
                ("lines", Line * 384), ("ellipses", Ellipse * 48), ("labels", Label * 64),
                ("target", C.c_int), ("target_x", C.c_int16), ("target_y", C.c_int16),
                ("target_dx", C.c_int16), ("target_dy", C.c_int16),
                ("target_sep", C.c_uint32), ("target_label", C.c_char * 24)]


CAT = {"M": 1, "NGC": 2, "IC": 3, "C": 4, "B": 5, "Sh2": 6}
LABEL_STAR, LABEL_DSO, LABEL_CONSTELLATION, LABEL_TARGET = 1, 2, 3, 4


def overlay_api(root: pathlib.Path = DATA) -> C.CDLL:
    api = build("astro_overlay", ["astro_overlay.c", "astro_wcs.c", "tests/overlay_native_runner.c"])
    P = C.POINTER
    api.open_files.argtypes = [C.c_char_p]
    api.lookup.argtypes = [C.c_int, C.c_int, P(Target)]
    api.common_name.argtypes = [P(Dso), C.c_char_p, C.c_int]
    api.scene.argtypes = [P(Wcs), C.c_int, C.c_int, P(OverlayOpts), P(Target), P(Scene)]
    api.astro_designation.argtypes = [C.c_int, C.c_int, C.c_char_p, C.c_int]
    assert api.open_files(str(root).encode()), f"STARS/DSO/CONST data missing under {root}"
    return api


def camera_wcs(ra, dec, roll, mirror, k):
    """WCS equal to test_sky_solver.Camera without distortion (k = px per tangent unit)."""
    kk = k * math.pi / 180000 * (1 << 24)
    cr, sr = math.cos(math.radians(roll)), math.sin(math.radians(roll))
    s = -1 if mirror else 1
    w = Wcs()
    w.ra0, w.dec0 = round(ra * 1e4) % 3600000, round(dec * 1e4)
    # px = W/2 - k*(s*(xi*cr - eta*sr)),  py = H/2 - k*(xi*sr + eta*cr)
    w.m[0], w.m[1] = round(-s * kk * cr), round(s * kk * sr)
    w.m[2], w.m[3] = round(-kk * sr), round(-kk * cr)
    w.t[0], w.t[1] = W // 2 * 65536, H // 2 * 65536
    return w


def local_direction(cam, ra0, dec0, ra, dec):
    """Float reference: image direction of the great circle from (ra0,dec0)."""
    a0, d0, a, d = map(math.radians, (ra0, dec0, ra, dec))
    e = math.cos(d) * math.sin(a - a0)
    n = math.cos(d0) * math.sin(d) - math.sin(d0) * math.cos(d) * math.cos(a - a0)
    u = e * cam.cr - n * cam.sr
    v = e * cam.sr + n * cam.cr
    if cam.mirror:
        u = -u
    dx, dy = -u, -v
    length = math.hypot(dx, dy)
    return dx / length, dy / length
