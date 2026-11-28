// Secure Enclave key per Flipper. The private key never leaves the enclave; what is
// stored on disk is the enclave's wrapped blob, which only this Mac's enclave can use.
import CryptoKit
import Foundation
import LocalAuthentication
import Security

enum KeyPolicy: String, Codable {
    /// Touch ID with the fingerprints enrolled today. Adding or removing a finger
    /// makes the key permanently unusable (re-enroll after a PIN unlock).
    case biometryCurrentSet = "biometry-current-set"
    /// Touch ID, Apple Watch or the login password. Survives fingerprint changes,
    /// but anyone who knows the Mac's password can approve a sign-in.
    case userPresence = "user-presence"
}

struct StoredKey: Codable {
    var version: Int
    var uid: String  // Flipper UID, hex
    var label: String
    var fingerprint: String
    var policy: KeyPolicy
    var keyBlob: Data  // SecureEnclave.P256.Signing.PrivateKey.dataRepresentation
    var biometryState: Data?  // Touch ID set at enrollment, to explain an invalidated key
}

enum KeyStoreError: Error, CustomStringConvertible {
    case noSecureEnclave, noTouchID, accessControl, missing(String), oldFormat(String)
    case cancelled, invalidated(String), signing(String)

    var description: String {
        switch self {
        case .noSecureEnclave: return "This Mac has no Secure Enclave."
        case .noTouchID:
            return "Touch ID is not available or has no fingers enrolled. Set it up, or enroll with --allow-password."
        case .accessControl: return "Could not create the key's access control."
        case .missing(let uid): return "No key for Flipper \(uid) on this Mac. Run `flipware-auth enroll` first."
        case .oldFormat(let uid):
            return "The key for Flipper \(uid) is from an older version. Run `flipware-auth forget --local-only`, then enroll again."
        case .cancelled: return "Cancelled on the Mac; the Flipper stays locked."
        case .invalidated(let uid):
            return """
                This Mac's key for Flipper \(uid) can no longer be used: the Touch ID fingerprints changed \
                since enrollment, so the Secure Enclave invalidated it (that is what --allow-password avoids). \
                Unlock the Flipper with its PIN, then run `flipware-auth forget` and `flipware-auth enroll`.
                """
        case .signing(let why): return "The Secure Enclave did not sign: \(why)"
        }
    }
}

enum KeyStore {
    static var directory: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Flipware/hostauth", isDirectory: true)
    }

    static func url(uid: String) -> URL { directory.appendingPathComponent("\(uid).json") }

    static func biometryState() -> Data? {
        let context = LAContext()
        var error: NSError?
        guard context.canEvaluatePolicy(.deviceOwnerAuthenticationWithBiometrics, error: &error) else {
            return nil
        }
        if #available(macOS 15.0, *) {
            return context.domainState.biometry.stateHash
        } else {
            return context.evaluatedPolicyDomainState
        }
    }

    /// The key's access control: the Secure Enclave asks for Touch ID (or the password)
    /// on every use. Shared by key creation and by the grant's one-time approval.
    static func accessControl(policy: KeyPolicy) throws -> SecAccessControl {
        var flags: SecAccessControlCreateFlags = [.privateKeyUsage]
        switch policy {
        case .biometryCurrentSet: flags.insert(.biometryCurrentSet)
        case .userPresence: flags.insert(.userPresence)
        }
        guard
            let access = SecAccessControlCreateWithFlags(
                nil, kSecAttrAccessibleWhenUnlockedThisDeviceOnly, flags, nil)
        else { throw KeyStoreError.accessControl }
        return access
    }

    /// UIDs of every Flipper this Mac has a key for.
    static func storedUIDs() -> [String] {
        let files = (try? FileManager.default.contentsOfDirectory(atPath: directory.path)) ?? []
        return files.filter { $0.hasSuffix(".json") }.map { String($0.dropLast(5)) }.sorted()
    }

    /// Throws when a Touch ID finger change has permanently invalidated the key.
    static func checkNotInvalidated(_ stored: StoredKey) throws {
        if stored.policy == .biometryCurrentSet, let then = stored.biometryState,
            let now = biometryState(), now != then
        {
            throw KeyStoreError.invalidated(stored.uid)
        }
    }

    static func create(uid: String, label: String, policy: KeyPolicy) throws
        -> (StoredKey, SecureEnclave.P256.Signing.PrivateKey)
    {
        guard SecureEnclave.isAvailable else { throw KeyStoreError.noSecureEnclave }
        let state = biometryState()
        if policy == .biometryCurrentSet && state == nil { throw KeyStoreError.noTouchID }

        // Every signature needs a fresh approval: there is no "reuse" setting on the
        // key itself, and each sign() below uses a new LAContext with reuse off.
        // (The timed grant in Grant.swift is the one deliberate exception.)
        let access = try accessControl(policy: policy)

        let key = try SecureEnclave.P256.Signing.PrivateKey(accessControl: access)
        let stored = StoredKey(
            version: 2, uid: uid, label: label,
            fingerprint: HostAuth.fingerprint(publicKeyX963: key.publicKey.x963Representation),
            policy: policy, keyBlob: key.dataRepresentation, biometryState: state)
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
        guard let key = try? JSONDecoder().decode(StoredKey.self, from: data), key.version == 2 else {
            throw KeyStoreError.oldFormat(uid)
        }
        return key
    }

    static func delete(uid: String) throws {
        try FileManager.default.removeItem(at: url(uid: uid))
    }

    /// Signs SHA-256(message) with the enclave key. The key's access control makes
    /// the enclave ask for Touch ID (or the password) for this one signature.
    static func sign(_ message: Data, with stored: StoredKey, reason: String) throws -> Data {
        let context = LAContext()
        context.localizedReason = reason
        context.touchIDAuthenticationAllowableReuseDuration = 0  // no "recently unlocked" grace
        context.interactionNotAllowed = false

        try checkNotInvalidated(stored)

        do {
            let key = try SecureEnclave.P256.Signing.PrivateKey(
                dataRepresentation: stored.keyBlob, authenticationContext: context)
            // CryptoKit hashes with SHA-256; WebAuthn carries the DER form
            return try key.signature(for: message).derRepresentation
        } catch let error as LAError {
            switch error.code {
            case .userCancel, .appCancel, .systemCancel, .userFallback: throw KeyStoreError.cancelled
            case .biometryNotEnrolled, .biometryNotAvailable:
                throw stored.policy == .biometryCurrentSet
                    ? KeyStoreError.invalidated(stored.uid) : KeyStoreError.signing(error.localizedDescription)
            default: throw KeyStoreError.signing(error.localizedDescription)
            }
        } catch {
            // An invalidated .biometryCurrentSet key fails to load or sign with a
            // CryptoKit / Security error rather than an LAError
            var why = "\(error)"
            if stored.policy == .biometryCurrentSet {
                why += ". If Touch ID fingerprints changed since enrollment, the key was invalidated:"
                why += " unlock the Flipper with its PIN, then `flipware-auth forget` and enroll again."
            }
            throw KeyStoreError.signing(why)
        }
    }
}
