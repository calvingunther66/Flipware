# Installing and Updating

> **No releases yet.** Flipware has no tagged releases so far. Until the first one (`flpw-001`, see the [roadmap](https://github.com/calvingunther66/flipware/blob/dev/ROADMAP.md)), build an update package yourself with [[Building from Source]] and install it as below. When releases exist they will be on the [releases page](https://github.com/calvingunther66/flipware/releases).

## Before you start

- Back up anything on the SD card you care about. Updating keeps the SD card's contents and Flipware's updater keeps system settings, but a backup costs nothing.
- Use a full **update package**, not a firmware-only flash, whenever you can. Several Flipware features ship parts on the SD card (see below), and an update package replaces them all at once.

## Option 1: qFlipper (`.tgz`)

1. Build or download the update package `.tgz`.
2. Open [qFlipper](https://flipperzero.one/update) and connect the Flipper.
3. Click **Install from file**, pick the `.tgz` and wait for the update to finish.

## Option 2: from the SD card (`.zip` or the package folder)

1. Extract the package so you have a folder with an `update.fuf` inside.
2. Copy that folder to `SD/update` on the Flipper.
3. On the Flipper, press **Down** to open the file browser, open that folder and run **Update**.

## Option 3: straight from a build

With the Flipper connected over USB and qFlipper closed:

```bash
./fbt flash_usb_full
```

This builds the full update package and installs it, SD card resources included.

## Parts that live on the SD card

A firmware-only flash (`./fbt flash_usb`) updates the firmware but leaves the old files on the SD card. These features need their SD card parts to match:

- **NFC:** `apps/NFC/nfc.fap` and `apps_assets/nfc/plugins/*.fal` must come from the same build. Old plugins refuse to load and the app shows "Plugin Not Loaded".
- **Host sign-in:** the `crypto` and `input` CLI plugins in `/ext/apps_data/cli/plugins/` carry part of its protection. See [[Host Sign-In]].
- **Sub-GHz:** the Sub-GHz app is external, on the SD card, as in Momentum.
- **iButton and RFID Write Targets** pages are `.fal` plugins.

If something misbehaves after a firmware-only flash, install the full update package.

## Coming from another firmware

Flipware keeps Momentum's app API, so apps built for Momentum work. Apps built only for the official firmware or another fork may need rebuilding against Flipware.

## Version names

Release tags use the `flpw-` prefix. Untagged builds report `flpw-dev`.
