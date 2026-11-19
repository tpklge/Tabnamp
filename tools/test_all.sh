#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p artifacts
./tools/host-test.sh 2>&1 | tee artifacts/core-tests.log
python3 -m unittest discover -s tests -p 'test_tools.py' 2>&1 | tee artifacts/tools-tests.log
./tools/build.sh build size 2>&1 | tee artifacts/firmware-build.log
TABNAMP_RADIO_SECONDS="${TABNAMP_RADIO_SECONDS:-1800}" TABNAMP_SOAK_SECONDS="${TABNAMP_SOAK_SECONDS:-3600}" ./tools/emulator.sh test 2>&1 | tee artifacts/emulator-tests.log
./tools/build_legacy_host.sh 2>&1 | tee artifacts/legacy-tests.log
cp .emulator-build/artifacts/*.json artifacts/
for orientation in landscape portrait preferences-landscape preferences-portrait keyboard-portuguese; do
    ffmpeg -v error -i ".emulator-build/artifacts/$orientation.ppm" -y "artifacts/$orientation.png"
done
python3 tools/test_report.py
