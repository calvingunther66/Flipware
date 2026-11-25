#include "host_auth.h"
#include "host_auth_core.h"
#include "pin_code.h"

#include <furi.h>
#include <furi_hal_bt.h>
#include <furi_hal_crypto.h>
#include <furi_hal_random.h>
#include <furi_hal_rtc.h>
#include <furi_hal_version.h>
#include <storage/storage.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>
#include <notification/notification_messages.h>
#include <toolbox/cli/cli_command.h>
#include <toolbox/cli/cli_registry.h>
#include <cli/cli.h>
#include <cli/cli_vcp_locked.h>

#include "../desktop.h"

#define TAG "HostAuth"

#define HOST_AUTH_PATH     HOST_AUTH_KEYS_PATH
#define HOST_AUTH_TMP_PATH HOST_AUTH_KEYS_TMP_PATH

#define HOST_AUTH_CLI_STACK  (5 * 1024)
#define HOST_AUTH_CORE2_WAIT (50) /**< x 100 ms */

#define HOST_AUTH_CONFIRM_TIMEOUT_S (60u)

// The four registers live in the RTC backup domain, next to the PIN fail counter
_Static_assert(FuriHalRtcRegisterMAX <= 20, "RTC backup registers exhausted");

// CBC-MAC IV. Doubles as domain separation from other users of the unique key.
static const uint8_t host_auth_mac_iv[16] = "hostauth-mac-v2";

typedef enum {
    HostAuthLoadOk,
    HostAuthLoadMissing,
    HostAuthLoadStorage,
    HostAuthLoadEnclave,
    HostAuthLoadIntegrity, /**< Bad length, bad MAC or bad structure */
    HostAuthLoadRollback, /**< Genuine file, but not the current one */
} HostAuthLoadResult;

typedef struct {
    FuriMutex* mutex;
    bool challenge_valid;
    uint8_t binding[HOST_AUTH_BINDING_LEN];
    uint32_t challenge_tick;
} HostAuthState;

static HostAuthState* host_auth_state = NULL;

// =============
// RTC registers
// =============

static void host_auth_backoff_register_failure(void) {
    uint32_t fails = furi_hal_rtc_get_register(FuriHalRtcRegisterHostAuthFails);
    if(fails < UINT32_MAX) fails++;
    furi_hal_rtc_set_register(FuriHalRtcRegisterHostAuthFails, fails);
    furi_hal_rtc_set_register(FuriHalRtcRegisterHostAuthFailTime, furi_hal_rtc_get_timestamp());
    FURI_LOG_W(TAG, "Bad response, %lu failures", (unsigned long)fails);
}

static uint32_t host_auth_backoff_remaining(void) {
    uint32_t fails = furi_hal_rtc_get_register(FuriHalRtcRegisterHostAuthFails);
    uint32_t last = furi_hal_rtc_get_register(FuriHalRtcRegisterHostAuthFailTime);
    uint32_t clamped = last;
    uint32_t remaining =
        host_auth_backoff_remaining_s(fails, last, furi_hal_rtc_get_timestamp(), &clamped);
    if(clamped != last) {
        furi_hal_rtc_set_register(FuriHalRtcRegisterHostAuthFailTime, clamped);
    }
    return remaining;
}

// =======
// Storage
// =======

static bool host_auth_wait_core2(void) {
    // The key used for the record MAC lives in the radio core's key store
    for(size_t i = 0; i < HOST_AUTH_CORE2_WAIT; i++) {
        if(furi_hal_bt_is_alive()) return true;
        furi_delay_ms(100);
    }
    return false;
}

