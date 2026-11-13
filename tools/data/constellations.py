"""d3-celestial constellation figures -> CONST.BIN.

Source: https://github.com/ofrohn/d3-celestial (BSD-3-Clause, Olaf Frohn).

header  <4sIII     "SCON", 1, segment count, label count
segment <IiIi      RA1, Dec1, RA2, Dec2 in 1e-4 degree (J2000)
label   <IiB3x20s  RA, Dec of the anchor, rank 1-3, ASCII name

Name anchors are the centroid of each figure's own vertices, so the name sits inside the
drawn figure (d3-celestial's anchors are placed for whole-sky maps).
"""
from __future__ import annotations

import json
import math
import pathlib
import struct

HEADER = struct.Struct("<4sIII")
SEGMENT = struct.Struct("<IiIi")
LABEL = struct.Struct("<IiB3x20s")
MAX_NAME = 19


def coord(lon: float, lat: float) -> tuple[int, int]:
    return round(lon * 10000) % 3600000, round(lat * 10000)


def centroid(vertices) -> tuple[float, float] | None:
    """Mean direction of a set of (lon, lat) points, or None if they cancel out."""
    x = y = z = 0.0
    for lon, lat in vertices:
        r, d = math.radians(lon), math.radians(lat)
        x += math.cos(d) * math.cos(r)
        y += math.cos(d) * math.sin(r)
        z += math.sin(d)
    norm = math.sqrt(x * x + y * y + z * z)
    if norm <= 1e-6:
        return None
    return math.degrees(math.atan2(y, x)), math.degrees(math.asin(z / norm))


def read_figures(lines_json: pathlib.Path):
    segments = set()
    centroids = {}
    for feature in json.loads(lines_json.read_text(encoding="utf-8"))["features"]:
        lines = feature["geometry"]["coordinates"]
        centre = centroid({tuple(p) for line in lines for p in line})
        if centre:
            centroids[feature["id"]] = centre
        for line in lines:
            for a, b in zip(line, line[1:]):
                p, q = coord(*a), coord(*b)
                if p != q:
                    segments.add(min(p, q) + max(p, q))  # undirected, deduplicated
    return sorted(segments), centroids


def read_labels(names_json: pathlib.Path, centroids):
    labels = []
    for feature in json.loads(names_json.read_text(encoding="utf-8"))["features"]:
        props = feature["properties"]
        anchor = centroids.get(feature["id"], feature["geometry"]["coordinates"])
        ra, dec = coord(*anchor)
        name = props["name"].encode("ascii", "ignore")[:MAX_NAME]
        labels.append((ra, dec, int(props.get("rank", 3)), name))
    labels.sort()
    return labels


def build(lines_json: pathlib.Path, names_json: pathlib.Path,
          dest: pathlib.Path) -> tuple[int, int]:
    segments, centroids = read_figures(lines_json)
    labels = read_labels(names_json, centroids)
    dest.parent.mkdir(parents=True, exist_ok=True)
    with dest.open("wb") as f:
        f.write(HEADER.pack(b"SCON", 1, len(segments), len(labels)))
        for segment in segments:
            f.write(SEGMENT.pack(*segment))
        for ra, dec, rank, name in labels:
            f.write(LABEL.pack(ra, dec, rank, name))
    return len(segments), len(labels)
