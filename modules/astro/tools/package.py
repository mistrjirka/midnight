#!/usr/bin/env python3
"""Combine a matching Magic Lantern build with the licensed HYG star catalog."""
import argparse
import pathlib
import zipfile
from datetime import date

ROOT = pathlib.Path(__file__).resolve().parents[3]
OVERLAY_FILES=("DSO.BIN","DSO.KEY","DSO.NAM","CONST.BIN")

def package_wide(catalog: pathlib.Path) -> pathlib.Path:
    """Optional wide-lens index (SKYW.*), a separate download for >60 deg fields."""
    target=ROOT / "dist" / "magiclantern-astro-wide-index.zip"
    target.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(target,"w") as out:
        for name in ("SKYW.DIR","SKYW.PAT","SKYW.FLD"):
            data=catalog.parent/name
            assert data.is_file(), "Generate it with make_sky_index.py --wide"
            out.write(data,"ML/ASTRO/"+name,compress_type=zipfile.ZIP_DEFLATED)
        out.writestr("ML/ASTRO/WIDE_INDEX.TXT",
            "Optional Midnight wide-lens sky index (19 and 27 degree fields).\n"
            "Extract at the card root next to an installed Midnight package.\n"
            "Used only when the normal index finds no match, e.g. 14-20 mm on full\n"
            "frame. Set the lens focal length for best results. HYG v4.1 data by\n"
            "David Nash / Astronexus (CC BY-SA 4.0).\n",compress_type=zipfile.ZIP_DEFLATED)
    with zipfile.ZipFile(target) as z:
        assert z.testzip() is None
    return target
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
        # Target picker and sky overlays (make_dso.py, make_constellations.py).
        for name in OVERLAY_FILES:
            data=catalog.parent/name
            assert data.is_file(), f"Generate {name} with make_dso.py / make_constellations.py"
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
            "\n"
            "DSO.BIN/KEY/NAM: OpenNGC by Mattia Verga (CC BY-SA 4.0),\n"
            "https://github.com/mattiaverga/OpenNGC; Barnard dark objects (CDS VII/220A,\n"
            "Barnard 1927) and Sharpless HII regions (CDS VII/20, Sharpless 1959),\n"
            "via VizieR, CDS, Strasbourg.\n"
            "CONST.BIN: constellation figures from d3-celestial by Olaf Frohn\n"
            "(BSD-3-Clause), https://github.com/ofrohn/d3-celestial\n"
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
            "DSO.BIN/KEY/NAM and CONST.BIN feed Target & guide and the sky overlays.\n"
            "Lenses wider than ~60 degrees: add the optional wide index ZIP (SKYW.*).\n"
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
                         "ML/ASTRO/SKY.DIR","ML/ASTRO/SKY.PAT","ML/ASTRO/SKY.FLD",
                         *("ML/ASTRO/"+n for n in OVERLAY_FILES)):
            assert required in z.namelist()
    return target

if __name__ == "__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("camera", nargs="?", choices=("600D.102","6D.116"))
    p.add_argument("catalog", type=pathlib.Path)
    p.add_argument("--wide-index", action="store_true", help="build only the optional SKYW.* ZIP")
    a=p.parse_args()
    if not a.wide_index and not a.camera:
        p.error("camera is required unless --wide-index is given")
    dest=package_wide(a.catalog) if a.wide_index else package(a.camera,a.catalog)
    print(f"Built and verified {dest} ({dest.stat().st_size:,} bytes)")
