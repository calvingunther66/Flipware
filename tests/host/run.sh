#!/usr/bin/env bash
# Run every host test: each tests/host/<name>/run.sh.
#   tests/host/run.sh            run all of them
#   tests/host/run.sh host_auth  run only the named ones
# Needs gcc, python3 with `cryptography` (and optionally `fido2`, via PYTHON=...), and the
# lib/mbedtls and lib/mlib submodules: git submodule update --init lib/mbedtls lib/mlib
# Exits non-zero if any test fails, after running all of them.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd -P)"

if [ "$#" -gt 0 ]; then
    names=("$@")
else
    names=()
    for script in "$HERE"/*/run.sh; do
        names+=("$(basename "$(dirname "$script")")")
    done
fi

failed=()
for name in "${names[@]}"; do
    script="$HERE/$name/run.sh"
    if [ ! -f "$script" ]; then
        echo "no such host test: $name" >&2
        failed+=("$name")
        continue
    fi
    echo "::group::host test: $name"
    echo "=== $name"
    if bash "$script"; then
        status=ok
    else
        status="FAILED (exit $?)"
        failed+=("$name")
    fi
    echo "::endgroup::"
    echo "--- $name: $status"
done

echo
if [ "${#failed[@]}" -gt 0 ]; then
    echo "Host tests failed: ${failed[*]}" >&2
    exit 1
fi
echo "All ${#names[@]} host tests passed"
