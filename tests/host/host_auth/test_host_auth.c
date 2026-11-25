// Host test for applications/services/desktop/helpers/host_auth_core.c (protocol v2)
// Built and run by run.sh with the firmware's own mbedtls and mbedtls_cfg.h,
// under AddressSanitizer and UndefinedBehaviorSanitizer.

#include "host_auth_core.h"
#include "vectors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                                         \
    do {                                                                    \
        checks++;                                                           \
        if(!(cond)) {                                                       \
            failures++;                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                   \
    } while(0)

static const uint8_t p256_n[32] = {0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
                                   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                   0xbc, 0xe6, 0xfa, 0xad, 0xa7, 0x17, 0x9e, 0x84,
                                   0xf3, 0xb9, 0xca, 0xc2, 0xfc, 0x63, 0x25, 0x51};

// Deterministic PRNG for the fuzz loops (reproducible failures)
static uint64_t rng_state = 0x9E3779B97F4A7C15ull;
static uint32_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 11);
}

static void vec_binding(const Vec* v, uint8_t out[HOST_AUTH_BINDING_LEN]) {
    host_auth_binding_build(out, HostAuthActionUnlock, v->uid, v->epoch, v->counter, v->nonce);
}

static void vec_record(const Vec* v, HostAuthRecord* rec) {
    memset(rec, 0, sizeof(*rec));
    rec->epoch = 1;
    // Another key first, so the signer index is not trivially 0
    const Vec* other = (v == &vectors[0]) ? &vectors[1] : &vectors[0];
    CHECK(host_auth_record_add(rec, other->pub, false, "other") == HostAuthAddOk);
    CHECK(host_auth_record_add(rec, v->pub, v->require_uv, "this") == HostAuthAddOk);
    rec->keys[1].sign_count = v->stored_count;
}

// ======
// Layout
// ======

