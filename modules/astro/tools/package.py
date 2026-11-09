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
    module=ROOT / "modules" / "astro" / "build" / "astro.mo"
    assert module.is_file(), "Build modules/astro before packaging"
    module_bytes=module.read_bytes()
    target.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(base) as orig, zipfile.ZipFile(target,"w") as out:
        assert "ML/modules/astro.mo" in orig.namelist(), "Astro module missing"
        for info in orig.infolist():
            payload=(module_bytes if info.filename=="ML/modules/astro.mo"
                     else orig.read(info.filename))
            out.writestr(info,payload)
        out.write(catalog,"ML/ASTRO/STARS.BIN",compress_type=zipfile.ZIP_DEFLATED)
        index=catalog.with_suffix(".IDX")
        assert index.is_file(), "Generate STARS.IDX with make_catalog.py"
        out.write(index,"ML/ASTRO/STARS.IDX",compress_type=zipfile.ZIP_DEFLATED)
        labels=catalog.with_suffix(".NAM")
        assert labels.is_file(), "Generate STARS.NAM with make_catalog.py"
        out.write(labels,"ML/ASTRO/STARS.NAM",compress_type=zipfile.ZIP_DEFLATED)
        # The current solver uses the shared-field all-sky index, not the
        # retired BLQ/BIX/BKX voting database. No user photographs are packed.
        for name in ("SKY.DIR","SKY.PAT","SKY.FLD"):
            data=catalog.parent/name
            assert data.is_file(), "Generate the all-sky index with make_sky_index.py"
            out.write(data,"ML/ASTRO/"+name,compress_type=zipfile.ZIP_DEFLATED)
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
            "Focus shortcut defaults ON: 600D DISP, 6D LCD-light.\n"
            "In manual focus it toggles the star focus score.\n"
            "Set Astronomy settings > Focus shortcut > Off to restore button.\n"
            "Electronic AF may move the lens. Not yet camera-validated.\n"
            "Trap Focus unchanged.\n"
            "Experimental blind solving needs no pointing or lens input.\n"
            "Requires >=8 verified stars. Long RAW exposure not implemented.\n"
            "Copy every ML/ASTRO file, including SKY.DIR, SKY.PAT, SKY.FLD and STARS.NAM.\n"
            "600D QEMU: both original photo-derived frames passed blind solving.\n"
            "A successful Live View or image-file solve opens the annotated result automatically.\n"
            "Q returns from the frozen result. Native album decoding remains unverified.\n"
            "6D is compile-tested only. No physical-camera speed or safety validation is claimed.\n",
            compress_type=zipfile.ZIP_DEFLATED)
    with zipfile.ZipFile(target) as z:
        assert z.testzip() is None
        assert z.read("ML/modules/astro.mo")==module_bytes, "Stale astro.mo in ZIP"
        for required in ("autoexec.bin","ML/modules/astro.mo",
                         "ML/ASTRO/STARS.BIN","ML/ASTRO/STARS.IDX","ML/ASTRO/STARS.NAM",
                         "ML/ASTRO/SKY.DIR","ML/ASTRO/SKY.PAT","ML/ASTRO/SKY.FLD"):
            assert required in z.namelist()
    return target

if __name__ == "__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("camera", choices=("600D.102","6D.116"))
    p.add_argument("catalog", type=pathlib.Path)
    a=p.parse_args()
    dest=package(a.camera,a.catalog)
    print(f"Built and verified {dest} ({dest.stat().st_size:,} bytes)")
