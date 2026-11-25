/**
 * @file host_auth_core.h
 * Host sign-in: pure logic, no Furi dependencies (host-testable).
 *
 * The Flipper is a minimal WebAuthn relying party. An enrolled, device-bound
 * FIDO2 credential (Mac Secure Enclave key, Windows Hello, a FIDO2 security key)
 * proves possession by signing a one-time challenge in the standard WebAuthn
 * assertion format. See documentation/HostAuth.md for the protocol and threat
 * model.
 *
 * Everything the Flipper parses from the host is fixed-format: 37 bytes of
 * authenticator data, a strict-DER ECDSA signature, hex and a printable label.
 * It never parses JSON or CBOR: it builds the client data JSON itself.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HOST_AUTH_PROTOCOL_VERSION (2u)

#define HOST_AUTH_PUBKEY_LEN      (65u) /**< SEC1 uncompressed P-256 point: 04 || X || Y */
#define HOST_AUTH_SCALAR_LEN      (32u)
#define HOST_AUTH_SIG_DER_MIN     (8u) /**< 30 06 02 01 r 02 01 s */
#define HOST_AUTH_SIG_DER_MAX     (72u) /**< 30 46 02 21 00 r(32) 02 21 00 s(32) */
#define HOST_AUTH_NONCE_LEN       (32u)
#define HOST_AUTH_UID_LEN         (8u)
#define HOST_AUTH_LABEL_LEN       (30u) /**< Including the terminating NUL */
#define HOST_AUTH_MAX_KEYS        (4u)
#define HOST_AUTH_FINGERPRINT_LEN (8u)
#define HOST_AUTH_MAC_LEN         (16u)
#define HOST_AUTH_HASH_LEN        (32u)

/** WebAuthn relying party. Domain-shaped so platform WebAuthn APIs accept it. */
#define HOST_AUTH_RP_ID         "hostauth.flipware.local"
#define HOST_AUTH_ORIGIN        "https://hostauth.flipware.local"
#define HOST_AUTH_AUTH_DATA_LEN (37u) /**< rpIdHash(32) || flags(1) || signCount(4, BE) */

/** Authenticator data flags (WebAuthn §6.1) */
#define HOST_AUTH_FLAG_UP (0x01u) /**< User present */
#define HOST_AUTH_FLAG_UV (0x04u) /**< User verified (biometry or PIN on the authenticator) */
#define HOST_AUTH_FLAG_BE (0x08u) /**< Backup eligible: the credential can be synced */
#define HOST_AUTH_FLAG_BS (0x10u) /**< Backed up */
#define HOST_AUTH_FLAG_AT (0x40u) /**< Attested credential data follows */
#define HOST_AUTH_FLAG_ED (0x80u) /**< Extensions follow */

/** Domain separation tag. Never reuse an enrolled key for anything else. */
#define HOST_AUTH_TAG     "flipware-hostauth-v2"
#define HOST_AUTH_TAG_LEN (sizeof(HOST_AUTH_TAG) - 1)

/**
 * Binding: what the WebAuthn challenge carries.
 * tag(20) || action(1) || uid_len(1) || uid(8) || epoch(4, BE) || counter(4, BE) || nonce(32)
 */
#define HOST_AUTH_BINDING_LEN \
    (HOST_AUTH_TAG_LEN + 2 + HOST_AUTH_UID_LEN + 4 + 4 + HOST_AUTH_NONCE_LEN)
/** base64url without padding of the binding: ceil(70 * 4 / 3) */
#define HOST_AUTH_CHALLENGE_B64_LEN ((HOST_AUTH_BINDING_LEN * 4 + 2) / 3)
/** Upper bound for the client data JSON the Flipper builds (asserted in the core) */
#define HOST_AUTH_CLIENT_DATA_MAX   (224u)

#define HOST_AUTH_RECORD_MAGIC   (0x41485746u) /**< "FWHA" little endian */
#define HOST_AUTH_RECORD_VERSION (2u)
#define HOST_AUTH_HEADER_LEN     (16u)
/** pubkey(65) || in_use(1) || flags(1) || sign_count(4, BE) || label(30) || pad(3) */
#define HOST_AUTH_KEY_ENTRY_LEN  (HOST_AUTH_PUBKEY_LEN + 1 + 1 + 4 + HOST_AUTH_LABEL_LEN + 3)
#define HOST_AUTH_RECORD_LEN     (HOST_AUTH_HEADER_LEN + HOST_AUTH_MAX_KEYS * HOST_AUTH_KEY_ENTRY_LEN)
#define HOST_AUTH_FILE_LEN       (HOST_AUTH_RECORD_LEN + HOST_AUTH_MAC_LEN)

/** Per-key policy bits stored in the record */
#define HOST_AUTH_KEY_REQUIRE_UV (0x01u)

#define HOST_AUTH_CHALLENGE_TTL_MS (30u * 1000u)
#define HOST_AUTH_FREE_FAILS       (3u)
#define HOST_AUTH_BACKOFF_BASE_S   (30u)
#define HOST_AUTH_BACKOFF_MAX_S    (3600u)

