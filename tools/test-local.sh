#!/usr/bin/env bash
# Run every emulation test against the last build (tools/build-local.sh).
set -euo pipefail
cd "$(dirname "$0")/.."
for test in sources/runtime/tools/tests/test_*.py validation/tests/test_*.py; do
    echo "== ${test}"
    python3 "${test}"
done
