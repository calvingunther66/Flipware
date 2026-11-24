// flipware-auth: enroll this Mac with a Flipper, and sign the Flipper back in over USB.
import Foundation

struct Options {
    var command = "help"
    var port: String?
    var label = Host.current().localizedName ?? "Mac"
    var requirePresence = true

    init(_ args: [String]) {
        var it = args.dropFirst().makeIterator()
        if let first = it.next() { command = first }
        while let arg = it.next() {
            switch arg {
            case "--port": port = it.next()
            case "--label": label = it.next() ?? label
            case "--no-presence": requirePresence = false
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
    return ([err.words.first ?? "unknown"] + err.fields.map { "\($0.key)=\($0.value)" }).joined(separator: " ")
}

func info(_ port: SerialPort) throws -> DeviceInfo {
    let replies = try port.command("hostauth info")
    guard let line = replies.first(where: { $0.kind == "info" }), let uid = line.fields["uid"] else {
        fail("This Flipper doesn't answer `hostauth info`. Is it running Flipware with host sign-in?")
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

    let (stored, key) = try KeyStore.create(
        uid: device.uid, label: options.label, requirePresence: options.requirePresence)
    print("Fingerprint: \(HostAuth.grouped(stored.fingerprint))")
    print("Check that the Flipper shows the same fingerprint, then press Enroll on it.")

    let pubHex = key.publicKey.x963Representation.hexString
    let replies = try port.command("hostauth enroll \(pubHex) \(options.label)", timeout: 120)
    guard let ok = replies.first(where: { $0.kind == "ok" }), ok.fields["fp"] == stored.fingerprint else {
        fail("Enrollment failed: \(errorText(replies))")
    }
    try KeyStore.save(stored)
    print("Enrolled with Flipper \(device.uid).")
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
    // Only sign what we expect: protocol v1, unlock, for the Flipper this key belongs to
    guard challenge.fields["v"] == "1", challenge.fields["action"] == "unlock",
        challenge.fields["uid"] == device.uid, device.uid == stored.uid,
        let uid = challenge.fields["uid"].flatMap(Data.init(hex:)), uid.count == HostAuth.uidLength,
        let nonce = challenge.fields["nonce"].flatMap(Data.init(hex:)), nonce.count == HostAuth.nonceLength
    else { fail("Unexpected challenge from the Flipper; not signing it.") }

    let message = HostAuth.message(action: HostAuth.actionUnlock, uid: [UInt8](uid), nonce: [UInt8](nonce))
    let signature = try KeyStore.sign(message, with: stored, reason: "unlock Flipper \(device.uid)")

    // A successful unlock restarts the Flipper's shell, so the reply can get cut off.
    // Ask again rather than trusting a missing line.
    let replies = (try? port.command("hostauth response \(signature.hexString)", timeout: 10)) ?? []
    if replies.contains(where: { $0.kind == "ok" }) {
        print("Flipper \(device.uid) unlocked.")
        return true
    }
    if let error = replies.first(where: { $0.kind == "error" }) {
        fail("Sign-in refused: \(errorText([error]))")
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
        print("This Mac: \(HostAuth.grouped(local.fingerprint)), enrolled on the Flipper: \(enrolled)")
    } else {
        print("This Mac has no key for it.")
    }
}

func forget(_ options: Options) throws {
    let port = try openPort(options)
    let device = try info(port)
    let local = try KeyStore.load(uid: device.uid)
    if device.locked { fail("Unlock the Flipper first.") }
    let replies = try port.command("hostauth revoke \(local.fingerprint)")
    if !replies.contains(where: { $0.kind == "ok" }) { print("Flipper said: \(errorText(replies))") }
    try KeyStore.delete(uid: device.uid)
    print("Forgot this Mac's key for Flipper \(device.uid).")
}

let options = Options(CommandLine.arguments)
do {
    switch options.command {
    case "enroll": try enroll(options)
    case "unlock": try unlock(options)
    case "watch": try watch(options)
    case "status": try status(options)
    case "forget": try forget(options)
    default:
        print("usage: flipware-auth enroll|unlock|watch|status|forget [--port PATH] [--label NAME] [--no-presence]")
    }
} catch {
    fail("\(error)")
}
