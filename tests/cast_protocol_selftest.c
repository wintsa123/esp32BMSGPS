#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_bms_cast_protocol.h"

static void test_jpeg_frame_boundaries(void)
{
    const uint8_t message[] = {
        ESP_BMS_CAST_TYPE_JPEG_FRAME,
        ESP_BMS_CAST_PROTOCOL_VERSION,
        0U,
        0U,
        0U,
        7U,
        1U,
        0xffU,
        0xd8U,
    };
    esp_bms_cast_jpeg_frame_t frame = { 0 };
    assert(esp_bms_cast_protocol_parse_jpeg_frame(message, sizeof(message), &frame));
    assert(frame.sequence == 7U && frame.rotation == 1U);
    assert(frame.jpeg_bytes == 2U && frame.jpeg[0] == 0xffU && frame.jpeg[1] == 0xd8U);
    assert(!esp_bms_cast_protocol_parse_jpeg_frame(message,
                                                   ESP_BMS_CAST_FRAME_HEADER_BYTES,
                                                   &frame));
    assert(!esp_bms_cast_protocol_parse_jpeg_frame(message,
                                                   ESP_BMS_CAST_MESSAGE_MAX_BYTES + 1U,
                                                   &frame));

    uint8_t invalid[sizeof(message)];
    for (size_t index = 0U; index < sizeof(message); ++index) {
        invalid[index] = message[index];
    }
    invalid[1]++;
    assert(!esp_bms_cast_protocol_parse_jpeg_frame(invalid, sizeof(invalid), &frame));
    invalid[1] = ESP_BMS_CAST_PROTOCOL_VERSION;
    invalid[6] = 4U;
    assert(!esp_bms_cast_protocol_parse_jpeg_frame(invalid, sizeof(invalid), &frame));
}

static void test_heartbeat_shape(void)
{
    const uint8_t heartbeat[] = { ESP_BMS_CAST_TYPE_HEARTBEAT };
    const uint8_t oversized[] = { ESP_BMS_CAST_TYPE_HEARTBEAT, 0U };
    assert(esp_bms_cast_protocol_is_heartbeat(heartbeat, sizeof(heartbeat)));
    assert(!esp_bms_cast_protocol_is_heartbeat(oversized, sizeof(oversized)));
}

static void test_ack_sequence(void)
{
    uint8_t ack[ESP_BMS_CAST_ACK_BYTES] = { 0 };
    assert(esp_bms_cast_protocol_encode_ack(UINT32_C(0x12345678), ack, sizeof(ack)));
    assert(ack[0] == ESP_BMS_CAST_TYPE_ACK && ack[1] == 0x12U && ack[2] == 0x34U &&
           ack[3] == 0x56U && ack[4] == 0x78U);
    assert(!esp_bms_cast_protocol_encode_ack(1U, ack, sizeof(ack) - 1U));
}

int main(void)
{
    test_jpeg_frame_boundaries();
    test_heartbeat_shape();
    test_ack_sequence();
    puts("cast protocol self-test passed");
    return 0;
}
