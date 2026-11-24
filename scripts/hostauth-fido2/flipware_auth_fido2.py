#!/usr/bin/env python3
"""flipware-auth for Windows and Linux: unlock a PIN-locked Flipper over USB with a
device-bound FIDO2 credential (Windows Hello, or a FIDO2 security key on Windows
and Linux). Protocol and threat model: documentation/HostAuth.md.

STATUS: UNTESTED on real hardware. It is written against python-fido2 2.x and the
Flipper's v2 protocol, and `selftest` checks the parts that need no authenticator,
but no Windows Hello machine or security key has been used with it yet.

Needs: pip install "fido2>=2.0" pyserial
"""
import argparse
import base64
import hashlib
import json
import os
import platform
import struct
import sys
import time
from pathlib import Path

RP_ID = "hostauth.flipware.local"
ORIGIN = "https://hostauth.flipware.local"
TAG = b"flipware-hostauth-v2"
BINDING_LEN = len(TAG) + 2 + 8 + 4 + 4 + 32
PROTOCOL = "2"
FLAG_UP, FLAG_UV, FLAG_BE, FLAG_BS = 0x01, 0x04, 0x08, 0x10
FLIPPER_VID, FLIPPER_PID = 0x0483, 0x5740


class Fail(Exception):
    pass


# ======================
# Pieces that need no I/O
# ======================


def b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode()


def b64url_decode(text: str) -> bytes:
    if "=" in text:
        raise ValueError("padded")
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def client_data_json(challenge_b64: str) -> bytes:
    """Byte for byte what the Flipper rebuilds (and what CollectedClientData.create makes)."""
    return json.dumps(
        {"type": "webauthn.get", "challenge": challenge_b64, "origin": ORIGIN, "crossOrigin": False},
        separators=(",", ":"),
    ).encode()


def parse_binding(raw: bytes):
    """tag || action || uid_len || uid || epoch(BE) || counter(BE) || nonce"""
    if len(raw) != BINDING_LEN or not raw.startswith(TAG):
        return None
    i = len(TAG)
    action, uid_len = raw[i], raw[i + 1]
    if uid_len != 8:
        return None
    uid = raw[i + 2 : i + 10]
    epoch, counter = struct.unpack(">II", raw[i + 10 : i + 18])
    return {"action": action, "uid": uid.hex(), "epoch": epoch, "counter": counter}


def fingerprint(pub65: bytes) -> str:
    return hashlib.sha256(pub65).digest()[:8].hex()


def grouped(fp: str) -> str:
    return "-".join(fp[i : i + 4] for i in range(0, len(fp), 4))


def sanitize_label(label: str) -> str:
    text = "".join(c if 0x20 <= ord(c) <= 0x7E else "?" for c in label).strip()
    return text[:29].strip() or "computer"


def cose_to_uncompressed(cose) -> bytes:
    # COSE_Key EC2: 1 (kty) = 2, 3 (alg) = -7 (ES256), -1 (crv) = 1 (P-256), -2 x, -3 y
    if cose.get(1) != 2 or cose.get(3) != -7 or cose.get(-1) != 1:
        raise Fail("The authenticator did not make a P-256 (ES256) key; the Flipper only verifies P-256.")
    x, y = cose[-2], cose[-3]
    if len(x) != 32 or len(y) != 32:
        raise Fail("Unexpected key size from the authenticator.")
    return b"\x04" + x + y


def check_device_bound(flags: int):
    if flags & (FLAG_BE | FLAG_BS):
        raise Fail(
            "This credential is backup-eligible (a synced passkey). The Flipper only accepts "
            "device-bound credentials, so it would refuse every sign-in. Use a security key, "
            "or a Windows Hello credential that is not synced."
        )


# ======
# Serial
# ======


