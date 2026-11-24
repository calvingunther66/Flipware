// Hotel key card identifier.
// Based on the "hotels" parser from the official Flipper Zero firmware 1.5.1
// (flipperdevices/flipperzero-firmware, GPL-3.0).
//
// Names the lock system a MIFARE Classic hotel key card belongs to, using the
// sector keys that the normal card read has already found. It reads nothing
// itself and changes nothing on the card.
//
// Saflok cards are left to the saflok_mfc_parser plugin, which decodes them in
// full, so that this plugin never hides that more detailed result.
#include "nfc_supported_card_plugin.h"

#include <flipper_application.h>
#include <nfc/protocols/mf_classic/mf_classic.h>
#include <bit_lib.h>

#define TAG "Hotels"

typedef struct {
    const char* name;
    uint8_t sector;
    MfClassicKeyType key_type;
    uint64_t key;
} HotelLockSignature;

// Static keys of each lock system, as listed in the system dictionary
// (mf_classic_dict.nfc) and in the official firmware's hotels parser.
static const HotelLockSignature hotel_lock_signatures[] = {
    {.name = "Onity", .sector = 1, .key_type = MfClassicKeyTypeA, .key = 0x8A19D40CF2B5},
    {.name = "Onity", .sector = 1, .key_type = MfClassicKeyTypeB, .key = 0x8A19D40CF2B5},
    {.name = "VingCard", .sector = 2, .key_type = MfClassicKeyTypeB, .key = 0x0000014B5C31},
};

static bool hotels_sector_key_matches(
    const MfClassicData* data,
    uint8_t sector,
    MfClassicKeyType key_type,
    uint64_t expected) {
    if(sector >= mf_classic_get_total_sectors_num(data->type)) return false;
    if(!mf_classic_is_key_found(data, sector, key_type)) return false;

    const MfClassicSectorTrailer* sec_tr = mf_classic_get_sector_trailer_by_sector(data, sector);
    if(sec_tr == NULL) return false;

    const MfClassicKey* key = (key_type == MfClassicKeyTypeA) ? &sec_tr->key_a : &sec_tr->key_b;
    return bit_lib_bytes_to_num_be(key->data, sizeof(key->data)) == expected;
}

static bool hotels_parse(const NfcDevice* device, FuriString* parsed_data) {
    furi_assert(device);
    furi_assert(parsed_data);

    const MfClassicData* data = nfc_device_get_data(device, NfcProtocolMfClassic);
    if(data == NULL) return false;

    const char* lock_system = NULL;
    for(size_t i = 0; i < COUNT_OF(hotel_lock_signatures); i++) {
        const HotelLockSignature* sig = &hotel_lock_signatures[i];
        if(hotels_sector_key_matches(data, sig->sector, sig->key_type, sig->key)) {
            lock_system = sig->name;
            break;
        }
    }

    if(lock_system == NULL) return false;

    FURI_LOG_D(TAG, "Identified %s", lock_system);
    furi_string_printf(
        parsed_data,
        "\e#Hotel Key Card\nLock system: %s\n\nIdentified from a known sector key.",
        lock_system);
    return true;
}

/* Actual implementation of app<>plugin interface */
static const NfcSupportedCardsPlugin hotels_plugin = {
    .protocol = NfcProtocolMfClassic,
    .verify = NULL,
    .read = NULL,
    .parse = hotels_parse,
};

/* Plugin descriptor to comply with basic plugin specification */
static const FlipperAppPluginDescriptor hotels_plugin_descriptor = {
    .appid = NFC_SUPPORTED_CARD_PLUGIN_APP_ID,
    .ep_api_version = NFC_SUPPORTED_CARD_PLUGIN_API_VERSION,
    .entry_point = &hotels_plugin,
};

/* Plugin entry point - must return a pointer to const descriptor  */
const FlipperAppPluginDescriptor* hotels_plugin_ep(void) {
    return &hotels_plugin_descriptor;
}
