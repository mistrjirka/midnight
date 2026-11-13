"""`astroml fixtures ...`: synthetic test inputs (EXIF GPS photos, PGM frames)."""
from __future__ import annotations

import datetime as dt
import io
import pathlib
import struct

import typer

app = typer.Typer(help="Generate synthetic test inputs.", no_args_is_help=True)

TYPE_ASCII, TYPE_SHORT, TYPE_LONG, TYPE_RATIONAL, TYPE_BYTE = 2, 3, 4, 5, 1


def _rationals(values, denominator):
    return [(round(v * denominator), denominator) for v in values]


def _dms(value: float):
    value = abs(value)
    degrees = int(value)
    minutes = int((value - degrees) * 60)
    seconds = (value - degrees - minutes / 60) * 3600
    return [(degrees, 1), (minutes, 1), (round(seconds * 1000), 1000)]


class TiffBuilder:
    """Writes IFD0 -> (EXIF IFD, GPS IFD) with values stored after each IFD."""

    def __init__(self, big_endian: bool = False):
        self.e = ">" if big_endian else "<"
        self.magic = b"MM\0*" if big_endian else b"II*\0"

    def _ifd(self, start: int, entries: list[tuple[int, int, object]]) -> bytes:
        """entries: (tag, type, value). Returns the IFD followed by its out-of-line data."""
        e = self.e
        fixed = 2 + 12 * len(entries) + 4
        data = bytearray()
        body = bytearray(struct.pack(e + "H", len(entries)))
        for tag, kind, value in sorted(entries, key=lambda t: t[0]):
            if kind == TYPE_ASCII:
                raw, count = value.encode("ascii") + b"\0", len(value) + 1
            elif kind == TYPE_RATIONAL:
                raw = b"".join(struct.pack(e + "II", n, d) for n, d in value)
                count = len(value)
            elif kind == TYPE_BYTE:
                raw, count = bytes([value]), 1
            elif kind == TYPE_SHORT:
                raw, count = struct.pack(e + "H", value), 1
            else:
                raw, count = struct.pack(e + "I", value), 1
            if len(raw) <= 4:
                inline = raw.ljust(4, b"\0")
            else:
                inline = struct.pack(e + "I", start + fixed + len(data))
                data += raw + (b"\0" if len(raw) % 2 else b"")
            body += struct.pack(e + "HHI", tag, kind, count) + inline
        body += struct.pack(e + "I", 0)
        return bytes(body + data)

    def build(self, capture: dt.datetime | None, gps: dict | None) -> bytes:
        ifd0_start = 8
        exif_entries = []
        if capture:
            exif_entries.append((0x9003, TYPE_ASCII, capture.strftime("%Y:%m:%d %H:%M:%S")))
        gps_entries = []
        if gps:
            gps_entries += [
                (0x0001, TYPE_ASCII, "N" if gps["lat"] >= 0 else "S"),
                (0x0002, TYPE_RATIONAL, _dms(gps["lat"])),
                (0x0003, TYPE_ASCII, "E" if gps["lon"] >= 0 else "W"),
                (0x0004, TYPE_RATIONAL, _dms(gps["lon"])),
                (0x0005, TYPE_BYTE, 0 if gps.get("alt", 0) >= 0 else 1),
                (0x0006, TYPE_RATIONAL, _rationals([abs(gps.get("alt", 0))], 1)),
            ]
            if gps.get("utc"):
                utc: dt.datetime = gps["utc"]
                gps_entries += [
                    (0x0007, TYPE_RATIONAL, [(utc.hour, 1), (utc.minute, 1), (utc.second, 1)]),
                    (0x001D, TYPE_ASCII, utc.strftime("%Y:%m:%d")),
                ]
        # IFD sizes do not depend on the offset values, so lay out IFD0 once with
        # placeholders, then again with the real sub-IFD offsets.
        def ifd0_entries(exif_at: int, gps_at: int):
            entries = [(0x010F, TYPE_ASCII, "Canon"), (0x0110, TYPE_ASCII, "Canon EOS 6D"),
                       (0x8769, TYPE_LONG, exif_at)]
            if gps:
                entries.append((0x8825, TYPE_LONG, gps_at))
            return entries

        exif_start = ifd0_start + len(self._ifd(ifd0_start, ifd0_entries(0, 0)))
        exif = self._ifd(exif_start, exif_entries)
        gps_start = exif_start + len(exif)
        ifd0 = self._ifd(ifd0_start, ifd0_entries(exif_start, gps_start))
        gps_block = self._ifd(gps_start, gps_entries) if gps else b""
        header = self.magic + struct.pack(self.e + "I", ifd0_start)
        return header + ifd0 + exif + gps_block


