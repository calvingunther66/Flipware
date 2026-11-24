#include "subghz_compare.h"

#include <string.h>

static void subghz_compare_set_bit(uint8_t* bytes, uint16_t index, bool value) {
    uint8_t mask = 0x80 >> (index % 8);
    if(value) {
        bytes[index / 8] |= mask;
    } else {
        bytes[index / 8] &= ~mask;
    }
}

static bool subghz_compare_read_bit(const uint8_t* bytes, uint16_t index) {
    return (bytes[index / 8] >> (7 - (index % 8))) & 1;
}

void subghz_compare_capture_reset(SubGhzCompareCapture* capture) {
    memset(capture, 0, sizeof(SubGhzCompareCapture));
}

void subghz_compare_capture_set_key(
    SubGhzCompareCapture* capture,
    const uint8_t key[8],
    uint16_t declared_bits) {
    uint16_t bits = declared_bits > 64 ? 64 : declared_bits;

    memset(capture->data, 0, sizeof(capture->data));
    capture->declared_bits = declared_bits;
    capture->bit_count = bits;
    capture->truncated = declared_bits > bits;

    // Key is big endian and right aligned: the first sent bit is bit (bits - 1)
    // of the 64-bit value, which sits at position (64 - bits) of the byte string.
    uint16_t offset = 64 - bits;
    for(uint16_t i = 0; i < bits; i++) {
        subghz_compare_set_bit(capture->data, i, subghz_compare_read_bit(key, offset + i));
    }
}

void subghz_compare_capture_set_bits(
    SubGhzCompareCapture* capture,
    const uint8_t* bytes,
    size_t bytes_len,
    uint16_t declared_bits) {
    size_t available = bytes_len * 8;
    uint16_t bits = declared_bits;
    if(bits > available) bits = (uint16_t)available;
    if(bits > SUBGHZ_COMPARE_MAX_BITS) bits = SUBGHZ_COMPARE_MAX_BITS;

    memset(capture->data, 0, sizeof(capture->data));
    capture->declared_bits = declared_bits;
    capture->bit_count = bits;
    capture->truncated = declared_bits > bits;

    for(uint16_t i = 0; i < bits; i++) {
        subghz_compare_set_bit(capture->data, i, subghz_compare_read_bit(bytes, i));
    }
}

bool subghz_compare_capture_get_bit(const SubGhzCompareCapture* capture, uint16_t index) {
    if(index >= capture->bit_count) return false;
    return subghz_compare_read_bit(capture->data, index);
}

bool subghz_compare_is_comparable(const SubGhzCompareCapture* a, const SubGhzCompareCapture* b) {
    bool a_ok = a->kind == SubGhzCompareKindKey || a->kind == SubGhzCompareKindBinRaw;
    bool b_ok = b->kind == SubGhzCompareKindKey || b->kind == SubGhzCompareKindBinRaw;
    return a_ok && b_ok && a->bit_count > 0 && b->bit_count > 0;
}

void subghz_compare_run(
    const SubGhzCompareCapture* a,
    const SubGhzCompareCapture* b,
    SubGhzCompareResult* result) {
    memset(result, 0, sizeof(SubGhzCompareResult));

    result->protocol_mismatch = strncmp(a->protocol, b->protocol, SUBGHZ_COMPARE_NAME_LEN) != 0;
    result->length_mismatch = a->declared_bits != b->declared_bits;
    result->frequency_mismatch = a->frequency != b->frequency;
    result->truncated = a->truncated || b->truncated;

    if(!subghz_compare_is_comparable(a, b)) {
        return;
    }

    uint16_t compared = a->bit_count < b->bit_count ? a->bit_count : b->bit_count;
    result->compared_bits = compared;
    result->first_diff = compared;

    uint16_t run_start = 0;
    uint16_t run_len = 0;
    bool prev_differs = false;

    for(uint16_t i = 0; i < compared; i++) {
        bool differs = subghz_compare_capture_get_bit(a, i) != subghz_compare_capture_get_bit(b, i);
        if(differs) {
            subghz_compare_set_bit(result->diff_mask, i, true);
            result->diff_bits++;
            if(!prev_differs) result->diff_regions++;
            if(result->first_diff == compared) result->first_diff = i;
            run_len = 0;
        } else {
            if(run_len == 0) run_start = i;
            run_len++;
            if(run_len > result->longest_fixed_len) {
                result->longest_fixed_len = run_len;
                result->longest_fixed_start = run_start;
            }
        }
        prev_differs = differs;
    }
}

bool subghz_compare_result_bit_differs(const SubGhzCompareResult* result, uint16_t index) {
    if(index >= result->compared_bits) return false;
    return subghz_compare_read_bit(result->diff_mask, index);
}

uint8_t subghz_compare_hex_pad(const SubGhzCompareCapture* capture) {
    if(capture->kind != SubGhzCompareKindKey) return 0;
    return (4 - (capture->bit_count % 4)) % 4;
}

uint16_t subghz_compare_nibble_count(const SubGhzCompareCapture* capture) {
    return (capture->bit_count + subghz_compare_hex_pad(capture) + 3) / 4;
}

char subghz_compare_nibble_char(const SubGhzCompareCapture* capture, uint16_t nibble) {
    static const char hex[] = "0123456789ABCDEF";
    int32_t pad = subghz_compare_hex_pad(capture);
    uint8_t value = 0;
    for(uint8_t i = 0; i < 4; i++) {
        int32_t index = (int32_t)nibble * 4 + i - pad;
        bool bit = index >= 0 && subghz_compare_capture_get_bit(capture, (uint16_t)index);
        value = (value << 1) | (bit ? 1 : 0);
    }
    return hex[value];
}

bool subghz_compare_nibble_differs(
    const SubGhzCompareResult* result,
    uint8_t pad,
    uint16_t nibble) {
    for(uint8_t i = 0; i < 4; i++) {
        int32_t index = (int32_t)nibble * 4 + i - pad;
        if(index >= 0 && subghz_compare_result_bit_differs(result, (uint16_t)index)) {
            return true;
        }
    }
    return false;
}
