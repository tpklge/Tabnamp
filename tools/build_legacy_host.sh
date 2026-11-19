#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
root=$PWD
src="$root/.tools/FFmpeg-n7.1.1"
build="$root/.tools/ffmpeg-host-build"
[ -f "$src/configure" ] || { echo 'Run tools/build.sh build first to prepare pinned FFmpeg sources.' >&2; exit 1; }
python3 tools/patch_ffmpeg.py "$src"
if [ ! -f "$build/.asan-ready-v2" ]; then
    mkdir -p "$build"
    cd "$build"
    if [ "$(uname -s)" = Darwin ] && [ -d /Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk ]; then
        "$src/configure" --prefix="$build/install" --cc=/usr/bin/clang --disable-everything --disable-programs --disable-doc --disable-avdevice --disable-avfilter --disable-swscale --disable-network --disable-pthreads --disable-asm --disable-autodetect --enable-small --enable-static --disable-shared --enable-decoder=wmav1,wmav2,atrac3,atrac3p --enable-demuxer=asf,oma,wav --enable-protocol=file --extra-cflags='-O2 -fsanitize=address -fno-omit-frame-pointer -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk' --extra-ldflags='-fsanitize=address -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk'
    else
        "$src/configure" --prefix="$build/install" --disable-everything --disable-programs --disable-doc --disable-avdevice --disable-avfilter --disable-swscale --disable-network --disable-pthreads --disable-asm --disable-autodetect --enable-small --enable-static --disable-shared --enable-decoder=wmav1,wmav2,atrac3,atrac3p --enable-demuxer=asf,oma,wav --enable-protocol=file --extra-cflags="-O2 -fsanitize=address -fno-omit-frame-pointer" --extra-ldflags="-fsanitize=address"
    fi
    make -j 8
    make install
    touch .asan-ready-v2
    cd "$root"
fi
if [ "$(uname -s)" = Darwin ] && [ -d /Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk ]; then
    cmake -S tests/legacy -B .legacy-tests -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk
else
    cmake -S tests/legacy -B .legacy-tests
fi
cmake --build .legacy-tests
ASAN_OPTIONS=detect_leaks=0 .legacy-tests/legacy_tests .emulator-build/fixtures
