# Blind plate-solving architecture

The release uses `astro_sky.c`, not the retired vote-ranking solver. It identifies star patterns without user coordinates or focal length.

`make_sky_index.py` constructs overlapping sky fields at several angular scales from HYG catalog positions and magnitudes. It produces:

- `SKY.DIR`: sparse sorted exact-key directory, approximately 142 KB resident in RAM.
- `SKY.PAT`: compact four-star pattern records read in bounded blocks from SD.
- `SKY.FLD`: shared fields with up to 96 catalog stars for geometric verification.
- `STARS.NAM`: indexed names, generated alongside the underlying HYG catalog.

A pattern is only a hypothesis. The firmware checks independent catalog stars immediately, refines the transform, and requires at least eight distinct matches, including at least four outside the proposed four-star pattern, with residual checks. It stops on a verified result instead of finishing hundreds of patterns or keeping only a fragile ranked shortlist.

The generic catalog contains no supplied photographs or their solved coordinates. The current build has 25,687 shared fields and 5,393,868 patterns. Large data files remain on SD, not in camera RAM. The implementation keeps ARM `-Os`, bounded I/O and cancellation/timeout checks.

## Acceptance: statistical significance

The four pattern stars match by construction, so only the remaining matches are independent evidence. Before v0.4, eight matches (four of them independent) were enough; after ~10^5 hypotheses, random point clouds passed that test 56 times in 60. Each candidate is now also scored with an upper bound on the chance of its independent matches: `C(n,k) * p^k`, where `p` is the catalogue density in the frame multiplied by the match area. That bound is multiplied by the number of hypotheses actually tested (Bonferroni) and must stay below 10^-6. All of this uses integer log-odds arithmetic. Random fields are now rejected (0/60), and recall did not drop.

## Exported transform (WCS)

The solver fits its transform about the index field's tangent point, which can be far from the image centre on wide lenses. After acceptance, every field star is re-projected about the image centre and refitted with Q24 precision. One extra association pass follows. The result is `sky_solution.wcs` (`astro_wcs.c`), used for coordinates, field of view, measured focal length and all overlays. Median reprojection error on synthetic frames is about 0.3-0.6 px (it was 8 px before the refit at 35 mm).

## Focal-length hint and wide lenses

Blind solving needs no lens data. An optional hint (lens-reported or manual focal length) limits the accepted image scale to +-30 %. On the synthetic test this cuts hypothesis checks from ~70,000 to ~70 per frame, which matters for the repeated solves of Live View guiding.

The default index covers fields up to about 60 degrees. For wider lenses, `make_sky_index.py --wide` builds an optional `SKYW.*` index of 19- and 27-degree fields, with coordinates in 4-millidegree units in the field header. It is shipped as a separate download. The camera tries it only after the normal index finds no match. Ultra-wide frames remain hard: off-axis gnomonic stretching distorts pattern shapes. Plate solving is rarely needed at those fields of view, so this is deliberately not a release requirement.

## Verification

`modules/astro/tests/test_sky_solver.py` runs the host build of the same C code against synthetic HYG frames. These cover random pointing over the whole sky, roll, mirror parity, both sensors, 14-300 mm lenses, centroid noise, missed and false detections, and barrel distortion. Any wrong sky position fails the test. Lenses with 9-55 degree fields must solve at least 85 % of frames (at most one miss in small samples). 60 random point clouds must be rejected. Results of one run (8 frames per lens):

| Field of view | Result |
|---|---|
| 6-55 deg (24-300 mm on 600D, 35-300 mm on 6D) | 7/8 or 8/8 per lens, except some 200-300 mm frames that have too few Live View stars |
| 58-65 deg | 5-8/8 |
| 70-104 deg | 0-3/8 blind; up to 6/8 with the optional wide index |
| Wrong positions | 0 |

These are synthetic star lists, not camera frames. Real Live View detection depth, lens distortion and hardware timing remain to be validated.

See [actual QEMU results](QEMU_RESULTS.md) for the two passing frames and exact limits. A successful pair of regression photographs is not proof that every possible field will solve.
