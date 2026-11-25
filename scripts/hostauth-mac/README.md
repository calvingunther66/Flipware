# flipware-auth (Mac)

Unlocks your PIN-locked Flipper over USB with a key that never leaves your Mac's
Secure Enclave. Each unlock needs Touch ID. The output is a standard WebAuthn
assertion, which the Flipper verifies. Protocol and threat model:
`documentation/HostAuth.md`.

**Status:** `swift build` works. The message format is checked against OpenSSL
using a software key. The Secure Enclave and Touch ID paths, and the whole flow
against a Flipper, have not been run yet.

```sh
cd scripts/hostauth-mac
swift build -c release
.build/release/flipware-auth enroll --label "My MacBook"   # Flipper unlocked; hold OK on it
.build/release/flipware-auth unlock                         # Flipper locked; Touch ID on the Mac
.build/release/flipware-auth watch                          # unlock whenever a locked Flipper is plugged in
.build/release/flipware-auth status
.build/release/flipware-auth forget                         # revoke on the Flipper, delete the local key
.build/release/flipware-auth forget --local-only            # delete the local key only (lost Flipper)
.build/release/flipware-auth revoke-all                     # revoke every computer on the Flipper
```

Options:
- `--port /dev/cu.usbmodemflip_NAME1`: which Flipper to use. By default, the only one found.
- `--allow-password` (at enroll): accept Touch ID, an Apple Watch or the login password.
  - Without it, the key is tied to the Touch ID fingers enrolled today.
  - Adding or removing a finger invalidates the key. You then unlock with the PIN and enroll again.

Keys are stored per Flipper (by its UID) under
`~/Library/Application Support/Flipware/hostauth/`. The file holds the Secure
Enclave's wrapped key blob, which is useless on any other Mac.
