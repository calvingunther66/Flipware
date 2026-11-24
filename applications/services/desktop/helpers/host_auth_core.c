#include "host_auth_core.h"

#include <string.h>

// Needed to set the point coordinates directly (see host_auth_load_point)
#define MBEDTLS_ALLOW_PRIVATE_ACCESS

#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/sha256.h>

// Order of the P-256 group
static const uint8_t host_auth_p256_n[HOST_AUTH_SCALAR_LEN] = {
    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xbc, 0xe6, 0xfa, 0xad, 0xa7, 0x17,
    0x9e, 0x84, 0xf3, 0xb9, 0xca, 0xc2, 0xfc, 0x63, 0x25, 0x51};

// =======
// Helpers
// =======

bool host_auth_ct_equal(const uint8_t* a, const uint8_t* b, size_t len) {
    volatile uint8_t acc = 0;
    for(size_t i = 0; i < len; i++)
        acc |= a[i] ^ b[i];
    return acc == 0;
}

static void host_auth_put_u32_be(uint8_t* out, uint32_t v) {
    out[0] = (v >> 24) & 0xFF;
    out[1] = (v >> 16) & 0xFF;
    out[2] = (v >> 8) & 0xFF;
    out[3] = v & 0xFF;
}

static uint32_t host_auth_get_u32_be(const uint8_t* in) {
    return ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) | ((uint32_t)in[2] << 8) |
           (uint32_t)in[3];
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

static bool host_auth_all_zero(const uint8_t* p, size_t n) {
    uint8_t acc = 0;
    for(size_t i = 0; i < n; i++)
        acc |= p[i];
    return acc == 0;
}

static bool host_auth_is_printable(char c) {
    return c >= 0x20 && c <= 0x7E;
}

static void host_auth_sha256(const uint8_t* in, size_t len, uint8_t out[HOST_AUTH_HASH_LEN]) {
    mbedtls_sha256(in, len, out, 0);
}

// ==========================
// Binding and client data
// ==========================

size_t host_auth_binding_build(
    uint8_t out[HOST_AUTH_BINDING_LEN],
    HostAuthAction action,
    const uint8_t uid[HOST_AUTH_UID_LEN],
    uint32_t epoch,
    uint32_t counter,
    const uint8_t nonce[HOST_AUTH_NONCE_LEN]) {
    size_t pos = 0;
    memcpy(out + pos, HOST_AUTH_TAG, HOST_AUTH_TAG_LEN);
    pos += HOST_AUTH_TAG_LEN;
    out[pos++] = (uint8_t)action;
    out[pos++] = HOST_AUTH_UID_LEN;
    memcpy(out + pos, uid, HOST_AUTH_UID_LEN);
    pos += HOST_AUTH_UID_LEN;
    host_auth_put_u32_be(out + pos, epoch);
    pos += 4;
    host_auth_put_u32_be(out + pos, counter);
    pos += 4;
    memcpy(out + pos, nonce, HOST_AUTH_NONCE_LEN);
    pos += HOST_AUTH_NONCE_LEN;
    return pos;
}

size_t host_auth_base64url_encode(const uint8_t* in, size_t len, char* out) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t o = 0;
    size_t i = 0;
    for(; i + 3 <= len; i += 3) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
        out[o++] = alphabet[(v >> 18) & 63];
        out[o++] = alphabet[(v >> 12) & 63];
        out[o++] = alphabet[(v >> 6) & 63];
        out[o++] = alphabet[v & 63];
    }
    if(len - i == 1) {
        uint32_t v = (uint32_t)in[i] << 16;
        out[o++] = alphabet[(v >> 18) & 63];
        out[o++] = alphabet[(v >> 12) & 63];
    } else if(len - i == 2) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8);
        out[o++] = alphabet[(v >> 18) & 63];
        out[o++] = alphabet[(v >> 12) & 63];
        out[o++] = alphabet[(v >> 6) & 63];
    }
    out[o] = '\0';
    return o;
}

