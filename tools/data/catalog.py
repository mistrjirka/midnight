"""HYG v4.1 CSV -> STARS.BIN / STARS.IDX / STARS.NAM.

HYG is CC BY-SA 4.0 (David Nash / Astronexus).

STARS.BIN  <4sIII "ASTR", 1, count, 12; then <IiHH records: RA and Dec in 1e-4 degree,
           (magnitude + 5) * 10, label id. Sorted by 5-degree sky tile.
STARS.IDX  <4sIIII "SIDX", 2, count, 72, 36; then 72*36+1 uint32 tile start offsets.
STARS.NAM  <4sIII "SNAM", 1, count, 24; then 24-byte zero-padded ASCII labels.
"""
from __future__ import annotations

import csv
import pathlib
import struct

HEADER = struct.Struct("<4sIII")
RECORD = struct.Struct("<IiHH")
INDEX_HEADER = struct.Struct("<4sIIII")
NAME_BYTES = 24
MAX_LABEL = 22

RA_BINS = 72  # 5-degree tiles, shared with DSO.BIN and the camera's scene builder
DEC_BINS = 36
TILE_COUNT = RA_BINS * DEC_BINS
TILE_SIZE = 50000  # 1e-4 degree

# Bayer letters are stored as one control byte, 0x01 (alpha) .. 0x18 (omega); the camera
# draws them with its own Greek glyphs because ML fonts are ASCII-only.
GREEK = ("Alp", "Bet", "Gam", "Del", "Eps", "Zet", "Eta", "The", "Iot", "Kap", "Lam", "Mu",
         "Nu", "Xi", "Omi", "Pi", "Rho", "Sig", "Tau", "Ups", "Phi", "Chi", "Psi", "Ome")
FAMOUS_MAG = 2.0  # brighter stars keep their proper name (Vega, Polaris)
HIP_LABEL_MAG = 7.5


def ascii_bytes(text: str) -> bytes:
    return text.encode("ascii", "ignore")


def star_label(row: dict, mag: float) -> bytes:
    """Label for one HYG row, or b"" for none.

    Preference: famous proper name, Bayer (Greek glyph + component digit + constellation),
    other proper name, Flamsteed, the HYG "bf" field, Hipparcos number.
    """
    proper = ascii_bytes(row["proper"].strip())
    con = ascii_bytes(row["con"].strip())
    bayer = row["bayer"].strip()
    flamsteed = row["flam"].strip()

    if proper and mag <= FAMOUS_MAG:
        return proper[:MAX_LABEL]
    if bayer and con:
        letter, _, component = bayer.partition("-")
        if letter in GREEK:
            glyph = bytes([GREEK.index(letter) + 1])
            return (glyph + ascii_bytes(component) + b" " + con)[:MAX_LABEL]
    if proper:
        return proper[:MAX_LABEL]
    if flamsteed and con:
        return (ascii_bytes(flamsteed) + b" " + con)[:MAX_LABEL]
    bf = ascii_bytes(" ".join(row["bf"].split()))
    if bf:
        return bf[:MAX_LABEL]
    if mag <= HIP_LABEL_MAG and row["hip"].strip():
        return ascii_bytes("HIP " + row["hip"].strip())[:MAX_LABEL]
    return b""


def tile_id(ra: int, dec: int) -> int:
    ra_bin = ra // TILE_SIZE
    dec_bin = min(DEC_BINS - 1, max(0, (dec + 900000) // TILE_SIZE))
    return dec_bin * RA_BINS + ra_bin


def tile_bounds(tiles: list[int]) -> list[int]:
    """Prefix sums: records of tile t are [bounds[t], bounds[t + 1])."""
    bounds = [0] * (TILE_COUNT + 1)
    for tile in tiles:
        bounds[tile + 1] += 1
    for i in range(1, len(bounds)):
        bounds[i] += bounds[i - 1]
    return bounds


def read_stars(source: pathlib.Path, limiting_mag: float):
    stars = []
    labels = [b""]  # id 0 means no label
    with source.open(newline="", encoding="utf-8") as file:
        for row in csv.DictReader(file):
            if not row["ra"] or not row["dec"] or not row["mag"]:
                continue
            mag = float(row["mag"])
            if not -2.0 < mag <= limiting_mag:  # also drops the Sun
                continue
            ra = round(float(row["ra"]) * 15 * 10000) % 3600000
            dec = round(float(row["dec"]) * 10000)
            mag10 = max(0, min(65535, round((mag + 5) * 10)))
            label_id = 0
            encoded = star_label(row, mag)
            if encoded:
                labels.append(encoded)
                label_id = len(labels) - 1
                assert label_id < 65536
            stars.append((ra, dec, mag10, label_id))
    stars.sort(key=lambda rec: (tile_id(rec[0], rec[1]), rec[0], rec[1]))
    return stars, labels


def build(source: pathlib.Path, dest: pathlib.Path, limiting_mag: float = 9.5) -> int:
    """Writes STARS.BIN next to .IDX and .NAM. Returns the number of stars."""
    stars, labels = read_stars(source, limiting_mag)
    bounds = tile_bounds([tile_id(ra, dec) for ra, dec, _, _ in stars])
    assert bounds[-1] == len(stars)

    dest.parent.mkdir(parents=True, exist_ok=True)
    with dest.open("wb") as file:
        file.write(HEADER.pack(b"ASTR", 1, len(stars), RECORD.size))
        for star in stars:
            file.write(RECORD.pack(*star))
    with dest.with_suffix(".IDX").open("wb") as file:
        file.write(INDEX_HEADER.pack(b"SIDX", 2, len(stars), RA_BINS, DEC_BINS))
        file.write(struct.pack(f"<{TILE_COUNT + 1}I", *bounds))
    with dest.with_suffix(".NAM").open("wb") as file:
        file.write(HEADER.pack(b"SNAM", 1, len(labels), NAME_BYTES))
        for label in labels:
            file.write(label.ljust(NAME_BYTES, b"\0"))
    return len(stars)
