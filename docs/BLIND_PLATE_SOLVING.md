# Blind plate solving on Canon 600D and 6D

## Desired behavior

Point the camera anywhere, select "Solve sky", and see RA/Dec without
entering a starting sky position. Use the lens-reported focal length
and camera sensor width to estimate image field of view, or let users
enter the focal length / FOV for unchipped lenses.

## Research and test results

- ESA [tetra3](https://github.com/esa/tetra3) demonstrates lost-in-space
  (blind) star-field solving using precomputed four-star geometry. The
  default data covers 10-30 degree FOV; a new database can be built for
  a few-degree FOV. Desktop timing claims do NOT represent Canon DIGIC.
- [Astrometry.net](https://astrometry.net/doc/build-index.html) uses
  quad-pattern index files organized by angular scale. Its standard
  indexes/solver are not lightweight enough to embed unchanged here.
- Our native current solver needs an approximate position and >=8
  matching stars. It has not been validated on real photographic skies.
- Exploratory test 2026-11-08: used 26,824 HYG stars to approx mag 7.5
  and generated 541,148 geometric triangle patterns (50,306 occupied
  0.04-degree distance bins). Naive triangle voting selected the right
  sky patch as first choice in 2/5 synthetic scenes. It failed in
  3/5. This is NOT sufficient for production use.
- A packed reference index could stay on SD card rather than RAM.
  Actual memory, SD bandwidth and processing time must be measured.

## Required implementation before user access

1. Acquire an image with enough detectable stars. Ordinary Canon LiveView
   is likely too dark for narrowband or faint star fields. Prototype a
   safe long-exposure RAW or multi-frame LiveView pipeline.
2. Port or implement a tested four-star all-sky index and pattern lookup
   in portable C, with a bounded RAM cache and SD streaming.
3. Verify solutions with at least eight independent stars, multiple
   pattern votes, plausible geometry and low RMS error.
4. Test with both 600D (6.4 x 4.3 deg at 200 mm) and 6D (10.3 x 6.9 deg
   at 200 mm), wide and narrow fields, rotations, hot pixels and dust.
5. Benchmark on camera. If it cannot solve quickly enough, offer a
   cancelable operation and a useful timeout. Never display a guess
   as a valid position.

Current release intentionally retains "Solve sky (hinted)" and hides
the manual RA/Dec entries under "Astronomy settings".
