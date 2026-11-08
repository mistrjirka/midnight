# Magic Lantern astronomy module: experimental prototype

**Status:** Native algorithm tests and ARM builds pass. This is NOT a
camera-validated release. Both firmware packages are experimental.

## Locations

- Camera module: `modules/astro/astro.c`
- Portable C solver: `modules/astro/astro_core.{c,h}`
- User guide: `modules/astro/docs/USAGE.rst`
- Camera build output: `dist/magiclantern-astro-{600D.102,6D.116}.zip`
- Generated data: `.astro-data/STARS.BIN` (ignored by Git)
- Synthetic image: `.astro-data/TEST.PGM` (ignored by Git)

## Build / test

```bash
# Requires arm-none-eabi-gcc, binutils, newlib headers, python3-docutils
python3 modules/astro/tools/make_catalog.py \
  .astro-data/hygdata_v41.csv .astro-data/STARS.BIN
python3 modules/astro/tests/test_solver.py .astro-data/STARS.BIN
python3 modules/astro/tests/test_focus.py
make -C modules -j4
make -C platform/600D.102 -j4
make -C platform/6D.116 -j4
python3 modules/astro/tools/package.py 600D.102 .astro-data/STARS.BIN
python3 modules/astro/tools/package.py 6D.116 .astro-data/STARS.BIN
```

## UI and first safe test

1. Confirm camera firmware version is **600D 1.0.2** or **6D 1.1.6**.
2. Copy the matching ZIP contents to a **spare**, bootable Magic Lantern SD
   card. Preserve the existing known-good card. Do not use firmware for a
   different camera or firmware version.
3. Enable `astro` in the Magic Lantern **Modules** tab, then restart.
4. Open **Focus > Astro tools > Measure star only**.
   This examines Live View and DOES NOT drive the lens.
5. **Solve test PGM** loads the synthetic `ML/ASTRO/TEST.PGM` from the SD
   card. Set hint RA **05:00**, Dec **+35°** and FOV **6.4°**.
6. **Solve LiveView** instead uses the live sensor preview (not a long
   exposure). Set actual focal length FOV and approximate RA/Dec. It needs
   at least eight detected/matched stars.
7. **Auto focus star** moves an electronically driven lens and is **not
   tested on hardware**. Inspect firmware compatibility and lens behavior
   before exercising it. The autofocus switch must be in AF mode and
   continuous AF disabled. Focus with a bright star in magnified LiveView.
8. Optional autofocus shortcut: **Off / Double half-press / INFO button**.
   Off is default. This does not modify Trap Focus. Conflicts are possible
   with existing shortcut controls.

## Remaining limitations

- No QEMU run was performed. The emulator needs a Canon ROM dump to boot.
  The PGM test input is present to allow future QEMU testing.
- Motor precision, backlash, focus restoration and 600D/6D LiveView
  buffer access have NOT been validated on either camera.
- Each focus step requires position feedback, and failure stops any
  further motor commands. No blind restoration after a motor error.
- Manual focus readout is available with or without electronic lens AF.
  Star radius is adaptive, and the same star is tracked between frames.
- Default plate-solving FOV is derived from lens focal length and camera
  sensor width. Unchipped lenses require a manual field width.
- Normal LiveView rarely shows faint stars. Long-exposure readout or
  frame accumulation is required for robust night-time plate solving.
- The solver requires an approximate pointing hint and a valid FOV, and has
  not been validated on real star-field photographs. It cannot yet
  blind-solve arbitrary sky regions.
- Memory: 86,400-byte grayscale image buffer plus small static tables.
  Runtime memory availability has not been measured on the cameras.
- Measured synthetic tests: five varied simulated HYG sky fields solved
  within 0.01° in the test set; 1000/1000 random-field rejects in a
  separate native stress test. These figures do not predict real-photo
  or on-camera performance.
- Avoid using the experimental full firmware ZIP on your main SD card
  until camera-specific testing is done. A single module is not guaranteed
  compatible with unrelated Magic Lantern build versions.

### Data attribution

HYG Database v4.1 by David Nash / Astronexus:
https://github.com/astronexus/HYG-Database (CC BY-SA 4.0).
The binary `STARS.BIN` is a modified, packed subset and remains under
CC BY-SA 4.0. The code is GPL-2.0-or-later.

## Real image tests

Downloaded into ignored .astro-data/real/:

- HorseHead.fits and M13_blue_0001.fits, Astropy data repo
- canon_eos_600d_sample.cr2, Photography Blog EOS 600D review
- bahtinov_mask_example.jpg, Wikimedia Commons (Axleottal, public domain)

Run with .astro-data/venv/bin/python modules/astro/tests/test_real_images.py.
The tests convert these files to 360x240 8-bit input, then invoke the
same portable C functions as the ARM module. We tested star detection
and relative focus scoring, not on-camera CR2 decoding or Bahtinov
spike geometry. HYG-generated fields remain the only plate-solve tests.
