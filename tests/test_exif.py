"""EXIF GPS reader: synthetic JPEG/CR2-style headers, both byte orders, and fuzzing."""
from __future__ import annotations

import ctypes as C
import datetime as dt
import subprocess

import pytest

from native import BUILD, CORE, REPO, Exif, sky_math
from tools.fixtures import exif_jpeg, exif_tiff

CAPTURE = dt.datetime(2026, 11, 9, 21, 14, 7)
UTC = dt.datetime(2026, 11, 9, 20, 14, 7)
PRAGUE = {"lat": 50.0755, "lon": 14.4378, "alt": 235, "utc": UTC}


def parse(data: bytes) -> tuple[int, Exif]:
    out = Exif()
    rc = sky_math().astro_exif_parse(data, len(data), C.byref(out))
    return rc, out


def as_datetime(d) -> dt.datetime:
    return dt.datetime(d.year, d.month, d.day, d.hour, d.minute, d.second)


@pytest.mark.parametrize("big_endian", [False, True])
@pytest.mark.parametrize("container", ["jpeg", "tiff"])
def test_gps_fix_and_times(container, big_endian):
    make = exif_jpeg if container == "jpeg" else exif_tiff
    rc, out = parse(make(CAPTURE, PRAGUE, big_endian))
    assert rc == 1
    assert out.has_capture_time and as_datetime(out.capture_local) == CAPTURE
    assert out.has_gps
    assert abs(out.latitude - 500755) <= 1 and abs(out.longitude - 144378) <= 1
    assert out.altitude_m == 235
    assert out.has_gps_time and as_datetime(out.gps_utc) == UTC


def test_southern_and_western_hemisphere():
    rc, out = parse(exif_jpeg(CAPTURE, {"lat": -33.8568, "lon": -70.6693, "alt": -12,
                                        "utc": UTC}))
    assert rc == 1 and out.has_gps
    assert abs(out.latitude + 338568) <= 1 and abs(out.longitude + 706693) <= 1
    assert out.altitude_m == -12


def test_photo_without_gps():
    rc, out = parse(exif_jpeg(CAPTURE, None))
    assert rc == 1 and out.has_capture_time and not out.has_gps and not out.has_gps_time


def test_not_an_image():
    for data in (b"", b"hello world", b"\xff\xd8\xff\xda\x00\x02", bytes(64)):
        assert parse(data)[0] == 0


def test_fuzzed_headers_never_read_out_of_bounds(tmp_path):
    seeds = []
    for i, data in enumerate((exif_jpeg(CAPTURE, PRAGUE), exif_tiff(CAPTURE, PRAGUE, True))):
        seed = tmp_path / f"seed{i}"
        seed.write_bytes(data)
        seeds.append(str(seed))
    exe = BUILD / "exif_fuzz"
    BUILD.mkdir(parents=True, exist_ok=True)
    subprocess.run(["gcc", "-std=gnu99", "-g", "-O1", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", f"-I{CORE}", "-o", str(exe),
                    str(REPO / "tests" / "harness" / "exif_fuzz.c"), str(CORE / "exif.c"),
                    str(CORE / "astro_time.c"), str(CORE / "sphere.c"),
                    str(CORE / "fixed_math.c")], check=True)
    result = subprocess.run([str(exe), "20000", *seeds], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr[-2000:]
    assert "parsed" in result.stdout
