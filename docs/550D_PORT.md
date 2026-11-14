# Canon 550D / Rebel T2i / Kiss X4 port

**Experimental. Physical 550D testing is pending.** The module and full
Magic Lantern firmware build target Canon **550D firmware 1.0.9**.

## Differences from Midnight's released camera targets

| Camera | 550D | 600D | 6D |
| --- | --- | --- | --- |
| Firmware | 1.0.9 | 1.0.2 | 1.1.6 |
| Sensor width | 22.3 mm | 22.3 mm | 35.8 mm |
| Processor family | DIGIC IV | DIGIC IV | DIGIC V |
| Magic Lantern platform | 550D.109 | 600D.102 | 6D.116 |
| Default raw focus shortcut | None | DISP | LCD-light |

- The same image-solving algorithms can work on the 550D. The sensor
  width and focal-length calculations match the 600D.
- Button codes differ from the 600D. The 550D does not have the same
  DISP shortcut, and no raw key is intercepted for automatic focusing.
  INFO or double half-press remain opt-in alternatives.
- Midnight continues to require **1x photo Live View** for plate solving.
  The gallery displays coordinates and annotations over the displayed
  JPEG/RAW preview, not inside the original image file.
- This Magic Lantern 550D platform uses the AllocateMemory pool because
  the camera's normal malloc region is small. Midnight's detection
  scratch buffer is large. A compile-test cannot prove runtime memory
  availability. The code checks allocation failures and reports errors.

## Build inputs and outputs

The Magic Lantern source is pinned to
142472e95054d73640f50789ac0fd66325499d82.

Build with:

    PYTHON=python3 RUN_SLOW_TESTS=1 bash scripts/build.sh

Packages:

- midnight-module-550D.109.zip: module plus matching sky catalogues
  for an **existing compatible** Magic Lantern 550D card. Does not
  modify autoexec.bin, ML-SETUP.FIR, photos, or camera firmware.
- magiclantern-astro-550D.109.zip: full Magic Lantern card files for
  the 550D. Use only on a suitable spare card after checking firmware.
- magiclantern-astro-wide-index.zip: optional wide-field sky index.

The build verifies required symbols in 550D_109.sym and runs the
portable solver regressions. It also rebuilds the existing 600D
and 6D packages. The 700D port is tracked separately in PR #4.

## First hardware tests

1. Verify Canon firmware 1.0.9 and back up the SD card.
2. If Magic Lantern already works, use the **module-only** package.
3. Enable astro in Modules and restart, then open Focus -> Midnight.
4. Run Developer -> Solve image file using the included TEST.PGM.
5. Run Play -> Q -> SET on a full-screen sky photo. Check overlays.
6. Run Solve Live View in photo mode at 1x. Verify zoomed views
   report that 1x is required.
7. Confirm no memory exhaustion or unexpected button behavior.
8. Only then consider tracking, alignment, or motor focus tests.

Do not replace your working 550D Magic Lantern installation until
physical tests pass. A 550D firmware ZIP is **not compatible** with
600D, 700D, or 6D models.