static bool
    host_auth_mac(const uint8_t record[HOST_AUTH_RECORD_LEN], uint8_t mac[HOST_AUTH_MAC_LEN]) {
    if(!host_auth_wait_core2()) return false;
    if(!furi_hal_crypto_enclave_ensure_key(FURI_HAL_CRYPTO_ENCLAVE_UNIQUE_KEY_SLOT)) return false;
    if(!furi_hal_crypto_enclave_load_key(FURI_HAL_CRYPTO_ENCLAVE_UNIQUE_KEY_SLOT, host_auth_mac_iv))
        return false;

    uint8_t* out = malloc(HOST_AUTH_RECORD_LEN);
    // Fixed-length input, fixed IV: the last CBC block is a sound MAC
    bool ok = furi_hal_crypto_encrypt(record, out, HOST_AUTH_RECORD_LEN);
    furi_hal_crypto_enclave_unload_key(FURI_HAL_CRYPTO_ENCLAVE_UNIQUE_KEY_SLOT);
    if(ok) memcpy(mac, out + HOST_AUTH_RECORD_LEN - HOST_AUTH_MAC_LEN, HOST_AUTH_MAC_LEN);
    memset(out, 0, HOST_AUTH_RECORD_LEN);
    free(out);
    return ok;
}

static HostAuthLoadResult
    host_auth_read_file(Storage* storage, const char* path, HostAuthRecord* record) {
    memset(record, 0, sizeof(*record));
    if(!storage_file_exists(storage, path)) return HostAuthLoadMissing;

    uint8_t* buf = malloc(HOST_AUTH_FILE_LEN);
    HostAuthLoadResult result = HostAuthLoadIntegrity;
    File* file = storage_file_alloc(storage);
    do {
        if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
            result = HostAuthLoadStorage;
            break;
        }
        // Exactly FILE_LEN bytes, nothing more
        if(storage_file_size(file) != HOST_AUTH_FILE_LEN) break;
        if(storage_file_read(file, buf, HOST_AUTH_FILE_LEN) != HOST_AUTH_FILE_LEN) break;

        uint8_t mac[HOST_AUTH_MAC_LEN];
        if(!host_auth_mac(buf, mac)) {
            result = HostAuthLoadEnclave;
            break;
        }
        if(!host_auth_ct_equal(mac, buf + HOST_AUTH_RECORD_LEN, HOST_AUTH_MAC_LEN)) break;
        if(!host_auth_record_parse(buf, record)) break;
        // Old copies (an updater backup, a file copied back) carry an old epoch
        if(!host_auth_epoch_matches(
               record->epoch, furi_hal_rtc_get_register(FuriHalRtcRegisterHostAuthEpoch))) {
            result = HostAuthLoadRollback;
            break;
        }
        result = HostAuthLoadOk;
    } while(0);
    storage_file_close(file);
    storage_file_free(file);
    if(result != HostAuthLoadOk) memset(record, 0, sizeof(*record));
    memset(buf, 0, HOST_AUTH_FILE_LEN);
    free(buf);
    return result;
}

static HostAuthFileState host_auth_file_state(HostAuthLoadResult result) {
    switch(result) {
    case HostAuthLoadOk:
        return HostAuthFileOk;
    case HostAuthLoadMissing:
        return HostAuthFileMissing;
    case HostAuthLoadIntegrity:
        return HostAuthFileInvalid;
    case HostAuthLoadRollback:
        return HostAuthFileStale;
    default:
        return HostAuthFileUnreadable;
    }
}

static HostAuthLoadResult host_auth_load(HostAuthRecord* record) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    HostAuthLoadResult result = host_auth_read_file(storage, HOST_AUTH_PATH, record);
    HostAuthFileState tmp_state = HostAuthFileMissing;
    HostAuthRecord* tmp = NULL;
    if(result != HostAuthLoadOk && storage_file_exists(storage, HOST_AUTH_TMP_PATH)) {
        tmp = malloc(sizeof(HostAuthRecord));
        tmp_state = host_auth_file_state(host_auth_read_file(storage, HOST_AUTH_TMP_PATH, tmp));
    }

    switch(host_auth_load_decide(host_auth_file_state(result), tmp_state)) {
    case HostAuthLoadUseMain:
        // Left over from a save that stopped before the epoch moved: stale
        storage_common_remove(storage, HOST_AUTH_TMP_PATH);
        break;
    case HostAuthLoadRecoverTmp:
        // A save that stopped after the epoch moved but before the rename
        *record = *tmp;
        storage_common_remove(storage, HOST_AUTH_PATH);
        storage_common_rename(storage, HOST_AUTH_TMP_PATH, HOST_AUTH_PATH);
        FURI_LOG_I(TAG, "Recovered the key file from an interrupted save");
        result = HostAuthLoadOk;
        break;
    default:
        memset(record, 0, sizeof(*record));
        break;
    }

    if(tmp) {
        memset(tmp, 0, sizeof(*tmp));
        free(tmp);
    }
    furi_record_close(RECORD_STORAGE);
    return result;
}

