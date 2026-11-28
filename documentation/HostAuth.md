# Host sign-in: unlock the Flipper with a device-bound passkey

Status: **protocol v2, untested on hardware.**
- The firmware builds, and its core has host tests: known-answer vectors, a fuzzed parser and a fuzzed DER decoder under ASan/UBSan.
- The Mac tool builds, and its message format is checked against OpenSSL.
- The Windows/Linux tool passes its self-test, but has never been used with a real authenticator.
- See [What still needs hardware testing](#what-still-needs-hardware-testing); [HostAuthHardwareTest.md](HostAuthHardwareTest.md) has it as a step-by-step runbook with the expected output of each step.

## What "sign in" means

The Flipper has no accounts. What locks you out is the **PIN lock**.
- After a firmware update, or a reboot with "lock on boot", the device comes up PIN-locked.
- While it is PIN-locked, the firmware switches the USB serial CLI off.
- qFlipper, `fbt cli` and scripts all stop working until you type the PIN on the d-pad.

Host sign-in lets a computer you enrolled unlock that PIN lock over USB, using a passkey that is bound to that computer's hardware. The PIN keeps working exactly as before: this is an extra way in, not a replacement.

Out of scope:
- Bluetooth.
- Signing in to anything other than the PIN lock.
- Per-command authorization of an already unlocked CLI.

## Design: the Flipper is a tiny WebAuthn relying party

The computer proves possession of a key with a standard **WebAuthn assertion**, the same kind a website gets from a passkey.
- **Relying party:** RP ID `hostauth.flipware.local`, origin `https://hostauth.flipware.local`. The name is shaped like a domain only so that platform WebAuthn APIs accept it; nothing is ever looked up.

Three kinds of authenticator can produce the assertion:

| Computer | Authenticator | Tool |
| --- | --- | --- |
| Mac | Secure Enclave P-256 key, Touch ID on every use | `scripts/hostauth-mac` (`flipware-auth`, Swift) |
| Windows | Windows Hello, or a FIDO2 security key, through the Windows WebAuthn API | `scripts/hostauth-fido2` (Python, python-fido2) |
| Linux (and anything else) | FIDO2 security key over USB HID | `scripts/hostauth-fido2` |

**Only device-bound credentials are accepted.**
- The Flipper rejects any assertion with the backup-eligible (BE) or backed-up (BS) flag set, and the tools refuse to enroll such a credential.
- So a synced passkey (iCloud Keychain, Google Password Manager, a password-manager passkey) can never unlock a Flipper.
- That is what "hardware linked" means here: the private key exists in one secure element, and nowhere else.

### Why not Apple's passkey API on the Mac?

AuthenticationServices (`ASAuthorizationPlatformPublicKeyCredentialProvider`) can't be used here, for two reasons:
- **Associated domains:** it only lets an app make assertions for an RP ID whose website lists the app in `apple-app-site-association`, served over HTTPS (or a browser holding a special entitlement). A local relying party like this one can't satisfy that.
- **Syncing:** the passkeys it creates live in iCloud Keychain, which syncs them (BE=1, BS=1). The Flipper rejects those on purpose.

So the Mac tool creates a Secure Enclave key directly and emits the same WebAuthn format itself.

**Caveat (self-asserted flags):**
- On the Mac, the UP and UV flags in the authenticator data are set by `flipware-auth`, not by an authenticator.
- What makes them true is the key's Secure Enclave access control: the enclave will not sign without Touch ID (or the login password with `--allow-password`).
- A Windows Hello or security-key assertion carries flags set by the authenticator itself.

## Protocol

```
Computer                                  Flipper (stores only public keys)
--------                                  ---------------------------------
                                          PIN-locked; USB shell up in "locked mode":
                                          only `hostauth` (+ help/exit) exists
hostauth info              ------------>
                           <------------  hostauth-info v=2 uid=… rp=hostauth.flipware.local locked=1 …
hostauth challenge         ------------>  binding = tag, action, UID, epoch, counter, 32-byte nonce
                           <------------  hostauth-challenge v=2 rp=… challenge=<base64url(binding)> ttl=30
check v, rp, action and UID;
Touch ID / Windows Hello / key touch;
sign authData || SHA-256(clientDataJSON)
hostauth response <authData hex> <DER sig hex>
                           ------------>  rebuild clientDataJSON from its own binding,
                                          check rpIdHash and flags, verify ECDSA P-256
                                          against each enrolled key, check signCount
                           <------------  hostauth-ok unlocked fp=…
                                          desktop unlocks; shell restarts as the full CLI
```

### Binding (the WebAuthn challenge, 70 bytes)

| Field | Bytes | Value |
| --- | --- | --- |
| tag | 20 | `flipware-hostauth-v2` (domain separation) |
| action | 1 | 1 = unlock |
| uid_len | 1 | 8 |
| uid | 8 | `furi_hal_version_uid()` |
| epoch | 4, BE | current key-file epoch (see Storage) |
| counter | 4, BE | monotonic challenge counter, an RTC backup register, incremented before every challenge |
| nonce | 32 | hardware RNG (`furi_hal_random_fill_buf`) |

The challenge string is the unpadded base64url of those 70 bytes (94 characters).

### What gets signed

The Flipper builds the client data JSON itself, byte for byte:

```
{"type":"webauthn.get","challenge":"<94 chars>","origin":"https://hostauth.flipware.local","crossOrigin":false}
```

This is exactly what python-fido2's `CollectedClientData.create` produces; the host test checks it. The Flipper never parses JSON or CBOR from the host.

The authenticator data is exactly 37 bytes:
- `SHA-256("hostauth.flipware.local")`
- one flags byte
- signCount, 4 bytes, big-endian

The signature is ECDSA P-256 over `SHA-256(authenticatorData || SHA-256(clientDataJSON))`, sent as DER.

The Flipper accepts an assertion only if all of these hold:
- **rpIdHash:** matches, compared in constant time.
- **Flags:**
  - UP is set.
  - UV is set, if this key was enrolled with `uv=1`.
  - BE and BS are clear (not synced).
  - AT, ED and the reserved bits are clear, so the data is exactly 37 bytes.
- **Signature:**
  - It is strict DER: SEQUENCE of two INTEGERs, short-form lengths, minimal positive encoding, nothing trailing.
  - 1 ≤ r, s < n.
  - Verification is `mbedtls_ecdsa_verify` against a public key that was checked to be on the curve, both at enrollment and at every load.
- **signCount:** either 0 → 0 (an authenticator without a counter, such as the Secure Enclave), or strictly greater than the stored value. The new value is saved before the device unlocks, so a cloned security key is caught.

### CLI (`hostauth`)

The grammar is strict:
- Printable ASCII only.
- Single spaces between tokens.
- No leading or trailing space.
- At most 320 characters.
- Fixed-length hex.

Anything else is rejected before it reaches a handler.

| Command | When | Reply |
| --- | --- | --- |
| `info` | always | `hostauth-info v=2 uid= rp= locked= pin= keys= status=`. When unlocked, also one `hostauth-key fp= uv= label=` per key. |
| `challenge` | always | `hostauth-challenge v=2 rp= challenge= ttl=30`, or `hostauth-error backoff s=N` |
| `response <74 hex> <16..144 hex DER>` | after a challenge | `hostauth-ok unlocked fp=` / `hostauth-ok verified fp=`, or an error |
| `enroll <130 hex> <0\|1> <label>` | unlocked only | `hostauth-pending confirm-on-device fp=`, then `hostauth-ok enrolled fp=` once OK is held on the Flipper |
| `revoke <16 hex fingerprint\|all>` | unlocked only | `hostauth-ok revoked …` |

Error codes:
- **Command and challenge:** `usage`, `locked`, `backoff`, `counter-exhausted`, `no-challenge`, `expired`.
- **Rejected assertion:** `bad-signature`, `rp-mismatch`, `no-presence`, `no-verification`, `synced-credential`, `bad-flags`, `counter`.
- **Key file:** `no-keys`, `integrity`, `rollback`, `enclave`, `storage`.
- **Enrollment and revoke:** `invalid-key`, `duplicate`, `full`, `cancelled`, `not-found`.

`hostauth` is not a "parallel safe" command, so the shell refuses it while an app is open: it prints `this command cannot be run while an application is open`, and the tools recognise that text. This is deliberate:
- With Sub-GHz open the heap has about 4.7 KB free.
- A 5 KB command thread plus the ECDSA working memory would crash the device out of memory.
- The device can only be PIN-locked from the desktop, so this never gets in the way of an unlock.

### Rules on the Flipper

**Challenges:**
- There is at most one outstanding challenge.
- It is valid for 30 s (measured with the tick counter).
- It is used up by the first `response`, whatever the outcome, including a malformed `response` line.
- A new `challenge` kills the old one.

**Backoff:**
- **Allowance:** three free failures. After the third, `challenge` is refused for 30 s; each further failure doubles the wait, up to a maximum of 1 h.
- **What counts as a failure:** a bad, expired or malformed response.
- **Persistence:** the fail count and the time of the last failure (RTC timestamp) are kept in RTC backup registers, so the wait survives a reboot and a power-off/on.
- **Clock set backwards:** the wait restarts from "now" instead of trusting a future timestamp.
- **Reset:** any unlock, by the PIN or by a host, clears the backoff.
- **No PIN lockout:** the backoff never touches the PIN or its own failure counter, so a hostile USB host can delay host sign-in but can never lock you out of the PIN.

**Keys and enrollment:**
- Up to 4 enrolled keys. Labels are 1–29 printable ASCII characters.
- **Enrollment** is accepted only while the device is unlocked, and only after a physical confirmation:
  - **The prompt:** a full-screen prompt shows the label, the fingerprint (the first 8 bytes of SHA-256 over the 65-byte public key, in four groups) and whether unlocks need user verification.
  - **Confirming:** you confirm by **holding OK**; Back cancels, and the prompt times out after 60 s.
  - **Physical buttons only:** the prompt ignores every input event not marked as coming from the physical buttons. That excludes RPC/qFlipper key presses, the Video Game Module, and `input send` (which now marks its events as software).
  - **Recheck:** the firmware checks again that the device is still unlocked after the confirmation.
  - **Same fingerprint:** the tool on the computer prints the same fingerprint.
- **Failed responses** flash the red LED.

## Storage

**On the Flipper: `/int/.hostauth.keys`, 448 bytes**

- **Record (432 bytes):**
  - **Header:** magic `FWHA`, version 2, count, zero padding, and a 32-bit **epoch**.
  - **Four fixed key slots,** each holding:
    - the public key
    - an in-use byte
    - the policy flags (require UV)
    - the stored signCount
    - the label
  - **Strict parsing:** every reserved and unused byte must be zero, and every label must be NUL-terminated printable ASCII.
- **MAC (16 bytes):** AES-CBC-MAC over the fixed-length record.
  - It uses the device's **unique key**, in the STM32WB radio core's key store (slot 11), with fixed IV `hostauth-mac-v2`.
  - The firmware's `crypto` CLI no longer encrypts or decrypts with slot 11. That CLI was a chosen-IV oracle that let anyone with USB access compute this MAC.
  - That guard lives in the `crypto` CLI plugin (`/ext/apps_data/cli/plugins/cli_crypto.fal`), like the `input send` fix in `cli_input.fal`. A firmware-only flash (`./fbt flash_usb`) leaves the old plugins on the SD card; install the update package so they are replaced. An old plugin copied back brings the oracle back, which is the "malicious USB host while unlocked" case below.

**Anti-rollback:**
- An RTC backup register holds the current epoch. A file loads only if its epoch matches.
- Every save writes a fresh random epoch, so an older genuine copy of the file stops loading the moment the list changes: for example a copy in an updater backup, or one saved before a key was revoked. That copy reports `rollback`.
- Revoke-all and **Forget Computers** move the epoch first and then delete the file, so even an undeleted copy is dead.

**Crash safety:**
1. Write `.hostauth.tmp` with the new epoch and sync it.
2. Move the epoch register.
3. Rename over the main file.

If the device loses power between steps 2 and 3, the next load finds that only the tmp copy is current and promotes it. A tmp copy that is not current is deleted. The decision is a pure function (`host_auth_load_decide`) with its own test.

**Updates:**
- During an update the updater tars `/int` to the SD card and unpacks it afterwards.
- The epoch, counter, backoff, PIN and "locked" flag are all RTC backup registers, which survive the update reboot.
- So the device boots locked with its key file still current, and brings the USB shell up in locked mode. That is the "sign back in after an update" case: run `flipware-auth unlock`, or leave `flipware-auth watch` running.
- Editing the key file in the SD backup fails the MAC. Swapping in an older backup fails the epoch check.

**When the registers are cleared,** every enrollment stops loading (`rollback`), and you unlock with the PIN and enroll again. That happens when:
- the battery runs completely flat
- you do a factory reset or a wipe
- you enter recovery mode
- the "format after 10 wrong PINs" option fires

This fails safe.

**On the Mac:**
- Location: `~/Library/Application Support/Flipware/hostauth/<uid>.json`, mode 0600.
- Contents: the Secure Enclave's wrapped key blob (useless on any other Mac), the label, the fingerprint, the key policy, and the Touch ID state at enrollment.

**On Windows/Linux:**
- Location: `%APPDATA%\Flipware\hostauth-fido2\` or `~/.config/Flipware/hostauth-fido2/`.
- Contents: the credential ID, the fingerprint and the UV policy. The private key stays in Windows Hello or on the security key.

## Clients

### Mac (`flipware-auth`)

- **Key:** `SecureEnclave.P256.Signing.PrivateKey`. It can't be exported; only the enclave-wrapped blob leaves the enclave. It is created with `kSecAttrAccessibleWhenUnlockedThisDeviceOnly` and `.privateKeyUsage` plus:
  - **Default, `.biometryCurrentSet`:** Touch ID with the fingers enrolled today. The Flipper key is enrolled with `uv=1`.
  - **`--allow-password`, `.userPresence`:** Touch ID, Apple Watch or the login password. Also `uv=1`, since the login password is user verification. It survives fingerprint changes, but anyone who knows the Mac's password can approve an unlock.
  - There is no "no presence" mode any more. Every signature needs a fresh approval, and each one uses a new `LAContext` with `touchIDAuthenticationAllowableReuseDuration = 0`.
- **If Touch ID enrollment changes** (a finger added or removed) with the default policy, the Secure Enclave permanently invalidates the key:
  - The tool compares the stored Touch ID state with the current one before signing, and explains this instead of failing with an opaque error.
  - `status` shows it too.
  - **Recovery:** unlock the Flipper with its PIN, then run `flipware-auth forget` and `flipware-auth enroll`.
- **Before signing,** the tool decodes the binding and checks:
  - the protocol version
  - the RP ID
  - the domain tag
  - that the action is unlock
  - that the UID matches the Flipper this key was enrolled with

  So a device relaying another Flipper's challenge gets nothing.
- **Commands:** `enroll [--label] [--allow-password]`, `unlock [--no-grant]`, `watch`, `status`, `forget [--local-only]`, `revoke-all`, `grant [--hours N] [--uid] [--max-unlocks]`, `revoke-grant`, `grant-selftest`.

#### Timed grant

`grant` lets one Touch ID cover every unlock for up to 2 hours, so agents (or `watch`) can unlock without a fingerprint each time. The Flipper side is unchanged: it still gets a normal assertion from the enrolled key, and cannot tell a granted unlock from a fresh one. The `UV` flag now means "Touch ID at grant time".

- **Mechanism:** the background `grant-serve` process asks for Touch ID once with `LAContext.evaluateAccessControl` against the key's own access control, and keeps that approved context in memory. `unlock` sends the Flipper's challenge to it over a Unix socket (`~/Library/Application Support/Flipware/hostauth/grant.sock`, mode 0600, peer uid checked). It signs only after the same checks `unlock` does: the challenge decodes as a v2 binding, the action is unlock, and the UID is the enrolled Flipper's. The reply is the DER signature; the private key never leaves the Secure Enclave.
- **Why not a stored grant, a Keychain item or a second key:** a grant file, a Keychain item or a second Secure Enclave key without Touch ID would each be usable by any process running as you, with no way for the expiry to be enforced by anything stronger than a timestamp check in code. Keeping the Touch ID approval in one process's memory means the end of the grant is the end of that process, and the Secure Enclave asks for a fingerprint again. (Measured: `LAContext.invalidate()` alone does not stop a key object already bound to that context, so the grant relies on the process exiting, not on invalidating.) A second key would also have to be enrolled on the Flipper, with the Flipper unlocked and OK held, and would stay enrolled after the grant.
- **Limits:** at most 2 hours (a larger `--hours` is an error, not clamped), at most `--max-unlocks` unlocks (default 25), measured on `CLOCK_MONOTONIC`, which keeps counting during sleep and ignores clock changes.
- **Fails closed:** the server never signs after its deadline (checked on every request, with a 3 s cap on reading a request). If a signature fails because the system dropped the approval, the server stops (no prompt is raised unattended) and `unlock` falls back to asking for Touch ID. That fallback needs someone at the Mac, so an unattended agent waits. The key is "when unlocked, this device only", so a locked screen may end a grant early. If the server is not running, `unlock` behaves as before. Logout, restart, `kill` and `revoke-grant` all end it.
- **What it adds to the threat model:** while a grant is active, any program running as you can have an unlock signed for a Flipper that is plugged in and PIN-locked. That is the point of the feature, and it is limited to the window, to unlock assertions and to the enrolled Flipper. It does not help anyone without code execution as you on your logged-in Mac. A hostile program that waits for a grant could unlock a Flipper that has just been plugged in; keep grants short, and `revoke-grant` when done.
- **Status:** built and self-tested on a Mac without a Flipper: one approval kept signing for at least 330 s (past the 5 minute reuse limit). No real unlock through a grant has been run yet (runbook A16-A23). `grant-selftest --wait 330` repeats the check, and `grant` signs once as a check before it reports success.

### Windows / Linux (`scripts/hostauth-fido2`, untested)

- **Windows:** uses the system WebAuthn API through python-fido2's `WindowsClient`, so Windows Hello and security keys work with Windows' own prompts.
- **Elsewhere:** uses the first FIDO2 security key over USB HID, asking for its PIN when needed.
- **Enrollment:**
  - Refuses backup-eligible (synced) credentials and anything that is not ES256.
  - Enrolls `uv=1` if the authenticator verified the user at registration; `--require-uv` forces it.
  - The credential is non-resident (`residentKey: discouraged`), and its ID is kept on the computer.
- **Assertions:** use the same client data bytes as the Flipper (a custom `ClientDataCollector`), and a 25 s timeout so they land inside the 30 s challenge.
- **Self-test:** `selftest` checks the formatting against OpenSSL and `CollectedClientData` without hardware.

## Failure modes and recovery

| Situation | What to do |
| --- | --- |
| Lost or wiped computer, new Mac | Unlock with the **PIN**. Then **Settings > Desktop > PIN Code > Forget Computers**, or `hostauth revoke <fp>` / `revoke all` from any unlocked CLI. Enroll the new computer. No lockout is possible: the PIN always works. |
| Revoke from the computer | `flipware-auth forget` (or `revoke-all`) with the Flipper unlocked. `forget --local-only` deletes only the local key, when the Flipper is lost or its list is already reset. |
| Revoke on the device | **Forget Computers** (needs no PIN, since it only removes access). It also kills copies in updater backups. |
| Touch ID fingers changed (default Mac policy) | PIN, `forget`, `enroll`. Or enroll with `--allow-password` to avoid this. |
| `integrity` (edited file, other device's file, radio-core key store reset) | PIN, then `revoke all` (works even when the file doesn't verify), then enroll. |
| `rollback` (older backup restored, RTC registers cleared) | Same as above. |
| `backoff` | Wait (at most 1 h), or unlock with the PIN, which clears it. |
| `counter` (signCount went backwards) | Possibly a cloned security key. Revoke that key. |
| `counter-exhausted` | Only after 2³² challenges. A factory reset clears it; so does a full battery drain, which also drops enrollments. |
| App open on the Flipper | Go back to the desktop; `hostauth` doesn't run alongside apps. |

## Threat model

| Threat | Outcome |
| --- | --- |
| Stolen Flipper, no enrolled computer | It can't be unlocked over USB: that needs an assertion from an enrolled key. The PIN is still the only other way in. |
| Replay of a captured response | Fails. Each challenge has a fresh nonce and counter, is single use, and lasts 30 s. |
| Signature reused for another purpose or another Flipper | Fails. The domain tag, action, device UID, epoch and counter are all in the signed challenge, and the RP ID hash is checked. |
| Synced passkey | Rejected (BE/BS) at enrollment by the tools and at every sign-in by the Flipper. |
| Cloned security key | Caught by signCount once the original and the clone have both been used. Not for Secure Enclave keys, which have no counter, but those can't be cloned. |
| Malicious USB host while locked | **What it can reach:** only `hostauth info/challenge/response` (plus `help`/`exit`). Nothing is loaded from the SD card in locked mode, and `enroll`/`revoke` say `locked`. The parser takes fixed-format input only, and it is fuzzed together with the DER decoder in the host test. **What it can do:** trigger the backoff, delaying host sign-in by up to 1 h, which is cleared by a PIN unlock. Or burn the challenge counter, which takes billions of challenges. **What it can't do:** forge an unlock, or touch the PIN. |
| **Malicious USB host while unlocked** | **It can enroll its own key. This is a real limitation; read this row.** An unlocked Flipper gives USB the full CLI and RPC. A hostile computer doesn't need `hostauth` at all: it can copy a FAP to the SD card, start it, and that FAP can use the slot-11 key, set the epoch register and write a valid, current key file. The hold-OK prompt (physical buttons only) and the `crypto` CLI guard close the *protocol* paths, so a careless script or a confused tool can't enroll without your thumb. They don't stop a determined attacker with code execution. **Rule: never leave the Flipper unlocked on a computer you don't trust.** That was already true, because the unlocked CLI can also flash firmware. |
| Tampering with the key file in the SD backup during an update | Detected: the MAC needs the device's unique key, and the epoch must be current. Sign-in refuses (`integrity` / `rollback`); the PIN still works. |
| Relay (an attacker forwards your Flipper's challenge to your computer) | Needs code running on your computer, plus your approval. Touch ID / Windows Hello on every unlock is the defense, and the tool only signs challenges carrying the enrolled Flipper's UID. |
| Stolen Mac | Unlocking still needs Touch ID (or your password with `--allow-password`). Revoke on the Flipper. |
| **Physical attacker with DFU** | **Not covered by this feature, and not covered by the PIN either.** A button combination at reset (Left + Back, then let go of Back) puts the STM32 into its ROM DFU bootloader, which will flash any firmware: firmware that ignores the PIN, reads `/int`, or just unlocks. Host sign-in is no weaker than the PIN against this, and no stronger. Closing it would need read-out protection and a locked bootloader, a separate project that also changes how updates work. |
| Module on the expansion port (GPIO pins 13/14) while locked | A module talks RPC over UART, which upstream never checked against the PIN lock: one could open a session and send `DesktopUnlockRequest`. **What it can reach now:** with "Allow Module RPC While Locked" off (the default), its session only gets ping, `IsLocked`, the desktop status subscription and the screen stream, so a Video Game Module keeps showing the lock screen. Every other command, input, storage, apps and `DesktopUnlockRequest` included, gets `ERROR`. **What it can't do:** unlock, press buttons, read or write files, or start apps. The coprocessor channel on the same link only carries jobs the Flipper sends and their results, so a module can't start anything there. |
| Any RPC session whose owner isn't allowed while locked | `DesktopUnlockRequest` is refused from it whatever its owner (USB, BLE, module, unknown), even if it was opened before the lock. USB and BLE sessions are also closed when the PIN lock comes on. |
| Momentum's "Allow RPC while locked (USB)" | If it is on, the USB CLI stays fully open while locked and RPC can unlock without the PIN, so nothing here holds while locked. Keep it off. |
| "Allow BLE RPC While Locked" / "Allow Module RPC While Locked" | The same for BLE and for expansion modules: a session from that owner gets the full RPC while locked, `DesktopUnlockRequest` included. Keep them off. |

**New attack surface:** with a key enrolled, the USB shell now runs while the device is locked; before, USB serial was off. It exposes only:
- the line editor
- `help` and `exit`
- `hostauth`

## Do the STM32WB's key slots help? (and why the key isn't split into them)

The radio core (CPU2) keeps AES keys that the main core can load into the AES engine but never read:
- slots 1 to 10: factory keys
- slot 11: the per-device unique key
- slots 12 to 100: user keys, write-once

They only do symmetric AES (CBC, CTR, GCM). There is no ECC and no signing in there.

**Storing part of the key there doesn't help:**
- **The private key isn't on the Flipper at all.** It lives in the computer's Secure Enclave, Windows Hello or security key. The Flipper holds public keys, and a public key gains nothing from being stored secretly.
- **The useful part is already done.** The key file's MAC is computed inside the AES engine with slot 11, a key that never leaves CPU2. So a key file edited elsewhere (the SD backup, another device's `/int`) is rejected.
- **A dedicated user slot (12–100) would add nothing over slot 11.**
  - Writing it is irreversible: a slot can't be rewritten or erased, even by a factory reset.
  - Any code on the main core could use it, exactly like slot 11.
  - The only real gap was the `crypto` CLI, which let anyone with USB access use slot 11 with a chosen IV. That gap is now closed; slot 11 is refused there.
- **Splitting a symmetric secret** between the computer and a key slot (an HMAC scheme instead of passkeys) was rejected. The Mac's Secure Enclave only does P-256, so the computer's half of that secret would have to live outside its secure hardware. That is the opposite of what a hardware-bound passkey gives you.

## Files

- `applications/services/desktop/helpers/host_auth_core.{c,h}`: binding, client data, strict DER, assertion checks, record, parser, backoff and load decision. No Furi; host-tested.
- `applications/services/desktop/helpers/host_auth.{c,h}`: storage, MAC, epoch, RTC state, the enrollment prompt and the `hostauth` CLI.
- `targets/f7/furi_hal/furi_hal_rtc.h`: registers `HostAuthEpoch`, `HostAuthCounter`, `HostAuthFails` and `HostAuthFailTime` (backup registers 8–11).
- `applications/services/cli/cli_vcp.c` and `cli_vcp_locked.h`: the locked-mode USB shell.
- `applications/services/desktop/desktop.c`: keeps USB up in locked mode when a key is enrolled, and clears the backoff on unlock.
- `applications/services/input/input_cli.c`: `input send` events are marked as software.
- `applications/services/crypto/crypto_cli.c`: the slot-11 refusal.
- `applications/settings/desktop_settings/scenes/desktop_settings_scene_pin_menu.c`: Forget Computers.
- `tests/host/host_auth/`: host test (`./run.sh`; set `PYTHON` to an interpreter with `fido2` to cross-check the client data).
- `scripts/hostauth-mac/`: Swift CLI `flipware-auth`.
- `scripts/hostauth-fido2/`: Windows/Linux tool (untested).

## What still needs hardware testing

The runbook with commands and expected results is [HostAuthHardwareTest.md](HostAuthHardwareTest.md). What it covers:

**Locked-mode shell:**
- After a lock, and after a reboot with lock on boot, `help` lists only `hostauth`.
- Nothing is read from the SD card.
- An unknown command is refused.

**Unlock:**
- Unlock end to end from the Mac with Touch ID, including the shell restart after the unlock and the tool's re-sync.
- Cancel Touch ID: `cancelled`, and the challenge is used up.
- A response after 30 s gives `expired`.

**Enrollment prompt:**
- The text fits the 128 px screen; "Enroll this computer?" in FontPrimary may be too wide.
- Hold OK enrolls; Back cancels; 60 s times out.
- `input send ok long`, qFlipper key presses and VGM keys are all ignored.
- Auto-lock while the prompt is up: the prompt times out, or enrollment answers `locked`.

**Backoff:**
- Four bad responses, then a reboot: `backoff` remains, with the wait counting down.
- A PIN unlock clears it.

**Updates and rollback:**
- Sign-in still works after a real `updater_package` update; `/int` goes through the SD backup.
- Restoring an older `/int` backup after a revoke gives `rollback`.
- Forget Computers, then restoring the old file, gives `rollback`.

**Crash recovery:** pull power between the epoch move and the rename. Hard to time; it may need a debug build with a delay inserted.

**Memory:** with Sub-GHz open, `hostauth challenge` is refused with the "application is open" text and doesn't crash the device out of memory.

**Mac key policy:**
- Add a Touch ID finger: the next `unlock` explains the invalidated key, and `status` shows it.
- `--allow-password` enrollment works with the password prompt.

**`crypto` CLI:** after an update-package install (not `flash_usb` alone, which keeps the old SD plugins), `crypto encrypt 11 …` is refused, and other slots still work. The same goes for `input send` being ignored by the enrollment prompt.

**Windows/Linux tool:** run it at all, with Windows Hello and with a YubiKey or similar. Check the signCount handling with a security key that increments its counter, and that the counter is saved.
