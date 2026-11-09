# Midnight development status

Current release candidate: v0.3.0-alpha.1. Read [QEMU results](docs/QEMU_RESULTS.md), [recovery/setup guide](docs/QEMU_DEVELOPMENT.md), and [current solver architecture](docs/BLIND_PLATE_SOLVING.md).

The current `astro_sky` module passes both original photo-derived frames inside Canon 600D QEMU, rejects a blank image, and renders named-star results on the emulated LCD. `Solve image file` and `Solve LiveView (3s)` automatically open the annotated screen after a successful solve. The Live View action preserves the original captured pixels before filtering. Q returns to Midnight.

Only the image-file route was exercised end to end. Native Canon playback/RAW decoding, actual sensor frames, physical motors, and real hardware timing remain unverified. 6D is compile-tested only.

The persistent full Magic Lantern checkout is `/workspace/magiclantern-astro`. The publication checkout is `/workspace/midnight-release`, which contains only the module, integration patch, tests and build scripts. Camera ROMs live on the separate `/models` mount and must never be committed. Original photographs and converted test inputs are private as well; only the approved result screenshots are public.

Always rebuild `modules/astro` before packaging. The packaging script injects its latest `astro.mo` rather than trusting a possibly stale copy in the platform ZIP, and checks exact byte equality. Only tagged v0.3.* commits publish experimental GitHub prereleases. Main-branch pushes build and test without changing existing releases.
