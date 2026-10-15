#pragma once

#include <lib/subghz/protocols/base.h>

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include "ws_generic.h"
#include <lib/subghz/blocks/math.h>

#define WS_PROTOCOL_FINEOFFSET_WH24_NAME "FineOffset-WH24"

typedef struct WSProtocolDecoderFineOffsetWh24 WSProtocolDecoderFineOffsetWh24;
typedef struct WSProtocolEncoderFineOffsetWh24 WSProtocolEncoderFineOffsetWh24;

extern const SubGhzProtocolDecoder ws_protocol_fineoffset_wh24_decoder;
extern const SubGhzProtocolEncoder ws_protocol_fineoffset_wh24_encoder;
extern const SubGhzProtocol ws_protocol_fineoffset_wh24;

/**
 * Allocate WSProtocolDecoderFineOffsetWh24.
 * @param environment Pointer to a SubGhzEnvironment instance
 * @return WSProtocolDecoderFineOffsetWh24* pointer to a WSProtocolDecoderFineOffsetWh24 instance
 */
void* ws_protocol_decoder_fineoffset_wh24_alloc(SubGhzEnvironment* environment);

/**
 * Free WSProtocolDecoderFineOffsetWh24.
 * @param context Pointer to a WSProtocolDecoderFineOffsetWh24 instance
 */
void ws_protocol_decoder_fineoffset_wh24_free(void* context);

/**
 * Reset decoder WSProtocolDecoderFineOffsetWh24.
 * @param context Pointer to a WSProtocolDecoderFineOffsetWh24 instance
 */
void ws_protocol_decoder_fineoffset_wh24_reset(void* context);

/**
 * Parse a raw sequence of levels and durations received from the air.
 * @param context Pointer to a WSProtocolDecoderFineOffsetWh24 instance
 * @param level Signal level true-high false-low
 * @param duration Duration of this level in, us
 */
void ws_protocol_decoder_fineoffset_wh24_feed(void* context, bool level, uint32_t duration);

/**
 * Getting the hash sum of the last randomly received parcel.
 * @param context Pointer to a WSProtocolDecoderFineOffsetWh24 instance
 * @return hash Hash sum
 */
uint8_t ws_protocol_decoder_fineoffset_wh24_get_hash_data(void* context);

/**
 * Serialize data WSProtocolDecoderFineOffsetWh24.
 * @param context Pointer to a WSProtocolDecoderFineOffsetWh24 instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @param preset The modulation on which the signal was received, SubGhzRadioPreset
 * @return status
 */
SubGhzProtocolStatus ws_protocol_decoder_fineoffset_wh24_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

/**
 * Deserialize data WSProtocolDecoderFineOffsetWh24.
 * @param context Pointer to a WSProtocolDecoderFineOffsetWh24 instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @return status
 */
SubGhzProtocolStatus
    ws_protocol_decoder_fineoffset_wh24_deserialize(void* context, FlipperFormat* flipper_format);

/**
 * Getting a textual representation of the received data.
 * @param context Pointer to a WSProtocolDecoderFineOffsetWh24 instance
 * @param output Resulting text
 */
void ws_protocol_decoder_fineoffset_wh24_get_string(void* context, FuriString* output);
