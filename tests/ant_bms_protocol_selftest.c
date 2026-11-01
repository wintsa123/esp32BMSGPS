#include "esp_bms_ant_protocol.h"

#include <assert.h>
#include <string.h>

static const uint8_t new_status_frame[] = {
    0x7EU, 0xA1U, 0x11U, 0x00U, 0x00U, 0x8EU, 0x05U, 0x01U, 0x02U, 0x10U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x80U, 0x00U, 0x80U, 0x01U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0xE4U, 0x0CU,
    0xE4U, 0x0CU, 0xE5U, 0x0CU, 0xE5U, 0x0CU, 0xE8U, 0x0CU, 0xE7U, 0x0CU, 0xE7U, 0x0CU,
    0xE6U, 0x0CU, 0xE8U, 0x0CU, 0xE7U, 0x0CU, 0xE7U, 0x0CU, 0xE7U, 0x0CU, 0xE7U, 0x0CU,
    0xE7U, 0x0CU, 0xE6U, 0x0CU, 0xE9U, 0x0CU, 0x01U, 0x00U, 0x02U, 0x00U, 0x02U, 0x00U,
    0x07U, 0x00U, 0xA4U, 0x14U, 0x03U, 0x00U, 0x5BU, 0x00U, 0x64U, 0x00U, 0x01U, 0x01U,
    0x00U, 0x00U, 0x00U, 0x76U, 0xB0U, 0x10U, 0xD5U, 0x67U, 0x0EU, 0x0FU, 0xBAU, 0x32U,
    0x4AU, 0x00U, 0x0FU, 0x00U, 0x00U, 0x00U, 0x10U, 0x58U, 0x2EU, 0x02U, 0x00U, 0x00U,
    0x00U, 0x00U, 0xE9U, 0x0CU, 0x10U, 0x00U, 0xE4U, 0x0CU, 0x01U, 0x00U, 0x05U, 0x00U,
    0xE6U, 0x0CU, 0x00U, 0x00U, 0x80U, 0x00U, 0x7AU, 0x00U, 0x0FU, 0x02U, 0xF2U, 0xFAU,
    0xB9U, 0x8CU, 0x3BU, 0x00U, 0xBBU, 0xD8U, 0x58U, 0x00U, 0xDAU, 0x2DU, 0x43U, 0x00U,
    0xE8U, 0xB6U, 0x49U, 0x00U, 0x05U, 0x43U, 0xAAU, 0x55U,
};

static const uint8_t old_status_frame[ESP_BMS_ANT_OLD_FRAME_LEN] = {
    0xAAU, 0x55U, 0xAAU, 0xFFU, 0x01U, 0x0BU, 0x0DU, 0x0CU, 0x0DU, 0x0CU, 0x0DU, 0x0BU,
    0x0DU, 0x0AU, 0x0DU, 0x0CU, 0x0DU, 0x0BU, 0x0DU, 0x0CU, 0x0DU, 0x0CU, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0xFFU, 0xFFU,
    0xFFU, 0x95U, 0x29U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U, 0xE3U, 0xDBU, 0x95U, 0x01U,
    0x26U, 0x22U, 0x63U, 0x01U, 0xB1U, 0x6EU, 0xBAU, 0x00U, 0x0FU, 0x00U, 0x0FU, 0x00U,
    0x0DU, 0x00U, 0x0DU, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U, 0x01U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0xFFU, 0xFFU, 0xFEU, 0xE3U, 0x01U, 0x0DU, 0x0CU, 0x04U, 0x0DU,
    0x0AU, 0x0DU, 0x0BU, 0x08U, 0x00U, 0x00U, 0x00U, 0x7EU, 0x00U, 0x7AU, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x04U, 0x0FU, 0x0EU, 0xDFU,
};

static uint16_t crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;
    for (size_t index = 0U; index < len; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 1U) != 0U ? (uint16_t)((crc >> 1U) ^ 0xA001U)
                                  : (uint16_t)(crc >> 1U);
        }
    }
    return crc;
}

static void write_u32(uint8_t *out, uint32_t value)
{
    for (unsigned index = 0U; index < 4U; ++index) {
        out[index] = (uint8_t)(value >> (index * 8U));
    }
}

static void update_crc(uint8_t *frame)
{
    const size_t offset = 6U + frame[5U];
    const uint16_t crc = crc16(frame + 1U, offset - 1U);
    frame[offset] = (uint8_t)crc;
    frame[offset + 1U] = (uint8_t)(crc >> 8U);
}

