#include "../lfrfid_i.h"

static void lfrfid_write_callback(LFRFIDWorkerWriteResult result, void* context) {
    LfRfid* app = context;
    uint32_t event = 0;

    if(result == LFRFIDWorkerWriteOK) {
        event = LfRfidEventWriteOK;
    } else if(result == LFRFIDWorkerWriteProtocolCannotBeWritten) {
        event = LfRfidEventWriteProtocolCannotBeWritten;
    } else if(result == LFRFIDWorkerWriteFobCannotBeWritten) {
        event = LfRfidEventWriteFobCannotBeWritten;
    } else if(result == LFRFIDWorkerWriteTooLongToWrite) {
        event = LfRfidEventWriteTooLongToWrite;
    } else if(result == LFRFIDWorkerWriteNoEnabledTarget) {
        event = LfRfidEventWriteNoEnabledTarget;
    } else {
        // Every result has a branch above, so a new one must get one too rather than reach the
        // dispatcher as custom event 0, which no scene handles.
        FURI_LOG_E("LfRfid", "Unhandled write result %d", result);
        return;
    }

    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

// Replace the "Writing" popup with a final error. Only for the two results the worker returns
// on - it has given up by then, so the message stays until the user backs out.
static void lfrfid_scene_write_show_error(LfRfid* app, const char* text) {
    popup_set_icon(app->popup, 83, 22, &I_WarningDolphinFlip_45x42);
    popup_set_header(app->popup, "Error", 64, 3, AlignCenter, AlignTop);
    popup_set_text(app->popup, text, 3, 17, AlignLeft, AlignTop);
    notification_message(app->notifications, &sequence_blink_start_red);
}

void lfrfid_scene_write_on_enter(void* context) {
    LfRfid* app = context;
    Popup* popup = app->popup;

    popup_set_icon(popup, 0, 8, &I_NFC_manual_60x50);
    popup_set_header(popup, "Writing", 94, 16, AlignCenter, AlignTop);

    if(!furi_string_empty(app->file_name)) {
        snprintf(
            app->text_store,
            LFRFID_TEXT_STORE_SIZE,
            "[%s]\n%s",
            protocol_dict_get_name(app->dict, app->protocol_id),
            furi_string_get_cstr(app->file_name));
        popup_set_text(popup, app->text_store, 94, 29, AlignCenter, AlignTop);
    } else {
        snprintf(
            app->text_store,
            LFRFID_TEXT_STORE_SIZE,
            "[%s]\nUnsaved Tag",
            protocol_dict_get_name(app->dict, app->protocol_id));
        popup_set_text(popup, app->text_store, 94, 29, AlignCenter, AlignTop);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, LfRfidViewPopup);

    size_t size = protocol_dict_get_data_size(app->dict, app->protocol_id);
    protocol_dict_get_data(app->dict, app->protocol_id, app->old_key_data, size);

    // Kept as scene state so the no-target message can tell "none enabled at all" from "none
    // that fit this protocol" without re-reading the settings file.
    LFRFIDWriteTargetMask enabled = lfrfid_settings_get_write_targets();
    scene_manager_set_scene_state(app->scene_manager, LfRfidSceneWrite, enabled);

    lfrfid_worker_set_write_targets(app->lfworker, enabled);
    lfrfid_worker_start_thread(app->lfworker);
    lfrfid_worker_write_start(
        app->lfworker, (LFRFIDProtocol)app->protocol_id, lfrfid_write_callback, app);
    notification_message(app->notifications, &sequence_blink_start_magenta);
}

bool lfrfid_scene_write_on_event(void* context, SceneManagerEvent event) {
    LfRfid* app = context;
    Popup* popup = app->popup;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == LfRfidEventWriteOK) {
            notification_message(app->notifications, &sequence_success);
            scene_manager_next_scene(app->scene_manager, LfRfidSceneWriteSuccess);
            consumed = true;
        } else if(event.event == LfRfidEventWriteProtocolCannotBeWritten) {
            lfrfid_scene_write_show_error(app, "This protocol\ncannot be written");
            consumed = true;
        } else if(event.event == LfRfidEventWriteNoEnabledTarget) {
            // Same remedy either way, but naming the emptier case saves a puzzled trip to a
            // settings screen the user may have switched fully off on purpose.
            bool none_enabled =
                scene_manager_get_scene_state(app->scene_manager, LfRfidSceneWrite) == 0;
            lfrfid_scene_write_show_error(
                app,
                none_enabled ? "No write chips\nenabled. Enable\none in Settings" :
                               "No enabled chip\ncan write this\nprotocol");
            consumed = true;
        } else if(
            (event.event == LfRfidEventWriteFobCannotBeWritten) ||
            (event.event == LfRfidEventWriteTooLongToWrite)) {
            popup_set_icon(popup, 83, 22, &I_WarningDolphinFlip_45x42);
            popup_set_header(popup, "Still Trying to Write...", 64, 0, AlignCenter, AlignTop);
            popup_set_text(
                popup,
                "Make sure this\n"
                "card is writable\n"
                "and not protected",
                0,
                13,
                AlignLeft,
                AlignTop);
            notification_message(app->notifications, &sequence_blink_start_yellow);
            consumed = true;
        }
    }

    return consumed;
}

void lfrfid_scene_write_on_exit(void* context) {
    LfRfid* app = context;
    notification_message(app->notifications, &sequence_blink_stop);
    popup_reset(app->popup);
    lfrfid_worker_stop(app->lfworker);
    lfrfid_worker_stop_thread(app->lfworker);

    size_t size = protocol_dict_get_data_size(app->dict, app->protocol_id);
    protocol_dict_set_data(app->dict, app->protocol_id, app->old_key_data, size);
}
