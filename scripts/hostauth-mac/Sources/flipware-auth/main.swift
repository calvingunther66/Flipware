// flipware-auth: enroll this Mac with a Flipper, and sign the Flipper back in over USB
// with a Secure Enclave key, in the WebAuthn assertion format the Flipper verifies.
import CryptoKit
import Foundation

struct Options {
    var command = "help"
    var port: String?
    var label = Host.current().localizedName ?? "Mac"
    var policy = KeyPolicy.biometryCurrentSet
    var localOnly = false

    init(_ args: [String]) {
        var it = args.dropFirst().makeIterator()
        if let first = it.next() { command = first }
        while let arg = it.next() {
            switch arg {
            case "--port": port = it.next()
            case "--label": label = it.next() ?? label
            case "--allow-password": policy = .userPresence
            case "--local-only": localOnly = true
            default: fail("Unknown option \(arg)")
            }
        }
    }
}

struct DeviceInfo {
    let uid: String
    let locked: Bool
    let pinSet: Bool
    let keys: Int
    let fingerprints: [String]
}

func fail(_ message: String) -> Never {
    FileHandle.standardError.write(Data((message + "\n").utf8))
    exit(1)
}

func errorText(_ replies: [ReplyLine]) -> String {
    guard let err = replies.first(where: { $0.kind == "error" }) else { return "no answer" }
    let code = err.words.first ?? "unknown"
    let extra = err.fields.map { "\($0.key)=\($0.value)" }.joined(separator: " ")
    let hint: String
    switch code {
    case "backoff": hint = " (too many failed sign-ins; wait, or unlock with the PIN)"
    case "integrity", "rollback":
        hint = " (the Flipper's key list is not the current one; unlock with the PIN and enroll again)"
    case "synced-credential": hint = " (synced passkeys are not accepted, only device-bound keys)"
    case "locked": hint = " (unlock the Flipper with its PIN first)"
    default: hint = ""
    }
    return ([code, extra].filter { !$0.isEmpty }).joined(separator: " ") + hint
}

func info(_ port: SerialPort) throws -> DeviceInfo {
    let replies = try port.command("hostauth info")
    guard let line = replies.first(where: { $0.kind == "info" }), let uid = line.fields["uid"] else {
        fail("This Flipper doesn't answer `hostauth info`. Is it running Flipware with host sign-in?")
    }
    guard line.fields["v"] == HostAuth.protocolVersion, line.fields["rp"] == HostAuth.rpId else {
        fail("This Flipper speaks host sign-in v\(line.fields["v"] ?? "?"); this tool speaks v\(HostAuth.protocolVersion).")
    }
    return DeviceInfo(
        uid: uid, locked: line.fields["locked"] == "1", pinSet: line.fields["pin"] == "1",
        keys: Int(line.fields["keys"] ?? "0") ?? 0,
        fingerprints: replies.filter { $0.kind == "key" }.compactMap { $0.fields["fp"] })
}

func openPort(_ options: Options) throws -> SerialPort {
    let port = try SerialPort(path: options.port ?? SerialPort.autodetect())
    try port.sync()
    return port
}

