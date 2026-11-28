#include <furi.h>
#include <furi_hal.h>
#include "../test.h" // IWYU pragma: keep
#include <lib/subghz/receiver.h>
#include <lib/subghz/transmitter.h>
#include <lib/subghz/subghz_keystore.h>
#include <lib/subghz/subghz_file_encoder_worker.h>
#include <lib/subghz/protocols/protocol_items.h>
#include <flipper_format/flipper_format_i.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/devices/cc1101_configs.h>
#include <flipper_application/plugins/plugin_manager.h>
#include <loader/firmware_api/firmware_api.h>
#include <string.h>

#define TAG "SubGhzTest"

#define KEYSTORE_DIR_NAME       EXT_PATH("subghz/assets/keeloq_mfcodes")
#define CAME_ATOMO_DIR_NAME     EXT_PATH("subghz/assets/came_atomo")
#define NICE_FLOR_S_DIR_NAME    EXT_PATH("subghz/assets/nice_flor_s")
#define ALUTECH_AT_4N_DIR_NAME  EXT_PATH("subghz/assets/alutech_at_4n")
#define TEST_RANDOM_DIR_NAME    EXT_PATH("unit_tests/subghz/test_random_raw.sub")
#define TEST_RANDOM_COUNT_PARSE 328
#define TEST_TIMEOUT            10000

static SubGhzEnvironment* environment_handler;
static SubGhzReceiver* receiver_handler;
//static SubGhzTransmitter* transmitter_handler;
static SubGhzFileEncoderWorker* file_worker_encoder_handler;
static uint16_t subghz_test_decoder_count = 0;

static void subghz_test_rx_callback(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    UNUSED(receiver);
    UNUSED(context);
    FuriString* text;
    text = furi_string_alloc();
    subghz_protocol_decoder_base_get_string(decoder_base, text);
    subghz_receiver_reset(receiver_handler);
    FURI_LOG_T(TAG, "\r\n%s", furi_string_get_cstr(text));
    furi_string_free(text);
    subghz_test_decoder_count++;
}

static void subghz_test_init(void) {
    environment_handler = subghz_environment_alloc();
    subghz_environment_set_nice_flor_s_rainbow_table_file_name(
        environment_handler, NICE_FLOR_S_DIR_NAME);
    subghz_environment_set_alutech_at_4n_rainbow_table_file_name(
        environment_handler, ALUTECH_AT_4N_DIR_NAME);
    subghz_environment_set_protocol_registry(
        environment_handler, (void*)&subghz_protocol_registry);

    subghz_devices_init();

    receiver_handler = subghz_receiver_alloc_init(environment_handler);
    subghz_receiver_set_filter(receiver_handler, SubGhzProtocolFlag_Decodable);
    subghz_receiver_set_rx_callback(receiver_handler, subghz_test_rx_callback, NULL);
}

static void subghz_test_deinit(void) {
    subghz_devices_deinit();
    subghz_receiver_free(receiver_handler);
    subghz_environment_free(environment_handler);
}

static bool subghz_decoder_test(const char* path, const char* name_decoder) {
    subghz_test_decoder_count = 0;
    uint32_t test_start = furi_get_tick();

    SubGhzProtocolDecoderBase* decoder =
        subghz_receiver_search_decoder_base_by_name(receiver_handler, name_decoder);

    if(decoder) {
        file_worker_encoder_handler = subghz_file_encoder_worker_alloc();
        if(subghz_file_encoder_worker_start(file_worker_encoder_handler, path, NULL)) {
            // the worker needs a file in order to open and read part of the file
            furi_delay_ms(100);

            LevelDuration level_duration;
            while(furi_get_tick() - test_start < TEST_TIMEOUT) {
                level_duration =
                    subghz_file_encoder_worker_get_level_duration(file_worker_encoder_handler);
                if(!level_duration_is_reset(level_duration)) {
                    bool level = level_duration_get_level(level_duration);
                    uint32_t duration = level_duration_get_duration(level_duration);
                    // Yield, to load data inside the worker
                    furi_thread_yield();
                    decoder->protocol->decoder->feed(decoder, level, duration);
                } else {
                    break;
                }
            }
            furi_delay_ms(10);
        }
        if(subghz_file_encoder_worker_is_running(file_worker_encoder_handler)) {
            subghz_file_encoder_worker_stop(file_worker_encoder_handler);
        }
        subghz_file_encoder_worker_free(file_worker_encoder_handler);
    }
    FURI_LOG_T(TAG, "Decoder count parse %d", subghz_test_decoder_count);
    if(furi_get_tick() - test_start > TEST_TIMEOUT) {
        printf("Test decoder %s ERROR TimeOut\r\n", name_decoder);
        return false;
    } else {
        return subghz_test_decoder_count ? true : false;
    }
}

static bool subghz_decode_random_test(const char* path) {
    subghz_test_decoder_count = 0;
    subghz_receiver_reset(receiver_handler);
    uint32_t test_start = furi_get_tick();

    file_worker_encoder_handler = subghz_file_encoder_worker_alloc();
    if(subghz_file_encoder_worker_start(file_worker_encoder_handler, path, NULL)) {
        // the worker needs a file in order to open and read part of the file
        furi_delay_ms(100);

        LevelDuration level_duration;
        while(furi_get_tick() - test_start < TEST_TIMEOUT * 10) {
            level_duration =
                subghz_file_encoder_worker_get_level_duration(file_worker_encoder_handler);
            if(!level_duration_is_reset(level_duration)) {
                bool level = level_duration_get_level(level_duration);
                uint32_t duration = level_duration_get_duration(level_duration);
                // Yield, to load data inside the worker
                furi_thread_yield();
                subghz_receiver_decode(receiver_handler, level, duration);
            } else {
                break;
            }
        }
        furi_delay_ms(10);
        if(subghz_file_encoder_worker_is_running(file_worker_encoder_handler)) {
            subghz_file_encoder_worker_stop(file_worker_encoder_handler);
        }
        subghz_file_encoder_worker_free(file_worker_encoder_handler);
    }
    FURI_LOG_D(TAG, "Decoder count parse %d", subghz_test_decoder_count);
    if(furi_get_tick() - test_start > TEST_TIMEOUT * 10) {
        printf("Random test ERROR TimeOut\r\n");
        return false;
    } else if(subghz_test_decoder_count == TEST_RANDOM_COUNT_PARSE) {
        return true;
    } else {
        return false;
    }
}

