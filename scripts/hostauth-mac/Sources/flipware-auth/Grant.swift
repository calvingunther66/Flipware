// Timed unlock grant: one Touch ID approval lets this Mac (and agents running as you)
// unlock the Flipper without a fingerprint each time, for at most two hours.
//
// How it works, and why nothing is stored:
// - `flipware-auth grant` starts a background process (`grant-serve`). It asks for Touch ID
//   once, against the same Secure Enclave key `unlock` uses, and keeps that approved
//   LAContext in memory only.
// - It listens on a Unix socket (mode 0600, same-user peers only). `flipware-auth unlock`
//   sends it the Flipper's challenge; it checks the challenge is an unlock for the enrolled
//   Flipper, signs it with the approved context and returns the signature. It never signs
//   anything else, and the private key still never leaves the Secure Enclave.
// - When the time is up, the unlock count is used up, `revoke-grant` is run, or the process
//   dies (logout, reboot, kill), the context is invalidated and the Secure Enclave asks for
//   Touch ID again. There is no grant file or Keychain item to steal, copy or forget to delete.
//   The Flipper is unchanged: it can't tell a granted unlock from a fresh Touch ID.
import CryptoKit
import Darwin
import Foundation
import LocalAuthentication

enum GrantError: Error, CustomStringConvertible {
    case hours(Double), noKey, severalKeys([String]), alreadyActive, unusable(String), socketPath

    var description: String {
        switch self {
        case .hours(let h):
            return "--hours \(h) is out of range: use more than 0 and at most \(Grant.maxHours) (a grant is deliberately short)."
        case .noKey: return "This Mac has no Flipper key. Run `flipware-auth enroll` first."
        case .severalKeys(let uids):
            return "This Mac has keys for several Flippers (\(uids.joined(separator: ", "))). Pick one with --uid."
        case .alreadyActive: return "A grant is already active. Run `flipware-auth revoke-grant` first to replace it."
        case .unusable(let why): return "No grant was started: \(why)"
        case .socketPath: return "The grant socket path is too long for a Unix socket."
        }
    }
}

enum Grant {
    static let maxHours = 2.0
    static let defaultHours = 1.0
    static let defaultMaxUnlocks = 25

    static var socketPath: String { KeyStore.directory.appendingPathComponent("grant.sock").path }
    static var logURL: URL { KeyStore.directory.appendingPathComponent("grant.log") }

    /// Which Flipper's key to grant: --uid, or the only key this Mac has.
    static func resolveUID(_ requested: String?) throws -> String {
        if let requested { return requested.lowercased() }
        let uids = KeyStore.storedUIDs()
        if uids.isEmpty { throw GrantError.noKey }
        if uids.count > 1 { throw GrantError.severalKeys(uids) }
        return uids[0]
    }

    /// Seconds from a clock that keeps counting while the Mac sleeps and ignores
    /// wall-clock changes (CLOCK_MONOTONIC on macOS), so a grant can't be stretched.
    static func monotonicNow() -> TimeInterval {
        var ts = timespec()
        clock_gettime(CLOCK_MONOTONIC, &ts)
        return TimeInterval(ts.tv_sec) + TimeInterval(ts.tv_nsec) / 1_000_000_000
    }

    static func log(_ text: String) {
        let line = "\(ISO8601DateFormatter().string(from: Date())) \(text)\n"
        let url = logURL
        if !FileManager.default.fileExists(atPath: url.path) {
            FileManager.default.createFile(atPath: url.path, contents: nil, attributes: [.posixPermissions: 0o600])
        }
        if let handle = try? FileHandle(forWritingTo: url) {
            _ = try? handle.seekToEnd()
            try? handle.write(contentsOf: Data(line.utf8))
            try? handle.close()
        }
    }

    // MARK: Unix socket helpers

    static func address() -> sockaddr_un? {
        var addr = sockaddr_un()
        addr.sun_family = sa_family_t(AF_UNIX)
        let bytes = Array(socketPath.utf8)
        let capacity = MemoryLayout.size(ofValue: addr.sun_path)
        guard bytes.count < capacity else { return nil }
        withUnsafeMutableBytes(of: &addr.sun_path) { raw in
            for (i, byte) in bytes.enumerated() { raw[i] = byte }
            raw[bytes.count] = 0
        }
        addr.sun_len = UInt8(MemoryLayout<sockaddr_un>.size)
        return addr
    }

