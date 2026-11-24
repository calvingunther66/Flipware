// Host test for applications/services/desktop/helpers/host_auth_core.c
// Built and run by run.sh with the firmware's own mbedtls and mbedtls_cfg.h.

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

static void test_message_layout(void) {
    uint8_t uid[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t nonce[32];
    for(int i = 0; i < 32; i++)
        nonce[i] = (uint8_t)(0xA0 + i);
    uint8_t msg[HOST_AUTH_MESSAGE_LEN];
    CHECK(host_auth_message_build(msg, HostAuthActionUnlock, uid, nonce) == 62);
    CHECK(HOST_AUTH_MESSAGE_LEN == 62);
    CHECK(memcmp(msg, "flipware-hostauth-v1", 20) == 0);
    CHECK(msg[20] == 1);
    CHECK(msg[21] == 8);
    CHECK(memcmp(msg + 22, uid, 8) == 0);
    CHECK(memcmp(msg + 30, nonce, 32) == 0);
}

static void test_vectors(void) {
    for(int i = 0; i < VEC_COUNT; i++) {
        const Vec* v = &vectors[i];
        uint8_t msg[HOST_AUTH_MESSAGE_LEN];
        host_auth_message_build(msg, HostAuthActionUnlock, v->uid, v->nonce);

        CHECK(host_auth_pubkey_is_valid(v->pub));
        CHECK(host_auth_verify(v->pub, msg, sizeof(msg), v->sig));

        uint8_t fp[8];
        host_auth_fingerprint(v->pub, fp);
        CHECK(memcmp(fp, v->fp, 8) == 0);

        // Any single-bit change in the message, the signature or the key fails
        for(size_t bit = 0; bit < sizeof(msg) * 8; bit += 7) {
            uint8_t bad[HOST_AUTH_MESSAGE_LEN];
            memcpy(bad, msg, sizeof(bad));
            bad[bit / 8] ^= (uint8_t)(1u << (bit % 8));
            CHECK(!host_auth_verify(v->pub, bad, sizeof(bad), v->sig));
        }
        for(size_t bit = 0; bit < HOST_AUTH_SIG_LEN * 8; bit += 5) {
            uint8_t bad[HOST_AUTH_SIG_LEN];
            memcpy(bad, v->sig, sizeof(bad));
            bad[bit / 8] ^= (uint8_t)(1u << (bit % 8));
            CHECK(!host_auth_verify(v->pub, msg, sizeof(msg), bad));
        }

        // Wrong action byte (a signature for one action must not authorize another)
        uint8_t other[HOST_AUTH_MESSAGE_LEN];
        host_auth_message_build(other, (HostAuthAction)2, v->uid, v->nonce);
        CHECK(!host_auth_verify(v->pub, other, sizeof(other), v->sig));

        // Signature from a different key
        const Vec* w = &vectors[(i + 1) % VEC_COUNT];
        CHECK(!host_auth_verify(w->pub, msg, sizeof(msg), v->sig));
    }
}

static void test_degenerate_signatures(void) {
    const Vec* v = &vectors[0];
    uint8_t msg[HOST_AUTH_MESSAGE_LEN];
    host_auth_message_build(msg, HostAuthActionUnlock, v->uid, v->nonce);

    uint8_t sig[64];
    memset(sig, 0, sizeof(sig));
    CHECK(!host_auth_verify(v->pub, msg, sizeof(msg), sig)); // r = s = 0

    memcpy(sig, v->sig, 32);
    memset(sig + 32, 0, 32);
    CHECK(!host_auth_verify(v->pub, msg, sizeof(msg), sig)); // s = 0

    memcpy(sig + 32, p256_n, 32);
    CHECK(!host_auth_verify(v->pub, msg, sizeof(msg), sig)); // s = n

    memcpy(sig, p256_n, 32);
    memcpy(sig + 32, v->sig + 32, 32);
    CHECK(!host_auth_verify(v->pub, msg, sizeof(msg), sig)); // r = n

    memset(sig, 0xFF, sizeof(sig));
    CHECK(!host_auth_verify(v->pub, msg, sizeof(msg), sig)); // r, s > n
}

static void test_invalid_pubkeys(void) {
    uint8_t key[65];
    memcpy(key, vectors[0].pub, 65);
    CHECK(host_auth_pubkey_is_valid(key));

    key[64] ^= 1; // off the curve
    CHECK(!host_auth_pubkey_is_valid(key));

    memcpy(key, vectors[0].pub, 65);
    key[0] = 0x02; // compressed prefix with a full-length buffer
    CHECK(!host_auth_pubkey_is_valid(key));

    memset(key, 0, sizeof(key)); // point at infinity encoding
    CHECK(!host_auth_pubkey_is_valid(key));

    memset(key, 0xFF, sizeof(key));
    key[0] = 0x04; // coordinates >= p
    CHECK(!host_auth_pubkey_is_valid(key));

    uint8_t msg[HOST_AUTH_MESSAGE_LEN] = {0};
    CHECK(!host_auth_verify(key, msg, sizeof(msg), vectors[0].sig));
}

static void test_record(void) {
    HostAuthRecord rec;
    memset(&rec, 0, sizeof(rec));

    CHECK(host_auth_record_add(&rec, vectors[0].pub, "Calvin's Mac") == HostAuthAddOk);
    CHECK(host_auth_record_add(&rec, vectors[0].pub, "again") == HostAuthAddDuplicate);
    uint8_t bad[65];
    memcpy(bad, vectors[1].pub, 65);
    bad[10] ^= 0x40;
    CHECK(host_auth_record_add(&rec, bad, "bad") == HostAuthAddInvalidKey);
    CHECK(
        host_auth_record_add(
            &rec,
            vectors[1].pub,
            "a\tb\x01"
            "c") == HostAuthAddOk);
    CHECK(strcmp(rec.keys[1].label, "a?b?c") == 0);
    CHECK(host_auth_record_add(&rec, vectors[2].pub, "") == HostAuthAddOk);
    CHECK(strcmp(rec.keys[2].label, "host") == 0);
    CHECK(
        host_auth_record_add(
            &rec, vectors[3].pub, "a label that is much longer than thirty characters") ==
        HostAuthAddOk);
    CHECK(strlen(rec.keys[3].label) == HOST_AUTH_LABEL_LEN - 1);
    CHECK(host_auth_record_add(&rec, vectors[4].pub, "fifth") == HostAuthAddFull);
    CHECK(rec.count == 4);

    // Round trip
    uint8_t buf[HOST_AUTH_RECORD_LEN];
    host_auth_record_serialize(&rec, buf);
    HostAuthRecord parsed;
    CHECK(host_auth_record_parse(buf, &parsed));
    CHECK(parsed.count == 4);
    CHECK(memcmp(&parsed, &rec, sizeof(rec)) == 0);

    // find_signer picks the right key
    for(int i = 0; i < 4; i++) {
        uint8_t msg[HOST_AUTH_MESSAGE_LEN];
        host_auth_message_build(msg, HostAuthActionUnlock, vectors[i].uid, vectors[i].nonce);
        CHECK(host_auth_record_find_signer(&parsed, msg, sizeof(msg), vectors[i].sig) == i);
    }
    {
        uint8_t msg[HOST_AUTH_MESSAGE_LEN];
        host_auth_message_build(msg, HostAuthActionUnlock, vectors[5].uid, vectors[5].nonce);
        CHECK(host_auth_record_find_signer(&parsed, msg, sizeof(msg), vectors[5].sig) == -1);
    }

    // Strict parsing
    uint8_t tampered[HOST_AUTH_RECORD_LEN];
    memcpy(tampered, buf, sizeof(buf));
    tampered[0] ^= 1;
    CHECK(!host_auth_record_parse(tampered, &parsed)); // magic
    memcpy(tampered, buf, sizeof(buf));
    tampered[4] = 2;
    CHECK(!host_auth_record_parse(tampered, &parsed)); // version
    memcpy(tampered, buf, sizeof(buf));
    tampered[5] = 5;
    CHECK(!host_auth_record_parse(tampered, &parsed)); // count
    memcpy(tampered, buf, sizeof(buf));
    tampered[9] = 1;
    CHECK(!host_auth_record_parse(tampered, &parsed)); // reserved
    memcpy(tampered, buf, sizeof(buf));
    tampered[16 + HOST_AUTH_PUBKEY_LEN] = 0;
    CHECK(!host_auth_record_parse(tampered, &parsed)); // in-use flag
    memcpy(tampered, buf, sizeof(buf));
    tampered[16 + HOST_AUTH_KEY_ENTRY_LEN - 1] = 'x';
    CHECK(!host_auth_record_parse(tampered, &parsed)); // unterminated label

    // Remove by fingerprint, then unused slots must be zero
    uint8_t fp[8];
    host_auth_fingerprint(vectors[1].pub, fp);
    CHECK(host_auth_record_remove(&rec, fp));
    CHECK(!host_auth_record_remove(&rec, fp));
    CHECK(rec.count == 3);
    CHECK(memcmp(rec.keys[1].pubkey, vectors[2].pub, 65) == 0);
    host_auth_record_serialize(&rec, buf);
    CHECK(host_auth_record_parse(buf, &parsed));
    CHECK(parsed.count == 3);
    memcpy(tampered, buf, sizeof(buf));
    tampered[16 + 3 * HOST_AUTH_KEY_ENTRY_LEN + 4] = 1; // junk in an unused slot
    CHECK(!host_auth_record_parse(tampered, &parsed));

    // Empty record
    HostAuthRecord empty;
    memset(&empty, 0, sizeof(empty));
    host_auth_record_serialize(&empty, buf);
    CHECK(host_auth_record_parse(buf, &parsed));
    CHECK(parsed.count == 0);
}

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

static void test_backoff(void) {
    CHECK(host_auth_backoff_s(0) == 0);
    CHECK(host_auth_backoff_s(2) == 0);
    CHECK(host_auth_backoff_s(3) == 30);
    CHECK(host_auth_backoff_s(4) == 60);
    CHECK(host_auth_backoff_s(5) == 120);
    CHECK(host_auth_backoff_s(9) == 1920);
    CHECK(host_auth_backoff_s(10) == 3600);
    CHECK(host_auth_backoff_s(0xFFFFFFFFu) == 3600);
}

int main(void) {
    test_message_layout();
    test_vectors();
    test_degenerate_signatures();
    test_invalid_pubkeys();
    test_record();
    test_hex();
    test_backoff();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