static bool subghz_encoder_test(const char* path) {
    subghz_test_decoder_count = 0;
    uint32_t test_start = furi_get_tick();
    FuriString* temp_str;
    temp_str = furi_string_alloc();
    bool file_load = false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* fff_data_file = flipper_format_file_alloc(storage);

    do {
        if(!flipper_format_file_open_existing(fff_data_file, path)) {
            FURI_LOG_E(TAG, "Error open file %s", path);
            break;
        }

        if(!flipper_format_read_string(fff_data_file, "Preset", temp_str)) {
            FURI_LOG_E(TAG, "Missing Preset");
            break;
        }

        if(!flipper_format_read_string(fff_data_file, "Protocol", temp_str)) {
            FURI_LOG_E(TAG, "Missing Protocol");
            break;
        }
        file_load = true;
    } while(false);
    if(file_load) {
        SubGhzTransmitter* transmitter =
            subghz_transmitter_alloc_init(environment_handler, furi_string_get_cstr(temp_str));
        subghz_transmitter_deserialize(transmitter, fff_data_file);

        SubGhzProtocolDecoderBase* decoder = subghz_receiver_search_decoder_base_by_name(
            receiver_handler, furi_string_get_cstr(temp_str));

        if(decoder) {
            LevelDuration level_duration;
            while(furi_get_tick() - test_start < TEST_TIMEOUT) {
                level_duration = subghz_transmitter_yield(transmitter);
                if(!level_duration_is_reset(level_duration)) {
                    bool level = level_duration_get_level(level_duration);
                    uint32_t duration = level_duration_get_duration(level_duration);
                    decoder->protocol->decoder->feed(decoder, level, duration);
                } else {
                    break;
                }
            }
            furi_delay_ms(10);
        }
        subghz_transmitter_free(transmitter);
    }
    flipper_format_free(fff_data_file);
    FURI_LOG_T(TAG, "Decoder count parse %d", subghz_test_decoder_count);
    if(furi_get_tick() - test_start > TEST_TIMEOUT) {
        printf("Test encoder %s ERROR TimeOut\r\n", furi_string_get_cstr(temp_str));
        subghz_test_decoder_count = 0;
    }
    furi_string_free(temp_str);

    return subghz_test_decoder_count ? true : false;
}

MU_TEST(subghz_keystore_test) {
    mu_assert(
        subghz_environment_load_keystore(environment_handler, KEYSTORE_DIR_NAME),
        "Test keystore error");
}

typedef enum {
    SubGhzHalAsyncTxTestTypeNormal,
    SubGhzHalAsyncTxTestTypeInvalidStart,
    SubGhzHalAsyncTxTestTypeInvalidMid,
    SubGhzHalAsyncTxTestTypeInvalidEnd,
    SubGhzHalAsyncTxTestTypeResetStart,
    SubGhzHalAsyncTxTestTypeResetMid,
    SubGhzHalAsyncTxTestTypeResetEnd,
} SubGhzHalAsyncTxTestType;

typedef struct {
    SubGhzHalAsyncTxTestType type;
    size_t pos;
} SubGhzHalAsyncTxTest;

#define SUBGHZ_HAL_TEST_DURATION 3

