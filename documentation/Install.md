# Installing Flipware

Flipware installs like any Flipper firmware update: you give the Flipper an
update package and its updater does the rest. There are two ways to hand it
the package, and the same steps work whether the Flipper runs the official
firmware, Momentum, Unleashed or an older Flipware.

Get the files from the [latest release](https://github.com/calvingunther66/Flipware/releases/latest).

| File | Use it for |
| ---- | ---------- |
| `flipper-z-f7-update-<tag>.tgz` | Installing with qFlipper (the easy way) |
| `flipper-z-f7-update-<tag>.zip` | Installing from the SD card, without a computer app |
| `flipper-z-f7-full-<tag>.dfu` | Recovery only, when the Flipper does not boot (see [If it goes wrong](#if-it-goes-wrong)) |
| `flipper-z-f7-sdk-<tag>.zip` | Building apps against this release |
| `SHA256SUMS` | Checking that a download is intact |

`<tag>` is the release name, for example `flpw-001`. Releases whose name ends in
`-rcN` are release candidates, marked as pre-release on the releases page; use
them only if you want to help test.

`flpw-001` was published by hand, under the git tag `v1.0.0`. It has the `.tgz`,
the `.zip` and the SDK zip, but **no `.dfu` and no `SHA256SUMS`**. Releases from
`flpw-002` on are published by the release pipeline and have all five files.

## Before you start

- **Charge the Flipper** to at least 50%. An update that loses power halfway
  can leave it needing recovery.
- **Copy your SD card to your computer** if it holds anything you would miss.
  The update leaves your own files alone (see [What the update changes](#what-the-update-changes)),
  but a backup costs two minutes.
- **Optional: check the download** (releases from `flpw-002` on). Put `SHA256SUMS` next to the files and run
  `sha256sum --check --ignore-missing SHA256SUMS` (on a Mac:
  `shasum -a 256 -c SHA256SUMS --ignore-missing`).
- Flipware is built for the Flipper Zero (hardware target `f7`). There is no
  package for other devices.

## Install with qFlipper

1. Download the `.tgz` from the release. Do not unpack it.
2. Open [qFlipper](https://flipperzero.one/update) and connect the Flipper over USB.
3. Click **Install from file**, choose the `.tgz` and wait.
4. The Flipper restarts several times and shows the updater screen while it
   works. Leave it connected until it is back at the main screen. The first
   start shows a short welcome slideshow.

## Install from the SD card

For when you have no computer handy, or qFlipper will not talk to the Flipper.

1. Download the `.zip` from the release. Its files (`update.fuf`, `Manifest`
   and the rest) are at the top level of the zip, with no folder around them.
2. On the SD card, make a folder for the update inside `update`, for example
   `SD/update/flipware`. Make `update` first if there is none. Extract the
   zip's contents into that folder, with an SD card reader or qFlipper's file
   manager. If your unzip tool already made a folder, copy that folder instead.
3. On the Flipper, open the file browser (press **Down** on the main screen),
   open `update`, then your folder, and run **Update** (the `update.fuf` file).
4. Wait for the updater to finish; the Flipper restarts on its own.

## Moving over from Momentum

Flipware is built on Momentum, so this is the smoothest switch.

- Install the package the usual way (above). There is nothing to uninstall first.
- **Settings carry over.** Momentum and Flipware keep their settings in the same
  place, so your lockscreen, menu, Sub-GHz and interface choices stay as they
  were. Settings that Flipware added start at their defaults.
- **Asset packs carry over.** Packs in `SD/asset_packs` are read the same way.
- **Apps built for Momentum keep working.** Flipware uses the same app API
  (87.x at `flpw-001`), so the apps you already have still load.
- **Names change.** The boot screen, updater and passport say Flipware, and
  the settings app is called Flipware settings. Menu paths in Momentum guides
  that say `Momentum` are `Flipware` here.
- **First boot.** You will not see the one-time welcome slideshow again: the
  updater remembers that a Momentum-family firmware has already shown it
  (see [OTA.md](OTA.md#first-boot-flag)).

If you would rather go back, install the Momentum `.tgz` the same way.

## Moving over from the official firmware

The same steps work from the official firmware (OFW), from Unleashed and from
other custom firmware.

- **Your saved signals, cards and keys are kept**: Sub-GHz, NFC, RFID, iButton,
  Infrared and Bad KB files live on the SD card and the update does not touch
  them. Internal settings and pairings (Bluetooth, desktop, name) are backed up
  before the update and put back afterwards.
- **A new menu.** Flipware has the Momentum interface: more settings, the lock
  screen with extra tiles, asset packs. Settings for these start at defaults.
- **App compatibility.** Each app is built for one API version, and the major
  number has to match the firmware's. Flipware is on API 87.x at `flpw-001`.
  An app built for a newer major (the official firmware had reached 88 when
  this was written) shows **Outdated Firmware**, and one built for an older
  major shows **Outdated App**. Apps from the official app catalog follow the
  official firmware, so some may not start until the official firmware and
  Flipware are on the same API again; the apps bundled in the update, and apps
  built for Momentum, load.
- **To go back**, install the official `.tgz` (qFlipper's own **Update**
  button does it for you).

## What the update changes

- The firmware and the radio stack, if the package carries a newer one. The
  radio update takes the longest and restarts the Flipper a few times.
- Files on the SD card that came with the previous firmware (its bundled apps,
  asset packs and databases) are removed and replaced by the ones in the new
  package. **Files you added yourself are never removed.** A bundled file you
  edited by hand is replaced, so keep a copy of anything like that.
- Everything else on the SD card stays as it is.

## If it goes wrong

- **The updater shows an error code** such as `[2-30]`: the codes are listed in
  [OTA.md](OTA.md#ota-update-error-codes). Most are fixed by trying again with
  the SD card reseated, or by installing with qFlipper instead of from the SD card.
- **Update from SD card finds nothing**: the folder has to be inside `SD/update`
  and the folder you open must contain the `update.fuf` file directly, not
  another folder around it.
- **The Flipper does not boot.** Put it in DFU mode as Flipper Devices'
  recovery guide describes and flash the `.dfu` with qFlipper. The `.dfu`
  replaces the firmware only and does not touch the radio stack (the `.dfu` is
  only on releases from `flpw-002` on; for `flpw-001`, ask in an issue); run a normal
  update afterwards if the radio needs one.
- **Anything else**: [open an issue](https://github.com/calvingunther66/Flipware/issues/new/choose)
  with the firmware you came from and what the screen showed.