static void check_new_stream(void)
{
    uint8_t stream[320] = { 0 };
    size_t stream_len = 0U;
    esp_bms_bms_telemetry_t telemetry = { 0 };
    bool info = false;
    /* Split every possible header, payload, CRC and trailer boundary. */
    for (size_t split = 1U; split < sizeof(new_status_frame); ++split) {
        stream_len = 0U;
        assert(!esp_bms_ant_protocol_feed(stream, &stream_len, sizeof(stream),
                                          new_status_frame, split, &telemetry, &info));
        assert(esp_bms_ant_protocol_feed(stream, &stream_len, sizeof(stream),
                                         new_status_frame + split,
                                         sizeof(new_status_frame) - split, &telemetry, &info));
        assert(stream_len == 0U && !info && telemetry.soc_percent == 91U);
    }

    uint8_t joined[3U * sizeof(new_status_frame)] = { 0 };
    for (size_t index = 0U; index < 3U; ++index) {
        memcpy(joined + index * sizeof(new_status_frame), new_status_frame, sizeof(new_status_frame));
    }
    uint8_t *last = joined + 2U * sizeof(new_status_frame);
    last[78U] = 77U;
    update_crc(last);
    assert(esp_bms_ant_protocol_feed(stream, &stream_len, sizeof(stream),
                                     joined, sizeof(joined), &telemetry, &info));
    assert(stream_len == 0U && telemetry.soc_percent == 77U);

    uint8_t compound[sizeof(new_status_frame) + 6U] = { 0 };
    memcpy(compound, new_status_frame, sizeof(new_status_frame) - 2U);
    uint8_t *aux = compound + sizeof(new_status_frame) - 2U;
    memcpy(aux, (uint8_t[]){ 0xFEU, 0x00U, 0x02U, 0x00U }, 4U);
    const uint16_t aux_crc = crc16(aux, 4U);
    aux[4U] = (uint8_t)aux_crc;
    aux[5U] = (uint8_t)(aux_crc >> 8U);
    aux[6U] = 0xAAU;
    aux[7U] = 0x55U;
    assert(esp_bms_ant_protocol_decode(compound, sizeof(compound), &telemetry, &info));
    for (size_t split = sizeof(new_status_frame) - 4U; split < sizeof(compound); ++split) {
        stream_len = 0U;
        assert(!esp_bms_ant_protocol_feed(stream, &stream_len, sizeof(stream),
                                          compound, split, &telemetry, &info));
        assert(esp_bms_ant_protocol_feed(stream, &stream_len, sizeof(stream),
                                         compound + split, sizeof(compound) - split,
                                         &telemetry, &info));
        assert(stream_len == 0U && telemetry.soc_percent == 91U);
    }
    aux[4U] ^= 1U;
    assert(!esp_bms_ant_protocol_decode(compound, sizeof(compound), &telemetry, &info));
    stream_len = 0U;
    assert(!esp_bms_ant_protocol_feed(stream, &stream_len, sizeof(stream),
                                      compound, sizeof(compound), &telemetry, &info));
    assert(esp_bms_ant_protocol_feed(stream, &stream_len, sizeof(stream),
                                     new_status_frame, sizeof(new_status_frame), &telemetry, &info));

    /* Noise, a corrupt primary CRC and a bogus length must resynchronize. */
    uint8_t noise[sizeof(new_status_frame) + 8U] = { 0x22U, 0x7EU, 0xA1U, 0x11U,
                                                    0x00U, 0x00U, 0xFFU, 0x00U };
    memcpy(noise + 8U, new_status_frame, sizeof(new_status_frame));
    stream_len = 0U;
    assert(esp_bms_ant_protocol_feed(stream, &stream_len, sizeof(stream),
                                     noise, sizeof(noise), &telemetry, &info));
    assert(stream_len == 0U);
    uint8_t corrupt[sizeof(new_status_frame)] = { 0 };
    memcpy(corrupt, new_status_frame, sizeof(corrupt));
    corrupt[50U] ^= 1U;
    assert(!esp_bms_ant_protocol_feed(stream, &stream_len, sizeof(stream),
                                      corrupt, sizeof(corrupt), &telemetry, &info));
    assert(esp_bms_ant_protocol_feed(stream, &stream_len, sizeof(stream),
                                     new_status_frame, sizeof(new_status_frame), &telemetry, &info));

    /* A trailer inside payload is data, even when it straddles notifications. */
    memcpy(corrupt, new_status_frame, sizeof(corrupt));
    corrupt[34U] = 0xAAU;
    corrupt[35U] = 0x55U;
    update_crc(corrupt);
    stream_len = 0U;
    assert(!esp_bms_ant_protocol_feed(stream, &stream_len, sizeof(stream),
                                      corrupt, 36U, &telemetry, &info));
    assert(esp_bms_ant_protocol_feed(stream, &stream_len, sizeof(stream),
                                     corrupt + 36U, sizeof(corrupt) - 36U, &telemetry, &info));
    assert(telemetry.ant_diagnostics.cell_voltage_mv[0U] == (0x55AAU & 0x1FFFU));

    /* Identity and status may be delivered together; neither hides the other. */
    uint8_t identity[10] = { 0x7EU, 0xA1U, 0x12U, 0U, 0U, 0U, 0U, 0U, 0xAAU, 0x55U };
    update_crc(identity);
    memcpy(joined, identity, sizeof(identity));
    memcpy(joined + sizeof(identity), new_status_frame, sizeof(new_status_frame));
    stream_len = 0U;
    assert(esp_bms_ant_protocol_feed(stream, &stream_len, sizeof(stream), joined,
                                     sizeof(identity) + sizeof(new_status_frame), &telemetry, &info));
    assert(info && telemetry.soc_percent == 91U && stream_len == 0U);
    assert(!esp_bms_ant_protocol_feed(stream, &stream_len, sizeof(stream), identity,
                                      sizeof(identity), &telemetry, &info));
    assert(info);
    assert(!esp_bms_ant_protocol_decode(identity, sizeof(identity) - 1U, &telemetry, &info));
}

