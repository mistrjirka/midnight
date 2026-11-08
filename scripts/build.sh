#!/usr/bin/env bash
set -euo pipefail
MIDNIGHT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORKROOT="$MIDNIGHT_ROOT/.build"
UPSTREAM_SHA="142472e95054d73640f50789ac0fd66325499d82"
ML_ROOT="$WORKROOT/magiclantern"
mkdir -p "$WORKROOT" "$MIDNIGHT_ROOT/dist"
if [[ ! -d "$ML_ROOT/.git" ]]; then
  git clone --no-checkout --filter=blob:none https://github.com/reticulatedpines/magiclantern_simplified.git "$ML_ROOT"
fi
git -C "$ML_ROOT" fetch --depth 1 origin "$UPSTREAM_SHA"
git -C "$ML_ROOT" checkout --force --detach "$UPSTREAM_SHA"
git -C "$ML_ROOT" clean -ffdqx
cp -a "$MIDNIGHT_ROOT/modules/astro" "$ML_ROOT/modules/"
git -C "$ML_ROOT" apply --check "$MIDNIGHT_ROOT/integration.patch"
git -C "$ML_ROOT" apply "$MIDNIGHT_ROOT/integration.patch"
mkdir -p "$ML_ROOT/.astro-data"
curl --fail --location --retry 3 --silent --show-error --output "$ML_ROOT/.astro-data/hygdata_v41.csv" https://raw.githubusercontent.com/astronexus/HYG-Database/main/hyg/CURRENT/hygdata_v41.csv
(
  cd "$ML_ROOT"
  python3 modules/astro/tools/make_catalog.py .astro-data/hygdata_v41.csv .astro-data/STARS.BIN
  python3 modules/astro/tests/test_focus.py
  python3 modules/astro/tests/test_faults.py
  python3 modules/astro/tests/test_shortcut.py
  python3 modules/astro/tests/test_solver.py .astro-data/STARS.BIN
  python3 modules/astro/tests/test_streak.py .astro-data/TEST.PGM
  python3 modules/astro/tests/test_psf.py
  python3 modules/astro/tests/test_catalog_index.py .astro-data/STARS.BIN
  python3 modules/astro/tests/test_star_labels.py .astro-data/STARS.BIN
  make -C modules -j4
  make -C platform/600D.102 -j4
  make -C platform/6D.116 -j4
  python3 modules/astro/tools/package.py 600D.102 .astro-data/STARS.BIN
  python3 modules/astro/tools/package.py 6D.116 .astro-data/STARS.BIN
)
cp "$ML_ROOT"/dist/magiclantern-astro-*.zip "$MIDNIGHT_ROOT/dist/"
sha256sum "$MIDNIGHT_ROOT"/dist/magiclantern-astro-*.zip
echo "Experimental firmware packages ready in $MIDNIGHT_ROOT/dist"
