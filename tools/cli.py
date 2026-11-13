"""`astroml`: single entry point for every AstroML host tool."""
from __future__ import annotations

import typer

from .data.cli import app as data_app
from .fixtures import app as fixtures_app
from .package import app as package_app
from .qemu import app as qemu_app
from .testing import app as test_app

app = typer.Typer(help="AstroML: Magic Lantern astro module tooling for the Canon 600D and 6D.",
                  no_args_is_help=True)
app.add_typer(data_app, name="data")
app.add_typer(qemu_app, name="qemu")
app.add_typer(test_app, name="test")
app.add_typer(package_app, name="package")
app.add_typer(fixtures_app, name="fixtures")

if __name__ == "__main__":
    app()
