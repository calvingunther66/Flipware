#include "host_auth.h"
#include "host_auth_core.h"
#include "pin_code.h"

#include <furi.h>
#include <furi_hal_bt.h>
#include <furi_hal_crypto.h>
#include <furi_hal_random.h>
#include <furi_hal_version.h>
#include <storage/storage.h>
#include <dialogs/dialogs.h>
#include <notification/notification_messages.h>
#include <toolbox/args.h>
#include <toolbox/cli/cli_command.h>
#include <toolbox/cli/cli_registry.h>
#include <cli/cli.h>
#include <cli/cli_vcp_locked.h>

#include "../desktop.h"

#define TAG "HostAuth"

#define HOST_AUTH_PATH     HOST_AUTH_KEYS_PATH
#define HOST_AUTH_TMP_PATH INT_PATH(".hostauth.tmp")

#define HOST_AUTH_CLI_STACK  (5 * 1024)
#define HOST_AUTH_CORE2_WAIT (50) /**< x 100 ms */

// CBC-MAC IV. Doubles as domain separation from other users of the unique key.
static const uint8_t host_auth_mac_iv[16] = "hostauth-mac-v1";

typedef enum {
    HostAuthLoadOk,
    HostAuthLoadMissing,
    HostAuthLoadStorage,
    HostAuthLoadEnclave,
    HostAuthLoadIntegrity,
} HostAuthLoadResult;

typedef struct {
    FuriMutex* mutex;
    bool challenge_valid;
    uint8_t nonce[HOST_AUTH_NONCE_LEN];
    uint32_t challenge_tick;
    uint32_t fails;
    uint32_t blocked_until_tick;
} HostAuthState;

static HostAuthState* host_auth_state = NULL;

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

static bool host_auth_mac_equal(const uint8_t* a, const uint8_t* b) {
    uint8_t acc = 0;
    for(size_t i = 0; i < HOST_AUTH_MAC_LEN; i++)
        acc |= a[i] ^ b[i];
    return acc == 0;
}

