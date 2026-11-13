"""`astroml package`: module-only card ZIP (ASTRO.MO + ML/ASTRO data). No autoexec.bin."""
from __future__ import annotations

import hashlib
import pathlib
import zipfile
from datetime import date

import typer

from . import paths

app = typer.Typer(help="Build installable ZIPs for the card root.", no_args_is_help=True)

REQUIRED_DATA = ("STARS.BIN", "STARS.IDX", "STARS.NAM", "SKY.DIR", "SKY.PAT", "SKY.FLD",
                 "DSO.BIN", "DSO.KEY", "DSO.NAM", "CONST.BIN")
WIDE_DATA = ("SKYW.DIR", "SKYW.PAT", "SKYW.FLD")

CREDITS = f"""AstroML module for Magic Lantern (GPL-2.0-only).
Derived from Midnight (https://github.com/mistrjirka/midnight).

STARS.*, SKY.*: HYG v4.1 by David Nash / Astronexus (CC BY-SA 4.0),
https://github.com/astronexus/HYG-Database. Positions quantised to 0.0001 degree.
DSO.*: OpenNGC by Mattia Verga (CC BY-SA 4.0), https://github.com/mattiaverga/OpenNGC;
Barnard dark objects (CDS VII/220A) and Sharpless HII regions (CDS VII/20) via VizieR.
CONST.BIN: d3-celestial by Olaf Frohn (BSD-3-Clause).
Package generated {date.today()}.
"""

README = """Midnight module for Canon 600D firmware 1.0.2 or 6D firmware 1.1.6.
Magic Lantern core pinned at 142472e95054d73640f50789ac0fd66325499d82.

Install: extract this ZIP at the card root of a card that already boots Magic Lantern.
Nothing else is replaced: autoexec.bin and your ML settings stay as they are.
ML/SETTINGS/ASTRO.EN enables the module. Delete it to disable Midnight.

Open Focus -> Midnight in the ML menu. Gallery: Play -> Q -> SET.
Auto focus sends lens commands. It is not hardware validated yet.
"""


def write_files(out: zipfile.ZipFile, data: pathlib.Path, names) -> None:
    for name in names:
        source = data / name
        if not source.is_file():
            raise typer.BadParameter(f"{source} missing: run `astroml data all`")
        out.write(source, f"ML/ASTRO/{name}", compress_type=zipfile.ZIP_DEFLATED)


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


@app.command("module")
def package_module(module: pathlib.Path = typer.Option(paths.MODULE_BUILD, help="Built astro.mo."),
                   data: pathlib.Path = typer.Option(paths.DATA, help="Generated data files."),
                   dest: pathlib.Path = typer.Option(paths.DIST / f"astroml-{paths.CAMERA}.zip"),
                   test_files: bool = typer.Option(False, help="Include TEST*.PGM if present.")):
    """ASTRO.MO, enable flag and all ML/ASTRO data for the matching 600D or 6D."""
    if not module.is_file():
        raise typer.BadParameter(f"{module} missing: run scripts/build.sh")
    dest.parent.mkdir(parents=True, exist_ok=True)
    module_bytes = module.read_bytes()
    with zipfile.ZipFile(dest, "w") as out:
        out.writestr("ML/MODULES/ASTRO.MO", module_bytes, compress_type=zipfile.ZIP_DEFLATED)
        out.writestr("ML/SETTINGS/ASTRO.EN", b"")
        write_files(out, data, REQUIRED_DATA)
        if test_files:
            write_files(out, data, sorted(p.name for p in data.glob("TEST*.PGM")))
        out.writestr("ML/ASTRO/CREDITS.TXT", CREDITS, compress_type=zipfile.ZIP_DEFLATED)
        out.writestr("ML/ASTRO/README.TXT", README, compress_type=zipfile.ZIP_DEFLATED)
    with zipfile.ZipFile(dest) as z:
        assert z.testzip() is None
        assert z.read("ML/MODULES/ASTRO.MO") == module_bytes, "stale ASTRO.MO in ZIP"
        assert "autoexec.bin" not in z.namelist()
    typer.echo(f"{dest} ({dest.stat().st_size:,} bytes) sha256 {sha256(dest)}")


@app.command("wide")
def package_wide(data: pathlib.Path = typer.Option(paths.DATA),
                 dest: pathlib.Path = typer.Option(paths.DIST / "astroml-wide-index.zip")):
    """Optional SKYW.* wide-lens index (fields over ~60 degrees)."""
    dest.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(dest, "w") as out:
        write_files(out, data, WIDE_DATA)
        out.writestr("ML/ASTRO/WIDE.TXT",
                     "Optional wide-lens sky index (19 and 27 degree fields). Extract at the\n"
                     "card root next to AstroML. Used only when SKY.* finds no match.\n",
                     compress_type=zipfile.ZIP_DEFLATED)
    typer.echo(f"{dest} ({dest.stat().st_size:,} bytes) sha256 {sha256(dest)}")
