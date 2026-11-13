# Host tests

```sh
astroml test all         # everything (pytest)
astroml test regress     # port vs. original Midnight, byte for byte
astroml test format-check
```

The C core is compiled with the host gcc and `-fsanitize=undefined`, then driven through
ctypes (`native.py`). The EXIF fuzzer additionally runs under AddressSanitizer.

| File | Covers |
|---|---|
| `test_regress.py` | Port and `tests/reference_midnight` sources on identical inputs. Needs `astroml data reference`. |
| `test_sky_solver.py` | Blind and hinted solve rates on synthetic HYG star lists; no wrong answers allowed; north-pole coverage. |
| `test_pipeline.py` | Rendered frames through trail masking, detection and solving. |
| `test_time_altaz.py` | Calendar, GMST, precession and alt/az against floating-point references. |
| `test_exif.py` | GPS EXIF in JPEG and CR2/TIFF (both byte orders), plus a fuzzer. |
| `test_real_frames.py` | Two real 6D frames (`tests/data/RAW1.PGM`, `RAW2.PGM`): RAW2 must solve to Orion and project known stars onto bright pixels; RAW1 is a marginal frame (strict xfail on the default index). |
| `test_polar.py` | Recovering the mount axis from two simulated frames, and the alt/az error split. |

Deliberate differences from the original are listed in `docs/PORT_REVIEW.md`.

## Real frames

`tests/data/*.PGM` are 360x240 frames reduced from CR2 photos the way the module reads playback
(`astroml fixtures raw`). The CR2 originals go in `tests/data/raw/`, which git ignores because
their EXIF holds a serial number and owner name; the test that checks the PGMs against them skips
when they are absent.