#define HOST_AUTH_CD_PREFIX "{\"type\":\"webauthn.get\",\"challenge\":\""
#define HOST_AUTH_CD_SUFFIX "\",\"origin\":\"" HOST_AUTH_ORIGIN "\",\"crossOrigin\":false}"

_Static_assert(
    sizeof(HOST_AUTH_CD_PREFIX) - 1 + HOST_AUTH_CHALLENGE_B64_LEN + sizeof(HOST_AUTH_CD_SUFFIX) <=
        HOST_AUTH_CLIENT_DATA_MAX,
    "client data buffer too small");

size_t host_auth_client_data_build(
    const uint8_t binding[HOST_AUTH_BINDING_LEN],
    char out[HOST_AUTH_CLIENT_DATA_MAX]) {
    size_t pos = 0;
    memcpy(out, HOST_AUTH_CD_PREFIX, sizeof(HOST_AUTH_CD_PREFIX) - 1);
    pos += sizeof(HOST_AUTH_CD_PREFIX) - 1;
    pos += host_auth_base64url_encode(binding, HOST_AUTH_BINDING_LEN, out + pos);
    memcpy(out + pos, HOST_AUTH_CD_SUFFIX, sizeof(HOST_AUTH_CD_SUFFIX)); // with the NUL
    pos += sizeof(HOST_AUTH_CD_SUFFIX) - 1;
    return pos;
}

// =================
// Keys, signatures
// =================

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
    // Checks 0 <= X, Y < p and that the point is on the curve (so not infinity)
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

// 1 <= v < n, big-endian. Public values: no need for constant time.
static bool host_auth_scalar_in_range(const uint8_t v[HOST_AUTH_SCALAR_LEN]) {
    if(host_auth_all_zero(v, HOST_AUTH_SCALAR_LEN)) return false;
    return memcmp(v, host_auth_p256_n, HOST_AUTH_SCALAR_LEN) < 0;
}

// One DER INTEGER, positive, minimally encoded, at most 32 value bytes.
static bool host_auth_der_read_int(
    const uint8_t* p,
    size_t avail,
    size_t* consumed,
    uint8_t out[HOST_AUTH_SCALAR_LEN]) {
    if(avail < 2 || p[0] != 0x02) return false;
    size_t len = p[1];
    // Long-form lengths (0x80 and up) are > 33 and fail here too
    if(len == 0 || len > HOST_AUTH_SCALAR_LEN + 1) return false;
    if(avail - 2 < len) return false;
    const uint8_t* v = p + 2;
    if(v[0] & 0x80) return false; // negative
    if(len > 1 && v[0] == 0x00 && !(v[1] & 0x80)) return false; // superfluous leading zero
    *consumed = 2 + len;
    if(len > 1 && v[0] == 0x00) {
        v++;
        len--;
    }
    if(len > HOST_AUTH_SCALAR_LEN) return false;
    memset(out, 0, HOST_AUTH_SCALAR_LEN);
    memcpy(out + HOST_AUTH_SCALAR_LEN - len, v, len);
    return true;
}

bool host_auth_der_parse_signature(
    const uint8_t* der,
    size_t len,
    uint8_t r[HOST_AUTH_SCALAR_LEN],
    uint8_t s[HOST_AUTH_SCALAR_LEN]) {
    if(!der || len < HOST_AUTH_SIG_DER_MIN || len > HOST_AUTH_SIG_DER_MAX) return false;
    if(der[0] != 0x30) return false; // SEQUENCE
    if(der[1] & 0x80) return false; // short-form length only
    if((size_t)der[1] != len - 2) return false; // nothing trailing, nothing missing

    size_t pos = 2;
    size_t used = 0;
    if(!host_auth_der_read_int(der + pos, len - pos, &used, r)) return false;
    pos += used;
    if(!host_auth_der_read_int(der + pos, len - pos, &used, s)) return false;
    pos += used;
    if(pos != len) return false;

    return host_auth_scalar_in_range(r) && host_auth_scalar_in_range(s);
}

