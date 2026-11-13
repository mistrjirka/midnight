"""Deep-sky catalogue for the target picker and overlays -> DSO.BIN / DSO.KEY / DSO.NAM.

Inputs:
  OpenNGC NGC.csv + addendum.csv (CC BY-SA 4.0, Mattia Verga): NGC, IC, Messier and
      Caldwell cross-identifications, sizes, position angles, magnitudes, common names.
  sources/barnard_VII_220A.dat (CDS VII/220A): Barnard dark nebulae, J2000.
  sources/sharpless_VII_20.dat (CDS VII/20): Sharpless HII regions, B1950.

DSO.BIN  <4sIIIII "SDSO", 1, count, 24, 72, 36; then 72*36+1 uint32 tile offsets; then
         <IiHHhhBBHHH records: RA, Dec (1e-4 deg), major/minor axis (0.1 arcmin), position
         angle (deg, -1 unknown), magnitude*10 (32767 unknown), type, best catalogue, best
         number, common-name id, reserved.
DSO.KEY  <4sIII "SKEY", 1, count, 8; then sorted <BBHI (catalogue, 0, number, record).
DSO.NAM  Same layout as STARS.NAM.
"""
from __future__ import annotations

import csv
import math
import pathlib
import re
import struct

from .catalog import RA_BINS, DEC_BINS, NAME_BYTES, tile_bounds, tile_id

# Catalogue IDs shared with astro/core/catalog.h. Order = picker order.
CATALOGUES = {"M": 1, "NGC": 2, "IC": 3, "C": 4, "B": 5, "Sh2": 6}
# Preferred display designation: M, C, NGC, IC, Sh2, B.
PRIORITY = (1, 4, 2, 3, 6, 5)
# Object types shared with astro/core/catalog.h.
TYPES = {"G": 1, "GPair": 1, "GTrpl": 1, "GGroup": 1, "OCl": 2, "GCl": 3, "PN": 4,
         "HII": 5, "EmN": 5, "Neb": 5, "RfN": 6, "DrkN": 7, "SNR": 8, "Cl+N": 9,
         "*Ass": 10, "*": 11, "**": 11}
RECORD = struct.Struct("<IiHHhhBBHHH")
KEY = struct.Struct("<BBHI")
UNKNOWN_MAG = 32767
MAX_NAME = 23

# FK4 B1950 -> FK5 J2000 rotation (E-terms neglected: far below Sharpless region sizes).
B1950_TO_J2000 = ((0.9999256782, -0.0111820611, -0.0048579477),
                  (0.0111820610, 0.9999374784, -0.0000271765),
                  (0.0048579479, -0.0000271474, 0.9999881997))


def hms(text: str) -> float:
    h, m, s = text.split(":")
    return (int(h) + int(m) / 60 + float(s) / 3600) * 15


def dms(text: str) -> float:
    sign = -1 if text.startswith("-") else 1
    d, m, s = text.lstrip("+-").split(":")
    return sign * (int(d) + int(m) / 60 + float(s) / 3600)


def precess_b1950(ra: float, dec: float) -> tuple[float, float]:
    r, d = math.radians(ra), math.radians(dec)
    v = (math.cos(d) * math.cos(r), math.cos(d) * math.sin(r), math.sin(d))
    x, y, z = (sum(B1950_TO_J2000[i][j] * v[j] for j in range(3)) for i in range(3))
    return math.degrees(math.atan2(y, x)) % 360, math.degrees(math.asin(max(-1, min(1, z))))


def number(value: str) -> float | None:
    try:
        return float(value)
    except ValueError:
        return None


