"""All-sky multiscale quad index for the blind solver -> SKY.DIR / SKY.PAT / SKY.FLD.

Only HYG positions and magnitudes are inputs. Overlapping circular sky fields are laid out
at several radii; the 10 brightest stars of each field give C(10,4) = 210 quad patterns,
and up to 96 stars per field are stored for independent verification on the camera.

SKY.FLD  <4sIII "SFLD", 1, field count, 784; then fields: <IiHHI (RA, Dec, star count,
         radius * 100, coordinate unit in millidegrees or 0) + 96 x <hhHH stars.
SKY.PAT  <4sIII "SPAT", 1, count, 16; then <IIIBBBB (key, fine, field, a, b, flip, 0),
         sorted by key.
SKY.DIR  <4sIII "SDIR", 1, key count, pattern count; then <II (key, first pattern) per
         distinct key plus an end sentinel (0xffffffff, pattern count).

The quad encoding here must match astro/core/sky_pattern.c exactly.
"""
from __future__ import annotations

import math
import multiprocessing as mp
import os
import pathlib
import struct
import time
from itertools import combinations
from typing import Callable

import numpy as np
from scipy.spatial import cKDTree

PATTERN_DTYPE = np.dtype([("key", "<u4"), ("fine", "<u4"), ("field", "<u4"), ("a", "u1"),
                          ("b", "u1"), ("flip", "u1"), ("pad", "u1")])
STAR_DTYPE = np.dtype([("x", "<i2"), ("y", "<i2"), ("name", "<u2"), ("mag", "<u2")])
CATALOG_DTYPE = [("ra", "<u4"), ("dec", "<i4"), ("mag", "<u2"), ("name", "<u2")]

PATTERN_STARS = 10
QUADS = np.array(list(combinations(range(PATTERN_STARS), 4)), np.int32)
EDGES = np.array(list(combinations(range(4), 2)), np.int32)
VERIFY_STARS = 96
FIELD_BYTES = 16 + VERIFY_STARS * 8
CODE_STEPS = 24
FINE_MAX = 255
MIN_PATTERN_SPAN = 500  # field units: tiny quads amplify centroid error
INT16_SAFE = 32760

# Field radii (degrees). SKY.* covers up to ~60 degree frames; the optional SKYW.* index adds
# 19 and 27 degree fields for wider lenses, stored in 4-millidegree units.
RADII = (2.3, 3.5, 5.5, 8.5, 13.0)
RADII_WIDE = (19.0, 27.0)
FIELD_SPACING = 0.72  # centre spacing as a fraction of the radius
TASK_FIELDS = 256

# Shared read-only state for forked workers: catalogue, unit vectors, k-d tree, magnitudes.
_SHARED = None


def field_unit(radius: float) -> int:
    return 1 if radius <= 13 else 4


def encode(points: np.ndarray):
    """Canonical quad code, fine descriptor and baseline roles for all 210 quads.

    Mirrors sky_quad_code(): the longest pair is the baseline; both directions and both
    mirror parities are tried; the smallest code wins.
    """
    q = points[QUADS]
    edges = q[:, EDGES[:, 1]] - q[:, EDGES[:, 0]]
    lengths = np.sum(edges * edges, axis=2)
    longest = lengths.argmax(axis=1)
    a, b = EDGES[longest, 0], EDGES[longest, 1]
    valid = lengths.max(axis=1) > 1e-8
    ix = np.arange(len(q))
    others = np.array([[k for k in range(4) if k not in (aa, bb)] for aa, bb in zip(a, b)])

    best = np.full(len(q), 2**32 - 1, np.uint32)
    fine = np.zeros(len(q), np.uint32)
    best_a, best_b = a.copy(), b.copy()
    best_flip = np.zeros(len(q), np.uint8)
    for swap in range(2):
        start, end = (a, b) if not swap else (b, a)
        origin = q[ix, start]
        baseline = q[ix, end] - origin
        denom = np.maximum((baseline * baseline).sum(1), 1e-18)
        c = q[ix[:, None], others] - origin[:, None, :]
        along = np.clip((c[:, :, 0] * baseline[:, None, 0] + c[:, :, 1] * baseline[:, None, 1])
                        / denom[:, None], 0, 1)
        across = np.clip((c[:, :, 0] * baseline[:, None, 1] - c[:, :, 1] * baseline[:, None, 0])
                         / denom[:, None], -1, 1)
        for flip in range(2):
            signed = across * (-1 if flip else 1)
            cx = np.rint(along * CODE_STEPS).astype(np.uint32)
            cy = np.rint((signed + 1) * (CODE_STEPS / 2)).astype(np.uint32)
            fx = np.rint(along * FINE_MAX).astype(np.uint32)
            fy = np.rint((signed + 1) * (FINE_MAX / 2)).astype(np.uint32)
            reverse = (cx[:, 0] > cx[:, 1]) | ((cx[:, 0] == cx[:, 1]) & (cy[:, 0] > cy[:, 1]))
            k = np.where(reverse, 1, 0)
            other = 1 - k
            key = (cx[ix, k] << 15) | (cy[ix, k] << 10) | (cx[ix, other] << 5) | cy[ix, other]
            f = (fx[ix, k] << 24) | (fy[ix, k] << 16) | (fx[ix, other] << 8) | fy[ix, other]
            choose = key < best
            best[choose] = key[choose]
            fine[choose] = f[choose]
            best_a[choose] = start[choose]
            best_b[choose] = end[choose]
            best_flip[choose] = flip
    return best, fine, best_a, best_b, best_flip, valid


