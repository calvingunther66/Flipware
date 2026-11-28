#pragma once

#include <lib/subghz/types.h>

/**
 * One row per plugin bundle (one .fal file). Kept next to the loader, in the app, so that
 * finding the bundle of a protocol does not mean loading all of them. The host test
 * tests/host/subghz_plugins checks the rows against the plugin sources.
 */
typedef struct {
    /** Bundle id, as in the app id `subghz_plugin_<id>` and in the disabled list */
    const char* id;
    /** File name in the app's assets, plugins folder */
    const char* file;
    /** Modulation the bundle's protocols are for */
    SubGhzProtocolFlag modulation;
    /** Names of the protocols in the bundle (the `name` of their SubGhzProtocol) */
    const char* const* protocol_names;
    size_t protocol_count;
} SubGhzPluginBundle;

/** Upper bound for the loader's fixed-size slot array */
#define SUBGHZ_PLUGIN_BUNDLES_MAX 8

extern const SubGhzPluginBundle subghz_plugin_bundles[];
extern const size_t subghz_plugin_bundle_count;