static void check_diagnostics(void)
{
    esp_bms_bms_telemetry_t telemetry = { 0 };
    assert(esp_bms_ant_protocol_decode(new_status_frame, sizeof(new_status_frame), &telemetry, NULL));
    assert(telemetry.ant_diagnostics.valid && telemetry.ant_diagnostics.cell_count == 16U);
    assert(telemetry.ant_diagnostics.cell_voltage_mv[0U] == 3300U);
    assert(telemetry.ant_diagnostics.soh_percent == 100U);
    assert(telemetry.ant_diagnostics.charge_mos_state == 1U);
    assert(telemetry.ant_diagnostics.discharge_mos_state == 1U);
    assert(!telemetry.ant_diagnostics.charge_time_valid && !telemetry.ant_diagnostics.remaining_charge_valid);

    /* This fixture ends at the old CRC. Only actual payload bytes count as extensions. */
    uint8_t extended[sizeof(new_status_frame) + 14U] = { 0 };
    for (uint8_t extra = 0U; extra <= 14U; ++extra) {
        memset(extended, 0, sizeof(extended));
        memcpy(extended, new_status_frame, sizeof(new_status_frame) - 4U);
        extended[5U] = (uint8_t)(new_status_frame[5U] + extra);
        write_u32(extended + 98U, UINT32_C(0xFFFFFFD6));
        extended[35U] |= 0xE0U;
        extended[84U] = 3U;
        extended[106U] = 0U;
        write_u32(extended + 148U, 12345U);
        write_u32(extended + 152U, 67890U);
        extended[156U] = 31U;
        extended[158U] = 42U;
        const size_t len = sizeof(new_status_frame) + extra;
        update_crc(extended);
        extended[len - 2U] = 0xAAU;
        extended[len - 1U] = 0x55U;
        assert(esp_bms_ant_protocol_decode(extended, len, &telemetry, NULL));
        const esp_bms_bms_ant_diagnostics_t *const d = &telemetry.ant_diagnostics;
        assert(d->power_w == -42 && d->cell_voltage_mv[0U] == 3300U);
        assert(d->balance_state == 3U && !telemetry.balancing_active);
        assert(d->charge_time_valid == (extra >= 4U));
        assert(d->last_charge_gap_valid == (extra >= 8U));
        assert(d->remaining_charge_valid == (extra >= 10U));
        assert(d->remaining_discharge_valid == (extra >= 12U));
        if (d->charge_time_valid) assert(d->charge_time_seconds == 12345U);
        if (d->last_charge_gap_valid) assert(d->last_charge_gap_seconds == 67890U);
        if (d->remaining_charge_valid) assert(d->remaining_charge_minutes == 31U);
        if (d->remaining_discharge_valid) assert(d->remaining_discharge_minutes == 42U);
    }
    uint8_t truncated[10] = { 0x7EU, 0xA1U, 0x11U, 0U, 0U, 0U, 0U, 0U, 0xAAU, 0x55U };
    update_crc(truncated);
    assert(!esp_bms_ant_protocol_decode(truncated, sizeof(truncated), &telemetry, NULL));
}

