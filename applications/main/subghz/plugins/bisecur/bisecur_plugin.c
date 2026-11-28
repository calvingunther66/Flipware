#include "../subghz_protocol_plugin.h"

#include "hormann_bisecur.h"

// Hormann BiSecur garage doors (868 MHz, FM).
SUBGHZ_PROTOCOL_PLUGIN_DEFINE(subghz_bisecur_plugin_ep, &subghz_protocol_hormann_bisecur)
