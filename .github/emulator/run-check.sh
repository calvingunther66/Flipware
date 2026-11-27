#!/usr/bin/env bash
# Runs one FlipperEmu check against the firmware's update package. It runs
# inside the emulator image (see Dockerfile) and is started by the
# "Emulator checks" workflow:
#
#   run-check.sh CHECK FIRMWARE_DIR OUT_DIR
#
# CHECK is one of smoke, update, vgm, ble, uart, lock, fwbugs. FIRMWARE_DIR is searched
# for flipper-z-f7-update-*.tgz. The check's output goes to OUT_DIR/CHECK.log;
# when it fails, the logs, screenshots and recordings of its emulated devices
# are put in OUT_DIR as well. Exit status is the check's (124 when it ran past
# CHECK_TIMEOUT seconds, 660 unless set).
set -uo pipefail

check="${1:?usage: run-check.sh CHECK FIRMWARE_DIR OUT_DIR}"
fwdir="${2:?usage: run-check.sh CHECK FIRMWARE_DIR OUT_DIR}"
out="${3:?usage: run-check.sh CHECK FIRMWARE_DIR OUT_DIR}"
emu="${FLIPPEREMU_HOME:-/opt/flipperemu}"

fw="$(find "$fwdir" -name 'flipper-z-f7-update-*.tgz' | sort | head -n1)"
if [ -z "$fw" ] ; then
    echo "No flipper-z-f7-update-*.tgz under $fwdir"
    exit 2
fi
fw="$(realpath "$fw")"
mkdir -p "$out"
out="$(realpath "$out")"

case "$check" in
    smoke)  cmd=(python3 scripts/smoke_test.py "$fw" --out "$out/screens" --keep) ;;
    update) cmd=(python3 scripts/update_check.py "$fw" --keep) ;;
    vgm)    cmd=(python3 scripts/vgm_check.py "$fw" --coproc) ;;
    ble)    cmd=(python3 scripts/ble_check.py "$fw" --keep) ;;
    uart)   cmd=(python3 scripts/uart_check.py "$fw" --keep) ;;
    lock)   cmd=(python3 scripts/lock_check.py "$fw" --keep) ;;
    fwbugs) cmd=(python3 scripts/fwbugs_check.py "$fw" --keep) ;;
    *)
        echo "Unknown check: $check"
        exit 2
        ;;
esac

cd "$emu" || exit 2
echo "firmware: $fw"
echo "emulator: $(git rev-parse HEAD)"
echo "check: ${cmd[*]}"
echo
# A check has a time limit of its own (it stops its emulators and exits 124).
# This one is the outer limit, shorter than the workflow step's, so that a hung
# check still gets its logs packaged below before the step is cancelled.
timeout -k 20 "${CHECK_TIMEOUT:-660}" "${cmd[@]}" 2>&1 | tee "$out/$check.log"
status="${PIPESTATUS[0]}"

if [ "$status" -ne 0 ] ; then
    echo
    echo "$check failed with status $status: keeping the devices' logs and pictures"
    if [ -d devices ] ; then
        find devices -type f \( -name '*.log' -o -name '*.png' -o -name '*.gif' \) -size -20M -print0 \
            | tar --null -czf "$out/devices-$check.tgz" -T - 2>/dev/null || true
    fi
fi
exit "$status"
