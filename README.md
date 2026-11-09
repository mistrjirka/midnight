# Midnight

An astronomy-first interface for [Magic Lantern](https://github.com/reticulatedpines/magiclantern_simplified) on **Canon EOS 600D (1.0.2)** and **Canon EOS 6D (1.1.6)**.

**Experimental prerelease.** The 600D solver and annotated image-file results were tested in Canon firmware running in QEMU. The 6D is compile-tested only. Physical cameras, autofocus motors, and native Canon album decoding remain unverified. Keep a known-working card and use a spare SD card.

## Install

1. Check your model and Canon firmware under **MENU -> Firmware Ver.**
2. Download the matching full ZIP from [v0.3.1-alpha.1](https://github.com/mistrjirka/midnight/releases/tag/v0.3.1-alpha.1).
3. **Existing Magic Lantern card:** extract the entire ZIP at the root of a spare bootable card, replacing the old files. Do not rerun the installer. Preserve your known-working card.
4. **Fresh card:** format it in the camera, extract the matching ZIP at its root, then use **MENU -> Firmware Ver. -> Firmware Update** in **M mode** to run `ML-SETUP.FIR`. Follow the installer and restart. This sets up Magic Lantern booting, not a replacement Canon firmware.
5. Open Magic Lantern with **Trash/Delete**, enable **astro** under **Modules**, and restart.

After enabling astro, **Trash opens Midnight first**. Choose **Full Magic Lantern** or press **Q/Left** to access the original menus. Copy all the `ML/ASTRO` files, especially `SKY.DIR`, `SKY.PAT`, `SKY.FLD`, and `STARS.NAM`. The retired `STARS.BLQ/BIX/BKX` database is not used.

## Blind plate solving and automatic results

**No RA, Dec, focal length, or field-of-view entry is required.** Midnight identifies four-star patterns in a generic all-sky catalog and checks additional stars before accepting a solution.

### From Live View

Open **1x photo Live View** and select **Solve LiveView (3s)**. Midnight waits three seconds for vibration to settle, captures one frame, and solves it. On success it **automatically opens a frozen copy of that exact frame**, with coordinates, confirmed-star boxes, and names. **Q returns to Midnight.** The original frame is preserved before trail masking, so the displayed image is not the filtered working buffer.

The three-second delay is **not an exposure** and does not release the shutter. A dark Live View frame may not contain enough stars. Live sensor input and the new Live View preview transition still need real-camera validation; the renderer and solver are exercised by the QEMU image-file tests below.

### From an image file

Open **Midnight -> Midnight advanced -> Developer tests -> Image file**, select a prepared `TEST.PGM` or `TEST1.PGM` through `TEST3.PGM`, then select **Solve image file**. The annotated result opens automatically on success. This is the reproducible workflow tested in QEMU. Inputs are strict 360x240, 8-bit grayscale PGM files. The package includes a synthetic catalog-derived test image, not the original user photographs.

### From Canon playback

Open a still photo at normal full-screen zoom with **Play**, press **Q**, then **SET** at **Solve this photo?**. The code analyzes the displayed preview and overlays verified stars without changing the photograph. **Native Canon album/RAW decoding and this complete Play -> Q -> SET path are not yet validated end to end.** Do not confuse this with the tested image-file action.

## Actual Canon 600D QEMU screenshots

These are actual emulated LCD captures drawn by the ARM firmware, not mockups or desktop-generated annotations.

### Clean photograph: IMG_7975

<img src="docs/images/midnight-7975-qemu.png" alt="Actual Canon 600D QEMU LCD showing 30 verified stars, sky coordinates and named-star annotations" width="720">

### Photograph with a bright trail: IMG_6446

<img src="docs/images/midnight-6446-qemu.png" alt="Actual Canon 600D QEMU LCD showing 14 verified stars and labels over the original trail-heavy photograph" width="720">

| Original photograph | Matched stars | RA | Dec | Pre-publication QEMU firmware timer |
|---|---:|---:|---:|---:|
| `IMG_7975.CR2` | 30 | 80.1032 degrees | +33.8599 degrees | 420 ms |
| `IMG_6446.CR2`, bright trail | 14 | 327.9574 degrees | +47.5273 degrees | 846 ms |

Both passed again using the rebuilt release-source module. Earlier fresh-card repeats measured 434 ms and 836 ms; the screenshots above are from that earlier verified build. A blank image was rejected without coordinates. **These are emulated firmware timer readings, not predictions of physical-camera speed.** Label lookup and rendering follow the timed solve.

Exact test pipeline: original CR2 embedded JPEG -> 360x240 grayscale PGM -> virtual SD -> compiled ARM solver in Canon 600D 1.0.2 firmware -> QEMU LCD. Only image conversion happens before the camera test. Detection, trail rejection, blind identification, geometric verification, coordinates, name lookup, and rendering run in firmware. Neither the photos nor their reference coordinates are inputs to the all-sky index builder.

[Detailed verification and limits](docs/QEMU_RESULTS.md) · [QEMU setup and recovery tutorial](docs/QEMU_DEVELOPMENT.md)

## Midnight tools

<img src="docs/images/midnight-gain-menu-qemu.png" alt="Actual Canon 600D QEMU Midnight quick menu with photo-only Live View gain set to plus 3 EV, eight times brightness" width="720">

The quick menu fits on one screen. Video/audio/RAW-video and debug tools remain available under **Full Magic Lantern**, rather than being deleted or changing existing camera settings.


| Option | Action |
|---|---|
| Intervalometer settings | Opens Magic Lantern's existing interval, start-trigger and shot-count controls |
| Bulb Timer settings | Opens long-exposure timing controls |
| Manual star focus | Toggles a live star-sharpness score; does not move the lens |
| Live View gain | Brightens photo Live View only: Off to +6 EV (64x). ISO and saved photos are unchanged |
| Auto focus (no mask) | Tries small electronic focus steps on a bright star |
| Solve LiveView (3s) | Captures and blindly solves a frame, then automatically shows the annotated result |
| Midnight advanced | Optional focus tuning, read-only last result, and developer tests |
| Full Magic Lantern | Returns to the normal shooting menus |

### Live View gain

Select **Live View gain** and use **RIGHT** to increase or **LEFT** to decrease. Range: **Off, +1, +2, +3, +4, +5, +6 EV**. Each step doubles preview brightness: +3 EV is 8x, +6 EV is 64x. The control stops at both ends instead of wrapping.

This is Magic Lantern's existing **photo-preview gain**, shared with **Display -> LV display gain**. It does not change ISO, saved RAW/JPEG exposure, or movie digital ISO. It can be set before entering photo Live View. It is blocked in movie mode and during a Midnight focus or solve operation. Gain also amplifies noise and can clip highlights. A boosted preview is not an exposure guide.

### Advanced controls

| Location under Midnight advanced | Contents |
|---|---|
| Star focus settings | One-shot Measure star now, motor step size, focus search radius, and focus shortcut |
| Last solve | Read-only status, RA and Dec, not pointing inputs |
| Developer tests | Prepared PGM test files and Solve image file, not the Canon photo album |

### Focus

Center a bright star at **5x/10x Live View**. For manual focusing, turn **Manual star focus** on, leave the menu, and adjust for the **lowest Star focus value**. Compare the same star at the same zoom.

For electronic autofocus, set the lens to **AF**, disable continuous/servo AF, and select **Auto focus (no mask)**. Start with **Motor step size = 1** and **Focus radius = 3**. Motor behavior has not been tested on a physical camera.

**The focus shortcut is on by default:** DISP on 600D or LCD-light on 6D. In photo Live View it can move an electronic AF lens; with a manual-focus lens it toggles the focus score. Set **Midnight advanced -> Star focus settings -> Focus shortcut -> Off** to restore the original button behavior. It does not override menus, playback, or movie mode.

A Bahtinov mask can be used visually, but Midnight does not yet measure its diffraction-spike alignment. The numeric focus score measures star size without a mask.

## Troubleshooting and development

If the camera fails to start, turn it off, remove the card, and try without it. Remove/reinsert the battery if needed, without the card. Restore the known-working card. **Do not delete `autoexec.bin` from a bootable Magic Lantern card.**

[Build from source](scripts/build.sh) · [Architecture](docs/BLIND_PLATE_SOLVING.md) · [Development notes](ASTRO_DEVELOPMENT.md)

Midnight and Magic Lantern: GPL-2.0-or-later. HYG v4.1 catalog: David Nash / Astronexus, CC BY-SA 4.0. Camera ROMs and original photographs are not redistributed.
