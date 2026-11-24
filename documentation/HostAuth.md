# Host sign-in (unlock the Flipper from your Mac)

Status: **draft, untested on hardware.** Firmware side builds and its core has host
tests; the Mac tool is a skeleton that has not been compiled yet.

## What "sign in" means

The Flipper has no accounts. What locks you out is the **PIN lock**: after a firmware
update (or a reboot with "lock on boot") the device comes up PIN-locked, and while it is
PIN-locked the firmware switches the USB serial CLI off. qFlipper, `fbt cli` and scripts
all stop working until you type the PIN on the d-pad.

Host sign-in lets a computer you enrolled unlock that PIN lock over USB by proving it
holds a private key. The PIN keeps working exactly as before; this is an extra way in,
not a replacement.

Out of scope for v1: Bluetooth, signing in to anything other than the PIN lock,
per-command authorization of an already unlocked CLI.

## How it works

```
Mac (Secure Enclave key)                  Flipper (stores only the public key)
------------------------                  ------------------------------------
                                          PIN-locked; USB shell up in "locked mode":
                                          only `hostauth` (+ help/exit) exists
hostauth info              ------------>
                           <------------  hostauth-info v=1 uid=… locked=1 …
hostauth challenge         ------------>  32-byte nonce from the hardware RNG,
                           <------------  hostauth-challenge v=1 action=unlock uid=… nonce=… ttl=30
check v/action/uid, Touch ID,
sign SHA-256(message)
hostauth response <r||s>   ------------>  verify ECDSA P-256 against each enrolled key
                           <------------  hostauth-ok unlocked fp=…
                                          desktop unlocks; shell restarts as the full CLI
```

### Signed message (62 bytes)

```
"flipware-hostauth-v1"   20 bytes, domain tag
action                    1 byte, 1 = unlock
uid_len                   1 byte, 8
uid                       8 bytes, furi_hal_version_uid()
nonce                    32 bytes, from the challenge
```