class Flipper:
    def __init__(self, port=None):
        import serial
        from serial.tools import list_ports

        if port is None:
            found = [p.device for p in list_ports.comports() if p.vid == FLIPPER_VID and p.pid == FLIPPER_PID]
            if not found:
                raise Fail("No Flipper found on USB.")
            if len(found) > 1:
                raise Fail("Several Flippers found, pick one with --port: " + ", ".join(found))
            port = found[0]
        self.ser = serial.Serial(port, 115200, timeout=0.05)
        self.ser.dtr = True  # the Flipper starts its shell on DTR
        self.buf = ""
        self.sync()

    def _read_until_prompt(self, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            idx = self.buf.find(">: ")
            if idx >= 0:
                text, self.buf = self.buf[:idx], self.buf[idx + 3 :]
                lines = []
                for line in text.replace("\r", "\n").split("\n"):
                    clean = "".join(ch for ch in line if ch.isprintable())
                    # drop ANSI colour codes
                    while "[" in clean and "m" in clean[clean.find("[") :]:
                        start = clean.find("[")
                        end = clean.find("m", start)
                        if end < 0 or not all(c in "0123456789;" for c in clean[start + 1 : end]):
                            break
                        clean = clean[:start] + clean[end + 1 :]
                    clean = clean.strip()
                    if clean:
                        lines.append(clean)
                return lines
            self.buf += self.ser.read(512).decode("utf-8", "replace")
        raise Fail("Timed out waiting for the Flipper prompt.")

    def sync(self):
        self.ser.write(b"\r")
        self._read_until_prompt(5)

    def command(self, line, timeout=5.0):
        self.ser.write(line.encode() + b"\r")
        lines = self._read_until_prompt(timeout)
        if any("cannot be run while an application is open" in l for l in lines):
            raise Fail("An app is open on the Flipper. Go back to the desktop, then try again.")
        replies = []
        for l in lines:
            if not l.startswith("hostauth-"):
                continue
            parts = l.split(" ")
            kind = parts[0][len("hostauth-") :]
            fields, words = {}, []
            for p in parts[1:]:
                if "=" in p:
                    k, v = p.split("=", 1)
                    fields[k] = v
                else:
                    words.append(p)
            if " label=" in l:
                fields["label"] = l.split(" label=", 1)[1]
            replies.append((kind, words, fields))
        return replies

    def info(self):
        replies = self.command("hostauth info")
        info = next((f for k, w, f in replies if k == "info"), None)
        if not info or "uid" not in info:
            raise Fail("This Flipper doesn't answer `hostauth info`. Is it running Flipware with host sign-in?")
        if info.get("v") != PROTOCOL or info.get("rp") != RP_ID:
            raise Fail(f"This Flipper speaks host sign-in v{info.get('v')}; this tool speaks v{PROTOCOL}.")
        info["fps"] = [f.get("fp") for k, w, f in replies if k == "key"]
        return info


def error_text(replies):
    for kind, words, fields in replies:
        if kind == "error":
            return " ".join(words + [f"{k}={v}" for k, v in fields.items()])
    return "no answer"


# ==========
# Local keys
# ==========


def store_dir() -> Path:
    if platform.system() == "Windows":
        base = Path(os.environ.get("APPDATA", Path.home()))
    else:
        base = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
    return base / "Flipware" / "hostauth-fido2"


def store_path(uid: str) -> Path:
    return store_dir() / f"{uid}.json"


def store_save(uid, record):
    d = store_dir()
    d.mkdir(parents=True, exist_ok=True)
    try:
        os.chmod(d, 0o700)
    except OSError:
        pass
    path = store_path(uid)
    tmp = path.with_suffix(".tmp")
    tmp.write_text(json.dumps(record, indent=2))
    os.replace(tmp, path)


def store_load(uid):
    try:
        return json.loads(store_path(uid).read_text())
    except FileNotFoundError:
        raise Fail(f"No key for Flipper {uid} on this computer. Run `enroll` first.")


# =============
# Authenticator
# =============


def make_client():
    """Windows: the system WebAuthn API (Windows Hello and security keys, with
    Windows' own prompts). Elsewhere: the first FIDO2 security key on USB."""
    from fido2.client import ClientDataCollector, UserInteraction
    from fido2.webauthn import CollectedClientData, PublicKeyCredentialCreationOptions

    class Collector(ClientDataCollector):
        # Our own client data: never derived from anything but the challenge bytes
        def collect_client_data(self, options):
            if isinstance(options, PublicKeyCredentialCreationOptions):
                cd = CollectedClientData.create(type="webauthn.create", challenge=options.challenge, origin=ORIGIN)
            else:
                cd = CollectedClientData(client_data_json(b64url(options.challenge)))
            return cd, RP_ID

    if platform.system() == "Windows":
        from fido2.client.windows import WindowsClient

        if WindowsClient.is_available():
            return WindowsClient(client_data_collector=Collector()), "windows-webauthn"

    from fido2.client import Fido2Client
    from fido2.hid import CtapHidDevice

    class Prompt(UserInteraction):
        def prompt_up(self):
            print("Touch your security key...")

        def request_pin(self, permissions, rp_id):
            import getpass

            return getpass.getpass("Security key PIN: ")

    dev = next(CtapHidDevice.list_devices(), None)
    if dev is None:
        raise Fail("No FIDO2 security key found (and no Windows WebAuthn API).")
    return Fido2Client(dev, client_data_collector=Collector(), user_interaction=Prompt()), "ctap-hid"


# ========
# Commands
# ========


def cmd_enroll(args):
    from fido2.webauthn import (
        AuthenticatorSelectionCriteria,
        PublicKeyCredentialCreationOptions,
        PublicKeyCredentialParameters,
        PublicKeyCredentialRpEntity,
        PublicKeyCredentialType,
        PublicKeyCredentialUserEntity,
        ResidentKeyRequirement,
        UserVerificationRequirement,
    )

    flipper = Flipper(args.port)
    info = flipper.info()
    if info.get("locked") == "1":
        raise Fail("Unlock the Flipper with its PIN first, then enroll.")
    if store_path(info["uid"]).exists():
        raise Fail(f"This computer already has a key for Flipper {info['uid']}. Run `forget` first.")

    client, kind = make_client()
    options = PublicKeyCredentialCreationOptions(
        rp=PublicKeyCredentialRpEntity(id=RP_ID, name="Flipware host sign-in"),
        user=PublicKeyCredentialUserEntity(id=bytes.fromhex(info["uid"]), name=f"flipper-{info['uid']}"),
        challenge=os.urandom(32),  # no attestation is checked: the Flipper's on-device prompt is the trust step
        pub_key_cred_params=[PublicKeyCredentialParameters(type=PublicKeyCredentialType.PUBLIC_KEY, alg=-7)],
        authenticator_selection=AuthenticatorSelectionCriteria(
            resident_key=ResidentKeyRequirement.DISCOURAGED,
            user_verification=UserVerificationRequirement.PREFERRED,
        ),
        timeout=60000,
    )
    print("Approve the new credential on this computer...")
    reg = client.make_credential(options)
    auth_data = reg.response.attestation_object.auth_data
    check_device_bound(auth_data.flags)
    cred = auth_data.credential_data
    pub = cose_to_uncompressed(cred.public_key)
    fp = fingerprint(pub)
    uv = 1 if (auth_data.flags & FLAG_UV) else 0
    if args.require_uv:
        uv = 1

    label = sanitize_label(args.label or platform.node() or "computer")
    print(f"Fingerprint: {grouped(fp)}")
    print("Check that the Flipper shows the same fingerprint, then hold OK on it (Back cancels).")
    replies = flipper.command(f"hostauth enroll {pub.hex()} {uv} {label}", timeout=75)
    ok = next((f for k, w, f in replies if k == "ok"), None)
    if not ok or ok.get("fp") != fp:
        raise Fail("Enrollment failed: " + error_text(replies))
    store_save(
        info["uid"],
        {
            "version": 2,
            "uid": info["uid"],
            "label": label,
            "fingerprint": fp,
            "credential_id": b64url(cred.credential_id),
            "uv": uv,
            "client": kind,
        },
    )
    print(f"Enrolled with Flipper {info['uid']} ({'user verification' if uv else 'touch only'} on every unlock).")


def assertion_to_response(auth_data_bytes: bytes, signature: bytes, client_data: bytes, challenge_b64: str):
    """Check what the authenticator returned and format the Flipper's `response` line."""
    if bytes(client_data) != client_data_json(challenge_b64):
        raise Fail("The client data changed on the way; not sending it.")
    if len(auth_data_bytes) != 37:
        raise Fail("The authenticator returned extensions; the Flipper only takes plain 37-byte authenticator data.")
    if not (auth_data_bytes[32] & FLAG_UP):
        raise Fail("The authenticator did not confirm user presence.")
    return f"hostauth response {auth_data_bytes.hex()} {signature.hex()}"


def cmd_unlock(args):
    from fido2.webauthn import (
        PublicKeyCredentialDescriptor,
        PublicKeyCredentialRequestOptions,
        PublicKeyCredentialType,
        UserVerificationRequirement,
    )

    flipper = Flipper(args.port)
    info = flipper.info()
    if info.get("locked") != "1":
        print(f"Flipper {info['uid']} is already unlocked.")
        return
    record = store_load(info["uid"])

    replies = flipper.command("hostauth challenge")
    ch = next((f for k, w, f in replies if k == "challenge"), None)
    if not ch:
        raise Fail("No challenge: " + error_text(replies))
    try:
        raw = b64url_decode(ch.get("challenge", ""))
    except ValueError:
        raw = b""
    binding = parse_binding(raw)
    # Only sign unlock challenges for the Flipper this credential was enrolled with
    if (
        ch.get("v") != PROTOCOL
        or ch.get("rp") != RP_ID
        or not binding
        or binding["action"] != 1
        or binding["uid"] != info["uid"]
        or record["uid"] != info["uid"]
    ):
        raise Fail("Unexpected challenge from the Flipper; not signing it.")

    client, _ = make_client()
    options = PublicKeyCredentialRequestOptions(
        challenge=raw,
        rp_id=RP_ID,
        allow_credentials=[
            PublicKeyCredentialDescriptor(
                type=PublicKeyCredentialType.PUBLIC_KEY, id=b64url_decode(record["credential_id"])
            )
        ],
        user_verification=UserVerificationRequirement.REQUIRED
        if record.get("uv")
        else UserVerificationRequirement.DISCOURAGED,
        timeout=25000,  # inside the Flipper's 30 s challenge lifetime
    )
    print("Approve the sign-in on this computer...")
    result = client.get_assertion(options).get_response(0)
    line = assertion_to_response(
        bytes(result.response.authenticator_data),
        result.response.signature,
        result.response.client_data,
        ch["challenge"],
    )
    try:
        replies = flipper.command(line, timeout=10)
    except Fail:
        replies = []
    if any(k == "ok" for k, w, f in replies):
        print(f"Flipper {info['uid']} unlocked.")
        return
    if any(k == "error" for k, w, f in replies):
        raise Fail("Sign-in refused: " + error_text(replies))
    flipper.sync()  # unlocking restarts the shell
    if flipper.info().get("locked") == "1":
        raise Fail("Sign-in did not go through.")
    print(f"Flipper {info['uid']} unlocked.")


def cmd_status(args):
    flipper = Flipper(args.port)
    info = flipper.info()
    print(
        f"Flipper {info['uid']}: {'locked' if info.get('locked') == '1' else 'unlocked'}, "
        f"PIN {'set' if info.get('pin') == '1' else 'not set'}, {info.get('keys')} computer(s) enrolled"
    )
    if store_path(info["uid"]).exists():
        rec = store_load(info["uid"])
        enrolled = "unknown while locked" if info.get("locked") == "1" else ("yes" if rec["fingerprint"] in info["fps"] else "no")
        print(f"This computer: {grouped(rec['fingerprint'])} via {rec.get('client')}, enrolled on the Flipper: {enrolled}")
    else:
        print("This computer has no key for it.")


def cmd_forget(args):
    if args.local_only:
        # Lost Flipper, or its key list was already reset: just drop the local record.
        # The credential itself lives in the authenticator; remove it there too
        # (Windows: Settings > Accounts > Passkeys; security key: its manager app).
        removed = 0
        for p in store_dir().glob("*.json") if store_dir().exists() else []:
            if args.uid is None or p.stem == args.uid:
                p.unlink()
                removed += 1
        print(f"Deleted {removed} local record(s).")
        return
    flipper = Flipper(args.port)
    info = flipper.info()
    if info.get("locked") == "1":
        raise Fail("Unlock the Flipper first (or use --local-only).")
    rec = store_load(info["uid"])
    replies = flipper.command(f"hostauth revoke {rec['fingerprint']}")
    if not any(k == "ok" for k, w, f in replies):
        raise Fail("The Flipper did not revoke the key (" + error_text(replies) + "); the local record was kept.")
    store_path(info["uid"]).unlink()
    print(f"Revoked on the Flipper and deleted the local record for Flipper {info['uid']}.")


def cmd_revoke_all(args):
    flipper = Flipper(args.port)
    info = flipper.info()
    if info.get("locked") == "1":
        raise Fail("Unlock the Flipper first.")
    replies = flipper.command("hostauth revoke all")
    if not any(k == "ok" for k, w, f in replies):
        raise Fail("Revoke failed: " + error_text(replies))
    try:
        store_path(info["uid"]).unlink()
    except FileNotFoundError:
        pass
    print(f"Every computer was revoked on Flipper {info['uid']}.")


def cmd_selftest(args):
    """Checks the formatting against an independent software signer (no authenticator)."""
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    from fido2.webauthn import CollectedClientData

    for _ in range(50):
        uid = os.urandom(8)
        raw = TAG + bytes([1, 8]) + uid + struct.pack(">II", 7, 9) + os.urandom(32)
        ch = b64url(raw)
        b = parse_binding(b64url_decode(ch))
        assert b and b["uid"] == uid.hex() and b["epoch"] == 7 and b["counter"] == 9
        assert bytes(CollectedClientData.create(type="webauthn.get", challenge=raw, origin=ORIGIN)) == client_data_json(ch)
        key = ec.generate_private_key(ec.SECP256R1())
        ad = hashlib.sha256(RP_ID.encode()).digest() + bytes([FLAG_UP | FLAG_UV]) + struct.pack(">I", 3)
        cd = client_data_json(ch)
        sig = key.sign(ad + hashlib.sha256(cd).digest(), ec.ECDSA(hashes.SHA256()))
        line = assertion_to_response(ad, sig, cd, ch)
        _, _, ad_hex, sig_hex = line.split(" ")
        key.public_key().verify(
            bytes.fromhex(sig_hex),
            bytes.fromhex(ad_hex) + hashlib.sha256(client_data_json(ch)).digest(),
            ec.ECDSA(hashes.SHA256()),
        )
        pub = key.public_key().public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
        assert cose_to_uncompressed({1: 2, 3: -7, -1: 1, -2: pub[1:33], -3: pub[33:]}) == pub
    for bad in (ad[:32] + bytes([FLAG_UV]) + ad[33:],):
        try:
            assertion_to_response(bad, sig, cd, ch)
            raise AssertionError("accepted an assertion without UP")
        except Fail:
            pass
    try:
        check_device_bound(FLAG_UP | FLAG_BE)
        raise AssertionError("accepted a synced credential")
    except Fail:
        pass
    assert sanitize_label("  héllo\tworld  ") == "h?llo?world"
    assert len(sanitize_label("x" * 50)) == 29
    print("selftest ok")


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--port", help="serial port of the Flipper (default: the only one found)")
    sub = p.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("enroll", help="Flipper unlocked; hold OK on it to confirm")
    e.add_argument("--label")
    e.add_argument("--require-uv", action="store_true", help="refuse unlocks without PIN/biometry on the authenticator")
    sub.add_parser("unlock", help="Flipper locked; approve on this computer")
    sub.add_parser("status")
    f = sub.add_parser("forget", help="revoke on the Flipper and delete the local record")
    f.add_argument("--local-only", action="store_true")
    f.add_argument("--uid", help="with --local-only: only this Flipper's record")
    sub.add_parser("revoke-all", help="revoke every computer on the Flipper")
    sub.add_parser("selftest", help="check the message formatting (no hardware needed)")
    args = p.parse_args()
    try:
        {
            "enroll": cmd_enroll,
            "unlock": cmd_unlock,
            "status": cmd_status,
            "forget": cmd_forget,
            "revoke-all": cmd_revoke_all,
            "selftest": cmd_selftest,
        }[args.cmd](args)
    except Fail as e:
        print(e, file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
