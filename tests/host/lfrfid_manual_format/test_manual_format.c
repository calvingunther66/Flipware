// Host test for the RFID "enter by facility code and card number" helpers. Run run.sh.
// Builds the real helpers, bit_lib and protocol sources with a small furi shim, then:
//  - encodes FC/card values for every format and checks the field ranges are enforced,
//  - runs each protocol's own encoder into its own decoder and checks the data comes back,
//  - checks the rendered text shows the FC and card that were entered,
//  - round-trips the HID Proximity and Casi-Rusco formats through their readers,
//  - checks the Pyramid Wiegand parity fix against a reference parity.
#include <furi.h>
#include <inttypes.h>
#include <toolbox/protocols/protocol.h>
#include <lfrfid/protocols/lfrfid_protocols.h>
#include <lfrfid/protocols/protocol_em4100.h>
#include <lfrfid/protocols/protocol_h10301.h>
#include <lfrfid/protocols/protocol_io_prox_xsf.h>
#include <lfrfid/protocols/protocol_awid.h>
#include <lfrfid/protocols/protocol_pyramid.h>
#include <lfrfid/protocols/protocol_gallagher.h>
#include <lfrfid/protocols/protocol_hid_generic.h>
#include <bit_lib/bit_lib.h>
#include <toolbox/pulse_protocols/pulse_glue.h>
#include "lfrfid_manual_format.h"
#include "lfrfid_hid_format.h"
#include "lfrfid_casi_format.h"

// Only the protocols the manual formats touch are real; the rest are never dereferenced
// by these tests.
const ProtocolBase* const lfrfid_protocols[LFRFIDProtocolMax] = {
    [LFRFIDProtocolEM4100] = &protocol_em4100,
    [LFRFIDProtocolEM410032] = &protocol_em4100_32,
    [LFRFIDProtocolEM410016] = &protocol_em4100_16,
    [LFRFIDProtocolH10301] = &protocol_h10301,
    [LFRFIDProtocolIOProxXSF] = &protocol_io_prox_xsf,
    [LFRFIDProtocolAwid] = &protocol_awid,
    [LFRFIDProtocolHidGeneric] = &protocol_hid_generic,
    [LFRFIDProtocolPyramid] = &protocol_pyramid,
    [LFRFIDProtocolGallagher] = &protocol_gallagher,
};

bool protocol_pyramid_get_parity(const uint8_t* bits, size_t position, uint8_t type, int length);
void protocol_pyramid_add_wiegand_parity(
    uint8_t* target,
    uint8_t target_position,
    uint8_t* source,
    uint8_t length);

static int failures = 0;
static int checks = 0;
#define CHECK(cond, ...)                                           \
    do {                                                           \
        checks++;                                                  \
        if(!(cond)) {                                              \
            failures++;                                            \
            printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); \
            printf(__VA_ARGS__);                                   \
            printf("\n");                                          \
        }                                                          \
    } while(0)

// Run the protocol's encoder into a fresh decoder until it reports a card, and return
// whether the decoded data equals what was encoded.
static bool roundtrip(ProtocolId id, const uint8_t* data) {
    const ProtocolBase* base = lfrfid_protocols[id];
    void* enc = base->alloc();
    void* dec = base->alloc();
    memcpy(base->get_data(enc), data, base->data_size);
    bool ok = false;
    if(base->encoder.start(enc)) {
        // feed the decoder the way the reader does: carrier periods x8 through pulse glue,
        // the same path the on-device lfrfid unit tests use
        base->decoder.start(dec);
        PulseGlue* glue = pulse_glue_alloc();
        for(size_t i = 0; i < 200000 && !ok; i++) {
            LevelDuration ld = base->encoder.yield(enc);
            if(level_duration_is_reset(ld)) continue;
            if(!pulse_glue_push(
                   glue, level_duration_get_level(ld), level_duration_get_duration(ld) * 8)) {
                continue;
            }
            uint32_t length, period;
            pulse_glue_pop(glue, &length, &period);
            bool done = base->decoder.feed(dec, true, period) ||
                        base->decoder.feed(dec, false, length - period);
            if(done) {
                ok = memcmp(base->get_data(dec), data, base->data_size) == 0;
                break;
            }
        }
        pulse_glue_free(glue);
    }
    base->free(enc);
    base->free(dec);
    return ok;
}

