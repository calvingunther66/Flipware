# flipware-auth for Windows and Linux (untested)

Unlocks a PIN-locked Flipper with a device-bound FIDO2 credential:
- **Windows:** Windows Hello, or a security key, through the Windows WebAuthn API.
- **Linux:** a FIDO2 security key over USB.

Synced passkeys are refused, because the Flipper rejects them.
See `documentation/HostAuth.md`.

**Status:** written against python-fido2 2.x. `selftest` passes. It has never
been run with Windows Hello, a real security key or a Flipper.

```sh
pip install "fido2>=2.0" pyserial
python flipware_auth_fido2.py selftest
python flipware_auth_fido2.py enroll --label "Desk PC"    # Flipper unlocked; hold OK on it
python flipware_auth_fido2.py unlock                       # Flipper locked
python flipware_auth_fido2.py status
python flipware_auth_fido2.py forget [--local-only]
python flipware_auth_fido2.py revoke-all
```

On Linux the security key needs udev access (most distributions ship rules for
common keys, or see the libfido2 udev rules).
