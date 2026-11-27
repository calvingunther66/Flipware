# Host sign-in: hardware test runbook

This is the checklist at the end of [HostAuth.md](HostAuth.md) turned into steps you can run
with a real Flipper (roadmap 1.6, issue #43). It needs a person at the keyboard: Touch ID,
Windows Hello and a security key all ask for a physical touch, and so does the Flipper.

It is finished when **enrol, unlock, revoke and back-off all work on a real Flipper from
macOS and from at least one of Windows or Linux**. Paste the results table at the bottom
into issue #43.

## Before you start

- A Flipper running this firmware, installed with the **update package** (not only
  `./fbt flash_usb`, which leaves old plugins on the SD card), and an SD card in it.
- A PIN set (Settings > Desktop > PIN Setup). "Lock on boot" on makes the reboot steps easier.
- Know how to lock it: from the desktop, Up opens the menu, then Lock with PIN.
- The Flipper's serial port. On a Mac: `ls /dev/cu.usbmodemflip_*`. On Linux: `ls /dev/ttyACM*`.
  A tool picks the only Flipper by itself; with two, pass `--port`.
- A way to type raw commands, for the error and back-off steps:
  `screen /dev/cu.usbmodemflip_NAME1 115200` (leave with Ctrl-A, then `\`).
  Close it before running a tool: only one program can hold the port.
- Write down the firmware version (`./fbt` build or `Settings > About`) in the results.

## A. Mac (`scripts/hostauth-mac`)

```sh
cd scripts/hostauth-mac && swift build -c release
alias fa=.build/release/flipware-auth
```

| # | Do | Expect |
| --- | --- | --- |
| A1 | Flipper unlocked. `fa status` | `Flipper <uid>: unlocked, PIN set, 0 computer(s) enrolled` and `This Mac has no key for it.` |
| A2 | `fa enroll --label "Test Mac"` | Touch ID prompt, then `Fingerprint: xxxx-xxxx-xxxx-xxxx`. |
| A3 | Look at the Flipper | A full-screen prompt with the label and **the same fingerprint**. Check that the text fits the 128 px screen (known risk: "Enroll this computer?"). |
| A4 | Tap OK (not hold), then Back, then wait 60 s, each as its own enrolment | Tap does nothing; Back gives `cancelled`; 60 s gives a timeout. None enrols. |
| A5 | Enrol again and **hold OK** | `Enrolled with Flipper <uid>.` |
| A6 | `fa status` | `1 computer(s) enrolled` and `enrolled on the Flipper: yes`. |
| A7 | Lock the Flipper (desktop, Up, Lock with PIN). Open `screen`, type `help` | Only `hostauth` (and help/exit) is listed. `storage list /ext` is refused. |
| A8 | `fa unlock` | Touch ID prompt, then `Flipper <uid> unlocked.` The desktop opens; the shell restarts and the tool re-syncs. |
| A9 | Lock again, `fa unlock`, **cancel** Touch ID | `cancelled`. Run `fa unlock` straight after: it must ask for a new challenge and work (the cancelled one is used up). |
| A10 | Lock, start `fa unlock`, do nothing for 30 s, then approve | `expired`. A fresh `fa unlock` works. |
| A11 | Lock, `fa watch`, unplug and replug the Flipper | It unlocks by itself after Touch ID. Ctrl-C. |
| A12 | Unlocked: `fa revoke-all`, then `fa status` | `Every computer was revoked on Flipper <uid>.`, then `0 computer(s) enrolled`. Lock the Flipper: with no key enrolled the USB shell is off again, so `fa unlock` finds no Flipper (this is the same as before host sign-in). |
| A13 | Enrol again, then `fa forget`. Lock the Flipper; `fa unlock` | `Revoked on the Flipper and deleted this Mac's key for Flipper <uid>.` Locked, the USB shell is off again, as in A12. |

Mac-only key policy (needs a fingerprint change, so do these last):

| # | Do | Expect |
| --- | --- | --- |
| A14 | `fa enroll --allow-password`; lock; `fa unlock`; use the login password | Works, with the login-password prompt. |
| A15 | Default enrolment, then add a Touch ID finger in System Settings | `fa status` says the fingers changed; `fa unlock` explains the invalidated key instead of failing opaquely. Unlock with the PIN, `fa forget`, `fa enroll` recovers. |

## B. Windows or Linux (`scripts/hostauth-fido2`)

```sh
pip install "fido2>=2.0" pyserial
python flipware_auth_fido2.py selftest        # must print: selftest ok
alias fa="python flipware_auth_fido2.py"       # PowerShell: Set-Alias; or just type it out
```

Linux needs udev access to the key (the libfido2 udev rules). Windows needs Windows 10
19H1 or later; no admin rights are needed with the Windows WebAuthn API.

Run steps A1 to A13 with this tool, where the authenticator is Windows Hello (Windows) or a
FIDO2 security key (both). The output wording differs a little: `Fingerprint:`,
`Enrolled with Flipper <uid> (user verification on every unlock).`, `Flipper <uid> unlocked.`
Also check:

| # | Do | Expect |
| --- | --- | --- |
| B1 | Enrol with a security key that has a PIN | PIN prompt, touch, fingerprint matches the Flipper's. |
| B2 | Enrol with a credential that is synced (a Windows Hello passkey that syncs, or a password-manager passkey, if you can make one) | The tool says it is backup-eligible and refuses; nothing is sent to the Flipper. |
| B3 | Unlock three times with a security key that counts uses | Each works. `signCount` goes up each time and the Flipper accepts it. |
| B4 | Unlock with a security key that always reports counter 0 | Works (0 to 0 is allowed). |
| B5 | `fa forget --local-only`, then `fa unlock` | `No key for Flipper <uid> on this computer. Run enroll first.` |

If B1 to B4 fail inside python-fido2 (a traceback, not a `Flipper` message), copy the
traceback into the results: that is a tool bug to fix, not a firmware one.

## C. On the Flipper only

| # | Do | Expect |
| --- | --- | --- |
| C1 | Enrol a key. Lock. In `screen`: `help`, `storage list /ext`, `input send ok short`, an unknown word | `help` lists only `hostauth`; the others are refused. Nothing is read from the SD card. |
| C2 | Reboot with lock on boot (hold Left + Back), then `help` | Same locked shell after the reboot. |
| C3 | While the enrol prompt is up, run `input send ok long` in a second terminal, and press keys from qFlipper | Ignored; only the physical buttons count. |
| C4 | Start an enrol, let the Flipper auto-lock while the prompt shows | The prompt times out, or enrolment answers `locked`. Nothing is enrolled. |
| C5 | Locked, in `screen`: `hostauth challenge`, then `hostauth response 0000...0000 3006020101020101` (74 zeros, then the 16 hex digits). Repeat 3 times | Each gives `hostauth-error rp-mismatch`. After the third, `hostauth challenge` gives `hostauth-error backoff s=30` (counting down). |
| C6 | During the back-off, reboot the Flipper, then `hostauth challenge` | Still `backoff`, and the wait continues from where it was. |
| C7 | Unlock with the PIN, lock again, `hostauth challenge` | Challenge issued again: a PIN unlock clears the back-off. |
| C8 | Run a real update with `./fbt updater_package` and install it. Then `fa unlock` | The Flipper boots locked, the key still works. |
| C9 | Take a copy of `/int/.hostauth.keys` (from the update backup on the SD card), revoke the key, restore the copy, reboot, `fa unlock` | `hostauth-error rollback`. Same after Forget Computers (Settings > Desktop > PIN > Forget Computers) plus restoring the old file. |
| C10 | Unlocked, open Sub-GHz on the Flipper, then in `screen` run `hostauth challenge` | `this command cannot be run while an application is open`; the Flipper does not crash or reset. |
| C11 | After an update-package install: `crypto` with key slot 11 (see `crypto` for the usage line), then the same with another slot | Slot 11 is refused; other slots still work. |
| C12 | Crash recovery: power cut between the epoch move and the rename | Hard to time. Skip it unless you have a debug build with a delay; say so in the results. |

## What to change when it passes

Remove the "untested" wording only for what you ran and saw pass:

- `documentation/HostAuth.md`: the **Status** block at the top, the heading
  "Windows / Linux (`scripts/hostauth-fido2`, untested)", the Files entry
  "(untested)", and the ticked items in "What still needs hardware testing".
- `scripts/hostauth-mac/README.md` and `scripts/hostauth-fido2/README.md`: the **Status** paragraphs
  (and "(untested)" in the second title).
- `scripts/hostauth-fido2/flipware_auth_fido2.py`: the `STATUS: UNTESTED` paragraph in the docstring.
- `CHANGELOG.md`: the sentence about what still needs hardware testing.

If a step fails, fix it, then run that step and the ones after it again.

## Results (paste into issue #43)

```
Firmware:            (version / commit)
Flipper:             (hardware revision, PIN on, lock on boot on/off)
Mac:                 (macOS version, model, Touch ID yes/no)  A1-A15:
Windows/Linux:       (OS, authenticator make/model)           A1-A13, B1-B5:
Flipper only:        C1-C12:
Failures / notes:
```
