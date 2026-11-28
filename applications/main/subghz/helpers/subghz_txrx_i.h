#pragma once

#include "subghz_txrx.h"
#include "subghz_plugins.h"

struct SubGhzTxRx {
    SubGhzWorker* worker;

    SubGhzEnvironment* environment;
    SubGhzReceiver* receiver;
    SubGhzTransmitter* transmitter;
    SubGhzProtocolDecoderBase* decoder_result;
    FlipperFormat* fff_data;

    SubGhzRadioPreset* preset;
    SubGhzSetting* setting;

    uint8_t hopper_timeout;
    uint8_t hopper_idx_frequency;
    bool is_database_loaded;
    SubGhzHopperState hopper_state;

    SubGhzTxRxState txrx_state;
    SubGhzSpeakerState speaker_state;
    const SubGhzDevice* radio_device;
    SubGhzRadioDeviceType radio_device_type;

    SubGhzTxRxNeedSaveCallback need_save_callback;
    void* need_save_context;

    bool debug_pin_state;

    // Protocol plugins (NULL in Add Manually, which only needs firmware protocols)
    SubGhzPlugins* plugins;
    SubGhzPluginsModulation plugins_modulation;
    // What the receiver was last given, to give it again when it is allocated again
    SubGhzReceiverCallback rx_callback;
    void* rx_callback_context;
    SubGhzProtocolFlag receiver_filter;
    SubGhzProtocolFilter receiver_ignore_filter;
    bool receiver_filter_set;
    bool receiver_ignore_filter_set;
};
