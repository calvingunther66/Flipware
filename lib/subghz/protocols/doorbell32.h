#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_DOORBELL32_NAME "Doorbell32"

typedef struct SubGhzProtocolDecoderDoorbell32 SubGhzProtocolDecoderDoorbell32;
typedef struct SubGhzProtocolEncoderDoorbell32 SubGhzProtocolEncoderDoorbell32;

extern const SubGhzProtocolDecoder subghz_protocol_doorbell32_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_doorbell32_encoder;
extern const SubGhzProtocol subghz_protocol_doorbell32;

/**
 * Allocate SubGhzProtocolEncoderDoorbell32.
 * @param environment Pointer to a SubGhzEnvironment instance
 * @return SubGhzProtocolEncoderDoorbell32* pointer to a SubGhzProtocolEncoderDoorbell32 instance
 */
void* subghz_protocol_encoder_doorbell32_alloc(SubGhzEnvironment* environment);

/**
 * Free SubGhzProtocolEncoderDoorbell32.
 * @param context Pointer to a SubGhzProtocolEncoderDoorbell32 instance
 */
void subghz_protocol_encoder_doorbell32_free(void* context);

/**
 * Deserialize and generating an upload to send.
 * @param context Pointer to a SubGhzProtocolEncoderDoorbell32 instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @return status
 */
SubGhzProtocolStatus
    subghz_protocol_encoder_doorbell32_deserialize(void* context, FlipperFormat* flipper_format);

/**
 * Forced transmission stop.
 * @param context Pointer to a SubGhzProtocolEncoderDoorbell32 instance
 */
void subghz_protocol_encoder_doorbell32_stop(void* context);

/**
 * Getting the level and duration of the upload to be loaded into DMA.
 * @param context Pointer to a SubGhzProtocolEncoderDoorbell32 instance
 * @return LevelDuration 
 */
LevelDuration subghz_protocol_encoder_doorbell32_yield(void* context);

/**
 * Allocate SubGhzProtocolDecoderDoorbell32.
 * @param environment Pointer to a SubGhzEnvironment instance
 * @return SubGhzProtocolDecoderDoorbell32* pointer to a SubGhzProtocolDecoderDoorbell32 instance
 */
void* subghz_protocol_decoder_doorbell32_alloc(SubGhzEnvironment* environment);

/**
 * Free SubGhzProtocolDecoderDoorbell32.
 * @param context Pointer to a SubGhzProtocolDecoderDoorbell32 instance
 */
void subghz_protocol_decoder_doorbell32_free(void* context);

/**
 * Reset decoder SubGhzProtocolDecoderDoorbell32.
 * @param context Pointer to a SubGhzProtocolDecoderDoorbell32 instance
 */
void subghz_protocol_decoder_doorbell32_reset(void* context);

/**
 * Parse a raw sequence of levels and durations received from the air.
 * @param context Pointer to a SubGhzProtocolDecoderDoorbell32 instance
 * @param level Signal level true-high false-low
 * @param duration Duration of this level in, us
 */
void subghz_protocol_decoder_doorbell32_feed(void* context, bool level, uint32_t duration);

/**
 * Getting the hash sum of the last randomly received parcel.
 * @param context Pointer to a SubGhzProtocolDecoderDoorbell32 instance
 * @return hash Hash sum
 */
uint8_t subghz_protocol_decoder_doorbell32_get_hash_data(void* context);

/**
 * Serialize data SubGhzProtocolDecoderDoorbell32.
 * @param context Pointer to a SubGhzProtocolDecoderDoorbell32 instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @param preset The modulation on which the signal was received, SubGhzRadioPreset
 * @return status
 */
SubGhzProtocolStatus subghz_protocol_decoder_doorbell32_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

/**
 * Deserialize data SubGhzProtocolDecoderDoorbell32.
 * @param context Pointer to a SubGhzProtocolDecoderDoorbell32 instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @return status
 */
SubGhzProtocolStatus
    subghz_protocol_decoder_doorbell32_deserialize(void* context, FlipperFormat* flipper_format);

/**
 * Getting a textual representation of the received data.
 * @param context Pointer to a SubGhzProtocolDecoderDoorbell32 instance
 * @param output Resulting text
 */
void subghz_protocol_decoder_doorbell32_get_string(void* context, FuriString* output);
