# flipware-auth (Mac)

Signs your Flipper back in over USB with a key that never leaves your Mac's
Secure Enclave. Protocol and threat model: `documentation/HostAuth.md`.

**Status: skeleton.** It has not been built or run yet (it was written on Linux,
where there is no Swift toolchain, no CryptoKit Secure Enclave and no Flipper).
Expect small compile fixes on first build.

```sh
cd scripts/hostauth-mac
swift build -c release
.build/release/flipware-auth enroll --label "My MacBook"   # Flipper unlocked; press Enroll on it
.build/release/flipware-auth unlock                         # Flipper locked; Touch ID on the Mac
.build/release/flipware-auth watch                          # unlock whenever a locked Flipper is plugged in
.build/release/flipware-auth status
.build/release/flipware-auth forget                         # revoke on the Flipper and delete the local key
```

Options: `--port /dev/cu.usbmodemflip_NAME1` (default: the only Flipper found),
`--no-presence` at enroll (no Touch ID per unlock; any process running as you can
then use the key, so only use it if you accept that).

Keys are stored per Flipper (by its UID) under
`~/Library/Application Support/Flipware/hostauth/`. The file holds the Secure
Enclave's wrapped key blob, which is useless on any other Mac.
