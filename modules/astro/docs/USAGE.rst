Midnight astronomy controls
===========================

Experimental Canon 600D 1.0.2 and 6D 1.1.6 module. The 600D image-file
solver and annotated LCD result were tested in QEMU. The 6D is compile
tested only. Physical camera operation remains unverified.

Installation
------------

Use a spare SD card and keep your known-working Magic Lantern card.
Do not install a package for a different camera or Canon firmware version.

1. Check your camera and Canon firmware under MENU -> Firmware Ver.
2. Download the matching full ZIP from the Midnight release linked in the
   root README.
3. For an existing bootable Magic Lantern card, extract the entire ZIP at
   the card root and replace the old files. Do not rerun the installer.
4. For a fresh card, format it in the camera and extract the matching ZIP
   at the card root. In M mode, use MENU -> Firmware Ver. -> Firmware Update
   to run ML-SETUP.FIR. Follow the installer and restart. This sets up Magic
   Lantern booting, not a replacement Canon firmware.
5. Press Trash/Delete, enable astro under Modules, and restart.

Trash now opens Midnight first. Choose Full Magic Lantern or press Q/Left
for the original menus. Copy every ML/ASTRO file, including SKY.DIR,
SKY.PAT, SKY.FLD and STARS.NAM. The retired STARS.BLQ/BIX/BKX database is
not used.

Recovery
--------

If the camera fails to start, turn it off, remove the SD card and try
without it. If necessary, remove and reinsert the battery without the
card. Restore the known-working card. Do not delete autoexec.bin from a
bootable Magic Lantern card.

Plate solving
-------------

No RA, Dec, focal length or image-width hint is needed. Copy every file
under ML/ASTRO, including SKY.DIR, SKY.PAT, SKY.FLD and STARS.NAM.

Solve LiveView (3s) waits for vibration to settle, captures a 1x photo
Live View frame, and opens a frozen annotated result automatically on
success. The delay is not an exposure. Q returns to Midnight. This
sensor-input workflow remains unverified on hardware.

Midnight advanced -> Developer tests -> Solve image file reads a strict 360x240 grayscale
PGM selected under Image file. TEST.PGM and TEST1.PGM through TEST3.PGM
are supported. The result opens automatically. This is the QEMU-tested
workflow. Original user photographs are not distributed.

The implemented Canon playback shortcut is Play -> Q -> SET. It reads
the displayed preview, not the original full-resolution RAW. Complete
native Canon album decoding and this shortcut still need validation.

Results show the frame centre as RA ``05h35m17s`` and Dec ``-05d23'12"``
(J2000). They also show the lens focal length measured from the solved field.
Use it to check the value entered for a manual lens.

Lens focal length (optional)
----------------------------

Midnight advanced -> Lens focal length -> Focal length source:

- None (blind): default. No lens data is needed, including for manual lenses.
- From lens: uses the focal length reported by an EF/EF-S lens.
- Manual: for lenses without electronic contacts. Enter the value under
  Manual focal length. Edit it like the intervalometer time: LEFT/RIGHT
  selects a digit, UP/DOWN changes it.

A hint restricts the accepted image scale to +-30 %. Each solve then does far
less work, which makes Live View guiding much more responsive. A wrong hint can
prevent a solve, so set the source back to None if solving stops working.

The standard index covers fields up to about 60 degrees, for example 20 mm
on the 600D or 28 mm on the 6D. For wider lenses, extract the optional
``magiclantern-astro-wide-index.zip`` (``ML/ASTRO/SKYW.*``) onto the card. It
is used only when the standard index finds nothing. Very wide fields remain
unreliable; plate solving is rarely needed there.

Target & guide
--------------

Midnight -> Target & guide:

- Catalogue: Messier (M), NGC, IC, Caldwell (C), Barnard (B) or
  Sharpless (Sh2).
- Number: edit it like the intervalometer time. The digit count follows the
  catalogue, for example M 1-110 or NGC 1-7840.
- Object: the catalogue entry, for example ``Orion Nebula`` or
  ``galaxy, mag 8.9``, and its size in arcminutes. Numbers missing from the
  catalogue show ``Not in catalogue``.
- Guide in LiveView: open 1x photo Live View first. Midnight solves the
  frame repeatedly. If the target is outside the frame, a red arrow from the
  centre points along the shortest path to it and shows the distance. Move
  the camera in the arrow's direction. When the target is in the frame, its
  outline and a red reticle mark it. Q stops guiding. Opening a menu or
  playback also stops it. Hold the camera still between moves; a moving frame
  cannot be solved. Raise Live View gain if too few stars are found.
- Mark target: also show the target or arrow on Solve LiveView, image-file and
  playback results.

Overlays on every solved frame (Live View result, image file, playback):

- Star names up to magnitude 4.5. Famous bright stars use their proper names,
  for example Vega or Betelgeuse. Others use Bayer names with Greek letters,
  for example ``γ Cas`` or ``π5 Ori``, then Flamsteed numbers.
- Deep-sky outlines with their true size and orientation. M and C objects are
  always shown. NGC/IC objects are shown up to the DSO magnitude limit.
  Barnard and Sharpless objects are shown when visibly large.
- Constellation figures and names.

Each layer can be switched off in Target & guide. The data files are
``ML/ASTRO/DSO.BIN``, ``DSO.KEY``, ``DSO.NAM`` and ``CONST.BIN``. Without them,
solving still works, but the overlays and the picker are empty.

The guide arrow, overlays and catalogue lookups are verified on the desktop
with the same C code. Physical-camera Live View guiding is not yet validated.

Focusing
--------

Manual star focus updates the star-size score without moving a lens.
Use a bright star at 5x/10x and minimize the score at the same zoom.
Auto focus (no mask) sends electronic lens commands and is not hardware
validated. DISP on 600D or LCD-light on 6D is enabled as a focus shortcut
by default. Set Focus shortcut -> Off to restore the original behavior.
Bahtinov diffraction-spike measurement is not implemented.

See the root README for a feature overview, docs/QEMU_RESULTS.md for
verified test scope, and docs/QEMU_DEVELOPMENT.md for emulator setup.

Quick menu and display gain
---------------------------

Midnight home has nine rows. Live View gain reuses Magic Lantern's
photo-only preview control. RIGHT raises it by one stop; LEFT lowers it.
Off through +6 EV are supported, without wrapping at the endpoints.
+3 EV is eight times preview brightness. Saved photos, ISO and movie
exposure gain are unchanged. Gain also increases visible noise and clips
highlights, so do not judge exposure from the boosted preview.

This setting shares its value with Display -> LV display gain. It works
in photo Live View, not movie mode, and is locked during focus/solving.
No second gain configuration or new register-writing code is added.

Midnight advanced groups Star focus settings, Last solve and Developer
tests. The one-shot focus action is under Star focus settings. Developer
tests use prepared PGM files, not Canon album images. Full Magic Lantern
retains all original ML photo, video, audio and diagnostic controls.