/**
 * Write the record under a fresh epoch. Order matters for crash safety:
 * 1. write the tmp file with the new epoch (main file still current),
 * 2. move the epoch register (now only the tmp file is current; load recovers it),
 * 3. rename tmp over main.
 * Every save moves the epoch, so every older copy of the file stops loading.
 */
static bool host_auth_save(HostAuthRecord* record) {
    if(record->count == 0) return host_auth_forget_all();

    uint32_t old_epoch = furi_hal_rtc_get_register(FuriHalRtcRegisterHostAuthEpoch);
    record->epoch = host_auth_new_epoch(old_epoch);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    uint8_t* buf = malloc(HOST_AUTH_FILE_LEN);
    host_auth_record_serialize(record, buf);
    bool ok = false;
    do {
        if(!host_auth_mac(buf, buf + HOST_AUTH_RECORD_LEN)) break;

        File* file = storage_file_alloc(storage);
        bool written = false;
        if(storage_file_open(file, HOST_AUTH_TMP_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            written = storage_file_write(file, buf, HOST_AUTH_FILE_LEN) == HOST_AUTH_FILE_LEN &&
                      storage_file_sync(file);
        }
        storage_file_close(file);
        storage_file_free(file);
        if(!written) {
            storage_common_remove(storage, HOST_AUTH_TMP_PATH);
            break;
        }

        furi_hal_rtc_set_register(FuriHalRtcRegisterHostAuthEpoch, record->epoch);

        storage_common_remove(storage, HOST_AUTH_PATH);
        // If this fails, the next load recovers from the tmp file
        ok = storage_common_rename(storage, HOST_AUTH_TMP_PATH, HOST_AUTH_PATH) == FSE_OK;
    } while(0);
    if(!ok && furi_hal_rtc_get_register(FuriHalRtcRegisterHostAuthEpoch) == old_epoch) {
        record->epoch = old_epoch;
    }

    memset(buf, 0, HOST_AUTH_FILE_LEN);
    free(buf);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

// =======
// Helpers
// =======

static bool host_auth_device_locked(void) {
    Desktop* desktop = furi_record_open(RECORD_DESKTOP);
    bool locked = desktop_api_is_locked(desktop);
    furi_record_close(RECORD_DESKTOP);
    return locked;
}

static void host_auth_print_load_error(HostAuthLoadResult result) {
    switch(result) {
    case HostAuthLoadMissing:
        printf("hostauth-error no-keys\r\n");
        break;
    case HostAuthLoadEnclave:
        printf("hostauth-error enclave\r\n");
        break;
    case HostAuthLoadIntegrity:
        // Edited, copied from another device, or the radio core's key store
        // was reset. Unlock with the PIN and re-enroll.
        printf("hostauth-error integrity\r\n");
        break;
    case HostAuthLoadRollback:
        // A genuine but outdated copy (restored backup), or the RTC backup
        // registers were cleared (battery fully drained, factory reset)
        printf("hostauth-error rollback\r\n");
        break;
    default:
        printf("hostauth-error storage\r\n");
        break;
    }
}

static void host_auth_format_fp(const uint8_t pubkey[HOST_AUTH_PUBKEY_LEN], char out[17]) {
    uint8_t fp[HOST_AUTH_FINGERPRINT_LEN];
    host_auth_fingerprint(pubkey, fp);
    host_auth_hex_encode(fp, sizeof(fp), out);
}

static void host_auth_notify(const NotificationSequence* sequence) {
    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);
    notification_message(notification, sequence);
    furi_record_close(RECORD_NOTIFICATION);
}

static const char* host_auth_verify_error(HostAuthVerifyResult result) {
    switch(result) {
    case HostAuthVerifyRpMismatch:
        return "rp-mismatch";
    case HostAuthVerifyNoPresence:
        return "no-presence";
    case HostAuthVerifyNoVerification:
        return "no-verification";
    case HostAuthVerifySynced:
        return "synced-credential";
    case HostAuthVerifyBadFlags:
        return "bad-flags";
    case HostAuthVerifyCounter:
        return "counter";
    default:
        return "bad-signature";
    }
}

// =============================
// On-device enroll confirmation
// =============================

typedef enum {
    HostAuthConfirmEventAccept,
    HostAuthConfirmEventCancel,
} HostAuthConfirmEvent;

typedef struct {
    FuriMessageQueue* queue;
    char label[HOST_AUTH_LABEL_LEN];
    char fp[20]; // xxxx-xxxx-xxxx-xxxx
    bool require_uv;
    uint32_t seconds_left;
} HostAuthConfirm;

static void host_auth_confirm_draw(Canvas* canvas, void* context) {
    HostAuthConfirm* confirm = context;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 0, AlignCenter, AlignTop, "Enroll this computer?");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 13, AlignCenter, AlignTop, confirm->label);
    canvas_set_font(canvas, FontKeyboard);
    canvas_draw_str_aligned(canvas, 64, 25, AlignCenter, AlignTop, confirm->fp);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(
        canvas,
        64,
        37,
        AlignCenter,
        AlignTop,
        confirm->require_uv ? "Unlocks with biometry/PIN" : "Unlocks with a touch");
    char footer[40];
    snprintf(
        footer,
        sizeof(footer),
        "Hold OK: enroll  Back: no (%lus)",
        (unsigned long)confirm->seconds_left);
    canvas_draw_str_aligned(canvas, 64, 63, AlignCenter, AlignBottom, footer);
}

