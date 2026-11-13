"""Calendar, sidereal time, precession and alt/az against floating-point references."""
from __future__ import annotations

import ctypes as C
import datetime as dt
import math
import random

import pytest

from native import VISIBILITY, DateTime, Horizontal, Site, sky_math

J2000 = dt.datetime(2000, 1, 1, 12, 0, 0)
DEG = 1e4  # 1e-4 degree units


def seconds(when: dt.datetime) -> int:
    return int((when - J2000).total_seconds())


def angle_diff(a: float, b: float) -> float:
    return abs((a - b + 180) % 360 - 180)


def gmst_reference(s: float) -> float:
    d = s / 86400
    t = d / 36525
    return (280.46061837 + 360.98564736629 * d + 0.000387933 * t * t) % 360


def precess_reference(s: float, ra: float, dec: float, inverse=False):
    t = s / 86400 / 36525
    zeta = math.radians((2306.2181 * t + 0.30188 * t * t) / 3600)
    z = math.radians((2306.2181 * t + 1.09468 * t * t) / 3600)
    theta = math.radians((2004.3109 * t - 0.42665 * t * t) / 3600)
    r, d = math.radians(ra), math.radians(dec)
    if not inverse:
        a = math.cos(d) * math.sin(r + zeta)
        b = math.cos(theta) * math.cos(d) * math.cos(r + zeta) - math.sin(theta) * math.sin(d)
        c = math.sin(theta) * math.cos(d) * math.cos(r + zeta) + math.cos(theta) * math.sin(d)
        return math.degrees(math.atan2(a, b) + z) % 360, math.degrees(math.asin(c))
    a = math.cos(d) * math.sin(r - z)
    b = math.cos(theta) * math.cos(d) * math.cos(r - z) + math.sin(theta) * math.sin(d)
    c = -math.sin(theta) * math.cos(d) * math.cos(r - z) + math.cos(theta) * math.sin(d)
    return math.degrees(math.atan2(a, b) - zeta) % 360, math.degrees(math.asin(c))


def altaz_reference(lat, lon, s, ra, dec):
    ra_d, dec_d = precess_reference(s, ra, dec)
    ha = math.radians((gmst_reference(s) + lon - ra_d) % 360)
    la, de = math.radians(lat), math.radians(dec_d)
    alt = math.asin(math.sin(la) * math.sin(de) + math.cos(la) * math.cos(de) * math.cos(ha))
    az = math.atan2(-math.cos(de) * math.sin(ha),
                    math.sin(de) * math.cos(la) - math.cos(de) * math.sin(la) * math.cos(ha))
    return math.degrees(alt), math.degrees(az) % 360


def call_altaz(lib, lat, lon, s, ra, dec) -> Horizontal:
    out = Horizontal()
    site = Site(round(lat * DEG), round(lon * DEG))
    lib.astro_altaz(C.byref(site), s, round(ra * DEG) % 3600000, round(dec * DEG), C.byref(out))
    return out


def test_calendar_round_trip():
    lib = sky_math()
    rng = random.Random(1)
    assert lib.astro_seconds_since_j2000(C.byref(DateTime(2000, 1, 1, 12, 0, 0))) == 0
    for _ in range(2000):
        when = J2000 + dt.timedelta(seconds=rng.randrange(-10**9, 10**9))
        d = DateTime(when.year, when.month, when.day, when.hour, when.minute, when.second)
        s = lib.astro_seconds_since_j2000(C.byref(d))
        assert s == seconds(when)
        back = DateTime()
        lib.astro_datetime_from_seconds(s, C.byref(back))
        assert (back.year, back.month, back.day, back.hour, back.minute, back.second) == \
            (when.year, when.month, when.day, when.hour, when.minute, when.second)


def test_gmst():
    lib = sky_math()
    # Meeus, Astronomical Algorithms, example 12.a: 1987-04-10 0h UT -> 13h10m46.3668s.
    assert angle_diff(lib.astro_gmst(seconds(dt.datetime(1987, 4, 10))) / DEG,
                      197.693195) < 0.001
    rng = random.Random(2)
    for _ in range(2000):
        s = rng.randrange(-10**9, 2 * 10**9)
        assert angle_diff(lib.astro_gmst(s) / DEG, gmst_reference(s)) < 0.0005


