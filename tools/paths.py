"""Repository locations shared by all commands. Every default path comes from here."""
from __future__ import annotations

import os
import pathlib

REPO = pathlib.Path(__file__).resolve().parents[1]
DATA = pathlib.Path(os.environ.get("ASTRO_DATA", REPO / ".astro-data"))
BUILD = REPO / ".build"
DIST = REPO / "dist"
ROMS = REPO / "roms"
MODULE_SOURCE = REPO / "modules" / "astro"
SOURCES = REPO / "tools" / "data" / "sources"

# The ML tree the user's camera runs: magiclantern_simplified dev build 2025-06-20.
ML_COMMIT = "142472e95054d73640f50789ac0fd66325499d82"
ML_REPO_URL = "https://github.com/reticulatedpines/magiclantern_simplified.git"
ML_TREE = BUILD / "magiclantern"
QEMU_REPO_URL = "https://github.com/reticulatedpines/qemu-eos.git"
QEMU_BRANCH = "qemu-eos-v4.2.1"
QEMU_SOURCE = BUILD / "qemu-eos"
QEMU_BUILD = BUILD / "qemu-eos-build"

CAMERA = "600D.102"
MODULE_BUILD = ML_TREE / "modules" / "astro" / "build" / "astro.mo"

# Original Midnight, kept read-only as a regression oracle.
MIDNIGHT = REPO / "tests" / "reference_midnight"
MIDNIGHT_TOOLS = MIDNIGHT / "modules" / "astro" / "tools"
MIDNIGHT_DATA = DATA / "reference"
