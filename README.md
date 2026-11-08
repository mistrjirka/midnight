# Midnight

Experimental astronomy extensions for [Magic Lantern](https://github.com/reticulatedpines/magiclantern_simplified), targeting Canon 600D firmware 1.0.2 and Canon 6D firmware 1.1.6.

This repository is a **reproducible source overlay** pinned to an upstream Magic Lantern commit. It does not contain Canon proprietary firmware. The build script downloads upstream Magic Lantern source, applies Midnight's module and integration patch, then compiles firmware packages.

Features include guarded star autofocus, manual-focus sharpness readout, and hinted star-field plate solving. **No physical camera testing has been performed.** Use only a spare SD card, and test the read-only focus measurement first.

Source code: `modules/astro/`. Build instructions: `scripts/build.sh`. Release artifacts, when available, are experimental and not endorsed by Canon or Magic Lantern.

Magic Lantern and Midnight code: GPL-2.0-or-later. Catalog data: HYG Database v4.1 (CC BY-SA 4.0, David Nash / Astronexus).
