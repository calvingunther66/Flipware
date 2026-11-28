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
.build/release/flipware-auth grant --hours 1.5              # one Touch ID, then unlocks need none for 1.5 h (max 2)
.build/release/flipware-auth revoke-grant                   # end the grant now
.build/release/flipware-auth forget                         # revoke on the Flipper, delete the local key
.build/release/flipware-auth forget --local-only            # delete the local key only (lost Flipper)
.build/release/flipware-auth revoke-all                     # revoke every computer on the Flipper
```

## Timed grant (unlock without a fingerprint each time)

`flipware-auth grant [--hours N]` asks for Touch ID once. For up to N hours (default 1, at
most 2), `flipware-auth unlock` and `watch` (yours, or run by an agent as you) sign in
without another fingerprint. Then it ends by itself.

- **Nothing is stored.** A small background process keeps the one approval in memory and signs
  only unlock assertions for your enrolled Flipper, through a Unix socket in the key folder
  (mode 0600, same user only). The private key stays in the Secure Enclave.
- **Fails closed.** It ends at the deadline (a clock that keeps counting while the Mac sleeps),
  after `--max-unlocks` unlocks (default 25), on `revoke-grant`, on logout or restart, or if the
  system drops the approval. After that, `unlock` asks for Touch ID again.
- `flipware-auth status` shows the grant and the time left. Every use is logged to
  `~/Library/Application Support/Flipware/hostauth/grant.log`.
- `unlock --no-grant` ignores an active grant and asks for Touch ID.
- `grant-selftest [--wait 330]` checks, with no Flipper, that one approval keeps signing (also
  past 5 minutes) and stops after it is ended. Run it once after building.

While a grant is active, any program running as you can unlock a Flipper that is plugged in.
Only grant when you want that.

Options:
- `--port /dev/cu.usbmodemflip_NAME1`: which Flipper to use. By default, the only one found.
- `--allow-password` (at enroll): accept Touch ID, an Apple Watch or the login password.
  - Without it, the key is tied to the Touch ID fingers enrolled today.
  - Adding or removing a finger invalidates the key. You then unlock with the PIN and enroll again.

Keys are stored per Flipper (by its UID) under
`~/Library/Application Support/Flipware/hostauth/`. The file holds the Secure
Enclave's wrapped key blob, which is useless on any other Mac.
