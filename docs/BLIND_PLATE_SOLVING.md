# Blind plate-solving architecture

The release uses `astro_sky.c`, not the retired vote-ranking solver. It identifies star patterns without user coordinates or focal length.

`make_sky_index.py` constructs overlapping sky fields at several angular scales from HYG catalog positions and magnitudes. It produces:

- `SKY.DIR`: sparse sorted exact-key directory, approximately 142 KB resident in RAM.
- `SKY.PAT`: compact four-star pattern records read in bounded blocks from SD.
- `SKY.FLD`: shared fields with up to 96 catalog stars for geometric verification.
- `STARS.NAM`: indexed names, generated alongside the underlying HYG catalog.

A pattern is only a hypothesis. The firmware checks independent catalog stars immediately, refines the transform, and requires at least eight distinct matches, including at least four outside the proposed four-star pattern, with residual checks. It stops on a verified result instead of finishing hundreds of patterns or keeping only a fragile ranked shortlist.

The generic catalog contains no supplied photographs or their solved coordinates. The current build has 25,687 shared fields and 5,393,868 patterns. Large data files remain on SD, not in camera RAM. The implementation keeps ARM `-Os`, bounded I/O and cancellation/timeout checks.

See [actual QEMU results](QEMU_RESULTS.md) for the two passing frames and exact limits. Broad all-sky recall, faint-field sensitivity and physical camera timing need further validation. A successful pair of regression photographs is not proof that every possible field will solve.
