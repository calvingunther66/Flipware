#include "../subghz_protocol_plugin.h"

#include "schrader_gg4.h"

// Tyre pressure sensors.
SUBGHZ_PROTOCOL_PLUGIN_DEFINE(subghz_tpms_plugin_ep, &tpms_protocol_schrader_gg4)
