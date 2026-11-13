"""`astroml test ...`: host test runners."""
from __future__ import annotations

import re
import subprocess
import sys

import typer

from . import paths

app = typer.Typer(help="Run host tests (C core built with gcc + UBSan).", no_args_is_help=True)

C_SOURCES = ("astro/**/*.c", "astro/**/*.h", "tests/harness/*.c")


def pytest(*args: str) -> None:
    result = subprocess.run([sys.executable, "-m", "pytest", *args], cwd=paths.REPO)
    if result.returncode:
        raise typer.Exit(result.returncode)


@app.command("all")
def run_all(fast: bool = typer.Option(False, help="Skip the slow statistical solver run.")):
    """Unit, regression and solver tests."""
    pytest("tests", "-q", *(["-m", "not slow"] if fast else []))


@app.command("regress")
def regress():
    """Port vs. original Midnight: byte-identical outputs on identical inputs."""
    pytest("tests/test_regress.py", "-q")


# Canon's vsnprintf prints the letter for %u, and ML code sticks to %d/%x/%s.
UNSUPPORTED_FORMAT = re.compile(r"%[-+ #0-9.]*(?:[lhz]|[ufc]\b)")


def unsupported_formats() -> list[str]:
    hits = []
    for path in sorted((paths.REPO / "astro").glob("*.c")):
        for number, line in enumerate(path.read_text().splitlines(), 1):
            if UNSUPPORTED_FORMAT.search(line):
                hits.append(f"{path.relative_to(paths.REPO)}:{number}: {line.strip()}")
    return hits


@app.command("format-check")
def format_check():
    """Fails if any C file differs from .clang-format or uses a printf code Canon lacks."""
    bad = unsupported_formats()
    if bad:
        typer.echo("printf codes Canon's vsnprintf does not support (use %d):\n" + "\n".join(bad))
        raise typer.Exit(1)
    files = sorted({str(p.relative_to(paths.REPO)) for pattern in C_SOURCES
                    for p in paths.REPO.glob(pattern)})
    result = subprocess.run(["clang-format", "--dry-run", "--Werror", *files], cwd=paths.REPO)
    if result.returncode:
        raise typer.Exit(result.returncode)
    typer.echo(f"{len(files)} files formatted correctly")
