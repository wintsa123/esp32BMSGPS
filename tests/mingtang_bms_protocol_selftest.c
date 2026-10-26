#include "esp_bms_mingtang_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void put_u16_be(uint8_t *data, size_t offset, uint16_t value)
{
    data[offset] = (uint8_t)(value >> 8U);
    data[offset + 1U] = (uint8_t)(value & 0xFFU);
}

static void put_u32_be(uint8_t *data, size_t offset, uint32_t value)
{
    data[offset] = (uint8_t)(value >> 24U);
    data[offset + 1U] = (uint8_t)(value >> 16U);
    data[offset + 2U] = (uint8_t)(value >> 8U);
    data[offset + 3U] = (uint8_t)(value & 0xFFU);
}

/* Appends one TLV entry and returns the new stream length. */
static size_t put_tlv(uint8_t *frame, size_t position, uint16_t tag, const uint8_t *value,
                      size_t value_len)
{
    put_u16_be(frame, position, tag);
    frame[position + 2U] = (uint8_t)value_len;
    if (value_len > 0U) {
        memcpy(&frame[position + 3U], value, value_len);
    }
    return position + 3U + value_len;
}

/* Builds a vendor frame: header, TLVs, length field, trailing byte. */
static size_t build_frame(uint8_t *frame, uint8_t type, size_t tlv_end)
{
    frame[0] = type;
    frame[1] = 0x11U;
    frame[2] = 0x00U;
    frame[3] = 0x00U;
    put_u16_be(frame, 4U, (uint16_t)tlv_end);
    frame[tlv_end] = 0x00U;
    return tlv_end + 1U;
}

static size_t build_state_frame(uint8_t *frame)
{
    memset(frame, 0, ESP_BMS_MINGTANG_STREAM_CAPACITY);
    size_t position = 6U;
    uint8_t bytes[8];

    bytes[0] = 16U;
    position = put_tlv(frame, position, 3U, bytes, 1U);

    /* 16 cells between 3.300 V and 3.330 V. */
    uint8_t cells[32];
    for (size_t index = 0U; index < 16U; ++index) {
        put_u16_be(cells, index * 2U, (uint16_t)(3300U + index * 2U));
    }
    position = put_tlv(frame, position, 4U, cells, sizeof(cells));

    put_u16_be(bytes, 0U, 3330U);
    position = put_tlv(frame, position, 5U, bytes, 2U);
    put_u16_be(bytes, 0U, 3300U);
    position = put_tlv(frame, position, 6U, bytes, 2U);
    put_u16_be(bytes, 0U, 30U);
    position = put_tlv(frame, position, 7U, bytes, 2U);

    put_u16_be(bytes, 0U, 5320U); /* 53.20 V */
    position = put_tlv(frame, position, 10U, bytes, 2U);

    put_u32_be(bytes, 0U, (uint32_t)12500); /* 12.500 A charging */
    position = put_tlv(frame, position, 13U, bytes, 4U);
    put_u32_be(bytes, 0U, (uint32_t)500); /* 0.500 A discharging */
    position = put_tlv(frame, position, 15U, bytes, 4U);

    bytes[0] = 0U;
    position = put_tlv(frame, position, 17U, bytes, 1U);
    position = put_tlv(frame, position, 19U, bytes, 1U);
    position = put_tlv(frame, position, 21U, bytes, 1U);
    bytes[0] = 1U;
    position = put_tlv(frame, position, 24U, bytes, 1U);
    put_u32_be(bytes, 0U, 0x00000005U);
    position = put_tlv(frame, position, 25U, bytes, 4U);

    put_u16_be(bytes, 0U, (uint16_t)3150); /* 31.50 degC MOS */
    position = put_tlv(frame, position, 27U, bytes, 2U);
    put_u16_be(bytes, 0U, (uint16_t)2875); /* 28.75 degC BAT1 */
    position = put_tlv(frame, position, 28U, bytes, 2U);
    put_u16_be(bytes, 0U, (uint16_t)(int16_t)-1550); /* -15.50 degC BAT2 */
    position = put_tlv(frame, position, 29U, bytes, 2U);
    put_u16_be(bytes, 0U, (uint16_t)30000); /* vendor "invalid" sentinel */
    position = put_tlv(frame, position, 30U, bytes, 2U);

    put_u32_be(bytes, 0U, (uint32_t)100000); /* 100.000 Ah */
    position = put_tlv(frame, position, 36U, bytes, 4U);
    put_u32_be(bytes, 0U, (uint32_t)87500); /* 87.500 Ah */
    position = put_tlv(frame, position, 37U, bytes, 4U);
    bytes[0] = 87U;
    position = put_tlv(frame, position, 38U, bytes, 1U);

    put_u32_be(bytes, 0U, 0x00010004U); /* alarm bits 2 and 16 */
    position = put_tlv(frame, position, 170U, bytes, 4U);

    return build_frame(frame, 0xE1U, position);
}

