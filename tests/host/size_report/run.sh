#!/bin/sh
# Host test for scripts/size_report.py (flash and RAM report and its CI gates).
#   tests/host/size_report/run.sh
set -eu
HERE="$(cd "$(dirname "$0")" && pwd -P)"
exec python3 -I "$HERE/test_size_report.py" "$@"