static void render(ProtocolId id, const uint8_t* data, FuriString* out) {
    const ProtocolBase* base = lfrfid_protocols[id];
    void* p = base->alloc();
    memcpy(base->get_data(p), data, base->data_size);
    furi_string_reset(out);
    base->render_data(p, out);
    lfrfid_manual_format_render(id, data, out);
    base->free(p);
}

static void test_format(uint32_t format, const uint64_t* values, const char* const* expect) {
    FuriString* label = furi_string_alloc();
    FuriString* text = furi_string_alloc();
    lfrfid_manual_format_get_label(format, label);
    ProtocolId id = lfrfid_manual_format_protocol(format);
    size_t count = lfrfid_manual_format_fields_count(format);
    uint8_t data[16] = {0};
    size_t size = lfrfid_protocols[id]->data_size;

    bool encoded = lfrfid_manual_format_encode(format, values, count, data, size);
    CHECK(encoded, "%s encode", furi_string_get_cstr(label));

    CHECK(roundtrip(id, data), "%s encoder->decoder round trip", furi_string_get_cstr(label));

    render(id, data, text);
    for(size_t i = 0; expect[i]; i++) {
        CHECK(
            strstr(furi_string_get_cstr(text), expect[i]) != NULL,
            "%s render missing \"%s\" in \"%s\"",
            furi_string_get_cstr(label),
            expect[i],
            furi_string_get_cstr(text));
    }

    // every field's max is accepted and max + 1 refused
    for(size_t f = 0; f < count; f++) {
        LfRfidManualFormatField field;
        CHECK(lfrfid_manual_format_field(format, f, &field), "field %zu", f);
        uint64_t v[LFRFID_MANUAL_FORMAT_FIELDS_MAX];
        memcpy(v, values, sizeof(uint64_t) * count);
        v[f] = field.max;
        CHECK(
            lfrfid_manual_format_encode(format, v, count, data, size),
            "%s %s max accepted",
            furi_string_get_cstr(label),
            field.name);
        v[f] = field.max + 1;
        CHECK(
            !lfrfid_manual_format_encode(format, v, count, data, size),
            "%s %s max+1 refused",
            furi_string_get_cstr(label),
            field.name);
        if(field.min > 0) {
            v[f] = field.min - 1;
            CHECK(
                !lfrfid_manual_format_encode(format, v, count, data, size),
                "%s %s min-1 refused",
                furi_string_get_cstr(label),
                field.name);
        }
    }
    CHECK(
        !lfrfid_manual_format_encode(format, values, count - 1, data, size),
        "%s wrong value count refused",
        furi_string_get_cstr(label));
    CHECK(
        !lfrfid_manual_format_encode(format, values, count, data, size - 1),
        "%s short buffer refused",
        furi_string_get_cstr(label));

    printf("ok   %-22s -> %s\n", furi_string_get_cstr(label), furi_string_get_cstr(text));
    furi_string_free(label);
    furi_string_free(text);
}

static void test_hid_roundtrips(void) {
    for(size_t i = 0; i < LFRFID_HID_FORMAT_COUNT; i++) {
        const LfRfidHidFormat* fmt = lfrfid_hid_format_get(i);
        const uint64_t fc_max = lfrfid_hid_format_get_facility_code_max(fmt);
        const uint64_t cn_max = lfrfid_hid_format_get_card_number_max(fmt);
        const uint64_t fcs[] = {0, 1, fc_max / 3, fc_max};
        const uint64_t cns[] = {0, 1, 12345 % (cn_max + 1), cn_max / 7, cn_max};
        for(size_t a = 0; a < COUNT_OF(fcs); a++) {
            for(size_t b = 0; b < COUNT_OF(cns); b++) {
                uint8_t data[6];
                lfrfid_hid_format_encode(fmt, fcs[a], cns[b], data);
                CHECK(
                    roundtrip(LFRFIDProtocolHidGeneric, data),
                    "HID %s fc %" PRIu64 " cn %" PRIu64 " round trip",
                    lfrfid_hid_format_get_name(fmt),
                    fcs[a],
                    cns[b]);
                FuriString* text = furi_string_alloc();
                lfrfid_hid_format_render(data, text);
                char want[80];
                if(lfrfid_hid_format_has_facility_code(fmt)) {
                    snprintf(
                        want,
                        sizeof(want),
                        "%s: FC %" PRIu32 " Card %" PRIu64,
                        lfrfid_hid_format_get_name(fmt),
                        (uint32_t)fcs[a],
                        cns[b]);
                } else {
                    snprintf(
                        want,
                        sizeof(want),
                        "%s: Card %" PRIu64,
                        lfrfid_hid_format_get_name(fmt),
                        cns[b]);
                }
                CHECK(
                    strstr(furi_string_get_cstr(text), want) != NULL,
                    "HID render want \"%s\" got \"%s\"",
                    want,
                    furi_string_get_cstr(text));
                furi_string_free(text);
            }
        }
    }
    printf("ok   HID formats encode/read back across their ranges\n");
}

