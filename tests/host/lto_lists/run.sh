#!/bin/sh
# Host test for scripts/fbt/lto.py: its lists agree with the source tree.
#   tests/host/lto_lists/run.sh
set -eu
HERE="$(cd "$(dirname "$0")" && pwd -P)"
exec python3 -I "$HERE/test_lto_lists.py" "$@"