static LevelDuration subghz_hal_async_tx_test_yield(void* context) {
    SubGhzHalAsyncTxTest* test = context;
    bool is_odd = test->pos % 2;

    if(test->type == SubGhzHalAsyncTxTestTypeNormal) {
        if(test->pos < FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_FULL * 8) {
            test->pos++;
            return level_duration_make(is_odd, SUBGHZ_HAL_TEST_DURATION);
        } else if(test->pos == FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_FULL * 8) {
            test->pos++;
            return level_duration_reset();
        } else {
            furi_crash("Yield after reset");
        }
    } else if(test->type == SubGhzHalAsyncTxTestTypeInvalidStart) {
        if(test->pos == 0) {
            test->pos++;
            return level_duration_make(!is_odd, SUBGHZ_HAL_TEST_DURATION);
        } else if(test->pos < FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_FULL * 8) {
            test->pos++;
            return level_duration_make(is_odd, SUBGHZ_HAL_TEST_DURATION);
        } else if(test->pos == FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_FULL * 8) {
            test->pos++;
            return level_duration_reset();
        } else {
            furi_crash("Yield after reset");
        }
    } else if(test->type == SubGhzHalAsyncTxTestTypeInvalidMid) {
        if(test->pos == FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_HALF / 2) {
            test->pos++;
            return level_duration_make(!is_odd, SUBGHZ_HAL_TEST_DURATION);
        } else if(test->pos < FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_FULL * 8) {
            test->pos++;
            return level_duration_make(is_odd, SUBGHZ_HAL_TEST_DURATION);
        } else if(test->pos == FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_FULL * 8) {
            test->pos++;
            return level_duration_reset();
        } else {
            furi_crash("Yield after reset");
        }
    } else if(test->type == SubGhzHalAsyncTxTestTypeInvalidEnd) {
        if(test->pos == FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_FULL - 1) {
            test->pos++;
            return level_duration_make(!is_odd, SUBGHZ_HAL_TEST_DURATION);
        } else if(test->pos < FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_FULL * 8) {
            test->pos++;
            return level_duration_make(is_odd, SUBGHZ_HAL_TEST_DURATION);
        } else if(test->pos == FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_FULL * 8) {
            test->pos++;
            return level_duration_reset();
        } else {
            furi_crash("Yield after reset");
        }
    } else if(test->type == SubGhzHalAsyncTxTestTypeResetStart) {
        if(test->pos == 0) {
            test->pos++;
            return level_duration_reset();
        } else {
            furi_crash("Yield after reset");
        }
    } else if(test->type == SubGhzHalAsyncTxTestTypeResetMid) {
        if(test->pos < FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_HALF / 2) {
            test->pos++;
            return level_duration_make(is_odd, SUBGHZ_HAL_TEST_DURATION);
        } else if(test->pos == FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_HALF / 2) {
            test->pos++;
            return level_duration_reset();
        } else {
            furi_crash("Yield after reset");
        }
    } else if(test->type == SubGhzHalAsyncTxTestTypeResetEnd) {
        if(test->pos < FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_FULL) {
            test->pos++;
            return level_duration_make(is_odd, SUBGHZ_HAL_TEST_DURATION);
        } else if(test->pos == FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_FULL) {
            test->pos++;
            return level_duration_reset();
        } else {
            furi_crash("Yield after reset");
        }
    } else {
        furi_crash();
    }
}

bool subghz_hal_async_tx_test_run(SubGhzHalAsyncTxTestType type) {
    SubGhzHalAsyncTxTest test = {0};
    test.type = type;
    furi_hal_subghz_reset();
    furi_hal_subghz_load_custom_preset(subghz_device_cc1101_preset_ook_650khz_async_regs);
    furi_hal_subghz_set_frequency_and_path(433920000);

    if(!furi_hal_subghz_start_async_tx(subghz_hal_async_tx_test_yield, &test)) {
        mu_warn("SubGHZ transmission is prohibited");
        return false;
    }

    FuriHalCortexTimer timer = furi_hal_cortex_timer_get(30000000);

    while(!furi_hal_subghz_is_async_tx_complete()) {
        if(furi_hal_cortex_timer_is_expired(timer)) {
            furi_hal_subghz_stop_async_tx();
            furi_hal_subghz_sleep();
            return false;
        }
        furi_delay_ms(10);
    }
    furi_hal_subghz_stop_async_tx();
    furi_hal_subghz_sleep();

    return true;
}

MU_TEST(subghz_hal_async_tx_test) {
    mu_assert(
        subghz_hal_async_tx_test_run(SubGhzHalAsyncTxTestTypeNormal),
        "Test furi_hal_async_tx normal");
    mu_assert(
        subghz_hal_async_tx_test_run(SubGhzHalAsyncTxTestTypeInvalidStart),
        "Test furi_hal_async_tx invalid start");
    mu_assert(
        subghz_hal_async_tx_test_run(SubGhzHalAsyncTxTestTypeInvalidMid),
        "Test furi_hal_async_tx invalid mid");
    mu_assert(
        subghz_hal_async_tx_test_run(SubGhzHalAsyncTxTestTypeInvalidEnd),
        "Test furi_hal_async_tx invalid end");
    mu_assert(
        subghz_hal_async_tx_test_run(SubGhzHalAsyncTxTestTypeResetStart),
        "Test furi_hal_async_tx reset start");
    mu_assert(
        subghz_hal_async_tx_test_run(SubGhzHalAsyncTxTestTypeResetMid),
        "Test furi_hal_async_tx reset mid");
    mu_assert(
        subghz_hal_async_tx_test_run(SubGhzHalAsyncTxTestTypeResetEnd),
        "Test furi_hal_async_tx reset end");
}

//test decoders
MU_TEST(subghz_decoder_came_atomo_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/came_atomo_raw.sub"), SUBGHZ_PROTOCOL_CAME_ATOMO_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_CAME_ATOMO_NAME " error\r\n");
}

