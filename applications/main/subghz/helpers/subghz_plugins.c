#include "subghz_plugins.h"
#include "subghz_plugins_table.h"
#include "../plugins/subghz_protocol_plugin.h"

#include <furi.h>
#include <storage/storage.h>
#include <flipper_application/plugins/plugin_manager.h>
#include <loader/firmware_api/firmware_api.h>

#define TAG "SubGhzPlugins"

#define SUBGHZ_PLUGINS_PATH_FORMAT        APP_ASSETS_PATH("plugins/%s")
// One bundle id per line; bundles listed there are never loaded (saves RAM)
#define SUBGHZ_PLUGINS_DISABLED_FILE      EXT_PATH("subghz/assets/disabled_plugins.txt")
// Free heap that has to be left after a bundle is loaded, so the app and the radio keep working
#define SUBGHZ_PLUGINS_HEAP_RESERVE       (16 * 1024)
// A loaded .fal takes about its file size plus the relocation work space
#define SUBGHZ_PLUGINS_HEAP_PER_FILE_BYTE 2

typedef struct {
    PluginManager* manager;
    const SubGhzProtocolPlugin* plugin;
    bool loaded;
    bool wanted; // by the modulation, last time select() ran
    bool pinned; // loaded for a single protocol, stays until free
    bool disabled;
} SubGhzPluginSlot;

struct SubGhzPlugins {
    const SubGhzProtocolRegistry* base;
    SubGhzPluginSlot slots[SUBGHZ_PLUGIN_BUNDLES_MAX];
    SubGhzProtocolRegistry registry;
    const SubGhzProtocol** items;
    size_t skipped;
};

