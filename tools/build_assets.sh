#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
if [ -x .tools/node-v22.21.1-darwin-arm64/bin/node ]; then
    export PATH="$PWD/.tools/node-v22.21.1-darwin-arm64/bin:$PATH"
fi
if [ "$#" -ne 1 ]; then
    echo 'Usage: tools/build_assets.sh /path/to/licensed-font.ttf' >&2
    exit 2
fi
font=$1
npm --prefix tools/assets ci --ignore-scripts
for size in 20 32; do
    tools/assets/node_modules/.bin/lv_font_conv --font "$font" --size "$size" --bpp 4 --format bin --range 0x20-0xff --no-compress -o "sdcard/tabnamp/skins/classic/fonts/text${size}.bin"
done
