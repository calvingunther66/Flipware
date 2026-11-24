#!/bin/sh
# Host test for the 125 kHz RFID "Enter FC/ID" helpers (applications/main/lfrfid/helpers).
# Builds the helpers, bit_lib and the real protocol sources with a small furi shim under
# AddressSanitizer and UndefinedBehaviorSanitizer, then runs the checks.
#   tests/host/lfrfid_manual_format/run.sh
set -eu
HERE="$(cd "$(dirname "$0")" && pwd -P)"
R="$(cd "$HERE/../../.." && pwd -P)"
P="$R/lib/lfrfid/protocols"
OUT="${TMPDIR:-/tmp}/lfrfid_manual_format_test"
${CC:-gcc} -std=gnu17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -Wall -Wno-unused-parameter -Wno-format -Wno-incompatible-pointer-types \
    '-D_ATTRIBUTE(x)=__attribute__(x)' \
    -I "$HERE/shim" -I "$R/furi" -I "$R/lib" -I "$R/lib/mlib" -I "$R" \
    -I "$R/applications/main/lfrfid/helpers" \
    "$HERE/test_manual_format.c" \
    "$R"/applications/main/lfrfid/helpers/*.c \
    "$R/lib/bit_lib/bit_lib.c" "$R/furi/core/string.c" \
    "$P/protocol_em4100.c" "$P/protocol_h10301.c" "$P/protocol_io_prox_xsf.c" \
    "$P/protocol_awid.c" "$P/protocol_pyramid.c" "$P/protocol_gallagher.c" \
    "$P/protocol_hid_generic.c" \
    "$R/lib/lfrfid/tools/fsk_demod.c" "$R/lib/lfrfid/tools/fsk_ocs.c" \
    "$R/lib/toolbox/manchester_decoder.c" "$R/lib/toolbox/pulse_protocols/pulse_glue.c" \
    -o "$OUT"
"$OUT"
