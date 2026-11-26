# Building from Source

Flipware builds with **fbt**, the Flipper Build Tool, exactly like Momentum and the official firmware. The full reference is [documentation/fbt.md](https://github.com/calvingunther66/flipware/blob/dev/documentation/fbt.md).

## Get the code

```bash
git clone --recursive --jobs 8 https://github.com/calvingunther66/flipware.git
cd flipware
```

The default branch is `dev`. Use `--recursive`: several libraries are git submodules.

## Toolchain

The first `./fbt` run downloads Flipper's prebuilt toolchain into `toolchain/` in the repo and uses it only inside fbt, without touching your system `PATH`. To use the toolchain's tools by hand, open an fbt shell: `source scripts/toolchain/fbtenv.sh` on Linux and macOS, or `scripts/toolchain/fbtenv.cmd` on Windows.

## Common commands

```bash
# Update package (.tgz and folder under dist/), the way releases are built
./fbt COMPACT=1 DEBUG=0 updater_package

# Build and install everything over USB (Flipper connected, qFlipper closed)
./fbt flash_usb_full

# Firmware only, over USB (leaves old SD card files in place, see Installing and Updating)
./fbt flash_usb

# Build one app
./fbt fap_<appid>          # e.g. ./fbt fap_seed_capturer

# Build, upload and launch one app
./fbt launch APPSRC=<appid>

# Open the device CLI
./fbt cli
```

`COMPACT=1 DEBUG=0` matters for anything you put on a device: flash space is nearly full (see the roadmap), and a debug build may not fit.

## Checks before you open a PR

```bash
./fbt lint        # C/C++ formatting and image checks
./fbt lint_py     # Python formatting
./fbt format      # fix C/C++ formatting
./fbt format_py   # fix Python formatting
```

`./fbt format` on current `dev` also reflows a few existing files that drifted. Leave unrelated files out of your PR.

## Host tests

Some features have tests that run on a computer, no Flipper needed:

- `tests/host/host_auth/run.sh`: host sign-in core (ASan and UBSan, fuzzed parsers).
- `tests/host/lfrfid_manual_format/run.sh`: RFID FC/ID encoders and decoders.
- `applications/main/subghz/helpers/tests/subghz_compare_test.c`: capture compare.

On-device unit tests use the `unit_tests` app set, for example `./fbt FIRMWARE_APP_SET=unit_tests`. See [documentation/UnitTests.md](https://github.com/calvingunther66/flipware/blob/dev/documentation/UnitTests.md).

## API version

Flipware keeps Momentum's app API and its own API version in `targets/f7/api_symbols.csv`. If you add public symbols, add them there and bump the minor version. CI no longer fails because Flipware's API version differs from the official firmware's.