MU_TEST(subghz_decoder_came_test) {
    mu_assert(
        subghz_decoder_test(EXT_PATH("unit_tests/subghz/came_raw.sub"), SUBGHZ_PROTOCOL_CAME_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_CAME_NAME " error\r\n");
}

MU_TEST(subghz_decoder_came_twee_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/came_twee_raw.sub"), SUBGHZ_PROTOCOL_CAME_TWEE_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_CAME_TWEE_NAME " error\r\n");
}

MU_TEST(subghz_decoder_faac_slh_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/faac_slh_raw.sub"), SUBGHZ_PROTOCOL_FAAC_SLH_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_FAAC_SLH_NAME " error\r\n");
}

MU_TEST(subghz_decoder_gate_tx_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/gate_tx_raw.sub"), SUBGHZ_PROTOCOL_GATE_TX_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_GATE_TX_NAME " error\r\n");
}

MU_TEST(subghz_decoder_hormann_hsm_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/hormann_hsm_raw.sub"), SUBGHZ_PROTOCOL_HORMANN_HSM_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_HORMANN_HSM_NAME " error\r\n");
}

MU_TEST(subghz_decoder_ido_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/ido_117_111_raw.sub"), SUBGHZ_PROTOCOL_IDO_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_IDO_NAME " error\r\n");
}

MU_TEST(subghz_decoder_keeloq_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/doorhan_raw.sub"), SUBGHZ_PROTOCOL_KEELOQ_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_KEELOQ_NAME " error\r\n");
}

MU_TEST(subghz_decoder_nero_radio_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/nero_radio_raw.sub"), SUBGHZ_PROTOCOL_NERO_RADIO_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_NERO_RADIO_NAME " error\r\n");
}

MU_TEST(subghz_decoder_nero_sketch_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/nero_sketch_raw.sub"), SUBGHZ_PROTOCOL_NERO_SKETCH_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_NERO_SKETCH_NAME " error\r\n");
}

MU_TEST(subghz_decoder_nice_flo_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/nice_flo_raw.sub"), SUBGHZ_PROTOCOL_NICE_FLO_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_NICE_FLO_NAME " error\r\n");
}

MU_TEST(subghz_decoder_nice_flor_s_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/nice_flor_s_raw.sub"), SUBGHZ_PROTOCOL_NICE_FLOR_S_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_NICE_FLOR_S_NAME " error\r\n");
}

MU_TEST(subghz_decoder_princeton_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/Princeton_raw.sub"), SUBGHZ_PROTOCOL_PRINCETON_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_PRINCETON_NAME " error\r\n");
}

MU_TEST(subghz_decoder_somfy_keytis_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/Somfy_keytis_raw.sub"), SUBGHZ_PROTOCOL_SOMFY_KEYTIS_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_SOMFY_KEYTIS_NAME " error\r\n");
}

MU_TEST(subghz_decoder_somfy_telis_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/somfy_telis_raw.sub"), SUBGHZ_PROTOCOL_SOMFY_TELIS_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_SOMFY_TELIS_NAME " error\r\n");
}

MU_TEST(subghz_decoder_linear_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/linear_raw.sub"), SUBGHZ_PROTOCOL_LINEAR_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_LINEAR_NAME " error\r\n");
}

MU_TEST(subghz_decoder_linear_delta3_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/linear_delta3_raw.sub"),
            SUBGHZ_PROTOCOL_LINEAR_DELTA3_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_LINEAR_DELTA3_NAME " error\r\n");
}

MU_TEST(subghz_decoder_megacode_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/megacode_raw.sub"), SUBGHZ_PROTOCOL_MEGACODE_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_MEGACODE_NAME " error\r\n");
}

MU_TEST(subghz_decoder_secplus_v1_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/security_pls_1_0_raw.sub"),
            SUBGHZ_PROTOCOL_SECPLUS_V1_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_SECPLUS_V1_NAME " error\r\n");
}

MU_TEST(subghz_decoder_secplus_v2_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/security_pls_2_0_raw.sub"),
            SUBGHZ_PROTOCOL_SECPLUS_V2_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_SECPLUS_V2_NAME " error\r\n");
}

MU_TEST(subghz_decoder_holtek_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/holtek_raw.sub"), SUBGHZ_PROTOCOL_HOLTEK_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_HOLTEK_NAME " error\r\n");
}

MU_TEST(subghz_decoder_power_smart_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/power_smart_raw.sub"), SUBGHZ_PROTOCOL_POWER_SMART_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_POWER_SMART_NAME " error\r\n");
}

MU_TEST(subghz_decoder_marantec_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/marantec_raw.sub"), SUBGHZ_PROTOCOL_MARANTEC_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_MARANTEC_NAME " error\r\n");
}

MU_TEST(subghz_decoder_bett_test) {
    mu_assert(
        subghz_decoder_test(EXT_PATH("unit_tests/subghz/bett_raw.sub"), SUBGHZ_PROTOCOL_BETT_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_BETT_NAME " error\r\n");
}

MU_TEST(subghz_decoder_doitrand_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/doitrand_raw.sub"), SUBGHZ_PROTOCOL_DOITRAND_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_DOITRAND_NAME " error\r\n");
}

MU_TEST(subghz_decoder_phoenix_v2_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/phoenix_v2_raw.sub"), SUBGHZ_PROTOCOL_PHOENIX_V2_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_PHOENIX_V2_NAME " error\r\n");
}

MU_TEST(subghz_decoder_honeywell_wdb_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/honeywell_wdb_raw.sub"),
            SUBGHZ_PROTOCOL_HONEYWELL_WDB_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_HONEYWELL_WDB_NAME " error\r\n");
}

MU_TEST(subghz_decoder_magellan_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/magellan_raw.sub"), SUBGHZ_PROTOCOL_MAGELLAN_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_MAGELLAN_NAME " error\r\n");
}

MU_TEST(subghz_decoder_intertechno_v3_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/intertechno_v3_raw.sub"),
            SUBGHZ_PROTOCOL_INTERTECHNO_V3_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_INTERTECHNO_V3_NAME " error\r\n");
}

MU_TEST(subghz_decoder_clemsa_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/clemsa_raw.sub"), SUBGHZ_PROTOCOL_CLEMSA_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_CLEMSA_NAME " error\r\n");
}

MU_TEST(subghz_decoder_ansonic_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/ansonic_raw.sub"), SUBGHZ_PROTOCOL_ANSONIC_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_ANSONIC_NAME " error\r\n");
}

MU_TEST(subghz_decoder_smc5326_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/smc5326_raw.sub"), SUBGHZ_PROTOCOL_SMC5326_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_SMC5326_NAME " error\r\n");
}

MU_TEST(subghz_decoder_holtek_ht12x_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/holtek_ht12x_raw.sub"), SUBGHZ_PROTOCOL_HOLTEK_HT12X_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_HOLTEK_HT12X_NAME " error\r\n");
}