def exif_tiff(capture=None, gps=None, big_endian=False) -> bytes:
    return TiffBuilder(big_endian).build(capture, gps)


def exif_jpeg(capture=None, gps=None, big_endian=False, size=(64, 48)) -> bytes:
    """A real JPEG (decodable) with an APP1 Exif segment right after SOI."""
    from PIL import Image
    buffer = io.BytesIO()
    Image.new("RGB", size, (8, 8, 16)).save(buffer, "JPEG", quality=80)
    jpeg = buffer.getvalue()
    tiff = exif_tiff(capture, gps, big_endian)
    app1 = b"\xff\xe1" + struct.pack(">H", len(tiff) + 8) + b"Exif\0\0" + tiff
    return jpeg[:2] + app1 + jpeg[2:]


@app.command("exif")
def make_exif_jpeg(dest: pathlib.Path = typer.Argument(..., help="Output .JPG"),
                   lat: float = typer.Option(50.0755, help="Latitude, north positive."),
                   lon: float = typer.Option(14.4378, help="Longitude, east positive."),
                   alt: float = typer.Option(235.0, help="Altitude in metres."),
                   utc: dt.datetime = typer.Option(dt.datetime(2026, 11, 9, 20, 14, 0),
                                                   help="GPS UTC time."),
                   utc_offset_hours: int = typer.Option(1, help="Camera clock minus UTC.")):
    """A small JPEG whose EXIF carries a GPS fix and capture time (like a 6D with GPS on)."""
    local = utc + dt.timedelta(hours=utc_offset_hours)
    dest.write_bytes(exif_jpeg(local, {"lat": lat, "lon": lon, "alt": alt, "utc": utc}))
    typer.echo(f"{dest}: {lat:+.4f} {lon:+.4f}, {local:%Y-%m-%d %H:%M:%S} local")


def render_star_field(stars_bin: pathlib.Path, ra: float, dec: float, roll: float,
                      focal_mm: float, sensor_mm: float = 35.8, maglim: float = 6.0,
                      seed: int = 1, trail: bool = False):
    """360x240 8-bit frame of the HYG sky at a known pointing (gnomonic camera, numpy)."""
    import numpy as np

    width, height = 360, 240
    raw = np.frombuffer(stars_bin.read_bytes()[16:],
                        dtype=[("ra", "<u4"), ("dec", "<i4"), ("mag", "<u2"), ("name", "<u2")])
    mag = raw["mag"] / 10 - 5
    keep = mag <= maglim
    a = np.radians(raw["ra"][keep] / 1e4)
    d = np.radians(raw["dec"][keep] / 1e4)
    mag = mag[keep]
    a0, d0 = np.radians(ra), np.radians(dec)
    cosc = np.sin(d0) * np.sin(d) + np.cos(d0) * np.cos(d) * np.cos(a - a0)
    front = cosc > 0.2
    xi = np.cos(d) * np.sin(a - a0) / cosc
    eta = (np.cos(d0) * np.sin(d) - np.sin(d0) * np.cos(d) * np.cos(a - a0)) / cosc
    cr, sr = np.cos(np.radians(roll)), np.sin(np.radians(roll))
    k = width * focal_mm / sensor_mm
    x = width / 2 - k * (xi * cr - eta * sr)
    y = height / 2 - k * (xi * sr + eta * cr)
    inside = front & (x > -5) & (x < width + 5) & (y > -5) & (y < height + 5)

    rng = np.random.default_rng(seed)
    image = rng.normal(22, 3, (height, width))
    yy, xx = np.mgrid[0:height, 0:width]
    for sx, sy, m in zip(x[inside], y[inside], mag[inside]):
        peak = min(230.0, 2400.0 * 10 ** (-0.4 * m) + 35)
        y0, y1 = max(0, int(sy) - 6), min(height, int(sy) + 7)
        x0, x1 = max(0, int(sx) - 6), min(width, int(sx) + 7)
        r2 = (xx[y0:y1, x0:x1] - sx) ** 2 + (yy[y0:y1, x0:x1] - sy) ** 2
        image[y0:y1, x0:x1] += peak * np.exp(-r2 / (2 * 1.3 ** 2))
    if trail:
        for t in np.linspace(0, 1, 900):
            px, py = int(20 + 320 * t), int(30 + 170 * t)
            image[py - 1:py + 2, px] = 225
    return np.clip(image, 0, 255).astype(np.uint8)