func enroll(_ options: Options) throws {
    let port = try openPort(options)
    let device = try info(port)
    if device.locked { fail("Unlock the Flipper with its PIN first, then enroll.") }
    if (try? KeyStore.load(uid: device.uid)) != nil {
        fail("This Mac already has a key for Flipper \(device.uid). Run `flipware-auth forget` first.")
    }

    let label = HostAuth.sanitizeLabel(options.label)
    let (stored, key) = try KeyStore.create(uid: device.uid, label: label, policy: options.policy)
    print("Fingerprint: \(HostAuth.grouped(stored.fingerprint))")
    print("Check that the Flipper shows the same fingerprint, then hold OK on it (Back cancels).")

    // uv=1: every signature needs Touch ID (or the password with --allow-password),
    // which WebAuthn calls user verification
    let pubHex = key.publicKey.x963Representation.hexString
    let replies = try port.command("hostauth enroll \(pubHex) 1 \(label)", timeout: 75)
    guard let ok = replies.first(where: { $0.kind == "ok" }), ok.fields["fp"] == stored.fingerprint else {
        fail("Enrollment failed: \(errorText(replies))")
    }
    try KeyStore.save(stored)
    print("Enrolled with Flipper \(device.uid).")
    switch options.policy {
    case .biometryCurrentSet:
        print("Each unlock needs Touch ID. Changing your Touch ID fingers invalidates this key; then unlock with the PIN and enroll again.")
    case .userPresence:
        print("Each unlock needs Touch ID, your Watch or your Mac password.")
    }
    if !device.pinSet { print("Note: this Flipper has no PIN set, so there is nothing to unlock yet.") }
}

/// Returns true if the Flipper ended up unlocked.
@discardableResult
func unlock(_ options: Options, port existing: SerialPort? = nil) throws -> Bool {
    let port = try existing ?? openPort(options)
    let device = try info(port)
    if !device.locked {
        print("Flipper \(device.uid) is already unlocked.")
        return true
    }
    let stored = try KeyStore.load(uid: device.uid)

    let challengeReplies = try port.command("hostauth challenge")
    guard let challenge = challengeReplies.first(where: { $0.kind == "challenge" }) else {
        fail("No challenge: \(errorText(challengeReplies))")
    }
    // Only sign what we expect: protocol v2, our relying party, an unlock, for the
    // Flipper this key was enrolled with (a different device relaying another
    // Flipper's challenge gets nothing)
    guard challenge.fields["v"] == HostAuth.protocolVersion, challenge.fields["rp"] == HostAuth.rpId,
        let challengeText = challenge.fields["challenge"],
        let raw = Data(base64url: challengeText), let binding = HostAuth.parseBinding(raw),
        binding.action == HostAuth.actionUnlock,
        binding.uid.hexString == device.uid, device.uid == stored.uid
    else { fail("Unexpected challenge from the Flipper; not signing it.") }

    // WebAuthn assertion: sign authenticatorData || SHA-256(clientDataJSON).
    // UP and UV are set by this tool: what makes them true is the Secure Enclave
    // access control, which refuses to sign without Touch ID (or the password).
    let authData = HostAuth.authenticatorData(flags: HostAuth.flagUP | HostAuth.flagUV)
    var message = authData
    message.append(Data(SHA256.hash(data: HostAuth.clientData(challenge: challengeText))))
    let signature = try KeyStore.sign(message, with: stored, reason: "unlock Flipper \(device.uid)")

    // A successful unlock restarts the Flipper's shell, so the reply can get cut off.
    // Ask again rather than trusting a missing line.
    let replies =
        (try? port.command("hostauth response \(authData.hexString) \(signature.hexString)", timeout: 10)) ?? []
    if replies.contains(where: { $0.kind == "ok" }) {
        print("Flipper \(device.uid) unlocked.")
        return true
    }
    if replies.contains(where: { $0.kind == "error" }) {
        fail("Sign-in refused: \(errorText(replies))")
    }
    try port.sync()  // the restarted shell prints a fresh banner and prompt
    let after = try info(port)
    if !after.locked { print("Flipper \(device.uid) unlocked.") } else { fail("Sign-in did not go through.") }
    return !after.locked
}

func watch(_ options: Options) throws {
    print("Watching for locked Flipper devices. Ctrl-C to stop.")
    var seen = Set<String>()
    while true {
        let ports = Set(options.port.map { [$0] } ?? SerialPort.flipperPorts())
        for path in ports.subtracting(seen) {
            var opts = options
            opts.port = path
            do { try unlock(opts) } catch { print("\(path): \(error)") }
        }
        seen = ports.intersection(Set(SerialPort.flipperPorts()))
        sleep(2)
    }
}

