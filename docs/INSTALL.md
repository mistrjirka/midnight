# Install or update Midnight

Midnight is a Magic Lantern astronomy module for **Canon EOS 600D (firmware 1.0.2)** and **Canon EOS 6D (firmware 1.1.6)**.

**Already have working Magic Lantern?** Install the *module-only* ZIP. It updates `ML/modules/astro.mo` and the matching `ML/ASTRO/` catalogues without changing `autoexec.bin`, your camera firmware, the card boot flags, photographs, or existing ML settings.

[Latest experimental release: v0.4.0-alpha.1](https://github.com/mistrjirka/midnight/releases/tag/v0.4.0-alpha.1)

## 1. Choose the right download

First check your camera's **Firmware Ver.** in the Canon menu.

| Camera / firmware | Existing compatible Magic Lantern card (recommended) | New compatible Magic Lantern installation |
| --- | --- | --- |
| EOS 600D, **1.0.2** | [midnight-module-600D.102.zip](https://github.com/mistrjirka/midnight/releases/download/v0.4.0-alpha.1/midnight-module-600D.102.zip) | [magiclantern-astro-600D.102.zip](https://github.com/mistrjirka/midnight/releases/download/v0.4.0-alpha.1/magiclantern-astro-600D.102.zip) |
| EOS 6D, **1.1.6** | [midnight-module-6D.116.zip](https://github.com/mistrjirka/midnight/releases/download/v0.4.0-alpha.1/midnight-module-6D.116.zip) | [magiclantern-astro-6D.116.zip](https://github.com/mistrjirka/midnight/releases/download/v0.4.0-alpha.1/magiclantern-astro-6D.116.zip) |

**550D, firmware 1.0.9:** a new experimental port produces midnight-module-550D.109.zip and magiclantern-astro-550D.109.zip. These are **not** part of the v0.4.0-alpha.1 release. Download them from the successful 550D pull-request build artifact and consult the [550D port guide](550D_PORT.md) first.

**Never install an archive for another camera or firmware version.** The *full* ZIP also carries camera-specific `autoexec.bin` and `ML-SETUP.FIR`. Use it only when you are setting up a compatible spare card and understand Magic Lantern's boot-card procedure.

The optional [wide-field sky index](https://github.com/mistrjirka/midnight/releases/download/v0.4.0-alpha.1/magiclantern-astro-wide-index.zip) is only useful for very wide fields (roughly above 60°). You do not need it for most telescope lenses.

## 2. Update an existing Magic Lantern card

1. Switch off the camera. When its activity light is off, remove the SD card.
2. Connect the card to a reliable reader. If the reader disconnects repeatedly or the filesystem reports errors, **stop**. Do not write to an unstable connection.
3. **Back up the complete `ML/` directory and `autoexec.bin` to your computer.** Back up your `DCIM/` photos separately if they are not already safe.
4. Download the **module-only** ZIP from the table above.
5. Open the ZIP. **Extract its contents to the root of the card**, not inside a new directory. Accept replacement of the existing `ML/modules/astro.mo` and matching `ML/ASTRO/` files.
6. Wait for copying to finish, then **safely eject or unmount** the SD card.
7. Insert the card and start the camera. In Magic Lantern, press **Trash/Delete → Focus → Midnight**.
8. If Midnight is not listed, open **Magic Lantern → Modules**, enable **astro**, and restart the camera.

The card should contain:

```text
SD card/
├── autoexec.bin                 (existing file: DO NOT replace during module update)
└── ML/
    ├── modules/
    │   └── astro.mo             (updated)
    ├── SETTINGS/
    │   └── astro.en             (module enabled)
    └── ASTRO/
        ├── SKY.DIR
        ├── SKY.PAT
        ├── SKY.FLD
        ├── STARS.BIN / STARS.IDX / STARS.NAM
        ├── DSO.BIN / DSO.KEY / DSO.NAM
        ├── CONST.BIN
        └── TEST.PGM
```

**Linux example** for a 600D card already mounted under `/run/media/$USER/EOS_DIGITAL`. Check the path before running any command.

```bash
CARD="/run/media/$USER/EOS_DIGITAL"
test -f "$CARD/autoexec.bin" || { echo "Wrong card or mount point"; exit 1; }
test -d "$CARD/ML/modules" || { echo "Magic Lantern missing"; exit 1; }

# Save the old module, data, and settings on your computer.
tar -C "$CARD" -czf "$HOME/midnight-card-backup.tar.gz" ML autoexec.bin

# Extract the matching module-only ZIP. Do NOT use the full firmware ZIP here.
unzip -o "$HOME/Downloads/midnight-module-600D.102.zip" -d "$CARD"
sync
# Safely unmount the card using your file manager or udisksctl.
```

This example does not format the card or alter the boot sector. If you use the file manager instead, copying the ZIP contents to the **card root** is enough.

## 3. First camera tests

1. Open **Midnight → Developer → Solve image file**. The supplied `TEST.PGM` should produce an annotated sky result. This tests the solver and catalogue without requiring the night sky.
2. View an ordinary photo in Canon's full-screen **Play** mode. Press **Q**, then **SET**. Midnight should show progress and then annotate matched stars, RA/Dec, and deep-sky objects directly over the displayed photo. Press **Q** to hide them. The original JPG/CR2 is unchanged.
3. Enable **1× photo Live View** and choose **Solve Live View (3s)**. That action waits three seconds for tripod vibration to settle. It does *not* take an exposure. At 5×/10× zoom, Midnight should explicitly ask for **1×** instead.
4. To choose a target, open **Midnight → Target → Catalogue / Number**. The initial target is **M42**. Use **Mark target** to enable or disable its on-screen arrow/outline.

If a solve fails, check **Midnight → Last solve → Status**. For a gallery failure, Midnight should also show a visible error until Q dismisses it.

**Physical-camera status:** The compiled 600D module has run successfully in QEMU and has been used on a physical 600D, but all camera and lens combinations are not verified. Physical lens autofocus and mount polar alignment are experimental.

## 4. Troubleshooting

| Symptom | Action |
| --- | --- |
| Midnight does not appear | Enable `astro` in Magic Lantern **Modules**, then restart. Check `ML/modules/astro.mo` exists. |
| `Use 1x Live View` | Leave 5×/10× zoom and use photo-mode 1× Live View. |
| `SKY index missing/corrupt` | Extract the complete module-only ZIP again, including all `ML/ASTRO/SKY.*` files. Do not mix databases from different releases. |
| Gallery Q/SET displays no result | Use the camera's ordinary full-screen Play view. Check **Last solve → Status**. Record which error appears. |
| Memory or module load error | Disable unnecessary ML modules and reboot. Keep your backed-up working version available. |
| Reader repeatedly disconnects or files vanish | Stop using that USB connection. Change reader/port and check the filesystem before writing. |

## 5. Roll back

1. Power off the camera and remove the card.
2. Restore the backed-up `ML/modules/astro.mo` and the entire `ML/ASTRO/` folder. If needed, restore your backed-up `ML/SETTINGS/` files.
3. Safely eject, reinsert, and reboot.

For a **module-only** upgrade, restoring the old `autoexec.bin` is normally unnecessary because the update never replaced it.

## 6. Fresh card / first Magic Lantern installation

Use a **spare SD card** and follow the [Magic Lantern installation guide](https://wiki.magiclantern.fm/install) for your camera and firmware. Make sure Magic Lantern boots correctly **before** adding Midnight. A ZIP containing `autoexec.bin` is not by itself sufficient to prepare an SD card for booting. Do not format an existing working card merely to add Midnight.

**Source:** [Midnight](https://github.com/mistrjirka/midnight). The modular C implementation incorporates architectural improvements from [Jakub's AstroML](https://github.com/kubakubakuba/AstroML), with the previous Midnight solver retained for regression tests.
