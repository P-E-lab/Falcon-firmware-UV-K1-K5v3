#!/usr/bin/env bash
# Host tests of FMessage (gcc): the app logic with a simulated radio, then
# the screens rendered to PBM images. Build the app first (fmessage/build.sh):
# the tests read its generated assets.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
APP="$HERE/../../fmessage"
OUT="$(mktemp -d)"
gcc -w -I"$APP" -I"$HERE" -o "$OUT/host_test" "$HERE/fmessage_host_test.c"
gcc -w -I"$APP" -I"$HERE" -o "$OUT/render" "$HERE/fmessage_render.c"
cp "$APP/fmessage_assets.bin" "$OUT/"
cd "$OUT"
./host_test | grep -E "expect|saved|beeps"
./render > /dev/null
echo "Screens: $OUT/shot*.pbm"
