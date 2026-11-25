// The parts of host sign-in that the Desktop settings app also needs. Only
// exported firmware APIs are used here, so the settings app (built as a FAP)
// can compile this file in (see desktop_settings/mock_imports/mock_host_auth.c).
#include "host_auth.h"

#include <furi.h>
#include <furi_hal_random.h>
#include <furi_hal_rtc.h>
#include <storage/storage.h>

uint32_t host_auth_new_epoch(uint32_t avoid) {
    uint32_t epoch;
    do {
        epoch = furi_hal_random_get();
    } while(epoch == 0 || epoch == avoid);
    return epoch;
}

void host_auth_backoff_clear(void) {
    furi_hal_rtc_set_register(FuriHalRtcRegisterHostAuthFails, 0);
    furi_hal_rtc_set_register(FuriHalRtcRegisterHostAuthFailTime, 0);
}

void host_auth_on_unlock(void) {
    host_auth_backoff_clear();
}

bool host_auth_forget_all(void) {
    // Kill every copy first, then delete: a failed delete leaves a dead file
    furi_hal_rtc_set_register(
        FuriHalRtcRegisterHostAuthEpoch,
        host_auth_new_epoch(furi_hal_rtc_get_register(FuriHalRtcRegisterHostAuthEpoch)));
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_remove(storage, HOST_AUTH_KEYS_TMP_PATH);
    storage_common_remove(storage, HOST_AUTH_KEYS_PATH);
    bool gone = !storage_file_exists(storage, HOST_AUTH_KEYS_PATH) &&
                !storage_file_exists(storage, HOST_AUTH_KEYS_TMP_PATH);
    furi_record_close(RECORD_STORAGE);
    return gone;
}

bool host_auth_is_enrolled(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool exists = storage_file_exists(storage, HOST_AUTH_KEYS_PATH) ||
                  storage_file_exists(storage, HOST_AUTH_KEYS_TMP_PATH);
    furi_record_close(RECORD_STORAGE);
    return exists;
}