static void test_hid_rejects_bad_parity(void) {
    static const struct {
        size_t format;
        size_t field_bit;
    } data_bits[] = {
        {0, 25}, // H10304: frame bit 18 in the second parity group
        {1, 25}, // H10302
        {2, 25}, // S10401
        {3, 27}, // H10306: frame bit 17
        {4, 42}, // Corporate 1000: frame bit 33
    };

    FuriString* text = furi_string_alloc();
    for(size_t i = 0; i < COUNT_OF(data_bits); i++) {
        const LfRfidHidFormat* fmt = lfrfid_hid_format_get(data_bits[i].format);
        uint8_t data[6];
        lfrfid_hid_format_encode(fmt, 0, 0, data);
        bit_lib_set_bit(
            data,
            data_bits[i].field_bit,
            !bit_lib_get_bit(data, data_bits[i].field_bit));

        furi_string_set(text, "unchanged");
        lfrfid_hid_format_render(data, text);
        CHECK(
            strcmp(furi_string_get_cstr(text), "unchanged") == 0,
            "HID %s accepted data with bad parity: %s",
            lfrfid_hid_format_get_name(fmt),
            furi_string_get_cstr(text));
    }
    furi_string_free(text);
    printf("ok   HID formats reject frames with invalid parity\n");
}

static void test_casi(void) {
    const uint32_t creds[] = {150000, 151234, 159999};
    const uint32_t cards[] = {0, 1, 195537, 195538, 262143, 262144, 400000, LFRFID_CASI_CARD_MAX};
    for(size_t a = 0; a < COUNT_OF(creds); a++) {
        for(size_t b = 0; b < COUNT_OF(cards); b++) {
            uint8_t data[5];
            lfrfid_casi_format_encode(creds[a], cards[b], data);
            FuriString* text = furi_string_alloc();
            lfrfid_casi_format_render(data, text);
            char want[40];
            snprintf(want, sizeof(want), "C10106: %06" PRIu32 " %06" PRIu32, creds[a], cards[b]);
            CHECK(
                strstr(furi_string_get_cstr(text), want) != NULL,
                "Casi want \"%s\" got \"%s\"",
                want,
                furi_string_get_cstr(text));
            CHECK(roundtrip(LFRFIDProtocolEM410032, data), "Casi as EM4100/32 round trip");
            furi_string_free(text);
        }
    }
    printf("ok   Casi-Rusco encode/read back\n");
}

static void test_casi_rejects_invalid_frames(void) {
    static const uint32_t invalid_credentials[] = {
        LFRFID_CASI_CREDENTIAL_MIN - 1,
        LFRFID_CASI_CREDENTIAL_MAX + 1,
    };

    FuriString* text = furi_string_alloc();
    for(size_t i = 0; i < COUNT_OF(invalid_credentials); i++) {
        uint8_t data[5];
        lfrfid_casi_format_encode(LFRFID_CASI_CREDENTIAL_MIN, 0, data);
        for(uint8_t bit = 0; bit < LFRFID_CASI_FIELD_SIZE; bit++) {
            bit_lib_set_bit(
                data,
                2 + bit,
                (invalid_credentials[i] >> (LFRFID_CASI_FIELD_SIZE - 1 - bit)) & 1);
        }

        furi_string_set(text, "unchanged");
        lfrfid_casi_format_render(data, text);
        CHECK(
            strcmp(furi_string_get_cstr(text), "unchanged") == 0,
            "Casi accepted credential %" PRIu32,
            invalid_credentials[i]);
    }

    uint8_t data[5];
    lfrfid_casi_format_encode(LFRFID_CASI_CREDENTIAL_MIN, 0, data);
    bit_lib_set_bit(data, 0, true);
    furi_string_set(text, "unchanged");
    lfrfid_casi_format_render(data, text);
    CHECK(
        strcmp(furi_string_get_cstr(text), "unchanged") == 0,
        "Casi accepted a nonzero reserved prefix");
    furi_string_free(text);
    printf("ok   Casi-Rusco rejects invalid credentials and reserved bits\n");
}

