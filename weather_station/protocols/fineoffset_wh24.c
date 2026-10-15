#include "fineoffset_wh24.h"

#define TAG "WSProtocolFineOffsetWh24"

/*
 * Help:
 * https://github.com/merbanan/rtl_433/blob/master/src/devices/fineoffset.c
 *
 * Fine Offset Electronics WH24, WH65 and derivatives weather sensor protocol.
 * - Fine Offset WH24 / WH65 (A: 868 MHz, B: 915 MHz, C: 433 MHz) outdoor sensor array
 * - also Misol WS2320 (rebranded WH65) and Fine Offset WS69
 *
 * The sensor sends a package every ~16 s, ~11 ms wide.
 * The bits are PCM (NRZ) modulated with Frequency Shift Keying,
 * one bit lasts 58 us. Use one of the FM presets to receive it.
 *
 * - Preamble:  aa aa aa aa aa
 * - Sync word: 2d d4
 * - Payload:   FF II DD VT TT HH WW GG RR RR UU UU LL LL LL CC BB
 *
 * - F: 8 bit Family Code, fixed 0x24
 * - I: 8 bit Sensor ID, set on battery change
 * - D: 8 bit Wind direction
 * - V: 4 bit Various bits, wind dir 9th bit, wind speed 9th bit
 * - B: 1 bit low battery indicator
 * - T: 11 bit Temperature (+40*10)
 * - H: 8 bit Humidity
 * - W: 8 bit Wind speed
 * - G: 8 bit Gust speed
 * - R: 16 bit rainfall counter
 * - U: 16 bit UV value
 * - L: 24 bit light value
 * - C: 8 bit CRC-8 (poly 0x31) of the 15 data bytes
 * - B: 8 bit sum of the 16 data bytes
 *
 * The WH24 and WH65 send the same payload and differ only in preamble length,
 * but use different scaling: wind speed factor 1.12 m/s for WH24 and 0.51 m/s for
 * WH65, rain cup 0.3 mm for WH24 and 0.254 mm for WH65. The WH65 scaling is used here.
 *
 * Bytes DD..RR (8 bytes) are kept in the 64 bit data field so wind and rain
 * can be shown again after the record is loaded from history.
 */

#define FINEOFFSET_WH24_BIT_US          58
#define FINEOFFSET_WH24_BIT_MIN_US      50
#define FINEOFFSET_WH24_BIT_MAX_US      66
#define FINEOFFSET_WH24_MAX_RUN_BITS    64
#define FINEOFFSET_WH24_SYNC            0xAA2DD4
#define FINEOFFSET_WH24_SYNC_MASK       0xFFFFFF
#define FINEOFFSET_WH24_PACKET_BYTES    17
#define FINEOFFSET_WH24_FAMILY_CODE     0x24
#define FINEOFFSET_WH24_WIND_FACTOR     0.51f
#define FINEOFFSET_WH24_RAIN_MM_PER_TIP 0.254f

static const SubGhzBlockConst ws_protocol_fineoffset_wh24_const = {
    .te_short = FINEOFFSET_WH24_BIT_US,
    .te_long = FINEOFFSET_WH24_BIT_US,
    .te_delta = FINEOFFSET_WH24_BIT_US / 2,
    .min_count_bit_for_found = FINEOFFSET_WH24_PACKET_BYTES * 8,
};

struct WSProtocolDecoderFineOffsetWh24 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    WSBlockGeneric generic;

    uint32_t sync_shift;
    bool inverted;
    uint8_t packet[FINEOFFSET_WH24_PACKET_BYTES];
};

struct WSProtocolEncoderFineOffsetWh24 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    WSBlockGeneric generic;
};

const SubGhzProtocolDecoder ws_protocol_fineoffset_wh24_decoder = {
    .alloc = ws_protocol_decoder_fineoffset_wh24_alloc,
    .free = ws_protocol_decoder_fineoffset_wh24_free,

    .feed = ws_protocol_decoder_fineoffset_wh24_feed,
    .reset = ws_protocol_decoder_fineoffset_wh24_reset,

    .get_hash_data = ws_protocol_decoder_fineoffset_wh24_get_hash_data,
    .serialize = ws_protocol_decoder_fineoffset_wh24_serialize,
    .deserialize = ws_protocol_decoder_fineoffset_wh24_deserialize,
    .get_string = ws_protocol_decoder_fineoffset_wh24_get_string,
};

