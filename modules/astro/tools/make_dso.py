#!/usr/bin/env python3
"""Build the deep-sky catalogue used by the target picker and sky overlays.

Inputs:
  OpenNGC NGC.csv + addendum.csv (CC BY-SA 4.0, Mattia Verga): NGC, IC,
      Messier and Caldwell cross-identifications, sizes, position angles,
      magnitudes and common names.
  data/barnard_VII_220A.dat (CDS VII/220A): Barnard dark nebulae, J2000.
  data/sharpless_VII_20.dat (CDS VII/20): Sharpless HII regions, B1950.

Outputs (little endian), next to `dest` (normally DSO.BIN):
  DSO.BIN  <4sIIIII "SDSO", 1, count, 24, 72, 36; then 72*36+1 uint32 tile
           offsets (5-degree tiles, same layout as STARS.IDX), then records
           <IiHHhhBBHHH: RA, Dec (1e-4 deg), major/minor axis (0.1 arcmin),
           position angle (deg, -1 unknown), magnitude*10 (32767 unknown),
           type, best catalogue, best number, common-name id, reserved.
  DSO.KEY  <4sIII "SKEY", 1, count, 8; then sorted <BBHI records
           (catalogue, 0, number, record index) for every designation.
  DSO.NAM  Same layout as STARS.NAM: 24-byte ASCII common names.
"""
from __future__ import annotations
import argparse
import csv
import math
import pathlib
import re
import struct

# Catalogue IDs shared with astro_overlay.h. Order = picker order.
CATALOGUES = {"M": 1, "NGC": 2, "IC": 3, "C": 4, "B": 5, "Sh2": 6}
PRIORITY = (1, 4, 2, 3, 6, 5)  # display designation preference: M, C, NGC, IC, Sh2, B
# Object types shared with astro_overlay.h.
TYPES = {"G": 1, "GPair": 1, "GTrpl": 1, "GGroup": 1, "OCl": 2, "GCl": 3, "PN": 4,
         "HII": 5, "EmN": 5, "Neb": 5, "RfN": 6, "DrkN": 7, "SNR": 8, "Cl+N": 9,
         "*Ass": 10, "*": 11, "**": 11}