static void test_binding_layout(void) {
    uint8_t uid[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t nonce[32];
    for(int i = 0; i < 32; i++)
        nonce[i] = (uint8_t)(0xA0 + i);
    uint8_t b[HOST_AUTH_BINDING_LEN];
    CHECK(HOST_AUTH_BINDING_LEN == 70);
    CHECK(
        host_auth_binding_build(b, HostAuthActionUnlock, uid, 0x11223344u, 0xA0B0C0D0u, nonce) ==
        70);
    CHECK(memcmp(b, "flipware-hostauth-v2", 20) == 0);
    CHECK(b[20] == 1);
    CHECK(b[21] == 8);
    CHECK(memcmp(b + 22, uid, 8) == 0);
    CHECK(b[30] == 0x11 && b[31] == 0x22 && b[32] == 0x33 && b[33] == 0x44);
    CHECK(b[34] == 0xA0 && b[35] == 0xB0 && b[36] == 0xC0 && b[37] == 0xD0);
    CHECK(memcmp(b + 38, nonce, 32) == 0);
    CHECK(HOST_AUTH_CHALLENGE_B64_LEN == 94);
}

static void test_base64url(void) {
    // RFC 4648 section 10, unpadded, plus the two url-safe characters
    static const struct {
        const char* in;
        const char* out;
    } kat[] = {
        {"", ""},
        {"f", "Zg"},
        {"fo", "Zm8"},
        {"foo", "Zm9v"},
        {"foob", "Zm9vYg"},
        {"fooba", "Zm9vYmE"},
        {"foobar", "Zm9vYmFy"},
        {"\xfb\xff", "-_8"},
        {"\xff\xff\xfe", "___-"},
    };
    for(size_t i = 0; i < sizeof(kat) / sizeof(kat[0]); i++) {
        char out[16];
        size_t n = host_auth_base64url_encode((const uint8_t*)kat[i].in, strlen(kat[i].in), out);
        CHECK(n == strlen(kat[i].out));
        CHECK(strcmp(out, kat[i].out) == 0);
    }
}

static void test_client_data(void) {
    printf(
        "client data cross-checked against python-fido2: %s\n", VEC_FIDO2_CHECKED ? "yes" : "no");
    for(size_t i = 0; i < VEC_COUNT; i++) {
        const Vec* v = &vectors[i];
        uint8_t b[HOST_AUTH_BINDING_LEN];
        vec_binding(v, b);
        char challenge[HOST_AUTH_CHALLENGE_B64_LEN + 1];
        CHECK(host_auth_base64url_encode(b, sizeof(b), challenge) == HOST_AUTH_CHALLENGE_B64_LEN);
        CHECK(strcmp(challenge, v->challenge) == 0);

        char cd[HOST_AUTH_CLIENT_DATA_MAX];
        size_t n = host_auth_client_data_build(b, cd);
        CHECK(n == strlen(v->client_data));
        CHECK(strcmp(cd, v->client_data) == 0);
        CHECK(n < HOST_AUTH_CLIENT_DATA_MAX);
    }
}

// ==========
// Assertions
// ==========

static void test_vectors(void) {
    for(size_t i = 0; i < VEC_COUNT; i++) {
        const Vec* v = &vectors[i];
        uint8_t r[32], s[32];
        CHECK(host_auth_der_parse_signature(v->der, v->der_len, r, s));
        uint8_t re[HOST_AUTH_SIG_DER_MAX];
        CHECK(host_auth_der_encode_signature(r, s, re) == v->der_len);
        CHECK(memcmp(re, v->der, v->der_len) == 0);

        uint8_t fp[8];
        host_auth_fingerprint(v->pub, fp);
        CHECK(memcmp(fp, v->fp, 8) == 0);

        HostAuthRecord rec;
        vec_record(v, &rec);
        uint8_t b[HOST_AUTH_BINDING_LEN];
        vec_binding(v, b);
        int signer = -2;
        uint32_t count = 0xDEAD;
        HostAuthVerifyResult res =
            host_auth_verify_assertion(&rec, b, v->auth_data, r, s, &signer, &count);
        if((int)res != v->expected) fprintf(stderr, "vector %s: got %d\n", v->name, res);
        CHECK((int)res == v->expected);
        if(v->expected == HostAuthVerifyOk) {
            CHECK(signer == 1);
            CHECK(count == v->sign_count);
        } else if(v->expected == HostAuthVerifyRpMismatch) {
            CHECK(signer == -1);
        } else {
            // Genuine signature, unacceptable assertion
            CHECK(signer == 1);
        }
        if(v->expected != HostAuthVerifyOk) continue;

        // Any single-bit change in what was signed or in the signature fails
        for(size_t bit = 0; bit < HOST_AUTH_BINDING_LEN * 8; bit += 3) {
            uint8_t bad[HOST_AUTH_BINDING_LEN];
            memcpy(bad, b, sizeof(bad));
            bad[bit / 8] ^= (uint8_t)(1u << (bit % 8));
            CHECK(
                host_auth_verify_assertion(&rec, bad, v->auth_data, r, s, &signer, &count) ==
                HostAuthVerifyBadSignature);
        }
        for(size_t bit = 32 * 8; bit < HOST_AUTH_AUTH_DATA_LEN * 8; bit++) {
            // flags and signCount (rpIdHash bits fail earlier, as rp-mismatch)
            uint8_t bad[HOST_AUTH_AUTH_DATA_LEN];
            memcpy(bad, v->auth_data, sizeof(bad));
            bad[bit / 8] ^= (uint8_t)(1u << (bit % 8));
            CHECK(
                host_auth_verify_assertion(&rec, b, bad, r, s, &signer, &count) ==
                HostAuthVerifyBadSignature);
        }
        for(size_t bit = 0; bit < 32 * 8; bit += 5) {
            uint8_t bad[HOST_AUTH_AUTH_DATA_LEN];
            memcpy(bad, v->auth_data, sizeof(bad));
            bad[bit / 8] ^= (uint8_t)(1u << (bit % 8));
            CHECK(
                host_auth_verify_assertion(&rec, b, bad, r, s, &signer, &count) ==
                HostAuthVerifyRpMismatch);
        }
        for(size_t bit = 0; bit < 64 * 8; bit += 5) {
            uint8_t br[32], bs[32];
            memcpy(br, r, 32);
            memcpy(bs, s, 32);
            if(bit < 256)
                br[bit / 8] ^= (uint8_t)(1u << (bit % 8));
            else
                bs[(bit - 256) / 8] ^= (uint8_t)(1u << (bit % 8));
            CHECK(
                host_auth_verify_assertion(&rec, b, v->auth_data, br, bs, &signer, &count) ==
                HostAuthVerifyBadSignature);
        }

        // Wrong action, uid, epoch or counter: a signature is good for one purpose only
        uint8_t other[HOST_AUTH_BINDING_LEN];
        host_auth_binding_build(other, (HostAuthAction)2, v->uid, v->epoch, v->counter, v->nonce);
        CHECK(
            host_auth_verify_assertion(&rec, other, v->auth_data, r, s, &signer, &count) ==
            HostAuthVerifyBadSignature);
        host_auth_binding_build(
            other, HostAuthActionUnlock, v->uid, v->epoch, v->counter + 1, v->nonce);
        CHECK(
            host_auth_verify_assertion(&rec, other, v->auth_data, r, s, &signer, &count) ==
            HostAuthVerifyBadSignature);
        host_auth_binding_build(
            other, HostAuthActionUnlock, v->uid, v->epoch ^ 1, v->counter, v->nonce);
        CHECK(
            host_auth_verify_assertion(&rec, other, v->auth_data, r, s, &signer, &count) ==
            HostAuthVerifyBadSignature);

        // A record without this key
        HostAuthRecord without;
        memset(&without, 0, sizeof(without));
        const Vec* w = &vectors[(i + 1) % VEC_COUNT];
        CHECK(host_auth_record_add(&without, w->pub, false, "w") == HostAuthAddOk);
        CHECK(
            host_auth_verify_assertion(&without, b, v->auth_data, r, s, &signer, &count) ==
            HostAuthVerifyBadSignature);
        CHECK(signer == -1);

        // UV policy is per key
        if(!(v->auth_data[32] & HOST_AUTH_FLAG_UV)) {
            rec.keys[1].flags |= HOST_AUTH_KEY_REQUIRE_UV;
            CHECK(
                host_auth_verify_assertion(&rec, b, v->auth_data, r, s, &signer, &count) ==
                HostAuthVerifyNoVerification);
        }
    }
}

static void test_sign_count(void) {
    CHECK(host_auth_sign_count_ok(0, 0));
    CHECK(host_auth_sign_count_ok(0, 1));
    CHECK(host_auth_sign_count_ok(5, 6));
    CHECK(host_auth_sign_count_ok(5, 0xFFFFFFFFu));
    CHECK(!host_auth_sign_count_ok(5, 5));
    CHECK(!host_auth_sign_count_ok(5, 4));
    CHECK(!host_auth_sign_count_ok(5, 0));
    CHECK(!host_auth_sign_count_ok(0xFFFFFFFFu, 0xFFFFFFFFu));
}

// ===
// DER
// ===

static void test_der_strict(void) {
    const Vec* v = &vectors[0];
    uint8_t r[32], s[32];

    // Minimal valid: r = s = 1
    static const uint8_t one[] = {0x30, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01};
    CHECK(host_auth_der_parse_signature(one, sizeof(one), r, s));
    CHECK(r[31] == 1 && s[31] == 1 && r[0] == 0);

    static const struct {
        const char* why;
        uint8_t der[12];
        size_t len;
    } bad[] = {
        {"r = 0", {0x30, 0x06, 0x02, 0x01, 0x00, 0x02, 0x01, 0x01}, 8},
        {"s = 0", {0x30, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x00}, 8},
        {"negative r", {0x30, 0x06, 0x02, 0x01, 0x81, 0x02, 0x01, 0x01}, 8},
        {"non-minimal r", {0x30, 0x07, 0x02, 0x02, 0x00, 0x01, 0x02, 0x01, 0x01}, 9},
        {"empty r", {0x30, 0x05, 0x02, 0x00, 0x02, 0x01, 0x01}, 7},
        {"SET not SEQUENCE", {0x31, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01}, 8},
        {"BIT STRING not INTEGER", {0x30, 0x06, 0x03, 0x01, 0x01, 0x02, 0x01, 0x01}, 8},
        {"seq length too long", {0x30, 0x07, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01}, 8},
        {"seq length too short", {0x30, 0x05, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01}, 8},
        {"trailing byte", {0x30, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01, 0x00}, 9},
        {"trailing inside seq", {0x30, 0x07, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01, 0x00}, 9},
        {"long-form seq length", {0x30, 0x81, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01}, 9},
        {"long-form int length", {0x30, 0x07, 0x02, 0x81, 0x01, 0x01, 0x02, 0x01, 0x01}, 9},
        {"int overruns", {0x30, 0x06, 0x02, 0x05, 0x01, 0x02, 0x01, 0x01}, 8},
        {"one int only", {0x30, 0x03, 0x02, 0x01, 0x01}, 5},
        {"three ints", {0x30, 0x09, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01}, 11},
    };
    for(size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        bool ok = host_auth_der_parse_signature(bad[i].der, bad[i].len, r, s);
        if(ok) fprintf(stderr, "DER accepted: %s\n", bad[i].why);
        CHECK(!ok);
    }
    CHECK(!host_auth_der_parse_signature(NULL, 8, r, s));
    CHECK(!host_auth_der_parse_signature(one, 0, r, s));

    // Range: r, s in [1, n-1]
    uint8_t big[32];
    uint8_t der[HOST_AUTH_SIG_DER_MAX];
    uint8_t vr[32], vs[32];
    CHECK(host_auth_der_parse_signature(v->der, v->der_len, vr, vs));
    size_t n = host_auth_der_encode_signature(p256_n, vs, der); // r = n
    CHECK(!host_auth_der_parse_signature(der, n, r, s));
    n = host_auth_der_encode_signature(vr, p256_n, der); // s = n
    CHECK(!host_auth_der_parse_signature(der, n, r, s));
    memset(big, 0xFF, sizeof(big)); // > n
    n = host_auth_der_encode_signature(big, vs, der);
    CHECK(n >= HOST_AUTH_SIG_DER_MAX - 1); // r needs a 0x00 pad byte
    CHECK(!host_auth_der_parse_signature(der, n, r, s));
    memcpy(big, p256_n, 32);
    big[31] -= 1; // n - 1 is the largest valid value
    n = host_auth_der_encode_signature(big, big, der);
    CHECK(n == HOST_AUTH_SIG_DER_MAX);
    CHECK(host_auth_der_parse_signature(der, n, r, s));
    CHECK(memcmp(r, big, 32) == 0);

    // 33 value bytes with a nonzero first byte is out of range, not a long scalar
    static const uint8_t wide_hdr[] = {0x30, 0x46, 0x02, 0x21, 0x01};
    uint8_t wide[72];
    memset(wide, 0x11, sizeof(wide));
    memcpy(wide, wide_hdr, sizeof(wide_hdr));
    wide[37] = 0x02;
    wide[38] = 0x21;
    wide[39] = 0x00;
    wide[40] = 0x80;
    CHECK(!host_auth_der_parse_signature(wide, sizeof(wide), r, s));

    // verify_digest also rejects out-of-range scalars on its own
    uint8_t digest[32] = {0};
    uint8_t zero[32] = {0};
    CHECK(!host_auth_verify_digest(v->pub, digest, zero, vs));
    CHECK(!host_auth_verify_digest(v->pub, digest, vr, zero));
    CHECK(!host_auth_verify_digest(v->pub, digest, p256_n, vs));
    CHECK(!host_auth_verify_digest(v->pub, digest, vr, p256_n));
}

static void test_der_fuzz(void) {
    // Property: whatever the parser accepts is exactly the canonical encoding
    // of the values it returns, and they are in range.
    uint8_t buf[HOST_AUTH_SIG_DER_MAX + 8];
    uint8_t r[32], s[32], re[HOST_AUTH_SIG_DER_MAX];
    size_t accepted = 0;
    for(int iter = 0; iter < 300000; iter++) {
        size_t len;
        const Vec* v = &vectors[rng() % VEC_COUNT];
        if(iter % 3 == 0) {
            // random bytes with a plausible header
            len = 1 + rng() % sizeof(buf);
            for(size_t i = 0; i < len; i++)
                buf[i] = (uint8_t)rng();
            buf[0] = 0x30;
            if(len > 1) buf[1] = (uint8_t)(len - 2);
            if(len > 2) buf[2] = 0x02;
        } else {
            // mutate a real signature: flip, insert, delete or change lengths
            len = v->der_len;
            memcpy(buf, v->der, len);
            int mutations = 1 + rng() % 3;
            for(int m = 0; m < mutations; m++) {
                uint32_t op = rng() % 4;
                size_t pos = rng() % len;
                if(op == 0) {
                    buf[pos] ^= (uint8_t)(1u << (rng() % 8));
                } else if(op == 1 && len < sizeof(buf)) {
                    memmove(buf + pos + 1, buf + pos, len - pos);
                    buf[pos] = (uint8_t)rng();
                    len++;
                } else if(op == 2 && len > 1) {
                    memmove(buf + pos, buf + pos + 1, len - pos - 1);
                    len--;
                } else {
                    buf[pos] = (uint8_t)rng();
                }
            }
        }
        // Copy to an exact-size heap buffer so ASan catches any overread
        uint8_t* exact = malloc(len);
        memcpy(exact, buf, len);
        if(host_auth_der_parse_signature(exact, len, r, s)) {
            accepted++;
            size_t n = host_auth_der_encode_signature(r, s, re);
            CHECK(n == len);
            CHECK(memcmp(re, exact, len) == 0);
            CHECK(memcmp(r, p256_n, 32) < 0 && memcmp(s, p256_n, 32) < 0);
        }
        free(exact);
    }
    // Round trip from random scalars
    for(int iter = 0; iter < 20000; iter++) {
        uint8_t rr[32], ss[32];
        for(int i = 0; i < 32; i++) {
            rr[i] = (uint8_t)rng();
            ss[i] = (uint8_t)rng();
        }
        // Short values too
        size_t zr = rng() % 32, zs = rng() % 32;
        memset(rr, 0, zr);
        memset(ss, 0, zs);
        rr[0] &= 0x7F;
        ss[0] &= 0x7F;
        rr[31] |= 1;
        ss[31] |= 1;
        size_t n = host_auth_der_encode_signature(rr, ss, re);
        CHECK(n >= HOST_AUTH_SIG_DER_MIN && n <= HOST_AUTH_SIG_DER_MAX);
        CHECK(host_auth_der_parse_signature(re, n, r, s));
        CHECK(memcmp(r, rr, 32) == 0 && memcmp(s, ss, 32) == 0);
    }
    printf("DER fuzz: %zu of 300000 mutated inputs accepted, all canonical\n", accepted);
}

// ===========
// Public keys
// ===========

static void test_invalid_pubkeys(void) {
    uint8_t key[65];
    memcpy(key, vectors[0].pub, 65);
    CHECK(host_auth_pubkey_is_valid(key));

    key[64] ^= 1; // off the curve
    CHECK(!host_auth_pubkey_is_valid(key));

    memcpy(key, vectors[0].pub, 65);
    key[0] = 0x02; // compressed prefix with a full-length buffer
    CHECK(!host_auth_pubkey_is_valid(key));
    key[0] = 0x06; // hybrid
    CHECK(!host_auth_pubkey_is_valid(key));

    memset(key, 0, sizeof(key)); // point at infinity encoding
    CHECK(!host_auth_pubkey_is_valid(key));
    key[0] = 0x04; // (0, 0)
    CHECK(!host_auth_pubkey_is_valid(key));

    memset(key, 0xFF, sizeof(key));
    key[0] = 0x04; // coordinates >= p
    CHECK(!host_auth_pubkey_is_valid(key));

    // An invalid key never verifies anything, and can't be enrolled
    const Vec* v = &vectors[0];
    uint8_t r[32], s[32];
    CHECK(host_auth_der_parse_signature(v->der, v->der_len, r, s));
    uint8_t digest[32] = {0};
    CHECK(!host_auth_verify_digest(key, digest, r, s));
    HostAuthRecord rec;
    memset(&rec, 0, sizeof(rec));
    CHECK(host_auth_record_add(&rec, key, false, "bad") == HostAuthAddInvalidKey);
}

// ======
// Record
// ======

static void test_record(void) {
    HostAuthRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.epoch = 0x01020304u;

    CHECK(host_auth_record_add(&rec, vectors[0].pub, true, "Mac") == HostAuthAddOk);
    CHECK(host_auth_record_add(&rec, vectors[0].pub, false, "again") == HostAuthAddDuplicate);
    uint8_t bad[65];
    memcpy(bad, vectors[1].pub, 65);
    bad[10] ^= 0x40;
    CHECK(host_auth_record_add(&rec, bad, false, "bad") == HostAuthAddInvalidKey);
    CHECK(
        host_auth_record_add(
            &rec,
            vectors[1].pub,
            false,
            "a\tb\x01"
            "c\xc3\xa9") == HostAuthAddOk);
    CHECK(strcmp(rec.keys[1].label, "a?b?c??") == 0);
    CHECK(host_auth_record_add(&rec, vectors[2].pub, false, "") == HostAuthAddOk);
    CHECK(strcmp(rec.keys[2].label, "host") == 0);
    CHECK(
        host_auth_record_add(
            &rec, vectors[3].pub, false, "a label that is much longer than thirty characters") ==
        HostAuthAddOk);
    CHECK(strlen(rec.keys[3].label) == HOST_AUTH_LABEL_LEN - 1);
    CHECK(host_auth_record_add(&rec, vectors[4].pub, false, "fifth") == HostAuthAddFull);
    CHECK(rec.count == 4);
    CHECK(rec.keys[0].flags == HOST_AUTH_KEY_REQUIRE_UV);
    CHECK(rec.keys[1].flags == 0);
    rec.keys[2].sign_count = 0xA1B2C3D4u;

    // Round trip
    uint8_t buf[HOST_AUTH_RECORD_LEN];
    host_auth_record_serialize(&rec, buf);
    HostAuthRecord parsed;
    CHECK(host_auth_record_parse(buf, &parsed));
    CHECK(parsed.count == 4);
    CHECK(parsed.epoch == 0x01020304u);
    CHECK(memcmp(&parsed, &rec, sizeof(rec)) == 0);
    CHECK(HOST_AUTH_RECORD_LEN == 432 && HOST_AUTH_FILE_LEN == 448);

    // Strict parsing
    const size_t e0 = HOST_AUTH_HEADER_LEN;
    const size_t inuse = HOST_AUTH_PUBKEY_LEN, flags = inuse + 1, label = flags + 1 + 4;
    struct {
        const char* why;
        size_t off;
        uint8_t val;
    } tamper[] = {
        {"magic", 0, 0x00},
        {"version 1", 4, 1},
        {"version 3", 4, 3},
        {"count 5", 5, 5},
        {"reserved 6", 6, 1},
        {"reserved 7", 7, 1},
        {"reserved 12", 12, 1},
        {"reserved 15", 15, 0x80},
        {"in-use 0", e0 + inuse, 0},
        {"in-use 2", e0 + inuse, 2},
        {"unknown key flag", e0 + flags, 0x02},
        {"label control char", e0 + label, 0x07},
        {"label high bit", e0 + label, 0xC3},
        {"label empty", e0 + label, 0x00},
        {"label unterminated", e0 + label + HOST_AUTH_LABEL_LEN - 1, 'x'},
        {"junk after label NUL", e0 + label + 10, 'x'},
        {"padding", e0 + HOST_AUTH_KEY_ENTRY_LEN - 1, 1},
        {"pubkey prefix", e0, 0x02},
        {"pubkey off curve", e0 + 64, 0x00},
    };
    for(size_t i = 0; i < sizeof(tamper) / sizeof(tamper[0]); i++) {
        uint8_t t[HOST_AUTH_RECORD_LEN];
        memcpy(t, buf, sizeof(t));
        if(t[tamper[i].off] == tamper[i].val)
            t[tamper[i].off] ^= 0x01;
        else
            t[tamper[i].off] = tamper[i].val;
        bool ok = host_auth_record_parse(t, &parsed);
        if(ok) fprintf(stderr, "record accepted: %s\n", tamper[i].why);
        CHECK(!ok);
        CHECK(parsed.count == 0); // nothing half-parsed is left behind
    }
    {
        uint8_t t[HOST_AUTH_RECORD_LEN];
        memcpy(t, buf, sizeof(t));
        memset(t + 8, 0, 4); // epoch 0
        CHECK(!host_auth_record_parse(t, &parsed));
    }

    // Remove by fingerprint, then unused slots must be zero
    uint8_t fp[8];
    host_auth_fingerprint(vectors[1].pub, fp);
    CHECK(host_auth_record_remove(&rec, fp));
    CHECK(!host_auth_record_remove(&rec, fp));
    CHECK(rec.count == 3);
    CHECK(memcmp(rec.keys[1].pubkey, vectors[2].pub, 65) == 0);
    CHECK(rec.keys[1].sign_count == 0xA1B2C3D4u);
    host_auth_record_serialize(&rec, buf);
    CHECK(host_auth_record_parse(buf, &parsed));
    CHECK(parsed.count == 3);
    uint8_t t[HOST_AUTH_RECORD_LEN];
    memcpy(t, buf, sizeof(t));
    t[e0 + 3 * HOST_AUTH_KEY_ENTRY_LEN + 4] = 1; // junk in an unused slot
    CHECK(!host_auth_record_parse(t, &parsed));

    // Random records never crash the parser, and an accepted one re-serializes identically
    for(int iter = 0; iter < 2000; iter++) {
        memcpy(t, buf, sizeof(t));
        for(int m = 0; m < 1 + (int)(rng() % 4); m++)
            t[rng() % sizeof(t)] = (uint8_t)rng();
        if(host_auth_record_parse(t, &parsed)) {
            uint8_t again[HOST_AUTH_RECORD_LEN];
            host_auth_record_serialize(&parsed, again);
            CHECK(memcmp(again, t, sizeof(t)) == 0);
        }
    }
}

static void test_epoch(void) {
    CHECK(!host_auth_epoch_matches(0, 0)); // never enrolled, or registers were reset
    CHECK(!host_auth_epoch_matches(0, 5));
    CHECK(!host_auth_epoch_matches(5, 0));
    CHECK(!host_auth_epoch_matches(5, 6)); // an older copy of the file
    CHECK(host_auth_epoch_matches(5, 5));
}

static void test_load_decide(void) {
    const HostAuthFileState all[] = {
        HostAuthFileOk,
        HostAuthFileMissing,
        HostAuthFileUnreadable,
        HostAuthFileInvalid,
        HostAuthFileStale,
    };
    for(size_t m = 0; m < 5; m++) {
        for(size_t t = 0; t < 5; t++) {
            HostAuthLoadAction a = host_auth_load_decide(all[m], all[t]);
            HostAuthLoadAction want;
            if(all[m] == HostAuthFileOk) {
                want = HostAuthLoadUseMain; // a current main file always wins
            } else if(all[m] == HostAuthFileUnreadable) {
                want = HostAuthLoadFail; // transient error: keep the main file
            } else if(all[t] == HostAuthFileOk) {
                want = HostAuthLoadRecoverTmp; // interrupted save
            } else {
                want = HostAuthLoadFail; // a stale or invalid tmp never helps
            }
            CHECK(a == want);
        }
    }
}

// ======
// Parser
// ======

static bool parse(const char* s, HostAuthCommand* cmd) {
    // Exact-size heap copy without a NUL: the parser must honour len
    size_t len = strlen(s);
    char* exact = malloc(len ? len : 1);
    memcpy(exact, s, len);
    bool ok = host_auth_parse_command(exact, len, cmd);
    free(exact);
    return ok;
}

static void test_parser(void) {
    HostAuthCommand cmd;
    const Vec* v = &vectors[0];
    char auth_hex[2 * HOST_AUTH_AUTH_DATA_LEN + 1];
    char der_hex[2 * HOST_AUTH_SIG_DER_MAX + 1];
    char pub_hex[2 * HOST_AUTH_PUBKEY_LEN + 1];
    host_auth_hex_encode(v->auth_data, HOST_AUTH_AUTH_DATA_LEN, auth_hex);
    host_auth_hex_encode(v->der, v->der_len, der_hex);
    host_auth_hex_encode(v->pub, HOST_AUTH_PUBKEY_LEN, pub_hex);
    char line[400];

    CHECK(parse("info", &cmd) && cmd.type == HostAuthCmdInfo);
    CHECK(parse("challenge", &cmd) && cmd.type == HostAuthCmdChallenge);

    snprintf(line, sizeof(line), "response %s %s", auth_hex, der_hex);
    CHECK(parse(line, &cmd) && cmd.type == HostAuthCmdResponse);
    CHECK(memcmp(cmd.response.auth_data, v->auth_data, 37) == 0);

    snprintf(line, sizeof(line), "enroll %s 1 Calvin's MacBook Pro", pub_hex);
    CHECK(parse(line, &cmd) && cmd.type == HostAuthCmdEnroll);
    CHECK(cmd.enroll.require_uv);
    CHECK(strcmp(cmd.enroll.label, "Calvin's MacBook Pro") == 0);
    snprintf(line, sizeof(line), "enroll %s 0 x", pub_hex);
    CHECK(parse(line, &cmd) && !cmd.enroll.require_uv);
    snprintf(line, sizeof(line), "enroll %s 0 %s", pub_hex, "12345678901234567890123456789");
    CHECK(parse(line, &cmd)); // 29 chars: the longest label

    CHECK(parse("revoke all", &cmd) && cmd.type == HostAuthCmdRevoke && cmd.revoke.all);
    CHECK(parse("revoke 0011aabbccddeeff", &cmd) && !cmd.revoke.all);
    CHECK(cmd.revoke.fp[0] == 0x00 && cmd.revoke.fp[7] == 0xff);

    const char* invalid[] = {
        "",
        " ",
        "info ",
        " info",
        "info x",
        "INFO",
        "inf",
        "infoo",
        "challenge now",
        "challenge\t",
        "response",
        "response ",
        "revoke",
        "revoke ALL",
        "revoke all x",
        "revoke 0011aabbccddeef",
        "revoke 0011aabbccddeeff0",
        "revoke 0011aabbccddeefg",
        "enroll",
        "unknown",
    };
    for(size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        bool ok = parse(invalid[i], &cmd);
        if(ok) fprintf(stderr, "parser accepted: '%s'\n", invalid[i]);
        CHECK(!ok);
        CHECK(cmd.type == HostAuthCmdInvalid);
    }

    // Response shape
    snprintf(line, sizeof(line), "response %s  %s", auth_hex, der_hex);
    CHECK(!parse(line, &cmd)); // double space
    snprintf(line, sizeof(line), "response %s %s ", auth_hex, der_hex);
    CHECK(!parse(line, &cmd)); // trailing space
    snprintf(line, sizeof(line), "response %s %s 00", auth_hex, der_hex);
    CHECK(!parse(line, &cmd)); // extra token
    snprintf(line, sizeof(line), "response %.72s %s", auth_hex, der_hex);
    CHECK(!parse(line, &cmd)); // short auth data
    snprintf(line, sizeof(line), "response %s00 %s", auth_hex, der_hex);
    CHECK(!parse(line, &cmd)); // long auth data (extensions)
    snprintf(line, sizeof(line), "response %s %s0", auth_hex, der_hex);
    CHECK(!parse(line, &cmd)); // odd-length DER hex
    snprintf(line, sizeof(line), "response %s %s00", auth_hex, der_hex);
    CHECK(!parse(line, &cmd)); // trailing DER byte
    snprintf(line, sizeof(line), "response %s", auth_hex);
    CHECK(!parse(line, &cmd));
    {
        // raw r||s (protocol v1) is not DER
        char raw[129];
        memset(raw, 'a', 128);
        raw[128] = 0;
        snprintf(line, sizeof(line), "response %s %s", auth_hex, raw);
        CHECK(!parse(line, &cmd));
    }

    // Enroll shape
    snprintf(line, sizeof(line), "enroll %s 2 label", pub_hex);
    CHECK(!parse(line, &cmd)); // uv must be 0 or 1
    snprintf(line, sizeof(line), "enroll %s 1", pub_hex);
    CHECK(!parse(line, &cmd)); // no label
    snprintf(line, sizeof(line), "enroll %s 1 ", pub_hex);
    CHECK(!parse(line, &cmd)); // trailing space, empty label
    snprintf(line, sizeof(line), "enroll %s 1  label", pub_hex);
    CHECK(!parse(line, &cmd)); // leading space in label
    snprintf(line, sizeof(line), "enroll %s 0 %s", pub_hex, "123456789012345678901234567890");
    CHECK(!parse(line, &cmd)); // 30 chars: too long
    snprintf(line, sizeof(line), "enroll 02%s 1 label", pub_hex + 2);
    CHECK(!parse(line, &cmd)); // not an uncompressed point
    snprintf(line, sizeof(line), "enroll %.128s 1 label", pub_hex);
    CHECK(!parse(line, &cmd)); // short key

    // Non-printable bytes anywhere, including NUL inside the length
    {
        char nul[] = "revoke all";
        nul[6] = '\0';
        CHECK(!host_auth_parse_command(nul, sizeof(nul) - 1, &cmd));
        char tab[] = "revoke\tall";
        CHECK(!host_auth_parse_command(tab, sizeof(tab) - 1, &cmd));
        char hi[] = "revoke all\xff";
        CHECK(!host_auth_parse_command(hi, sizeof(hi) - 1, &cmd));
        char ctl[] = "info\r";
        CHECK(!host_auth_parse_command(ctl, sizeof(ctl) - 1, &cmd));
    }

    // Length limit
    {
        char* longline = malloc(HOST_AUTH_ARGS_MAX + 2);
        memset(longline, 'a', HOST_AUTH_ARGS_MAX + 1);
        memcpy(longline, "enroll ", 7);
        CHECK(!host_auth_parse_command(longline, HOST_AUTH_ARGS_MAX + 1, &cmd));
        free(longline);
    }
    CHECK(!host_auth_parse_command(NULL, 4, &cmd));
}

static void test_parser_fuzz(void) {
    const Vec* v = &vectors[0];
    char auth_hex[2 * HOST_AUTH_AUTH_DATA_LEN + 1];
    char der_hex[2 * HOST_AUTH_SIG_DER_MAX + 1];
    char pub_hex[2 * HOST_AUTH_PUBKEY_LEN + 1];
    host_auth_hex_encode(v->auth_data, HOST_AUTH_AUTH_DATA_LEN, auth_hex);
    host_auth_hex_encode(v->der, v->der_len, der_hex);
    host_auth_hex_encode(v->pub, HOST_AUTH_PUBKEY_LEN, pub_hex);
    char seeds[5][400];
    snprintf(seeds[0], sizeof(seeds[0]), "response %s %s", auth_hex, der_hex);
    snprintf(seeds[1], sizeof(seeds[1]), "enroll %s 1 Mac mini", pub_hex);
    snprintf(seeds[2], sizeof(seeds[2]), "revoke 0011aabbccddeeff");
    snprintf(seeds[3], sizeof(seeds[3]), "revoke all");
    snprintf(seeds[4], sizeof(seeds[4]), "challenge");

    size_t accepted = 0;
    char buf[HOST_AUTH_ARGS_MAX + 64];
    for(int iter = 0; iter < 300000; iter++) {
        size_t len;
        if(iter % 4 == 0) {
            len = rng() % sizeof(buf);
            for(size_t i = 0; i < len; i++)
                buf[i] = (char)(rng() % 3 ? 0x20 + rng() % 0x5F : rng());
        } else {
            const char* seed = seeds[rng() % 5];
            len = strlen(seed);
            memcpy(buf, seed, len);
            int mutations = 1 + rng() % 3;
            for(int m = 0; m < mutations && len > 0; m++) {
                uint32_t op = rng() % 4;
                size_t pos = rng() % len;
                if(op == 0) {
                    buf[pos] = (char)rng();
                } else if(op == 1 && len < sizeof(buf)) {
                    memmove(buf + pos + 1, buf + pos, len - pos);
                    buf[pos] = (rng() % 2) ? ' ' : (char)(0x20 + rng() % 0x5F);
                    len++;
                } else if(op == 2) {
                    memmove(buf + pos, buf + pos + 1, len - pos - 1);
                    len--;
                } else {
                    buf[pos] = "0123456789abcdef "[rng() % 17];
                }
            }
        }
        char* exact = malloc(len ? len : 1);
        memcpy(exact, buf, len);
        HostAuthCommand cmd;
        memset(&cmd, 0xA5, sizeof(cmd));
        bool ok = host_auth_parse_command(exact, len, &cmd);
        if(ok) {
            accepted++;
            CHECK(cmd.type != HostAuthCmdInvalid);
            CHECK(len <= HOST_AUTH_ARGS_MAX);
            if(cmd.type == HostAuthCmdEnroll) {
                size_t l = strnlen(cmd.enroll.label, HOST_AUTH_LABEL_LEN);
                CHECK(l > 0 && l < HOST_AUTH_LABEL_LEN);
            }
        } else {
            CHECK(cmd.type == HostAuthCmdInvalid);
        }
        free(exact);
    }
    printf("parser fuzz: %zu of 300000 inputs accepted\n", accepted);
}

// ====
// Misc
// ====

static void test_hex(void) {
    uint8_t out[4];
    CHECK(host_auth_hex_decode("deadBEEF", 8, out, 4));
    CHECK(out[0] == 0xde && out[1] == 0xad && out[2] == 0xbe && out[3] == 0xef);
    CHECK(!host_auth_hex_decode("deadbee", 7, out, 4));
    CHECK(!host_auth_hex_decode("deadbeef00", 10, out, 4));
    CHECK(!host_auth_hex_decode("deadbeeg", 8, out, 4));
    CHECK(!host_auth_hex_decode("dead bee", 8, out, 4));
    CHECK(!host_auth_hex_decode(NULL, 0, out, 4));
    char enc[9];
    host_auth_hex_encode(out, 4, enc);
    CHECK(strcmp(enc, "deadbeef") == 0);
}

static void test_ct_equal(void) {
    uint8_t a[16], b[16];
    for(int i = 0; i < 16; i++)
        a[i] = b[i] = (uint8_t)i;
    CHECK(host_auth_ct_equal(a, b, 16));
    CHECK(host_auth_ct_equal(a, b, 0));
    for(int i = 0; i < 16; i++) {
        b[i] ^= 0x80;
        CHECK(!host_auth_ct_equal(a, b, 16));
        b[i] ^= 0x80;
    }
}

static void test_backoff(void) {
    CHECK(host_auth_backoff_s(0) == 0);
    CHECK(host_auth_backoff_s(2) == 0);
    CHECK(host_auth_backoff_s(3) == 30);
    CHECK(host_auth_backoff_s(4) == 60);
    CHECK(host_auth_backoff_s(5) == 120);
    CHECK(host_auth_backoff_s(9) == 1920);
    CHECK(host_auth_backoff_s(10) == 3600);
    CHECK(host_auth_backoff_s(0xFFFFFFFFu) == 3600);

    uint32_t clamped;
    // Under the free allowance nothing waits, whatever the clock says
    CHECK(host_auth_backoff_remaining_s(2, 1000, 1000, &clamped) == 0 && clamped == 1000);
    // Waiting down across a reboot: state is (fails, timestamp), not a tick count
    CHECK(host_auth_backoff_remaining_s(3, 1000, 1000, &clamped) == 30);
    CHECK(host_auth_backoff_remaining_s(3, 1000, 1010, &clamped) == 20);
    CHECK(host_auth_backoff_remaining_s(3, 1000, 1030, &clamped) == 0);
    CHECK(host_auth_backoff_remaining_s(3, 1000, 0xFFFFFFFFu, &clamped) == 0);
    CHECK(host_auth_backoff_remaining_s(20, 1000, 1000 + 3599, &clamped) == 1);
    // Clock set backwards: the wait restarts from now, never longer than the cap
    CHECK(host_auth_backoff_remaining_s(4, 5000, 100, &clamped) == 60);
    CHECK(clamped == 100);
    CHECK(host_auth_backoff_remaining_s(4, clamped, 130, &clamped) == 30);
    CHECK(host_auth_backoff_remaining_s(20, 0xFFFFFFFFu, 0, &clamped) == 3600);
    CHECK(clamped == 0);
}

int main(void) {
    test_binding_layout();
    test_base64url();
    test_client_data();
    test_vectors();
    test_sign_count();
    test_der_strict();
    test_der_fuzz();
    test_invalid_pubkeys();
    test_record();
    test_epoch();
    test_load_decide();
    test_parser();
    test_parser_fuzz();
    test_hex();
    test_ct_equal();
    test_backoff();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