    static func setTimeout(_ fd: Int32, seconds: Int) {
        var tv = timeval(tv_sec: seconds, tv_usec: 0)
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, socklen_t(MemoryLayout<timeval>.size))
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, socklen_t(MemoryLayout<timeval>.size))
    }

    static func readLine(_ fd: Int32, limit: Int = 1024) -> String? {
        var data = [UInt8]()
        var byte: UInt8 = 0
        while data.count < limit {
            let n = Darwin.read(fd, &byte, 1)
            if n <= 0 { return nil }
            if byte == 10 { return String(decoding: data, as: UTF8.self) }
            data.append(byte)
        }
        return nil
    }

    static func writeLine(_ fd: Int32, _ text: String) {
        let bytes = Array((text + "\n").utf8)
        var offset = 0
        while offset < bytes.count {
            let n = bytes[offset...].withUnsafeBufferPointer { Darwin.write(fd, $0.baseAddress, $0.count) }
            if n <= 0 { return }
            offset += n
        }
    }

    /// Wait for an LAContext approval (the Touch ID prompt) and return its error, if any.
    static func approve(_ context: LAContext, policy: KeyPolicy, reason: String) throws {
        let access = try KeyStore.accessControl(policy: policy)
        final class Result: @unchecked Sendable { var error: Error?; var ok = false }
        let result = Result()
        let done = DispatchSemaphore(value: 0)
        context.evaluateAccessControl(access, operation: .useKeySign, localizedReason: reason) { ok, error in
            result.ok = ok
            result.error = error
            done.signal()
        }
        done.wait()
        if !result.ok {
            if let la = result.error as? LAError,
                [.userCancel, .appCancel, .systemCancel, .userFallback].contains(la.code)
            {
                throw KeyStoreError.cancelled
            }
            throw GrantError.unusable(result.error.map { "\($0)" } ?? "Touch ID was not approved.")
        }
    }
}

// MARK: Server (runs in the background `grant-serve` process)

var grantStopRequested: sig_atomic_t = 0

final class GrantServer {
    let stored: StoredKey
    let key: SecureEnclave.P256.Signing.PrivateKey
    let context: LAContext
    let deadline: TimeInterval  // monotonic
    var unlocksLeft: Int
    var revoked = false

    init(stored: StoredKey, key: SecureEnclave.P256.Signing.PrivateKey, context: LAContext, deadline: TimeInterval, maxUnlocks: Int) {
        self.stored = stored
        self.key = key
        self.context = context
        self.deadline = deadline
        self.unlocksLeft = maxUnlocks
    }

    func run(listener: Int32) {
        while !revoked && grantStopRequested == 0 {
            let remaining = deadline - Grant.monotonicNow()
            if remaining <= 0 { break }
            var pfd = pollfd(fd: listener, events: Int16(POLLIN), revents: 0)
            let ready = poll(&pfd, 1, Int32(min(remaining, 1.0) * 1000))
            if ready <= 0 { continue }
            let client = accept(listener, nil, nil)
            if client < 0 { continue }
            Grant.setTimeout(client, seconds: 5)
            handle(client)
            close(client)
        }
    }

    private func handle(_ client: Int32) {
        var uid: uid_t = 0
        var gid: gid_t = 0
        guard getpeereid(client, &uid, &gid) == 0, uid == geteuid() else {
            Grant.writeLine(client, "error not-allowed")
            return
        }
        var pid: pid_t = 0
        var len = socklen_t(MemoryLayout<pid_t>.size)
        getsockopt(client, 0 /* SOL_LOCAL */, 0x002 /* LOCAL_PEERPID */, &pid, &len)

        guard let line = Grant.readLine(client) else { return }
        let parts = line.split(separator: " ").map(String.init)
        switch parts.first {
        case "status":
            let left = Int(max(0, deadline - Grant.monotonicNow()))
            Grant.writeLine(client, "ok uid=\(stored.uid) seconds=\(left) unlocks-left=\(unlocksLeft)")
        case "revoke":
            Grant.log("revoked by pid \(pid)")
            revoked = true
            Grant.writeLine(client, "ok")
        case "sign" where parts.count == 2:
            let reply = sign(challenge: parts[1])
            Grant.log("sign pid=\(pid) -> \(reply.hasPrefix("ok") ? "ok" : reply) (\(unlocksLeft) left)")
            Grant.writeLine(client, reply)
        default:
            Grant.writeLine(client, "error usage")
        }
    }

