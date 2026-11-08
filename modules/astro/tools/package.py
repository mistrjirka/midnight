#!/usr/bin/env python3
"""Combine a matching Magic Lantern build with the licensed HYG star catalog."""
import argparse
import pathlib
import zipfile
from datetime import date

ROOT = pathlib.Path(__file__).resolve().parents[3]
def package(camera: str, catalog: pathlib.Path) -> pathlib.Path:
    base=ROOT / "platform" / camera / "build" / "magiclantern.zip"
    target=ROOT / "dist" / f"magiclantern-astro-{camera}.zip"
    assert camera in {"600D.102","6D.116"}, "Unsupported camera build"
    assert base.is_file() and catalog.is_file(), "Build firmware and catalog first"
    target.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(base) as orig, zipfile.ZipFile(target,"w") as out:
        for info in orig.infolist():
            out.writestr(info,orig.read(info.filename))
        assert "ML/modules/astro.mo" in orig.namelist(), "Astro module missing"
        out.write(catalog,"ML/ASTRO/STARS.BIN",compress_type=zipfile.ZIP_DEFLATED)
        index=catalog.with_suffix(".IDX")
        assert index.is_file(), "Generate STARS.IDX with make_catalog.py"
        out.write(index,"ML/ASTRO/STARS.IDX",compress_type=zipfile.ZIP_DEFLATED)
        synthetic=ROOT/".astro-data"/"TEST.PGM"
        if synthetic.is_file():
            out.write(synthetic,"ML/ASTRO/TEST.PGM",
                      compress_type=zipfile.ZIP_DEFLATED)
        out.writestr("ML/ASTRO/CREDITS.TXT",
            "Astro plate-solving module (GPL-2.0-or-later)\n"
            "HYG v4.1 stellar data by David Nash / Astronexus (CC BY-SA 4.0).\n"
            "Subset converted from CSV to packed binary; celestial positions\n"
            "quantized to 0.0001 degree and visual magnitudes to 0.1 mag.\n"
            "Source: https://github.com/astronexus/HYG-Database\n"
            "License: https://creativecommons.org/licenses/by-sa/4.0/\n"
            f"Package generated {date.today()}\n",compress_type=zipfile.ZIP_DEFLATED)
        out.writestr("ML/ASTRO/READ_ME_FIRST.TXT",
            "EXPERIMENTAL. NOT TESTED ON PHYSICAL CAMERAS.\n"
            "Requires matching Canon 600D 1.0.2 or Canon 6D 1.1.6 firmware.\n"
            "Do not install on an unsupported camera. Use a spare SD card.\n"
            "Enable 'astro' in ML Modules and restart.\n"
            "Read-only: Trash > Midnight > Measure star now.\n"
            "Autofocus sends focus commands and has not been hardware tested.\n"
            "Shortcuts are disabled by default. Normal Trap Focus unchanged.\n"
            "Plate solving requires an approximate position and enough stars\n"
            "visible in LiveView. Long exposure RAW is NOT implemented.\n"
            "STARS.BIN and STARS.IDX must both be copied to ML/ASTRO.\n"
            "The solver shows separate RA and Dec fields in Astronomy settings.\n"
            "To demo without a sensor, use Solve test PGM and set the hint to\n"
            "RA 05:00 Dec +35 and FOV width 6.4 degrees. TEST.PGM is synthetic.\n",
            compress_type=zipfile.ZIP_DEFLATED)
    with zipfile.ZipFile(target) as z:
        assert z.testzip() is None
        for required in ("autoexec.bin","ML/modules/astro.mo",
                         "ML/ASTRO/STARS.BIN","ML/ASTRO/STARS.IDX"):
            assert required in z.namelist()
    return target

if __name__ == "__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("camera", choices=("600D.102","6D.116"))
    p.add_argument("catalog", type=pathlib.Path)
    a=p.parse_args()
    dest=package(a.camera,a.catalog)
    print(f"Built and verified {dest} ({dest.stat().st_size:,} bytes)")
