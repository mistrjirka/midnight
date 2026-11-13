"""Pinned upstream catalogue downloads."""
from __future__ import annotations

import pathlib
import urllib.request

OPENNGC_COMMIT = "be3cd6c5498806a2fbb2f7e0452ef802dbb02be0"
D3_CELESTIAL_COMMIT = "7e720a3de062059d4c5400a379146a601d9010e0"

DOWNLOADS = {
    "hygdata_v41.csv":
        "https://raw.githubusercontent.com/astronexus/HYG-Database/main/hyg/CURRENT/hygdata_v41.csv",
    "NGC.csv":
        f"https://raw.githubusercontent.com/mattiaverga/OpenNGC/{OPENNGC_COMMIT}"
        "/database_files/NGC.csv",
    "addendum.csv":
        f"https://raw.githubusercontent.com/mattiaverga/OpenNGC/{OPENNGC_COMMIT}"
        "/database_files/addendum.csv",
    "constellations.lines.json":
        f"https://raw.githubusercontent.com/ofrohn/d3-celestial/{D3_CELESTIAL_COMMIT}"
        "/data/constellations.lines.json",
    "constellations.json":
        f"https://raw.githubusercontent.com/ofrohn/d3-celestial/{D3_CELESTIAL_COMMIT}"
        "/data/constellations.json",
}


def fetch(dest: pathlib.Path, force: bool = False) -> list[pathlib.Path]:
    """Downloads every source file that is missing (or all with force)."""
    dest.mkdir(parents=True, exist_ok=True)
    fetched = []
    for name, url in DOWNLOADS.items():
        target = dest / name
        if target.exists() and not force:
            continue
        partial = target.with_suffix(target.suffix + ".part")
        with urllib.request.urlopen(url, timeout=120) as response, partial.open("wb") as out:
            while chunk := response.read(1 << 20):
                out.write(chunk)
        partial.replace(target)
        fetched.append(target)
    return fetched