static HostAuthLoadResult host_auth_load(HostAuthRecord* record) {
    memset(record, 0, sizeof(*record));
    Storage* storage = furi_record_open(RECORD_STORAGE);
    uint8_t* buf = malloc(HOST_AUTH_FILE_LEN);
    HostAuthLoadResult result = HostAuthLoadStorage;

    do {
        if(!storage_file_exists(storage, HOST_AUTH_PATH)) {
            result = HostAuthLoadMissing;
            break;
        }
        File* file = storage_file_alloc(storage);
        bool read_ok = false;
        if(storage_file_open(file, HOST_AUTH_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
            read_ok = storage_file_read(file, buf, HOST_AUTH_FILE_LEN) == HOST_AUTH_FILE_LEN &&
                      storage_file_eof(file);
        }
        storage_file_close(file);
        storage_file_free(file);
        if(!read_ok) {
            result = HostAuthLoadIntegrity;
            break;
        }

        uint8_t mac[HOST_AUTH_MAC_LEN];
        if(!host_auth_mac(buf, mac)) {
            result = HostAuthLoadEnclave;
            break;
        }
        if(!host_auth_mac_equal(mac, buf + HOST_AUTH_RECORD_LEN) ||
           !host_auth_record_parse(buf, record)) {
            memset(record, 0, sizeof(*record));
            result = HostAuthLoadIntegrity;
            break;
        }
        result = HostAuthLoadOk;
    } while(0);

    free(buf);
    furi_record_close(RECORD_STORAGE);
    return result;
}

static bool host_auth_save(const HostAuthRecord* record) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool ok = false;

    if(record->count == 0) {
        storage_common_remove(storage, HOST_AUTH_PATH);
        ok = !storage_file_exists(storage, HOST_AUTH_PATH);
        furi_record_close(RECORD_STORAGE);
        return ok;
    }

    uint8_t* buf = malloc(HOST_AUTH_FILE_LEN);
    host_auth_record_serialize(record, buf);
    do {
        if(!host_auth_mac(buf, buf + HOST_AUTH_RECORD_LEN)) break;

        File* file = storage_file_alloc(storage);
        bool written = false;
        if(storage_file_open(file, HOST_AUTH_TMP_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            written = storage_file_write(file, buf, HOST_AUTH_FILE_LEN) == HOST_AUTH_FILE_LEN;
        }
        storage_file_close(file);
        storage_file_free(file);
        if(!written) break;

        storage_common_remove(storage, HOST_AUTH_PATH);
        ok = storage_common_rename(storage, HOST_AUTH_TMP_PATH, HOST_AUTH_PATH) == FSE_OK;
    } while(0);

    storage_common_remove(storage, HOST_AUTH_TMP_PATH);
    free(buf);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

bool host_auth_is_enrolled(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool exists = storage_file_exists(storage, HOST_AUTH_PATH);
    furi_record_close(RECORD_STORAGE);
    return exists;
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
        // Tampered with, restored from a different device, or the radio core's
        // key store was reset. Fall back to the PIN and re-enroll.
        printf("hostauth-error integrity\r\n");
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

static bool host_auth_read_hex_arg(FuriString* args, uint8_t* out, size_t out_len) {
    FuriString* word = furi_string_alloc();
    bool ok =
        args_read_string_and_trim(args, word) &&
        host_auth_hex_decode(furi_string_get_cstr(word), furi_string_size(word), out, out_len);
    furi_string_free(word);
    return ok;
}

static void host_auth_notify_failure(void) {
    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);
    notification_message(notification, &sequence_error);
    furi_record_close(RECORD_NOTIFICATION);
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
        "hostauth-info v=1 uid=%s locked=%d pin=%d keys=%u status=%s\r\n",
        uid_hex,
        locked,
        desktop_pin_code_is_set(),
        result == HostAuthLoadOk ? record->count : 0u,
        result == HostAuthLoadOk      ? "ok" :
        result == HostAuthLoadMissing ? "none" :
                                        "error");

    // Labels stay private while locked
    if(!locked && result == HostAuthLoadOk) {
        for(uint8_t i = 0; i < record->count; i++) {
            char fp_hex[17];
            host_auth_format_fp(record->keys[i].pubkey, fp_hex);
            printf("hostauth-key fp=%s label=%s\r\n", fp_hex, record->keys[i].label);
        }
    }
    free(record);
}

static void host_auth_cmd_challenge(void) {
    HostAuthState* state = host_auth_state;
    furi_check(furi_mutex_acquire(state->mutex, FuriWaitForever) == FuriStatusOk);

    uint32_t now = furi_get_tick();
    int32_t wait_ticks = (int32_t)(state->blocked_until_tick - now);
    if(state->fails >= HOST_AUTH_FREE_FAILS && wait_ticks > 0) {
        printf(
            "hostauth-error backoff s=%lu\r\n",
            (unsigned long)((furi_kernel_get_tick_frequency() - 1 + (uint32_t)wait_ticks) /
                            furi_kernel_get_tick_frequency()));
        furi_mutex_release(state->mutex);
        return;
    }

    furi_hal_random_fill_buf(state->nonce, HOST_AUTH_NONCE_LEN);
    state->challenge_tick = now;
    state->challenge_valid = true;

    char uid_hex[2 * HOST_AUTH_UID_LEN + 1];
    char nonce_hex[2 * HOST_AUTH_NONCE_LEN + 1];
    host_auth_hex_encode(furi_hal_version_uid(), HOST_AUTH_UID_LEN, uid_hex);
    host_auth_hex_encode(state->nonce, HOST_AUTH_NONCE_LEN, nonce_hex);
    furi_mutex_release(state->mutex);

    printf(
        "hostauth-challenge v=1 action=unlock uid=%s nonce=%s ttl=%u\r\n",
        uid_hex,
        nonce_hex,
        (unsigned)(HOST_AUTH_CHALLENGE_TTL_MS / 1000));
}

static void host_auth_register_failure(HostAuthState* state) {
    state->fails++;
    uint32_t backoff_ms = host_auth_backoff_s(state->fails) * 1000;
    state->blocked_until_tick = furi_get_tick() + furi_ms_to_ticks(backoff_ms);
    FURI_LOG_W(TAG, "Bad response, %lu failures", (unsigned long)state->fails);
}

static void host_auth_cmd_response(FuriString* args) {
    HostAuthState* state = host_auth_state;
    uint8_t sig[HOST_AUTH_SIG_LEN];
    bool sig_ok = host_auth_read_hex_arg(args, sig, sizeof(sig));

    furi_check(furi_mutex_acquire(state->mutex, FuriWaitForever) == FuriStatusOk);
    if(!state->challenge_valid) {
        furi_mutex_release(state->mutex);
        printf("hostauth-error no-challenge\r\n");
        return;
    }
    // One response per challenge, whatever happens next
    state->challenge_valid = false;
    uint8_t nonce[HOST_AUTH_NONCE_LEN];
    memcpy(nonce, state->nonce, sizeof(nonce));
    memset(state->nonce, 0, sizeof(state->nonce));
    bool expired = furi_get_tick() - state->challenge_tick >
                   furi_ms_to_ticks(HOST_AUTH_CHALLENGE_TTL_MS);

    if(expired || !sig_ok) {
        host_auth_register_failure(state);
        furi_mutex_release(state->mutex);
        printf(expired ? "hostauth-error expired\r\n" : "hostauth-error usage\r\n");
        return;
    }
    furi_mutex_release(state->mutex);

    HostAuthRecord* record = malloc(sizeof(HostAuthRecord));
    HostAuthLoadResult result = host_auth_load(record);
    int signer = -1;
    if(result == HostAuthLoadOk) {
        uint8_t msg[HOST_AUTH_MESSAGE_LEN];
        host_auth_message_build(msg, HostAuthActionUnlock, furi_hal_version_uid(), nonce);
        signer = host_auth_record_find_signer(record, msg, sizeof(msg), sig);
    }

    furi_check(furi_mutex_acquire(state->mutex, FuriWaitForever) == FuriStatusOk);
    if(signer >= 0) {
        state->fails = 0;
        state->blocked_until_tick = 0;
    } else if(result == HostAuthLoadOk) {
        host_auth_register_failure(state);
    }
    furi_mutex_release(state->mutex);

    if(result != HostAuthLoadOk) {
        host_auth_print_load_error(result);
    } else if(signer < 0) {
        printf("hostauth-error bad-signature\r\n");
        host_auth_notify_failure();
    } else {
        char fp_hex[17];
        host_auth_format_fp(record->keys[signer].pubkey, fp_hex);
        FURI_LOG_I(TAG, "Signed in: %s", record->keys[signer].label);
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
    }
    free(record);
}

static bool host_auth_confirm_enroll(const HostAuthKey* key) {
    char fp_hex[17];
    host_auth_format_fp(key->pubkey, fp_hex);
    FuriString* text = furi_string_alloc_printf(
        "%.20s\n%.4s-%.4s-%.4s-%.4s\nUnlocks this Flipper",
        key->label,
        fp_hex,
        fp_hex + 4,
        fp_hex + 8,
        fp_hex + 12);

    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, "Enroll computer?", 64, 0, AlignCenter, AlignTop);
    dialog_message_set_text(message, furi_string_get_cstr(text), 64, 32, AlignCenter, AlignCenter);
    dialog_message_set_buttons(message, "Cancel", NULL, "Enroll");
    DialogMessageButton button = dialog_message_show(dialogs, message);
    dialog_message_free(message);
    furi_record_close(RECORD_DIALOGS);
    furi_string_free(text);
    return button == DialogMessageButtonRight;
}