static void test_poll_request(void)
{
    uint8_t out[ESP_BMS_MINGTANG_REQUEST_MAX_LEN] = { 0 };
    size_t out_len = 0U;
    assert(esp_bms_mingtang_poll_request(0U, out, &out_len));
    assert(out_len == 9U && memcmp(out, "stateread", 9U) == 0);
    assert(esp_bms_mingtang_poll_request(1U, out, &out_len));
    assert(out_len == 11U && memcmp(out, "settingread", 11U) == 0);
    assert(esp_bms_mingtang_poll_request(2U, out, &out_len));
    assert(out_len == 7U && memcmp(out, "readhis", 7U) == 0);
    assert(esp_bms_mingtang_poll_request(3U, out, &out_len));
    assert(out_len == 9U && memcmp(out, "readevent", 9U) == 0);
    assert(esp_bms_mingtang_poll_request(4U, out, &out_len) == false);
    assert(esp_bms_mingtang_poll_request(0U, NULL, &out_len) == false);
    assert(esp_bms_mingtang_poll_request(0U, out, NULL) == false);
}

static void test_state_frame(void)
{
    uint8_t frame[ESP_BMS_MINGTANG_STREAM_CAPACITY];
    const size_t frame_len = build_state_frame(frame);

    uint8_t stream[ESP_BMS_MINGTANG_STREAM_CAPACITY] = { 0 };
    size_t stream_len = 0U;
    esp_bms_bms_telemetry_t telemetry;
    memset(&telemetry, 0, sizeof(telemetry));

    /* Fragmented arrival: header, middle, tail. */
    assert(!esp_bms_mingtang_feed(stream, &stream_len, sizeof(stream), frame, 5U, &telemetry));
    assert(!esp_bms_mingtang_feed(stream, &stream_len, sizeof(stream), &frame[5], 11U, &telemetry));
    assert(esp_bms_mingtang_feed(stream, &stream_len, sizeof(stream), &frame[16],
                                 frame_len - 16U, &telemetry));
    assert(stream_len == 0U);

    assert(telemetry.pack_voltage_mv == 53200U);
    assert(telemetry.current_deci_amps == 120); /* 12.500 - 0.500 = 12.000 A */
    assert(telemetry.soc_percent == 87U);
    assert(telemetry.total_capacity_mah == 100000U);
    assert(telemetry.capacity_remaining_mah == 87500U);
    assert(telemetry.max_cell_voltage_mv == 3330U);
    assert(telemetry.min_cell_voltage_mv == 3300U);
    assert(telemetry.delta_cell_voltage_mv == 30U);
    assert(telemetry.average_cell_voltage_mv == 3315U);
    assert(telemetry.temperature_valid[0] && telemetry.temperatures_celsius[0] == 31);
    assert(telemetry.temperature_valid[1] && telemetry.temperatures_celsius[1] == 28);
    /* -15.50 degC truncates toward zero, like the vendor double -> int projection. */
    assert(telemetry.temperature_valid[2] && telemetry.temperatures_celsius[2] == -15);
    assert(telemetry.temperature_valid[3] == false);
    assert(telemetry.temperature_valid[4] == false);
    assert(telemetry.balancing_supported && telemetry.balancing_active);
    assert(telemetry.protection_mask == 0U);
    assert(telemetry.warning_mask == 0x00010004U);
    assert(telemetry.partial == false);

    /* A short circuit protection state sets protection bit 2. */
    uint8_t modified[ESP_BMS_MINGTANG_STREAM_CAPACITY];
    memcpy(modified, frame, frame_len);
    size_t position = 6U;
    while (position + 3U <= frame_len - 1U) {
        const uint16_t tag = (uint16_t)(((uint16_t)modified[position] << 8U) |
                                        modified[position + 1U]);
        const size_t value_len = modified[position + 2U];
        if (tag == 21U) {
            modified[position + 3U] = 1U;
        }
        position += 3U + value_len;
    }
    stream_len = 0U;
    memset(&telemetry, 0, sizeof(telemetry));
    assert(esp_bms_mingtang_feed(stream, &stream_len, sizeof(stream), modified, frame_len,
                                 &telemetry));
    assert(telemetry.protection_mask == (UINT64_C(1) << 2U));
}