static void host_auth_confirm_input(InputEvent* event, void* context) {
    HostAuthConfirm* confirm = context;
    // Only the physical buttons count: RPC, `input send` and the Video Game
    // Module all inject events marked as software
    if(event->sequence_source != INPUT_SEQUENCE_SOURCE_HARDWARE) return;

    HostAuthConfirmEvent out;
    if(event->key == InputKeyOk && event->type == InputTypeLong) {
        out = HostAuthConfirmEventAccept;
    } else if(event->key == InputKeyBack && event->type == InputTypeShort) {
        out = HostAuthConfirmEventCancel;
    } else {
        return;
    }
    furi_message_queue_put(confirm->queue, &out, 0);
}

static bool host_auth_confirm_enroll(const HostAuthKey* key) {
    HostAuthConfirm* confirm = malloc(sizeof(HostAuthConfirm));
    confirm->queue = furi_message_queue_alloc(4, sizeof(HostAuthConfirmEvent));
    strlcpy(confirm->label, key->label, sizeof(confirm->label));
    char fp_hex[17];
    host_auth_format_fp(key->pubkey, fp_hex);
    snprintf(
        confirm->fp,
        sizeof(confirm->fp),
        "%.4s-%.4s-%.4s-%.4s",
        fp_hex,
        fp_hex + 4,
        fp_hex + 8,
        fp_hex + 12);
    confirm->require_uv = key->flags & HOST_AUTH_KEY_REQUIRE_UV;
    confirm->seconds_left = HOST_AUTH_CONFIRM_TIMEOUT_S;

    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, host_auth_confirm_draw, confirm);
    view_port_input_callback_set(view_port, host_auth_confirm_input, confirm);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);
    gui_view_port_send_to_front(gui, view_port);
    host_auth_notify(&sequence_display_backlight_on);
    host_auth_notify(&sequence_single_vibro);

    bool accepted = false;
    while(confirm->seconds_left > 0) {
        HostAuthConfirmEvent event;
        if(furi_message_queue_get(confirm->queue, &event, 1000) == FuriStatusOk) {
            accepted = event == HostAuthConfirmEventAccept;
            break;
        }
        confirm->seconds_left--;
        view_port_update(view_port);
    }

    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_message_queue_free(confirm->queue);
    free(confirm);
    return accepted;
}

// ===========
// Subcommands
// ===========

