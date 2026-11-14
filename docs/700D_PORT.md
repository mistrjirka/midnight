# Canon 700D / Rebel T5i / Kiss X7i port

**Status: experimental.** The module and firmware compile against Magic
Lantern 700D.115 and pass host compatibility tests. The real 700D has not
yet been tested. The current v0.4.0-alpha.1 release predates this port.

## Differences from 600D and 6D

| Component | 600D | 700D | 6D |
| --- | --- | --- | --- |
| Canon firmware | 1.0.2 | **1.1.5** | 1.1.6 |
| Sensor width | 22.3 mm | **22.3 mm** | 35.8 mm |
| ARM module format | ARMv5TE | ARMv5TE | ARMv5TE |
| ML platform | 600D.102 | **700D.115** | 6D.116 |
| Controls | DISP, Q, SET | Touchscreen, Q, INFO, SET | LCD-light, Q, SET |
| Special autofocus shortcut | DISP | **Unavailable** | LCD-light |

The same numerical solver works for all models. 600D and 700D share the
223 sensor-width value (0.1 mm units) for field-of-view estimation and
focal-length hints. The module refuses unsupported firmware.

**Buttons:** The 700D has no equivalent to the 600D DISP or 6D LCD-light
shortcut. No default raw button or touch event is intercepted for autofocus.
The double half-press and INFO shortcuts are selectable explicitly, but
neither is used automatically. Keep the shortcut Off for the first test.
Manual star-size measurement remains available in Midnight.

**Image capture:** The generic YUV frame reader averages displayed pixels
to a 360x240 grayscale image. Its 720x480 simulated input is regression
tested, but physical 700D Live View and gallery memory layouts must still
be checked.

**Source baseline:** ReticulatedPines Magic Lantern at commit
142472e95054d73640f50789ac0fd66325499d82, platform 700D.115.
The separate full firmware package is exclusively for Canon 700D 1.1.5.
Third-party Magic Lantern builds may export different symbols.

## Output files

- midnight-module-700D.115.zip: recommended for an existing compatible
  Magic Lantern installation. Contains astro.mo, enable file, and sky data.
  It does NOT replace autoexec.bin, the Canon firmware, or photos.
- magiclantern-astro-700D.115.zip: full experimental Magic Lantern build
  for a compatible spare card. Includes camera-specific boot files.
- magiclantern-astro-wide-index.zip: optional wide-angle sky index.

Existing 600D and 6D packages are retained. Build all three cameras with:

    PYTHON=python3 RUN_SLOW_TESTS=1 bash scripts/build.sh

The build must resolve every ARM module import against all three firmware
symbol tables and pass native regression tests.

## First physical 700D test

1. Verify Canon firmware version 1.1.5 and back up the complete SD card.
2. Install the module-only 700D package on an existing compatible
   Magic Lantern card. Do not replace autoexec.bin.
3. Enable astro under Modules and restart. Open Focus -> Midnight.
4. Run Developer -> Solve image file. It should annotate the included
   TEST.PGM and display sky coordinates.
5. Show a full-screen photo. Use Play -> Q -> SET, and confirm that the
   solution overlays the photo without touchscreen interference.
6. Activate 1x photo Live View and run Solve Live View. Verify the
   5x/10x error message asks you to return to 1x.
7. Check target guidance on a known bright star field. Leave physical
   autofocus motor testing to a later supervised test.

No 700D ROM or reliable 700D QEMU boot test was available during porting.
C compilation and host tests cannot prove the real camera behaves correctly.
Existing 600D QEMU screenshots are NOT validation of the 700D.