// Reference Wiegand 26: even parity over the first 12 data bits, odd over the last 12
static void test_pyramid_parity(void) {
    for(uint32_t fc = 0; fc < 256; fc += 17) {
        for(uint32_t cn = 0; cn < 65536; cn += 4099) {
            uint8_t src[3] = {fc, cn >> 8, cn & 0xFF};
            uint8_t out[8] = {0};
            protocol_pyramid_add_wiegand_parity(out, 0, src, 24);
            uint32_t bits = (fc << 16) | cn;
            int ones_hi = __builtin_popcount(bits >> 12);
            int ones_lo = __builtin_popcount(bits & 0xFFF);
            bool even = ones_hi & 1; // makes the first half even
            bool odd = !(ones_lo & 1); // makes the second half odd
            CHECK(bit_lib_get_bit(out, 0) == even, "pyramid even parity fc %u cn %u", fc, cn);
            CHECK(bit_lib_get_bits_32(out, 1, 24) == bits, "pyramid data fc %u cn %u", fc, cn);
            CHECK(bit_lib_get_bit(out, 25) == odd, "pyramid odd parity fc %u cn %u", fc, cn);
        }
    }
    printf("ok   Pyramid Wiegand parity matches reference\n");
}

int main(void) {
    {
        const uint64_t v[] = {123, 45678};
        const char* const e[] = {"123", "45678", NULL};
        test_format(LFRFIDProtocolEM4100, v, e);
        test_format(LFRFIDProtocolEM410032, v, e);
        test_format(LFRFIDProtocolEM410016, v, e);
        test_format(LFRFIDProtocolH10301, v, e);
        test_format(LFRFIDProtocolAwid, v, e);
        test_format(LFRFIDProtocolPyramid, v, e);
    }
    {
        const uint64_t v[] = {200, 4321, 1};
        const char* const e[] = {"200", "4321", NULL};
        test_format(LFRFIDProtocolIOProxXSF, v, e);
    }
    {
        const uint64_t v[] = {4660, 1193046, 3, 2};
        const char* const e[] = {"4660", "1193046", NULL};
        test_format(LFRFIDProtocolGallagher, v, e);
    }
    {
        const uint64_t v[] = {1234, 98765};
        const char* const e[] = {"H10304: FC 1234 Card 98765", NULL};
        test_format(LFRFID_MANUAL_FORMAT_HID + 0, v, e);
    }
    {
        const uint64_t v[] = {123456789};
        const char* const e[] = {"H10302: Card 123456789", NULL};
        test_format(LFRFID_MANUAL_FORMAT_HID + 1, v, e);
    }
    {
        const uint64_t v[] = {42, 1000000};
        const char* const e[] = {"S10401: FC 42 Card 1000000", NULL};
        test_format(LFRFID_MANUAL_FORMAT_HID + 2, v, e);
    }
    {
        const uint64_t v[] = {500, 40000};
        const char* const e[] = {"H10306: FC 500 Card 40000", NULL};
        test_format(LFRFID_MANUAL_FORMAT_HID + 3, v, e);
    }
    {
        const uint64_t v[] = {2047, 654321};
        const char* const e[] = {"Corp1000-35: FC 2047 Card 654321", NULL};
        test_format(LFRFID_MANUAL_FORMAT_HID + 4, v, e);
    }
    {
        const uint64_t v[] = {151234, 400000};
        const char* const e[] = {"C10106: 151234 400000", NULL};
        test_format(LFRFID_MANUAL_FORMAT_CASI, v, e);
    }

    // protocols without a FC/card layout take hex only
    CHECK(lfrfid_manual_format_fields_count(LFRFIDProtocolIndala26) == 0, "Indala26 hex only");
    CHECK(
        lfrfid_manual_format_protocol(LFRFID_MANUAL_FORMAT_CASI + 1) == PROTOCOL_NO,
        "unknown id has no protocol");

    test_hid_roundtrips();
    test_hid_rejects_bad_parity();
    test_casi();
    test_casi_rejects_invalid_frames();
    test_pyramid_parity();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