static size_t host_auth_der_write_int(const uint8_t v[HOST_AUTH_SCALAR_LEN], uint8_t* out) {
    size_t start = 0;
    while(start < HOST_AUTH_SCALAR_LEN - 1 && v[start] == 0)
        start++;
    size_t vlen = HOST_AUTH_SCALAR_LEN - start;
    bool pad = (v[start] & 0x80) != 0;
    size_t pos = 0;
    out[pos++] = 0x02;
    out[pos++] = (uint8_t)(vlen + (pad ? 1 : 0));
    if(pad) out[pos++] = 0x00;
    memcpy(out + pos, v + start, vlen);
    return pos + vlen;
}

size_t host_auth_der_encode_signature(
    const uint8_t r[HOST_AUTH_SCALAR_LEN],
    const uint8_t s[HOST_AUTH_SCALAR_LEN],
    uint8_t out[HOST_AUTH_SIG_DER_MAX]) {
    size_t pos = 2;
    pos += host_auth_der_write_int(r, out + pos);
    pos += host_auth_der_write_int(s, out + pos);
    out[0] = 0x30;
    out[1] = (uint8_t)(pos - 2);
    return pos;
}

bool host_auth_verify_digest(
    const uint8_t pubkey[HOST_AUTH_PUBKEY_LEN],
    const uint8_t digest[HOST_AUTH_HASH_LEN],
    const uint8_t r_bytes[HOST_AUTH_SCALAR_LEN],
    const uint8_t s_bytes[HOST_AUTH_SCALAR_LEN]) {
    // Checked again here so this function is safe on its own
    if(!host_auth_scalar_in_range(r_bytes) || !host_auth_scalar_in_range(s_bytes)) return false;

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
        if(mbedtls_mpi_read_binary(&r, r_bytes, HOST_AUTH_SCALAR_LEN) != 0) break;
        if(mbedtls_mpi_read_binary(&s, s_bytes, HOST_AUTH_SCALAR_LEN) != 0) break;
        ok = mbedtls_ecdsa_verify(&grp, digest, HOST_AUTH_HASH_LEN, &q, &r, &s) == 0;
    } while(0);

    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecp_point_free(&q);
    mbedtls_ecp_group_free(&grp);
    return ok;
}

bool host_auth_sign_count_ok(uint32_t stored, uint32_t received) {
    if(stored == 0 && received == 0) return true; // authenticator keeps no counter
    return received > stored;
}

HostAuthVerifyResult host_auth_verify_assertion(
    const HostAuthRecord* record,
    const uint8_t binding[HOST_AUTH_BINDING_LEN],
    const uint8_t auth_data[HOST_AUTH_AUTH_DATA_LEN],
    const uint8_t r[HOST_AUTH_SCALAR_LEN],
    const uint8_t s[HOST_AUTH_SCALAR_LEN],
    int* signer,
    uint32_t* new_count) {
    *signer = -1;
    *new_count = 0;

    uint8_t rp_hash[HOST_AUTH_HASH_LEN];
    host_auth_sha256((const uint8_t*)HOST_AUTH_RP_ID, sizeof(HOST_AUTH_RP_ID) - 1, rp_hash);
    if(!host_auth_ct_equal(auth_data, rp_hash, HOST_AUTH_HASH_LEN)) {
        return HostAuthVerifyRpMismatch;
    }

    // WebAuthn signs authenticatorData || SHA-256(clientDataJSON), and the client
    // data is rebuilt here from our own binding, never taken from the host
    char client_data[HOST_AUTH_CLIENT_DATA_MAX];
    size_t client_data_len = host_auth_client_data_build(binding, client_data);
    uint8_t signed_data[HOST_AUTH_AUTH_DATA_LEN + HOST_AUTH_HASH_LEN];
    memcpy(signed_data, auth_data, HOST_AUTH_AUTH_DATA_LEN);
    host_auth_sha256(
        (const uint8_t*)client_data, client_data_len, signed_data + HOST_AUTH_AUTH_DATA_LEN);
    uint8_t digest[HOST_AUTH_HASH_LEN];
    host_auth_sha256(signed_data, sizeof(signed_data), digest);

    int index = -1;
    for(uint8_t i = 0; i < record->count && i < HOST_AUTH_MAX_KEYS; i++) {
        if(host_auth_verify_digest(record->keys[i].pubkey, digest, r, s)) {
            index = i;
            break;
        }
    }
    if(index < 0) return HostAuthVerifyBadSignature;
    *signer = index;

    const HostAuthKey* key = &record->keys[index];
    uint8_t flags = auth_data[HOST_AUTH_HASH_LEN];
    uint8_t known = HOST_AUTH_FLAG_UP | HOST_AUTH_FLAG_UV | HOST_AUTH_FLAG_BE | HOST_AUTH_FLAG_BS;
    if(flags & ~known) return HostAuthVerifyBadFlags; // AT, ED and reserved bits
    if(flags & (HOST_AUTH_FLAG_BE | HOST_AUTH_FLAG_BS)) return HostAuthVerifySynced;
    if(!(flags & HOST_AUTH_FLAG_UP)) return HostAuthVerifyNoPresence;
    if((key->flags & HOST_AUTH_KEY_REQUIRE_UV) && !(flags & HOST_AUTH_FLAG_UV)) {
        return HostAuthVerifyNoVerification;
    }
    uint32_t count = host_auth_get_u32_be(auth_data + HOST_AUTH_HASH_LEN + 1);
    if(!host_auth_sign_count_ok(key->sign_count, count)) return HostAuthVerifyCounter;

    *new_count = count;
    return HostAuthVerifyOk;
}

