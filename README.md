# Midnight

Experimental astronomy extensions for [Magic Lantern](https://github.com/reticulatedpines/magiclantern_simplified), targeting **Canon 600D firmware 1.0.2** and **Canon 6D firmware 1.1.6**.

**Developer prototype. Not tested on a physical camera.** Use a spare bootable SD card and retain a known-working one.

## Install prebuilt firmware

**Requirements:** Canon **600D with firmware 1.0.2**, or Canon **6D with firmware 1.1.6**, an SD-card reader, and a charged battery. Check the camera firmware in Canon's **MENU → Firmware Ver.** Do not install a package for a different camera or firmware version.

1. **Back up your SD card** and use a **spare card** for Midnight. Keep your existing working Magic Lantern card unchanged.
2. Download the matching ZIP from the [Midnight releases](https://github.com/mistrjirka/midnight/releases/tag/v0.1.0-alpha.1). Select `magiclantern-astro-600D.102.zip` or `magiclantern-astro-6D.116.zip`.
3. **If the spare SD card already boots Magic Lantern:** Copy the **entire contents** of the Midnight ZIP to the **root** of that card. Merge/replace its existing Magic Lantern files, including `autoexec.bin`, `ML/`, and the matching module symbol file. Do **not** rerun the firmware installer on an already working card.
4. **If the card does not yet boot Magic Lantern:** Format the spare card **in the camera**, then copy the ZIP contents to the card root. With the card inserted and the camera in **M** mode, use **MENU → Firmware Ver. → Firmware Update** to run the included `ML-SETUP.FIR` installer. Follow its on-screen instructions, wait for confirmation, and restart the camera. This prepares the card for Magic Lantern. It is **not a Canon firmware upgrade**. Do not continue if the camera model or firmware version does not match.
5. Eject the SD card safely, insert it, and start the camera. Open Magic Lantern with the **Delete/Trash** button.
6. Open **Modules**, enable `astro`, and **restart** the camera. The new tools appear under **Focus → Astro tools**.

The SD-card root should contain these files and folders (alongside any existing `DCIM/` and `MISC/`):

```text
SD card/
├── autoexec.bin
├── ML-SETUP.FIR
└── ML/
    ├── modules/astro.mo
    ├── modules/600D_102.sym  (600D only; 6D uses 6D_116.sym)
    └── ASTRO/
        ├── STARS.BIN
        └── TEST.PGM
```

### First safe tests

1. In **Live View**, open **Focus → Astro tools → Measure star only**. This reads the sharpness of a visible star **without moving the lens**.
2. For continuous manual focusing, enable **Manual focus readout** in the same submenu. The score updates about once per second. **Lower is sharper** when comparing the *same star* at the *same zoom*.
3. To test plate solving without a sensor image, choose **Solve test PGM**. Use pointing hint **RA 05:00, Dec +35°** and frame width **6.4°**. The test frame and star catalog are already in the ZIP.
4. For real **Solve LiveView**, use **1× Live View** and enter an approximate sky position. Field width defaults to the electronically reported lens focal length; set it manually if the lens does not report one. This is **hinted**, not blind, plate solving.

**Leave autofocus shortcuts off and avoid "Auto focus star" until the motor behavior is validated on your specific lens.** The software builds and simulated fault tests pass, but no physical camera has been tested. The solver uses Live View rather than a long-exposure RAW frame, so faint fields may not solve.

### If the camera does not boot

Turn the camera off, remove the SD card, and try booting without it. If the camera is unresponsive, remove and reinsert the battery with the card removed. Restore your known-working card if needed. A bootable Magic Lantern card **without a matching `autoexec.bin` can cause a startup hang**. Do not delete that file from a bootable card.

See the [Magic Lantern installation and recovery notes](https://magiclantern.fandom.com/wiki/Unified/Install) and [Midnight development notes](ASTRO_DEVELOPMENT.md) for further limitations.

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
