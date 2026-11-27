/*
 * Host-side tests for the Sub-GHz capture compare logic.
 * Not built into the firmware (excluded in applications/main/subghz/application.fam).
 *
 * Run from the repo root:
 *   tests/host/subghz_compare/run.sh
 * or all host tests: ./fbt test_host. CI runs it on every pull request.
 */

#include "subghz_compare.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                \
    do {                                                           \
        checks++;                                                  \
        if(!(cond)) {                                              \
            failures++;                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while(0)

static void key_capture(
    SubGhzCompareCapture* cap,
    const char* protocol,
    uint64_t value,
    uint16_t bits,
    uint32_t frequency) {
    uint8_t key[8];
    for(int i = 0; i < 8; i++) {
        key[7 - i] = (value >> (i * 8)) & 0xFF;
    }
    subghz_compare_capture_reset(cap);
    cap->kind = SubGhzCompareKindKey;
    strncpy(cap->protocol, protocol, SUBGHZ_COMPARE_NAME_LEN - 1);
    cap->frequency = frequency;
    subghz_compare_capture_set_key(cap, key, bits);
}

static void hex_string(const SubGhzCompareCapture* cap, char* out) {
    uint16_t n = subghz_compare_nibble_count(cap);
    for(uint16_t i = 0; i < n; i++) {
        out[i] = subghz_compare_nibble_char(cap, i);
    }
    out[n] = 0;
}

static void test_identical(void) {
    SubGhzCompareCapture a, b;
    SubGhzCompareResult r;
    key_capture(&a, "Princeton", 0x95D5D4, 24, 433920000);
    key_capture(&b, "Princeton", 0x95D5D4, 24, 433920000);
    subghz_compare_run(&a, &b, &r);
    CHECK(r.compared_bits == 24);
    CHECK(r.diff_bits == 0);
    CHECK(r.diff_regions == 0);
    CHECK(r.longest_fixed_len == 24);
    CHECK(r.longest_fixed_start == 0);
    CHECK(r.first_diff == 24);
    CHECK(!r.protocol_mismatch);
    CHECK(!r.length_mismatch);
    CHECK(!r.frequency_mismatch);
}

static void test_bit_order(void) {
    SubGhzCompareCapture a;
    // 24-bit 0x800001: first sent bit and last sent bit set
    key_capture(&a, "Princeton", 0x800001, 24, 433920000);
    CHECK(a.bit_count == 24);
    CHECK(subghz_compare_capture_get_bit(&a, 0));
    CHECK(!subghz_compare_capture_get_bit(&a, 1));
    CHECK(!subghz_compare_capture_get_bit(&a, 22));
    CHECK(subghz_compare_capture_get_bit(&a, 23));
    CHECK(!subghz_compare_capture_get_bit(&a, 24)); // out of range reads 0
}

static void test_rolling_low_bits(void) {
    // Fixed 16-bit serial in front, 8 rolling bits at the end
    SubGhzCompareCapture a, b;
    SubGhzCompareResult r;
    key_capture(&a, "Test", 0xABCD12, 24, 433920000);
    key_capture(&b, "Test", 0xABCDE7, 24, 433920000);
    subghz_compare_run(&a, &b, &r);
    // 0x12 ^ 0xE7 = 0xF5 = 11110101 -> 6 bits differ, regions: 1111,1,1 = 3
    CHECK(r.diff_bits == 6);
    CHECK(r.diff_regions == 3);
    CHECK(r.first_diff == 16);
    CHECK(r.longest_fixed_start == 0);
    CHECK(r.longest_fixed_len == 16);
    CHECK(subghz_compare_result_bit_differs(&r, 16));
    CHECK(!subghz_compare_result_bit_differs(&r, 15));
    CHECK(!subghz_compare_result_bit_differs(&r, 20));
    CHECK(subghz_compare_result_bit_differs(&r, 23));
}

static void test_longest_run_in_middle(void) {
    SubGhzCompareCapture a, b;
    SubGhzCompareResult r;
    // differ at bit 0 and bit 31 only -> fixed run 1..30
    key_capture(&a, "X", 0x80000001, 32, 1);
    key_capture(&b, "X", 0x00000000, 32, 1);
    subghz_compare_run(&a, &b, &r);
    CHECK(r.diff_bits == 2);
    CHECK(r.diff_regions == 2);
    CHECK(r.longest_fixed_start == 1);
    CHECK(r.longest_fixed_len == 30);
}

static void test_mismatches(void) {
    SubGhzCompareCapture a, b;
    SubGhzCompareResult r;
    key_capture(&a, "Princeton", 0xFFFFFF, 24, 433920000);
    key_capture(&b, "CAME", 0xFFF, 12, 315000000);
    subghz_compare_run(&a, &b, &r);
    CHECK(r.protocol_mismatch);
    CHECK(r.length_mismatch);
    CHECK(r.frequency_mismatch);
    CHECK(r.compared_bits == 12);
    CHECK(r.diff_bits == 0);
}

static void test_64_and_over(void) {
    SubGhzCompareCapture a, b;
    SubGhzCompareResult r;
    key_capture(&a, "KeeLoq", 0x0123456789ABCDEFULL, 64, 433920000);
    key_capture(&b, "KeeLoq", 0x0123456789ABCDEEULL, 64, 433920000);
    CHECK(a.bit_count == 64);
    CHECK(!a.truncated);
    subghz_compare_run(&a, &b, &r);
    CHECK(r.diff_bits == 1);
    CHECK(r.first_diff == 63);

    key_capture(&a, "Nice FloR-S", 0x1ULL, 72, 433920000);
    CHECK(a.bit_count == 64);
    CHECK(a.truncated);
    subghz_compare_run(&a, &a, &r);
    CHECK(r.truncated);
}

static void test_hex(void) {
    SubGhzCompareCapture a, b;
    SubGhzCompareResult r;
    char hex[SUBGHZ_COMPARE_MAX_BITS / 4 + 2];

    key_capture(&a, "Princeton", 0x95D5D4, 24, 1);
    hex_string(&a, hex);
    CHECK(strcmp(hex, "95D5D4") == 0);
    CHECK(subghz_compare_hex_pad(&a) == 0);

    // 10 bits: 0x2AB -> padded left by 2 -> "2AB"
    key_capture(&a, "X", 0x2AB, 10, 1);
    CHECK(subghz_compare_hex_pad(&a) == 2);
    CHECK(subghz_compare_nibble_count(&a) == 3);
    hex_string(&a, hex);
    CHECK(strcmp(hex, "2AB") == 0);

    key_capture(&b, "X", 0x2AA, 10, 1);
    subghz_compare_run(&a, &b, &r);
    CHECK(r.diff_bits == 1);
    CHECK(!subghz_compare_nibble_differs(&r, subghz_compare_hex_pad(&a), 0));
    CHECK(!subghz_compare_nibble_differs(&r, subghz_compare_hex_pad(&a), 1));
    CHECK(subghz_compare_nibble_differs(&r, subghz_compare_hex_pad(&a), 2));
}

static void test_bin_raw(void) {
    SubGhzCompareCapture a, b;
    SubGhzCompareResult r;
    char hex[SUBGHZ_COMPARE_MAX_BITS / 4 + 2];
    const uint8_t da[] = {0xAA, 0x55, 0xF0};
    const uint8_t db[] = {0xAA, 0x54, 0xF0};

    subghz_compare_capture_reset(&a);
    a.kind = SubGhzCompareKindBinRaw;
    strcpy(a.protocol, "BinRAW");
    subghz_compare_capture_set_bits(&a, da, sizeof(da), 20);
    subghz_compare_capture_reset(&b);
    b.kind = SubGhzCompareKindBinRaw;
    strcpy(b.protocol, "BinRAW");
    subghz_compare_capture_set_bits(&b, db, sizeof(db), 20);

    CHECK(a.bit_count == 20);
    CHECK(subghz_compare_hex_pad(&a) == 0);
    hex_string(&a, hex);
    CHECK(strcmp(hex, "AA55F") == 0);

    subghz_compare_run(&a, &b, &r);
    CHECK(r.diff_bits == 1);
    CHECK(r.first_diff == 15);

    // declared bits beyond the supplied bytes are cut and flagged
    subghz_compare_capture_set_bits(&a, da, sizeof(da), 40);
    CHECK(a.bit_count == 24);
    CHECK(a.truncated);

    // more than the cap
    uint8_t big[SUBGHZ_COMPARE_MAX_BYTES + 8];
    memset(big, 0xFF, sizeof(big));
    subghz_compare_capture_set_bits(&a, big, sizeof(big), sizeof(big) * 8);
    CHECK(a.bit_count == SUBGHZ_COMPARE_MAX_BITS);
    CHECK(a.truncated);
    CHECK(subghz_compare_capture_get_bit(&a, SUBGHZ_COMPARE_MAX_BITS - 1));
}

static void test_raw_not_comparable(void) {
    SubGhzCompareCapture a, b;
    SubGhzCompareResult r;
    key_capture(&a, "Princeton", 0x1, 24, 1);
    subghz_compare_capture_reset(&b);
    b.kind = SubGhzCompareKindRaw;
    strcpy(b.protocol, "RAW");
    CHECK(!subghz_compare_is_comparable(&a, &b));
    subghz_compare_run(&a, &b, &r);
    CHECK(r.compared_bits == 0);
    CHECK(r.diff_bits == 0);
    CHECK(r.protocol_mismatch);
}

int main(void) {
    test_identical();
    test_bit_order();
    test_rolling_low_bits();
    test_longest_run_in_middle();
    test_mismatches();
    test_64_and_over();
    test_hex();
    test_bin_raw();
    test_raw_not_comparable();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
