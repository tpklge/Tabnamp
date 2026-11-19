#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
if [ "$(uname -s)" = Darwin ] && [ -d /Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk ]; then
    cmake -S tests -B .host-build -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk
else
    cmake -S tests -B .host-build
fi
cmake --build .host-build
ctest --test-dir .host-build --output-on-failure
