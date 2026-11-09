# QEMU setup, recovery and publication guide

## Find the existing environment first

Use the **Development Sandbox** connector, list active sessions and reuse the `magic-lantern-astro` session. Do not recreate the project or confuse the conversation's temporary container with this persistent environment.

| Location | Purpose |
|---|---|
| `/workspace/magiclantern-astro` | Full Magic Lantern source and current module build |
| `/workspace/midnight-release` | Small `mistrjirka/midnight` publication repository |
| `/workspace/qemu-eos-midnight` | EOS QEMU source |
| `/workspace/qemu-eos-midnight-build/arm-softmmu/qemu-system-arm` | Existing emulator build |
| `/models/ROM0.BIN`, `/models/ROM1.BIN` | Private Canon 600D 1.0.2 ROMs, 16 MiB each |

**`/models` is a separate mount. Searching `/workspace` cannot find these ROMs.** Check `ls -lh /models/ROM*.BIN` and `findmnt /models` before assuming that any files are missing. Do not use the 600D ROMs for 6D. Never put ROMs or private photographs into GitHub.

Known ROM SHA256 values:

```
525aaa969f6a8c5dfe419f2f716e038593ff884515906bd1810be96f9528e320  ROM0.BIN
fd4b83bb0fb6a4c099de902e3fbbda02cea923b104a27708b2815ec2c8605fd4  ROM1.BIN
```

## Build and package

From the publication checkout, `bash scripts/build.sh` clones the pinned Magic Lantern commit, applies the integration patch, builds the generic HYG index, runs portable tests, compiles both camera packages and checks the ZIPs. Dependencies include the ARM toolchain, newlib, docutils, NumPy and SciPy. On a system with an existing project virtual environment:

```bash
cd /workspace/midnight-release
PATH=/workspace/magiclantern-astro/.astro-data/venv/bin:$PATH bash scripts/build.sh
```

For an incremental change in the full development checkout:

```bash
cd /workspace/magiclantern-astro
make -C modules/astro -j4
# Regenerate only if missing or changing the index format/catalog:
.astro-data/venv/bin/python modules/astro/tools/make_sky_index.py
make -C platform/600D.102 -j4
python3 modules/astro/tools/package.py 600D.102 .astro-data/STARS.BIN
```

The current engine is **`astro_sky.c`**. The old `astro_blind.c`, `STARS.BLQ`, `STARS.BIX` and `STARS.BKX` belong to a retired, failing prototype. Do not add them back into the module Makefile. Required active files are `SKY.DIR`, `SKY.PAT`, `SKY.FLD`, and `STARS.NAM`.

A platform ZIP can contain an older module even after `astro.mo` has been rebuilt. `package.py` inserts the current `modules/astro/build/astro.mo` and checks byte equality. Always rebuild the module before packaging.

## Prepare a disposable virtual card

Never alter the known-working base image. The saved SD image has an MBR partition beginning at sector 99, which is byte **50688**. mtools needs the `@@50688` suffix.

```bash
cd /workspace/magiclantern-astro
RUN="$PWD/.astro-data/acceptance-new"
test ! -e "$RUN" || { echo 'Choose a new test directory'; exit 1; }
mkdir -p "$RUN" .astro-data/private-roms/600D
ln -sfn /models/ROM0.BIN .astro-data/private-roms/600D/ROM0.BIN
ln -sfn /models/ROM1.BIN .astro-data/private-roms/600D/ROM1.BIN
qemu-img convert -f qcow2 -O raw \
  .astro-data/emulator-assets/gridfmt-midnight.qcow2 "$RUN/sd.raw"
IMG="$RUN/sd.raw@@50688"
mcopy -o -i "$IMG" modules/astro/build/astro.mo ::/ML/MODULES/ASTRO.MO
for name in SKY.DIR SKY.PAT SKY.FLD STARS.NAM; do
  mcopy -o -i "$IMG" ".astro-data/$name" "::/ML/ASTRO/$name"
done
# Add private, prepared 360x240 grayscale PGM test inputs here, when available:
# mcopy -o -i "$IMG" /private/path/photo7975.pgm ::/ML/ASTRO/TEST1.PGM
# mcopy -o -i "$IMG" /private/path/photo6446.pgm ::/ML/ASTRO/TEST2.PGM
qemu-img convert -f raw -O qcow2 "$RUN/sd.raw" "$RUN/sd.qcow2"
cp --reflink=auto .astro-data/emulator-assets/pristine.qcow2 "$RUN/cf.qcow2"
```