    /// Signs one WebAuthn unlock assertion for the enrolled Flipper, and nothing else.
    private func sign(challenge text: String) -> String {
        guard unlocksLeft > 0 else { return "error unlock-limit-reached" }
        guard text.utf8.count == 94,
            text.allSatisfy({ $0.isASCII && ($0.isLetter || $0.isNumber || $0 == "-" || $0 == "_") }),
            let raw = Data(base64url: text), let binding = HostAuth.parseBinding(raw),
            binding.action == HostAuth.actionUnlock, binding.uid.hexString == stored.uid
        else { return "error not-an-unlock-for-this-flipper" }

        var message = HostAuth.authenticatorData(flags: HostAuth.flagUP | HostAuth.flagUV)
        message.append(Data(SHA256.hash(data: HostAuth.clientData(challenge: text))))
        do {
            let signature = try key.signature(for: message).derRepresentation
            unlocksLeft -= 1
            return "ok \(signature.hexString)"
        } catch {
            // The approval is gone (system invalidated it): fail closed, never prompt unattended
            revoked = true
            Grant.log("approval lost: \(error)")
            return "error approval-lost"
        }
    }
}

/// `flipware-auth grant-serve`: the background half of `grant`. Reports one line on stdout
/// ("ready <seconds>" or "error <why>"), then detaches from the terminal.
func grantServe(_ options: Options) -> Never {
    func report(_ line: String) {
        FileHandle.standardOutput.write(Data((line.replacingOccurrences(of: "\n", with: " ") + "\n").utf8))
    }
    func die(_ why: String) -> Never {
        report("error \(why)")
        exit(1)
    }

    setsid()
    signal(SIGHUP, SIG_IGN)
    signal(SIGPIPE, SIG_IGN)
    signal(SIGTERM) { _ in grantStopRequested = 1 }
    signal(SIGINT) { _ in grantStopRequested = 1 }
    umask(0o077)

    do {
        let hours = options.hours ?? Grant.defaultHours
        guard hours > 0, hours <= Grant.maxHours else { throw GrantError.hours(hours) }
        let uid = try Grant.resolveUID(options.uid)
        let stored = try KeyStore.load(uid: uid)
        try KeyStore.checkNotInvalidated(stored)
        guard var addr = Grant.address() else { throw GrantError.socketPath }

        if GrantClient.status() != nil { throw GrantError.alreadyActive }
        try FileManager.default.createDirectory(
            at: KeyStore.directory, withIntermediateDirectories: true, attributes: [.posixPermissions: 0o700])
        unlink(Grant.socketPath)  // a stale socket from a crashed grant

        // One Touch ID for the whole window
        let context = LAContext()
        let length = hours == hours.rounded() ? "\(Int(hours)) hour(s)" : String(format: "%.1f hours", hours)
        try Grant.approve(
            context, policy: stored.policy,
            reason: "let this Mac unlock Flipper \(uid) without Touch ID for \(length)")
        context.interactionNotAllowed = true  // from here on a lost approval fails, it never prompts

        let key = try SecureEnclave.P256.Signing.PrivateKey(
            dataRepresentation: stored.keyBlob, authenticationContext: context)
        // Prove the approval carries over before reporting success
        _ = try key.signature(for: Data("flipware-auth grant self-check".utf8))

        let listener = socket(AF_UNIX, SOCK_STREAM, 0)
        guard listener >= 0 else { throw GrantError.unusable("socket: \(String(cString: strerror(errno)))") }
        let bound = withUnsafePointer(to: &addr) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                bind(listener, $0, socklen_t(MemoryLayout<sockaddr_un>.size))
            }
        }
        guard bound == 0, listen(listener, 4) == 0 else {
            throw GrantError.unusable("could not listen on the grant socket: \(String(cString: strerror(errno)))")
        }
        chmod(Grant.socketPath, 0o600)

        let maxUnlocks = options.maxUnlocks ?? Grant.defaultMaxUnlocks
        let server = GrantServer(
            stored: stored, key: key, context: context,
            deadline: Grant.monotonicNow() + hours * 3600, maxUnlocks: maxUnlocks)
        Grant.log("started uid=\(uid) hours=\(hours) max-unlocks=\(maxUnlocks) pid=\(getpid())")
        report("ready \(Int(hours * 3600))")

        // Detach from the starting terminal's stdio
        let null = open("/dev/null", O_RDWR)
        dup2(null, 0); dup2(null, 1); dup2(null, 2)

        server.run(listener: listener)

        context.invalidate()
        close(listener)
        unlink(Grant.socketPath)
        Grant.log("ended (\(server.revoked ? "revoked or approval lost" : grantStopRequested != 0 ? "signal" : "time is up"))")
        exit(0)
    } catch {
        die("\(error)")
    }
}

