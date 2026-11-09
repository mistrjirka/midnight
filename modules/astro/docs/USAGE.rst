Midnight astronomy controls
===========================

Experimental Canon 600D 1.0.2 and 6D 1.1.6 module. The 600D image-file
solver and annotated LCD result were tested in QEMU. The 6D is compile
tested only. Physical camera operation remains unverified.

Plate solving
-------------

No RA, Dec, focal length or image-width hint is needed. Copy every file
under ML/ASTRO, including SKY.DIR, SKY.PAT, SKY.FLD and STARS.NAM.

Solve LiveView (3s) waits for vibration to settle, captures a 1x photo
Live View frame, and opens a frozen annotated result automatically on
success. The delay is not an exposure. Q returns to Midnight. This
sensor-input workflow remains unverified on hardware.

Astronomy settings -> Solve image file reads a strict 360x240 grayscale
PGM selected under Image file. TEST.PGM and TEST1.PGM through TEST3.PGM
are supported. The result opens automatically. This is the QEMU-tested
workflow. Original user photographs are not distributed.

The implemented Canon playback shortcut is Play -> Q -> SET. It reads
the displayed preview, not the original full-resolution RAW. Complete
native Canon album decoding and this shortcut still need validation.

Focusing
--------

Manual star focus updates the star-size score without moving a lens.
Use a bright star at 5x/10x and minimize the score at the same zoom.
Auto focus (no mask) sends electronic lens commands and is not hardware
validated. DISP on 600D or LCD-light on 6D is enabled as a focus shortcut
by default. Set Focus shortcut -> Off to restore the original behavior.
Bahtinov diffraction-spike measurement is not implemented.

See the root README, docs/QEMU_RESULTS.md and docs/QEMU_DEVELOPMENT.md
for full installation, test scope, recovery and reproduction details.
