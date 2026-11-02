#!/usr/bin/env bash
# Build the runtime, its external store and the one-file installer BIN.
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=$(python3 -c "import json;print(json.load(open('sources/runtime/CMakePresets.json'))['configurePresets'][0]['cacheVariables']['VERSION_STRING_2'])")

cmake --preset S4K -S sources/runtime -B sources/runtime/build/S4K -DTARGET=falcon.data
cmake --build sources/runtime/build/S4K -j
python3 tools/build_payload.py sources/runtime sources/installer --name falcon.data
cmake --preset S4K -S sources/installer -B sources/installer/build/S4K -DTARGET=falcon.installer
cmake --build sources/installer/build/S4K -j
mkdir -p firmware
cp sources/installer/build/S4K/falcon.installer.bin "firmware/Falcon4K_v${VERSION}-OA.bin"