/** Longest `hostauth ...` argument string the parser accepts */
#define HOST_AUTH_ARGS_MAX (320u)

typedef enum {
    HostAuthActionUnlock = 1,
} HostAuthAction;

typedef struct {
    uint8_t pubkey[HOST_AUTH_PUBKEY_LEN];
    uint8_t flags; /**< HOST_AUTH_KEY_* */
    uint32_t sign_count; /**< Last authenticator signCount seen */
    char label[HOST_AUTH_LABEL_LEN];
} HostAuthKey;

typedef struct {
    uint8_t count;
    uint32_t epoch; /**< Anti-rollback value, must match the RTC register */
    HostAuthKey keys[HOST_AUTH_MAX_KEYS];
} HostAuthRecord;

typedef enum {
    HostAuthAddOk,
    HostAuthAddInvalidKey,
    HostAuthAddDuplicate,
    HostAuthAddFull,
} HostAuthAddResult;

typedef enum {
    HostAuthVerifyOk,
    HostAuthVerifyBadSignature, /**< No enrolled key signed this */
    HostAuthVerifyRpMismatch, /**< Assertion made for a different relying party */
    HostAuthVerifyNoPresence, /**< UP flag clear */
    HostAuthVerifyNoVerification, /**< UV flag clear on a key that requires it */
    HostAuthVerifySynced, /**< BE/BS set: not a device-bound credential */
    HostAuthVerifyBadFlags, /**< AT/ED set: assertions carry neither */
    HostAuthVerifyCounter, /**< signCount went backwards: possible cloned authenticator */
} HostAuthVerifyResult;

typedef enum {
    HostAuthCmdInvalid,
    HostAuthCmdInfo,
    HostAuthCmdChallenge,
    HostAuthCmdResponse,
    HostAuthCmdEnroll,
    HostAuthCmdRevoke,
} HostAuthCmdType;

typedef struct {
    HostAuthCmdType type;
    union {
        struct {
            uint8_t auth_data[HOST_AUTH_AUTH_DATA_LEN];
            uint8_t r[HOST_AUTH_SCALAR_LEN];
            uint8_t s[HOST_AUTH_SCALAR_LEN];
        } response;
        struct {
            uint8_t pubkey[HOST_AUTH_PUBKEY_LEN];
            bool require_uv;
            char label[HOST_AUTH_LABEL_LEN];
        } enroll;
        struct {
            bool all;
            uint8_t fp[HOST_AUTH_FINGERPRINT_LEN];
        } revoke;
    };
} HostAuthCommand;

/** Constant-time equality, for MACs and hashes. */
bool host_auth_ct_equal(const uint8_t* a, const uint8_t* b, size_t len);

/** Build the binding bytes carried in the challenge. Returns HOST_AUTH_BINDING_LEN. */
size_t host_auth_binding_build(
    uint8_t out[HOST_AUTH_BINDING_LEN],
    HostAuthAction action,
    const uint8_t uid[HOST_AUTH_UID_LEN],
    uint32_t epoch,
    uint32_t counter,
    const uint8_t nonce[HOST_AUTH_NONCE_LEN]);

/** base64url without padding. out must hold ceil(len*4/3)+1 chars. Returns the length. */
size_t host_auth_base64url_encode(const uint8_t* in, size_t len, char* out);

/**
 * The exact client data JSON both sides hash:
 * {"type":"webauthn.get","challenge":"<b64url(binding)>","origin":"<HOST_AUTH_ORIGIN>","crossOrigin":false}
 * Returns its length (no NUL is counted; out is NUL-terminated).
 */
size_t host_auth_client_data_build(
    const uint8_t binding[HOST_AUTH_BINDING_LEN],
    char out[HOST_AUTH_CLIENT_DATA_MAX]);

/** True if pubkey is a valid uncompressed point on P-256 (not infinity, coordinates < p). */
bool host_auth_pubkey_is_valid(const uint8_t pubkey[HOST_AUTH_PUBKEY_LEN]);

/**
 * Strict DER ECDSA-Sig-Value: SEQUENCE { INTEGER r, INTEGER s }, short-form
 * lengths, minimal positive INTEGERs, nothing trailing, 1 <= r, s < n.
 * Writes r and s as 32-byte big-endian.
 */
bool host_auth_der_parse_signature(
    const uint8_t* der,
    size_t len,
    uint8_t r[HOST_AUTH_SCALAR_LEN],
    uint8_t s[HOST_AUTH_SCALAR_LEN]);

/** Canonical DER for (r, s). out must hold HOST_AUTH_SIG_DER_MAX bytes. Returns the length. */
size_t host_auth_der_encode_signature(
    const uint8_t r[HOST_AUTH_SCALAR_LEN],
    const uint8_t s[HOST_AUTH_SCALAR_LEN],
    uint8_t out[HOST_AUTH_SIG_DER_MAX]);

/** ECDSA P-256 verify of (r, s) over a 32-byte SHA-256 digest. */
bool host_auth_verify_digest(
    const uint8_t pubkey[HOST_AUTH_PUBKEY_LEN],
    const uint8_t digest[HOST_AUTH_HASH_LEN],
    const uint8_t r[HOST_AUTH_SCALAR_LEN],
    const uint8_t s[HOST_AUTH_SCALAR_LEN]);

