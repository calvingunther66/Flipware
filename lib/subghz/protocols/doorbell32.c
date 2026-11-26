#include "doorbell32.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

// Ported from Unleashed firmware (unlshd-094).

#define TAG "SubGhzProtocolDoorbell32"

// Princeton style 1:3 PWM, but 32 bits and a 14*Te guard instead of 24 and 31*Te, so the
// Princeton decoder syncs on the guard and then drops the frame on the bit count. Te is
// measured from the signal and saved, it differs between units.
#define DOORBELL32_GUARD_TIME 14u

static const SubGhzBlockConst subghz_protocol_doorbell32_const = {
    .te_short = 515,
    .te_long = 1545,
    .te_delta = 200,
    .min_count_bit_for_found = 32,
};

struct SubGhzProtocolDecoderDoorbell32 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t te;
    uint32_t last_data;
};

struct SubGhzProtocolEncoderDoorbell32 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint32_t te;
};

typedef enum {
    Doorbell32DecoderStepReset = 0,
    Doorbell32DecoderStepSaveDuration,
    Doorbell32DecoderStepCheckDuration,
} Doorbell32DecoderStep;

const SubGhzProtocolDecoder subghz_protocol_doorbell32_decoder = {
    .alloc = subghz_protocol_decoder_doorbell32_alloc,
    .free = subghz_protocol_decoder_doorbell32_free,

    .feed = subghz_protocol_decoder_doorbell32_feed,
    .reset = subghz_protocol_decoder_doorbell32_reset,

    .get_hash_data = subghz_protocol_decoder_doorbell32_get_hash_data,
    .serialize = subghz_protocol_decoder_doorbell32_serialize,
    .deserialize = subghz_protocol_decoder_doorbell32_deserialize,
    .get_string = subghz_protocol_decoder_doorbell32_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_doorbell32_encoder = {
    .alloc = subghz_protocol_encoder_doorbell32_alloc,
    .free = subghz_protocol_encoder_doorbell32_free,

    .deserialize = subghz_protocol_encoder_doorbell32_deserialize,
    .stop = subghz_protocol_encoder_doorbell32_stop,
    .yield = subghz_protocol_encoder_doorbell32_yield,
};

const SubGhzProtocol subghz_protocol_doorbell32 = {
    .name = SUBGHZ_PROTOCOL_DOORBELL32_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_doorbell32_decoder,
    .encoder = &subghz_protocol_doorbell32_encoder,

    .filter = SubGhzProtocolFilter_Sensors,
};

void* subghz_protocol_encoder_doorbell32_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderDoorbell32* instance = malloc(sizeof(SubGhzProtocolEncoderDoorbell32));

    instance->base.protocol = &subghz_protocol_doorbell32;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 10;
    instance->encoder.size_upload = 68;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_doorbell32_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderDoorbell32* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

/**
 * Generating an upload from data.
 * @param instance Pointer to a SubGhzProtocolEncoderDoorbell32 instance
 */
static void
    subghz_protocol_encoder_doorbell32_get_upload(SubGhzProtocolEncoderDoorbell32* instance) {
    furi_assert(instance);

    size_t index = 0;
    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if(bit_read(instance->generic.data, i - 1)) {
            instance->encoder.upload[index++] = level_duration_make(true, instance->te * 3);
            instance->encoder.upload[index++] = level_duration_make(false, instance->te);
        } else {
            instance->encoder.upload[index++] = level_duration_make(true, instance->te);
            instance->encoder.upload[index++] = level_duration_make(false, instance->te * 3);
        }
    }
    // closing pulse, then the guard that separates the repeats
    instance->encoder.upload[index++] = level_duration_make(true, instance->te);
    instance->encoder.upload[index++] =
        level_duration_make(false, instance->te * DOORBELL32_GUARD_TIME);

    instance->encoder.size_upload = index;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_doorbell32_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderDoorbell32* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_doorbell32_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) break;

        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        if(!flipper_format_read_uint32(flipper_format, "TE", &instance->te, 1)) {
            instance->te = subghz_protocol_doorbell32_const.te_short;
        }
        // Optional value
        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        subghz_protocol_encoder_doorbell32_get_upload(instance);
        instance->encoder.is_running = true;
    } while(false);

    return ret;
}

