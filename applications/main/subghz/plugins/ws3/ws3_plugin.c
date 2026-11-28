#include "../subghz_protocol_plugin.h"

#include "solight_te44.h"
#include "auriol_hg0601a.h"
#include "oregon_v1.h"
#include "tx_8300.h"
#include "wendox_w6726.h"
#include "auriol_ahfl.h"
#include "kedsum_th.h"
#include "emos_e601x.h"
#include "acurite_5n1.h"
#include "vauno_en8822c.h"

// Weather stations, part 3 of 3. Keep the order: Solight TE44 has to see pulses before Auriol.
SUBGHZ_PROTOCOL_PLUGIN_DEFINE(
    subghz_ws3_plugin_ep,
    &ws_protocol_solight_te44,
    &ws_protocol_auriol_th,
    &ws_protocol_oregon_v1,
    &ws_protocol_tx_8300,
    &ws_protocol_wendox_w6726,
    &ws_protocol_auriol_ahfl,
    &ws_protocol_kedsum_th,
    &ws_protocol_emose601x,
    &ws_protocol_acurite_5n1,
    &ws_protocol_vauno_en8822c)
