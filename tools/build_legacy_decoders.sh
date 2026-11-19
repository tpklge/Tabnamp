#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
root=$PWD
src="$root/.tools/FFmpeg-n7.1.1"
build="$root/.tools/ffmpeg-p4-build"
if [ -f "$build/install/lib/libavcodec.a" ] && [ -f "$build/.tabnamp-ready-v4" ]; then exit 0; fi
mkdir -p "$root/.tools" "$build"
if [ ! -f "$src/configure" ]; then
    archive="$root/.tools/ffmpeg-n7.1.1.tar.gz"
    curl -L --fail https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n7.1.1.tar.gz -o "$archive"
    expected=f117507dc501f2a6c11f9241d8d0c3213846cfad91764361af37befd6b6c523d
    actual=$(shasum -a 256 "$archive" | cut -d ' ' -f 1)
    [ "$actual" = "$expected" ] || { echo 'FFmpeg source checksum mismatch' >&2; exit 1; }
    tar -xzf "$archive" -C "$root/.tools"
fi
python3 tools/patch_ffmpeg.py "$src"
cd "$build"
"$src/configure" --prefix="$build/install" --enable-cross-compile --target-os=none --arch=riscv32 --cpu=generic --cross-prefix=riscv32-esp-elf- --disable-everything --disable-programs --disable-doc --disable-avdevice --disable-avfilter --disable-swscale --disable-network --disable-pthreads --disable-w32threads --disable-os2threads --disable-asm --disable-autodetect --disable-symver --disable-runtime-cpudetect --enable-small --enable-static --disable-shared --enable-decoder=wmav1,wmav2,atrac3,atrac3p --enable-demuxer=asf,oma,wav --enable-protocol=file --extra-cflags='-Os -ffunction-sections -fdata-sections -march=rv32imafc_zicsr_zifencei -mabi=ilp32f' --extra-ldflags='-march=rv32imafc_zicsr_zifencei -mabi=ilp32f -nostartfiles'
make -j 8
python3 "$root/tools/relocate_legacy_bss.py" "$build"
make -j 8
make install
touch .tabnamp-ready-v4