static void host_auth_cmd_enroll(FuriString* args) {
    if(host_auth_device_locked()) {
        printf("hostauth-error locked\r\n");
        return;
    }
    uint8_t pubkey[HOST_AUTH_PUBKEY_LEN];
    if(!host_auth_read_hex_arg(args, pubkey, sizeof(pubkey))) {
        printf("hostauth-error usage\r\n");
        return;
    }
    // The rest of the line is the label
    furi_string_trim(args);

    HostAuthRecord* record = malloc(sizeof(HostAuthRecord));
    do {
        HostAuthLoadResult result = host_auth_load(record);
        if(result != HostAuthLoadOk && result != HostAuthLoadMissing) {
            // Don't silently overwrite a record we can't verify; revoke all first
            host_auth_print_load_error(result);
            break;
        }

        HostAuthAddResult add = host_auth_record_add(record, pubkey, furi_string_get_cstr(args));
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
        printf("hostauth-pending confirm-on-device\r\n");
        fflush(stdout);
        if(!host_auth_confirm_enroll(&record->keys[record->count - 1])) {
            printf("hostauth-error cancelled\r\n");
            break;
        }
        if(!host_auth_save(record)) {
            printf("hostauth-error storage\r\n");
            break;
        }

        char fp_hex[17];
        host_auth_format_fp(pubkey, fp_hex);
        printf("hostauth-ok enrolled fp=%s\r\n", fp_hex);
        if(!desktop_pin_code_is_set()) {
            printf("hostauth-note no-pin\r\n");
        }
    } while(0);
    free(record);
}