@app.command("pgm")
def make_test_pgm(dest: pathlib.Path = typer.Argument(..., help="Output TEST.PGM"),
                  stars: pathlib.Path = typer.Option(pathlib.Path(".astro-data/STARS.BIN")),
                  ra: float = typer.Option(83.82, help="Frame centre RA, degrees."),
                  dec: float = typer.Option(-5.39, help="Frame centre Dec, degrees."),
                  roll: float = typer.Option(12.0, help="Image rotation, degrees."),
                  focal: float = typer.Option(85.0, help="Lens focal length on the 6D, mm."),
                  maglim: float = typer.Option(6.5, help="Faintest star drawn."),
                  trail: bool = typer.Option(False, help="Add a bright satellite trail.")):
    """Synthetic 360x240 PGM of a known sky position for `Developer > Solve image file`."""
    image = render_star_field(stars, ra, dec, roll, focal, maglim=maglim, trail=trail)
    dest.write_bytes(b"P5\n360 240\n255\n" + image.tobytes())
    typer.echo(f"{dest}: RA {ra} Dec {dec} roll {roll} at {focal} mm")


TAG_COMPRESSION, TAG_STRIP_OFFSET, TAG_STRIP_BYTES = 0x0103, 0x0111, 0x0117
JPEG_COMPRESSION = 6
DISPLAY_SIZE = (720, 480)  # the 6D's playback buffer; the module averages 2x2 blocks


def cr2_preview_jpeg(path: pathlib.Path) -> bytes:
    """The full-size preview JPEG a CR2 keeps in IFD0 (what the camera shows in playback)."""
    data = path.read_bytes()
    order = "<" if data[:2] == b"II" else ">"
    (ifd,) = struct.unpack_from(order + "I", data, 4)
    (count,) = struct.unpack_from(order + "H", data, ifd)
    tags = {}
    for i in range(count):
        tag, kind, n, value = struct.unpack_from(order + "HHII", data, ifd + 2 + 12 * i)
        tags[tag] = value
    if tags.get(TAG_COMPRESSION) != JPEG_COMPRESSION or TAG_STRIP_OFFSET not in tags:
        raise typer.BadParameter(f"{path} has no JPEG preview in IFD0")
    start, length = tags[TAG_STRIP_OFFSET], tags[TAG_STRIP_BYTES]
    return data[start:start + length]


def playback_luma(jpeg: bytes):
    """360x240 luma exactly as `astro_capture_display` reads the 720x480 playback buffer:
    the integer mean of each 2x2 block."""
    import numpy as np
    from PIL import Image

    image = Image.open(io.BytesIO(jpeg)).convert("YCbCr").resize(DISPLAY_SIZE, Image.BOX)
    luma = np.asarray(image)[:, :, 0].astype(np.uint16)
    return (luma.reshape(240, 2, 360, 2).sum(axis=(1, 3)) // 4).astype(np.uint8)


@app.command("raw")
def make_raw_pgm(source: pathlib.Path = typer.Argument(..., help="Canon .CR2 photo"),
                 dest: pathlib.Path = typer.Argument(..., help="Output 360x240 .PGM")):
    """360x240 PGM from a CR2, as the module sees it in playback. Carries no EXIF."""
    luma = playback_luma(cr2_preview_jpeg(source))
    dest.write_bytes(b"P5\n360 240\n255\n" + luma.tobytes())
    typer.echo(f"{dest}: mean {luma.mean():.0f}, max {luma.max()}")
