#!/usr/bin/env python3
"""Produce matching full Magic Lantern and module-only Midnight packages.

Full ZIP replaces autoexec.bin only when deliberately used for a fresh installation.
Module-only ZIP updates ASTRO.MO and star data on an existing compatible card.
"""
from __future__ import annotations

import argparse
from datetime import date
from hashlib import sha256
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = ("STARS.BIN", "STARS.IDX", "STARS.NAM", "SKY.DIR",
            "SKY.PAT", "SKY.FLD", "DSO.BIN", "DSO.KEY", "DSO.NAM", "CONST.BIN")
WIDE = ("SKYW.DIR", "SKYW.PAT", "SKYW.FLD")
CAMERAS = ("600D.102", "700D.115", "6D.116")

CREDITS = """Midnight astronomy module (GPL-2.0-only combined work).
Ported architecture by AstroML contributors from kubakubakuba/AstroML.
Midnight project: https://github.com/mistrjirka/midnight.
HYG v4.1: David Nash / Astronexus (CC BY-SA 4.0).
OpenNGC: Mattia Verga (CC BY-SA 4.0).
Barnard and Sharpless: CDS/VizieR VII/220A and VII/20.
Constellation figures: d3-celestial, Olaf Frohn (BSD-3-Clause).
"""

def add_data(out: ZipFile, data: Path, names: tuple[str,...]) -> None:
    for name in names:
        path=data/name
        if not path.is_file():
            raise RuntimeError(f"{name} missing: run the data build")
        out.write(path, f"ML/ASTRO/{name}",compress_type=ZIP_DEFLATED)

def package_all(data: Path, base: Path, module: Path, target: Path, camera: str) -> None:
    target.parent.mkdir(parents=True,exist_ok=True)
    module_bytes=module.read_bytes()
    with ZipFile(base) as previous,ZipFile(target,"w") as out:
        filenames=set(previous.namelist())
        assert "ML/modules/astro.mo" in filenames, "Base firmware must include astro.mo"
        assert "autoexec.bin" in filenames, "Base firmware must include autoexec.bin"
        for entry in previous.infolist():
            content=module_bytes if entry.filename=="ML/modules/astro.mo" else previous.read(entry.filename)
            out.writestr(entry,content)
        add_data(out,data,REQUIRED)
        out.writestr("ML/ASTRO/CREDITS.TXT",CREDITS+f"Build: {date.today()}.\n")
        out.writestr("ML/ASTRO/READ_ME_FIRST.TXT",
            f"EXPERIMENTAL Midnight for Canon {camera}. Installed as Magic Lantern module.\n"
            "Requires a matching camera firmware revision. Unmodified Canon bootloader.\n"
            "The full ZIP includes autoexec.bin: do not use it for a module-only update.\n"
            "The safer module-only ZIP preserves your existing Magic Lantern installation.\n"
            "Menu: Trash -> Focus -> Midnight. Gallery: Play -> Q -> SET.\n"
            "Autofocus lens motors and on-camera photo playback remain hardware-experimental.\n")
        if (ROOT/"tests/data/TEST.PGM").exists():
            out.write(ROOT/"tests/data/TEST.PGM","ML/ASTRO/TEST.PGM")
    with ZipFile(target) as z:
        assert z.testzip() is None
        assert z.read("ML/modules/astro.mo")==module_bytes
        for name in REQUIRED:assert f"ML/ASTRO/{name}" in z.namelist()

def package_module(data: Path,module: Path,target: Path,camera: str) -> None:
    target.parent.mkdir(parents=True,exist_ok=True)
    with ZipFile(target,"w") as z:
        z.write(module,"ML/modules/astro.mo",ZIP_DEFLATED)
        z.writestr("ML/SETTINGS/astro.en",b"")
        add_data(z,data,REQUIRED)
        z.writestr("ML/ASTRO/CREDITS.TXT",CREDITS)
        z.writestr("ML/ASTRO/READ_ME_FIRST.TXT",
          f"Midnight module-only update, Canon {camera}.\n"
          "Requires an existing compatible Magic Lantern installation.\n"
          "No firmware, boot flags, photos or existing settings are replaced.\n"
          "Menu: Trash -> Focus -> Midnight. Gallery: Play -> Q -> SET.\n")
        if (ROOT/"tests/data/TEST.PGM").exists():
            z.write(ROOT/"tests/data/TEST.PGM","ML/ASTRO/TEST.PGM")
    with ZipFile(target) as z:
        assert z.testzip() is None
        assert "autoexec.bin" not in z.namelist()
        assert z.read("ML/modules/astro.mo")==module.read_bytes()

def package_wide(data: Path,target: Path)->None:
    with ZipFile(target,"w") as z:
        add_data(z,data,WIDE)
        z.writestr("ML/ASTRO/WIDE.TXT","Optional wide-field sky index for lenses wider than 60 degrees.\n")
    with ZipFile(target) as z:assert z.testzip() is None

def main()->None:
    p=argparse.ArgumentParser()
    p.add_argument("--data",type=Path,default=ROOT/".astro-data")
    p.add_argument("--ml",type=Path,default=ROOT/".build/magiclantern")
    p.add_argument("--output",type=Path,default=ROOT/"dist")
    a=p.parse_args()
    module=a.ml/"modules/astro/build/astro.mo"
    assert module.is_file()
    for cam in CAMERAS:
        base=a.ml/f"platform/{cam}/build/magiclantern.zip"
        assert base.is_file()
        package_all(a.data,base,module,a.output/f"magiclantern-astro-{cam}.zip",cam)
        package_module(a.data,module,a.output/f"midnight-module-{cam}.zip",cam)
    package_wide(a.data,a.output/"magiclantern-astro-wide-index.zip")
    for path in sorted(a.output.glob("*.zip")):
        print(f"{path.name}: {path.stat().st_size:,} bytes sha256={sha256(path.read_bytes()).hexdigest()}")

if __name__=="__main__":main()