static void host_auth_cmd_info(void) {
    char uid_hex[2 * HOST_AUTH_UID_LEN + 1];
    host_auth_hex_encode(furi_hal_version_uid(), HOST_AUTH_UID_LEN, uid_hex);
    bool locked = host_auth_device_locked();

    HostAuthRecord* record = malloc(sizeof(HostAuthRecord));
    HostAuthLoadResult result = host_auth_load(record);
    printf(
        "hostauth-info v=%u uid=%s rp=%s locked=%d pin=%d keys=%u status=%s\r\n",
        (unsigned)HOST_AUTH_PROTOCOL_VERSION,
        uid_hex,
        HOST_AUTH_RP_ID,
        locked,
        desktop_pin_code_is_set(),
        result == HostAuthLoadOk ? record->count : 0u,
        result == HostAuthLoadOk      ? "ok" :
        result == HostAuthLoadMissing ? "none" :
                                        "error");

    // Labels and fingerprints stay private while locked
    if(!locked && result == HostAuthLoadOk) {
        for(uint8_t i = 0; i < record->count; i++) {
            char fp_hex[17];
            host_auth_format_fp(record->keys[i].pubkey, fp_hex);
            printf(
                "hostauth-key fp=%s uv=%u label=%s\r\n",
                fp_hex,
                (unsigned)(record->keys[i].flags & HOST_AUTH_KEY_REQUIRE_UV),
                record->keys[i].label);
        }
    }
    free(record);
}

static void host_auth_cmd_challenge(void) {
    HostAuthState* state = host_auth_state;
    furi_check(furi_mutex_acquire(state->mutex, FuriWaitForever) == FuriStatusOk);
    // A new request always kills the outstanding challenge
    state->challenge_valid = false;
    memset(state->binding, 0, sizeof(state->binding));

    do {
        uint32_t remaining = host_auth_backoff_remaining();
        if(remaining > 0) {
            printf("hostauth-error backoff s=%lu\r\n", (unsigned long)remaining);
            break;
        }

        uint32_t counter = furi_hal_rtc_get_register(FuriHalRtcRegisterHostAuthCounter);
        if(counter == UINT32_MAX) {
            printf("hostauth-error counter-exhausted\r\n");
            break;
        }
        counter++;
        // Persist before use: a counter value is never handed out twice
        furi_hal_rtc_set_register(FuriHalRtcRegisterHostAuthCounter, counter);

        uint8_t nonce[HOST_AUTH_NONCE_LEN];
        furi_hal_random_fill_buf(nonce, sizeof(nonce));
        host_auth_binding_build(
            state->binding,
            HostAuthActionUnlock,
            furi_hal_version_uid(),
            furi_hal_rtc_get_register(FuriHalRtcRegisterHostAuthEpoch),
            counter,
            nonce);
        memset(nonce, 0, sizeof(nonce));
        state->challenge_tick = furi_get_tick();
        state->challenge_valid = true;

        char challenge[HOST_AUTH_CHALLENGE_B64_LEN + 1];
        host_auth_base64url_encode(state->binding, HOST_AUTH_BINDING_LEN, challenge);
        printf(
            "hostauth-challenge v=%u rp=%s challenge=%s ttl=%u\r\n",
            (unsigned)HOST_AUTH_PROTOCOL_VERSION,
            HOST_AUTH_RP_ID,
            challenge,
            (unsigned)(HOST_AUTH_CHALLENGE_TTL_MS / 1000));
    } while(0);

    furi_mutex_release(state->mutex);
}

/** Take the outstanding challenge (single use). Returns false if there is none. */
static bool host_auth_take_challenge(uint8_t binding[HOST_AUTH_BINDING_LEN], bool* expired) {
    HostAuthState* state = host_auth_state;
    furi_check(furi_mutex_acquire(state->mutex, FuriWaitForever) == FuriStatusOk);
    bool valid = state->challenge_valid;
    if(valid) {
        memcpy(binding, state->binding, HOST_AUTH_BINDING_LEN);
        *expired = furi_get_tick() - state->challenge_tick >
                   furi_ms_to_ticks(HOST_AUTH_CHALLENGE_TTL_MS);
    }
    state->challenge_valid = false;
    memset(state->binding, 0, sizeof(state->binding));
    furi_mutex_release(state->mutex);
    return valid;
}

