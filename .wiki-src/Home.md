# Flipware wiki

Flipware is an independent, open-source custom firmware for the Flipper Zero. It is built on [Momentum](https://github.com/Next-Flip/Momentum-Firmware) (`dev` branch), which builds on the [Official Firmware](https://github.com/flipperdevices/flipperzero-firmware) and [Unleashed](https://github.com/DarkFlippers/unleashed-firmware).

Flipware is not affiliated with Flipper Devices Inc. or the Momentum team. "Flipper Zero" is a trademark of Flipper Devices Inc. and is used here only to say which device this firmware runs on.

## How it differs from Momentum

Flipware keeps everything Momentum has: asset packs, main menu and lock screen customisation, Bad KB, the extended Sub-GHz and NFC tools, and Momentum's app API (it still defines `FW_ORIGIN_Momentum`, so apps built for Momentum keep working).

On top of that it adds:

- **Video Game Module work:** a coprocessor offload service on the VGM's RP2040, a faster expansion link, a GPIO expander on the VGM breakout, and a JS module for the VGM. See [[Video Game Module]].
- **Host sign-in:** unlock the PIN lock over USB from an enrolled computer with a device-bound passkey (Mac Secure Enclave, Windows Hello or a FIDO2 key). See [[Host Sign-In]].
- **Sub-GHz:** compare two captures bit by bit ([[Sub-GHz Capture Compare]]), the [[Seed Capturer]] app, and the Doorbell32, KeyFinder2 and Genius protocols.
- **NFC, RFID, iButton and IR:** hotel card identifier, Texkom intercom keys, RFID entry by facility code and card number, choosing which blank chips a write may use, and saving the working code from a universal IR remote.
- **Performance:** faster app folders and SD access, fewer freezes and fewer out-of-memory reboots, and NFC protocol screens loaded as plugins.
- **Power:** battery cycle count and a battery health page.

The full list, with how to use each one and where it came from, is on [[Features]].

## Status

- There are no tagged releases yet. Until there are, build an update package yourself: see [[Installing and Updating]] and [[Building from Source]].
- Many features are built and unit-tested but not yet checked on a device. [[Features]] marks each one.
- The plan for the next year is in [ROADMAP.md](https://github.com/calvingunther66/flipware/blob/dev/ROADMAP.md), with one tracking issue per stage (label `roadmap`).

## Links

- Repository: https://github.com/calvingunther66/flipware
- Issues: https://github.com/calvingunther66/flipware/issues
- Roadmap board: https://github.com/users/calvingunther66/projects/3
- Changelog inherited from Momentum: [CHANGELOG.md](https://github.com/calvingunther66/flipware/blob/dev/CHANGELOG.md)
- In-repo docs: [documentation/](https://github.com/calvingunther66/flipware/tree/dev/documentation)

## License

GPL-3.0, like the firmware it is based on. Bundled apps and libraries keep their own licenses, found next to their code.
