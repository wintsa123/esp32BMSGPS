#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_bms_gps_stream.h"

typedef struct {
    uint32_t lines;
    uint32_t overflows;
    char last_line[ESP_BMS_GPS_STREAM_CAPACITY + 1U];
} stream_result_t;

static void feed_text(esp_bms_gps_stream_t *stream,
                      stream_result_t *result,
                      const char *text)
{
    while (*text) {
        const esp_bms_gps_stream_event_t event =
            esp_bms_gps_stream_feed(stream, (uint8_t)*text++);
        if (event == ESP_BMS_GPS_STREAM_EVENT_LINE) {
            assert(stream->line_len <= ESP_BMS_GPS_STREAM_CAPACITY);
            memcpy(result->last_line, stream->line, stream->line_len);
            result->last_line[stream->line_len] = '\0';
            result->lines++;
        } else if (event == ESP_BMS_GPS_STREAM_EVENT_OVERFLOW) {
            result->overflows++;
        }
    }
}

static void test_noise_and_consecutive_rmc(void)
{
    esp_bms_gps_stream_t stream;
    stream_result_t result = { 0 };
    esp_bms_gps_stream_reset(&stream);

    feed_text(&stream, &result, "noise,,,145030.00,V,N*57\r\n");
    assert(result.lines == 0U && result.overflows == 0U);
    feed_text(&stream,
              &result,
              "$GNRMC,145030.00,V,,,,,,,130726,,,N*57\r\n"
              "$GNRMC,145031.00,A,,,,,0.100,,130726,,,A*00\n");
    assert(result.lines == 2U && result.overflows == 0U);
    assert(strcmp(result.last_line,
                  "$GNRMC,145031.00,A,,,,,0.100,,130726,,,A*00") == 0);
}

static void test_rmc_sentence_classification(void)
{
    static const uint8_t gnrmc[] = "$GNRMC,160017.00,A,,,,,,,130726,,,E,V*00";
    static const uint8_t gprmc[] = "$GPRMC,160017.00,V,,,,,,,130726,,,N*00";
    static const uint8_t corrupted_gsa[] =
        "$GNGSA,A,3,,,.41924,N,11315.24899,E,160018.00,A,E*77";
    static const uint8_t tail[] = ",,,145030.00,V,N*57";

    assert(esp_bms_gps_stream_line_is_rmc(gnrmc, sizeof(gnrmc) - 1U));
    assert(esp_bms_gps_stream_line_is_rmc(gprmc, sizeof(gprmc) - 1U));
    assert(!esp_bms_gps_stream_line_is_rmc(corrupted_gsa,
                                            sizeof(corrupted_gsa) - 1U));
    assert(!esp_bms_gps_stream_line_is_rmc(tail, sizeof(tail) - 1U));
}

static void test_nmea_checksum_validation(void)
{
    static const uint8_t valid[] =
        "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
    static const uint8_t missing[] =
        "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W";
    static const uint8_t trailing[] =
        "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6AX";
    static const uint8_t bad[] =
        "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*00";

    assert(esp_bms_gps_stream_nmea_checksum_valid(valid, sizeof(valid) - 1U));
    assert(!esp_bms_gps_stream_nmea_checksum_valid(missing, sizeof(missing) - 1U));
    assert(!esp_bms_gps_stream_nmea_checksum_valid(trailing, sizeof(trailing) - 1U));
    assert(!esp_bms_gps_stream_nmea_checksum_valid(bad, sizeof(bad) - 1U));
}

static void test_overflow_discards_whole_sentence(void)
{
    esp_bms_gps_stream_t stream;
    stream_result_t result = { 0 };
    esp_bms_gps_stream_reset(&stream);

    feed_text(&stream, &result, "$GNGSV,4,1,16,");
    for (size_t index = 0U; index < ESP_BMS_GPS_STREAM_CAPACITY + 32U; ++index) {
        feed_text(&stream, &result, "9");
    }
    feed_text(&stream, &result, ",,,145030.00,V,N*57\r\n");
    assert(result.lines == 0U && result.overflows == 1U);

    feed_text(&stream, &result, "$GNRMC,145030.00,V,,,,,,,130726,,,N*57\n");
    assert(result.lines == 1U && result.overflows == 1U);
    assert(strncmp(result.last_line, "$GNRMC,", 7U) == 0);
}

static void test_new_dollar_recovers_without_newline(void)
{
    esp_bms_gps_stream_t stream;
    stream_result_t result = { 0 };
    esp_bms_gps_stream_reset(&stream);

    feed_text(&stream,
              &result,
              "$GNRMC,broken$GNRMC,145030.00,V,,,,,,,130726,,,N*57\n");
    assert(result.lines == 1U && result.overflows == 0U);
    assert(strcmp(result.last_line,
                  "$GNRMC,145030.00,V,,,,,,,130726,,,N*57") == 0);
}

