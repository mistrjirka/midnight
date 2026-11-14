# Midnight

Astronomy tools for **Magic Lantern on nine Canon EOS cameras**: 100D, 550D, 600D, 650D, 700D, 1100D, 60D, 70D and 6D. Exact Canon firmware versions are in the [installation guide](docs/INSTALL.md).

[Experimental nine-camera release: v0.5.0-alpha.1](https://github.com/mistrjirka/midnight/releases/tag/v0.5.0-alpha.1) · [Installation and recovery guide](docs/INSTALL.md)

## Features

| Feature | Function |
|---|---|
| Plate solving | Identifies sky coordinates without pointing hints. Optional focal-length hints speed up solving |
| Target guiding | Finds M, NGC, IC, Caldwell, Barnard and Sharpless objects. Draws an arrow or outline |
| Polar alignment | Uses two sky solutions about the RA axis, then guides mount adjustment |
| Sky annotations | Star names, constellations, and oriented deep-sky outlines |
| Manual star focus | Size score at 5x/10x Live View |
| Autofocus | Experimental electronic lens adjustments, not hardware validated |
| Live View gain | Photo-only preview gain, Off to +6 EV, without changing saved photos |
| Timed shooting | Original Magic Lantern intervalometer and Bulb Timer links |

Open **Trash/Delete → Focus → Midnight**.

## 600D emulation

![Canon 600D QEMU: 11 matched stars, RA/Dec, sky outlines and guiding arrow](docs/images/midnight-modular-600d-qemu.png)

The screenshot is from **Canon 600D QEMU**, not a physical exposure.
Its bundled synthetic sky solved with **11 matches in 0.19 s of emulator-reported solving time**.
[Testing details and limits](docs/MODULAR_MIGRATION.md).

## Installation

**Recommended:** update an existing compatible Magic Lantern card using the module-only ZIP. Back up the card, then extract the ZIP to the card root. Do not overwrite `autoexec.bin` when updating an existing Magic Lantern installation.

See the [installation guide](docs/INSTALL.md), [new-camera test guide](docs/EASY_CAMERA_PORTS.md) and model-specific [550D](docs/550D_PORT.md) / [700D](docs/700D_PORT.md) notes. New ports need physical-camera validation.

## Source

The modular architecture is derived from
[kubakubakuba/AstroML](https://github.com/kubakubakuba/AstroML).
Portable algorithms live in modules/astro/core, camera integration in
modules/astro, and host tests in tests. Original Midnight code is retained in
tests/reference_midnight for regression comparisons.

Build and run tests:

    PYTHON=python3 RUN_SLOW_TESTS=1 bash scripts/build.sh

[Migration status](docs/MODULAR_MIGRATION.md) · [C code review](docs/C_CODE_REVIEW.md)

**Safety:** The newly added camera ports are compile-tested, not hardware-tested. Gallery solving, Live View, available RAM and lens motors require on-camera checks. Back up the card. Use module-only updates on existing compatible Magic Lantern installations.