void host_auth_fingerprint(
    const uint8_t pubkey[HOST_AUTH_PUBKEY_LEN],
    uint8_t fp[HOST_AUTH_FINGERPRINT_LEN]) {
    uint8_t hash[HOST_AUTH_HASH_LEN];
    host_auth_sha256(pubkey, HOST_AUTH_PUBKEY_LEN, hash);
    memcpy(fp, hash, HOST_AUTH_FINGERPRINT_LEN);
}

// ======
// Record
// ======

// Entry layout offsets
#define HOST_AUTH_E_INUSE (HOST_AUTH_PUBKEY_LEN)
#define HOST_AUTH_E_FLAGS (HOST_AUTH_E_INUSE + 1)
#define HOST_AUTH_E_COUNT (HOST_AUTH_E_FLAGS + 1)
#define HOST_AUTH_E_LABEL (HOST_AUTH_E_COUNT + 4)
#define HOST_AUTH_E_PAD   (HOST_AUTH_E_LABEL + HOST_AUTH_LABEL_LEN)

_Static_assert(HOST_AUTH_E_PAD + 3 == HOST_AUTH_KEY_ENTRY_LEN, "entry layout");
_Static_assert(HOST_AUTH_RECORD_LEN % 16 == 0, "CBC-MAC needs whole AES blocks");

void host_auth_record_serialize(const HostAuthRecord* record, uint8_t out[HOST_AUTH_RECORD_LEN]) {
    memset(out, 0, HOST_AUTH_RECORD_LEN);
    host_auth_put_u32_le(out, HOST_AUTH_RECORD_MAGIC);
    out[4] = HOST_AUTH_RECORD_VERSION;
    out[5] = record->count;
    host_auth_put_u32_be(out + 8, record->epoch);
    for(uint8_t i = 0; i < record->count && i < HOST_AUTH_MAX_KEYS; i++) {
        uint8_t* entry = out + HOST_AUTH_HEADER_LEN + i * HOST_AUTH_KEY_ENTRY_LEN;
        memcpy(entry, record->keys[i].pubkey, HOST_AUTH_PUBKEY_LEN);
        entry[HOST_AUTH_E_INUSE] = 1;
        entry[HOST_AUTH_E_FLAGS] = record->keys[i].flags & HOST_AUTH_KEY_REQUIRE_UV;
        host_auth_put_u32_be(entry + HOST_AUTH_E_COUNT, record->keys[i].sign_count);
        memcpy(entry + HOST_AUTH_E_LABEL, record->keys[i].label, HOST_AUTH_LABEL_LEN);
        entry[HOST_AUTH_E_LABEL + HOST_AUTH_LABEL_LEN - 1] = '\0';
    }
}

