# Changelog

All notable changes to Flipware. Each release gets its own section, newest
first. Changes that are merged but not yet in a release are under
[Unreleased](#unreleased).

Flipware started from Momentum `dev` at `33671b9` plus ports from Unleashed
(`unlshd-093` and `unlshd-094`). Momentum's own changelog from that point is kept
for reference in [documentation/upstream/MomentumChangelog.md](documentation/upstream/MomentumChangelog.md);
it is not part of Flipware's release notes.

**Adding a line:** put one line under Unreleased, in Added, Updated, Fixed or
Removed, with the PR number. Write it for someone who uses the firmware. Credit
the original author when the change is a port, for example
`UL: Add Doorbell32 protocol (by @author)`.

**Releasing:** rename `## Unreleased` to `## flpw-001 - YYYY-MM-DD` (use the
tag you are about to push), add a new empty `## Unreleased` above it, merge that,
then push the tag. Building the tag publishes the GitHub release by itself: the
update `.tgz` and `.zip`, the full `.dfu`, the SDK zip and `SHA256SUMS`, with this
section as the notes (`.github/workflows/release.yml`,
`.github/workflow_data/release.py`). A tag ending in `-rcN`, for example
`flpw-001-rc1`, is published as a pre-release and may be cut before the rename: it
uses its own section, then the final tag's section, then `## Unreleased`. A final
tag with no section of its own fails the release job.

## Unreleased

### Added
- Install guide (`documentation/Install.md`): qFlipper, SD card, moving over from Momentum and from the official firmware, what an update changes, recovery; and `documentation/ReleaseChecklist.md`, the release candidate and device pass every release goes through (#44)
- Build: host tests for the host sign-in code, the 125 kHz RFID Add Manually formats, Sub-GHz capture compare and release notes now run in CI on every pull request, and locally with `./fbt test_host` (#40)

### Updated
- Flipware no longer follows Momentum or any other upstream: `documentation/UpstreamSync.md` is now a short independence note (last synced point, how to port a fix by hand), and the ReadMe, contributing guide, security policy and roadmap say so (#45)
- Website: a Download button for the latest release and a link to the install guide (#44)
- Docs: step-by-step hardware test runbook for host sign-in, `documentation/HostAuthHardwareTest.md` (#99)
- CI: every Build now boots the firmware in the FlipperEmu emulator and runs its checks: boot and radios, an update install, the VGM coprocessor, BLE pairing, the serial ports, the PIN lock and regression checks for the firmware bugs fixed here (FW-01, FW-02, FW-06). A pull request that breaks one of them turns red, with the device's logs and screenshots attached (#97)
- CI: every Build writes a flash and RAM report (image size, free flash below the radio stack, `.data` and `.bss`, change against `dev`, symbols that grew most) as a job summary and a pull request comment, and fails when free flash drops below the budget or a PR grows the image by more than 512 bytes without the `flash-budget` label (#96)
- Releases: pushing a `flpw-NNN` tag publishes the GitHub release by itself, with the update `.tgz` and `.zip`, the full `.dfu`, the SDK zip, `SHA256SUMS` and the changelog section as notes; `-rcN` tags are pre-releases (#94)

### Fixed
- Firmware bug burndown (roadmap 1.5, `documentation/FirmwareBugTriage.md`): an RPC request with no path no longer restarts the Flipper; the NFC shell's `dump` of an ISO14443-3B card no longer freezes it, and `dump -k` of an Ultralight C uses the key you gave; the NFC shell's `scanner` no longer restarts on a Type 4 Tag, an EMV card or an NTAG4xx (its `dump` of an EMV card still can, FW-05); reading an ST25TB card of an unknown kind is refused instead of restarting; closing the NFC app while it reads, scans or emulates no longer restarts the Flipper; the desktop no longer restarts when its slideshow is drawn too early after an update; a lost thread flag no longer freezes an NFC read; the USB-UART bridge keeps its second port; DESFire record files are dumped; a 32-bit Radio Key card is emulated with its own facility code; Sub-GHz transmit right after infrared transmit no longer loses its start; the ISO14443A listener works after ISO15693 emulation; `crypto encrypt` no longer encrypts leftover memory; the Bluetooth serial status is four bytes of its own; the JavaScript serial module sends its last two bytes (#101)

## flpw-001 - 2026-11-26

The first Flipware release: everything since the fork from Momentum.

### Added
- Video Game Module (VGM):
  - Coprocessor offload: the Flipper can hand compute jobs to the VGM's RP2040 over the expansion link, with automatic local fallback when no module is attached or a job fails. `storage crc32 <path>` is the demo job. The link is slower than the Flipper's own CPU for per-byte work, so it only pays off for jobs with a small request and a lot of computing (#1)
  - GPIO expander task for the VGM breakout, with a `vgm` CLI command and JS bindings (#2)
- Desktop: unlock the PIN lock over USB with a device-bound passkey (Mac Secure Enclave with Touch ID, Windows Hello or a FIDO2 key). The PIN keeps working. Design, limits and what still needs hardware testing: `documentation/HostAuth.md` (#13)
- Desktop: new setting "Allow Module RPC While Locked" (Interface > Lockscreen), off by default (#85)
- Power settings: battery health detail page (#2)
- NFC:
  - Hotel key cards (Onity, VingCard) show a "Hotel Key Card" page naming the lock system (based on the official firmware 1.5.1 `hotels` parser) (#9)
  - Texkom intercom keys TK13, TK15, TK17 and MMBIT: read, save, emulate, Add Manually for TK13, TK17 and MMBIT (UL: by @mishamyte) (#17)
- RFID 125 kHz: Add Manually by facility code and card number for EM4100 (and EM4100/32, EM4100/16), H10301, AWID, ioProx XSF, Pyramid, Gallagher and the HID Generic formats; Read and Saved screens show the decoded FC and card number for the added HID formats (UL unlshd-094) (#10)
- iButton and RFID: Settings > Write Targets chooses which blank chips a write may try (RW1990.1, RW1990.2, TM2004, TM01x for iButton; T5577, EM4305 for RFID). The CLI follows the same setting (#19)
- Infrared: save the working signal from a universal remote, as a new remote or added to an existing one (UL unlshd-093) (#11)
- Sub-GHz:
  - Compare two saved captures bit by bit (Saved signal > Compare with...) (#18)
  - Doorbell32 and KeyFinder2 protocols; Genius remotes read and resend as Genius instead of Faac SLH (UL unlshd-094) (#22)
  - Apps: Seed Capturer collects Fix and Hop pairs from FAAC SLH, Genius and Erreka remotes, receive-only (UL unlshd-094, original app by @xMasterX) (#21)
- Project: `ROADMAP.md` (#24), `CONTRIBUTING.md`, `SECURITY.md`, `CODEOWNERS` (#87), a project wiki linked from the ReadMe (#83), and this changelog (#36)

### Updated
- Boot screen and updater screen say Flipware instead of Momentum (#20)
- Passport screen says FLPW·ID instead of MNTM·ID (#86)
- ReadMe, website and docs say what the code does: the VGM desktop and USB display stream are labelled preview, and menu paths say Flipware (#86)
- Performance: faster SD access, app folders load in about half the time (171-app Games folder: 0.75 s to 0.32 s), fewer UI freezes, and lower peak RAM when loading large apps (#12)
- NFC: the extra screens of each protocol (MIFARE Classic dictionary attack, Ultralight C unlock, SLIX unlock, FeliCa, DESFire and EMV more-info and others) load only while that protocol is in use, shrinking the NFC app's code by about 48% (RogueMaster PR 1073, by Mykhailo Shevchuk) (#23)
- Build: official-firmware API version mismatch is a warning, not a failure (#15); each PR commit runs one Build and one Lint, Build writes the DFU size and free flash to the job summary, and the toolchain is cached (#84)

### Fixed
- Security: while the PIN lock is on, an expansion module could open an RPC session and unlock the Flipper. A locked session now only gets screen mirroring and lock status, and `DesktopUnlockRequest` is refused for it (#85)
- Fix 22 real bugs from code scanning: an EMV card could make the reader read past its buffer, an NFC tag could hang the vCard parser, a JS array of more than 255 elements hung the Flipper, plus smaller overflow and NULL-write fixes in FeliCa, mJS, Sub-GHz, the message queue and exFAT size checks (#16)
- Stability: a full USB event queue during a CLI command no longer reboots the device, `storage read_chunks` stops when the host disconnects, loader launch failures report an error instead of rebooting, plus three memory leaks (#12)
- SD: I/O error retries no longer power-cycle the 3.3V rail while an expansion module is attached (#2)
- RFID: writing a protocol that no enabled chip can carry shows a message instead of looping forever (#19)
- RFID: Pyramid parity check no longer reads past its buffer (#10)
- Repository: stop tracking a Wrangler cache file that held Cloudflare account details, and ignore `.wrangler/` (#8)

### Removed
- Momentum's instructions telling AI coding assistants to refuse work on the repository (`AGENTS.md`, `.cursor/rules/momentum.mdc`, `.github/copilot-instructions.md`) (#2, #14)
- Upstream funding links and donation text (`.github/FUNDING.yml`, the Support block in the release notes template) (#87)
- Momentum's Webhook workflow, which failed on every push (#15)
- Momentum's changelog, moved to `documentation/upstream/MomentumChangelog.md` (#36)