/** A `response` line that did not parse still uses up the challenge. */
static void host_auth_cmd_response_malformed(void) {
    uint8_t binding[HOST_AUTH_BINDING_LEN];
    bool expired = false;
    if(host_auth_take_challenge(binding, &expired)) {
        host_auth_backoff_register_failure();
        memset(binding, 0, sizeof(binding));
    }
    printf("hostauth-error usage\r\n");
}

static void host_auth_cmd_response(const HostAuthCommand* cmd) {
    uint8_t binding[HOST_AUTH_BINDING_LEN];
    bool expired = false;
    if(!host_auth_take_challenge(binding, &expired)) {
        printf("hostauth-error no-challenge\r\n");
        return;
    }
    if(expired) {
        host_auth_backoff_register_failure();
        printf("hostauth-error expired\r\n");
        return;
    }

    HostAuthRecord* record = malloc(sizeof(HostAuthRecord));
    do {
        HostAuthLoadResult load = host_auth_load(record);
        if(load != HostAuthLoadOk) {
            host_auth_print_load_error(load);
            break;
        }

        int signer = -1;
        uint32_t new_count = 0;
        HostAuthVerifyResult result = host_auth_verify_assertion(
            record,
            binding,
            cmd->response.auth_data,
            cmd->response.r,
            cmd->response.s,
            &signer,
            &new_count);
        if(result != HostAuthVerifyOk) {
            host_auth_backoff_register_failure();
            printf("hostauth-error %s\r\n", host_auth_verify_error(result));
            host_auth_notify(&sequence_error);
            break;
        }

        HostAuthKey* key = &record->keys[signer];
        if(new_count != key->sign_count) {
            // Persist the counter before unlocking, or a cloned authenticator
            // could replay an older count after a reboot
            key->sign_count = new_count;
            if(!host_auth_save(record)) {
                printf("hostauth-error storage\r\n");
                break;
            }
        }

        host_auth_backoff_clear();
        char fp_hex[17];
        host_auth_format_fp(key->pubkey, fp_hex);
        FURI_LOG_I(TAG, "Signed in: %s", key->label);
        Desktop* desktop = furi_record_open(RECORD_DESKTOP);
        if(desktop_api_is_locked(desktop)) {
            // Print first: unlocking restarts this shell in full mode
            printf("hostauth-ok unlocked fp=%s\r\n", fp_hex);
            fflush(stdout);
            desktop_api_unlock(desktop);
        } else {
            printf("hostauth-ok verified fp=%s\r\n", fp_hex);
        }
        furi_record_close(RECORD_DESKTOP);
    } while(0);
    memset(binding, 0, sizeof(binding));
    free(record);
}

static void host_auth_cmd_enroll(const HostAuthCommand* cmd) {
    HostAuthRecord* record = malloc(sizeof(HostAuthRecord));
    do {
        HostAuthLoadResult result = host_auth_load(record);
        if(result != HostAuthLoadOk && result != HostAuthLoadMissing) {
            // Don't silently replace a record we can't verify; revoke all first
            host_auth_print_load_error(result);
            break;
        }

        HostAuthAddResult add = host_auth_record_add(
            record, cmd->enroll.pubkey, cmd->enroll.require_uv, cmd->enroll.label);
        if(add == HostAuthAddInvalidKey) {
            printf("hostauth-error invalid-key\r\n");
            break;
        } else if(add == HostAuthAddDuplicate) {
            printf("hostauth-error duplicate\r\n");
            break;
        } else if(add == HostAuthAddFull) {
            printf("hostauth-error full max=%u\r\n", (unsigned)HOST_AUTH_MAX_KEYS);
            break;
        }

        // A key that unlocks the device is only added with a press on the device
        char fp_hex[17];
        host_auth_format_fp(cmd->enroll.pubkey, fp_hex);
        printf("hostauth-pending confirm-on-device fp=%s\r\n", fp_hex);
        fflush(stdout);
        if(!host_auth_confirm_enroll(&record->keys[record->count - 1])) {
            printf("hostauth-error cancelled\r\n");
            break;
        }
        // The device may have been locked while the prompt was up
        if(host_auth_device_locked()) {
            printf("hostauth-error locked\r\n");
            break;
        }
        if(!host_auth_save(record)) {
            printf("hostauth-error storage\r\n");
            break;
        }

        printf("hostauth-ok enrolled fp=%s\r\n", fp_hex);
        if(!desktop_pin_code_is_set()) {
            printf("hostauth-note no-pin\r\n");
        }
    } while(0);
    free(record);
}

