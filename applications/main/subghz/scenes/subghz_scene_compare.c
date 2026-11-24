#include "../subghz_i.h" // IWYU pragma: keep

#include <flipper_format/flipper_format.h>
#include <storage/storage.h>
#include <lib/subghz/protocols/raw.h>
#include <lib/subghz/protocols/bin_raw.h>

#define TAG "SubGhzSceneCompare"

typedef struct {
    SubGhzCompareCapture a;
    SubGhzCompareCapture b;
    SubGhzCompareResult result;
} SubGhzSceneCompareData;

/** Load one .sub file into a capture. On failure, err holds a short reason. */
static bool subghz_scene_compare_load(
    Storage* storage,
    const char* path,
    SubGhzCompareCapture* capture,
    FuriString* err) {
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    FuriString* temp = furi_string_alloc();
    uint32_t version = 0;
    uint32_t value = 0;
    bool ok = false;

    subghz_compare_capture_reset(capture);

    do {
        if(!flipper_format_file_open_existing(ff, path)) {
            furi_string_set(err, "Can't open file");
            break;
        }
        if(!flipper_format_read_header(ff, temp, &version)) {
            furi_string_set(err, "Not a Sub-GHz file");
            break;
        }
        bool is_raw_file = furi_string_equal_str(temp, SUBGHZ_RAW_FILE_TYPE);
        if(!is_raw_file && !furi_string_equal_str(temp, SUBGHZ_KEY_FILE_TYPE)) {
            furi_string_set(err, "Not a Sub-GHz file");
            break;
        }

        if(flipper_format_read_uint32(ff, "Frequency", &value, 1)) {
            capture->frequency = value;
        }

        flipper_format_rewind(ff);
        if(!flipper_format_read_string(ff, "Protocol", temp)) {
            furi_string_set(err, "No protocol in file");
            break;
        }
        strlcpy(capture->protocol, furi_string_get_cstr(temp), sizeof(capture->protocol));

        if(is_raw_file || furi_string_equal_str(temp, SUBGHZ_PROTOCOL_RAW_NAME)) {
            capture->kind = SubGhzCompareKindRaw;
            ok = true;
            break;
        }

        flipper_format_rewind(ff);
        if(!flipper_format_read_uint32(ff, "Bit", &value, 1) || value == 0) {
            furi_string_set(err, "No bit count in file");
            break;
        }
        uint16_t declared_bits = value > UINT16_MAX ? UINT16_MAX : (uint16_t)value;

        if(furi_string_equal_str(temp, SUBGHZ_PROTOCOL_BIN_RAW_NAME)) {
            // Compare the first Data_RAW block, the first packet BinRAW found
            flipper_format_rewind(ff);
            if(!flipper_format_read_uint32(ff, "Bit_RAW", &value, 1) || value == 0) {
                furi_string_set(err, "No BinRAW data");
                break;
            }
            uint16_t block_bits = value > UINT16_MAX ? UINT16_MAX : (uint16_t)value;
            size_t bytes = (block_bits + 7) / 8;
            if(bytes > SUBGHZ_COMPARE_MAX_BYTES) bytes = SUBGHZ_COMPARE_MAX_BYTES;
            uint8_t buffer[SUBGHZ_COMPARE_MAX_BYTES] = {0};
            if(!flipper_format_read_hex(ff, "Data_RAW", buffer, bytes)) {
                furi_string_set(err, "Bad BinRAW data");
                break;
            }
            capture->kind = SubGhzCompareKindBinRaw;
            subghz_compare_capture_set_bits(capture, buffer, bytes, block_bits);
            ok = true;
        } else {
            uint8_t key[8] = {0};
            flipper_format_rewind(ff);
            if(!flipper_format_read_hex(ff, "Key", key, sizeof(key))) {
                furi_string_set(err, "No Key in file");
                break;
            }
            capture->kind = SubGhzCompareKindKey;
            subghz_compare_capture_set_key(capture, key, declared_bits);
            ok = true;
        }
    } while(false);

    furi_string_free(temp);
    flipper_format_free(ff);
    return ok;
}

static void subghz_scene_compare_show(SubGhz* subghz, const char* path_b) {
    SubGhzSceneCompareData* data = malloc(sizeof(SubGhzSceneCompareData));
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FuriString* err = furi_string_alloc();
    FuriString* name_a = furi_string_alloc();
    FuriString* name_b = furi_string_alloc();

    path_extract_filename(subghz->file_path, name_a, true);
    path_extract_filename_no_ext(path_b, name_b);

    if(!subghz_scene_compare_load(
           storage, furi_string_get_cstr(subghz->file_path), &data->a, err)) {
        furi_string_cat_printf(err, "\nin %s", furi_string_get_cstr(name_a));
        subghz_view_compare_set_error(subghz->subghz_compare, furi_string_get_cstr(err));
    } else if(!subghz_scene_compare_load(storage, path_b, &data->b, err)) {
        furi_string_cat_printf(err, "\nin %s", furi_string_get_cstr(name_b));
        subghz_view_compare_set_error(subghz->subghz_compare, furi_string_get_cstr(err));
    } else if(data->a.kind == SubGhzCompareKindRaw || data->b.kind == SubGhzCompareKindRaw) {
        subghz_view_compare_set_error(
            subghz->subghz_compare,
            "RAW captures can't be\n"
            "compared yet.\n"
            "Decode the RAW file, or\n"
            "pick a decoded or\n"
            "BinRAW signal.");
    } else {
        subghz_compare_run(&data->a, &data->b, &data->result);
        FURI_LOG_I(
            TAG,
            "%s vs %s: %u of %u bits differ",
            data->a.protocol,
            data->b.protocol,
            data->result.diff_bits,
            data->result.compared_bits);
        subghz_view_compare_set_result(
            subghz->subghz_compare,
            &data->a,
            &data->b,
            &data->result,
            furi_string_get_cstr(name_a),
            furi_string_get_cstr(name_b));
    }

    furi_string_free(name_b);
    furi_string_free(name_a);
    furi_string_free(err);
    furi_record_close(RECORD_STORAGE);
    free(data);
}

void subghz_scene_compare_on_enter(void* context) {
    SubGhz* subghz = context;

    subghz->subghz_compare = subghz_view_compare_alloc();
    view_dispatcher_add_view(
        subghz->view_dispatcher,
        SubGhzViewIdCompare,
        subghz_view_compare_get_view(subghz->subghz_compare));

    // Pick the second capture, starting in the folder of the first
    FuriString* path_b = furi_string_alloc_set(subghz->file_path);
    DialogsFileBrowserOptions browser_options;
    dialog_file_browser_set_basic_options(
        &browser_options, SUBGHZ_APP_FILENAME_EXTENSION, &I_sub1_10px);
    browser_options.base_path = SUBGHZ_APP_FOLDER;

    bool picked = dialog_file_browser_show(subghz->dialogs, path_b, path_b, &browser_options);

    if(picked) {
        subghz_scene_compare_show(subghz, furi_string_get_cstr(path_b));
    }
    furi_string_free(path_b);

    if(picked) {
        view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdCompare);
    } else {
        scene_manager_previous_scene(subghz->scene_manager);
    }
}

bool subghz_scene_compare_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void subghz_scene_compare_on_exit(void* context) {
    SubGhz* subghz = context;
    if(subghz->subghz_compare) {
        view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdCompare);
        subghz_view_compare_free(subghz->subghz_compare);
        subghz->subghz_compare = NULL;
    }
}