static void test_bad_frames(void)
{
    uint8_t frame[ESP_BMS_MINGTANG_STREAM_CAPACITY];
    const size_t frame_len = build_state_frame(frame);
    uint8_t stream[ESP_BMS_MINGTANG_STREAM_CAPACITY] = { 0 };
    size_t stream_len = 0U;
    esp_bms_bms_telemetry_t telemetry;
    memset(&telemetry, 0, sizeof(telemetry));

    /* Truncated TLV value: the last tag claims more bytes than the frame holds.
       The 0xE1 frame ends with tag 170's four byte value, so its length byte sits
       six bytes before the end of the frame. */
    uint8_t broken[ESP_BMS_MINGTANG_STREAM_CAPACITY];
    memcpy(broken, frame, frame_len);
    assert(frame[frame_len - 8U] == 0x00U && frame[frame_len - 7U] == 0xAAU);
    broken[frame_len - 6U] = 0xF0U;
    stream_len = 0U;
    assert(!esp_bms_mingtang_feed(stream, &stream_len, sizeof(stream), broken, frame_len,
                                  &telemetry));

    /* Wrong header marker (byte 1 must be 0x11). */
    memcpy(broken, frame, frame_len);
    broken[1] = 0x12U;
    stream_len = 0U;
    assert(!esp_bms_mingtang_feed(stream, &stream_len, sizeof(stream), broken, frame_len,
                                  &telemetry));

    /* Inconsistent length field pointing past the buffer. */
    memcpy(broken, frame, frame_len);
    put_u16_be(broken, 4U, (uint16_t)(frame_len + 40U));
    stream_len = 0U;
    assert(!esp_bms_mingtang_feed(stream, &stream_len, sizeof(stream), broken, frame_len,
                                  &telemetry));

    /* Partial frame stays buffered and does not decode. */
    stream_len = 0U;
    assert(!esp_bms_mingtang_feed(stream, &stream_len, sizeof(stream), frame, frame_len - 3U,
                                  &telemetry));
    assert(stream_len == frame_len - 3U);
    assert(esp_bms_mingtang_feed(stream, &stream_len, sizeof(stream), &frame[frame_len - 3U], 3U,
                                 &telemetry));
    assert(telemetry.pack_voltage_mv == 53200U);
}

static void test_settings_frame_is_skipped(void)
{
    uint8_t frame[ESP_BMS_MINGTANG_STREAM_CAPACITY];
    memset(frame, 0, sizeof(frame));
    size_t position = 6U;
    uint8_t bytes[4] = { 0x01U, 0x00U, 0x00U, 0x00U };
    position = put_tlv(frame, position, 22U, bytes, 1U);
    position = put_tlv(frame, position, 33U, bytes, 1U);
    const size_t settings_len = build_frame(frame, 0xE2U, position);
    memcpy(&frame[settings_len], "[END]", 5U);

    uint8_t stream[ESP_BMS_MINGTANG_STREAM_CAPACITY] = { 0 };
    size_t stream_len = 0U;
    esp_bms_bms_telemetry_t telemetry;
    memset(&telemetry, 0, sizeof(telemetry));
    assert(!esp_bms_mingtang_feed(stream, &stream_len, sizeof(stream), frame, settings_len + 5U,
                                  &telemetry));
    assert(stream_len == 0U);

    /* A state frame directly after the settings block still decodes. */
    uint8_t state[ESP_BMS_MINGTANG_STREAM_CAPACITY];
    const size_t state_len = build_state_frame(state);
    uint8_t combined[ESP_BMS_MINGTANG_STREAM_CAPACITY * 2U];
    memcpy(combined, frame, settings_len + 5U);
    memcpy(&combined[settings_len + 5U], state, state_len);
    stream_len = 0U;
    assert(esp_bms_mingtang_feed(stream, &stream_len, sizeof(stream), combined,
                                 settings_len + 5U + state_len, &telemetry));
    assert(telemetry.pack_voltage_mv == 53200U);
}

