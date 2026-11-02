#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
docker build -t falcon-onefile-build .
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -w /src falcon-onefile-build bash tools/build-local.sh
