#pragma once

/**
 * @file subghz_protocol_plugin.h
 * @brief Interface between the Sub-GHz app and its protocol plugins (.fal files on the SD card).
 *
 * A protocol plugin is a bundle of Sub-GHz protocols (decoders, and the encoders that go with
 * them) that is not part of the firmware image. The Sub-GHz app loads the bundles it needs when
 * it starts or when the modulation changes, adds their protocols to the registry the receiver is
 * built from, and unloads them again. See documentation/SubGhzPlugins.md.
 */

#include <flipper_application/flipper_application.h>
#include <lib/subghz/types.h>
#include <lib/subghz/registry.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SUBGHZ_PROTOCOL_PLUGIN_APP_ID      "subghz_protocol_plugin"
#define SUBGHZ_PROTOCOL_PLUGIN_API_VERSION 1

typedef struct {
    /** Protocols in feed order. Entries earlier in the list see a pulse first. */
    const SubGhzProtocol* const* items;
    size_t count;
} SubGhzProtocolPlugin;

/**
 * @brief Define the entry point of a protocol plugin.
 * @param ep_name Entry point name; must match `entry_point` in application.fam
 * @param ... Pointers to the SubGhzProtocol structs of the bundle, in feed order
 */
#define SUBGHZ_PROTOCOL_PLUGIN_DEFINE(ep_name, ...)                       \
    static const SubGhzProtocol* const ep_name##_items[] = {__VA_ARGS__}; \
    static const SubGhzProtocolPlugin ep_name##_plugin = {                \
        .items = ep_name##_items,                                         \
        .count = sizeof(ep_name##_items) / sizeof(ep_name##_items[0]),    \
    };                                                                    \
    static const FlipperAppPluginDescriptor ep_name##_descriptor = {      \
        .appid = SUBGHZ_PROTOCOL_PLUGIN_APP_ID,                           \
        .ep_api_version = SUBGHZ_PROTOCOL_PLUGIN_API_VERSION,             \
        .entry_point = &ep_name##_plugin,                                 \
    };                                                                    \
    const FlipperAppPluginDescriptor* ep_name(void) {                     \
        return &ep_name##_descriptor;                                     \
    }

#ifdef __cplusplus
}
#endif
