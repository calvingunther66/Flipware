#include "../subghz_protocol_plugin.h"

#include "lacrosse_tx.h"
#include "lacrosse_tx141thbv2.h"
#include "oregon2.h"
#include "oregon3.h"
#include "acurite_592txr.h"
#include "ambient_weather.h"

// Weather stations, part 2 of 3. Loaded after part 1 (Bresser 3CH has to come before LaCrosse).
SUBGHZ_PROTOCOL_PLUGIN_DEFINE(
    subghz_ws2_plugin_ep,
    &ws_protocol_lacrosse_tx,
    &ws_protocol_lacrosse_tx141thbv2,
    &ws_protocol_oregon2,
    &ws_protocol_oregon3,
    &ws_protocol_acurite_592txr,
    &ws_protocol_ambient_weather)
