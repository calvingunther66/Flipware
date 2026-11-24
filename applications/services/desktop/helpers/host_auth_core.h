/**
 * @file host_auth_core.h
 * Host sign-in: pure logic, no Furi dependencies (host-testable).
 *
 * A computer (e.g. a Mac holding a P-256 key in its Secure Enclave) proves
 * possession of an enrolled key by signing a one-time challenge. See
 * documentation/HostAuth.md for the protocol and threat model.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HOST_AUTH_PUBKEY_LEN      (65u) /**< SEC1 uncompressed P-256 point: 04 || X || Y */
#define HOST_AUTH_SIG_LEN         (64u) /**< Raw ECDSA signature r || s (CryptoKit rawRepresentation) */
#define HOST_AUTH_NONCE_LEN       (32u)
#define HOST_AUTH_UID_LEN         (8u)
#define HOST_AUTH_LABEL_LEN       (30u) /**< Including the terminating NUL */
#define HOST_AUTH_MAX_KEYS        (4u)
#define HOST_AUTH_FINGERPRINT_LEN (8u)
#define HOST_AUTH_MAC_LEN         (16u)

/** Domain separation tag. Never reuse the enrolled key for anything else. */
#define HOST_AUTH_TAG     "flipware-hostauth-v1"
#define HOST_AUTH_TAG_LEN (sizeof(HOST_AUTH_TAG) - 1)

/** tag || action(1) || uid_len(1) || uid(8) || nonce(32) */
#define HOST_AUTH_MESSAGE_LEN (HOST_AUTH_TAG_LEN + 2 + HOST_AUTH_UID_LEN + HOST_AUTH_NONCE_LEN)

#define HOST_AUTH_RECORD_MAGIC   (0x41485746u) /**< "FWHA" little endian */
#define HOST_AUTH_RECORD_VERSION (1u)
#define HOST_AUTH_KEY_ENTRY_LEN  (HOST_AUTH_PUBKEY_LEN + 1 + HOST_AUTH_LABEL_LEN) /**< 96 */
#define HOST_AUTH_RECORD_LEN     (16u + HOST_AUTH_MAX_KEYS * HOST_AUTH_KEY_ENTRY_LEN) /**< 400 */
#define HOST_AUTH_FILE_LEN       (HOST_AUTH_RECORD_LEN + HOST_AUTH_MAC_LEN) /**< 416 */

#define HOST_AUTH_CHALLENGE_TTL_MS (30u * 1000u)
#define HOST_AUTH_FREE_FAILS       (3u)
#define HOST_AUTH_BACKOFF_BASE_S   (30u)
#define HOST_AUTH_BACKOFF_MAX_S    (3600u)

typedef enum {
    HostAuthActionUnlock = 1,
} HostAuthAction;

typedef struct {
    uint8_t pubkey[HOST_AUTH_PUBKEY_LEN];
    char label[HOST_AUTH_LABEL_LEN];
} HostAuthKey;

typedef struct {
    uint8_t count;
    HostAuthKey keys[HOST_AUTH_MAX_KEYS];
} HostAuthRecord;

typedef enum {
    HostAuthAddOk,
    HostAuthAddInvalidKey,
    HostAuthAddDuplicate,
    HostAuthAddFull,
} HostAuthAddResult;

/** Build the exact byte string the host signs. Returns HOST_AUTH_MESSAGE_LEN. */
size_t host_auth_message_build(
    uint8_t out[HOST_AUTH_MESSAGE_LEN],
    HostAuthAction action,
    const uint8_t uid[HOST_AUTH_UID_LEN],
    const uint8_t nonce[HOST_AUTH_NONCE_LEN]);

/** True if pubkey is a valid uncompressed point on P-256. */
bool host_auth_pubkey_is_valid(const uint8_t pubkey[HOST_AUTH_PUBKEY_LEN]);

/** ECDSA P-256 / SHA-256 verify of a raw r||s signature over msg. */
bool host_auth_verify(
    const uint8_t pubkey[HOST_AUTH_PUBKEY_LEN],
    const uint8_t* msg,
    size_t msg_len,
    const uint8_t sig[HOST_AUTH_SIG_LEN]);

/** First 8 bytes of SHA-256(pubkey). Shown on both devices at enrollment. */
void host_auth_fingerprint(
    const uint8_t pubkey[HOST_AUTH_PUBKEY_LEN],
    uint8_t fp[HOST_AUTH_FINGERPRINT_LEN]);

/** Fixed-length serialization (fixed length keeps CBC-MAC sound). */
void host_auth_record_serialize(const HostAuthRecord* record, uint8_t out[HOST_AUTH_RECORD_LEN]);

/** Strict parse: magic, version, count, and that unused slots are zero. */
bool host_auth_record_parse(const uint8_t in[HOST_AUTH_RECORD_LEN], HostAuthRecord* record);

HostAuthAddResult host_auth_record_add(
    HostAuthRecord* record,
    const uint8_t pubkey[HOST_AUTH_PUBKEY_LEN],
    const char* label);

/** Remove the key whose fingerprint matches. Returns true if one was removed. */
bool host_auth_record_remove(HostAuthRecord* record, const uint8_t fp[HOST_AUTH_FINGERPRINT_LEN]);

/** Index of the first enrolled key that verifies sig over msg, or -1. */
int host_auth_record_find_signer(
    const HostAuthRecord* record,
    const uint8_t* msg,
    size_t msg_len,
    const uint8_t sig[HOST_AUTH_SIG_LEN]);

/** Decode exactly out_len bytes of hex (2*out_len chars, nothing else). */
bool host_auth_hex_decode(const char* hex, size_t hex_len, uint8_t* out, size_t out_len);

/** Lower-case hex; out must hold 2*len+1 chars. */
void host_auth_hex_encode(const uint8_t* in, size_t len, char* out);

/** Seconds a host must wait before the next challenge after `fails` bad responses. */
uint32_t host_auth_backoff_s(uint32_t fails);

#ifdef __cplusplus
}
#endif