static void test_casbin_build_and_parse(void)
{
    static const uint8_t payload[] = { 0x09U, 0x07U, 0x03U, 0x00U };
    uint8_t frame[ESP_BMS_GPS_CASBIN_MAX_FRAME] = { 0 };
    const size_t frame_len = esp_bms_gps_casbin_build(0x06U,
                                                       0x10U,
                                                       payload,
                                                       sizeof(payload),
                                                       frame,
                                                       sizeof(frame));
    assert(frame_len == 14U);
    assert(frame[0] == 0xBAU && frame[1] == 0xCEU);

    esp_bms_gps_casbin_stream_t stream;
    esp_bms_gps_casbin_stream_reset(&stream);
    esp_bms_gps_casbin_event_t event = ESP_BMS_GPS_CASBIN_EVENT_NONE;
    for (size_t index = 0U; index < frame_len; ++index) {
        event = esp_bms_gps_casbin_stream_feed(&stream, frame[index]);
    }
    assert(event == ESP_BMS_GPS_CASBIN_EVENT_FRAME);
    assert(stream.message_class == 0x06U && stream.message_id == 0x10U);
    assert(stream.payload_len == sizeof(payload));
    assert(memcmp(&stream.frame[6], payload, sizeof(payload)) == 0);

    frame[frame_len - 1U] ^= 0x01U;
    esp_bms_gps_casbin_stream_reset(&stream);
    for (size_t index = 0U; index < frame_len; ++index) {
        event = esp_bms_gps_casbin_stream_feed(&stream, frame[index]);
    }
    assert(event == ESP_BMS_GPS_CASBIN_EVENT_ERROR);
}

static void test_casbin_zero_payload_query(void)
{
    uint8_t frame[ESP_BMS_GPS_CASBIN_MAX_FRAME] = { 0 };
    const size_t frame_len =
        esp_bms_gps_casbin_build(0x0AU, 0x0BU, NULL, 0U, frame, sizeof(frame));
    static const uint8_t expected[] = {
        0xBAU, 0xCEU, 0x00U, 0x00U, 0x0AU, 0x0BU, 0x00U, 0x00U, 0x0AU, 0x0BU,
    };
    assert(frame_len == sizeof(expected));
    assert(memcmp(frame, expected, sizeof(expected)) == 0);
}

static void test_casbin_agnss_payload_validation(void)
{
    static uint8_t igp_payload[ESP_BMS_GPS_CASBIN_MAX_PAYLOAD];
    static uint8_t frame[ESP_BMS_GPS_CASBIN_MAX_FRAME];
    static const uint8_t short_payload[4] = { 0 };
    igp_payload[14] = 254U;
    igp_payload[15] = 254U;

    assert(esp_bms_gps_casbin_agnss_payload_valid(0x08U,
                                                   0x17U,
                                                   igp_payload,
                                                   sizeof(igp_payload)));
    assert(!esp_bms_gps_casbin_agnss_payload_valid(0x08U,
                                                    0x07U,
                                                    short_payload,
                                                    sizeof(short_payload)));
    assert(esp_bms_gps_casbin_build(0x08U,
                                    0x17U,
                                    igp_payload,
                                    sizeof(igp_payload),
                                    frame,
                                    sizeof(frame)) == sizeof(frame));

    esp_bms_gps_casbin_stream_t stream;
    esp_bms_gps_casbin_stream_reset(&stream);
    uint32_t frames = 0U;
    for (size_t repeat = 0U; repeat < 2U; ++repeat) {
        for (size_t index = 0U; index < sizeof(frame); ++index) {
            if (esp_bms_gps_casbin_stream_feed(&stream, frame[index]) ==
                ESP_BMS_GPS_CASBIN_EVENT_FRAME) {
                frames++;
            }
        }
    }
    assert(frames == 2U);

    igp_payload[14] = 252U;
    assert(!esp_bms_gps_casbin_agnss_payload_valid(0x08U,
                                                    0x17U,
                                                    igp_payload,
                                                    sizeof(igp_payload)));
}

int main(void)
{
    test_noise_and_consecutive_rmc();
    test_rmc_sentence_classification();
    test_nmea_checksum_validation();
    test_overflow_discards_whole_sentence();
    test_new_dollar_recovers_without_newline();
    test_casbin_build_and_parse();
    test_casbin_zero_payload_query();
    test_casbin_agnss_payload_validation();
    puts("GPS stream self-test passed");
    return 0;
}
