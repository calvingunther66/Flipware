#include "nfc_cli_dump_mf_ultralight.h"
#include <nfc/protocols/mf_ultralight/mf_ultralight_poller.h>

#define TAG "MFU"

NfcCommand nfc_cli_dump_poller_callback_mf_ultralight(NfcGenericEvent event, void* context) {
    furi_assert(event.protocol == NfcProtocolMfUltralight);
    NfcCliDumpContext* instance = context;
    const MfUltralightPollerEvent* mf_ultralight_event = event.event_data;
    NfcCommand command = NfcCommandContinue;

    if(mf_ultralight_event->type == MfUltralightPollerEventTypeReadSuccess) {
        nfc_device_set_data(
            instance->nfc_device, NfcProtocolMfUltralight, nfc_poller_get_data(instance->poller));

        instance->result = NfcCliDumpErrorNone;
        command = NfcCommandStop;
    } else if(mf_ultralight_event->type == MfUltralightPollerEventTypeAuthRequest) {
        nfc_device_set_data(
            instance->nfc_device, NfcProtocolMfUltralight, nfc_poller_get_data(instance->poller));

        const NfcCliDumpAuthContext* auth_ctx = &instance->auth_ctx;
        MfUltralightPollerEventData* event_data = mf_ultralight_event->data;
        // The event's data is a union. An Ultralight C's poller takes its key from
        // key_request_data, which overlaps the password and the 3DES key of auth_context: fill
        // the one that this card's key is for, and nothing of the other.
        memset(event_data, 0, sizeof(*event_data));
        event_data->auth_context.skip_auth = auth_ctx->skip_auth;
        if(auth_ctx->key_size == sizeof(MfUltralightC3DesAuthKey)) {
            event_data->key_request_data.key = auth_ctx->key.tdes_key;
            event_data->key_request_data.key_provided = true;
        } else if(auth_ctx->key_size == sizeof(MfUltralightAuthPassword)) {
            event_data->auth_context.password = auth_ctx->key.password;
        }
    } else if(mf_ultralight_event->type == MfUltralightPollerEventTypeAuthFailed) {
        instance->result = NfcCliDumpErrorAuthFailed;
        command = NfcCommandStop;
    } else if(mf_ultralight_event->type == MfUltralightPollerEventTypeReadFailed) {
        instance->result = NfcCliDumpErrorAuthFailed;
        command = NfcCommandStop;
    }

    if(command == NfcCommandStop) {
        furi_semaphore_release(instance->sem_done);
    }

    return command;
}
