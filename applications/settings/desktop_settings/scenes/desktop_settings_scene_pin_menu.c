#include <gui/scene_manager.h>
#include <applications.h>

#include "../desktop_settings_app.h"
#include "desktop_settings_scene.h"
#include "desktop_settings_scene_i.h"
#include "../desktop_settings_custom_event.h"
#include <desktop/helpers/host_auth.h>

static void desktop_settings_scene_pin_menu_submenu_callback(void* context, uint32_t index) {
    DesktopSettingsApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void desktop_settings_scene_pin_menu_on_enter(void* context) {
    DesktopSettingsApp* app = context;
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);

    if(!desktop_pin_code_is_set()) {
        submenu_add_item(
            submenu,
            "Set PIN",
            DesktopSettingsCustomEventSetPin,
            desktop_settings_scene_pin_menu_submenu_callback,
            app);

    } else {
        submenu_add_item(
            submenu,
            "Change PIN",
            DesktopSettingsCustomEventChangePin,
            desktop_settings_scene_pin_menu_submenu_callback,
            app);

        submenu_add_item(
            submenu,
            "Remove PIN",
            DesktopSettingsCustomEventDisablePin,
            desktop_settings_scene_pin_menu_submenu_callback,
            app);
    }

    // Host sign-in keys (see documentation/HostAuth.md). Removing keys needs no PIN:
    // it can only take access away.
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool hosts_enrolled = storage_file_exists(storage, HOST_AUTH_KEYS_PATH);
    furi_record_close(RECORD_STORAGE);
    if(hosts_enrolled) {
        submenu_add_item(
            submenu,
            "Forget Computers",
            DesktopSettingsCustomEventForgetHosts,
            desktop_settings_scene_pin_menu_submenu_callback,
            app);
    }

    submenu_set_header(app->submenu, "PIN Code Settings");
    submenu_set_selected_item(app->submenu, app->pin_menu_idx);
    view_dispatcher_switch_to_view(app->view_dispatcher, DesktopSettingsAppViewMenu);
}

bool desktop_settings_scene_pin_menu_on_event(void* context, SceneManagerEvent event) {
    DesktopSettingsApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case DesktopSettingsCustomEventSetPin:
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppScenePinSetupHowto);
            consumed = true;
            break;
        case DesktopSettingsCustomEventChangePin:
            scene_manager_set_scene_state(
                app->scene_manager,
                DesktopSettingsAppScenePinAuth,
                SCENE_STATE_PIN_AUTH_CHANGE_PIN);
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppScenePinAuth);
            consumed = true;
            break;
        case DesktopSettingsCustomEventDisablePin:
            scene_manager_set_scene_state(
                app->scene_manager, DesktopSettingsAppScenePinAuth, SCENE_STATE_PIN_AUTH_DISABLE);
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppScenePinAuth);
            consumed = true;
            break;
        case DesktopSettingsCustomEventForgetHosts: {
            DialogMessage* message = dialog_message_alloc();
            dialog_message_set_header(message, "Forget Computers?", 64, 0, AlignCenter, AlignTop);
            dialog_message_set_text(
                message,
                "Computers you enrolled\nwill no longer be able\nto unlock this Flipper.",
                64,
                34,
                AlignCenter,
                AlignCenter);
            dialog_message_set_buttons(message, "Cancel", NULL, "Forget");
            if(dialog_message_show(app->dialogs, message) == DialogMessageButtonRight) {
                Storage* storage = furi_record_open(RECORD_STORAGE);
                storage_simply_remove(storage, HOST_AUTH_KEYS_PATH);
                furi_record_close(RECORD_STORAGE);
            }
            dialog_message_free(message);
            // Rebuild the menu so the item disappears
            desktop_settings_scene_pin_menu_on_exit(app);
            desktop_settings_scene_pin_menu_on_enter(app);
            consumed = true;
            break;
        }
        default:
            consumed = true;
            break;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        submenu_set_selected_item(app->submenu, 0);
    }

    return consumed;
}

void desktop_settings_scene_pin_menu_on_exit(void* context) {
    DesktopSettingsApp* app = context;

    app->pin_menu_idx = submenu_get_selected_item(app->submenu);
    submenu_reset(app->submenu);
}