Signature: ECDSA over SHA-256 of that message, P-256, sent as raw `r || s` (64 bytes,
CryptoKit's `rawRepresentation`). Public keys travel as 65-byte uncompressed X9.63
points (`04 || X || Y`, CryptoKit's `x963Representation`). Fingerprint: first 8 bytes of
SHA-256 over the 65-byte key, shown on both screens at enrollment.

### CLI (`hostauth`)

| Command | When | Reply |
| --- | --- | --- |
| `info` | always | `hostauth-info v=1 uid= locked= pin= keys= status=`; when unlocked also `hostauth-key fp= label=` per key |
| `challenge` | always | `hostauth-challenge …`, or `hostauth-error backoff s=N` |
| `response <128 hex>` | after a challenge | `hostauth-ok unlocked/verified fp=` or `hostauth-error …` |
| `enroll <130 hex> <label>` | unlocked only | `hostauth-pending confirm-on-device`, then `hostauth-ok enrolled fp=` after a press on the Flipper |
| `revoke <fp16 hex\|all>` | unlocked only | `hostauth-ok revoked …` |

Error codes: `usage`, `locked`, `backoff`, `no-challenge`, `expired`, `bad-signature`,
`no-keys`, `integrity`, `enclave`, `storage`, `invalid-key`, `duplicate`, `full`,
`cancelled`, `not-found`.

### Rules on the Flipper

- One outstanding challenge, valid 30 s, consumed by the first response whatever its
  outcome. A new `challenge` replaces the old one.
- Three free failures, then `challenge` is refused for 30 s, doubling to at most 1 h.
  The counter lives in RAM, so a hostile USB host can delay host sign-in but can't lock
  you out of the PIN, and a reboot clears it. It is separate from the PIN's own counter
  for the same reason.
- Failed responses flash the red LED.
- Up to 4 enrolled keys, labels printable ASCII, 29 characters.
- Enrolling needs a press of **Enroll** on the Flipper's screen, which shows the label
  and fingerprint. The Mac prints the same fingerprint.
- Revoke from the Mac (`flipware-auth forget`), from any CLI (`hostauth revoke`), or on
  the device: **Settings > Desktop > PIN Code > Forget Computers** (no PIN needed, since it
  only removes access).

### Storage and updates

The Flipper keeps `/int/.hostauth.keys` (416 bytes): a fixed-size record of up to four
public keys and labels, plus a 16-byte AES-CBC-MAC computed with the device's **unique
key** in the STM32WB radio core's key store (slot 11, fixed IV `hostauth-mac-v1`).

During an update the updater tars `/int` to the SD card and unpacks it afterwards, so the
key file survives updates and the record crosses the SD card on the way. The PIN and the
"locked" flag live in RTC backup registers, which also survive, so the device boots
locked and, with a key enrolled, brings the USB shell up in locked mode. That is the
"sign back in after an update" case: `flipware-auth unlock`, or leave
`flipware-auth watch` running.

The Mac keeps one JSON file per Flipper UID in
`~/Library/Application Support/Flipware/hostauth/`, holding the Secure Enclave's wrapped
key blob (useless on any other Mac), the label and the fingerprint.

## Mac side

- Key: `SecureEnclave.P256.Signing.PrivateKey`, non-exportable, access control
  `.privateKeyUsage` plus `.userPresence` (Touch ID, Apple Watch or login password per
  unlock), `kSecAttrAccessibleWhenUnlockedThisDeviceOnly`. `--no-presence` drops the
  Touch ID prompt for unattended use; any process running as you can then use the key.
- Before signing, the tool checks protocol version, action and that the UID matches the
  Flipper this key was enrolled with. It never signs a message it didn't build itself.
- `watch` polls `/dev/cu.usbmodemflip_*` every 2 s and runs `unlock` on each newly
  attached Flipper. A launchd agent and IOKit notifications are the obvious next step.

## Threat model

| Threat | Outcome |
| --- | --- |
| Stolen Flipper, no Mac | Can't unlock over USB: needs a signature from an enrolled key. The PIN is still the only other way in. |
| Replay of a captured response | Fails: fresh 32-byte nonce each time, single use, 30 s. |
| Signature reused for something else | Fails: domain tag, action byte and device UID are in the signed message. |
| Malicious USB host while locked | Can ask for challenges and send garbage. Can't forge. Can trigger the backoff (delay host sign-in up to 1 h, until reboot). Can't touch the PIN counter. |
| Malicious USB host while unlocked | Already has the full CLI, as today, which can also flash firmware. The Enroll press stops a confused or casual host from adding a key, but **not** a determined one: the unlocked CLI can write `/int` and use the key store directly. Unlocked CLI = full control; that is unchanged. |
| Tampering with the key file in the SD backup during an update | Detected: the MAC needs the device's unique key. Sign-in refuses (`integrity`); the PIN still works; re-enroll. |
| Relay (attacker forwards your Flipper's challenge to your Mac) | Needs code running on your Mac, or `watch` signing for a device that claims your Flipper's UID. Touch ID per unlock is the defense, which is why presence is on by default. |
| Stolen Mac | Unlocking still needs Touch ID or your login password. Revoke on the Flipper (Forget Computers). |
| Lost Mac key (new Mac, wiped Mac, enclave reset) | No lockout: use the PIN, then `revoke all` or Forget Computers and enroll again. |
| Radio stack reinstalled and the key store reset | The MAC no longer verifies; same as above. |
| Physical attacker with DFU | Not covered, and not covered today either: DFU can flash firmware that ignores the PIN. This feature is no weaker than the PIN, and no stronger. Read-out protection would be a separate project. |
| Momentum's "Allow RPC while locked (USB)" | If on, the USB CLI stays fully open while locked and RPC can unlock without the PIN. Host sign-in adds nothing then. Keep it off. |

New attack surface: with a key enrolled, the USB shell now runs while locked (before, USB
serial was off). It exposes only the line editor, `help`, `exit` and `hostauth`, loads
nothing from the SD card, and parses fixed-length hex only.

## Do the STM32WB's key slots help?

The radio core (CPU2) keeps AES keys that the main core can load into the AES engine but
never read: factory slots 1 to 10, the per-device unique key in slot 11, user slots 12 to
100. They only do symmetric AES (CBC, CTR, GCM). There is no ECC and no signing in there,
so they can't do the ECDSA check, and a public key gains nothing from being stored
secretly.

What they do add is the MAC above: the key file is bound to this one Flipper, so a copy
edited elsewhere (the SD backup, another device's `/int`) is rejected. What they don't add:
the main core still runs the verify, and any code running on the main core can use slot 11
(the `crypto` CLI can). A symmetric design (an HMAC key shared between the Mac and a key
slot) was rejected because the Mac's Secure Enclave only does P-256, so the Mac's half of
that secret would have to live outside the enclave.

## Files

- `applications/services/desktop/helpers/host_auth_core.{c,h}`: message, verify, record, parsing (no Furi; host-tested)
- `applications/services/desktop/helpers/host_auth.{c,h}`: storage, MAC, rate limit, `hostauth` CLI
- `applications/services/cli/cli_vcp.c`, `cli_vcp_locked.h`: locked-mode USB shell
- `applications/services/desktop/desktop.c`: keeps USB up in locked mode when a key is enrolled
- `applications/settings/desktop_settings/scenes/desktop_settings_scene_pin_menu.c`: Forget Computers
- `tests/host/host_auth/`: host test (`./run.sh`), signatures made independently with OpenSSL
- `scripts/hostauth-mac/`: Swift CLI `flipware-auth`

## Not done yet

- Test on a Flipper: locked-mode shell, unlock, the shell restart after unlock, enrollment dialog, Forget Computers, behaviour across a real update.
- Build and run the Mac tool; store the blob in the Keychain instead of a file; launchd agent.
- Decide whether changing or removing the PIN should also forget enrolled computers.