// MARK: Client (used by unlock, status, revoke-grant)

enum GrantClient {
    /// One request/reply on the grant socket; nil when no grant is running.
    static func request(_ line: String) -> String? {
        guard var addr = Grant.address() else { return nil }
        let fd = socket(AF_UNIX, SOCK_STREAM, 0)
        guard fd >= 0 else { return nil }
        defer { close(fd) }
        Grant.setTimeout(fd, seconds: 5)
        let connected = withUnsafePointer(to: &addr) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                connect(fd, $0, socklen_t(MemoryLayout<sockaddr_un>.size))
            }
        }
        guard connected == 0 else { return nil }
        Grant.writeLine(fd, line)
        return Grant.readLine(fd)
    }

    struct Status {
        let uid: String
        let seconds: Int
        let unlocksLeft: Int
    }

    static func status() -> Status? {
        guard let reply = request("status"), reply.hasPrefix("ok ") else { return nil }
        var fields: [String: String] = [:]
        for part in reply.split(separator: " ").dropFirst() {
            if let eq = part.firstIndex(of: "=") {
                fields[String(part[..<eq])] = String(part[part.index(after: eq)...])
            }
        }
        guard let uid = fields["uid"], let seconds = Int(fields["seconds"] ?? ""),
            let left = Int(fields["unlocks-left"] ?? "")
        else { return nil }
        return Status(uid: uid, seconds: seconds, unlocksLeft: left)
    }

    enum SignResult {
        case noGrant
        case refused(String)
        case signed(Data)
    }

    static func sign(challenge: String, uid: String) -> SignResult {
        guard let reply = request("sign \(challenge)") else { return .noGrant }
        let parts = reply.split(separator: " ").map(String.init)
        if parts.count == 2, parts[0] == "ok", let der = Data(hex: parts[1]) {
            return .signed(der)
        }
        return .refused(reply)
    }

    static func revoke() -> Bool {
        guard let reply = request("revoke") else {
            unlink(Grant.socketPath)  // nothing answers: clear a stale socket
            return false
        }
        return reply == "ok"
    }
}

func formatDuration(_ seconds: Int) -> String {
    let h = seconds / 3600
    let m = (seconds % 3600) / 60
    return h > 0 ? "\(h) h \(m) min" : "\(max(m, seconds > 0 ? 1 : 0)) min"
}

