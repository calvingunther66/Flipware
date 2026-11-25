#!/usr/bin/env bash
# Host test for the host sign-in core (applications/services/desktop/helpers/host_auth_core.c).
# Needs: gcc, python3 with `cryptography`, and the lib/mbedtls submodule checked out.
# Set PYTHON to an interpreter that also has `fido2` to cross-check the client data JSON.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
OUT="${OUT:-$(mktemp -d)}"
MBED="$ROOT/lib/mbedtls"

if [ ! -f "$MBED/library/ecdsa.c" ]; then
    echo "lib/mbedtls is not checked out: git submodule update --init lib/mbedtls" >&2
    exit 2
fi

"${PYTHON:-python3}" -I "$HERE/gen_vectors.py" "$OUT/vectors.h"

SRCS=(
    "$ROOT/applications/services/desktop/helpers/host_auth_core.c"
    "$HERE/test_host_auth.c"
)
# Same mbedtls objects the firmware links (lib/mbedtls.scons)
for f in bignum bignum_core ecdsa ecp ecp_curves md md5 platform_util sha1 sha256; do
    [ -f "$MBED/library/$f.c" ] && SRCS+=("$MBED/library/$f.c")
done

# Drop unused mbedtls code: GNU ld and Apple ld spell this differently
if [ "$(uname -s)" = "Darwin" ]; then GC_FLAGS=(-Wl,-dead_strip); else GC_FLAGS=(-Wl,--gc-sections); fi

gcc -std=gnu11 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-sanitize-recover=all \
    -DMBEDTLS_CONFIG_FILE='"mbedtls_cfg.h"' \
    -I"$ROOT/lib" -I"$MBED/include" -I"$MBED/library" \
    -I"$ROOT/applications/services/desktop/helpers" -I"$OUT" \
    -ffunction-sections -fdata-sections "${GC_FLAGS[@]}" \
    "${SRCS[@]}" -o "$OUT/test_host_auth" 2>"$OUT/build.log" || { cat "$OUT/build.log" >&2; exit 1; }

"$OUT/test_host_auth"
