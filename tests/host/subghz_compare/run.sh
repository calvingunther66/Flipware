#!/bin/sh
# Host test for the Sub-GHz capture compare logic (applications/main/subghz/helpers/subghz_compare.c).
# The test source stays next to the code (applications/main/subghz/helpers/tests/) and is
# excluded from the firmware in applications/main/subghz/application.fam.
#   tests/host/subghz_compare/run.sh
set -eu
HERE="$(cd "$(dirname "$0")" && pwd -P)"
R="$(cd "$HERE/../../.." && pwd -P)"
H="$R/applications/main/subghz/helpers"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
${CC:-gcc} -std=c11 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -I "$H" "$H/subghz_compare.c" "$H/tests/subghz_compare_test.c" -o "$OUT/subghz_compare_test"
"$OUT/subghz_compare_test"