MU_TEST(subghz_decoder_dooya_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/dooya_raw.sub"), SUBGHZ_PROTOCOL_DOOYA_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_DOOYA_NAME " error\r\n");
}

MU_TEST(subghz_decoder_alutech_at_4n_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/alutech_at_4n_raw.sub"),
            SUBGHZ_PROTOCOL_ALUTECH_AT_4N_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_ALUTECH_AT_4N_NAME " error\r\n");
}

MU_TEST(subghz_decoder_nice_one_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/nice_one_raw.sub"), SUBGHZ_PROTOCOL_NICE_FLOR_S_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_NICE_FLOR_S_NAME " error\r\n");
}

MU_TEST(subghz_decoder_kinggates_stylo4k_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/kinggates_stylo4k_raw.sub"),
            SUBGHZ_PROTOCOL_KINGGATES_STYLO_4K_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_KINGGATES_STYLO_4K_NAME " error\r\n");
}

MU_TEST(subghz_decoder_mastercode_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/mastercode_raw.sub"), SUBGHZ_PROTOCOL_MASTERCODE_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_MASTERCODE_NAME " error\r\n");
}

MU_TEST(subghz_decoder_dickert_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/dickert_raw.sub"), SUBGHZ_PROTOCOL_DICKERT_MAHS_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_DICKERT_MAHS_NAME " error\r\n");
}

MU_TEST(subghz_decoder_legrand_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/legrand_raw.sub"), SUBGHZ_PROTOCOL_LEGRAND_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_LEGRAND_NAME " error\r\n");
}

MU_TEST(subghz_decoder_marantec24_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/marantec24_raw.sub"), SUBGHZ_PROTOCOL_MARANTEC24_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_MARANTEC24_NAME " error\r\n");
}

MU_TEST(subghz_decoder_roger_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/roger_raw.sub"), SUBGHZ_PROTOCOL_ROGER_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_ROGER_NAME " error\r\n");
}

MU_TEST(subghz_decoder_feron_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/feron_raw.sub"), SUBGHZ_PROTOCOL_FERON_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_FERON_NAME " error\r\n");
}

MU_TEST(subghz_decoder_gangqi_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/gangqi_raw.sub"), SUBGHZ_PROTOCOL_GANGQI_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_GANGQI_NAME " error\r\n");
}

MU_TEST(subghz_decoder_hollarm_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/hollarm_raw.sub"), SUBGHZ_PROTOCOL_HOLLARM_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_HOLLARM_NAME " error\r\n");
}

MU_TEST(subghz_decoder_reversrb2_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/revers_rb2_raw.sub"), SUBGHZ_PROTOCOL_REVERSRB2_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_REVERSRB2_NAME " error\r\n");
}

MU_TEST(subghz_decoder_hay21_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/hay21_raw.sub"), SUBGHZ_PROTOCOL_HAY21_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_HAY21_NAME " error\r\n");
}

//test encoders
MU_TEST(subghz_encoder_princeton_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/princeton.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_PRINCETON_NAME " error\r\n");
}

MU_TEST(subghz_encoder_came_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/came.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_CAME_NAME " error\r\n");
}

MU_TEST(subghz_encoder_came_twee_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/came_twee.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_CAME_TWEE_NAME " error\r\n");
}

MU_TEST(subghz_encoder_gate_tx_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/gate_tx.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_GATE_TX_NAME " error\r\n");
}

MU_TEST(subghz_encoder_nice_flo_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/nice_flo.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_NICE_FLO_NAME " error\r\n");
}

MU_TEST(subghz_encoder_keeloq_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/doorhan.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_KEELOQ_NAME " error\r\n");
}

MU_TEST(subghz_encoder_linear_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/linear.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_LINEAR_NAME " error\r\n");
}

MU_TEST(subghz_encoder_linear_delta3_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/linear_delta3.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_LINEAR_DELTA3_NAME " error\r\n");
}

MU_TEST(subghz_encoder_megacode_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/megacode.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_MEGACODE_NAME " error\r\n");
}

MU_TEST(subghz_encoder_holtek_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/holtek.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_HOLTEK_NAME " error\r\n");
}

MU_TEST(subghz_encoder_secplus_v1_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/security_pls_1_0.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_SECPLUS_V1_NAME " error\r\n");
}

MU_TEST(subghz_encoder_secplus_v2_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/security_pls_2_0.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_SECPLUS_V2_NAME " error\r\n");
}

MU_TEST(subghz_encoder_power_smart_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/power_smart.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_POWER_SMART_NAME " error\r\n");
}

MU_TEST(subghz_encoder_marantec_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/marantec.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_MARANTEC_NAME " error\r\n");
}

MU_TEST(subghz_encoder_bett_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/bett.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_BETT_NAME " error\r\n");
}

MU_TEST(subghz_encoder_doitrand_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/doitrand.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_DOITRAND_NAME " error\r\n");
}

MU_TEST(subghz_encoder_phoenix_v2_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/phoenix_v2.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_PHOENIX_V2_NAME " error\r\n");
}

MU_TEST(subghz_encoder_honeywell_wdb_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/honeywell_wdb.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_HONEYWELL_WDB_NAME " error\r\n");
}

MU_TEST(subghz_encoder_magellan_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/magellan.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_MAGELLAN_NAME " error\r\n");
}

MU_TEST(subghz_encoder_intertechno_v3_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/intertechno_v3.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_INTERTECHNO_V3_NAME " error\r\n");
}

MU_TEST(subghz_encoder_clemsa_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/clemsa.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_CLEMSA_NAME " error\r\n");
}

MU_TEST(subghz_encoder_ansonic_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/ansonic.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_ANSONIC_NAME " error\r\n");
}

