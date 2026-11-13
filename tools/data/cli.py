"""`astroml data ...`: download catalogues and generate the ML/ASTRO card files."""
from __future__ import annotations

import enum
import hashlib
import pathlib
import subprocess
import sys
import tempfile

import typer

from .. import paths
from . import catalog, constellations, dso, sky_index, sources

app = typer.Typer(help="Download source catalogues and build the ML/ASTRO data files.",
                  no_args_is_help=True)

DataDir = typer.Option(paths.DATA, "--data", "-d", help="Directory for sources and outputs.")


class Layout(str, enum.Enum):
    polar = "polar"
    midnight = "midnight"


@app.command()
def fetch(data: pathlib.Path = DataDir,
          force: bool = typer.Option(False, help="Download again even if present.")):
    """Download HYG, OpenNGC and d3-celestial at pinned versions."""
    fetched = sources.fetch(data, force)
    typer.echo(f"Fetched {len(fetched)} file(s) into {data}" if fetched else "Sources up to date")


@app.command("catalog")
def build_catalog(data: pathlib.Path = DataDir,
                  limit: float = typer.Option(9.5, help="Faintest magnitude kept.")):
    """STARS.BIN/.IDX/.NAM from the HYG catalogue."""
    count = catalog.build(data / "hygdata_v41.csv", data / "STARS.BIN", limit)
    typer.echo(f"STARS.BIN: {count:,} stars")


@app.command("dso")
def build_dso(data: pathlib.Path = DataDir,
              barnard: pathlib.Path = typer.Option(paths.SOURCES / "barnard_VII_220A.dat"),
              sharpless: pathlib.Path = typer.Option(paths.SOURCES / "sharpless_VII_20.dat")):
    """DSO.BIN/.KEY/.NAM from OpenNGC, Barnard and Sharpless."""
    counts = dso.build(data, barnard, sharpless, data / "DSO.BIN")
    objects = counts.pop("objects")
    typer.echo(f"DSO.BIN: {objects:,} objects; " +
               ", ".join(f"{k} {v}" for k, v in counts.items()))


@app.command("constellations")
def build_constellations(data: pathlib.Path = DataDir):
    """CONST.BIN from d3-celestial figures and names."""
    nseg, nlab = constellations.build(data / "constellations.lines.json",
                                      data / "constellations.json", data / "CONST.BIN")
    typer.echo(f"CONST.BIN: {nseg} segments, {nlab} labels")


@app.command("sky-index")
def build_sky_index(data: pathlib.Path = DataDir,
                    wide: bool = typer.Option(False, help="Build the optional SKYW.* index."),
                    layout: Layout = typer.Option(Layout.polar,
                                                  help="polar: covers both celestial poles; "
                                                       "midnight: original layout."),
                    jobs: int = typer.Option(0, help="Worker processes (0: all CPUs).")):
    """SKY.* (or SKYW.*) blind-solver index from STARS.BIN."""
    sky_index.build(data / "STARS.BIN", data, "SKYW" if wide else "SKY", wide, layout.value,
                    jobs or None, typer.echo)


@app.command("all")
def build_all(data: pathlib.Path = DataDir,
              wide: bool = typer.Option(False, help="Also build the SKYW.* wide index.")):
    """fetch + catalog + dso + constellations + sky-index."""
    fetch(data, force=False)
    build_catalog(data, 9.5)
    build_dso(data, paths.SOURCES / "barnard_VII_220A.dat",
              paths.SOURCES / "sharpless_VII_20.dat")
    build_constellations(data)
    build_sky_index(data, False, Layout.polar, 0)
    if wide:
        build_sky_index(data, True, Layout.polar, 0)


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        while chunk := f.read(1 << 20):
            digest.update(chunk)
    return digest.hexdigest()


GENERATED = ("STARS.BIN", "STARS.IDX", "STARS.NAM", "DSO.BIN", "DSO.KEY", "DSO.NAM",
             "CONST.BIN", "SKY.DIR", "SKY.PAT", "SKY.FLD")


@app.command("reference")
def reference():
    """Create Midnight-layout sky data for comparisons with the pre-migration solver."""
    import shutil

    data = paths.DATA
    reference_dir = paths.MIDNIGHT_DATA
    fetch(data, force=False)
    if not (data / "STARS.BIN").exists():
        build_catalog(data, 9.5)
    if not (data / "DSO.BIN").exists():
        build_dso(data, paths.SOURCES / "barnard_VII_220A.dat",
                  paths.SOURCES / "sharpless_VII_20.dat")
    if not (data / "CONST.BIN").exists():
        build_constellations(data)
    reference_dir.mkdir(parents=True, exist_ok=True)
    for name in GENERATED:
        if name.startswith("SKY."):
            continue
        shutil.copy2(data / name, reference_dir / name)
    sky_index.build(reference_dir / "STARS.BIN", reference_dir, "SKY", False,
                    "midnight", 8, typer.echo)
    typer.echo(f"Regression reference created under {reference_dir}")


@app.command("verify-midnight")
def verify_midnight():
    """Rebuild everything with --layout midnight and compare SHA-256 with the original tools."""
    missing = [n for n in GENERATED if not (paths.MIDNIGHT_DATA / n).exists()]
    if missing:
        typer.echo(f"Reference data missing ({', '.join(missing)}): run `astroml data reference`")
        raise typer.Exit(2)
    with tempfile.TemporaryDirectory(prefix="astroml-verify-") as tmp:
        out = pathlib.Path(tmp)
        for name in sources.DOWNLOADS:
            (out / name).symlink_to(paths.MIDNIGHT_DATA / name)
        build_catalog(out, 9.5)
        build_dso(out, paths.SOURCES / "barnard_VII_220A.dat",
                  paths.SOURCES / "sharpless_VII_20.dat")
        build_constellations(out)
        build_sky_index(out, False, Layout.midnight, 0)
        failures = 0
        for name in GENERATED:
            same = sha256(out / name) == sha256(paths.MIDNIGHT_DATA / name)
            failures += not same
            typer.echo(f"{'same     ' if same else 'DIFFERENT'} {name}")
    if failures:
        raise typer.Exit(1)
