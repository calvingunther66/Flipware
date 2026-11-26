# Features

Everything here is on top of what Momentum already has. Each entry says where to find it, how to use it, the pull request it came from, and whether it has been checked on a real Flipper.

**Status key**
- **Device-tested:** checked on a Flipper.
- **Partly device-tested:** some of it checked on a Flipper, the rest not yet.
- **Untested on device:** builds and passes its host tests, but nobody has run it on a Flipper yet. Reports are welcome in [Issues](https://github.com/calvingunther66/flipware/issues).

## Video Game Module

| Feature | Status | From |
| --- | --- | --- |
| Coprocessor offload (CRC32, heatshrink) on the VGM's RP2040 | Device-tested | [#1](https://github.com/calvingunther66/flipware/pull/1) |
| Faster expansion link (DMA receive, 2 Mbaud) | Device-tested | [#1](https://github.com/calvingunther66/flipware/pull/1) |
| VGM breakout GPIO expander, `vgm` CLI and JS bindings | Device-tested (integration build) | [#2](https://github.com/calvingunther66/flipware/pull/2) |
| VGM desktop over HDMI and USB display stream | Needs VGM firmware that isn't released yet | VGM fork |

How to use: [[Video Game Module]].

## Security

| Feature | Status | From |
| --- | --- | --- |
| Host sign-in: unlock the PIN lock over USB with a device-bound passkey | Partly device-tested (Mac works; Windows/Linux untested) | [#13](https://github.com/calvingunther66/flipware/pull/13) |

How to use: [[Host Sign-In]].

## Sub-GHz

| Feature | Status | From |
| --- | --- | --- |
| Compare two saved captures bit by bit | Device-tested | [#18](https://github.com/calvingunther66/flipware/pull/18) |
| Seed Capturer app (FAAC SLH, Genius, Erreka) | Untested on device | [#21](https://github.com/calvingunther66/flipware/pull/21) |
| Doorbell32, KeyFinder2 and Genius protocols | Untested on device | [#22](https://github.com/calvingunther66/flipware/pull/22) |

- **Compare:** open a saved signal, choose **Compare with...**, pick a second `.sub`. Details: [[Sub-GHz Capture Compare]].
- **Seed Capturer:** Apps > Sub-GHz > Seed Capturer. Details: [[Seed Capturer]].
- **Doorbell32 and KeyFinder2** decode, save and send like any static protocol. Both sit in the Sensors ignore group. The KeyFinder2 button colour is unverified upstream.
- **Genius** remotes read and resend as Genius instead of FAAC SLH. Add Manually has Genius 433MHz and Genius 868MHz. A `.sub` can carry an optional `Manufacture: Genius` line; files without it behave as before. The KeeLoq keystore was replaced with Unleashed unlshd-094's, so a quick check on a known KeeLoq remote is worth doing.

## NFC

| Feature | Status | From |
| --- | --- | --- |
| Hotel key card identifier (Onity, VingCard) | Untested on device | [#9](https://github.com/calvingunther66/flipware/pull/9) |
| Texkom intercom keys (TK13, TK15, TK17, MMBIT) | Untested on device | [#17](https://github.com/calvingunther66/flipware/pull/17) |
| Protocol screens loaded from the protocol plugins | Untested on device | [#23](https://github.com/calvingunther66/flipware/pull/23) |

- **Hotel cards:** NFC > Read a MIFARE Classic hotel key. If the read found the right sector key, a "Hotel Key Card" page names the lock system. It reads nothing extra and writes nothing. Saflok cards keep their own detailed parser.
- **Texkom:** NFC > Read picks up Texkom keys and shows the UID, the variant and whether the checksum is valid. Read keys can be saved and emulated. TK13, TK17 and MMBIT can be made under Add Manually, and `nfc emulate` in the CLI handles them. Writing to a blank is not supported.
- **Plugins:** the MIFARE Classic, Ultralight C, SLIX, FeliCa, DESFire and EMV extra screens now live in each protocol's `.fal` plugin and load only when that protocol is in use, so the NFC app is about half the size in RAM. **Install `nfc.fap` and the plugins together** (an update package does this). Old plugins refuse to load against the new app and show "Plugin Not Loaded".

## 125 kHz RFID and iButton

| Feature | Status | From |
| --- | --- | --- |
| Add Manually by facility code and card number | Partly device-tested (entry works; writing a card from FC/ID not yet checked) | [#10](https://github.com/calvingunther66/flipware/pull/10) |
| Write Targets: choose which blank chips a write may use | Untested on device | [#19](https://github.com/calvingunther66/flipware/pull/19) |

- **FC/ID entry:** 125 kHz RFID > Add Manually, pick a protocol, then **Enter FC/ID** (or **Enter Hex Data** as before). Fill in facility code, card number and any format fields with the number pad, then Save. Covers EM4100 (and its /32 Casi-Rusco and /16 variants), H10301, AWID, ioProx XSF, Pyramid, Gallagher, and HID Generic H10304, H10302, S10401, H10306 and Corp1000-35. Read and saved info screens also show the decoded FC and card number for those HID formats.
- **Write Targets:** iButton > Settings > **Write Blanks** (RW1990.1, RW1990.2, TM2004, TM01x) and 125 kHz RFID > Settings > **Write Chips** (T5577, EM4305). The CLI `ikey write` and `rfid write` follow the same setting. All on is the default and behaves as before. If no enabled chip can carry the key, the write screen says so instead of looping.

## Infrared

| Feature | Status | From |
| --- | --- | --- |
| Save the working signal from a universal remote | Partly device-tested (save menu checked) | [#11](https://github.com/calvingunther66/flipware/pull/11) |

While a universal remote sweep is paused, press **Down** to save the signal on screen: **Save as New Remote** (named, written to `/ext/infrared`) or **Add to Existing Remote** (appended to a `.ir` you pick). The button keeps the name being swept (Power, Vol_up and so on).

## Power, storage and system

| Feature | Status | From |
| --- | --- | --- |
| Battery cycle count in power info | No device test recorded | on `dev` |
| Battery health detail page | Device-tested (integration build) | [#2](https://github.com/calvingunther66/flipware/pull/2) |
| SD: no ext 3.3V power-cycle while a module is attached | No device test recorded | [#2](https://github.com/calvingunther66/flipware/pull/2) |
| Updater keeps system settings | No device test recorded | on `dev` |
| Performance pass: faster folders and SD, fewer freezes and OOM reboots | Device-tested | [#12](https://github.com/calvingunther66/flipware/pull/12) |
| Flipware boot and updater screens | No device test recorded | [#20](https://github.com/calvingunther66/flipware/pull/20) |

- **Battery health:** Settings > Power > Battery Info, then open the detail page from the summary page.
- **Performance:** on a test device the Games folder (171 apps) opened in 0.32 s instead of 0.75 s. App launches that run out of memory now fail with an error instead of rebooting.

## Housekeeping (no user-facing change)

- [#8](https://github.com/calvingunther66/flipware/pull/8): stop tracking the Wrangler cache.
- [#14](https://github.com/calvingunther66/flipware/pull/14): remove Momentum's AI assistant policy files.
- [#15](https://github.com/calvingunther66/flipware/pull/15): CI no longer fails on the API version difference with official firmware; Webhook workflow removed.
- [#16](https://github.com/calvingunther66/flipware/pull/16): fixes for 22 code scanning alerts.
- [#24](https://github.com/calvingunther66/flipware/pull/24): [ROADMAP.md](https://github.com/calvingunther66/flipware/blob/dev/ROADMAP.md).

## Sources

Several features are ports, credited in their pull requests and source files: Unleashed unlshd-093 and unlshd-094 (IR save, RFID FC/ID, Write Targets, Texkom, Seed Capturer, new Sub-GHz protocols), official firmware 1.5.1 (hotel card parser) and RogueMaster (NFC protocol plugins).