/// `flipware-auth grant`: ask once for Touch ID, then keep the approval alive in a background process.
func grantCommand(_ options: Options) throws {
    let hours = options.hours ?? Grant.defaultHours
    guard hours > 0, hours <= Grant.maxHours else { throw GrantError.hours(hours) }
    let uid = try Grant.resolveUID(options.uid)
    _ = try KeyStore.load(uid: uid)
    if GrantClient.status() != nil { throw GrantError.alreadyActive }

    guard let exe = Bundle.main.executableURL else { fail("Can't find this program's path to start the grant.") }
    let process = Process()
    process.executableURL = exe
    var args = ["grant-serve", "--hours", "\(hours)", "--uid", uid]
    if let n = options.maxUnlocks { args += ["--max-unlocks", "\(n)"] }
    process.arguments = args
    let out = Pipe()
    process.standardOutput = out
    process.standardInput = FileHandle.nullDevice
    process.standardError = FileHandle.nullDevice

    print("Touch ID on the Mac: approve to allow unlocks of Flipper \(uid) without Touch ID for up to \(hours) h.")
    try process.run()

    // Wait for the background process to report (the Touch ID prompt can take a while)
    let fd = out.fileHandleForReading.fileDescriptor
    var pfd = pollfd(fd: fd, events: Int16(POLLIN), revents: 0)
    guard poll(&pfd, 1, 120_000) > 0, let line = String(data: out.fileHandleForReading.availableData, encoding: .utf8),
        !line.isEmpty
    else {
        process.terminate()
        fail("The grant did not start (no answer in 2 minutes). Nothing is granted.")
    }
    let text = line.trimmingCharacters(in: .whitespacesAndNewlines)
    if text.hasPrefix("ready ") {
        let seconds = Int(text.dropFirst(6)) ?? Int(hours * 3600)
        let until = Date().addingTimeInterval(TimeInterval(seconds))
        let fmt = DateFormatter()
        fmt.timeStyle = .short
        fmt.dateStyle = .none
        print("Grant active until \(fmt.string(from: until)) (\(formatDuration(seconds))).")
        print("`flipware-auth unlock` (and `watch`) now work without Touch ID until then.")
        print("End it early with `flipware-auth revoke-grant`. It ends by itself if you log out or restart.")
    } else {
        fail(text.hasPrefix("error ") ? String(text.dropFirst(6)) : text)
    }
}

func revokeGrant() {
    if GrantClient.revoke() {
        print("Grant revoked. Unlocking needs Touch ID again.")
    } else {
        print("No grant was active.")
    }
}

/// `flipware-auth grant-selftest [--wait SECONDS]`: checks on this Mac, with no Flipper, that
/// one Touch ID approval keeps working (also past the 5 minute LAContext reuse limit), and
/// stops working after invalidate(). Uses a throwaway Secure Enclave key that is not stored.
func grantSelftest(_ options: Options) throws {
    guard SecureEnclave.isAvailable else { throw KeyStoreError.noSecureEnclave }
    let access = try KeyStore.accessControl(policy: options.policy)
    let probe = try SecureEnclave.P256.Signing.PrivateKey(accessControl: access)
    let context = LAContext()
    print("Touch ID: approve the self-test (nothing is unlocked).")
    try Grant.approve(context, policy: options.policy, reason: "self-test of the timed unlock grant (no Flipper involved)")
    context.interactionNotAllowed = true
    let key = try SecureEnclave.P256.Signing.PrivateKey(
        dataRepresentation: probe.dataRepresentation, authenticationContext: context)

    func trySign(_ label: String, expectSuccess: Bool) -> Bool {
        let message = Data(label.utf8)
        let outcome: Bool
        do {
            let sig = try key.signature(for: message)
            outcome = probe.publicKey.isValidSignature(sig, for: message)
        } catch {
            outcome = false
            if expectSuccess { print("  error: \(error)") }
        }
        let pass = outcome == expectSuccess
        print("\(pass ? "PASS" : "FAIL"): \(label) \(outcome ? "signed without a prompt" : "refused")")
        return pass
    }

    var ok = trySign("sign right after approval", expectSuccess: true)
    ok = trySign("sign again immediately", expectSuccess: true) && ok
    if let wait = options.wait, wait > 0 {
        print("Waiting \(Int(wait)) s (use 330 or more to pass the 5 minute reuse limit)...")
        sleep(UInt32(wait))
        ok = trySign("sign after \(Int(wait)) s", expectSuccess: true) && ok
    }
    context.invalidate()
    ok = trySign("sign after invalidate()", expectSuccess: false) && ok
    print(ok ? "Self-test passed: the grant approach works on this Mac." : "Self-test FAILED: do not rely on `grant` on this Mac.")
    if !ok { exit(1) }
}