int main(void)
{
    check_new_stream();
    check_diagnostics();
    uint8_t request[6] = { 0 };
    assert(esp_bms_ant_protocol_old_poll_request(request));
    assert(memcmp(request, (const uint8_t[]){ 0xDBU, 0xDBU, 0x00U, 0x00U, 0x00U, 0x00U }, sizeof(request)) == 0);

    uint8_t stream[320] = { 0 };
    size_t stream_len = 0U;
    esp_bms_bms_telemetry_t telemetry = { 0 };
    bool device_info = false;
    assert(esp_bms_ant_protocol_decode(new_status_frame, sizeof(new_status_frame), &telemetry, &device_info));
    assert(!device_info && telemetry.pack_voltage_mv == 52840U && telemetry.current_deci_amps == 3 &&
           telemetry.soc_percent == 91U && telemetry.total_capacity_mah == 280000U &&
           telemetry.capacity_remaining_mah == 252602U && telemetry.total_cycle_valid &&
           telemetry.total_cycle_mah == 4862650U);
    assert(telemetry.running_time_valid && telemetry.running_time_seconds == 36591632U);
    assert(telemetry.min_cell_voltage_mv == 3300U && telemetry.max_cell_voltage_mv == 3305U &&
           telemetry.delta_cell_voltage_mv == 5U && telemetry.average_cell_voltage_mv == 3302U);

    assert(!esp_bms_ant_protocol_old_feed(stream, &stream_len, sizeof(stream), old_status_frame, 37U, &telemetry));
    assert(stream_len == 37U);
    assert(esp_bms_ant_protocol_old_feed(stream, &stream_len, sizeof(stream), old_status_frame + 37U,
                                         sizeof(old_status_frame) - 37U, &telemetry));
    assert(stream_len == 0U);
    /* 产品要求：ANT 旧协议按原始符号显示（充电为负），不做 Motexa 的反向修正。 */
    assert(telemetry.pack_voltage_mv == 26700U && telemetry.current_deci_amps == -107 &&
           telemetry.soc_percent == 41U && telemetry.total_capacity_mah == 0U &&
           telemetry.capacity_remaining_mah == 48487U && telemetry.total_cycle_valid &&
           telemetry.total_cycle_mah == 19276387U);
    assert(telemetry.running_time_valid && telemetry.running_time_seconds == 28405434U);
    assert(telemetry.min_cell_voltage_mv == 3338U && telemetry.max_cell_voltage_mv == 3340U &&
           telemetry.delta_cell_voltage_mv == 2U && telemetry.average_cell_voltage_mv == 3339U);
    assert(telemetry.temperature_valid[0U] && telemetry.temperatures_celsius[0U] == 15 &&
           telemetry.temperature_valid[5U] && telemetry.temperatures_celsius[5U] == 0);

    uint8_t joined[280] = { 0 };
    memcpy(joined, old_status_frame, sizeof(old_status_frame));
    memcpy(joined + sizeof(old_status_frame), old_status_frame, sizeof(old_status_frame));
    assert(esp_bms_ant_protocol_old_feed(stream, &stream_len, sizeof(stream), joined, sizeof(joined), &telemetry));
    assert(stream_len == sizeof(old_status_frame));
    assert(esp_bms_ant_protocol_old_feed(stream, &stream_len, sizeof(stream), joined, 1U, &telemetry));

    uint8_t corrupt[ESP_BMS_ANT_OLD_FRAME_LEN] = { 0 };
    memcpy(corrupt, old_status_frame, sizeof(corrupt));
    corrupt[138U] ^= 1U;
    stream_len = 0U;
    assert(!esp_bms_ant_protocol_old_feed(stream, &stream_len, sizeof(stream), corrupt, sizeof(corrupt), &telemetry));
    return 0;
}
