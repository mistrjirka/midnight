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

def make_catalog(source: pathlib.Path, dest: pathlib.Path, limiting_mag: float) -> int:
    stars = []
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
            stars.append((ra, dec, mag10, 0))
    dest.parent.mkdir(parents=True, exist_ok=True)
    with dest.open("wb") as file:
        file.write(HEADER.pack(b"ASTR", 1, len(stars), RECORD.size))
        for star in stars:
            file.write(RECORD.pack(*star))
    return len(stars)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("hyg_csv", type=pathlib.Path)
    parser.add_argument("dest", type=pathlib.Path)
    parser.add_argument("--limit", type=float, default=9.5)
    args = parser.parse_args()
    count = make_catalog(args.hyg_csv, args.dest, args.limit)
    print(f"Saved {count:,} stars to {args.dest} ({args.dest.stat().st_size:,} bytes)")
