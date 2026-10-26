#include "esp_bms_bluebabe_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define STATE_FRAME_LEN (ESP_BMS_BLUEBABE_STATE_PAYLOAD_LEN + 19U)
#define INFO_FRAME_LEN (ESP_BMS_BLUEBABE_INFO_PAYLOAD_LEN + 19U)

static const uint8_t k_code[ESP_BMS_BLUEBABE_MATCHING_CODE_LEN] = { 1U, 2U, 3U, 4U, 5U, 6U };

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

static void put_u32_be_signed(uint8_t *data, size_t offset, int32_t value)
{
    put_u32_be(data, offset, (uint32_t)value);
}

/* Builds a frame the way the meter does: header, payload, CRC over [0, len-3), 0xE5. */
static size_t build_frame(uint8_t *frame,
                          uint8_t table,
                          const uint8_t *payload,
                          size_t payload_len)
{
    const size_t frame_len = payload_len + 19U;
    memset(frame, 0, frame_len);
    frame[0] = 0xFAU;
    frame[1] = 0x50U;
    frame[2] = 0xA0U;
    frame[3] = 0x28U;
    frame[4] = 0x15U;
    put_u16_be(frame, 5U, (uint16_t)(payload_len + 9U));
    memcpy(&frame[7], k_code, sizeof(k_code));
    frame[13] = table;
    frame[14] = 0U;
    frame[15] = (uint8_t)payload_len;
    memcpy(&frame[16], payload, payload_len);
    const uint16_t crc = esp_bms_bluebabe_crc16(frame, frame_len - 3U);
    frame[frame_len - 3U] = (uint8_t)(crc >> 8U);
    frame[frame_len - 2U] = (uint8_t)(crc & 0xFFU);
    frame[frame_len - 1U] = 0xE5U;
    return frame_len;
}

static void test_request(void)
{
    uint8_t request[ESP_BMS_BLUEBABE_REQUEST_LEN] = { 0 };
    assert(esp_bms_bluebabe_poll_request(0U, request) == false); /* no code yet */
    assert(esp_bms_bluebabe_set_matching_code(k_code));
    assert(esp_bms_bluebabe_matching_code_configured());
    assert(esp_bms_bluebabe_poll_request(0U, request));
    assert(request[0] == 0xFAU && request[1] == 0x50U && request[2] == 0x28U &&
           request[3] == 0xA0U && request[4] == 0x15U);
    assert(request[5] == 0x00U && request[6] == 0x09U);
    assert(memcmp(&request[7], k_code, sizeof(k_code)) == 0);
    assert(request[13] == 1U && request[14] == 0U && request[15] == 111U);
    assert(request[18] == 0xE5U);
    const uint16_t crc = (uint16_t)(((uint16_t)request[16] << 8U) | request[17]);
    assert(crc == esp_bms_bluebabe_crc16(request, 16U));
    assert(request[16] != 0U || request[17] != 0U);

    assert(esp_bms_bluebabe_poll_request(1U, request));
    assert(request[13] == 2U && request[15] == 134U);
    assert(esp_bms_bluebabe_poll_request(2U, request) == false);
    assert(esp_bms_bluebabe_build_request(0U, NULL, request));
    for (size_t index = 7U; index < 13U; ++index) {
        assert(request[index] == 0U);
    }
    const uint8_t bad_code[ESP_BMS_BLUEBABE_MATCHING_CODE_LEN] = { '1', '2', '3', '4', '5', '6' };
    assert(esp_bms_bluebabe_build_request(0U, bad_code, request) == false);
}