static bool host_auth_label_is_valid(const uint8_t label[HOST_AUTH_LABEL_LEN]) {
    size_t n = 0;
    while(n < HOST_AUTH_LABEL_LEN && label[n] != 0) {
        if(!host_auth_is_printable((char)label[n])) return false;
        n++;
    }
    if(n == 0 || n >= HOST_AUTH_LABEL_LEN) return false; // empty or unterminated
    return host_auth_all_zero(label + n, HOST_AUTH_LABEL_LEN - n);
}

bool host_auth_record_parse(const uint8_t in[HOST_AUTH_RECORD_LEN], HostAuthRecord* record) {
    memset(record, 0, sizeof(*record));
    if(host_auth_get_u32_le(in) != HOST_AUTH_RECORD_MAGIC) return false;
    if(in[4] != HOST_AUTH_RECORD_VERSION) return false;
    if(in[5] > HOST_AUTH_MAX_KEYS) return false;
    if(!host_auth_all_zero(in + 6, 2) || !host_auth_all_zero(in + 12, 4)) return false;
    uint32_t epoch = host_auth_get_u32_be(in + 8);
    if(epoch == 0) return false;

    HostAuthRecord parsed;
    memset(&parsed, 0, sizeof(parsed));
    for(uint8_t i = 0; i < HOST_AUTH_MAX_KEYS; i++) {
        const uint8_t* entry = in + HOST_AUTH_HEADER_LEN + i * HOST_AUTH_KEY_ENTRY_LEN;
        if(i >= in[5]) {
            if(!host_auth_all_zero(entry, HOST_AUTH_KEY_ENTRY_LEN)) return false;
            continue;
        }
        if(entry[HOST_AUTH_E_INUSE] != 1) return false;
        if(entry[HOST_AUTH_E_FLAGS] & ~HOST_AUTH_KEY_REQUIRE_UV) return false;
        if(!host_auth_label_is_valid(entry + HOST_AUTH_E_LABEL)) return false;
        if(!host_auth_all_zero(entry + HOST_AUTH_E_PAD, 3)) return false;
        if(!host_auth_pubkey_is_valid(entry)) return false;

        memcpy(parsed.keys[i].pubkey, entry, HOST_AUTH_PUBKEY_LEN);
        parsed.keys[i].flags = entry[HOST_AUTH_E_FLAGS];
        parsed.keys[i].sign_count = host_auth_get_u32_be(entry + HOST_AUTH_E_COUNT);
        memcpy(parsed.keys[i].label, entry + HOST_AUTH_E_LABEL, HOST_AUTH_LABEL_LEN);
    }
    parsed.count = in[5];
    parsed.epoch = epoch;
    *record = parsed;
    return true;
}

bool host_auth_epoch_matches(uint32_t record_epoch, uint32_t register_epoch) {
    return record_epoch != 0 && record_epoch == register_epoch;
}

HostAuthLoadAction host_auth_load_decide(HostAuthFileState main, HostAuthFileState tmp) {
    if(main == HostAuthFileOk) return HostAuthLoadUseMain;
    if(main == HostAuthFileUnreadable) return HostAuthLoadFail;
    return tmp == HostAuthFileOk ? HostAuthLoadRecoverTmp : HostAuthLoadFail;
}

static void host_auth_label_copy(char out[HOST_AUTH_LABEL_LEN], const char* label) {
    memset(out, 0, HOST_AUTH_LABEL_LEN);
    size_t n = 0;
    for(const char* c = label ? label : ""; *c && n < HOST_AUTH_LABEL_LEN - 1; c++) {
        // Printable ASCII only: the label is drawn on the Flipper's screen
        out[n++] = host_auth_is_printable(*c) ? *c : '?';
    }
    if(n == 0) memcpy(out, "host", 5);
}