class Catalogue:
    def __init__(self):
        self.objects: list[dict] = []
        self.keys: dict[tuple[int, int], int] = {}
        self.names: list[bytes] = [b""]

    def add(self, ra, dec, major, minor, pa, mag, kind, name="") -> int:
        label = 0
        encoded = name.encode("ascii", "ignore")[:MAX_NAME] if name else b""
        if encoded:
            self.names.append(encoded)
            label = len(self.names) - 1
        self.objects.append(dict(ra=ra, dec=dec, maj=major, min=minor or major, pa=pa,
                                 mag=mag, type=kind, name=label, ids=[]))
        return len(self.objects) - 1

    def key(self, catalogue: int, num: int, index: int) -> None:
        """First designation wins; later duplicates are ignored."""
        if (catalogue, num) not in self.keys:
            self.keys[(catalogue, num)] = index
            self.objects[index]["ids"].append((catalogue, num))


def designation(name: str) -> tuple[int, int] | None:
    m = re.fullmatch(r"(NGC|IC|M|C|B)(\d+)", name)
    return (CATALOGUES[m.group(1)], int(m.group(2))) if m else None


def cross_identifications(row: dict) -> list[tuple[int, int]]:
    ids = []
    own = designation(row["Name"])
    if own:
        ids.append(own)
    if row["M"]:
        ids.append((CATALOGUES["M"], int(row["M"])))
    for ident in row["Identifiers"].split(","):
        ident = ident.strip()
        for pattern, catalogue in ((r"C (\d+)", "C"), (r"SH 2-(\d+)", "Sh2"), (r"B (\d+)", "B")):
            m = re.fullmatch(pattern, ident)
            if m:
                ids.append((CATALOGUES[catalogue], int(m.group(1))))
    return ids


def add_openngc_row(cat: Catalogue, row: dict) -> int | None:
    kind = row["Type"]
    ids = cross_identifications(row)
    if kind not in TYPES or not row["RA"] or not row["Dec"]:
        # Non-existent objects, novae and "Other": only keep Messier/Caldwell objects.
        if not any(c in (CATALOGUES["M"], CATALOGUES["C"]) for c, _ in ids) or not row["RA"]:
            return None
    pa = number(row["PosAng"])
    index = cat.add(hms(row["RA"]), dms(row["Dec"]), number(row["MajAx"]) or 0.0,
                    number(row["MinAx"]) or 0.0, -1 if pa is None else round(pa) % 180,
                    number(row["V-Mag"]) or number(row["B-Mag"]), TYPES.get(kind, 11),
                    row["Common names"].split(",")[0].strip())
    for c, n in ids:
        cat.key(c, n, index)
    return index


def link_duplicate(cat: Catalogue, row: dict, index_by_name: dict) -> None:
    """OpenNGC "Dup" rows share the canonical object's record."""
    name = row["Name"]
    if name == "M102":
        target = index_by_name.get("NGC5866")  # usual modern identification
    elif row["NGC"]:
        target = index_by_name.get("NGC" + row["NGC"].zfill(4))
    elif row["IC"]:
        target = index_by_name.get("IC" + row["IC"].zfill(4))
    else:
        target = None
    if target is None:
        return
    own = designation(name)
    if own:
        cat.key(*own, target)
    if name == "M102":
        cat.key(CATALOGUES["M"], 102, target)
    elif row["M"]:
        cat.key(CATALOGUES["M"], int(row["M"]), target)


def load_openngc(cat: Catalogue, paths: list[pathlib.Path]) -> None:
    rows = []
    for path in paths:
        with path.open(newline="", encoding="utf-8") as f:
            rows += list(csv.DictReader(f, delimiter=";"))
    index_by_name = {}
    duplicates = []
    for row in rows:
        if row["Type"] == "Dup":
            duplicates.append(row)
            continue
        index = add_openngc_row(cat, row)
        if index is not None:
            index_by_name[row["Name"]] = index
    for row in duplicates:
        link_duplicate(cat, row, index_by_name)


