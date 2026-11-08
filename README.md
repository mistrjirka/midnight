# Midnight

An astronomy-first interface for [Magic Lantern](https://github.com/reticulatedpines/magiclantern_simplified) on **Canon 600D (1.0.2)** and **Canon 6D (1.1.6)**.

**Experimental:** Not tested on physical cameras. Use a spare SD card and keep your existing working Magic Lantern card.

## Install

1. Check your camera version under **Canon MENU → Firmware Ver.**
2. Download the matching ZIP from the [Midnight release](https://github.com/mistrjirka/midnight/releases/tag/v0.2.0-alpha.1).
3. **Already have Magic Lantern?** Extract the **entire ZIP** onto a spare, bootable Magic Lantern SD card. Replace the existing Magic Lantern files. Do not rerun the installer.
4. **Fresh card?** Format it in the camera, extract the ZIP at the SD-card root, then use **MENU → Firmware Ver. → Firmware Update** in **M mode** to run **ML-SETUP.FIR**. Follow its instructions and restart. This installs the Magic Lantern boot setup, not a Canon firmware update.
5. Press **Trash/Delete** to open Magic Lantern, enable **astro** in **Modules**, and restart.

**After enabling astro, Trash opens Midnight first.** The original Magic Lantern menus are still available by pressing **Q/Left** or choosing **Full Magic Lantern**. Shutter, playback and essential Canon buttons remain unchanged.

## Midnight home menu

<img src="docs/images/astro-tools-menu.png" alt="Mockup of Midnight dashboard with intervalometer, bulb timer, focus and plate solving" width="600">

*UI illustrations, not photos from a real camera.*

| Option | What it does |
|---|---|
| **Intervalometer settings** | Opens Magic Lantern's existing interval, start trigger and shot-count controls |
| **Bulb Timer settings** | Opens its long-exposure duration and display controls |
| **Manual star focus** | Toggles a live sharpness score. Works with manual lenses |
| **Measure star now** | Measures focus once without moving the lens |
| **Auto focus (no mask)** | Tries small electronic focus steps on a bright star |
| **Solve sky (hinted)** | Finds pointing coordinates using a rough initial sky position |
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

**The focus motor has not been tested on a real camera.** Keep **Focus shortcut = Off** until the lens is validated. INFO and double half-press can be enabled later, but may conflict with other controls.

### Plate solving

Use **1× Live View** with enough visible stars. Set approximate **Pointing RA hour**, **RA minute** and **Dec deg** under **Astronomy settings**. Read the approximate location in Stellarium. Leave **Frame width (0.1 deg) = 0** to use an electronic lens's focal length, or enter the field width for a manual lens. Select **Solve sky (hinted)** and read **Last result** in the settings submenu.

<img src="docs/images/plate-solving-preview.png" alt="Illustrative pointing result, with rough RA and Dec hint" width="600">

**Blind solving is not ready yet.** A first all-sky index failed 3 of 5 synthetic test fields, so it is not enabled. The current solver also uses Live View rather than long RAW exposures and may miss faint stars. [Blind-solving research](docs/BLIND_PLATE_SOLVING.md).

**Offline test:** Choose **Astronomy settings → Solve test PGM** with RA **05:00**, Dec **+35°**, and field width **64**. The included test image needs no sky or lens.

## Troubleshooting

If the camera does not start, turn it off, remove the SD card and try booting without it. If necessary, remove and reinsert the battery without the card. Restore your known-working card. **Do not delete autoexec.bin from a bootable Magic Lantern card.**

[Development details](ASTRO_DEVELOPMENT.md) · [Build from source](scripts/build.sh)

Midnight and Magic Lantern: GPL-2.0-or-later. HYG star catalog: David Nash / Astronexus, CC BY-SA 4.0.