RA_BINS, DEC_BINS = 72, 36
RECORD = struct.Struct("<IiHHhhBBHHH")
KEY = struct.Struct("<BBHI")
UNKNOWN_MAG = 32767
# FK4 B1950 -> FK5 J2000 rotation (standard matrix; E-terms neglected,
# well below the arcminute sizes of Sharpless regions).
B1950 = ((0.9999256782, -0.0111820611, -0.0048579477),
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
    x, y, z = (sum(B1950[i][j] * v[j] for j in range(3)) for i in range(3))
    return math.degrees(math.atan2(y, x)) % 360, math.degrees(math.asin(max(-1, min(1, z))))


class Catalogue:
    def __init__(self):
        self.objects: list[dict] = []
        self.keys: dict[tuple[int, int], int] = {}
        self.names: list[bytes] = [b""]

    def add(self, ra, dec, maj, minor, pa, mag, kind, name=""):
        label = 0
        if name:
            encoded = name.encode("ascii", "ignore")[:23]
            if encoded:
                self.names.append(encoded)
                label = len(self.names) - 1
        self.objects.append(dict(ra=ra, dec=dec, maj=maj, min=minor or maj, pa=pa,
                                 mag=mag, type=kind, name=label, ids=[]))
        return len(self.objects) - 1

    def key(self, cat: int, num: int, index: int) -> None:
        if (cat, num) not in self.keys:
            self.keys[(cat, num)] = index
            self.objects[index]["ids"].append((cat, num))


def designation(name: str) -> tuple[int, int] | None:
    m = re.fullmatch(r"(NGC|IC|M|C|B)(\d+)", name)
    if m:
        return CATALOGUES[m.group(1)], int(m.group(2))
    return None


def number(value: str) -> float | None:
    try:
        return float(value)
    except ValueError:
        return None


def load_openngc(cat: Catalogue, paths: list[pathlib.Path]) -> None:
    rows = []
    for path in paths:
        with path.open(newline="", encoding="utf-8") as f:
            rows += list(csv.DictReader(f, delimiter=";"))
    index_by_name = {}
    duplicates = []
    for row in rows:
        kind = row["Type"]
        if kind == "Dup":
            duplicates.append(row)
            continue
        ids = []
        own = designation(row["Name"])
        if own:
            ids.append(own)
        if row["M"]:
            ids.append((CATALOGUES["M"], int(row["M"])))
        for ident in row["Identifiers"].split(","):
            ident = ident.strip()
            m = re.fullmatch(r"C (\d+)", ident)
            if m:
                ids.append((CATALOGUES["C"], int(m.group(1))))
            m = re.fullmatch(r"SH 2-(\d+)", ident)
            if m:
                ids.append((CATALOGUES["Sh2"], int(m.group(1))))
            m = re.fullmatch(r"B (\d+)", ident)
            if m:
                ids.append((CATALOGUES["B"], int(m.group(1))))
        if kind not in TYPES or not row["RA"] or not row["Dec"]:
            if not any(c in (1, 4) for c, _ in ids) or not row["RA"]:
                continue  # non-existent, novae, "Other": only keep M/C objects
        mag = number(row["V-Mag"]) or number(row["B-Mag"])
        maj = number(row["MajAx"]) or 0.0
        minor = number(row["MinAx"]) or 0.0
        pa = number(row["PosAng"])
        common = row["Common names"].split(",")[0].strip()
        index = cat.add(hms(row["RA"]), dms(row["Dec"]), maj, minor,
                        -1 if pa is None else round(pa) % 180, mag,
                        TYPES.get(kind, 11), common)
        index_by_name[row["Name"]] = index
        for c, n in ids:
            cat.key(c, n, index)
    # Duplicate entries share the canonical object's record.
    for row in duplicates:
        own = designation(row["Name"])
        target = None
        if row["Name"] == "M102":
            target = index_by_name.get("NGC5866")  # usual modern identification
        elif row["NGC"]:
            target = index_by_name.get("NGC" + row["NGC"].zfill(4))
        elif row["IC"]:
            target = index_by_name.get("IC" + row["IC"].zfill(4))
        if target is None:
            continue
        if own:
            cat.key(*own, target)
        if row["M"] and row["Name"] != "M102":
            cat.key(CATALOGUES["M"], int(row["M"]), target)
        if row["Name"] == "M102":
            cat.key(CATALOGUES["M"], 102, target)


def load_barnard(cat: Catalogue, path: pathlib.Path) -> None:
    for line in path.read_text(encoding="ascii").splitlines():
        m = re.fullmatch(r"(\d+)(a?)", line[1:5].strip())
        if not m or not line[22:38].strip():
            continue
        num = int(m.group(1))
        if (CATALOGUES["B"], num) in cat.keys:
            continue  # already present via OpenNGC (e.g. B33, Horsehead)
        ra = (int(line[22:24]) + int(line[25:27]) / 60 + int(line[28:30].strip() or 0) / 3600) * 15
        dec = int(line[33:35]) + int(line[36:38]) / 60
        if line[32] == "-":
            dec = -dec
        diam = number(line[39:44]) or 0.0
        cat.key(CATALOGUES["B"], num, cat.add(ra, dec, diam, diam, -1, None, TYPES["DrkN"]))


def load_sharpless(cat: Catalogue, path: pathlib.Path) -> None:
    for line in path.read_text(encoding="ascii").splitlines():
        num = int(line[0:4])
        if (CATALOGUES["Sh2"], num) in cat.keys:
            continue  # already present via OpenNGC (e.g. Sh2-155, Cave Nebula)
        ra = (int(line[34:36]) + int(line[36:38]) / 60 + int(line[38:41]) / 36000) * 15
        dec = int(line[42:44]) + int(line[44:46]) / 60 + int(line[46:48]) / 3600
        if line[41] == "-":
            dec = -dec
        ra, dec = precess_b1950(ra, dec)
        diam = float(line[48:52])
        cat.key(CATALOGUES["Sh2"], num, cat.add(ra, dec, diam, diam, -1, None, TYPES["HII"]))


def write(cat: Catalogue, dest: pathlib.Path) -> None:
    records = []
    for index, o in enumerate(cat.objects):
        ids = sorted(o["ids"], key=lambda i: PRIORITY.index(i[0]))
        best = ids[0] if ids else (0, 0)
        ra = round(o["ra"] * 10000) % 3600000
        dec = round(o["dec"] * 10000)
        mag = UNKNOWN_MAG if o["mag"] is None else round(o["mag"] * 10)
        rec = (ra, dec, min(65535, round(o["maj"] * 10)), min(65535, round(o["min"] * 10)),
               o["pa"], mag, o["type"], best[0], best[1], o["name"], 0)
        tile = min(DEC_BINS - 1, max(0, (dec + 900000) // 50000)) * RA_BINS + ra // 50000
        records.append((tile, ra, dec, index, rec))
    records.sort()
    position = {index: i for i, (_, _, _, index, _) in enumerate(records)}
    bounds = [0] * (RA_BINS * DEC_BINS + 1)
    for tile, *_ in records:
        bounds[tile + 1] += 1
    for i in range(1, len(bounds)):
        bounds[i] += bounds[i - 1]
    dest.parent.mkdir(parents=True, exist_ok=True)
    with dest.open("wb") as f:
        f.write(struct.pack("<4sIIIII", b"SDSO", 1, len(records), RECORD.size, RA_BINS, DEC_BINS))
        f.write(struct.pack(f"<{len(bounds)}I", *bounds))
        for *_, rec in records:
            f.write(RECORD.pack(*rec))
    keys = sorted((c, n, position[i]) for (c, n), i in cat.keys.items())
    with dest.with_suffix(".KEY").open("wb") as f:
        f.write(struct.pack("<4sIII", b"SKEY", 1, len(keys), KEY.size))
        for c, n, rec in keys:
            f.write(KEY.pack(c, 0, n, rec))
    with dest.with_suffix(".NAM").open("wb") as f:
        f.write(struct.pack("<4sIII", b"SNAM", 1, len(cat.names), 24))
        for label in cat.names:
            f.write(label.ljust(24, b"\0"))


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("openngc", type=pathlib.Path, help="directory holding NGC.csv and addendum.csv")
    ap.add_argument("dest", type=pathlib.Path, help="output DSO.BIN")
    data = pathlib.Path(__file__).resolve().parents[1] / "data"
    ap.add_argument("--barnard", type=pathlib.Path, default=data / "barnard_VII_220A.dat")
    ap.add_argument("--sharpless", type=pathlib.Path, default=data / "sharpless_VII_20.dat")
    a = ap.parse_args()
    cat = Catalogue()
    load_openngc(cat, [a.openngc / "NGC.csv", a.openngc / "addendum.csv"])
    load_barnard(cat, a.barnard)
    load_sharpless(cat, a.sharpless)
    write(cat, a.dest)
    counts = {name: sum(1 for c, _ in cat.keys if c == cid) for name, cid in CATALOGUES.items()}
    print(f"Saved {len(cat.objects):,} objects, {len(cat.keys):,} designations to {a.dest}:",
          ", ".join(f"{k} {v}" for k, v in counts.items()))


if __name__ == "__main__":
    main()
