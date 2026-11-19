#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
if [ "$(uname -s)" = Darwin ] && pkg-config --exists sdl3; then
    export DYLD_LIBRARY_PATH="$(pkg-config --variable=libdir sdl3)${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
fi
if [ "$(uname -s)" = Darwin ] && [ -d /Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk ]; then
    cmake -S emulator -B .emulator-build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk
else
    cmake -S emulator -B .emulator-build -DCMAKE_BUILD_TYPE=RelWithDebInfo
fi
cmake --build .emulator-build -j 8
if [ "${1:-}" = test ]; then
    python3 tools/emulator_fixtures.py .emulator-build/fixtures
    ctest --test-dir .emulator-build --output-on-failure
else
    exec .emulator-build/tabnamp_emulator "${1:-sdcard/tabnamp}"
fi
