# Host Sign-In (unlock the PIN lock from your computer)

Status: **partly device-tested.** The Mac flow works on the maintainer's Flipper. The Windows and Linux tool has only passed its self-test, and the full hardware checklist in [documentation/HostAuth.md](https://github.com/calvingunther66/flipware/blob/dev/documentation/HostAuth.md#what-still-needs-hardware-testing) is still open. From [#13](https://github.com/calvingunther66/flipware/pull/13).

## What it does

After an update, or a reboot with "lock on boot", the Flipper comes up PIN-locked and its USB serial CLI is off, so qFlipper, `fbt cli` and scripts stop working until you type the PIN on the d-pad.

Host sign-in lets a computer you enrolled unlock that PIN lock over USB with a **device-bound passkey**:

| Computer | Authenticator | Tool |
| --- | --- | --- |
| Mac | Secure Enclave key, Touch ID on every unlock | `scripts/hostauth-mac` (`flipware-auth`) |
| Windows | Windows Hello or a FIDO2 security key | `scripts/hostauth-fido2` (untested) |
| Linux | FIDO2 security key over USB | `scripts/hostauth-fido2` (untested) |

The PIN keeps working exactly as before. This is an extra way in, not a replacement. Synced passkeys (iCloud Keychain, Google Password Manager, password managers) are always refused.

## Set it up on a Mac

You need the Flipper **unlocked** and on the desktop (no app open).

```sh
cd scripts/hostauth-mac
swift build -c release
.build/release/flipware-auth enroll --label "My MacBook"
```

The Flipper shows a full-screen prompt with the label and a fingerprint. Check it matches the fingerprint the Mac prints, then **hold OK** on the Flipper. Only the physical buttons count: qFlipper, the VGM and `input send` can't confirm it.

Then, whenever the Flipper is PIN-locked:

```sh
.build/release/flipware-auth unlock    # Touch ID on the Mac
.build/release/flipware-auth watch     # unlock automatically whenever a locked Flipper is plugged in
.build/release/flipware-auth status
```

Options:
- `--port /dev/cu.usbmodemflip_NAME1` picks a Flipper when more than one is connected.
- `--allow-password` at enroll also accepts an Apple Watch or the login password. Without it, the key is tied to the Touch ID fingers enrolled today, and adding or removing a finger invalidates it (unlock with the PIN, then `forget` and `enroll` again).

## Set it up on Windows or Linux (untested)

```sh
pip install "fido2>=2.0" pyserial
python scripts/hostauth-fido2/flipware_auth_fido2.py selftest
python scripts/hostauth-fido2/flipware_auth_fido2.py enroll --label "Desk PC"   # hold OK on the Flipper
python scripts/hostauth-fido2/flipware_auth_fido2.py unlock
```

On Linux the security key needs udev access.

## Removing a computer

- **From the Flipper:** Settings > Desktop > PIN Setup > **Forget Computers**. It needs no PIN, since it only removes access.
- **From the computer:** `flipware-auth forget` (Flipper unlocked), or `forget --local-only` if the Flipper is lost.
- **Everything at once:** `flipware-auth revoke-all`, or `hostauth revoke all` from an unlocked CLI.

## Good to know

- Install Flipware with a full **update package**. Part of the protection lives in the `crypto` and `input` CLI plugins on the SD card, and a firmware-only flash leaves the old ones in place.
- `hostauth` doesn't run while an app is open. Go back to the desktop first.
- After three failed attempts, new challenges wait 30 s, doubling up to 1 hour. A PIN unlock clears the wait. Host sign-in can never lock you out of the PIN.
- Up to 4 computers can be enrolled.
- If the Flipper's battery goes completely flat, or after a factory reset, enrollments stop working. Unlock with the PIN and enroll again.
- Keep "Allow USB RPC While Locked" (Momentum settings > Interface > Lockscreen) **off**. With it on, the lock doesn't hold over USB at all.
- Never leave the Flipper unlocked on a computer you don't trust. An unlocked Flipper gives that computer the full CLI, which is enough to enroll its own key.
- Like the PIN itself, this doesn't protect against someone with physical access using DFU mode to flash other firmware.
- A related open issue is [#33](https://github.com/calvingunther66/flipware/issues/33): a module on the GPIO header may be able to open an RPC session while the device is PIN-locked.

The protocol, storage format and full threat model are in [documentation/HostAuth.md](https://github.com/calvingunther66/flipware/blob/dev/documentation/HostAuth.md).