/** signCount rule: 0 -> 0 is allowed (authenticator has no counter), otherwise strictly up. */
bool host_auth_sign_count_ok(uint32_t stored, uint32_t received);

/**
 * Verify a WebAuthn assertion over the binding against every enrolled key.
 * On HostAuthVerifyOk, *signer is the key index and *new_count the received
 * signCount. On flag/counter failures *signer is the key whose signature
 * verified (the assertion was genuine but not acceptable); otherwise -1.
 */
HostAuthVerifyResult host_auth_verify_assertion(
    const HostAuthRecord* record,
    const uint8_t binding[HOST_AUTH_BINDING_LEN],
    const uint8_t auth_data[HOST_AUTH_AUTH_DATA_LEN],
    const uint8_t r[HOST_AUTH_SCALAR_LEN],
    const uint8_t s[HOST_AUTH_SCALAR_LEN],
    int* signer,
    uint32_t* new_count);

/** First 8 bytes of SHA-256(pubkey). Shown on both devices at enrollment. */
void host_auth_fingerprint(
    const uint8_t pubkey[HOST_AUTH_PUBKEY_LEN],
    uint8_t fp[HOST_AUTH_FINGERPRINT_LEN]);

/** Fixed-length serialization (fixed length keeps CBC-MAC sound). */
void host_auth_record_serialize(const HostAuthRecord* record, uint8_t out[HOST_AUTH_RECORD_LEN]);

/** Strict parse: magic, version, count, reserved/padding bytes zero, epoch != 0, labels terminated. */
bool host_auth_record_parse(const uint8_t in[HOST_AUTH_RECORD_LEN], HostAuthRecord* record);

/** Anti-rollback: the record's epoch must be nonzero and equal the RTC register. */
bool host_auth_epoch_matches(uint32_t record_epoch, uint32_t register_epoch);

/** Outcome of reading one copy of the key file. */
typedef enum {
    HostAuthFileOk, /**< Good MAC, good structure, current epoch */
    HostAuthFileMissing,
    HostAuthFileUnreadable, /**< Storage or enclave error: says nothing about the file */
    HostAuthFileInvalid, /**< Wrong length, bad MAC or bad structure */
    HostAuthFileStale, /**< Genuine, but its epoch is not the current one */
} HostAuthFileState;

typedef enum {
    HostAuthLoadUseMain, /**< Use the main file (and drop any tmp file) */
    HostAuthLoadRecoverTmp, /**< A save stopped after the epoch moved: promote tmp */
    HostAuthLoadFail, /**< Report the main file's state */
} HostAuthLoadAction;

/**
 * What to do with the main and tmp copies after reading them. Only a tmp file
 * that is itself current can replace the main one, and never when the main
 * file could not be read at all (a transient error must not discard it).
 */
HostAuthLoadAction host_auth_load_decide(HostAuthFileState main, HostAuthFileState tmp);

HostAuthAddResult host_auth_record_add(
    HostAuthRecord* record,
    const uint8_t pubkey[HOST_AUTH_PUBKEY_LEN],
    bool require_uv,
    const char* label);

/** Remove the key whose fingerprint matches. Returns true if one was removed. */
bool host_auth_record_remove(HostAuthRecord* record, const uint8_t fp[HOST_AUTH_FINGERPRINT_LEN]);

/** Decode exactly out_len bytes of hex (2*out_len chars, nothing else). */
bool host_auth_hex_decode(const char* hex, size_t hex_len, uint8_t* out, size_t out_len);

/** Lower-case hex; out must hold 2*len+1 chars. */
void host_auth_hex_encode(const uint8_t* in, size_t len, char* out);

/**
 * Parse the arguments of `hostauth` (everything after the command name).
 * Grammar (tokens separated by single spaces, printable ASCII only):
 *   info
 *   challenge
 *   response <auth_data: 74 hex> <signature: strict DER, 16..144 hex>
 *   enroll <pubkey: 130 hex> <uv: 0|1> <label: 1..29 printable chars, spaces allowed>
 *   revoke <fingerprint: 16 hex | all>
 * Anything else, including extra tokens, is HostAuthCmdInvalid.
 */
bool host_auth_parse_command(const char* args, size_t len, HostAuthCommand* cmd);

/** Seconds a host must wait before the next challenge after `fails` bad responses. */
uint32_t host_auth_backoff_s(uint32_t fails);

/**
 * Seconds left in the backoff, from persisted state (RTC timestamps).
 * If the clock went backwards (now < last_fail_ts), the wait restarts from now
 * and *clamped_last_fail_ts says so; callers persist it. Never longer than
 * HOST_AUTH_BACKOFF_MAX_S.
 */
uint32_t host_auth_backoff_remaining_s(
    uint32_t fails,
    uint32_t last_fail_ts,
    uint32_t now,
    uint32_t* clamped_last_fail_ts);

#ifdef __cplusplus
}
#endif