static void host_auth_cmd_revoke(const HostAuthCommand* cmd) {
    if(cmd->revoke.all) {
        // Works even if the record fails its integrity check
        printf(
            host_auth_forget_all() ? "hostauth-ok revoked all\r\n" : "hostauth-error storage\r\n");
        return;
    }

    HostAuthRecord* record = malloc(sizeof(HostAuthRecord));
    HostAuthLoadResult result = host_auth_load(record);
    if(result != HostAuthLoadOk) {
        host_auth_print_load_error(result);
    } else if(!host_auth_record_remove(record, cmd->revoke.fp)) {
        printf("hostauth-error not-found\r\n");
    } else if(!host_auth_save(record)) {
        printf("hostauth-error storage\r\n");
    } else {
        printf("hostauth-ok revoked n=1 left=%u\r\n", (unsigned)record->count);
    }
    free(record);
}

static void host_auth_print_usage(void) {
    printf("Usage:\r\n"
           "hostauth info\r\n"
           "hostauth challenge\r\n"
           "hostauth response <authenticatorData:74 hex> <signature:DER hex>\r\n"
           "hostauth enroll <pubkey:130 hex> <uv:0|1> <label>   (unlocked only)\r\n"
           "hostauth revoke <fingerprint:16 hex|all>           (unlocked only)\r\n");
}

static void host_auth_cli(PipeSide* pipe, FuriString* args, void* context) {
    UNUSED(pipe);
    UNUSED(context);

    HostAuthCommand* cmd = malloc(sizeof(HostAuthCommand));
    const char* text = furi_string_get_cstr(args);
    size_t len = furi_string_size(args);
    if(!host_auth_parse_command(text, len, cmd)) {
        if(len >= 8 && memcmp(text, "response", 8) == 0) {
            host_auth_cmd_response_malformed();
        } else {
            host_auth_print_usage();
        }
    } else if(
        (cmd->type == HostAuthCmdEnroll || cmd->type == HostAuthCmdRevoke) &&
        host_auth_device_locked()) {
        // Changing the key list needs an unlocked device
        printf("hostauth-error locked\r\n");
    } else {
        switch(cmd->type) {
        case HostAuthCmdInfo:
            host_auth_cmd_info();
            break;
        case HostAuthCmdChallenge:
            host_auth_cmd_challenge();
            break;
        case HostAuthCmdResponse:
            host_auth_cmd_response(cmd);
            break;
        case HostAuthCmdEnroll:
            host_auth_cmd_enroll(cmd);
            break;
        case HostAuthCmdRevoke:
            host_auth_cmd_revoke(cmd);
            break;
        default:
            host_auth_print_usage();
            break;
        }
    }

    memset(cmd, 0, sizeof(*cmd));
    free(cmd);
}

void host_auth_init(void) {
    furi_check(!host_auth_state);
    host_auth_state = malloc(sizeof(HostAuthState));
    host_auth_state->mutex = furi_mutex_alloc(FuriMutexTypeNormal);

#ifdef SRV_CLI
    // Not CliCommandFlagParallelSafe: the shell takes the loader lock, so the
    // command never runs (and never needs its stack and the ECDSA working
    // memory) while an app such as Sub-GHz is holding most of the heap.
    CliRegistry* registry = furi_record_open(RECORD_CLI);
    cli_registry_add_command_ex(
        registry, "hostauth", CliCommandFlagDefault, host_auth_cli, NULL, HOST_AUTH_CLI_STACK);
    furi_record_close(RECORD_CLI);

    CliVcp* cli_vcp = furi_record_open(RECORD_CLI_VCP);
    cli_registry_add_command_ex(
        cli_vcp_get_locked_registry(cli_vcp),
        "hostauth",
        CliCommandFlagDefault,
        host_auth_cli,
        NULL,
        HOST_AUTH_CLI_STACK);
    furi_record_close(RECORD_CLI_VCP);
#endif
}