static void test_state_frame(void)
{
    uint8_t payload[ESP_BMS_BLUEBABE_STATE_PAYLOAD_LEN];
    memset(payload, 0, sizeof(payload));
    put_u16_be(payload, 0U, 5320U);              /* 53.20 V  -> 53200 mV */
    put_u32_be_signed(payload, 2U, (int32_t)-1234); /* -12.34 A -> -123 deciamps */
    put_u16_be(payload, 6U, 875U);               /* 87.5 %   -> 87 % */
    payload[14] = (uint8_t)(int8_t)-7;           /* -7 degC */
    put_u32_be(payload, 15U, 4500000U);          /* 4.5 Ah   -> 45000 mAh */
    put_u16_be(payload, 82U, 3330U);             /* max cell 3.330 V */
    put_u16_be(payload, 84U, 3300U);             /* min cell 3.300 V */
    put_u16_be(payload, 86U, 30U);               /* delta 0.030 V */
    memcpy(&payload[90], "MTB-1.02", 8U);

    uint8_t frame[STATE_FRAME_LEN];
    const size_t frame_len = build_frame(frame, ESP_BMS_BLUEBABE_TABLE_STATE, payload,
                                         sizeof(payload));
    assert(frame_len == STATE_FRAME_LEN);

    uint8_t stream[ESP_BMS_BLUEBABE_STREAM_CAPACITY] = { 0 };
    size_t stream_len = 0U;
    esp_bms_bms_telemetry_t telemetry;
    memset(&telemetry, 0, sizeof(telemetry));

    /* Split delivery: header first, payload later. */
    assert(!esp_bms_bluebabe_feed(stream, &stream_len, sizeof(stream), frame, 40U, &telemetry));
    assert(!esp_bms_bluebabe_feed(stream, &stream_len, sizeof(stream), &frame[40], 60U, &telemetry));
    assert(esp_bms_bluebabe_feed(stream, &stream_len, sizeof(stream), &frame[100],
                                 frame_len - 100U, &telemetry));
    assert(stream_len == 0U);
    assert(telemetry.pack_voltage_mv == 53200U);
    assert(telemetry.current_deci_amps == -123);
    assert(telemetry.soc_percent == 87U);
    assert(telemetry.total_capacity_mah == 0U);
    assert(telemetry.capacity_remaining_mah == 45000U);
    assert(telemetry.max_cell_voltage_mv == 3330U);
    assert(telemetry.min_cell_voltage_mv == 3300U);
    assert(telemetry.delta_cell_voltage_mv == 30U);
    assert(telemetry.temperature_valid[0] && telemetry.temperatures_celsius[0] == -7);
    assert(telemetry.balancing_supported == false && telemetry.balancing_active == false);
    assert(telemetry.protection_mask == 0U && telemetry.warning_mask == 0U);
    assert(telemetry.partial);

    /* CRC error: same frame with the last CRC byte flipped. */
    frame[frame_len - 2U] ^= 0xFFU;
    stream_len = 0U;
    assert(!esp_bms_bluebabe_feed(stream, &stream_len, sizeof(stream), frame, frame_len, &telemetry));
    frame[frame_len - 2U] ^= 0xFFU;
    uint8_t expected_crc_byte = frame[frame_len - 2U];
    frame[frame_len - 2U] = (uint8_t)(expected_crc_byte ^ 0x01U);
    stream_len = 0U;
    assert(!esp_bms_bluebabe_feed(stream, &stream_len, sizeof(stream), frame, frame_len, &telemetry));
    frame[frame_len - 2U] = expected_crc_byte;

    /* Length mismatch: lie about the table length in the header. */
    frame[15] = 110U;
    stream_len = 0U;
    assert(!esp_bms_bluebabe_feed(stream, &stream_len, sizeof(stream), frame, frame_len, &telemetry));
    frame[15] = 111U;

    /* Truncated frame never decodes. */
    stream_len = 0U;
    assert(!esp_bms_bluebabe_feed(stream, &stream_len, sizeof(stream), frame, frame_len - 1U,
                                  &telemetry));
    assert(stream_len == frame_len - 1U);

    /* Noise in front is skipped, then the frame decodes. */
    uint8_t noisy[STATE_FRAME_LEN + 4U];
    noisy[0] = 0x00U;
    noisy[1] = 0xFAU;
    noisy[2] = 0x11U;
    noisy[3] = 0x22U;
    memcpy(&noisy[4], frame, frame_len);
    stream_len = 0U;
    assert(esp_bms_bluebabe_feed(stream, &stream_len, sizeof(stream), noisy, sizeof(noisy),
                                 &telemetry));
    assert(telemetry.pack_voltage_mv == 53200U);
}

