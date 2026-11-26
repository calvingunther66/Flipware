#!/bin/sh
# Host test for .github/workflow_data/release.py (release notes read only the tag's CHANGELOG.md section).
#   tests/host/release_notes/run.sh
set -eu
HERE="$(cd "$(dirname "$0")" && pwd -P)"
exec python3 -I "$HERE/test_release_notes.py" "$@"
