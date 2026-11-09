#!/usr/bin/env python3
"""Convert d3-celestial constellation figures to CONST.BIN for the camera.

Source: https://github.com/ofrohn/d3-celestial (data/constellations.lines.json
and data/constellations.json), BSD-3-Clause, Olaf Frohn.

Output (little endian):
  header  <4sIII  b"SCON", version 1, segment count, label count
  segment <IiIi   RA1, Dec1, RA2, Dec2 in 1e-4 degree (J2000)
  label   <IiB3x20s RA, Dec (label anchor), rank 1-3, ASCII name

Name anchors are the centroid of each constellation's own figure, so the
name sits inside the drawn figure on a camera frame (d3-celestial's anchors
are placed for whole-sky maps and often lie outside it).
"""
import argparse
import json
import math
import pathlib
import struct

HEADER = struct.Struct("<4sIII")
SEGMENT = struct.Struct("<IiIi")
LABEL = struct.Struct("<IiB3x20s")


def coord(lon: float, lat: float) -> tuple[int, int]:
    return round(lon * 10000) % 3600000, round(lat * 10000)


def make(lines_json: pathlib.Path, names_json: pathlib.Path, dest: pathlib.Path) -> tuple[int, int]:
    segments = set()
    centroids = {}
    for feature in json.loads(lines_json.read_text(encoding="utf-8"))["features"]:
        vertices = {tuple(p) for line in feature["geometry"]["coordinates"] for p in line}
        v = [0.0, 0.0, 0.0]
        for lon, lat in vertices:
            r, d = math.radians(lon), math.radians(lat)
            v[0] += math.cos(d) * math.cos(r); v[1] += math.cos(d) * math.sin(r); v[2] += math.sin(d)
        norm = math.sqrt(sum(c * c for c in v))
        if norm > 1e-6:
            centroids[feature["id"]] = (math.degrees(math.atan2(v[1], v[0])),
                                        math.degrees(math.asin(v[2] / norm)))
        for line in feature["geometry"]["coordinates"]:
            for a, b in zip(line, line[1:]):
                p, q = coord(*a), coord(*b)
                if p != q:
                    segments.add(min(p, q) + max(p, q))
    labels = []
    for feature in json.loads(names_json.read_text(encoding="utf-8"))["features"]:
        props = feature["properties"]
        ra, dec = coord(*centroids.get(feature["id"], feature["geometry"]["coordinates"]))
        name = props["name"].encode("ascii", "ignore")[:19]
        labels.append((ra, dec, int(props.get("rank", 3)), name))
    segments = sorted(segments)
    labels.sort()
    dest.parent.mkdir(parents=True, exist_ok=True)
    with dest.open("wb") as f:
        f.write(HEADER.pack(b"SCON", 1, len(segments), len(labels)))
        for s in segments:
            f.write(SEGMENT.pack(*s))
        for ra, dec, rank, name in labels:
            f.write(LABEL.pack(ra, dec, rank, name))
    return len(segments), len(labels)


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("lines_json", type=pathlib.Path)
    ap.add_argument("names_json", type=pathlib.Path)
    ap.add_argument("dest", type=pathlib.Path)
    a = ap.parse_args()
    nseg, nlab = make(a.lines_json, a.names_json, a.dest)
    print(f"Saved {nseg} constellation segments and {nlab} labels to {a.dest} "
          f"({a.dest.stat().st_size:,} bytes)")