const SubGhzProtocolEncoder ws_protocol_fineoffset_wh24_encoder = {
    .alloc = NULL,
    .free = NULL,

    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

const SubGhzProtocol ws_protocol_fineoffset_wh24 = {
    .name = WS_PROTOCOL_FINEOFFSET_WH24_NAME,
    .type = SubGhzProtocolWeatherStation,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 | SubGhzProtocolFlag_FM |
            SubGhzProtocolFlag_Decodable,

    .decoder = &ws_protocol_fineoffset_wh24_decoder,
    .encoder = &ws_protocol_fineoffset_wh24_encoder,
};

typedef enum {
    FineOffsetWh24DecoderStepSync = 0,
    FineOffsetWh24DecoderStepData,
} FineOffsetWh24DecoderStep;

void* ws_protocol_decoder_fineoffset_wh24_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    WSProtocolDecoderFineOffsetWh24* instance = malloc(sizeof(WSProtocolDecoderFineOffsetWh24));
    instance->base.protocol = &ws_protocol_fineoffset_wh24;
    instance->generic.protocol_name = instance->base.protocol->name;
    ws_protocol_decoder_fineoffset_wh24_reset(instance);
    return instance;
}

void ws_protocol_decoder_fineoffset_wh24_free(void* context) {
    furi_assert(context);
    WSProtocolDecoderFineOffsetWh24* instance = context;
    free(instance);
}

void ws_protocol_decoder_fineoffset_wh24_reset(void* context) {
    furi_assert(context);
    WSProtocolDecoderFineOffsetWh24* instance = context;
    instance->decoder.parser_step = FineOffsetWh24DecoderStepSync;
    instance->decoder.decode_count_bit = 0;
    instance->decoder.te_last = ws_protocol_fineoffset_wh24_const.te_short;
    instance->sync_shift = 0;
}

static bool ws_protocol_fineoffset_wh24_check(const uint8_t* packet) {
    if(packet[0] != FINEOFFSET_WH24_FAMILY_CODE) return false;
    // CRC over the 15 data bytes plus the CRC byte itself is zero
    if(subghz_protocol_blocks_crc8(packet, 16, 0x31, 0x00) != 0) return false;
    return subghz_protocol_blocks_add_bytes(packet, 16) == packet[16];
}

/**
 * Analysis of received data
 * @param instance Pointer to a WSBlockGeneric* instance
 * @param packet Received payload, FINEOFFSET_WH24_PACKET_BYTES long
 */
static void
    ws_protocol_fineoffset_wh24_extract_data(WSBlockGeneric* instance, const uint8_t* packet) {
    instance->data = 0;
    for(uint8_t i = 2; i < 10; i++) {
        instance->data = (instance->data << 8) | packet[i];
    }
    instance->data_count_bit = ws_protocol_fineoffset_wh24_const.min_count_bit_for_found;

    instance->id = packet[1];
    instance->battery_low = (packet[3] >> 3) & 0x1;
    instance->channel = WS_NO_CHANNEL;
    instance->btn = WS_NO_BTN;

    uint16_t temp_raw = ((packet[3] & 0x07) << 8) | packet[4];
    if(temp_raw == 0x7ff) {
        instance->temp = WS_NO_TEMPERATURE;
    } else {
        instance->temp = ((float)temp_raw - 400.0f) / 10.0f;
    }

    // 0xff means no reading, same value as WS_NO_HUMIDITY
    instance->humidity = packet[5];
}

