#include "../subghz_protocol_plugin.h"

#include "infactory.h"
#include "thermopro_tx4.h"
#include "nexus_th.h"
#include "gt_wt_02.h"
#include "gt_wt_03.h"
#include "acurite_606tx.h"
#include "acurite_609txc.h"
#include "acurite_986.h"
#include "bresser_3ch.h"

// Weather stations, part 1 of 3. Keep the order: Bresser 3CH has to see pulses before LaCrosse.
SUBGHZ_PROTOCOL_PLUGIN_DEFINE(
    subghz_ws1_plugin_ep,
    &ws_protocol_infactory,
    &ws_protocol_thermopro_tx4,
    &ws_protocol_nexus_th,
    &ws_protocol_gt_wt_02,
    &ws_protocol_gt_wt_03,
    &ws_protocol_acurite_606tx,
    &ws_protocol_acurite_609txc,
    &ws_protocol_acurite_986,
    &ws_protocol_bresser_3ch)
