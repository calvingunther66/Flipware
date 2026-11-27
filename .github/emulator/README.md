# Emulator checks in CI

Every Build run that is not for a tag, and not for a pull request from a fork, ends with
the **Emulator checks** workflow ([emulator.yml](../workflows/emulator.yml)). It takes the update package that Build
made, starts it in [FlipperEmu](https://github.com/calvingunther66/FlipperEmu) (QEMU
with the Flipper Zero board), and runs these checks, each in its own job with a
15 minute limit:

| Job (the name that Actions shows) | Check | What it shows |
|---|---|---|
| `emulator / emulator (smoke)` | `scripts/smoke_test.py` | boots, SD card, buttons, screen, USB CLI, IR, Sub-GHz, RFID, iButton, NFC, reboot |
| `emulator / emulator (update)` | `scripts/update_check.py` | an update package installs through the firmware's updater |
| `emulator / emulator (vgm)` | `scripts/vgm_check.py --coproc` | the Video Game Module link and Flipware's coprocessor offload |
| `emulator / emulator (ble)` | `scripts/ble_check.py` | the radio stack starts, a phone pairs, RPC over BLE |
| `emulator / emulator (uart)` | `scripts/uart_check.py` | the header's serial ports and the expansion link |
| `emulator / emulator (lock)` | `scripts/lock_check.py` | a module on the expansion port cannot unlock a PIN-locked Flipper |

Make a job a required check in the `dev` ruleset only after several green runs, and
use the name in the table exactly: a required name that never reports blocks every pull
request.

When a job fails, its **Summary** has the end of the check's output, and the run has an
artifact `emulator-<check>` with the full output, the logs of the emulated device
(`devices-<check>.tgz`) and its screenshots and recordings (smoke test: `screens/`).

## The image

QEMU takes about 6 minutes to build, so it is built once into a container image,
`ghcr.io/calvingunther66/flipware-emulator`, tagged with a hash of `Dockerfile` and
`flipperemu.pin`. A run that finds no image for the current hash builds and pushes it
first (the `image` job, about 10 minutes); every other run only pulls it.

- `flipperemu.pin` is the FlipperEmu commit the image is built from. To move to a newer
  FlipperEmu, put its commit hash there (the full 40 characters) in a pull request; that
  pull request builds the new image. Nothing else changes by itself, so a FlipperEmu
  push cannot turn Flipware's pull requests red.
- `run-check.sh` is not in the image: the workflow mounts it, so a change to how the
  checks are run does not rebuild QEMU.
- The image is built on Python 3.13: FlipperEmu's launcher needs 3.13 or newer (3.12's
  argparse rejects commands such as `uart send --hex 41 42`).
- Pull requests from forks are skipped: they cannot push the image.

## Running it by hand

On the repository's Actions tab, **Emulator checks** > Run workflow, with the ID of a
Build run (the number after `/actions/runs/` in its URL, not the `#N` run number) to
use that run's firmware. Handy when changing the workflow or a check, because it does
not wait for a new build. (GitHub offers the button only once the workflow is on `dev`.)

On a machine with FlipperEmu built, one check is:

    .github/emulator/run-check.sh lock path/to/firmware-folder out
    # FLIPPEREMU_HOME=~/FlipperEmu if it is not in /opt/flipperemu