def test_precession_matches_reference_and_inverts():
    lib = sky_math()
    rng = random.Random(3)
    for _ in range(2000):
        s = rng.randrange(0, 1_600_000_000)
        ra, dec = rng.uniform(0, 360), math.degrees(math.asin(rng.uniform(-0.999, 0.999)))
        ra_d, dec_d = C.c_uint32(), C.c_int32()
        lib.astro_precess_to_date(s, round(ra * DEG) % 3600000, round(dec * DEG),
                                  C.byref(ra_d), C.byref(dec_d))
        ref_ra, ref_dec = precess_reference(s, ra, dec)
        tolerance = 0.0005 / max(0.05, math.cos(math.radians(dec)))
        assert angle_diff(ra_d.value / DEG, ref_ra) < tolerance
        assert abs(dec_d.value / DEG - ref_dec) < 0.0005
        ra_b, dec_b = C.c_uint32(), C.c_int32()
        lib.astro_precess_to_j2000(s, ra_d.value, dec_d.value, C.byref(ra_b), C.byref(dec_b))
        assert angle_diff(ra_b.value / DEG, ra) < 2 * tolerance
        assert abs(dec_b.value / DEG - dec) < 0.001


def test_pole_of_date_2026():
    lib = sky_math()
    s = seconds(dt.datetime(2026, 11, 9, 21, 0))
    ra, dec = C.c_uint32(), C.c_int32()
    lib.astro_pole_of_date(s, 1, C.byref(ra), C.byref(dec))
    ref_ra, ref_dec = precess_reference(s, 0.0, 90.0, inverse=True)
    assert abs(dec.value / DEG - ref_dec) < 0.0005
    assert angle_diff(ra.value / DEG, ref_ra) < 0.05  # RA is ill-conditioned at the pole
    assert 0.12 < 90 - dec.value / DEG < 0.18  # the pole has moved ~9 arcmin since 2000


@pytest.mark.parametrize("lat,lon", [(50.08, 14.42), (-33.9, 18.4), (64.1, -21.9), (0.0, 0.0)])
def test_altaz_matches_reference(lat, lon):
    lib = sky_math()
    rng = random.Random(int(lat * 100))
    for _ in range(500):
        s = rng.randrange(800_000_000, 900_000_000)
        ra, dec = rng.uniform(0, 360), math.degrees(math.asin(rng.uniform(-0.99, 0.99)))
        out = call_altaz(lib, lat, lon, s, ra, dec)
        alt, az = altaz_reference(lat, lon, s, ra, dec)
        assert abs(out.altitude / DEG - alt) < 0.002
        if abs(alt) < 89:
            assert angle_diff(out.azimuth / DEG, az) < 0.002 / math.cos(math.radians(alt))


def test_rise_and_set_times():
    lib = sky_math()
    lat, lon = 50.08, 14.42  # Prague
    rng = random.Random(5)
    for _ in range(300):
        s = rng.randrange(800_000_000, 900_000_000)
        ra, dec = rng.uniform(0, 360), rng.uniform(-35, 35)
        out = call_altaz(lib, lat, lon, s, ra, dec)
        alt, _ = altaz_reference(lat, lon, s, ra, dec)
        state = VISIBILITY[out.visibility]
        assert state == ("up" if alt > -0.5667 else "down") or abs(alt + 0.5667) < 0.01
        # Step the clock to the predicted crossing: the altitude must be at the horizon.
        later = s + round(out.minutes_to_change * 60 * 0.99727)  # sidereal -> solar minutes
        crossing_alt, _ = altaz_reference(lat, lon, later, ra, dec)
        assert abs(crossing_alt + 0.5667) < 0.5


def test_circumpolar_and_never_rising():
    lib = sky_math()
    s = seconds(dt.datetime(2026, 11, 9, 21, 0))
    assert VISIBILITY[call_altaz(lib, 50.08, 14.42, s, 37.95, 89.26).visibility] == "circumpolar"
    assert VISIBILITY[call_altaz(lib, 50.08, 14.42, s, 95.99, -52.7).visibility] == "never rises"