static void
    ws_protocol_fineoffset_wh24_push_bit(WSProtocolDecoderFineOffsetWh24* instance, bool bit) {
    switch(instance->decoder.parser_step) {
    case FineOffsetWh24DecoderStepSync:
        instance->sync_shift = ((instance->sync_shift << 1) | bit) & FINEOFFSET_WH24_SYNC_MASK;
        // The FSK polarity depends on the receiver, accept both
        if(instance->sync_shift == FINEOFFSET_WH24_SYNC) {
            instance->inverted = false;
        } else if(instance->sync_shift == (~FINEOFFSET_WH24_SYNC & FINEOFFSET_WH24_SYNC_MASK)) {
            instance->inverted = true;
        } else {
            break;
        }
        memset(instance->packet, 0, sizeof(instance->packet));
        instance->decoder.decode_count_bit = 0;
        instance->decoder.parser_step = FineOffsetWh24DecoderStepData;
        break;

    case FineOffsetWh24DecoderStepData:
        if(bit ^ instance->inverted) {
            instance->packet[instance->decoder.decode_count_bit / 8] |=
                1 << (7 - (instance->decoder.decode_count_bit % 8));
        }
        instance->decoder.decode_count_bit++;

        if(instance->decoder.decode_count_bit ==
           ws_protocol_fineoffset_wh24_const.min_count_bit_for_found) {
            if(ws_protocol_fineoffset_wh24_check(instance->packet)) {
                ws_protocol_fineoffset_wh24_extract_data(&instance->generic, instance->packet);

                if(instance->base.callback) {
                    instance->base.callback(&instance->base, instance->base.context);
                }
            }
            instance->sync_shift = 0;
            instance->decoder.parser_step = FineOffsetWh24DecoderStepSync;
        }
        break;
    }
}

void ws_protocol_decoder_fineoffset_wh24_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    WSProtocolDecoderFineOffsetWh24* instance = context;

    // NRZ: each level lasts a whole number of bit periods.
    // te_last tracks the measured bit period, the preamble is a run of single bits.
    uint32_t bits = (duration + instance->decoder.te_last / 2) / instance->decoder.te_last;

    if(bits == 0 || bits > FINEOFFSET_WH24_MAX_RUN_BITS) {
        ws_protocol_decoder_fineoffset_wh24_reset(instance);
        return;
    }

    if(bits == 1) {
        uint32_t te = (instance->decoder.te_last * 3 + duration) / 4;
        instance->decoder.te_last =
            CLAMP(te, FINEOFFSET_WH24_BIT_MAX_US, FINEOFFSET_WH24_BIT_MIN_US);
    }

    for(uint32_t i = 0; i < bits; i++) {
        ws_protocol_fineoffset_wh24_push_bit(instance, level);
    }
}

uint8_t ws_protocol_decoder_fineoffset_wh24_get_hash_data(void* context) {
    furi_assert(context);
    WSProtocolDecoderFineOffsetWh24* instance = context;
    uint8_t hash = 0;
    for(size_t i = 0; i < sizeof(instance->packet); i++) {
        hash ^= instance->packet[i];
    }
    return hash;
}

SubGhzProtocolStatus ws_protocol_decoder_fineoffset_wh24_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    WSProtocolDecoderFineOffsetWh24* instance = context;
    return ws_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus
    ws_protocol_decoder_fineoffset_wh24_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    WSProtocolDecoderFineOffsetWh24* instance = context;
    return ws_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        ws_protocol_fineoffset_wh24_const.min_count_bit_for_found);
}

void ws_protocol_decoder_fineoffset_wh24_get_string(void* context, FuriString* output) {
    furi_assert(context);
    WSProtocolDecoderFineOffsetWh24* instance = context;

    // data holds payload bytes DD VT TT HH WW GG RR RR
    uint64_t data = instance->generic.data;
    uint8_t various = (data >> 48) & 0xff;
    uint16_t wind_dir = ((data >> 56) & 0xff) | ((various & 0x80) << 1);
    uint16_t wind_raw = ((data >> 24) & 0xff) | ((various & 0x10) << 4);
    uint8_t gust_raw = (data >> 16) & 0xff;
    uint16_t rain_raw = data & 0xffff;

    float wind = wind_raw * 0.125f * FINEOFFSET_WH24_WIND_FACTOR;
    float gust = gust_raw * FINEOFFSET_WH24_WIND_FACTOR;
    float rain = rain_raw * FINEOFFSET_WH24_RAIN_MM_PER_TIP;

    furi_string_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%lX%08lX\r\n"
        "Sn:0x%lX Bat:%d\r\n"
        "Temp:%3.1f C Hum:%d%%\r\n"
        "Wind:%3.1f Gust:%3.1f m/s\r\n"
        "Dir:%d deg Rain:%3.1f mm",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(data >> 32),
        (uint32_t)(data),
        instance->generic.id,
        instance->generic.battery_low,
        (double)instance->generic.temp,
        instance->generic.humidity,
        (double)wind,
        (double)gust,
        wind_dir,
        (double)rain);
}
