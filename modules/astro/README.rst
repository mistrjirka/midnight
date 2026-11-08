Astro
=====

:Author: Experimental community module
:Summary: Star autofocus and experimental plate solving
:License: GPL-2.0-or-later

Experimental 600D/6D module. Not hardware tested.
Measure focus without moving lens before autofocus.
Focus menu includes tools, hint coordinates and shortcut options.
Focus shortcut defaults ON: 600D DISP / 6D LCD illumination button.
Photo LiveView only. In MF, the button toggles the focus readout.
Use Astronomy settings > Focus shortcut > Off to restore Canon behavior.
Requires matched STARS.BIN and STARS.IDX files on the SD card for solving.
See docs/USAGE.rst for full instructions and limits.

The solver uses a bounded integer two-scale PSF filter to rank compact
stellar peaks, then masks bright continuous satellite/aircraft trails in
its private image copy. A second-order tangent-plane correction reduces
projection errors over wide fields. This does not affect the lens focus
metric. Blind solving and long-exposure capture are NOT implemented.
