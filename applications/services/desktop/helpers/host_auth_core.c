#include "host_auth_core.h"

#include <string.h>

// Needed to set the point coordinates directly (see host_auth_load_point)
#define MBEDTLS_ALLOW_PRIVATE_ACCESS

#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/sha256.h>

size_t host_auth_message_build(
    uint8_t out[HOST_AUTH_MESSAGE_LEN],
    HostAuthAction action,
    const uint8_t uid[HOST_AUTH_UID_LEN],
    const uint8_t nonce[HOST_AUTH_NONCE_LEN]) {
    size_t pos = 0;
    memcpy(out + pos, HOST_AUTH_TAG, HOST_AUTH_TAG_LEN);
    pos += HOST_AUTH_TAG_LEN;
    out[pos++] = (uint8_t)action;
    out[pos++] = HOST_AUTH_UID_LEN;
    memcpy(out + pos, uid, HOST_AUTH_UID_LEN);
    pos += HOST_AUTH_UID_LEN;
    memcpy(out + pos, nonce, HOST_AUTH_NONCE_LEN);
    pos += HOST_AUTH_NONCE_LEN;
    return pos;
}

static bool host_auth_load_point(
    mbedtls_ecp_group* grp,
    mbedtls_ecp_point* q,
    const uint8_t pubkey[HOST_AUTH_PUBKEY_LEN]) {
    if(pubkey[0] != 0x04) return false; // uncompressed only
    if(mbedtls_ecp_group_load(grp, MBEDTLS_ECP_DP_SECP256R1) != 0) return false;
    // Read X and Y directly instead of mbedtls_ecp_point_read_binary(): that one also
    // accepts compressed points, which pulls in modular square roots and code the
    // firmware's trimmed mbedtls build does not include.
    if(mbedtls_mpi_read_binary(&q->MBEDTLS_PRIVATE(X), pubkey + 1, 32) != 0) return false;
    if(mbedtls_mpi_read_binary(&q->MBEDTLS_PRIVATE(Y), pubkey + 33, 32) != 0) return false;
    if(mbedtls_mpi_lset(&q->MBEDTLS_PRIVATE(Z), 1) != 0) return false;
    // Checks 0 <= X, Y < p and that the point is on the curve
    return mbedtls_ecp_check_pubkey(grp, q) == 0;
}

bool host_auth_pubkey_is_valid(const uint8_t pubkey[HOST_AUTH_PUBKEY_LEN]) {
    mbedtls_ecp_group grp;
    mbedtls_ecp_point q;
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&q);
    bool ok = host_auth_load_point(&grp, &q, pubkey);
    mbedtls_ecp_point_free(&q);
    mbedtls_ecp_group_free(&grp);
    return ok;
}

bool host_auth_verify(
    const uint8_t pubkey[HOST_AUTH_PUBKEY_LEN],
    const uint8_t* msg,
    size_t msg_len,
    const uint8_t sig[HOST_AUTH_SIG_LEN]) {
    uint8_t hash[32];
    if(mbedtls_sha256(msg, msg_len, hash, 0) != 0) return false;

    mbedtls_ecp_group grp;
    mbedtls_ecp_point q;
    mbedtls_mpi r, s;
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&q);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    bool ok = false;
    do {
        if(!host_auth_load_point(&grp, &q, pubkey)) break;
        if(mbedtls_mpi_read_binary(&r, sig, HOST_AUTH_SIG_LEN / 2) != 0) break;
        if(mbedtls_mpi_read_binary(&s, sig + HOST_AUTH_SIG_LEN / 2, HOST_AUTH_SIG_LEN / 2) != 0)
            break;
        // mbedtls rejects r, s outside [1, n-1]
        ok = mbedtls_ecdsa_verify(&grp, hash, sizeof(hash), &q, &r, &s) == 0;
    } while(0);

    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecp_point_free(&q);
    mbedtls_ecp_group_free(&grp);
    return ok;
}

void host_auth_fingerprint(
    const uint8_t pubkey[HOST_AUTH_PUBKEY_LEN],
    uint8_t fp[HOST_AUTH_FINGERPRINT_LEN]) {
    uint8_t hash[32];
    mbedtls_sha256(pubkey, HOST_AUTH_PUBKEY_LEN, hash, 0);
    memcpy(fp, hash, HOST_AUTH_FINGERPRINT_LEN);
}

static void host_auth_put_u32_le(uint8_t* out, uint32_t v) {
    out[0] = v & 0xFF;
    out[1] = (v >> 8) & 0xFF;
    out[2] = (v >> 16) & 0xFF;
    out[3] = (v >> 24) & 0xFF;
}

static uint32_t host_auth_get_u32_le(const uint8_t* in) {
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) |
           ((uint32_t)in[3] << 24);
}

void host_auth_record_serialize(const HostAuthRecord* record, uint8_t out[HOST_AUTH_RECORD_LEN]) {
    memset(out, 0, HOST_AUTH_RECORD_LEN);
    host_auth_put_u32_le(out, HOST_AUTH_RECORD_MAGIC);
    out[4] = HOST_AUTH_RECORD_VERSION;
    out[5] = record->count;
    for(uint8_t i = 0; i < record->count && i < HOST_AUTH_MAX_KEYS; i++) {
        uint8_t* entry = out + 16 + i * HOST_AUTH_KEY_ENTRY_LEN;
        memcpy(entry, record->keys[i].pubkey, HOST_AUTH_PUBKEY_LEN);
        entry[HOST_AUTH_PUBKEY_LEN] = 1; // in use
        memcpy(entry + HOST_AUTH_PUBKEY_LEN + 1, record->keys[i].label, HOST_AUTH_LABEL_LEN);
        entry[HOST_AUTH_KEY_ENTRY_LEN - 1] = '\0';
    }
}