MU_TEST(subghz_encoder_smc5326_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/smc5326.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_SMC5326_NAME " error\r\n");
}

MU_TEST(subghz_encoder_holtek_ht12x_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/holtek_ht12x.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_HOLTEK_HT12X_NAME " error\r\n");
}

MU_TEST(subghz_encoder_dooya_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/dooya.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_DOOYA_NAME " error\r\n");
}

MU_TEST(subghz_encoder_mastercode_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/mastercode.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_MASTERCODE_NAME " error\r\n");
}

MU_TEST(subghz_encoder_dickert_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/dickert_mahs.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_DICKERT_MAHS_NAME " error\r\n");
}

MU_TEST(subghz_encoder_legrand_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/legrand.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_LEGRAND_NAME " error\r\n");
}

MU_TEST(subghz_encoder_feron_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/feron.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_FERON_NAME " error\r\n");
}

MU_TEST(subghz_encoder_gangqi_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/gangqi.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_GANGQI_NAME " error\r\n");
}

MU_TEST(subghz_encoder_hollarm_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/hollarm.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_HOLLARM_NAME " error\r\n");
}

MU_TEST(subghz_encoder_reversrb2_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/revers_rb2.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_REVERSRB2_NAME " error\r\n");
}

MU_TEST(subghz_encoder_roger_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/roger.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_ROGER_NAME " error\r\n");
}

MU_TEST(subghz_encoder_marantec24_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/marantec24.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_MARANTEC24_NAME " error\r\n");
}

/* Decoders that live in SD card plugins (applications/main/subghz/plugins). The plugins are loaded
 * the way the Sub-GHz app loads them, and all their protocols go into a receiver of their own,
 * next to the firmware ones, so the tests also cover the plugins not tripping over each other. */

#define SUBGHZ_PLUGIN_APP_ID      "subghz_protocol_plugin"
#define SUBGHZ_PLUGIN_API_VERSION 1
#define SUBGHZ_PLUGIN_COUNT       7

// Same layout as SubGhzProtocolPlugin in applications/main/subghz/plugins/subghz_protocol_plugin.h
typedef struct {
    const SubGhzProtocol* const* items;
    size_t count;
} SubGhzTestPlugin;

static const char* const subghz_test_plugin_files[SUBGHZ_PLUGIN_COUNT] = {
    EXT_PATH("apps_assets/subghz/plugins/subghz_plugin_ws1.fal"),
    EXT_PATH("apps_assets/subghz/plugins/subghz_plugin_ws2.fal"),
    EXT_PATH("apps_assets/subghz/plugins/subghz_plugin_ws3.fal"),
    EXT_PATH("apps_assets/subghz/plugins/subghz_plugin_tpms.fal"),
    EXT_PATH("apps_assets/subghz/plugins/subghz_plugin_x10.fal"),
    EXT_PATH("apps_assets/subghz/plugins/subghz_plugin_pager.fal"),
    EXT_PATH("apps_assets/subghz/plugins/subghz_plugin_bisecur.fal"),
};

typedef struct {
    PluginManager* managers[SUBGHZ_PLUGIN_COUNT];
    const SubGhzProtocol** items;
    SubGhzProtocolRegistry registry;
    SubGhzEnvironment* environment;
    SubGhzReceiver* receiver;
    // What the receiver reported
    const char* expect_name;
    uint16_t expect_count;
    uint16_t other_count;
} SubGhzTestPluginSet;

static void subghz_test_plugin_rx_callback(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    SubGhzTestPluginSet* set = context;
    if(strcmp(decoder_base->protocol->name, set->expect_name) == 0) {
        set->expect_count++;
    } else {
        set->other_count++;
    }
    subghz_receiver_reset(receiver);
}

static SubGhzTestPluginSet* subghz_test_plugin_set_alloc(void) {
    SubGhzTestPluginSet* set = malloc(sizeof(SubGhzTestPluginSet));
    memset(set, 0, sizeof(SubGhzTestPluginSet));

    size_t count = subghz_protocol_registry_count(&subghz_protocol_registry);
    const SubGhzTestPlugin* plugins[SUBGHZ_PLUGIN_COUNT] = {0};
    for(size_t i = 0; i < SUBGHZ_PLUGIN_COUNT; i++) {
        set->managers[i] = plugin_manager_alloc(
            SUBGHZ_PLUGIN_APP_ID, SUBGHZ_PLUGIN_API_VERSION, firmware_api_interface);
        PluginManagerError error =
            plugin_manager_load_single(set->managers[i], subghz_test_plugin_files[i]);
        if(error != PluginManagerErrorNone) {
            FURI_LOG_E(TAG, "Cannot load %s (error %d)", subghz_test_plugin_files[i], error);
            continue;
        }
        plugins[i] = plugin_manager_get_ep(set->managers[i], 0);
        count += plugins[i]->count;
    }

    set->items = malloc(count * sizeof(SubGhzProtocol*));
    size_t n = 0;
    for(size_t i = 0; i < subghz_protocol_registry_count(&subghz_protocol_registry); i++) {
        set->items[n++] = subghz_protocol_registry_get_by_index(&subghz_protocol_registry, i);
    }
    for(size_t i = 0; i < SUBGHZ_PLUGIN_COUNT; i++) {
        if(!plugins[i]) continue;
        for(size_t j = 0; j < plugins[i]->count; j++) {
            set->items[n++] = plugins[i]->items[j];
        }
    }
    const SubGhzProtocolRegistry registry = {.items = set->items, .size = n};
    memcpy(&set->registry, &registry, sizeof(registry));

    set->environment = subghz_environment_alloc();
    subghz_environment_set_protocol_registry(set->environment, &set->registry);
    set->receiver = subghz_receiver_alloc_init(set->environment);
    subghz_receiver_set_filter(set->receiver, SubGhzProtocolFlag_Decodable);
    subghz_receiver_set_rx_callback(set->receiver, subghz_test_plugin_rx_callback, set);
    return set;
}