def load_barnard(cat: Catalogue, path: pathlib.Path) -> None:
    for line in path.read_text(encoding="ascii").splitlines():
        m = re.fullmatch(r"(\d+)(a?)", line[1:5].strip())
        if not m or not line[22:38].strip():
            continue
        num = int(m.group(1))
        if (CATALOGUES["B"], num) in cat.keys:
            continue  # already present via OpenNGC (e.g. B33, the Horsehead)
        ra = (int(line[22:24]) + int(line[25:27]) / 60 + int(line[28:30].strip() or 0) / 3600) * 15
        dec = int(line[33:35]) + int(line[36:38]) / 60
        if line[32] == "-":
            dec = -dec
        diameter = number(line[39:44]) or 0.0
        index = cat.add(ra, dec, diameter, diameter, -1, None, TYPES["DrkN"])
        cat.key(CATALOGUES["B"], num, index)


def load_sharpless(cat: Catalogue, path: pathlib.Path) -> None:
    for line in path.read_text(encoding="ascii").splitlines():
        num = int(line[0:4])
        if (CATALOGUES["Sh2"], num) in cat.keys:
            continue  # already present via OpenNGC (e.g. Sh2-155, the Cave Nebula)
        ra = (int(line[34:36]) + int(line[36:38]) / 60 + int(line[38:41]) / 36000) * 15
        dec = int(line[42:44]) + int(line[44:46]) / 60 + int(line[46:48]) / 3600
        if line[41] == "-":
            dec = -dec
        ra, dec = precess_b1950(ra, dec)
        diameter = float(line[48:52])
        index = cat.add(ra, dec, diameter, diameter, -1, None, TYPES["HII"])
        cat.key(CATALOGUES["Sh2"], num, index)


def record(o: dict) -> tuple:
    ids = sorted(o["ids"], key=lambda i: PRIORITY.index(i[0]))
    best = ids[0] if ids else (0, 0)
    ra = round(o["ra"] * 10000) % 3600000
    dec = round(o["dec"] * 10000)
    mag = UNKNOWN_MAG if o["mag"] is None else round(o["mag"] * 10)
    return (ra, dec, min(65535, round(o["maj"] * 10)), min(65535, round(o["min"] * 10)),
            o["pa"], mag, o["type"], best[0], best[1], o["name"], 0)


def write(cat: Catalogue, dest: pathlib.Path) -> None:
    records = []
    for index, o in enumerate(cat.objects):
        rec = record(o)
        records.append((tile_id(rec[0], rec[1]), rec[0], rec[1], index, rec))
    records.sort()
    position = {index: i for i, (_, _, _, index, _) in enumerate(records)}
    bounds = tile_bounds([tile for tile, *_ in records])

    dest.parent.mkdir(parents=True, exist_ok=True)
    with dest.open("wb") as f:
        f.write(struct.pack("<4sIIIII", b"SDSO", 1, len(records), RECORD.size, RA_BINS,
                            DEC_BINS))
        f.write(struct.pack(f"<{len(bounds)}I", *bounds))
        for *_, rec in records:
            f.write(RECORD.pack(*rec))
    keys = sorted((c, n, position[i]) for (c, n), i in cat.keys.items())
    with dest.with_suffix(".KEY").open("wb") as f:
        f.write(struct.pack("<4sIII", b"SKEY", 1, len(keys), KEY.size))
        for c, n, rec in keys:
            f.write(KEY.pack(c, 0, n, rec))
    with dest.with_suffix(".NAM").open("wb") as f:
        f.write(struct.pack("<4sIII", b"SNAM", 1, len(cat.names), NAME_BYTES))
        for label in cat.names:
            f.write(label.ljust(NAME_BYTES, b"\0"))


def build(openngc_dir: pathlib.Path, barnard: pathlib.Path, sharpless: pathlib.Path,
          dest: pathlib.Path) -> dict[str, int]:
    """Writes DSO.BIN/.KEY/.NAM. Returns designations per catalogue."""
    cat = Catalogue()
    load_openngc(cat, [openngc_dir / "NGC.csv", openngc_dir / "addendum.csv"])
    load_barnard(cat, barnard)
    load_sharpless(cat, sharpless)
    write(cat, dest)
    counts = {name: sum(1 for c, _ in cat.keys if c == cid) for name, cid in CATALOGUES.items()}
    counts["objects"] = len(cat.objects)
    return counts
