#include "esp_bms_jbd_protocol.h"

#include <assert.h>
#include <string.h>

static uint16_t checksum(const uint8_t *data, size_t len)
{
    uint16_t value = 0U;
    for (size_t index = 0U; index < len; ++index) {
        value = (uint16_t)(value - data[index]);
    }
    return value;
}

static void finish_frame(uint8_t *frame)
{
    const size_t frame_len = 7U + frame[3];
    const uint16_t crc = checksum(frame + 2U, (size_t)frame[3] + 2U);
    frame[frame_len - 3U] = (uint8_t)(crc >> 8U);
    frame[frame_len - 2U] = (uint8_t)crc;
    frame[frame_len - 1U] = 0x77U;
}

int main(void)
{
    uint8_t request[7] = { 0 };
    assert(esp_bms_jbd_poll_request(0U, request));
    assert(request[0] == 0xDDU && request[1] == 0xA5U && request[2] == 0x03U);
    assert(esp_bms_jbd_poll_request(1U, request) && request[2] == 0x04U);

    uint8_t frame[32] = { 0 };
    frame[0] = 0xDDU;
    frame[1] = 0x03U;
    frame[2] = 0U;
    frame[3] = 25U;
    frame[4] = 0x14U;
    frame[5] = 0x50U;
    frame[6] = 0xFFU;
    frame[7] = 0x9CU;
    frame[8] = 0x01U;
    frame[9] = 0x2CU;
    frame[10] = 0x01U;
    frame[11] = 0x90U;
    frame[23] = 85U;
    frame[26] = 1U;
    frame[27] = 0x0BU;
    frame[28] = 0xB8U;
    finish_frame(frame);

    uint8_t stream[320] = { 0 };
    size_t stream_len = 0U;
    esp_bms_bms_telemetry_t telemetry = { 0 };
    assert(!esp_bms_jbd_feed(stream, &stream_len, sizeof(stream), frame, 8U, &telemetry));
    assert(esp_bms_jbd_feed(stream, &stream_len, sizeof(stream), frame + 8U, sizeof(frame) - 8U, &telemetry));
    assert(telemetry.pack_voltage_mv == 52000U && telemetry.soc_percent == 85U &&
           telemetry.balancing_supported && !telemetry.balancing_active);
    frame[16] = 0x01U;
    finish_frame(frame);
    stream_len = 0U;
    assert(esp_bms_jbd_feed(stream, &stream_len, sizeof(stream), frame, sizeof(frame), &telemetry));
    assert(telemetry.balancing_active);
    frame[30] ^= 1U;
    stream_len = 0U;
    assert(!esp_bms_jbd_feed(stream, &stream_len, sizeof(stream), frame, sizeof(frame), &telemetry));

    frame[30] ^= 1U;
    uint8_t auth_frame[] = { 0xFFU, 0xAAU, 0x17U, 0x02U, 0x12U, 0x34U, 0x5FU };
    uint8_t combined[sizeof(auth_frame) + sizeof(frame)] = { 0 };
    memcpy(combined, auth_frame, sizeof(auth_frame));
    memcpy(combined + sizeof(auth_frame), frame, sizeof(frame));
    stream_len = 0U;
    assert(!esp_bms_jbd_feed(stream, &stream_len, sizeof(stream), combined, 3U, &telemetry));
    assert(esp_bms_jbd_feed(stream,
                            &stream_len,
                            sizeof(stream),
                            combined + 3U,
                            sizeof(combined) - 3U,
                            &telemetry));

    uint8_t bad_auth_frame[5U + sizeof(frame)] = { 0xFFU, 0xAAU, 0x17U, sizeof(frame) };
    memcpy(bad_auth_frame + 4U, frame, sizeof(frame));
    bad_auth_frame[sizeof(bad_auth_frame) - 1U] = 1U;
    stream_len = 0U;
    telemetry.soc_percent = 0U;
    assert(esp_bms_jbd_feed(stream,
                            &stream_len,
                            sizeof(stream),
                            bad_auth_frame,
                            sizeof(bad_auth_frame),
                            &telemetry));
    assert(stream_len == 1U && telemetry.soc_percent == 85U);

    uint8_t diagnostic[8] = { 0xDDU, 0x05U, 0U, 1U, 0x42U, 0U, 0U, 0U };
    finish_frame(diagnostic);
    uint8_t cell_info[9] = { 0xDDU, 0x04U, 0U, 2U, 0x0CU, 0xE4U, 0U, 0U, 0U };
    finish_frame(cell_info);
    uint8_t error_counts[31] = { 0xDDU, 0xAAU, 0U, 24U };
    finish_frame(error_counts);
    uint8_t diagnostics[sizeof(diagnostic) + sizeof(cell_info) + sizeof(error_counts) + sizeof(frame)] = { 0 };
    memcpy(diagnostics, diagnostic, sizeof(diagnostic));
    memcpy(diagnostics + sizeof(diagnostic), cell_info, sizeof(cell_info));
    memcpy(diagnostics + sizeof(diagnostic) + sizeof(cell_info), error_counts, sizeof(error_counts));
    memcpy(diagnostics + sizeof(diagnostic) + sizeof(cell_info) + sizeof(error_counts), frame, sizeof(frame));
    stream_len = 0U;
    assert(esp_bms_jbd_feed(stream, &stream_len, sizeof(stream), diagnostics, sizeof(diagnostics), &telemetry));

    uint8_t bad_length[4U + sizeof(frame)] = { 0xDDU, 0x05U, 0U, 0xFFU };
    memcpy(bad_length + 4U, frame, sizeof(frame));
    stream_len = 0U;
    assert(esp_bms_jbd_feed(stream, &stream_len, sizeof(stream), bad_length, sizeof(bad_length), &telemetry));

    uint8_t bad_auth_length[4U + sizeof(frame)] = { 0xFFU, 0xAAU, 0x17U, 0xFFU };
    memcpy(bad_auth_length + 4U, frame, sizeof(frame));
    stream_len = 0U;
    assert(esp_bms_jbd_feed(stream,
                            &stream_len,
                            sizeof(stream),
                            bad_auth_length,
                            sizeof(bad_auth_length),
                            &telemetry));

    uint8_t bad_crc_then_valid[sizeof(frame) * 2U] = { 0 };
    memcpy(bad_crc_then_valid, frame, sizeof(frame));
    bad_crc_then_valid[30] ^= 1U;
    memcpy(bad_crc_then_valid + sizeof(frame), frame, sizeof(frame));
    stream_len = 0U;
    assert(esp_bms_jbd_feed(stream,
                            &stream_len,
                            sizeof(stream),
                            bad_crc_then_valid,
                            sizeof(bad_crc_then_valid),
                            &telemetry));
    assert(stream_len == 0U);

    memcpy(bad_crc_then_valid, frame, sizeof(frame));
    bad_crc_then_valid[2] = 1U;
    bad_crc_then_valid[23] = 13U;
    finish_frame(bad_crc_then_valid);
    stream_len = 0U;
    assert(esp_bms_jbd_feed(stream,
                            &stream_len,
                            sizeof(stream),
                            bad_crc_then_valid,
                            sizeof(bad_crc_then_valid),
                            &telemetry));
    assert(stream_len == 0U && telemetry.soc_percent == 85U);

    uint8_t second_frame[sizeof(frame)] = { 0 };
    memcpy(second_frame, frame, sizeof(frame));
    second_frame[23] = 42U;
    finish_frame(second_frame);
    memcpy(bad_crc_then_valid, frame, sizeof(frame));
    memcpy(bad_crc_then_valid + sizeof(frame), second_frame, sizeof(second_frame));
    stream_len = 0U;
    assert(esp_bms_jbd_feed(stream,
                            &stream_len,
                            sizeof(stream),
                            bad_crc_then_valid,
                            sizeof(bad_crc_then_valid),
                            &telemetry));
    assert(stream_len == sizeof(second_frame));
    const uint8_t noise = 0U;
    assert(esp_bms_jbd_feed(stream, &stream_len, sizeof(stream), &noise, 1U, &telemetry));
    assert(telemetry.soc_percent == 42U);
    return 0;
}