static bool host_auth_all_zero(const uint8_t* p, size_t n) {
    uint8_t acc = 0;
    for(size_t i = 0; i < n; i++)
        acc |= p[i];
    return acc == 0;
}

bool host_auth_record_parse(const uint8_t in[HOST_AUTH_RECORD_LEN], HostAuthRecord* record) {
    memset(record, 0, sizeof(*record));
    if(host_auth_get_u32_le(in) != HOST_AUTH_RECORD_MAGIC) return false;
    if(in[4] != HOST_AUTH_RECORD_VERSION) return false;
    if(in[5] > HOST_AUTH_MAX_KEYS) return false;
    if(!host_auth_all_zero(in + 6, 10)) return false;

    for(uint8_t i = 0; i < HOST_AUTH_MAX_KEYS; i++) {
        const uint8_t* entry = in + 16 + i * HOST_AUTH_KEY_ENTRY_LEN;
        if(i >= in[5]) {
            if(!host_auth_all_zero(entry, HOST_AUTH_KEY_ENTRY_LEN)) return false;
            continue;
        }
        if(entry[HOST_AUTH_PUBKEY_LEN] != 1) return false;
        if(entry[HOST_AUTH_KEY_ENTRY_LEN - 1] != '\0') return false;
        memcpy(record->keys[i].pubkey, entry, HOST_AUTH_PUBKEY_LEN);
        memcpy(record->keys[i].label, entry + HOST_AUTH_PUBKEY_LEN + 1, HOST_AUTH_LABEL_LEN);
    }
    record->count = in[5];
    return true;
}

static void host_auth_label_copy(char out[HOST_AUTH_LABEL_LEN], const char* label) {
    memset(out, 0, HOST_AUTH_LABEL_LEN);
    size_t n = 0;
    for(const char* c = label ? label : ""; *c && n < HOST_AUTH_LABEL_LEN - 1; c++) {
        // Printable ASCII only: the label is drawn on the Flipper's screen
        out[n++] = (*c >= 0x20 && *c <= 0x7E) ? *c : '?';
    }
    if(n == 0) memcpy(out, "host", 5);
}

HostAuthAddResult host_auth_record_add(
    HostAuthRecord* record,
    const uint8_t pubkey[HOST_AUTH_PUBKEY_LEN],
    const char* label) {
    if(!host_auth_pubkey_is_valid(pubkey)) return HostAuthAddInvalidKey;
    for(uint8_t i = 0; i < record->count; i++) {
        if(memcmp(record->keys[i].pubkey, pubkey, HOST_AUTH_PUBKEY_LEN) == 0)
            return HostAuthAddDuplicate;
    }
    if(record->count >= HOST_AUTH_MAX_KEYS) return HostAuthAddFull;

    HostAuthKey* key = &record->keys[record->count];
    memcpy(key->pubkey, pubkey, HOST_AUTH_PUBKEY_LEN);
    host_auth_label_copy(key->label, label);
    record->count++;
    return HostAuthAddOk;
}

bool host_auth_record_remove(HostAuthRecord* record, const uint8_t fp[HOST_AUTH_FINGERPRINT_LEN]) {
    for(uint8_t i = 0; i < record->count; i++) {
        uint8_t key_fp[HOST_AUTH_FINGERPRINT_LEN];
        host_auth_fingerprint(record->keys[i].pubkey, key_fp);
        if(memcmp(key_fp, fp, HOST_AUTH_FINGERPRINT_LEN) == 0) {
            for(uint8_t j = i; j + 1 < record->count; j++) {
                record->keys[j] = record->keys[j + 1];
            }
            record->count--;
            memset(&record->keys[record->count], 0, sizeof(HostAuthKey));
            return true;
        }
    }
    return false;
}

int host_auth_record_find_signer(
    const HostAuthRecord* record,
    const uint8_t* msg,
    size_t msg_len,
    const uint8_t sig[HOST_AUTH_SIG_LEN]) {
    for(uint8_t i = 0; i < record->count && i < HOST_AUTH_MAX_KEYS; i++) {
        if(host_auth_verify(record->keys[i].pubkey, msg, msg_len, sig)) return i;
    }
    return -1;
}

static int host_auth_hex_nibble(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool host_auth_hex_decode(const char* hex, size_t hex_len, uint8_t* out, size_t out_len) {
    if(!hex || hex_len != out_len * 2) return false;
    for(size_t i = 0; i < out_len; i++) {
        int hi = host_auth_hex_nibble(hex[2 * i]);
        int lo = host_auth_hex_nibble(hex[2 * i + 1]);
        if(hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

void host_auth_hex_encode(const uint8_t* in, size_t len, char* out) {
    static const char digits[] = "0123456789abcdef";
    for(size_t i = 0; i < len; i++) {
        out[2 * i] = digits[in[i] >> 4];
        out[2 * i + 1] = digits[in[i] & 0x0F];
    }
    out[2 * len] = '\0';
}

uint32_t host_auth_backoff_s(uint32_t fails) {
    if(fails < HOST_AUTH_FREE_FAILS) return 0;
    uint32_t wait = HOST_AUTH_BACKOFF_BASE_S;
    for(uint32_t i = HOST_AUTH_FREE_FAILS; i < fails; i++) {
        wait *= 2;
        if(wait >= HOST_AUTH_BACKOFF_MAX_S) return HOST_AUTH_BACKOFF_MAX_S;
    }
    return wait;
}
