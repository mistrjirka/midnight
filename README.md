# Midnight

An astronomy-first interface for [Magic Lantern](https://github.com/reticulatedpines/magiclantern_simplified) on **Canon 600D (1.0.2)** and **Canon 6D (1.1.6)**.

**Experimental:** Not tested on physical cameras. Use a spare SD card and keep your existing working Magic Lantern card.

## Install

1. Check your camera version under **Canon MENU → Firmware Ver.**
2. Download the matching ZIP from the [Midnight release](https://github.com/mistrjirka/midnight/releases/tag/v0.2.4-alpha.1).
3. **Already have Magic Lantern?** Extract the **entire ZIP** onto a spare, bootable Magic Lantern SD card. Replace the existing Magic Lantern files. Do not rerun the installer.
4. **Fresh card?** Format it in the camera, extract the ZIP at the SD-card root, then use **MENU → Firmware Ver. → Firmware Update** in **M mode** to run **ML-SETUP.FIR**. Follow its instructions and restart. This installs the Magic Lantern boot setup, not a Canon firmware update.
5. Press **Trash/Delete** to open Magic Lantern, enable **astro** in **Modules**, and restart.

**After enabling astro, Trash opens Midnight first.** The original Magic Lantern menus are still available by pressing **Q/Left** or choosing **Full Magic Lantern**. Shutter, playback and essential Canon buttons remain unchanged.

## Midnight home menu

<img src="docs/images/astro-tools-menu.png" alt="Mockup of Midnight dashboard with intervalometer, bulb timer, focus and plate solving" width="600">

*The interface pictures in this README are labeled illustrations. Actual 600D QEMU screenshots have verified that the Midnight menu opens and the bundled plate-solving test completes.*

| Option | What it does |
|---|---|
| **Intervalometer settings** | Opens Magic Lantern's existing interval, start trigger and shot-count controls |
| **Bulb Timer settings** | Opens its long-exposure duration and display controls |
| **Manual star focus** | Toggles a live sharpness score. Works with manual lenses |
| **Measure star now** | Measures focus once without moving the lens |
| **Auto focus (no mask)** | Tries small electronic focus steps on a bright star |
| **Solve LiveView (3s)** | Waits 3 seconds, then reads a Live View image for sky solving |
| **Astronomy settings** | Advanced focus settings, pointing hint, lens FOV and test image |
| **Full Magic Lantern** | Returns to the normal shooting menus |

### Manual focusing

1. Center a bright star and open **5×/10× Live View**.
2. Turn **Manual star focus** on, then leave the menu.
3. Adjust focus for the **lowest Star focus value**. It updates about once per second. Compare the *same star* at the *same zoom*.

<img src="docs/images/manual-focus-preview.png" alt="Illustration of a star and the on-screen focus score" width="600">

**Bahtinov mask:** You can place the mask over the front of the lens and visually center the middle diffraction spike between the two diagonal spikes. Remove the mask before imaging. **Midnight does not yet measure spike alignment.** Its numeric focus score measures star size without a mask.

### Automatic focusing

Center a bright star at **5×/10×**, set the lens to **AF**, disable continuous/servo AF, then select **Auto focus (no mask)**. The routine attempts small movements and stops if communication or position confirmation fails. Start with **Motor step size = 1** and **Focus radius = 3** under **Astronomy settings**.

**Focus shortcut is ON by default:** DISP on the 600D, or the LCD-light button on the 6D. Press it in **photo Live View** to run autofocus with an electronic AF lens. With a manual-focus lens it toggles the **Star focus** readout instead. It does not override the button in menus, playback or movie mode.

To restore the original DISP/LCD-light behavior, open **Astronomy settings → Focus shortcut → Off**. You can instead select INFO or double half-press, but these may conflict with other controls.

**Physical camera testing remains necessary.** Autofocus motor behavior has not been verified on a real lens; pressing the enabled shortcut can move an electronic AF lens.

### Solve a saved photograph (main workflow)

1. Press **Play** and open a still photo at normal full-screen zoom.
2. Press **Q**. Midnight asks **Solve this photo?**.
3. Press **SET** to start. Press **Q** to cancel.
4. On success, Midnight shows the result **over the displayed photograph**, including confirmed star names.

The result uses **degrees**, not hour/minute notation:

| Result | Meaning |
|---|---|
| **RA: 80.12°** | Right ascension, similar to sky longitude |
| **Dec: +33.87°** | Declination, similar to sky latitude |
| **16 stars matched** | Number of independently verified catalog stars |

The overlay draws boxes around up to 12 confirmed stars and shows up to 8 proper names, Bayer/Flamsteed designations, or HIP catalog IDs. Press **Q** to hide it. The original photo is **not modified**. Midnight currently analyzes the camera's **displayed YUV preview**, not the original full-resolution RAW file.

**Current limitation:** A rough sky RA/Dec hint is still needed in **Astronomy settings**. The all-sky blind solver is not yet reliable enough to ship. The gallery can now search possible frame widths automatically, so manual lenses do **not** require a FOV entry. Lens metadata from a different, currently attached lens is ignored during gallery solving.

**Gallery UI status:** The module compiles, and names/coordinate mapping pass native tests. The photo-overlay workflow still needs physical camera verification. Canon's QEMU emulator did not recognize the JPG copied onto its virtual card, so the playback UI could not be tested end to end.

### Solve from Live View

Open **1× Live View** and choose **Solve LiveView (3s)**. Midnight waits **3 seconds after you press the button** to reduce vibration, then reads a single Live View image. The three-second wait is **not an exposure**, and this mode does not trigger a shutter release.

### Plate-solving tests

Canon 600D QEMU successfully solved processed grayscale frames derived from two real Canon photographs using their approximate pointing hints:

| Image | Matched stars | RA (degrees) | Dec (degrees) |
|---|---:|---:|---:|
| IMG_7975 | 16 | 80.12° | +33.87° |
| IMG_6446, with a satellite/aircraft trail | 8 | 327.95° | +47.52° |

Both are close to independent Astrometry.net solutions. Those test frames were manually prepared, not captured or decoded automatically by Midnight.

The firmware ZIP contains **STARS.BIN** (catalog), **STARS.IDX** (fast sky-region lookup), and **STARS.NAM** (star names). No user photographs or proprietary ROM dumps are included.

[Blind-solving research](docs/BLIND_PLATE_SOLVING.md).

## Troubleshooting

If the camera does not start, turn it off, remove the SD card and try booting without it. If necessary, remove and reinsert the battery without the card. Restore your known-working card. **Do not delete autoexec.bin from a bootable Magic Lantern card.**

[Development details](ASTRO_DEVELOPMENT.md) · [Build from source](scripts/build.sh)

Midnight and Magic Lantern: GPL-2.0-or-later. HYG star catalog: David Nash / Astronexus, CC BY-SA 4.0.
