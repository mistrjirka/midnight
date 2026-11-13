"""Polar alignment geometry with a simulated camera rotating about a known mount axis."""
from __future__ import annotations

import ctypes as C
import datetime as dt
import math
import random

import numpy as np
import pytest

from native import H, W, PolarAxis, PolarError, Site, Wcs, separation, sky_math
from test_time_altaz import J2000, altaz_reference, precess_reference

SITE = (50.08, 14.42)  # Prague
WHEN = dt.datetime(2026, 11, 9, 21, 0)
SECONDS = int((WHEN - J2000).total_seconds())


def unit(ra: float, dec: float) -> np.ndarray:
    r, d = math.radians(ra), math.radians(dec)
    return np.array([math.cos(d) * math.cos(r), math.cos(d) * math.sin(r), math.sin(d)])


def radec(v: np.ndarray) -> tuple[float, float]:
    v = v / np.linalg.norm(v)
    return math.degrees(math.atan2(v[1], v[0])) % 360, math.degrees(math.asin(v[2]))


def rotation(axis: np.ndarray, angle_deg: float) -> np.ndarray:
    """Rodrigues rotation matrix."""
    a = axis / np.linalg.norm(axis)
    k = np.array([[0, -a[2], a[1]], [a[2], 0, -a[0]], [-a[1], a[0], 0]])
    t = math.radians(angle_deg)
    return np.eye(3) + math.sin(t) * k + (1 - math.cos(t)) * k @ k


def wcs_from_orientation(forward, right, up, focal_mm=135, sensor_mm=35.8) -> Wcs:
    """WCS of a camera looking along `forward`, image x along `right`, image y along -up."""
    ra0, dec0 = radec(forward)
    r, d = math.radians(ra0), math.radians(dec0)
    east = np.array([-math.sin(r), math.cos(r), 0.0])
    north = np.array([-math.sin(d) * math.cos(r), -math.sin(d) * math.sin(r), math.cos(d)])
    k = W * focal_mm / sensor_mm * math.pi / 180000 * (1 << 24)  # Q24 px per tangent mdeg
    w = Wcs()
    w.ra0, w.dec0 = round(ra0 * 1e4) % 3600000, round(dec0 * 1e4)
    w.m[0], w.m[1] = round(k * right @ east), round(k * right @ north)
    w.m[2], w.m[3] = round(-k * up @ east), round(-k * up @ north)
    w.t[0], w.t[1] = W // 2 * 65536, H // 2 * 65536
    return w


def camera(forward, roll_deg):
    f = forward / np.linalg.norm(forward)
    helper = np.array([0.0, 0.0, 1.0]) if abs(f[2]) < 0.9 else np.array([1.0, 0.0, 0.0])
    right = np.cross(f, helper)
    right /= np.linalg.norm(right)
    up = np.cross(right, f)
    turn = rotation(f, roll_deg)
    return f, turn @ right, turn @ up


def two_frames(axis, pointing_offset_deg, angle_deg, rng):
    """Camera near the axis, then the same camera turned about the axis."""
    ra_a, _ = radec(axis)
    tilt = rotation(np.cross(axis, unit(ra_a + 90, 0)), pointing_offset_deg)
    forward = tilt @ axis
    cam = camera(forward, rng.uniform(-180, 180))
    turn = rotation(axis, angle_deg)
    return (wcs_from_orientation(*cam),
            wcs_from_orientation(*(turn @ v for v in cam)))


@pytest.mark.parametrize("angle", [30, -45, 90])
def test_axis_recovered(angle):
    lib = sky_math()
    rng = random.Random(angle)
    for _ in range(10):
        # A mount axis within 2 degrees of the north pole, camera pointing 0.5-3 deg away.
        axis = unit(rng.uniform(0, 360), 90 - rng.uniform(0, 2))
        first, second = two_frames(axis, rng.uniform(0.5, 3), angle, rng)
        out = PolarAxis()
        assert lib.astro_polar_find_axis(C.byref(first), C.byref(second), W, H,
                                         C.byref(out)) == 0
        ra, dec = radec(axis)
        assert separation(out.axis_ra / 1e4, out.axis_dec / 1e4, ra, dec) < 0.01
        assert abs(abs(out.rotation / 1e4) - abs(angle)) < 0.05


def test_small_rotation_is_rejected():
    lib = sky_math()
    axis = unit(10, 89)
    first, second = two_frames(axis, 1.0, 8, random.Random(1))
    out = PolarAxis()
    assert lib.astro_polar_find_axis(C.byref(first), C.byref(second), W, H, C.byref(out)) == 1


def test_measure_reports_altitude_and_azimuth_error():
    lib = sky_math()
    lat, lon = SITE
    pole_ra, pole_dec = precess_reference(SECONDS, 0, 90, inverse=True)
    pole_alt, pole_az = altaz_reference(lat, lon, SECONDS, pole_ra, pole_dec)

    # An axis 0.3 degrees too high and 0.2 degrees (on sky) east of the pole.
    target_alt, target_az = pole_alt + 0.3, pole_az + 0.2 / math.cos(math.radians(pole_alt))
    axis = horizontal_to_j2000(lat, lon, SECONDS, target_alt, target_az)
    first, second = two_frames(axis, 1.5, 60, random.Random(7))

    found = PolarAxis()
    assert lib.astro_polar_find_axis(C.byref(first), C.byref(second), W, H,
                                     C.byref(found)) == 0
    err = PolarError()
    site = Site(round(lat * 1e4), round(lon * 1e4))
    assert lib.astro_polar_measure(C.byref(second), found.axis_x_q4, found.axis_y_q4,
                                   C.byref(site), SECONDS, C.byref(err)) == 0
    assert abs(err.altitude_error / 1e4 - 0.3) < 0.01
    assert abs(err.azimuth_error / 1e4 - 0.2) < 0.01
    assert abs(err.separation / 1e4 - math.hypot(0.3, 0.2)) < 0.01
    assert err.pole_projectable


def horizontal_to_j2000(lat, lon, seconds, alt, az) -> np.ndarray:
    """Inverse of altaz_reference (geometric), returned as a J2000 unit vector."""
    from test_time_altaz import gmst_reference
    la, a, z = map(math.radians, (lat, alt, az))
    dec = math.asin(math.sin(la) * math.sin(a) + math.cos(la) * math.cos(a) * math.cos(z))
    ha = math.atan2(-math.sin(z) * math.cos(a),
                    math.cos(la) * math.sin(a) - math.sin(la) * math.cos(a) * math.cos(z))
    ra_date = (gmst_reference(seconds) + lon - math.degrees(ha)) % 360
    ra, dec2000 = precess_reference(seconds, ra_date, math.degrees(dec), inverse=True)
    return unit(ra, dec2000)