In v0.3.1-alpha.1 and newer, open **Midnight -> Midnight advanced -> Developer tests -> Solve image file**. Focus tuning is under **Star focus settings** and coordinates under **Last solve**. The quick menu has eight rows, with **Live View gain** replacing the duplicate one-shot focus action.

The PGM header must be exactly `P5\n360 240\n255\n`, followed by 86,400 grayscale bytes. The accepted photographs were converted from their original embedded Canon JPEGs before the test. No pointing or reference coordinates are supplied to the firmware. Do not describe this pre-conversion as native camera RAW decoding.

## Boot and control the emulated camera

Run through Development Sandbox's structured command API and retain the returned job ID. Set a sufficient timeout for interactive testing, then shut down explicitly. Both drives are required, even on the 600D model; omitting CF gives `CF init failed`.

```bash
export QEMU_EOS_WORKDIR="$PWD/.astro-data/private-roms"
/workspace/qemu-eos-midnight-build/arm-softmmu/qemu-system-arm \
  -M 600D,firmware=boot=1 \
  -drive if=sd,file="$RUN/sd.qcow2",format=qcow2 \
  -drive if=ide,file="$RUN/cf.qcow2",format=qcow2 \
  -display none \
  -serial file:"$RUN/serial.log" \
  -monitor unix:"$RUN/monitor",server=on,wait=off \
  >"$RUN/qemu.log" 2>&1
```

The retained executable exported as `qemu-system-arm-rom0fix` was used for the recorded acceptance run. Its hash and precise test scope are in [QEMU_RESULTS.md](QEMU_RESULTS.md). A disposable local test copy was used because the original photos were mounted in the conversation workspace. Source changes still belong in Development Sandbox.

The current emulator build has no VNC support. Use its UNIX monitor and `screendump`. The supplied `scripts/qemu_600d_control.py` sends keys and captures the actual LCD as PNG:

```bash
python3 /workspace/midnight-release/scripts/qemu_600d_control.py \
  --monitor "$RUN/monitor" --keys delete --screenshot "$RUN/menu.png"
```

Wait for `in menu_open` in the serial log before navigating. The first Delete can be ignored during startup. Arrows navigate, `right` activates the tested menu action, and `q` returns from the annotated result to Midnight HOME. Do not assume `space` reaches SET in this emulator build. Menu ordering changes across versions, so inspect each screenshot instead of reusing a blind key sequence.

## Acceptance gates

A boot, screenshot of a menu, host benchmark or timeout is **not** a passed solve. Require the completed serial result and inspect the actual annotated LCD:

- `IMG_7975`: at least eight independent matches; the verified frame currently returns 30, RA 80.1032 deg, Dec +33.8599 deg.
- `IMG_6446` with a bright trail: currently 14, RA 327.9574 deg, Dec +47.5273 deg. The display must preserve the original visible trail, not show the masked analysis buffer.
- Blank frame: reject it and leave both coordinates empty.
- Pressing the image-file solve action opens the annotated result automatically. Q returns to Midnight. The Live View solve also routes to this renderer after success and preserves its captured frame, but sensor acquisition remains unverified.
- Reboot with a fresh disposable card and rerun both photographs.

Serial file offsets are **byte offsets**. Reading in text mode can translate line endings and hide result lines. Capture screenshots with `screendump` and only lossless conversion. Do not synthesize annotations on the desktop and call them camera screenshots.

QEMU firmware timers are not physical camera timings. Native Canon album/RAW decoding, the complete Play -> Q -> SET route, physical lens motors, real SD timing and 6D emulation remain unverified.

## Publish without overwriting earlier releases

Use `/workspace/midnight-release`, inspect changes and private assets, then commit and push main. Tagged `v0.3.*` commits trigger the experimental prerelease job. The workflow refuses to overwrite an existing release. Verify both camera ZIPs, screenshot assets and `SHA256SUMS.txt` before reporting success.

No proprietary ROM dumps, original photographs or converted private test frames belong in a source commit or firmware package. Only the approved annotated screenshots are public.
