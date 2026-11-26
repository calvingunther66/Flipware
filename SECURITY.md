# Security policy

## Reporting a vulnerability

Please report security problems **privately**, not in a public issue or pull
request.

Use GitHub's private reporting: open the
[Security tab](https://github.com/calvingunther66/Flipware/security) of this
repository and choose **Report a vulnerability**
([direct link](https://github.com/calvingunther66/Flipware/security/advisories/new)).
Only the maintainer can see the report.

Include the firmware build (version shown in Settings, or the commit), the
steps to reproduce, and what an attacker gains. A proof of concept or an
[emulator](https://github.com/calvingunther66/FlipperEmu) transcript helps.

What to expect:

- A first answer within about 7 days. Flipware has one maintainer, so this is a
  target, not a guarantee.
- A fix on `dev` and in the next release, with a changelog line. The reporter is
  credited unless they ask not to be.
- Please keep the details private until a fix is released, and give a fix a
  reasonable time before you publish.

## What is in scope

Flipware's own security-relevant code. These matter most:

| Area | Where | What counts |
| --- | --- | --- |
| **PIN lock** | `applications/services/desktop`, `applications/services/rpc` | Getting a CLI, RPC session, file access or app launch on a PIN-locked device without the PIN, other than what the settings `allow_locked_rpc_usb`, `allow_locked_rpc_ble` and `allow_locked_rpc_expansion` deliberately allow (all off by default). |
| **Host sign-in** | [documentation/HostAuth.md](documentation/HostAuth.md), `scripts/hostauth-*` | Unlocking without an enrolled, device-bound credential; accepting a synced passkey; replaying or forging an assertion; parser bugs in the assertion or DER decoding. |
| **U2F** | `applications/main/u2f` | Using a key without the user's button press, leaking or reusing key material, counter rollback, bad handling of malformed requests. |
| **Expansion port and Video Game Module link** | `applications/services/expansion`, `documentation/ExpansionModules.md` | A module or a USB host gaining access that the lock should deny; memory-safety bugs in the frame and coprocessor parsers. |

Memory-safety bugs in anything reachable over USB, the expansion port or
Bluetooth are in scope even if they are not in a table row.

## What is out of scope

- Attacks that need the device to be unlocked and in the attacker's hands for
  as long as they like. A DFU reflash or a debug probe on a device you hold
  gets around the PIN lock, and Flipware does not claim otherwise.
- Bugs in the official firmware, Momentum or Unleashed that Flipware has not
  changed. Report those to their projects; tell us too if Flipware ships the
  bug, and we will pick up the upstream fix.
- Bundled third-party apps and libraries. Report them to their authors; we will
  update the copy here.
- The inherited radio features (Sub-GHz, NFC, RFID, infrared, Bad KB) working
  as designed, including against other people's locks and cards.
- Findings that need a modified firmware or a modified module.

## Supported versions

There is no Flipware release yet. Until `flpw-001` ships, only the tip of
`dev` is supported. After that, the latest release and `dev`.
