# Midnight

Star focusing and plate solving for [Magic Lantern](https://github.com/reticulatedpines/magiclantern_simplified) on **Canon 600D (firmware 1.0.2)** and **Canon 6D (firmware 1.1.6)**.

> **Experimental:** Neither camera has been hardware-tested. Use a spare SD card and keep your working Magic Lantern card unchanged. Start with the read-only focusing tools.

## Install

1. On the camera, check **Menu → Firmware Ver.** Only the versions above are supported.
2. Back up your card. Download the matching ZIP from the [Midnight release](https://github.com/mistrjirka/midnight/releases/tag/v0.1.0-alpha.1).
3. **Already using Magic Lantern?** Extract the **entire ZIP** to the root of a **spare Magic Lantern-bootable SD card**, replacing the existing Magic Lantern files. Do not run the installer again.
4. **Starting with a new card?** Format the spare SD card in the camera, extract the ZIP to its root, then run **Menu → Firmware Ver. → Firmware Update** in **M mode** to start the included **ML-SETUP.FIR**. Follow the installer instructions and restart. This is Magic Lantern setup, not a Canon firmware upgrade.
5. Open Magic Lantern with the **Delete/Trash** button. Under **Modules**, enable **astro**, then restart the camera.

The ZIP already contains **astro.mo**, the matching camera symbol file, the star catalog (**ML/ASTRO/STARS.BIN**), and a test image. Do not copy just the module into an unrelated Magic Lantern build.

## Use the new features

Open **Magic Lantern → Focus → Astro tools**.

<img src="docs/images/astro-tools-menu.png" alt="Illustrated Astro tools menu showing the feature names" width="600">

*The interface images below are mockups based on the implemented UI. They are not screenshots from a physical camera.*

### Manual star focusing (works without autofocus)

1. Enable **Live View** and zoom to **5× or 10×** on a bright star.
2. Enable **Manual focus readout** in Astro tools.
3. Turn the focus ring until the on-screen **Star focus** value is as low as possible.

The value refreshes about once per second. Compare it only for the **same star and zoom level**. If you see **No star**, choose a brighter star or improve its visibility. For a one-time reading without the overlay, select **Measure star only**. Neither option moves the lens motor.

<img src="docs/images/manual-focus-preview.png" alt="Illustrative Live View with a star sharpness score" width="600">

### Automatic star focusing

1. Center a bright star in **5×/10× Live View**.
2. Set the lens switch to **AF** and turn off continuous/servo AF.
3. Select **Auto focus star**. The camera makes small electronic focus steps, measures the same star, then reports a result.

The routine stops when it cannot confirm lens movement or loses the star. **Motor behavior is still untested on physical cameras**, so use this only after confirming safe operation with your lens. Start with **Motor step size = 1** and **Focus radius = 3**.

**Focus shortcut** defaults to **Off**. You can optionally choose **INFO button** or **Double half-press**. Avoid conflicts with Trap Focus or other button assignments.

### Plate solving (find where the camera points)

1. Use **1× Live View** with several visible stars.
2. Set an approximate sky position using **Pointing RA hour**, **Pointing RA minute**, and **Pointing Dec deg**. You can find these values in Stellarium.
3. Leave **Frame width (0.1 deg) = 0** to estimate field of view from an electronic lens. For a manual lens, enter the horizontal width in tenths of a degree, for example **64 = 6.4°**.
4. Select **Solve LiveView** and check **Last result** for RA/Dec.

<img src="docs/images/plate-solving-preview.png" alt="Illustrative plate-solving coordinates shown in Astro tools" width="600">

**Test without the sky:** Select **Solve test PGM** with RA **05:00**, Dec **+35°**, and frame width **64** (or leave it at 0 for this built-in test). The test image is included in the ZIP.

This is **hinted plate solving**, not blind solving. It uses ordinary Live View, **not a long-exposure RAW image**. Faint star fields may not contain enough visible stars to solve.

## If the camera does not boot

Turn it off, remove the SD card, and try starting without it. If necessary, remove and reinsert the battery with the card removed. Return to your known-working SD card. Do **not** remove **autoexec.bin** from a bootable Magic Lantern card.

## For developers

[Source, build instructions, tests, and limitations](ASTRO_DEVELOPMENT.md) · [Reproducible build script](scripts/build.sh)

Midnight and Magic Lantern code: GPL-2.0-or-later. Star catalog: HYG v4.1, David Nash / Astronexus, CC BY-SA 4.0.