static void subghz_plugins_read_disabled(SubGhzPlugins* instance) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    FuriString* text = furi_string_alloc();
    if(storage_file_open(file, SUBGHZ_PLUGINS_DISABLED_FILE, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char buf[64];
        size_t read;
        while((read = storage_file_read(file, buf, sizeof(buf))) > 0) {
            for(size_t i = 0; i < read; i++) {
                furi_string_push_back(text, buf[i]);
            }
            if(furi_string_size(text) > 512) break;
        }
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    for(size_t i = 0; i < subghz_plugin_bundle_count; i++) {
        const char* id = subghz_plugin_bundles[i].id;
        size_t pos = 0;
        const char* haystack = furi_string_get_cstr(text);
        size_t id_len = strlen(id);
        // Whole-word match: the id is bounded by the start, the end or whitespace
        while((pos = furi_string_search_str(text, id, pos)) != FURI_STRING_FAILURE) {
            bool start_ok = pos == 0 || haystack[pos - 1] <= ' ';
            bool end_ok = haystack[pos + id_len] <= ' ';
            if(start_ok && end_ok) {
                instance->slots[i].disabled = true;
                break;
            }
            pos += id_len;
        }
    }
    furi_string_free(text);
}

static void subghz_plugins_rebuild_registry(SubGhzPlugins* instance) {
    size_t count = subghz_protocol_registry_count(instance->base);
    for(size_t i = 0; i < subghz_plugin_bundle_count; i++) {
        if(instance->slots[i].loaded) count += instance->slots[i].plugin->count;
    }

    const SubGhzProtocol** items = malloc(count * sizeof(SubGhzProtocol*));
    size_t n = 0;
    for(size_t i = 0; i < subghz_protocol_registry_count(instance->base); i++) {
        items[n++] = subghz_protocol_registry_get_by_index(instance->base, i);
    }
    for(size_t i = 0; i < subghz_plugin_bundle_count; i++) {
        const SubGhzPluginSlot* slot = &instance->slots[i];
        if(!slot->loaded) continue;
        for(size_t j = 0; j < slot->plugin->count; j++) {
            items[n++] = slot->plugin->items[j];
        }
    }
    furi_check(n == count);

    free(instance->items);
    instance->items = items;
    // SubGhzProtocolRegistry has const members, so it is replaced as a whole
    const SubGhzProtocolRegistry registry = {.items = items, .size = count};
    memcpy(&instance->registry, &registry, sizeof(registry));
}

static bool subghz_plugins_fits_in_heap(const char* path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FileInfo info;
    bool ok = storage_common_stat(storage, path, &info) == FSE_OK;
    furi_record_close(RECORD_STORAGE);
    if(!ok) {
        FURI_LOG_W(TAG, "%s is missing", path);
        return false;
    }
    size_t need = info.size * SUBGHZ_PLUGINS_HEAP_PER_FILE_BYTE + SUBGHZ_PLUGINS_HEAP_RESERVE;
    if(memmgr_get_free_heap() < need) {
        FURI_LOG_W(
            TAG,
            "Not enough memory for %s: %zu free, %zu needed",
            path,
            memmgr_get_free_heap(),
            need);
        return false;
    }
    return true;
}

static bool subghz_plugins_load_slot(SubGhzPlugins* instance, size_t index) {
    SubGhzPluginSlot* slot = &instance->slots[index];
    furi_assert(!slot->loaded);

    FuriString* path =
        furi_string_alloc_printf(SUBGHZ_PLUGINS_PATH_FORMAT, subghz_plugin_bundles[index].file);
    bool loaded = false;
    do {
        if(!subghz_plugins_fits_in_heap(furi_string_get_cstr(path))) break;

        slot->manager = plugin_manager_alloc(
            SUBGHZ_PROTOCOL_PLUGIN_APP_ID,
            SUBGHZ_PROTOCOL_PLUGIN_API_VERSION,
            firmware_api_interface);
        PluginManagerError error =
            plugin_manager_load_single(slot->manager, furi_string_get_cstr(path));
        if(error != PluginManagerErrorNone) {
            FURI_LOG_E(TAG, "Failed to load %s (error %d)", furi_string_get_cstr(path), error);
            break;
        }
        slot->plugin = plugin_manager_get_ep(slot->manager, 0);
        if(!slot->plugin || slot->plugin->count == 0) break;
        loaded = true;
    } while(false);

    if(loaded) {
        slot->loaded = true;
        FURI_LOG_I(
            TAG,
            "Loaded %s: %zu protocols, %zu bytes heap left",
            subghz_plugin_bundles[index].id,
            slot->plugin->count,
            memmgr_get_free_heap());
    } else {
        if(slot->manager) plugin_manager_free(slot->manager);
        slot->manager = NULL;
        slot->plugin = NULL;
    }
    furi_string_free(path);
    return loaded;
}

static void subghz_plugins_unload_slot(SubGhzPluginSlot* slot) {
    if(slot->manager) plugin_manager_free(slot->manager);
    slot->manager = NULL;
    slot->plugin = NULL;
    slot->loaded = false;
    slot->pinned = false;
}

SubGhzPlugins* subghz_plugins_alloc(const SubGhzProtocolRegistry* base) {
    furi_check(base);
    furi_check(subghz_plugin_bundle_count <= SUBGHZ_PLUGIN_BUNDLES_MAX);
    SubGhzPlugins* instance = malloc(sizeof(SubGhzPlugins));
    memset(instance, 0, sizeof(SubGhzPlugins));
    instance->base = base;
    subghz_plugins_read_disabled(instance);
    subghz_plugins_rebuild_registry(instance);
    return instance;
}

void subghz_plugins_free(SubGhzPlugins* instance) {
    furi_check(instance);
    for(size_t i = 0; i < subghz_plugin_bundle_count; i++) {
        subghz_plugins_unload_slot(&instance->slots[i]);
    }
    free(instance->items);
    free(instance);
}

SubGhzPluginsModulation subghz_plugins_modulation_for_preset(const char* preset_name) {
    // Built-in presets are AM270, AM650, FM238, FM476, FM12K. Custom ones in setting_user are
    // named after their modulation, 2FSK/GFSK/MSK being frequency modulated.
    if(preset_name &&
       (strstr(preset_name, "FM") || strstr(preset_name, "FSK") || strstr(preset_name, "MSK"))) {
        return SubGhzPluginsModulationFM;
    }
    return SubGhzPluginsModulationAM;
}

bool subghz_plugins_select(SubGhzPlugins* instance, SubGhzPluginsModulation modulation) {
    furi_check(instance);
    const SubGhzProtocolFlag wanted_flag =
        modulation == SubGhzPluginsModulationFM ? SubGhzProtocolFlag_FM : SubGhzProtocolFlag_AM;
    bool changed = false;
    instance->skipped = 0;

    // Unload first, so the memory is there for the ones to come
    for(size_t i = 0; i < subghz_plugin_bundle_count; i++) {
        SubGhzPluginSlot* slot = &instance->slots[i];
        slot->wanted = (subghz_plugin_bundles[i].modulation & wanted_flag) != 0;
        if(slot->loaded && !slot->wanted && !slot->pinned) {
            subghz_plugins_unload_slot(slot);
            changed = true;
        }
    }
    for(size_t i = 0; i < subghz_plugin_bundle_count; i++) {
        SubGhzPluginSlot* slot = &instance->slots[i];
        if(!slot->wanted || slot->loaded) continue;
        if(slot->disabled || !subghz_plugins_load_slot(instance, i)) {
            instance->skipped++;
        } else {
            changed = true;
        }
    }
    if(changed) subghz_plugins_rebuild_registry(instance);
    return changed;
}

static bool subghz_plugins_find_bundle(const char* protocol_name, size_t* index) {
    for(size_t i = 0; i < subghz_plugin_bundle_count; i++) {
        const SubGhzPluginBundle* bundle = &subghz_plugin_bundles[i];
        for(size_t j = 0; j < bundle->protocol_count; j++) {
            if(strcmp(bundle->protocol_names[j], protocol_name) == 0) {
                *index = i;
                return true;
            }
        }
    }
    return false;
}

bool subghz_plugins_protocol_needs_load(SubGhzPlugins* instance, const char* protocol_name) {
    furi_check(instance);
    furi_check(protocol_name);
    size_t index;
    return subghz_plugins_find_bundle(protocol_name, &index) && !instance->slots[index].loaded;
}

bool subghz_plugins_ensure_protocol(SubGhzPlugins* instance, const char* protocol_name) {
    furi_check(instance);
    furi_check(protocol_name);
    size_t index;
    if(!subghz_plugins_find_bundle(protocol_name, &index)) return false;
    SubGhzPluginSlot* slot = &instance->slots[index];
    if(slot->loaded) return false;
    // Opening a file is an explicit ask: load it even if the bundle was disabled
    if(!subghz_plugins_load_slot(instance, index)) return false;
    slot->pinned = true;
    subghz_plugins_rebuild_registry(instance);
    return true;
}

const SubGhzProtocolRegistry* subghz_plugins_get_registry(SubGhzPlugins* instance) {
    furi_check(instance);
    return &instance->registry;
}

size_t subghz_plugins_loaded_count(SubGhzPlugins* instance) {
    furi_check(instance);
    size_t n = 0;
    for(size_t i = 0; i < subghz_plugin_bundle_count; i++) {
        if(instance->slots[i].loaded) n++;
    }
    return n;
}

size_t subghz_plugins_skipped_count(SubGhzPlugins* instance) {
    furi_check(instance);
    return instance->skipped;
}