static void subghz_test_plugin_set_free(SubGhzTestPluginSet* set) {
    // The receiver first: its decoders run code from the plugins
    subghz_receiver_free(set->receiver);
    subghz_environment_free(set->environment);
    for(size_t i = 0; i < SUBGHZ_PLUGIN_COUNT; i++) {
        plugin_manager_free(set->managers[i]);
    }
    free(set->items);
    free(set);
}

static bool subghz_test_plugin_has_protocol(SubGhzTestPluginSet* set, const char* name) {
    return subghz_receiver_search_decoder_base_by_name(set->receiver, name) != NULL;
}

// Feeds a RAW capture to every decoder and reports whether `name` decoded it
static bool
    subghz_test_plugin_decode(SubGhzTestPluginSet* set, const char* path, const char* name) {
    set->expect_name = name;
    set->expect_count = 0;
    set->other_count = 0;
    subghz_receiver_reset(set->receiver);
    uint32_t test_start = furi_get_tick();

    SubGhzFileEncoderWorker* worker = subghz_file_encoder_worker_alloc();
    if(subghz_file_encoder_worker_start(worker, path, NULL)) {
        // the worker needs a file in order to open and read part of the file
        furi_delay_ms(100);
        while(furi_get_tick() - test_start < TEST_TIMEOUT) {
            LevelDuration level_duration = subghz_file_encoder_worker_get_level_duration(worker);
            if(level_duration_is_reset(level_duration)) break;
            // Yield, to load data inside the worker
            furi_thread_yield();
            subghz_receiver_decode(
                set->receiver,
                level_duration_get_level(level_duration),
                level_duration_get_duration(level_duration));
        }
        furi_delay_ms(10);
    }
    if(subghz_file_encoder_worker_is_running(worker)) {
        subghz_file_encoder_worker_stop(worker);
    }
    subghz_file_encoder_worker_free(worker);
    FURI_LOG_D(TAG, "%s: %d decoded, %d by others", name, set->expect_count, set->other_count);
    return set->expect_count > 0;
}

MU_TEST(subghz_plugins_decoders_test) {
    SubGhzTestPluginSet* set = subghz_test_plugin_set_alloc();

    // Every one of the 29 decoders is there, plus the firmware ones
    static const char* const names[] = {
        "inFactory-TH",  "ThermoPRO-TX4",  "Nexus-TH",    "GT-WT02",         "GT-WT03",
        "Acurite-606TX", "Acurite-609TXC", "Acurite-986", "Bresser-3CH",     "LaCrosse_TX",
        "TX141THBv2",    "Oregon2",        "Oregon3",     "Acurite 592TXR",  "Ambient_Weather",
        "Solight TE44",  "Auriol HG06061", "Oregon-v1",   "TX8300",          "Wendox W6726",
        "Auriol AHFL",   "Kedsum-TH",      "EMOS E601x",  "Acurite 5n1",     "Vauno-EN8822C",
        "Schrader GG4",  "POCSAG",         "X10",         "Hormann BiSecur",
    };
    for(size_t i = 0; i < COUNT_OF(names); i++) {
        mu_assert(subghz_test_plugin_has_protocol(set, names[i]), names[i]);
    }
    mu_assert(subghz_test_plugin_has_protocol(set, "Princeton"), "firmware protocols are missing");

    mu_check(subghz_test_plugin_decode(
        set, EXT_PATH("unit_tests/subghz/bresser_3ch_raw.sub"), "Bresser-3CH"));
    mu_check(subghz_test_plugin_decode(
        set, EXT_PATH("unit_tests/subghz/bresser_3ch_v0_raw.sub"), "Bresser-3CH"));
    mu_check(subghz_test_plugin_decode(
        set, EXT_PATH("unit_tests/subghz/solight_te44_raw.sub"), "Solight TE44"));
    mu_check(subghz_test_plugin_decode(
        set, EXT_PATH("unit_tests/subghz/vauno_en8822c_raw.sub"), "Vauno-EN8822C"));
    mu_check(subghz_test_plugin_decode(
        set, EXT_PATH("unit_tests/subghz/acurite_592txr.sub"), "Acurite 592TXR"));
    mu_check(subghz_test_plugin_decode(
        set, EXT_PATH("unit_tests/subghz/hormann_bisecur_1C84A55B_raw.sub"), "Hormann BiSecur"));

    subghz_test_plugin_set_free(set);
}

MU_TEST(subghz_random_test) {
    mu_assert(subghz_decode_random_test(TEST_RANDOM_DIR_NAME), "Random test error\r\n");
}