HostAuthAddResult host_auth_record_add(
    HostAuthRecord* record,
    const uint8_t pubkey[HOST_AUTH_PUBKEY_LEN],
    bool require_uv,
    const char* label) {
    if(!host_auth_pubkey_is_valid(pubkey)) return HostAuthAddInvalidKey;
    for(uint8_t i = 0; i < record->count && i < HOST_AUTH_MAX_KEYS; i++) {
        if(memcmp(record->keys[i].pubkey, pubkey, HOST_AUTH_PUBKEY_LEN) == 0)
            return HostAuthAddDuplicate;
    }
    if(record->count >= HOST_AUTH_MAX_KEYS) return HostAuthAddFull;

    HostAuthKey* key = &record->keys[record->count];
    memset(key, 0, sizeof(*key));
    memcpy(key->pubkey, pubkey, HOST_AUTH_PUBKEY_LEN);
    key->flags = require_uv ? HOST_AUTH_KEY_REQUIRE_UV : 0;
    key->sign_count = 0;
    host_auth_label_copy(key->label, label);
    record->count++;
    return HostAuthAddOk;
}

bool host_auth_record_remove(HostAuthRecord* record, const uint8_t fp[HOST_AUTH_FINGERPRINT_LEN]) {
    for(uint8_t i = 0; i < record->count && i < HOST_AUTH_MAX_KEYS; i++) {
        uint8_t key_fp[HOST_AUTH_FINGERPRINT_LEN];
        host_auth_fingerprint(record->keys[i].pubkey, key_fp);
        if(host_auth_ct_equal(key_fp, fp, HOST_AUTH_FINGERPRINT_LEN)) {
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

// ===
// Hex
// ===

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

// ======
// Parser
// ======

typedef struct {
    const char* s;
    size_t len;
    size_t pos;
} HostAuthLexer;

// Next space-separated word. Exactly one space between words.
static bool host_auth_lex_word(HostAuthLexer* lx, const char** word, size_t* word_len) {
    if(lx->pos >= lx->len) return false;
    size_t start = lx->pos;
    while(lx->pos < lx->len && lx->s[lx->pos] != ' ')
        lx->pos++;
    *word = lx->s + start;
    *word_len = lx->pos - start;
    if(*word_len == 0) return false; // two spaces in a row
    if(lx->pos < lx->len) lx->pos++; // the separator
    return true;
}

static bool host_auth_lex_done(const HostAuthLexer* lx) {
    return lx->pos >= lx->len;
}

static bool host_auth_word_is(const char* word, size_t word_len, const char* literal) {
    size_t n = strlen(literal);
    return word_len == n && memcmp(word, literal, n) == 0;
}

static bool host_auth_parse_response(HostAuthLexer* lx, HostAuthCommand* cmd) {
    const char* word;
    size_t word_len;
    if(!host_auth_lex_word(lx, &word, &word_len)) return false;
    if(!host_auth_hex_decode(
           word, word_len, cmd->response.auth_data, sizeof(cmd->response.auth_data)))
        return false;

    if(!host_auth_lex_word(lx, &word, &word_len)) return false;
    if(word_len % 2 != 0 || word_len < 2 * HOST_AUTH_SIG_DER_MIN ||
       word_len > 2 * HOST_AUTH_SIG_DER_MAX)
        return false;
    uint8_t der[HOST_AUTH_SIG_DER_MAX];
    if(!host_auth_hex_decode(word, word_len, der, word_len / 2)) return false;
    if(!host_auth_der_parse_signature(der, word_len / 2, cmd->response.r, cmd->response.s))
        return false;
    return host_auth_lex_done(lx);
}

static bool host_auth_parse_enroll(HostAuthLexer* lx, HostAuthCommand* cmd) {
    const char* word;
    size_t word_len;
    if(!host_auth_lex_word(lx, &word, &word_len)) return false;
    if(!host_auth_hex_decode(word, word_len, cmd->enroll.pubkey, sizeof(cmd->enroll.pubkey)))
        return false;
    if(cmd->enroll.pubkey[0] != 0x04) return false; // curve check happens when adding

    if(!host_auth_lex_word(lx, &word, &word_len)) return false;
    if(host_auth_word_is(word, word_len, "1")) {
        cmd->enroll.require_uv = true;
    } else if(!host_auth_word_is(word, word_len, "0")) {
        return false;
    }

    // The rest of the line is the label (spaces allowed, but not leading)
    size_t label_len = lx->len - lx->pos;
    if(label_len == 0 || label_len >= HOST_AUTH_LABEL_LEN) return false;
    if(lx->s[lx->pos] == ' ') return false;
    memcpy(cmd->enroll.label, lx->s + lx->pos, label_len);
    cmd->enroll.label[label_len] = '\0';
    lx->pos = lx->len;
    return true;
}

static bool host_auth_parse_revoke(HostAuthLexer* lx, HostAuthCommand* cmd) {
    const char* word;
    size_t word_len;
    if(!host_auth_lex_word(lx, &word, &word_len)) return false;
    if(host_auth_word_is(word, word_len, "all")) {
        cmd->revoke.all = true;
    } else if(!host_auth_hex_decode(word, word_len, cmd->revoke.fp, sizeof(cmd->revoke.fp))) {
        return false;
    }
    return host_auth_lex_done(lx);
}

bool host_auth_parse_command(const char* args, size_t len, HostAuthCommand* cmd) {
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = HostAuthCmdInvalid;
    if(!args || len == 0 || len > HOST_AUTH_ARGS_MAX) return false;
    for(size_t i = 0; i < len; i++) {
        if(!host_auth_is_printable(args[i])) return false; // also rejects NUL
    }
    if(args[0] == ' ' || args[len - 1] == ' ') return false;

    HostAuthLexer lx = {.s = args, .len = len, .pos = 0};
    const char* word;
    size_t word_len;
    if(!host_auth_lex_word(&lx, &word, &word_len)) return false;

    HostAuthCmdType type = HostAuthCmdInvalid;
    bool ok = false;
    if(host_auth_word_is(word, word_len, "info")) {
        type = HostAuthCmdInfo;
        ok = host_auth_lex_done(&lx);
    } else if(host_auth_word_is(word, word_len, "challenge")) {
        type = HostAuthCmdChallenge;
        ok = host_auth_lex_done(&lx);
    } else if(host_auth_word_is(word, word_len, "response")) {
        type = HostAuthCmdResponse;
        ok = host_auth_parse_response(&lx, cmd);
    } else if(host_auth_word_is(word, word_len, "enroll")) {
        type = HostAuthCmdEnroll;
        ok = host_auth_parse_enroll(&lx, cmd);
    } else if(host_auth_word_is(word, word_len, "revoke")) {
        type = HostAuthCmdRevoke;
        ok = host_auth_parse_revoke(&lx, cmd);
    }

    if(!ok) {
        memset(cmd, 0, sizeof(*cmd));
        cmd->type = HostAuthCmdInvalid;
        return false;
    }
    cmd->type = type;
    return true;
}

// =======
// Backoff
// =======

uint32_t host_auth_backoff_s(uint32_t fails) {
    if(fails < HOST_AUTH_FREE_FAILS) return 0;
    uint32_t wait = HOST_AUTH_BACKOFF_BASE_S;
    for(uint32_t i = HOST_AUTH_FREE_FAILS; i < fails; i++) {
        wait *= 2;
        if(wait >= HOST_AUTH_BACKOFF_MAX_S) return HOST_AUTH_BACKOFF_MAX_S;
    }
    return wait;
}

uint32_t host_auth_backoff_remaining_s(
    uint32_t fails,
    uint32_t last_fail_ts,
    uint32_t now,
    uint32_t* clamped_last_fail_ts) {
    *clamped_last_fail_ts = last_fail_ts;
    uint32_t wait = host_auth_backoff_s(fails);
    if(wait == 0) return 0;
    if(now < last_fail_ts) {
        // The clock went backwards (set by hand, or reset). Restart the wait from
        // now rather than trusting a timestamp in the future.
        *clamped_last_fail_ts = now;
        last_fail_ts = now;
    }
    uint32_t elapsed = now - last_fail_ts;
    return elapsed >= wait ? 0 : wait - elapsed;
}
