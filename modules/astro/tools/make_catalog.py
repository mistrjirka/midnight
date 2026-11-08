#!/usr/bin/env python3
"""Convert HYG v4.1 CSV to a compact binary catalog for Magic Lantern.
Source: https://github.com/astronexus/HYG-Database/tree/main/hyg/CURRENT
HYG is CC BY-SA 4.0. See README.rst for attribution.
Output: little-endian ASTR header and 12-byte records (RA*10000 deg,
Dec*10000 deg, mag offset by +5 then multiplied by 10, reserved).
"""
import argparse
import csv
import pathlib
import struct

HEADER = struct.Struct("<4sIII")
RECORD = struct.Struct("<IiHH")
INDEX_HEADER = struct.Struct("<4sIII")
RA_BINS = 72     # 5-degree right ascension regions
DEC_BINS = 36    # 5-degree declination regions
TILE_COUNT = RA_BINS * DEC_BINS

def make_catalog(source: pathlib.Path, dest: pathlib.Path, limiting_mag: float) -> int:
    stars = []
    labels = [b""]  # ID 0 means no label.
    with source.open(newline="", encoding="utf-8") as file:
        for row in csv.DictReader(file):
            if not row["ra"] or not row["dec"] or not row["mag"]:
                continue
            mag = float(row["mag"])
            if not (-2.0 < mag <= limiting_mag):
                continue
            ra = round(float(row["ra"]) * 15 * 10000) % 3600000
            dec = round(float(row["dec"]) * 10000)
            mag10 = max(0, min(65535, round((mag + 5) * 10)))
            # Prefer the proper star name, then Bayer/Flamsteed, then
            # Hipparcos number for bright unnamed stars. The same label ID
            # is placed into the unused 16-bit record field.
            label = row["proper"].strip() or " ".join(row["bf"].split())
            if not label and mag <= 7.5 and row["hip"].strip():
                label = "HIP " + row["hip"].strip()
            encoded = label.encode("ascii","ignore")[:22]
            label_id = 0
            if encoded:
                labels.append(encoded)
                label_id = len(labels)-1
                assert label_id < 65536
            stars.append((ra, dec, mag10, label_id))
    # Each record belongs to one fixed 5x5 degree sky tile.
    # Fixed integer IDs avoid a hash table's RAM and collision overhead.
    def tile_id(record: tuple[int, int, int, int]) -> int:
        ra, dec, _, _ = record
        ra_bin = ra // 50000
        dec_bin = min(DEC_BINS - 1, max(0, (dec + 900000) // 50000))
        return dec_bin * RA_BINS + ra_bin

    stars.sort(key=lambda rec: (tile_id(rec), rec[0], rec[1]))
    bounds = [0] * (TILE_COUNT + 1)
    for star in stars:
        bounds[tile_id(star) + 1] += 1
    for i in range(1, len(bounds)):
        bounds[i] += bounds[i-1]
    assert bounds[-1] == len(stars)
    dest.parent.mkdir(parents=True, exist_ok=True)
    with dest.open("wb") as file:
        file.write(HEADER.pack(b"ASTR", 1, len(stars), RECORD.size))
        for star in stars:
            file.write(RECORD.pack(*star))
    # 20-byte header plus 2593 offsets (10,392 bytes total).
    with dest.with_suffix(".IDX").open("wb") as file:
        file.write(struct.pack("<4sIIII", b"SIDX", 2, len(stars),
                               RA_BINS, DEC_BINS))
        file.write(struct.pack(f"<{TILE_COUNT+1}I", *bounds))
    # Each label is 24 bytes: maximum 22 ASCII characters plus zero padding.
    # O(1) lookup by the record's 16-bit label_id. No RAM dictionary.
    with dest.with_suffix(".NAM").open("wb") as file:
        file.write(struct.pack("<4sIII", b"SNAM", 1, len(labels), 24))
        for label in labels:
            file.write(label.ljust(24,bytes(1)))
    return len(stars)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("hyg_csv", type=pathlib.Path)
    parser.add_argument("dest", type=pathlib.Path)
    parser.add_argument("--limit", type=float, default=9.5)
    args = parser.parse_args()
    count = make_catalog(args.hyg_csv, args.dest, args.limit)
    print(f"Saved {count:,} stars to {args.dest} ({args.dest.stat().st_size:,} bytes)")