MU_TEST_SUITE(subghz) {
    subghz_test_init();
    MU_RUN_TEST(subghz_keystore_test);

    MU_RUN_TEST(subghz_hal_async_tx_test);

    MU_RUN_TEST(subghz_decoder_came_atomo_test);
    MU_RUN_TEST(subghz_decoder_came_test);
    MU_RUN_TEST(subghz_decoder_came_twee_test);
    MU_RUN_TEST(subghz_decoder_faac_slh_test);
    MU_RUN_TEST(subghz_decoder_gate_tx_test);
    MU_RUN_TEST(subghz_decoder_hormann_hsm_test);
    MU_RUN_TEST(subghz_decoder_ido_test);
    MU_RUN_TEST(subghz_decoder_keeloq_test);
    MU_RUN_TEST(subghz_decoder_nero_radio_test);
    MU_RUN_TEST(subghz_decoder_nero_sketch_test);
    MU_RUN_TEST(subghz_decoder_nice_flo_test);
    MU_RUN_TEST(subghz_decoder_nice_flor_s_test);
    MU_RUN_TEST(subghz_decoder_princeton_test);
    MU_RUN_TEST(subghz_decoder_somfy_keytis_test);
    MU_RUN_TEST(subghz_decoder_somfy_telis_test);
    MU_RUN_TEST(subghz_decoder_linear_test);
    MU_RUN_TEST(subghz_decoder_linear_delta3_test);
    MU_RUN_TEST(subghz_decoder_megacode_test);
    MU_RUN_TEST(subghz_decoder_secplus_v1_test);
    MU_RUN_TEST(subghz_decoder_secplus_v2_test);
    MU_RUN_TEST(subghz_decoder_holtek_test);
    MU_RUN_TEST(subghz_decoder_power_smart_test);
    MU_RUN_TEST(subghz_decoder_marantec_test);
    MU_RUN_TEST(subghz_decoder_bett_test);
    MU_RUN_TEST(subghz_decoder_doitrand_test);
    MU_RUN_TEST(subghz_decoder_phoenix_v2_test);
    MU_RUN_TEST(subghz_decoder_honeywell_wdb_test);
    MU_RUN_TEST(subghz_decoder_magellan_test);
    MU_RUN_TEST(subghz_decoder_intertechno_v3_test);
    MU_RUN_TEST(subghz_decoder_clemsa_test);
    MU_RUN_TEST(subghz_decoder_ansonic_test);
    MU_RUN_TEST(subghz_decoder_smc5326_test);
    MU_RUN_TEST(subghz_decoder_holtek_ht12x_test);
    MU_RUN_TEST(subghz_decoder_dooya_test);
    MU_RUN_TEST(subghz_decoder_alutech_at_4n_test);
    MU_RUN_TEST(subghz_decoder_nice_one_test);
    MU_RUN_TEST(subghz_decoder_kinggates_stylo4k_test);
    MU_RUN_TEST(subghz_decoder_mastercode_test);
    MU_RUN_TEST(subghz_decoder_dickert_test);
    MU_RUN_TEST(subghz_decoder_roger_test);
    MU_RUN_TEST(subghz_decoder_hollarm_test);
    MU_RUN_TEST(subghz_decoder_reversrb2_test);
    MU_RUN_TEST(subghz_decoder_gangqi_test);
    MU_RUN_TEST(subghz_decoder_hay21_test);
    MU_RUN_TEST(subghz_decoder_feron_test);
    MU_RUN_TEST(subghz_decoder_legrand_test);
    MU_RUN_TEST(subghz_decoder_marantec24_test);
    // Weather station, TPMS, POCSAG, X10 and BiSecur decoders: subghz_plugins_decoders_test

    MU_RUN_TEST(subghz_encoder_princeton_test);
    MU_RUN_TEST(subghz_encoder_came_test);
    MU_RUN_TEST(subghz_encoder_came_twee_test);
    MU_RUN_TEST(subghz_encoder_gate_tx_test);
    MU_RUN_TEST(subghz_encoder_nice_flo_test);
    MU_RUN_TEST(subghz_encoder_keeloq_test);
    MU_RUN_TEST(subghz_encoder_linear_test);
    MU_RUN_TEST(subghz_encoder_linear_delta3_test);
    MU_RUN_TEST(subghz_encoder_megacode_test);
    MU_RUN_TEST(subghz_encoder_holtek_test);
    MU_RUN_TEST(subghz_encoder_secplus_v1_test);
    MU_RUN_TEST(subghz_encoder_secplus_v2_test);
    MU_RUN_TEST(subghz_encoder_power_smart_test);
    MU_RUN_TEST(subghz_encoder_marantec_test);
    MU_RUN_TEST(subghz_encoder_bett_test);
    MU_RUN_TEST(subghz_encoder_doitrand_test);
    MU_RUN_TEST(subghz_encoder_phoenix_v2_test);
    MU_RUN_TEST(subghz_encoder_honeywell_wdb_test);
    MU_RUN_TEST(subghz_encoder_magellan_test);
    MU_RUN_TEST(subghz_encoder_intertechno_v3_test);
    MU_RUN_TEST(subghz_encoder_clemsa_test);
    MU_RUN_TEST(subghz_encoder_ansonic_test);
    MU_RUN_TEST(subghz_encoder_smc5326_test);
    MU_RUN_TEST(subghz_encoder_holtek_ht12x_test);
    MU_RUN_TEST(subghz_encoder_dooya_test);
    MU_RUN_TEST(subghz_encoder_mastercode_test);
    MU_RUN_TEST(subghz_encoder_dickert_test);
    MU_RUN_TEST(subghz_encoder_feron_test);
    MU_RUN_TEST(subghz_encoder_roger_test);
    MU_RUN_TEST(subghz_encoder_gangqi_test);
    MU_RUN_TEST(subghz_encoder_marantec24_test);
    MU_RUN_TEST(subghz_encoder_hollarm_test);
    MU_RUN_TEST(subghz_encoder_reversrb2_test);
    MU_RUN_TEST(subghz_encoder_legrand_test);

    MU_RUN_TEST(subghz_plugins_decoders_test);
    MU_RUN_TEST(subghz_random_test);
    subghz_test_deinit();
}

int run_minunit_test_subghz(void) {
    MU_RUN_SUITE(subghz);
    return MU_EXIT_CODE;
}

TEST_API_DEFINE(run_minunit_test_subghz)