def brightest(ids, mags, limit):
    return sorted(ids, key=lambda j: (int(mags[j]), int(j)))[:limit]


def field_block(field_id: int, ra0: float, dec0: float, radius: float):
    """One SKY.FLD record and its pattern records (or an empty field)."""
    catalog, xyz, tree, mags = _SHARED
    ra, dec = np.deg2rad([ra0, dec0])
    centre = np.array([math.cos(dec) * math.cos(ra), math.cos(dec) * math.sin(ra), math.sin(dec)])
    east = np.array([-math.sin(ra), math.cos(ra), 0.0])
    north = np.cross(centre, east)

    pattern_ids = brightest(tree.query_ball_point(centre, 2 * math.sin(math.radians(radius) / 2)),
                            mags, PATTERN_STARS)
    if len(pattern_ids) < PATTERN_STARS:
        return bytes(FIELD_BYTES), None

    # Verification stars cover the frame beyond the pattern region. Wide fields use coarser
    # units so 55-degree fields still fit int16.
    unit = field_unit(radius)
    reach = min(30, radius * 2.1) if unit == 1 else min(55, radius * 2.1)
    nearby = np.asarray(tree.query_ball_point(centre, 2 * math.sin(math.radians(reach) / 2)),
                        dtype=int)
    extra = brightest((j for j in nearby if j not in pattern_ids), mags,
                      VERIFY_STARS - PATTERN_STARS)
    selected = np.array(pattern_ids + extra, dtype=int)
    depth = xyz[selected] @ centre
    pos = np.column_stack((xyz[selected] @ east, xyz[selected] @ north)) / depth[:, None]
    pos *= 180000 / math.pi / unit
    inside = np.all(np.abs(pos) < INT16_SAFE, axis=1)
    selected, pos = selected[inside], pos[inside]
    if len(selected) < PATTERN_STARS:
        return bytes(FIELD_BYTES), None

    stars = np.zeros(VERIFY_STARS, dtype=STAR_DTYPE)
    stars["x"][:len(pos)] = np.rint(pos[:, 0]).astype(np.int16)
    stars["y"][:len(pos)] = np.rint(pos[:, 1]).astype(np.int16)
    stars["name"][:len(pos)] = catalog["name"][selected]
    stars["mag"][:len(pos)] = mags[selected]
    header = struct.pack("<IiHHI", round(ra0 * 10000) % 3600000, round(dec0 * 10000),
                         len(selected), round(radius * 100), unit if unit > 1 else 0)

    # Encode the quantised coordinates actually stored on the card.
    points = np.column_stack((stars["x"][:PATTERN_STARS], stars["y"][:PATTERN_STARS])).astype(float)
    key, fine, a, b, flip, valid = encode(points)
    rows = np.arange(len(QUADS))
    span = np.sum((points[QUADS[rows, a]] - points[QUADS[rows, b]]) ** 2, axis=1)
    valid &= span >= MIN_PATTERN_SPAN ** 2
    records = np.empty(np.count_nonzero(valid), PATTERN_DTYPE)
    records["key"] = key[valid]
    records["fine"] = fine[valid]
    records["field"] = field_id
    records["a"] = QUADS[rows, a][valid]
    records["b"] = QUADS[rows, b][valid]
    records["flip"] = flip[valid]
    records["pad"] = 0
    return header + stars.tobytes(), records


