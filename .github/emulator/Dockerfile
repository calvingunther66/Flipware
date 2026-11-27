# The emulator image that CI runs the firmware in: FlipperEmu at the commit in
# flipperemu.pin, with its QEMU built. Built once per pin by the "Emulator
# checks" workflow and kept in GitHub's container registry (see README.md).
#
#   docker build --build-arg FLIPPEREMU_REF=$(cat flipperemu.pin) .github/emulator
# Python 3.13 or newer: FlipperEmu's launcher parses `uart send --hex 41 42` and
# similar commands the way 3.13's argparse does; Ubuntu 24.04's 3.12 (and older)
# rejects them, and uart_check fails 12 of its 75 checks.
FROM python:3.13-bookworm

ARG FLIPPEREMU_REF
ARG DEBIAN_FRONTEND=noninteractive

LABEL org.opencontainers.image.source="https://github.com/calvingunther66/Flipware"
LABEL org.opencontainers.image.description="FlipperEmu (QEMU with the Flipper Zero board) for Flipware CI"

# What FlipperEmu's scripts/setup_qemu.sh needs to build QEMU (gtk is in
# because the script configures --enable-gtk on Linux), and what its
# headless checks need to run: dosfstools and mtools make the SD card image,
# Pillow and numpy are used by the screenshot and Sub-GHz checks.
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        build-essential ca-certificates curl flex bison git gnupg libfdt-dev \
        libglib2.0-dev libgtk-3-dev libpixman-1-dev libslirp-dev ninja-build \
        patch pkg-config xz-utils dosfstools mtools procps \
 && rm -rf /var/lib/apt/lists/* \
 && pip install --no-cache-dir pillow numpy

RUN test -n "$FLIPPEREMU_REF" \
 && git clone --quiet https://github.com/calvingunther66/FlipperEmu /opt/flipperemu \
 && git -C /opt/flipperemu checkout --quiet "$FLIPPEREMU_REF"

WORKDIR /opt/flipperemu

# About 6 minutes on four cores. The object files are not needed once the
# emulator is linked (nothing rebuilds it here), and take most of the space.
RUN scripts/setup_qemu.sh \
 && find build -name '*.o' -delete \
 && rm -f third_party/*.tar.xz third_party/*.tar.xz.sig

# The build must have the Flipper board, or every check would fail on a
# machine type that QEMU does not know.
RUN build/qemu/qemu-system-arm -M help | grep -q flipper \
 && ./flipperemu --help > /dev/null
