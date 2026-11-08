# Midnight

Experimental astronomy extensions for [Magic Lantern](https://github.com/reticulatedpines/magiclantern_simplified), targeting **Canon 600D firmware 1.0.2** and **Canon 6D firmware 1.1.6**.

**Developer prototype. Not tested on a physical camera.** Use a spare bootable SD card and retain a known-working one.

## Features

- Guarded star autofocus with lens movement confirmation and fail-closed error handling.
- Manual-focus sharpness score in Live View, even without an autofocus lens.
- Hinted plate solver with a small SD-card star catalog.
- Field width estimated from Canon sensor size and electronic focal length. Manual override for non-electronic lenses.
- Synthetic PGM test action for emulator development.

The image source is 8-bit Live View, not a long-exposure RAW capture. Very faint stars may be missed. Bahtinov diffraction spike offset fitting is not yet implemented.

## Build

This repository is a **reproducible Magic Lantern source overlay**. The script downloads a pinned upstream revision, copies the astronomy module, applies the integration patch, runs tests and builds both cameras. Canon proprietary firmware is not distributed.

Linux dependencies: git, curl, Python 3, arm-none-eabi GCC/binutils, newlib headers, python3-docutils.

Run: bash scripts/build.sh

Result: dist/magiclantern-astro-600D.102.zip and dist/magiclantern-astro-6D.116.zip.

The Actions workflow publishes experimental builds as artifacts and attempts a prerelease.

Detailed safety notes and testing: [ASTRO_DEVELOPMENT.md](ASTRO_DEVELOPMENT.md).

## Licensing

Midnight and Magic Lantern source: GPL-2.0-or-later. Star catalog: HYG v4.1, David Nash / Astronexus, CC BY-SA 4.0.
