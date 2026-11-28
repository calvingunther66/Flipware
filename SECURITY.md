# Security policy

## Reporting a vulnerability

Report security problems privately, not in a public issue or pull request.

Open the [Security tab](https://github.com/calvingunther66/Flipware/security)
of this repository and choose **Report a vulnerability**
([direct link](https://github.com/calvingunther66/Flipware/security/advisories/new)).
Only the maintainer can see the report.

Please include:

- the firmware build (the version in Settings, or the commit);
- the steps to reproduce it;
- what an attacker gains.

You will get an answer as soon as the maintainer can give one. Flipware has one
maintainer, so there is no fixed deadline. A fix goes into `dev` and the next
release with a changelog line, and you are credited unless you ask not to be.
Please keep the details private until a fix is out.

## What counts

The parts of Flipware where a flaw matters most:

- **The PIN lock.** Getting a CLI, RPC session, files or apps on a locked
  device without the PIN, other than through the `allow_locked_rpc_*` settings,
  which are off by default.
- **Host sign-in** ([documentation/HostAuth.md](documentation/HostAuth.md)).
  Unlocking without an enrolled device-bound credential, or forging or
  replaying a sign-in.
- **U2F.** Using a key without a button press, leaking key material, or
  mishandling malformed requests.
- **The expansion port and Video Game Module link.** A module or USB host
  getting access the lock should deny.
- **Memory-safety bugs** in anything reachable over USB, the expansion port or
  Bluetooth.

## What doesn't

- Anything that needs the device unlocked, or physical access with a DFU
  reflash or debug probe. The PIN lock doesn't claim to stop those.
- Bugs in the firmware Flipware was built from (official, Momentum, Unleashed)
  or in bundled third-party apps that Flipware hasn't changed. Tell us if
  Flipware ships the bug.
- The inherited radio features working as designed.

## Supported versions

There is no Flipware release yet, so only the tip of `dev` is supported. After
`flpw-001`, the latest release and `dev`.
