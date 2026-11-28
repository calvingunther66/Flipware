#!/bin/sh
# Host test: the Sub-GHz plugin bundle table matches the plugin sources and the app manifest.
#   tests/host/subghz_plugins/run.sh
set -eu
HERE="$(cd "$(dirname "$0")" && pwd -P)"
exec python3 -I "$HERE/test_subghz_plugins.py" "$@"
