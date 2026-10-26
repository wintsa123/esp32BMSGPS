#include "esp_bms_em02_protocol.h"

#include <assert.h>
#include <string.h>

static uint16_t crc16(const uint8_t *bytes, size_t len)
{
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0U; i < len; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 1U) ? (uint16_t)((crc >> 1U) ^ 0xA001U) : (uint16_t)(crc >> 1U);
        }
    }
    return crc;
}

static size_t make_frame(uint8_t *frame, uint8_t byte_count)
{
    const size_t len = (size_t)byte_count + 5U;
    memset(frame, 0, len);
    frame[0] = 1U;
    frame[1] = 3U;
    frame[2] = byte_count;
    const uint16_t crc = crc16(frame, len - 2U);
    frame[len - 2U] = (uint8_t)crc;
    frame[len - 1U] = (uint8_t)(crc >> 8U);
    return len;
}

int main(void)
{
    static const uint8_t expected[4][8] = {
        { 1, 3, 0x0F, 0xA0, 0, 0x59, 0x86, 0xC6 },
        { 1, 3, 0x0F, 0xFC, 0, 0x36, 0x06, 0xF8 },
        { 1, 3, 0x10, 0x68, 0, 0x2D, 0, 0xCB },
        { 1, 3, 0x10, 0x38, 0, 0x30, 0xC0, 0xD3 },
    };
    uint8_t request[8];
    for (uint8_t i = 0U; i < 4U; ++i) {
        assert(esp_bms_em02_poll_request(i, request));
        assert(memcmp(request, expected[i], sizeof(request)) == 0);
    }

    uint8_t identity[183];
    const size_t identity_len = make_frame(identity, 178U);
    memcpy(identity + 67U, "LX-EM02", 7U);
    uint16_t crc = crc16(identity, identity_len - 2U);
    identity[identity_len - 2U] = (uint8_t)crc;
    identity[identity_len - 1U] = (uint8_t)(crc >> 8U);
    uint8_t stream[512] = { 0 };
    size_t stream_len = 0U;
    bool known = false;
    esp_bms_bms_telemetry_t telemetry = { 0 };
    assert(!esp_bms_em02_feed(stream, &stream_len, sizeof(stream), identity, 40U, 0U, &known, &telemetry));
    assert(esp_bms_em02_feed(stream, &stream_len, sizeof(stream), identity + 40U,
                             identity_len - 40U, 0U, &known, &telemetry));
    assert(known);

    uint8_t wrong_identity[183];
    memcpy(wrong_identity, identity, sizeof(wrong_identity));
    memset(wrong_identity + 67U, 0, 26U);
    memcpy(wrong_identity + 67U, "OTHER-BMS", 9U);
    crc = crc16(wrong_identity, sizeof(wrong_identity) - 2U);
    wrong_identity[sizeof(wrong_identity) - 2U] = (uint8_t)crc;
    wrong_identity[sizeof(wrong_identity) - 1U] = (uint8_t)(crc >> 8U);
    stream_len = 0U;
    known = true;
    telemetry.pack_voltage_mv = 0x1234U;
    assert(!esp_bms_em02_feed(stream, &stream_len, sizeof(stream), wrong_identity,
                              sizeof(wrong_identity), 0U, &known, &telemetry));
    assert(!known);
    assert(telemetry.pack_voltage_mv == 0x1234U);

    uint8_t realtime[113];
    const size_t realtime_len = make_frame(realtime, 108U);
    realtime[3] = 2U;
    realtime[5] = 1U;
    realtime[6] = 60U;
    realtime[7] = 1U;
    realtime[8] = 61U;
    realtime[10] = 1U;
    realtime[11] = 62U;
    realtime[25] = 0x00; realtime[26] = 0x00; realtime[27] = 0x30; realtime[28] = 0xD4;
    realtime[33] = 0x0D; realtime[34] = 0xAC;
    realtime[35] = 0x0D; realtime[36] = 0xB0;
    realtime[105] = 0x01; realtime[106] = 0xF4;
    realtime[107] = 0x01; realtime[108] = 0x2C;
    realtime[109] = 88U;
    crc = crc16(realtime, realtime_len - 2U);
    realtime[realtime_len - 2U] = (uint8_t)crc;
    realtime[realtime_len - 1U] = (uint8_t)(crc >> 8U);
    stream_len = 0U;
    assert(esp_bms_em02_feed(stream, &stream_len, sizeof(stream), realtime, realtime_len,
                             1U, &known, &telemetry));
    assert(telemetry.pack_voltage_mv == 12500U);
    assert(telemetry.soc_percent == 88U);
    assert(telemetry.capacity_remaining_mah == 44000U);
    assert(telemetry.min_cell_voltage_mv == 3500U);
    assert(telemetry.max_cell_voltage_mv == 3504U);
    assert(telemetry.temperatures_celsius[0] == 22);
    assert(telemetry.temperatures_celsius[4] == 21);
    assert(telemetry.temperatures_celsius[5] == 20);

    /* Valid auxiliary reads are consumed without overwriting realtime data. */
    uint8_t auxiliary[101];
    const uint8_t auxiliary_counts[] = { 90U, 96U };
    for (uint8_t i = 0U; i < 2U; ++i) {
        const size_t auxiliary_len = make_frame(auxiliary, auxiliary_counts[i]);
        stream_len = 0U;
        assert(esp_bms_em02_feed(stream, &stream_len, sizeof(stream), auxiliary,
                                 auxiliary_len, (uint8_t)(i + 2U), &known, &telemetry));
        assert(telemetry.pack_voltage_mv == 12500U);
    }

    realtime[realtime_len - 1U] ^= 0x01U;
    stream_len = 0U;
    assert(!esp_bms_em02_feed(stream, &stream_len, sizeof(stream), realtime, realtime_len,
                              1U, &known, &telemetry));
    uint8_t oversized[513] = { 0 };
    stream_len = 12U;
    assert(!esp_bms_em02_feed(stream, &stream_len, sizeof(stream), oversized, sizeof(oversized),
                              1U, &known, &telemetry));
    assert(stream_len == 0U);
    return 0;
}
