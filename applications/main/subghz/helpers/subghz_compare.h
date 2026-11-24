#pragma once

/**
 * @file subghz_compare.h
 * Bit-by-bit comparison of two saved Sub-GHz captures.
 *
 * Pure logic with no Furi dependencies, so it can be unit tested on the host
 * (see tests/subghz_compare_test.c). Loading .sub files into a
 * SubGhzCompareCapture is done by the compare scene.
 *
 * A capture holds its bits as an MSB-first bit string: bit 0 is the first
 * bit that is sent. For key-based protocols that is the most significant of
 * the `Bit:` low bits of the 64-bit `Key:`; for BinRAW it is the first bit
 * of the first `Data_RAW:` block.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Most bits kept per capture. Longer BinRAW blocks are cut here. */
#define SUBGHZ_COMPARE_MAX_BITS  512
#define SUBGHZ_COMPARE_MAX_BYTES (SUBGHZ_COMPARE_MAX_BITS / 8)
#define SUBGHZ_COMPARE_NAME_LEN  32

typedef enum {
    SubGhzCompareKindNone, /**< Not loaded */
    SubGhzCompareKindKey, /**< Decoded protocol with a 64-bit Key */
    SubGhzCompareKindBinRaw, /**< BinRAW, first Data_RAW block */
    SubGhzCompareKindRaw, /**< RAW pulse timings, not comparable in v1 */
} SubGhzCompareKind;

typedef struct {
    SubGhzCompareKind kind;
    char protocol[SUBGHZ_COMPARE_NAME_LEN];
    uint32_t frequency;
    uint16_t declared_bits; /**< Value of the file's Bit: field */
    uint16_t bit_count; /**< Bits actually held in data */
    bool truncated; /**< declared_bits > bit_count */
    uint8_t data[SUBGHZ_COMPARE_MAX_BYTES]; /**< MSB-first bit string */
} SubGhzCompareCapture;

typedef struct {
    uint16_t compared_bits; /**< min(a.bit_count, b.bit_count) */
    uint16_t diff_bits; /**< Bits that differ within compared_bits */
    uint16_t diff_regions; /**< Runs of consecutive differing bits */
    uint16_t longest_fixed_start;
    uint16_t longest_fixed_len; /**< Longest run of equal bits */
    uint16_t first_diff; /**< Index of first differing bit, or compared_bits */
    bool protocol_mismatch;
    bool length_mismatch; /**< Declared bit counts differ */
    bool frequency_mismatch;
    bool truncated; /**< Either capture was cut to fit */
    uint8_t diff_mask[SUBGHZ_COMPARE_MAX_BYTES]; /**< Bit i set when bit i differs */
} SubGhzCompareResult;

/** Reset a capture to empty. */
void subghz_compare_capture_reset(SubGhzCompareCapture* capture);

/**
 * Fill a capture from a 64-bit Key as stored in .sub files (8 bytes, big
 * endian, value right aligned). Keeps the low min(declared_bits, 64) bits.
 */
void subghz_compare_capture_set_key(
    SubGhzCompareCapture* capture,
    const uint8_t key[8],
    uint16_t declared_bits);

/**
 * Fill a capture from an MSB-first byte string (BinRAW Data_RAW). Keeps at
 * most SUBGHZ_COMPARE_MAX_BITS bits and never more than bytes_len * 8.
 */
void subghz_compare_capture_set_bits(
    SubGhzCompareCapture* capture,
    const uint8_t* bytes,
    size_t bytes_len,
    uint16_t declared_bits);

/** Bit at index (0 = first sent). Out-of-range indexes read as 0. */
bool subghz_compare_capture_get_bit(const SubGhzCompareCapture* capture, uint16_t index);

/** True when the two captures can be compared bit by bit. */
bool subghz_compare_is_comparable(const SubGhzCompareCapture* a, const SubGhzCompareCapture* b);

/** Compare two captures. Fills every field of result. */
void subghz_compare_run(
    const SubGhzCompareCapture* a,
    const SubGhzCompareCapture* b,
    SubGhzCompareResult* result);

/** True when bit index differs (index < compared_bits). */
bool subghz_compare_result_bit_differs(const SubGhzCompareResult* result, uint16_t index);

/**
 * Zero bits added in front of the bit string so hex nibbles line up with the
 * Key as the file shows it. Key captures pad on the left, BinRAW does not.
 */
uint8_t subghz_compare_hex_pad(const SubGhzCompareCapture* capture);

/** Number of hex nibbles needed to show the capture with its padding. */
uint16_t subghz_compare_nibble_count(const SubGhzCompareCapture* capture);

/** Hex digit ('0'-'F') of nibble index, using the capture's padding. */
char subghz_compare_nibble_char(const SubGhzCompareCapture* capture, uint16_t nibble);

/**
 * True when any real bit inside nibble index differs. pad is the padding of
 * the capture the nibbles are drawn for (normally capture A).
 */
bool subghz_compare_nibble_differs(const SubGhzCompareResult* result, uint8_t pad, uint16_t nibble);

#ifdef __cplusplus
}
#endif