static void test_info_frame(void)
{
    uint8_t payload[ESP_BMS_BLUEBABE_INFO_PAYLOAD_LEN];
    memset(payload, 0, sizeof(payload));
    memcpy(&payload[16], "HW-1.0  ", 8U);
    memcpy(&payload[24], "SW-2.07 ", 8U);
    payload[47] = 16U;
    put_u32_be(payload, 48U, 100000U); /* 100.000 Ah -> 100000 mAh */
    put_u16_be(payload, 57U, 42U);

    uint8_t frame[INFO_FRAME_LEN];
    const size_t frame_len = build_frame(frame, ESP_BMS_BLUEBABE_TABLE_INFO, payload,
                                         sizeof(payload));
    assert(frame_len == INFO_FRAME_LEN);

    uint8_t stream[ESP_BMS_BLUEBABE_STREAM_CAPACITY] = { 0 };
    size_t stream_len = 0U;
    esp_bms_bms_telemetry_t telemetry;
    memset(&telemetry, 0, sizeof(telemetry));
    assert(esp_bms_bluebabe_feed(stream, &stream_len, sizeof(stream), frame, frame_len, &telemetry));
    assert(telemetry.total_capacity_mah == 100000U);
    assert(telemetry.pack_voltage_mv == 0U);
    assert(telemetry.partial);
}

static void test_matching_code_rejection(void)
{
    uint8_t payload[ESP_BMS_BLUEBABE_STATE_PAYLOAD_LEN];
    memset(payload, 0, sizeof(payload));
    put_u16_be(payload, 0U, 5000U);
    put_u16_be(payload, 82U, 3400U);
    put_u16_be(payload, 84U, 3390U);
    uint8_t frame[STATE_FRAME_LEN];
    const size_t frame_len = build_frame(frame, ESP_BMS_BLUEBABE_TABLE_STATE, payload,
                                         sizeof(payload));
    frame[9] ^= 0x01U; /* corrupt the echoed code, CRC stays valid */
    const uint16_t crc = esp_bms_bluebabe_crc16(frame, frame_len - 3U);
    frame[frame_len - 3U] = (uint8_t)(crc >> 8U);
    frame[frame_len - 2U] = (uint8_t)(crc & 0xFFU);

    uint8_t stream[ESP_BMS_BLUEBABE_STREAM_CAPACITY] = { 0 };
    size_t stream_len = 0U;
    esp_bms_bms_telemetry_t telemetry;
    memset(&telemetry, 0, sizeof(telemetry));
    assert(!esp_bms_bluebabe_feed(stream, &stream_len, sizeof(stream), frame, frame_len, &telemetry));
    /* Without a configured code the frame is accepted. */
    assert(esp_bms_bluebabe_set_matching_code(NULL));
    assert(esp_bms_bluebabe_feed(stream, &stream_len, sizeof(stream), frame, frame_len, &telemetry));
    assert(telemetry.pack_voltage_mv == 50000U);
}

static void test_small_stream_capacity(void)
{
    uint8_t payload[ESP_BMS_BLUEBABE_STATE_PAYLOAD_LEN];
    memset(payload, 0, sizeof(payload));
    put_u16_be(payload, 0U, 4800U);
    put_u16_be(payload, 82U, 3200U);
    put_u16_be(payload, 84U, 3190U);
    uint8_t frame[STATE_FRAME_LEN];
    const size_t frame_len = build_frame(frame, ESP_BMS_BLUEBABE_TABLE_STATE, payload,
                                         sizeof(payload));

    /* A buffer that cannot hold the frame must drain, not overflow. */
    uint8_t tiny[64] = { 0 };
    size_t tiny_len = 0U;
    esp_bms_bms_telemetry_t telemetry;
    memset(&telemetry, 0, sizeof(telemetry));
    for (size_t offset = 0U; offset < frame_len; offset += 16U) {
        const size_t span = (frame_len - offset) < 16U ? frame_len - offset : 16U;
        (void)esp_bms_bluebabe_feed(tiny, &tiny_len, sizeof(tiny), &frame[offset], span,
                                    &telemetry);
        assert(tiny_len <= sizeof(tiny));
    }
    assert(telemetry.pack_voltage_mv == 0U);

    /* The documented capacity is large enough for the biggest frame. */
    assert(ESP_BMS_BLUEBABE_STREAM_CAPACITY >= INFO_FRAME_LEN);
}

int main(void)
{
    test_request();
    test_state_frame();
    test_info_frame();
    test_matching_code_rejection();
    test_small_stream_capacity();
    printf("bluebabe selftest ok\n");
    return 0;
}
