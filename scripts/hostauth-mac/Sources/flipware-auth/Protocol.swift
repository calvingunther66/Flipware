// Wire format shared with applications/services/desktop/helpers/host_auth_core.h.
// Keep the two in sync; the firmware's host test (tests/host/host_auth) pins the layout.
import CryptoKit
import Foundation

enum HostAuth {
    static let protocolVersion = "2"
    static let rpId = "hostauth.flipware.local"
    static let origin = "https://hostauth.flipware.local"
    static let tag = Array("flipware-hostauth-v2".utf8)
    static let actionUnlock: UInt8 = 1
    static let uidLength = 8
    static let nonceLength = 32
    /// tag || action || uid_len || uid || epoch(4) || counter(4) || nonce
    static let bindingLength = tag.count + 2 + uidLength + 4 + 4 + nonceLength  // 70

    static let flagUP: UInt8 = 0x01
    static let flagUV: UInt8 = 0x04

    struct Binding {
        let action: UInt8
        let uid: Data
        let epoch: UInt32
        let counter: UInt32
    }

    /// Decode and check the binding the Flipper put in its challenge. We only
    /// sign challenges that are for unlocking, for the Flipper this key belongs to.
    static func parseBinding(_ raw: Data) -> Binding? {
        let b = [UInt8](raw)
        guard b.count == bindingLength, Array(b[0..<tag.count]) == tag else { return nil }
        var i = tag.count
        let action = b[i]; i += 1
        guard b[i] == UInt8(uidLength) else { return nil }
        i += 1
        let uid = Data(b[i..<(i + uidLength)]); i += uidLength
        func u32(_ at: Int) -> UInt32 {
            (UInt32(b[at]) << 24) | (UInt32(b[at + 1]) << 16) | (UInt32(b[at + 2]) << 8) | UInt32(b[at + 3])
        }
        return Binding(action: action, uid: uid, epoch: u32(i), counter: u32(i + 4))
    }

    /// The exact client data JSON the Flipper rebuilds and hashes. Same bytes as
    /// python-fido2's CollectedClientData.create (compact separators, this key order).
    static func clientData(challenge: String) -> Data {
        Data(
            "{\"type\":\"webauthn.get\",\"challenge\":\"\(challenge)\",\"origin\":\"\(origin)\",\"crossOrigin\":false}"
                .utf8)
    }

    /// rpIdHash || flags || signCount (big-endian). The Secure Enclave has no
    /// signature counter, so it is always 0 (the Flipper accepts 0 -> 0).
    static func authenticatorData(flags: UInt8) -> Data {
        var data = Data(SHA256.hash(data: Data(rpId.utf8)))
        data.append(flags)
        data.append(contentsOf: [0, 0, 0, 0])
        return data
    }

    /// First 8 bytes of SHA-256 over the 65-byte X9.63 public key, as hex.
    static func fingerprint(publicKeyX963: Data) -> String {
        let digest = SHA256.hash(data: publicKeyX963)
        return Data(digest.prefix(8)).hexString
    }

    static func grouped(_ fp: String) -> String {
        stride(from: 0, to: fp.count, by: 4).map { i -> String in
            let start = fp.index(fp.startIndex, offsetBy: i)
            let end = fp.index(start, offsetBy: min(4, fp.count - i))
            return String(fp[start..<end])
        }.joined(separator: "-")
    }

    /// The Flipper draws the label on its screen and only accepts 1..29 printable
    /// ASCII characters, with no leading or trailing space.
    static func sanitizeLabel(_ label: String) -> String {
        let ascii = label.unicodeScalars.map { $0.value >= 0x20 && $0.value <= 0x7E ? Character($0) : "?" }
        let trimmed = String(ascii).trimmingCharacters(in: .whitespaces)
        let capped = String(trimmed.prefix(29)).trimmingCharacters(in: .whitespaces)
        return capped.isEmpty ? "Mac" : capped
    }
}

/// One `hostauth-<kind> key=value ...` line from the Flipper.
struct ReplyLine {
    let kind: String  // "info", "challenge", "ok", "error", "key", "pending", "note"
    let words: [String]
    let fields: [String: String]

    init?(_ line: String) {
        guard line.hasPrefix("hostauth-") else { return nil }
        let parts = line.split(separator: " ").map(String.init)
        kind = String(parts[0].dropFirst("hostauth-".count))
        var fields: [String: String] = [:]
        var words: [String] = []
        for part in parts.dropFirst() {
            if let eq = part.firstIndex(of: "=") {
                fields[String(part[..<eq])] = String(part[part.index(after: eq)...])
            } else {
                words.append(part)
            }
        }
        // label= runs to the end of the line
        if let range = line.range(of: " label=") {
            fields["label"] = String(line[range.upperBound...])
        }
        self.words = words
        self.fields = fields
    }
}

extension Data {
    var hexString: String { map { String(format: "%02x", $0) }.joined() }

    init?(hex: String) {
        guard hex.count % 2 == 0 else { return nil }
        var bytes: [UInt8] = []
        bytes.reserveCapacity(hex.count / 2)
        var index = hex.startIndex
        while index < hex.endIndex {
            let next = hex.index(index, offsetBy: 2)
            guard let byte = UInt8(hex[index..<next], radix: 16) else { return nil }
            bytes.append(byte)
            index = next
        }
        self.init(bytes)
    }

    init?(base64url: String) {
        var s = base64url.replacingOccurrences(of: "-", with: "+").replacingOccurrences(of: "_", with: "/")
        guard !s.contains("=") else { return nil }
        while s.count % 4 != 0 { s += "=" }
        self.init(base64Encoded: s)
    }
}
