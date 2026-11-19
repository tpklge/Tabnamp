#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
: "${IDF_PATH:=$HOME/esp/esp-idf}"
# This workstation has a Python 3.9 IDF environment; prefer it if available.
if [ -d "$HOME/.espressif/python_env/idf5.4_py3.9_env/bin" ]; then
    export PATH="$HOME/.espressif/python_env/idf5.4_py3.9_env/bin:$PATH"
fi
. "$IDF_PATH/export.sh"
case " $* " in
  *" build "*|*" reconfigure "*) ./tools/build_legacy_decoders.sh ;;
esac
exec idf.py "$@"
