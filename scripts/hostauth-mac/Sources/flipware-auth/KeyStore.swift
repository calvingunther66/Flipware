// Secure Enclave key per Flipper. The private key never leaves the enclave; what is
// stored on disk is the enclave's wrapped blob, which only this Mac can use.
import CryptoKit
import Foundation
import LocalAuthentication
import Security

struct StoredKey: Codable {
    var uid: String  // Flipper UID, hex
    var label: String
    var fingerprint: String
    var requiresPresence: Bool
    var keyBlob: Data  // SecureEnclave.P256.Signing.PrivateKey.dataRepresentation
}

enum KeyStoreError: Error, CustomStringConvertible {
    case noSecureEnclave, accessControl, missing(String)

    var description: String {
        switch self {
        case .noSecureEnclave: return "This Mac has no Secure Enclave."
        case .accessControl: return "Could not create the key's access control."
        case .missing(let uid): return "No key for Flipper \(uid) on this Mac. Run `flipware-auth enroll` first."
        }
    }
}

enum KeyStore {
    static var directory: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Flipware/hostauth", isDirectory: true)
    }

    static func url(uid: String) -> URL { directory.appendingPathComponent("\(uid).json") }

    static func create(uid: String, label: String, requirePresence: Bool) throws
        -> (StoredKey, SecureEnclave.P256.Signing.PrivateKey)
    {
        guard SecureEnclave.isAvailable else { throw KeyStoreError.noSecureEnclave }
        var flags: SecAccessControlCreateFlags = [.privateKeyUsage]
        if requirePresence { flags.insert(.userPresence) }  // Touch ID, Watch or password
        guard
            let access = SecAccessControlCreateWithFlags(
                nil, kSecAttrAccessibleWhenUnlockedThisDeviceOnly, flags, nil)
        else { throw KeyStoreError.accessControl }

        let key = try SecureEnclave.P256.Signing.PrivateKey(accessControl: access)
        let stored = StoredKey(
            uid: uid, label: label,
            fingerprint: HostAuth.fingerprint(publicKeyX963: key.publicKey.x963Representation),
            requiresPresence: requirePresence, keyBlob: key.dataRepresentation)
        return (stored, key)
    }

    static func save(_ key: StoredKey) throws {
        try FileManager.default.createDirectory(
            at: directory, withIntermediateDirectories: true,
            attributes: [.posixPermissions: 0o700])
        let data = try JSONEncoder().encode(key)
        try data.write(to: url(uid: key.uid), options: [.atomic])
        try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: url(uid: key.uid).path)
    }

    static func load(uid: String) throws -> StoredKey {
        guard let data = try? Data(contentsOf: url(uid: uid)) else { throw KeyStoreError.missing(uid) }
        return try JSONDecoder().decode(StoredKey.self, from: data)
    }

    static func delete(uid: String) throws {
        try FileManager.default.removeItem(at: url(uid: uid))
    }

    /// Signs with the enclave key; prompts for Touch ID if the key requires presence.
    static func sign(_ message: Data, with stored: StoredKey, reason: String) throws -> Data {
        let context = LAContext()
        context.localizedReason = reason
        let key = try SecureEnclave.P256.Signing.PrivateKey(
            dataRepresentation: stored.keyBlob, authenticationContext: context)
        // CryptoKit hashes with SHA-256; rawRepresentation is r || s (64 bytes)
        return try key.signature(for: message).rawRepresentation
    }
}
