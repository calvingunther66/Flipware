# Release checklist

How a Flipware release is cut, and the pass on a real Flipper that has to be
done before the final tag. The emulator checks in CI cannot replace this pass:
they do not have radios, a real SD card or a phone.

Copy the "Device pass" table into the release candidate's tracking issue or
pull request and fill it in. One line per row: pass, fail with a link to the
issue, or skipped with the reason.

## 1. Freeze

- All pull requests meant for the release are merged and `dev` is green
  (Build with its emulator checks, Lint, and the host tests).
- `CHANGELOG.md` has every change under `## Unreleased`.

## 2. Release candidate

1. Push the tag `flpw-NNN-rc1` on the commit to be tested. The tag's Build
   publishes a **pre-release** with the `.tgz`, `.zip`, `.dfu`, SDK and
   `SHA256SUMS` (see [OTA.md](OTA.md#flipware-release-packages)). The release
   notes come from `## Unreleased` until the changelog is renamed.
2. Install that pre-release on a Flipper, following [Install.md](Install.md)
   (both ways), and run the device pass below.
3. Fix what fails on `dev`, then tag `-rc2` and repeat the rows that failed or
   were touched by the fix. A change to firmware code after a pass voids the
   rows it could affect.

## 3. Device pass

Run it on a Flipper that was **not** just flashed from your own build: install
the pre-release the way users will.

| # | Area | What to do | Result |
| - | ---- | ---------- | ------ |
| 1 | Sub-GHz | Read a remote you own (static or rolling), save it, send it back; the receiver reacts. Run a Read RAW capture and replay it. | |
| 2 | NFC | Read a MIFARE Classic or Ultralight tag, save it, emulate it against a reader or a phone. Read an NTAG and a hotel card page if you have one. | |
| 3 | LF RFID | Read an EM4100 or HID tag, emulate it, write it to a blank T5577. | |
| 4 | iButton | Read a Dallas key, emulate it, write it to a blank with the Write Targets setting you chose. | |
| 5 | Infrared | Send a command from a universal remote and from a saved remote; learn a new button. | |
| 6 | Bad KB over USB | Run a short script on a computer; check the layout and the typing speed. | |
| 7 | Bad KB over BLE | Pair with a phone or computer, run the same script. | |
| 8 | BLE pairing | Pair with the Flipper mobile app, open the device, browse the SD card. Pair again after an update. | |
| 9 | Install from Momentum | On a Flipper running Momentum, install the pre-release with qFlipper. Settings, asset packs and saved files are still there. Then install from the SD card the same way. | |
| 10 | Install from official firmware | On a Flipper running the official firmware, install with qFlipper, then go back with qFlipper's own Update. Saved files survive both ways. | |
| 11 | SD apps | Install an app from the app catalog, open it; open the bundled apps in `Apps` (Games, GPIO, NFC, Sub-GHz, Tools) and one JavaScript app. Note the app API message if one refuses to load. | |
| 12 | VGM screen mirroring | With a Video Game Module attached: the screen mirrors, the lock screen still shows, input works. Also with PIN lock on and "Allow Module RPC While Locked" off: mirror only, no unlock. | |
| 13 | Desktop sign-in | With the PIN lock on, unlock from a computer with the host sign-in tool, if it ships enabled (see `HostAuth.md`). | |
| 14 | Settings and lock | Change a few settings in Flipware settings, set a PIN, reboot, check they stayed. | |
| 15 | Battery and power | Charge to full, check `power info`, check the battery page in Power settings. | |

Also write down: the firmware version and API version each Flipper had before
(the About page in Settings, or `info device`), the SD card, and the date.

## 4. Final tag

1. Rename `## Unreleased` in `CHANGELOG.md` to `## flpw-NNN - YYYY-MM-DD`, add a
   new empty `## Unreleased` above it, check that the install text in the
   ReadMe and the website still matches [Install.md](Install.md), and merge.
2. Push the tag `flpw-NNN` on that commit. The Build for the tag publishes the
   release. A final tag with no changelog section fails the release job.
3. Open the release and check: all five files are attached (the update `.tgz`
   and `.zip`, the full `.dfu`, the SDK `.zip` and `SHA256SUMS`; `flpw-001` was
   cut by hand and has only three of them), `SHA256SUMS`
   matches (`sha256sum --check --ignore-missing SHA256SUMS`), the notes are the
   section you wrote, and the download links work.
4. Install the published `.tgz` once more from a clean download, on a Flipper
   running the official firmware and on one running Momentum if you have both.
5. Check the download link on the [website](https://flipware-fw.dev) points at
   the new release.
