#pragma once

/**
 * @file subghz_plugins.h
 * @brief Loads Sub-GHz protocol plugins (.fal files) and builds the protocol registry the
 * receiver is allocated from. Lives in the app, not in the firmware, so it costs no firmware
 * flash. Design: documentation/SubGhzPlugins.md
 */

#include <lib/subghz/registry.h>
#include <lib/subghz/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SubGhzPlugins SubGhzPlugins;

/** Which plugin bundles are wanted for the radio preset in use. */
typedef enum {
    SubGhzPluginsModulationAM,
    SubGhzPluginsModulationFM,
} SubGhzPluginsModulation;

/**
 * @brief Allocate the plugin set. Nothing is loaded yet.
 * @param base Registry of the protocols built into the firmware; its entries come first
 * @return instance
 */
SubGhzPlugins* subghz_plugins_alloc(const SubGhzProtocolRegistry* base);

/**
 * @brief Unload every plugin and free the instance.
 * The receiver and transmitters built from the registry must be freed before.
 */
void subghz_plugins_free(SubGhzPlugins* instance);

/**
 * @brief Modulation class of a Sub-GHz preset name ("AM650", "FM476", "2FSK ...", custom ones).
 */
SubGhzPluginsModulation subghz_plugins_modulation_for_preset(const char* preset_name);

/**
 * @brief Load the bundles that suit a modulation and unload the ones that do not.
 * Bundles loaded earlier for a single protocol (see subghz_plugins_ensure_protocol) stay.
 * Bundles listed in the disabled list, or that do not fit in the free heap, are skipped.
 * The receiver and transmitters built from the old registry must be freed before.
 * @return true if the registry changed, so the receiver has to be allocated again
 */
bool subghz_plugins_select(SubGhzPlugins* instance, SubGhzPluginsModulation modulation);

/**
 * @brief Make sure the bundle that holds a protocol is loaded (for opening a saved file,
 * whatever the current modulation is). Does nothing for a firmware protocol or an unknown name.
 * @return true if a bundle was loaded, so the receiver has to be allocated again
 */
bool subghz_plugins_ensure_protocol(SubGhzPlugins* instance, const char* protocol_name);

/**
 * @brief Would subghz_plugins_ensure_protocol() load a bundle for this protocol?
 * Use it to know whether the receiver has to be freed before calling it.
 */
bool subghz_plugins_protocol_needs_load(SubGhzPlugins* instance, const char* protocol_name);

/**
 * @brief The registry to give to the environment: firmware protocols, then plugin protocols.
 * The pointer stays valid for the life of the instance; its contents change with the set loaded.
 */
const SubGhzProtocolRegistry* subghz_plugins_get_registry(SubGhzPlugins* instance);

/** @brief Number of plugin bundles loaded now. */
size_t subghz_plugins_loaded_count(SubGhzPlugins* instance);

/** @brief Number of bundles that were wanted but skipped (disabled, missing file, low memory). */
size_t subghz_plugins_skipped_count(SubGhzPlugins* instance);

#ifdef __cplusplus
}
#endif