void subghz_protocol_encoder_doorbell32_stop(void* context) {
    SubGhzProtocolEncoderDoorbell32* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_doorbell32_yield(void* context) {
    SubGhzProtocolEncoderDoorbell32* instance = context;

    if(instance->encoder.repeat == 0 || !instance->encoder.is_running) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }

    LevelDuration ret = instance->encoder.upload[instance->encoder.front];

    if(++instance->encoder.front == instance->encoder.size_upload) {
        if(!subghz_block_generic_global.endless_tx) instance->encoder.repeat--;
        instance->encoder.front = 0;
    }

    return ret;
}

void* subghz_protocol_decoder_doorbell32_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderDoorbell32* instance = malloc(sizeof(SubGhzProtocolDecoderDoorbell32));
    instance->base.protocol = &subghz_protocol_doorbell32;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_doorbell32_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderDoorbell32* instance = context;
    free(instance);
}

void subghz_protocol_decoder_doorbell32_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderDoorbell32* instance = context;
    instance->decoder.parser_step = Doorbell32DecoderStepReset;
}

void subghz_protocol_decoder_doorbell32_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderDoorbell32* instance = context;
    const SubGhzBlockConst* c = &subghz_protocol_doorbell32_const;

    switch(instance->decoder.parser_step) {
    case Doorbell32DecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, c->te_short * DOORBELL32_GUARD_TIME) <
                        c->te_delta * DOORBELL32_GUARD_TIME / 2)) {
            instance->decoder.parser_step = Doorbell32DecoderStepSaveDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->te = 0;
        }
        break;
    case Doorbell32DecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->te += duration;
            instance->decoder.parser_step = Doorbell32DecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = Doorbell32DecoderStepReset;
        }
        break;
    case Doorbell32DecoderStepCheckDuration:
        if(!level) {
            if(duration >= ((uint32_t)c->te_long * 2)) {
                // guard reached, the frame ends here
                if((instance->decoder.decode_count_bit == c->min_count_bit_for_found) &&
                   (instance->last_data == instance->decoder.decode_data) && instance->last_data) {
                    instance->te /= (instance->decoder.decode_count_bit * 4 + 1);
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->last_data = instance->decoder.decode_data;
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->te = 0;
                instance->decoder.parser_step = Doorbell32DecoderStepSaveDuration;
                break;
            }
            instance->te += duration;
            // Bit 0 is short HIGH (te_last) and long LOW, bit 1 is the other way round
            if((DURATION_DIFF(instance->decoder.te_last, c->te_short) < c->te_delta) &&
               (DURATION_DIFF(duration, c->te_long) < c->te_delta * 2)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = Doorbell32DecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, c->te_long) < c->te_delta * 2) &&
                (DURATION_DIFF(duration, c->te_short) < c->te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = Doorbell32DecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = Doorbell32DecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = Doorbell32DecoderStepReset;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_doorbell32_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderDoorbell32* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_doorbell32_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderDoorbell32* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if((ret == SubGhzProtocolStatusOk) &&
       !flipper_format_write_uint32(flipper_format, "TE", &instance->te, 1)) {
        FURI_LOG_E(TAG, "Unable to add TE");
        ret = SubGhzProtocolStatusErrorParserTe;
    }
    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_doorbell32_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderDoorbell32* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_doorbell32_const.min_count_bit_for_found);
    if(ret == SubGhzProtocolStatusOk) {
        // Optional, so a loaded file shows the Te it was captured with
        if(!flipper_format_rewind(flipper_format) ||
           !flipper_format_read_uint32(flipper_format, "TE", &instance->te, 1)) {
            instance->te = subghz_protocol_doorbell32_const.te_short;
        }
    }
    return ret;
}

void subghz_protocol_decoder_doorbell32_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderDoorbell32* instance = context;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%08lX\r\n"
        "Te:%luus  GT:Te*%u",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data & 0xFFFFFFFF),
        instance->te,
        DOORBELL32_GUARD_TIME);
}