func status(_ options: Options) throws {
    let device = try info(try openPort(options))
    let local = try? KeyStore.load(uid: device.uid)
    print("Flipper \(device.uid): \(device.locked ? "locked" : "unlocked"), PIN \(device.pinSet ? "set" : "not set"), \(device.keys) computer(s) enrolled")
    if let local {
        let enrolled = device.locked ? "unknown while locked" : (device.fingerprints.contains(local.fingerprint) ? "yes" : "no")
        print("This Mac: \(HostAuth.grouped(local.fingerprint)) (\(local.policy.rawValue)), enrolled on the Flipper: \(enrolled)")
        if local.policy == .biometryCurrentSet, let then = local.biometryState, let now = KeyStore.biometryState(), now != then {
            print("Touch ID fingers changed since enrollment: this key no longer works. Unlock with the PIN, then forget and enroll.")
        }
    } else {
        print("This Mac has no key for it.")
    }
}

/// Revoke this Mac's key on the Flipper and delete it here. With --local-only
/// (Flipper lost, or its key list already reset) only the local key is deleted.
func forget(_ options: Options) throws {
    if options.localOnly {
        guard let path = options.port ?? (try? SerialPort.autodetect()) else {
            // No Flipper attached: forget every local key
            let files = (try? FileManager.default.contentsOfDirectory(atPath: KeyStore.directory.path)) ?? []
            for file in files where file.hasSuffix(".json") {
                try KeyStore.delete(uid: String(file.dropLast(5)))
            }
            print("Deleted \(files.filter { $0.hasSuffix(".json") }.count) local key(s).")
            return
        }
        var opts = options
        opts.port = path
        let device = try info(try openPort(opts))
        try KeyStore.delete(uid: device.uid)
        print("Deleted this Mac's key for Flipper \(device.uid). It may still be listed on the Flipper; it can't be used without this Mac.")
        return
    }
    let port = try openPort(options)
    let device = try info(port)
    if device.locked { fail("Unlock the Flipper first (or use --local-only).") }
    guard let local = try? KeyStore.load(uid: device.uid) else {
        fail("This Mac has no key for Flipper \(device.uid).")
    }
    let replies = try port.command("hostauth revoke \(local.fingerprint)")
    if !replies.contains(where: { $0.kind == "ok" }) {
        fail("The Flipper did not revoke the key (\(errorText(replies))); the local key was kept.")
    }
    try KeyStore.delete(uid: device.uid)
    print("Revoked on the Flipper and deleted this Mac's key for Flipper \(device.uid).")
}

/// Revoke every computer on the Flipper (also works if its key list fails its integrity check).
func revokeAll(_ options: Options) throws {
    let port = try openPort(options)
    let device = try info(port)
    if device.locked { fail("Unlock the Flipper first.") }
    let replies = try port.command("hostauth revoke all")
    guard replies.contains(where: { $0.kind == "ok" }) else { fail("Revoke failed: \(errorText(replies))") }
    try? KeyStore.delete(uid: device.uid)
    print("Every computer was revoked on Flipper \(device.uid).")
}

let options = Options(CommandLine.arguments)
do {
    switch options.command {
    case "enroll": try enroll(options)
    case "unlock": try unlock(options)
    case "watch": try watch(options)
    case "status": try status(options)
    case "forget": try forget(options)
    case "revoke-all": try revokeAll(options)
    default:
        print(
            """
            usage: flipware-auth <command> [--port PATH]
              enroll [--label NAME] [--allow-password]   Flipper unlocked; hold OK on it to confirm
              unlock                                      Flipper locked; Touch ID on the Mac
              watch                                       unlock whenever a locked Flipper is plugged in
              status
              forget [--local-only]                       revoke on the Flipper and delete the local key
              revoke-all                                  revoke every computer on the Flipper
            """)
    }
} catch {
    fail("\(error)")
}
