Astro
=====

Experimental star autofocus and plate solving for 600D/6D.
Do not use on irreplaceable hardware without hardware validation.

:Author: Community experimental module
:Summary: Plate solve and autofocus stars in LiveView
:License: GPL-2.0-or-later

Features:
- Star sharpness measurement and guarded lens focus sweep in Live View.
- Plate solving with a pointing hint using a compact star catalog.
- Optional button shortcuts for focus (default DISP/LIGHT).
- PGM input for QEMU and repeatable tests.

The module does not alter the original Trap Focus feature.\nTrash/Delete now opens Midnight first while the module is enabled.

Camera models: 600D firmware 1.0.2 and 6D firmware 1.1.6 only.

Quick start
-----------

1. Install only with matching same-checkout camera firmware and symbol file.
2. Copy STARS.BIN generated from HYG v4.1 into ML/ASTRO/.
3. Enable astro in the Modules tab, restart, then press Trash > Midnight.
4. Select Measure star now at 5x/10x LiveView first. No lens movement.
5. For actual autofocus (no Bahtinov mask) set the lens to AF, disable continuous AF, center
   a bright star and select Auto focus (no mask). This action moves the lens.
6. For plate solving, use 1x LiveView and set pointing RA and Dec
   approximately (within several degrees), plus frame FOV width.
   600D at 200mm: 6.4 deg. 6D at 200mm: 10.3 deg.
7. For deterministic QEMU tests place TEST.PGM (P5, 360x240) alongside
   STARS.BIN and choose Astronomy settings > Solve test PGM.
8. Focus shortcut defaults ON: DISP on 600D, LCD-light on 6D.
Photo LiveView only. If electronic AF is available, the button runs
autofocus (motor commands); with a manual lens it toggles the on-screen
focus score. Choose Astronomy settings > Focus shortcut > Off to restore
the button's normal behavior. Other choices: Double half-press or INFO.
Trap Focus is not changed.

Important: the YUV LiveView feed is NOT a long exposure. Most deep-sky
fields have too few visible stars without capture or frame stacking.
The real camera image pipeline and motor steps are not yet validated.

HYG v4.1 catalog credit: David Nash / Astronexus, CC BY-SA 4.0
https://github.com/astronexus/HYG-Database

Safety and test status
----------------------

This is a developer build, NOT hardware-tested firmware. The 600D/6D
LiveView buffer and physical lens position feedback need validation.
An autofocus request immediately fails if the lens is not electronically
attached or AF is unavailable. Each single-step motor command must show
a position change before any other command is sent. Lens timeout, no
movement, missing frame or lost star => stop issuing focus commands.
After a loss of position or lens communication, the camera does not
attempt blind recovery motor movements.

Focus > Astro tools > Manual focus readout shows a 1 Hz sharpness
measurement that also works without autofocus. It is off by default.
Smaller scores mean tighter star images for the SAME star and same zoom.
It uses adaptive star radii and locks onto one star between samples.
This is not a calibrated score comparable across different targets.

Plate-solving FOV uses electronically reported focal length and sensor
width by default, for Canon 600D (22.3 mm) or 6D (35.8 mm).
Manual FOV remains available for non-electronic lenses and unusual
magnification. Pointing is assumed within a few degrees. Solver
requires 1x LiveView. Magnified LiveView is supported for focusing only.

The unit tests simulate dead lens motors, missing feedback, loss of
LiveView frames and random sensor noise. Real-image smoke tests used:
- HorseHead.fits and M13_blue_0001.fits from Astropy data,
  https://github.com/astropy/astropy-data
- Canon EOS 600D CR2 sample from Photography Blog,
  https://www.photographyblog.com/reviews/canon_eos_600d_review
- Bahtinov diffraction-pattern example by Axleottal, public domain,
  https://commons.wikimedia.org/wiki/File:Bahtinov_mask_example.jpg

These are processed as 8-bit test images by the same C code.
Mask-spike intersection/offset analysis and full-resolution CR2 decoding
ON CAMERA are not implemented.

Midnight home menu
------------------

- Intervalometer settings: original Magic Lantern interval controls.
- Bulb Timer settings: original Magic Lantern bulb controls.
- Manual star focus: toggle LiveView sharpness score.
- Measure star now: one-shot, read-only sharpness check.
- Auto focus (no mask): electronic focus steps and star size.
- Solve sky (hinted): current RA/Dec solver.
- Astronomy settings: more options and offline test image.
- Full Magic Lantern: back to the standard menus.

Trash/Delete opens Midnight after enabling the module; Q/Left backs out
to the native tabs. No camera buttons are removed. A Bahtinov mask can
still be used visually, but the spike-alignment algorithm is NOT
implemented and the numeric star-size score assumes no mask.

Spatial catalog index
---------------------

The HYG catalog is stored in one contiguous STARS.BIN file sorted by
5-degree RA/Dec sky tiles, not thousands of small files.
STARS.IDX is a 10,392-byte direct-offset table. The camera reads only
overlapping tile ranges using FIO_SeekSkipFile. This significantly
reduces reads for hinted solving without a hash table or large RAM use.
All index records must come from the same catalog version.

Button shortcut compatibility
-----------------------------

600D: BGMT_PRESS_DISP (raw code 8). This otherwise switches display
screens and is also used by some existing Magic Lantern LiveView shortcuts.
6D: BGMT_LIGHT (raw code 0x20). This otherwise illuminates the top LCD.
No shortcut event is intercepted in playback, movie mode or when a
camera/menu interface is open. Disable the shortcut to restore original
button behavior. Default mode was unit-tested with a simulated raw event,
but physical key routing and autofocus motor safety have not been
verified on a real camera.
