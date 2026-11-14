#!/usr/bin/env bash
# Reproducible Midnight 9-camera build with the modular AstroML-derived code.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ML="$ROOT/.build/magiclantern"
PIN="142472e95054d73640f50789ac0fd66325499d82"
PYTHON="${PYTHON:-python3}"
JOBS="${JOBS:-8}"
mkdir -p "$ROOT/.build" "$ROOT/dist"
if [[ ! -e "$ML/.git" ]]; then
    git clone --no-checkout --filter=blob:none \
        https://github.com/reticulatedpines/magiclantern_simplified.git "$ML"
fi
git -C "$ML" fetch --depth 1 origin "$PIN"
git -C "$ML" checkout --force --detach "$PIN"
git -C "$ML" clean -ffdqx
cp -a "$ROOT/modules/astro" "$ML/modules/"
git -C "$ML" apply --check "$ROOT/integration.patch"
git -C "$ML" apply "$ROOT/integration.patch"

# Regenerate the star index when the label IDs or sky layout change.
export PYTHONPATH="$ROOT${PYTHONPATH:+:$PYTHONPATH}"
"$PYTHON" - <<'PY'
from pathlib import Path
import os, shutil
from tools.data import sources, catalog, dso, constellations, sky_index
from tools import paths

data=paths.DATA
data.mkdir(parents=True,exist_ok=True)
sources.fetch(data)
catalog.build(data/"hygdata_v41.csv",data/"STARS.BIN")
dso.build(data, paths.SOURCES/"barnard_VII_220A.dat",
          paths.SOURCES/"sharpless_VII_20.dat",data/"DSO.BIN")
constellations.build(data/"constellations.lines.json",
                     data/"constellations.json",data/"CONST.BIN")
jobs=max(1,min(8,os.cpu_count() or 1))
sky_index.build(data/"STARS.BIN",data,"SKY",False,"polar",jobs)
sky_index.build(data/"STARS.BIN",data,"SKYW",True,"polar",jobs)
ref=data/"reference";ref.mkdir(parents=True,exist_ok=True)
for n in ("STARS.BIN","STARS.IDX","STARS.NAM",
          "DSO.BIN","DSO.KEY","DSO.NAM","CONST.BIN"):
    shutil.copy2(data/n,ref/n)
sky_index.build(ref/"STARS.BIN",ref,"SKY",False,"midnight",jobs)
PY

export PYTEST_DISABLE_PLUGIN_AUTOLOAD=1
"$PYTHON" -m pytest -q -m "not slow" "$ROOT/tests"
if [[ "${RUN_SLOW_TESTS:-0}" == "1" ]]; then
    "$PYTHON" -m pytest -q -m slow "$ROOT/tests"
fi

make -C "$ML/modules" -j"$JOBS"

# All platforms and their exported symbol filenames come from one manifest.
# 1100D has a non-standard symbol name, recorded in that manifest.
camera_lines="$("$PYTHON" "$ROOT/scripts/cameras.py")"
mapfile -t cameras <<< "$camera_lines"
for item in "${cameras[@]}"; do
    IFS=$'\t' read -r camera symbols <<< "$item"
    make -C "$ML/platform/$camera" -j"$JOBS"

    missing="$(comm -23 <(sort -u "$ML/modules/astro/build/astro.dep") \
                           <(awk '{print $2}' "$ML/platform/$camera/build/$symbols" | sort -u))"
    if [[ -n "$missing" ]]; then
        echo "Missing $camera firmware symbols:" >&2
        echo "$missing" >&2
        exit 1
    fi
    echo "$camera: all module symbols resolved"
done

"$PYTHON" "$ROOT/scripts/package_firmware.py" --data "$ROOT/.astro-data" \
    --ml "$ML" --output "$ROOT/dist"
sha256sum "$ROOT"/dist/*.zip
echo "Experimental Midnight firmware and module-only updates verified."
