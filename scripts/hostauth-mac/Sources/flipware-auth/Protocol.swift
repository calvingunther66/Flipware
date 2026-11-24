// Wire format shared with applications/services/desktop/helpers/host_auth_core.h.
// Keep the two in sync; the firmware's host test (tests/host/host_auth) pins the layout.
import CryptoKit
import Foundation

enum HostAuth {
    static let tag = Array("flipware-hostauth-v1".utf8)
    static let actionUnlock: UInt8 = 1
    static let uidLength = 8
    static let nonceLength = 32

    /// tag || action || uid_len || uid || nonce  (62 bytes)
    static func message(action: UInt8, uid: [UInt8], nonce: [UInt8]) -> Data {
        precondition(uid.count == uidLength && nonce.count == nonceLength)
        var bytes = tag
        bytes.append(action)
        bytes.append(UInt8(uid.count))
        bytes += uid
        bytes += nonce
        return Data(bytes)
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
}
