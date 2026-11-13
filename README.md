# Midnight

Astronomy tools for **Magic Lantern on Canon 600D (1.0.2) and 6D (1.1.6)**.

[Current experimental release: v0.3.1-alpha.1](https://github.com/mistrjirka/midnight/releases/tag/v0.3.1-alpha.1)

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

**Already using compatible Magic Lantern:** use the matching module-only package
from a validated release. It replaces only the Midnight module and star data,
not camera boot flags, autoexec.bin, photos, or existing ML settings.

**New installation on a spare SD card:** use the full Magic Lantern firmware
ZIP matching your camera firmware. Follow the usual Magic Lantern boot-card
procedure. Never cross-install the 600D and 6D full firmware files.

Enable the astro module in the Modules tab and restart.

- **Gallery:** Play → Q → SET to solve the displayed photo preview.
- **Live View:** Activate 1x photo Live View before selecting Solve Live View (3s).
- **Focus:** Use the same star at 5x/10x and minimize its size score.
- **Lens hint:** Under Lens, select From lens or Manual.

## Source

The modular architecture is derived from
[kubakubakuba/AstroML](https://github.com/kubakubakuba/AstroML).
Portable algorithms live in modules/astro/core, camera integration in
modules/astro, and host tests in tests. Original Midnight code is retained in
tests/reference_midnight for regression comparisons.

Build and run tests:

    PYTHON=python3 RUN_SLOW_TESTS=1 bash scripts/build.sh

[Migration status](docs/MODULAR_MIGRATION.md)

**Safety:** The modular branch is not yet a camera-validated release.
Physical gallery solving, sensor Live View, polar alignment and lens motors
must pass on-camera tests before it replaces the working firmware.