static void test_history_frame_is_skipped(void)
{
    /* A 0xF1 history block shares the header and length layout and must be consumed
       whole so that its payload cannot be mistaken for a state frame. */
    uint8_t frame[ESP_BMS_MINGTANG_STREAM_CAPACITY];
    memset(frame, 0, sizeof(frame));
    size_t position = 6U;
    uint8_t payload[4] = { 0xE1U, 0x11U, 0x00U, 0x00U };
    position = put_tlv(frame, position, 1U, payload, sizeof(payload));
    const size_t history_len = build_frame(frame, 0xF1U, position);

    uint8_t stream[ESP_BMS_MINGTANG_STREAM_CAPACITY] = { 0 };
    size_t stream_len = 0U;
    esp_bms_bms_telemetry_t telemetry;
    memset(&telemetry, 0, sizeof(telemetry));
    assert(!esp_bms_mingtang_feed(stream, &stream_len, sizeof(stream), frame, history_len,
                                  &telemetry));
    assert(stream_len == 0U);
    assert(telemetry.pack_voltage_mv == 0U);
}

static void test_missing_tags(void)
{
    /* Only voltage and SOC: decodes but is flagged partial. */
    uint8_t frame[ESP_BMS_MINGTANG_STREAM_CAPACITY];
    memset(frame, 0, sizeof(frame));
    size_t position = 6U;
    uint8_t bytes[4] = { 0 };
    put_u16_be(bytes, 0U, 5000U);
    position = put_tlv(frame, position, 10U, bytes, 2U);
    bytes[0] = 55U;
    position = put_tlv(frame, position, 38U, bytes, 1U);
    const size_t frame_len = build_frame(frame, 0xE1U, position);

    uint8_t stream[ESP_BMS_MINGTANG_STREAM_CAPACITY] = { 0 };
    size_t stream_len = 0U;
    esp_bms_bms_telemetry_t telemetry;
    memset(&telemetry, 0, sizeof(telemetry));
    assert(esp_bms_mingtang_feed(stream, &stream_len, sizeof(stream), frame, frame_len, &telemetry));
    assert(telemetry.pack_voltage_mv == 50000U);
    assert(telemetry.soc_percent == 55U);
    assert(telemetry.current_deci_amps == 0);
    assert(telemetry.total_capacity_mah == 0U);
    assert(telemetry.partial);
    assert(telemetry.balancing_supported == false);
}

static void test_small_stream_capacity(void)
{
    uint8_t frame[ESP_BMS_MINGTANG_STREAM_CAPACITY];
    const size_t frame_len = build_state_frame(frame);
    uint8_t tiny[32] = { 0 };
    size_t tiny_len = 0U;
    esp_bms_bms_telemetry_t telemetry;
    memset(&telemetry, 0, sizeof(telemetry));
    for (size_t offset = 0U; offset < frame_len; offset += 7U) {
        const size_t span = (frame_len - offset) < 7U ? frame_len - offset : 7U;
        (void)esp_bms_mingtang_feed(tiny, &tiny_len, sizeof(tiny), &frame[offset], span,
                                    &telemetry);
        assert(tiny_len <= sizeof(tiny));
    }
    assert(telemetry.pack_voltage_mv == 0U);
    assert(ESP_BMS_MINGTANG_STREAM_CAPACITY >= frame_len);
}

int main(void)
{
    test_poll_request();
    test_state_frame();
    test_bad_frames();
    test_settings_frame_is_skipped();
    test_history_frame_is_skipped();
    test_missing_tags();
    test_small_stream_capacity();
    printf("mingtang selftest ok\n");
    return 0;
}