static void host_auth_cmd_revoke(FuriString* args) {
    if(host_auth_device_locked()) {
        printf("hostauth-error locked\r\n");
        return;
    }
    FuriString* word = furi_string_alloc();
    do {
        if(!args_read_string_and_trim(args, word)) {
            printf("hostauth-error usage\r\n");
            break;
        }
        if(furi_string_equal(word, "all")) {
            // Works even if the record fails its integrity check
            Storage* storage = furi_record_open(RECORD_STORAGE);
            storage_common_remove(storage, HOST_AUTH_PATH);
            bool gone = !storage_file_exists(storage, HOST_AUTH_PATH);
            furi_record_close(RECORD_STORAGE);
            printf(gone ? "hostauth-ok revoked all\r\n" : "hostauth-error storage\r\n");
            break;
        }

        uint8_t fp[HOST_AUTH_FINGERPRINT_LEN];
        if(!host_auth_hex_decode(
               furi_string_get_cstr(word), furi_string_size(word), fp, sizeof(fp))) {
            printf("hostauth-error usage\r\n");
            break;
        }
        HostAuthRecord* record = malloc(sizeof(HostAuthRecord));
        HostAuthLoadResult result = host_auth_load(record);
        if(result != HostAuthLoadOk) {
            host_auth_print_load_error(result);
        } else if(!host_auth_record_remove(record, fp)) {
            printf("hostauth-error not-found\r\n");
        } else if(!host_auth_save(record)) {
            printf("hostauth-error storage\r\n");
        } else {
            printf("hostauth-ok revoked n=1\r\n");
        }
        free(record);
    } while(0);
    furi_string_free(word);
}

static void host_auth_print_usage(void) {
    printf("Usage:\r\n"
           "hostauth info\r\n"
           "hostauth challenge\r\n"
           "hostauth response <signature:128 hex, r||s>\r\n"
           "hostauth enroll <pubkey:130 hex, 04||X||Y> <label>   (unlocked only)\r\n"
           "hostauth revoke <fingerprint:16 hex|all>             (unlocked only)\r\n");
}

static void host_auth_cli(PipeSide* pipe, FuriString* args, void* context) {
    UNUSED(pipe);
    UNUSED(context);
    FuriString* cmd = furi_string_alloc();

    if(!args_read_string_and_trim(args, cmd)) {
        host_auth_print_usage();
    } else if(furi_string_equal(cmd, "info")) {
        host_auth_cmd_info();
    } else if(furi_string_equal(cmd, "challenge")) {
        host_auth_cmd_challenge();
    } else if(furi_string_equal(cmd, "response")) {
        host_auth_cmd_response(args);
    } else if(furi_string_equal(cmd, "enroll")) {
        host_auth_cmd_enroll(args);
    } else if(furi_string_equal(cmd, "revoke")) {
        host_auth_cmd_revoke(args);
    } else {
        host_auth_print_usage();
    }

    furi_string_free(cmd);
}

void host_auth_init(void) {
    furi_check(!host_auth_state);
    host_auth_state = malloc(sizeof(HostAuthState));
    host_auth_state->mutex = furi_mutex_alloc(FuriMutexTypeNormal);

#ifdef SRV_CLI
    CliRegistry* registry = furi_record_open(RECORD_CLI);
    cli_registry_add_command_ex(
        registry, "hostauth", CliCommandFlagParallelSafe, host_auth_cli, NULL, HOST_AUTH_CLI_STACK);
    furi_record_close(RECORD_CLI);

    CliVcp* cli_vcp = furi_record_open(RECORD_CLI_VCP);
    cli_registry_add_command_ex(
        cli_vcp_get_locked_registry(cli_vcp),
        "hostauth",
        CliCommandFlagParallelSafe,
        host_auth_cli,
        NULL,
        HOST_AUTH_CLI_STACK);
    furi_record_close(RECORD_CLI_VCP);
#endif
}
