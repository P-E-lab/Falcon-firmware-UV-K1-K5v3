#!/usr/bin/env bash
# Build the runtime, its external store and the one-file installer BIN, for
# each edition: Falcon 1K (1024 channels in the slot's own config bank) and
# Falcon 4K (4096 channels in the shared Sonic 4K reserve). 4K is built last:
# the generated sources/installer/App/install_payload.c is the 4K payload.
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=$(python3 -c "import json;print(json.load(open('sources/runtime/CMakePresets.json'))['configurePresets'][0]['cacheVariables']['VERSION_STRING_2'])")

mkdir -p firmware
for EDITION in 1K 4K; do
    PRESET="S$EDITION"
    cmake --preset "$PRESET" -S sources/runtime -B "sources/runtime/build/$PRESET" -DTARGET=falcon.data
    cmake --build "sources/runtime/build/$PRESET" -j
    python3 tools/build_payload.py sources/runtime sources/installer --name falcon.data --preset "$PRESET"
    cmake --preset S4K -S sources/installer -B sources/installer/build/S4K -DTARGET=falcon.installer \
        "-DINSTALL_EDITION=Falcon $EDITION"
    cmake --build sources/installer/build/S4K -j
    cp sources/installer/build/S4K/falcon.installer.bin "firmware/Falcon${EDITION}_v${VERSION}-OA.bin"
done
