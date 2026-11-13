# Modular Midnight C code review

**Scope:** AstroML-derived portable C algorithms, Canon 600D/6D integration, the message-queue worker, card I/O, frame capture and drawing. Baseline: `v0.4.0-alpha.1`. This review focuses on meaningful defects and regressions, not formatting rules.

## Checks performed

| Validation | Measured result |
| --- | --- |
| GCC `-fanalyzer` on portable `modules/astro/core/*.c` | No diagnostics |
| Clang `--analyze` on the same portable C files | No diagnostics |
| Host tests (includes the new lifecycle test with ASan/UBSan) | 54 passed, 2 skipped |
| Long solver-rate tests | 10 passed |
| 600D and 6D ARM link-symbol compatibility | 82 imports, 0 missing for either firmware |
| Canon 600D QEMU boot and native ARM image solve | 11 matches, 211 ms emulator-reported solver time |
| Photo-gallery on physical camera | Previously exercised with v0.4.0-alpha.1; current changes need a new hardware check |

The two skipped tests require privately held original CR2 files. A clean static analysis report does not prove that runtime races or hardware differences are absent.

## Findings and changes

| Priority | Finding | Result |
| --- | --- | --- |
| High | The task accepted messages queued after `QUIT`. These could remain unprocessed while the module unloads. | **Fixed.** A `closing` state rejects all later actions, including duplicate quit requests. |
| High | Failed or invalid DryOS semaphore/queue IDs were not detected before the worker used them. | **Fixed.** Reject null/low-bit-error IDs, invalid actions, and failed task creation. |
| Medium | `astro_busy()` read the worker's task-shared flags without synchronization. | **Improved.** A nonblocking semaphore read provides a conservative busy result while another task updates queue state. |
| Medium | The cached 360×240 result image was never released on module unload. | **Fixed.** Release after successfully stopping the worker; fail closed if the task is still running. |
| Medium | Installation documentation suggested extracting a full camera-specific firmware archive over a working card. | **Fixed in docs.** Recommend module-only updates with backups and model checks. |
| Medium | Silent gallery failures and generic Live View cancellation prevented diagnostics on the camera. | **Already addressed** in the modular release. Regression tests retained. |

The changes intentionally do **not** alter the star-detection thresholds, plate-solver mathematics, catalogue layout or camera sensor geometry. This reduces the risk of regressing a solver that already works on a real 600D.

## Remaining limitations and risk

1. **Thread coordination:** `volatile` remains for cancellation hints, playback state and UI state shared between DryOS tasks. This is not full C11 synchronization. A broader change would need task-scheduler-aware tests and physical-camera stress validation.
2. **Module hot reload:** This Magic Lantern interface exposes semaphore and message-queue creation but no supported destruction API here. Repeated hot unload/reload could retain OS queue/semaphore handles. Prefer rebooting the camera after updating the module.
3. **Memory pressure:** The 600D has limited runtime RAM. Image buffers are reused, catalogue chunks are bounded, and the worker owns analysis memory, but worst-case RAM with other ML modules enabled needs device testing.
4. **Gallery image source:** Native Canon photo playback uses a live YUV display buffer, not a full CR2/JPEG decoder. QEMU cannot yet reproduce every camera playback transition.
5. **Visual overlap:** Labels avoid other label rectangles, but constellation lines and deep-sky outlines are not included in the exclusion geometry.

## Relevant practices

- [SEI CERT C: data races (`CON43-C`)](https://cmu-sei.github.io/secure-coding-standards/sei-cert-c-coding-standard/rules/concurrency-con/con43-c/)
- [SEI CERT C: `volatile` does not synchronize (`CON02-C`)](https://cmu-sei.github.io/secure-coding-standards/sei-cert-c-coding-standard/recommendations/concurrency-con/con02-c/)
- [SEI CERT C: out-of-bounds memory access (`ARR30-C`)](https://cmu-sei.github.io/secure-coding-standards/sei-cert-c-coding-standard/rules/arrays-arr/arr30-c/)
- [SEI CERT C: file I/O](https://cmu-sei.github.io/secure-coding-standards/sei-cert-c-coding-standard/rules/input-output-fio/)
- [Magic Lantern build and module notes](https://github.com/reticulatedpines/magiclantern_simplified/blob/dev/CONTRIBUTING.md). In particular, clean module builds matter when evaluating new platform binaries.

## Next hardware check

On the Canon 600D, verify **Developer → Solve image file**, then **Play → Q → SET**, then **1× Live View solving**, with the updated module. After any failed solve, **Last solve → Status** should record the reason. Do not claim camera-wide regression coverage until these tests pass.
