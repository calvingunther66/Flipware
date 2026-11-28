#include "subghz_plugins_table.h"

#include <furi.h>

// Protocol names are the strings of the *_NAME macros in plugins/<bundle>/*.h.
static const char* const ws1_names[] = {
    "inFactory-TH",
    "ThermoPRO-TX4",
    "Nexus-TH",
    "GT-WT02",
    "GT-WT03",
    "Acurite-606TX",
    "Acurite-609TXC",
    "Acurite-986",
    "Bresser-3CH",
};
static const char* const ws2_names[] = {
    "LaCrosse_TX",
    "TX141THBv2",
    "Oregon2",
    "Oregon3",
    "Acurite 592TXR",
    "Ambient_Weather",
};
static const char* const ws3_names[] = {
    "Solight TE44",
    "Auriol HG06061",
    "Oregon-v1",
    "TX8300",
    "Wendox W6726",
    "Auriol AHFL",
    "Kedsum-TH",
    "EMOS E601x",
    "Acurite 5n1",
    "Vauno-EN8822C",
};
static const char* const tpms_names[] = {"Schrader GG4"};
static const char* const pager_names[] = {"POCSAG"};
static const char* const x10_names[] = {"X10"};
static const char* const bisecur_names[] = {"Hormann BiSecur"};

// Order is the order bundles are added to the registry, and so the order decoders see pulses:
// weather part 1 has to come before part 2 (Bresser 3CH before LaCrosse).
const SubGhzPluginBundle subghz_plugin_bundles[] = {
    {"ws1", "subghz_plugin_ws1.fal", SubGhzProtocolFlag_AM, ws1_names, COUNT_OF(ws1_names)},
    {"ws2", "subghz_plugin_ws2.fal", SubGhzProtocolFlag_AM, ws2_names, COUNT_OF(ws2_names)},
    {"ws3", "subghz_plugin_ws3.fal", SubGhzProtocolFlag_AM, ws3_names, COUNT_OF(ws3_names)},
    {"tpms", "subghz_plugin_tpms.fal", SubGhzProtocolFlag_AM, tpms_names, COUNT_OF(tpms_names)},
    {"x10", "subghz_plugin_x10.fal", SubGhzProtocolFlag_AM, x10_names, COUNT_OF(x10_names)},
    {"pager", "subghz_plugin_pager.fal", SubGhzProtocolFlag_FM, pager_names, COUNT_OF(pager_names)},
    {"bisecur",
     "subghz_plugin_bisecur.fal",
     SubGhzProtocolFlag_FM,
     bisecur_names,
     COUNT_OF(bisecur_names)},
};

const size_t subghz_plugin_bundle_count = COUNT_OF(subghz_plugin_bundles);