def worker(task):
    start, fields = task
    blocks, records = [], []
    for offset, (ra0, dec0, radius) in enumerate(fields):
        block, recs = field_block(start + offset, ra0, dec0, radius)
        blocks.append(block)
        if recs is not None:
            records.append(recs)
    patterns = np.concatenate(records) if records else np.empty(0, PATTERN_DTYPE)
    return patterns, b"".join(blocks), start


def declination_bands(spacing: float, layout: str) -> np.ndarray:
    """Band centres. "polar" is symmetric and includes +-90 degree cap fields; "midnight"
    reproduces the original layout, which stops short of the north celestial pole."""
    if layout == "midnight":
        return np.arange(-86.0, 86.001, spacing)
    return np.linspace(-90.0, 90.0, math.ceil(180.0 / spacing) + 1)


def field_layout(radii, layout: str):
    fields = []
    for radius in radii:
        spacing = radius * FIELD_SPACING
        for dec in declination_bands(spacing, layout):
            count = max(1, round(360 * math.cos(math.radians(dec)) / spacing))
            for k in range(count):
                fields.append((k * 360 / count, float(dec), radius))
    return fields


def write_patterns(patterns: np.ndarray, pat_path: pathlib.Path, dir_path: pathlib.Path) -> int:
    patterns.sort(order=["key", "fine", "field", "a", "b"])
    with pat_path.open("wb") as f:
        f.write(struct.pack("<4sIII", b"SPAT", 1, len(patterns), PATTERN_DTYPE.itemsize))
        patterns.tofile(f)
    unique, begin = np.unique(patterns["key"], return_index=True)
    table = np.zeros(len(unique) + 1, dtype=[("key", "<u4"), ("offset", "<u4")])
    table["key"][:-1] = unique
    table["offset"][:-1] = begin
    table[-1] = (0xFFFFFFFF, len(patterns))
    with dir_path.open("wb") as f:
        f.write(struct.pack("<4sIII", b"SDIR", 1, len(unique), len(patterns)))
        table.tofile(f)
    return len(unique)


def build(stars_bin: pathlib.Path, dest: pathlib.Path, prefix: str = "SKY", wide: bool = False,
          layout: str = "polar", jobs: int | None = None,
          log: Callable[[str], None] = print) -> dict[str, int]:
    """Writes <prefix>.FLD/.PAT/.DIR into dest. Returns counts."""
    global _SHARED
    started = time.monotonic()
    catalog = np.frombuffer(stars_bin.read_bytes()[16:], dtype=CATALOG_DTYPE)
    ra = np.deg2rad(catalog["ra"] / 10000)
    dec = np.deg2rad(catalog["dec"] / 10000)
    xyz = np.column_stack((np.cos(dec) * np.cos(ra), np.cos(dec) * np.sin(ra), np.sin(dec)))
    _SHARED = (catalog, xyz, cKDTree(xyz), catalog["mag"])

    fields = field_layout(RADII_WIDE if wide else RADII, layout)
    log(f"{prefix}: {len(fields)} fields, up to {len(fields) * len(QUADS):,} patterns")
    dest.mkdir(parents=True, exist_ok=True)
    tasks = [(i, fields[i:i + TASK_FIELDS]) for i in range(0, len(fields), TASK_FIELDS)]
    arrays = []
    with (dest / f"{prefix}.FLD").open("wb") as fld:
        fld.write(struct.pack("<4sIII", b"SFLD", 1, len(fields), FIELD_BYTES))
        fld.truncate(16 + len(fields) * FIELD_BYTES)
        with mp.get_context("fork").Pool(jobs or os.cpu_count()) as pool:
            for done, (patterns, blocks, start) in enumerate(pool.imap_unordered(worker, tasks)):
                arrays.append(patterns)
                fld.seek(16 + start * FIELD_BYTES)
                fld.write(blocks)
                if done % 16 == 0:
                    log(f"  {done + 1}/{len(tasks)} chunks, {time.monotonic() - started:.1f} s")
    patterns = np.concatenate(arrays)
    keys = write_patterns(patterns, dest / f"{prefix}.PAT", dest / f"{prefix}.DIR")
    log(f"{prefix}: {len(patterns):,} patterns, {keys:,} keys, "
        f"{time.monotonic() - started:.1f} s")
    return {"fields": len(fields), "patterns": len(patterns), "keys": keys}
