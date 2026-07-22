#include "esp_bms_gps.h"

#include "esp_bms_gps_stream.h"
#include "esp_bms_idf_runtime.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_bms_profile_hardware.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "esp_bms_gps";

#define GPS_UART_PORT UART_NUM_1
#define GPS_UART_TX_GPIO ESP_BMS_PROFILE_GPS_TX
#define GPS_UART_RX_GPIO ESP_BMS_PROFILE_GPS_RX
#define GPS_UART_BAUD 115200
#define GPS_UART_RX_BUFFER_SIZE 1024U
#define GPS_PPS_GPIO ESP_BMS_PROFILE_GPS_PPS
#define GPS_PPS_TIMEOUT_MS 3000U
#define GPS_RMC_TIMEOUT_MS 3000U
#define GPS_DIAGNOSTIC_PERIOD_MS 60000U
#define GPS_SECURITY_QUERY_PERIOD_MS 1000U
#define GPS_SECURITY_JAM_CHANNEL_MASK 0x07U
#define GPS_CASBIN_CLASS_ACK 0x05U
#define GPS_CASBIN_CLASS_CFG 0x06U
#define GPS_CASBIN_CLASS_MON 0x0AU
#define GPS_CASBIN_ID_CFG_JSM 0x10U
#define GPS_CASBIN_ID_MON_SEC 0x0BU

typedef enum {
    GPS_PARSE_IGNORE,
    GPS_PARSE_ERROR,
    GPS_PARSE_FIX,
} gps_parse_result_t;

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} gps_datetime_t;

typedef struct {
    uart_port_t uart;
    esp_bms_gps_stream_t nmea_stream;
    esp_bms_gps_casbin_stream_t casbin_stream;
    volatile uint32_t pps_count;
    uint32_t pps_processed_count;
    uint32_t elapsed_ms;
    uint32_t rmc_elapsed_ms;
    uint32_t pps_elapsed_ms;
    uint32_t diagnostics_elapsed_ms;
    uint32_t security_elapsed_ms;
    uint32_t security_verify_elapsed_ms;
    uint32_t bytes_seen;
    uint32_t parse_errors;
    uint32_t casbin_errors;
    uint32_t overflow_lines;
    uint32_t rmc_valid_count;
    uint32_t rmc_invalid_count;
    uint8_t raw_sample[32];
    uint8_t raw_sample_len;
    uint8_t debug_lines_logged;
    uint8_t spoof_state;
    uint8_t jam_level;
    uint8_t security_config_attempts;
    bool uart_ready;
    bool pps_active;
    bool pps_ever_seen;
    bool rmc_seen;
    bool rmc_timed_out;
    esp_bms_gps_motion_filter_t motion_filter;
    bool security_state_valid;
    bool security_configured;
    bool security_verify_pending;
    bool agnss_injection_active;
} esp_bms_gps_state_t;

static esp_bms_gps_state_t s_gps;

static bool gps_uart_write(const uint8_t *data, size_t data_len)
{
    if (!data || data_len == 0U || !s_gps.uart_ready) {
        return false;
    }
    const int written = uart_write_bytes(s_gps.uart, data, data_len);
    if (written != (int)data_len) {
        ESP_LOGW(TAG, "UART write failed: requested=%u written=%d", (unsigned)data_len, written);
        return false;
    }
    return true;
}

static bool gps_send_text_command(const char *payload)
{
    if (!payload) {
        return false;
    }
    uint8_t checksum = 0U;
    for (const char *cursor = payload; *cursor != '\0'; ++cursor) {
        checksum ^= (uint8_t)*cursor;
    }
    char command[96] = { 0 };
    const int written = snprintf(command, sizeof(command), "$%s*%02X\r\n", payload, checksum);
    return written > 0 && (size_t)written < sizeof(command) &&
           gps_uart_write((const uint8_t *)command, (size_t)written);
}

static bool gps_send_casbin(uint8_t message_class,
                            uint8_t message_id,
                            const uint8_t *payload,
                            size_t payload_len)
{
    uint8_t frame[ESP_BMS_GPS_CASBIN_OVERHEAD + 4U] = { 0 };
    const size_t frame_len = esp_bms_gps_casbin_build(message_class,
                                                       message_id,
                                                       payload,
                                                       payload_len,
                                                       frame,
                                                       sizeof(frame));
    return frame_len > 0U && gps_uart_write(frame, frame_len);
}

static bool gps_parse_two_digits(const char *field, uint8_t *value)
{
    if (!field || !value || !isdigit((unsigned char)field[0]) ||
        !isdigit((unsigned char)field[1])) {
        return false;
    }
    *value = (uint8_t)(((uint8_t)(field[0] - '0') * 10U) + (uint8_t)(field[1] - '0'));
    return true;
}

static bool gps_parse_utc_time(const char *field, size_t field_len, gps_datetime_t *utc)
{
    if (!field || !utc || field_len < 6U || !gps_parse_two_digits(field, &utc->hour) ||
        !gps_parse_two_digits(field + 2U, &utc->minute) ||
        !gps_parse_two_digits(field + 4U, &utc->second) || utc->hour > 23U ||
        utc->minute > 59U || utc->second > 60U) {
        return false;
    }
    if (field_len == 6U) {
        return true;
    }
    if (field[6] != '.' || field_len == 7U) {
        return false;
    }
    for (size_t index = 7U; index < field_len; ++index) {
        if (!isdigit((unsigned char)field[index])) {
            return false;
        }
    }
    return true;
}

static gps_parse_result_t gps_parse_rmc(const uint8_t *line,
                                        size_t line_len,
                                        bool *fix_valid,
                                        uint32_t *speed_knots_milli,
                                        gps_datetime_t *utc)
{
    if (!line || !fix_valid || !speed_knots_milli || !utc || line_len == 0U) {
        return GPS_PARSE_ERROR;
    }
    if (!esp_bms_gps_stream_line_is_rmc(line, line_len)) {
        return GPS_PARSE_IGNORE;
    }
    if (!esp_bms_gps_stream_nmea_checksum_valid(line, line_len)) {
        return GPS_PARSE_ERROR;
    }

    const char *payload = (const char *)&line[1];
    const size_t payload_len = line_len - 4U;
    bool status_seen = false;
    bool speed_seen = false;
    bool time_seen = false;
    bool date_seen = false;
    uint8_t status = 'V';
    uint32_t parsed_speed_milli = 0U;
    size_t field_index = 0U;
    size_t field_start = 0U;

    for (size_t index = 0U; index <= payload_len; ++index) {
        if (index != payload_len && payload[index] != ',') {
            continue;
        }
        const char *field = &payload[field_start];
        const size_t field_len = index - field_start;
        switch (field_index) {
        case 1:
            if (!gps_parse_utc_time(field, field_len, utc)) {
                return GPS_PARSE_ERROR;
            }
            time_seen = true;
            break;
        case 2:
            if (field_len == 0U) {
                return GPS_PARSE_ERROR;
            }
            status = (uint8_t)field[0];
            status_seen = true;
            break;
        case 7:
            if (!esp_bms_gps_speed_knots_milli_parse(field,
                                                      field_len,
                                                      &parsed_speed_milli)) {
                return GPS_PARSE_ERROR;
            }
            speed_seen = true;
            break;
        case 9: {
            uint8_t short_year = 0U;
            if (field_len != 6U || !gps_parse_two_digits(field, &utc->day) ||
                !gps_parse_two_digits(field + 2U, &utc->month) ||
                !gps_parse_two_digits(field + 4U, &short_year) || utc->day == 0U ||
                utc->day > 31U || utc->month == 0U || utc->month > 12U) {
                return GPS_PARSE_ERROR;
            }
            utc->year = short_year >= 80U ? (uint16_t)(1900U + short_year)
                                           : (uint16_t)(2000U + short_year);
            date_seen = true;
            break;
        }
        default:
            break;
        }
        field_index++;
        field_start = index + 1U;
    }

    if (!status_seen || !speed_seen || !time_seen || !date_seen) {
        return GPS_PARSE_ERROR;
    }
    *fix_valid = status == 'A';
    *speed_knots_milli = parsed_speed_milli;
    return GPS_PARSE_FIX;
}

static bool gps_is_leap_year(uint16_t year)
{
    return (year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U;
}

static uint8_t gps_days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t days[] = { 31U, 28U, 31U, 30U, 31U, 30U,
                                    31U, 31U, 30U, 31U, 30U, 31U };
    if (month == 0U || month > 12U) {
        return 0U;
    }
    return month == 2U && gps_is_leap_year(year) ? 29U : days[month - 1U];
}

static bool gps_utc_to_local_utc8(const gps_datetime_t *utc, gps_datetime_t *local)
{
    if (!utc || !local || utc->year == 0U || utc->month == 0U || utc->month > 12U ||
        utc->day == 0U || utc->day > gps_days_in_month(utc->year, utc->month)) {
        return false;
    }
    *local = *utc;
    const uint8_t shifted_hour = (uint8_t)(utc->hour + 8U);
    local->hour = (uint8_t)(shifted_hour % 24U);
    if (shifted_hour < 24U) {
        return true;
    }
    local->day++;
    if (local->day <= gps_days_in_month(local->year, local->month)) {
        return true;
    }
    local->day = 1U;
    local->month++;
    if (local->month <= 12U) {
        return true;
    }
    if (local->year == UINT16_MAX) {
        return false;
    }
    local->month = 1U;
    local->year++;
    return true;
}

static bool gps_set_module_state(esp_bms_idf_runtime_t *runtime,
                                 esp_bms_gps_module_state_t state,
                                 const char *reason)
{
    return esp_bms_idf_runtime_set_gps_module_state(runtime, state, reason);
}

static void gps_pps_isr(void *arg)
{
    esp_bms_gps_state_t *state = (esp_bms_gps_state_t *)arg;
    state->pps_count++;
}

static void gps_init_pps(void)
{
    if (GPS_PPS_GPIO == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "PPS GPIO is not configured");
        return;
    }
    const gpio_config_t config = {
        .pin_bit_mask = UINT64_C(1) << GPS_PPS_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    esp_err_t ret = gpio_config(&config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PPS GPIO%d config failed: %s", GPS_PPS_GPIO, esp_err_to_name(ret));
        return;
    }
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "PPS ISR service failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = gpio_isr_handler_add(GPS_PPS_GPIO, gps_pps_isr, &s_gps);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PPS GPIO%d handler failed: %s", GPS_PPS_GPIO, esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "PPS ready: gpio=%d edge=rising pull=external", GPS_PPS_GPIO);
}

static void gps_configure_receiver(void)
{
    const bool rate_ok = gps_send_text_command("PCAS02,100");
    const bool output_ok = gps_send_text_command("PCAS03,9,0,9,9,1,0,0,0,0,0,,,0,0");
    (void)gps_send_text_command("PCAS06,2");
    (void)gps_send_text_command("PCAS06,4");
    const bool security_query_ok =
        gps_send_casbin(GPS_CASBIN_CLASS_CFG, GPS_CASBIN_ID_CFG_JSM, NULL, 0U);
    ESP_LOGI(TAG,
             "receiver config requested: rate=10Hz output=%s security_query=%s",
             rate_ok && output_ok ? "rmc10_diag1" : "failed",
             security_query_ok ? "sent" : "failed");
}

static esp_err_t gps_init_uart(esp_bms_idf_runtime_t *runtime)
{
    if (GPS_UART_RX_GPIO == GPIO_NUM_NC || GPS_UART_TX_GPIO == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "GPS UART GPIO is not configured");
        (void)gps_set_module_state(runtime, ESP_BMS_GPS_MODULE_UNAVAILABLE, "uart-gpio");
        return ESP_ERR_INVALID_ARG;
    }
    const uart_config_t config = {
        .baud_rate = GPS_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    s_gps.uart = GPS_UART_PORT;
    esp_err_t ret = uart_param_config(s_gps.uart, &config);
    if (ret == ESP_OK) {
        ret = uart_set_pin(s_gps.uart, GPS_UART_TX_GPIO, GPS_UART_RX_GPIO,
                           UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (ret == ESP_OK && !uart_is_driver_installed(s_gps.uart)) {
        ret = uart_driver_install(s_gps.uart, GPS_UART_RX_BUFFER_SIZE, 0, 0, NULL, 0);
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "UART init failed: %s", esp_err_to_name(ret));
        (void)gps_set_module_state(runtime, ESP_BMS_GPS_MODULE_UNAVAILABLE, "uart-init");
        return ret;
    }
    s_gps.uart_ready = true;
    ESP_LOGI(TAG, "UART ready: uart=%d rx=%d tx=%d baud=%d",
             s_gps.uart, GPS_UART_RX_GPIO, GPS_UART_TX_GPIO, GPS_UART_BAUD);
    gps_configure_receiver();
    return ESP_OK;
}

static void gps_apply_security_config(const uint8_t *payload)
{
    const bool configured = (payload[0] & 0x01U) != 0U &&
                            (payload[1] & GPS_SECURITY_JAM_CHANNEL_MASK) ==
                                GPS_SECURITY_JAM_CHANNEL_MASK;
    if (configured) {
        s_gps.security_configured = true;
        s_gps.security_verify_pending = false;
        return;
    }
    if (s_gps.security_config_attempts >= 2U) {
        ESP_LOGW(TAG, "spoof/jam detection remains disabled");
        return;
    }
    uint8_t desired[4] = { payload[0], payload[1], payload[2], payload[3] };
    desired[0] |= 0x01U;
    desired[1] |= GPS_SECURITY_JAM_CHANNEL_MASK;
    if (gps_send_casbin(GPS_CASBIN_CLASS_CFG, GPS_CASBIN_ID_CFG_JSM, desired, sizeof(desired))) {
        s_gps.security_config_attempts++;
        s_gps.security_verify_pending = true;
        s_gps.security_verify_elapsed_ms = 0U;
    }
}

static void gps_apply_casbin(const esp_bms_gps_casbin_stream_t *stream)
{
    const uint8_t *payload = &stream->frame[6];
    if (stream->message_class == GPS_CASBIN_CLASS_CFG &&
        stream->message_id == GPS_CASBIN_ID_CFG_JSM && stream->payload_len == 4U) {
        gps_apply_security_config(payload);
        return;
    }
    if (stream->message_class == GPS_CASBIN_CLASS_ACK && stream->payload_len == 4U &&
        payload[0] == GPS_CASBIN_CLASS_CFG && payload[1] == GPS_CASBIN_ID_CFG_JSM) {
        if (stream->message_id == 0x00U) {
            s_gps.security_verify_pending = false;
            ESP_LOGW(TAG, "spoof/jam configuration rejected by receiver");
        }
        return;
    }
    if (stream->message_class != GPS_CASBIN_CLASS_MON ||
        stream->message_id != GPS_CASBIN_ID_MON_SEC || stream->payload_len != 4U) {
        return;
    }
    s_gps.spoof_state = payload[1];
    s_gps.jam_level = payload[3];
    s_gps.security_configured = (payload[0] & 0x01U) != 0U &&
                                (payload[2] & GPS_SECURITY_JAM_CHANNEL_MASK) ==
                                    GPS_SECURITY_JAM_CHANNEL_MASK;
    s_gps.security_state_valid = true;
}

static bool gps_apply_line(esp_bms_idf_runtime_t *runtime, const uint8_t *line, size_t line_len)
{
    bool fix_valid = false;
    uint32_t speed_knots_milli = 0U;
    gps_datetime_t utc = { 0 };
    const gps_parse_result_t result =
        gps_parse_rmc(line, line_len, &fix_valid, &speed_knots_milli, &utc);
    if (result == GPS_PARSE_IGNORE) {
        return false;
    }
    if (result == GPS_PARSE_ERROR) {
        s_gps.parse_errors++;
        return false;
    }
    if (fix_valid) {
        s_gps.rmc_valid_count++;
    } else {
        s_gps.rmc_invalid_count++;
    }
    s_gps.rmc_seen = true;
    s_gps.rmc_timed_out = false;
    s_gps.rmc_elapsed_ms = 0U;
    gps_datetime_t local = { 0 };
    const bool local_valid = gps_utc_to_local_utc8(&utc, &local);
    esp_bms_idf_runtime_publish_gps_datetime(runtime,
                                              local.year,
                                              local.month,
                                              local.day,
                                              local.hour,
                                              local.minute,
                                              local_valid);
    return esp_bms_idf_runtime_publish_gps_sample(
        runtime,
        fix_valid,
        esp_bms_gps_motion_filter_apply(&s_gps.motion_filter,
                                        fix_valid,
                                        speed_knots_milli));
}

static bool gps_feed_byte(esp_bms_idf_runtime_t *runtime, uint8_t byte)
{
    const bool casbin_was_active = esp_bms_gps_casbin_stream_active(&s_gps.casbin_stream);
    const esp_bms_gps_casbin_event_t casbin_event =
        esp_bms_gps_casbin_stream_feed(&s_gps.casbin_stream, byte);
    const bool casbin_is_active = esp_bms_gps_casbin_stream_active(&s_gps.casbin_stream);
    if (casbin_was_active || casbin_is_active || casbin_event != ESP_BMS_GPS_CASBIN_EVENT_NONE) {
        if (!casbin_was_active && casbin_is_active) {
            esp_bms_gps_stream_reset(&s_gps.nmea_stream);
        }
        if (casbin_event == ESP_BMS_GPS_CASBIN_EVENT_FRAME) {
            const bool detected = gps_set_module_state(runtime,
                                                       ESP_BMS_GPS_MODULE_AVAILABLE,
                                                       "casbin-frame");
            gps_apply_casbin(&s_gps.casbin_stream);
            return detected;
        }
        if (casbin_event == ESP_BMS_GPS_CASBIN_EVENT_ERROR) {
            s_gps.casbin_errors++;
        }
        return false;
    }

    const esp_bms_gps_stream_event_t event = esp_bms_gps_stream_feed(&s_gps.nmea_stream, byte);
    if (event == ESP_BMS_GPS_STREAM_EVENT_OVERFLOW) {
        s_gps.overflow_lines++;
        return false;
    }
    if (event != ESP_BMS_GPS_STREAM_EVENT_LINE) {
        return false;
    }
    const bool detected =
        esp_bms_gps_stream_nmea_checksum_valid(s_gps.nmea_stream.line, s_gps.nmea_stream.line_len) &&
        gps_set_module_state(runtime, ESP_BMS_GPS_MODULE_AVAILABLE, "nmea-checksum");
    return gps_apply_line(runtime, s_gps.nmea_stream.line, s_gps.nmea_stream.line_len) || detected;
}

static bool gps_poll_uart(esp_bms_idf_runtime_t *runtime)
{
    if (!s_gps.uart_ready) {
        return false;
    }
    size_t available = 0U;
    if (uart_get_buffered_data_len(s_gps.uart, &available) != ESP_OK || available == 0U) {
        return false;
    }
    uint8_t bytes[ESP_BMS_GPS_STREAM_CAPACITY];
    if (available > sizeof(bytes)) {
        available = sizeof(bytes);
    }
    const int read = uart_read_bytes(s_gps.uart, bytes, (uint32_t)available, 0);
    if (read <= 0) {
        return false;
    }
    s_gps.bytes_seen += (uint32_t)read;
    if (s_gps.raw_sample_len < sizeof(s_gps.raw_sample)) {
        size_t sample_len = sizeof(s_gps.raw_sample) - s_gps.raw_sample_len;
        if (sample_len > (size_t)read) {
            sample_len = (size_t)read;
        }
        memcpy(&s_gps.raw_sample[s_gps.raw_sample_len], bytes, sample_len);
        s_gps.raw_sample_len += (uint8_t)sample_len;
    }
    bool changed = false;
    for (int index = 0; index < read; ++index) {
        changed = gps_feed_byte(runtime, bytes[index]) || changed;
    }
    return changed;
}

static esp_err_t gps_send_text(httpd_req_t *req, const char *status, const char *text)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t gps_http_handler(httpd_req_t *req, void *context)
{
    esp_bms_gps_state_t *state = (esp_bms_gps_state_t *)context;
    if (!req || !state || req->method != HTTP_POST || strcmp(req->uri, "/api/gps/agnss") != 0) {
        return ESP_ERR_NOT_FOUND;
    }
    if (!state->uart_ready) {
        return gps_send_text(req, "503 Service Unavailable", "GPS module unavailable");
    }
    if (req->content_len < ESP_BMS_GPS_CASBIN_OVERHEAD) {
        return gps_send_text(req, "400 Bad Request", "A-GNSS payload is empty");
    }
    if (req->content_len > ESP_BMS_GPS_CASBIN_MAX_FRAME) {
        return gps_send_text(req, "413 Payload Too Large", "A-GNSS payload is too large");
    }

    esp_bms_gps_casbin_stream_t stream;
    esp_bms_gps_casbin_stream_reset(&stream);
    uint8_t buffer[256] = { 0 };
    size_t remaining = req->content_len;
    uint32_t packet_count = 0U;
    esp_err_t result = ESP_OK;
    state->agnss_injection_active = true;
    while (remaining > 0U && result == ESP_OK) {
        const size_t requested = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        const int received = httpd_req_recv(req, (char *)buffer, requested);
        if (received <= 0) {
            result = received == HTTPD_SOCK_ERR_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
            break;
        }
        remaining -= (size_t)received;
        for (int index = 0; index < received; ++index) {
            const bool was_active = esp_bms_gps_casbin_stream_active(&stream);
            const esp_bms_gps_casbin_event_t event =
                esp_bms_gps_casbin_stream_feed(&stream, buffer[index]);
            if ((!was_active && buffer[index] != 0xBAU && event == ESP_BMS_GPS_CASBIN_EVENT_NONE) ||
                event == ESP_BMS_GPS_CASBIN_EVENT_ERROR) {
                result = ESP_ERR_INVALID_CRC;
                break;
            }
            if (event == ESP_BMS_GPS_CASBIN_EVENT_FRAME) {
                packet_count++;
            }
        }
    }
    if (result == ESP_OK &&
        (remaining != 0U || esp_bms_gps_casbin_stream_active(&stream) || packet_count != 1U ||
         stream.frame_len != req->content_len)) {
        result = ESP_ERR_INVALID_SIZE;
    }
    if (result == ESP_OK && !esp_bms_gps_casbin_agnss_payload_valid(stream.message_class,
                                                                     stream.message_id,
                                                                     &stream.frame[6],
                                                                     stream.payload_len)) {
        result = ESP_ERR_NOT_SUPPORTED;
    }
    if (result == ESP_OK && !gps_uart_write(stream.frame, stream.frame_len)) {
        result = ESP_FAIL;
    }
    state->agnss_injection_active = false;
    if (result != ESP_OK) {
        const char *status = result == ESP_ERR_TIMEOUT ? "408 Request Timeout" :
                             result == ESP_FAIL ? "500 Internal Server Error" : "400 Bad Request";
        return gps_send_text(req, status, "invalid A-GNSS CASBIN payload");
    }
    char json[96] = { 0 };
    (void)snprintf(json, sizeof(json),
                   "{\"status\":\"injected\",\"packets\":%lu,\"bytes\":%u}",
                   (unsigned long)packet_count, (unsigned)stream.frame_len);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

esp_err_t esp_bms_gps_init(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(&s_gps, 0, sizeof(s_gps));
    esp_bms_idf_runtime_register_optional_http_handler(runtime, gps_http_handler, &s_gps);
    gps_init_pps();
    return gps_init_uart(runtime);
}

bool esp_bms_gps_tick(esp_bms_idf_runtime_t *runtime, uint32_t elapsed_ms)
{
    if (!runtime || !s_gps.uart_ready) {
        return false;
    }
    s_gps.elapsed_ms += elapsed_ms;
    s_gps.rmc_elapsed_ms += elapsed_ms;
    s_gps.pps_elapsed_ms += elapsed_ms;
    s_gps.diagnostics_elapsed_ms += elapsed_ms;
    s_gps.security_elapsed_ms += elapsed_ms;
    if (s_gps.security_verify_pending) {
        s_gps.security_verify_elapsed_ms += elapsed_ms;
        if (s_gps.security_verify_elapsed_ms >= 1000U) {
            s_gps.security_verify_pending = false;
            (void)gps_send_casbin(GPS_CASBIN_CLASS_CFG, GPS_CASBIN_ID_CFG_JSM, NULL, 0U);
        }
    }
    if (!s_gps.agnss_injection_active && s_gps.security_elapsed_ms >= GPS_SECURITY_QUERY_PERIOD_MS) {
        s_gps.security_elapsed_ms = 0U;
        (void)gps_send_casbin(GPS_CASBIN_CLASS_MON, GPS_CASBIN_ID_MON_SEC, NULL, 0U);
    }

    bool changed = gps_poll_uart(runtime);
    const uint32_t pps_count = s_gps.pps_count;
    if (pps_count != s_gps.pps_processed_count) {
        s_gps.pps_processed_count = pps_count;
        s_gps.pps_active = true;
        s_gps.pps_ever_seen = true;
        s_gps.pps_elapsed_ms = 0U;
    } else if (s_gps.pps_active && s_gps.pps_elapsed_ms >= GPS_PPS_TIMEOUT_MS) {
        s_gps.pps_active = false;
    }
    if (s_gps.rmc_seen && !s_gps.rmc_timed_out && s_gps.rmc_elapsed_ms >= GPS_RMC_TIMEOUT_MS) {
        s_gps.rmc_timed_out = true;
        changed = esp_bms_idf_runtime_timeout_gps(runtime) || changed;
        ESP_LOGW(TAG, "RMC timeout");
    }
    if (s_gps.diagnostics_elapsed_ms >= GPS_DIAGNOSTIC_PERIOD_MS) {
        s_gps.diagnostics_elapsed_ms = 0U;
        ESP_LOGI(TAG,
                 "summary: fix=%d pps=%d A=%lu V=%lu overflow=%lu parse_errors=%lu casbin_errors=%lu bytes=%lu",
                 esp_bms_dashboard_snapshot_flag_get(&runtime->snapshot,
                                                     ESP_BMS_DASHBOARD_FLAG_GPS_FIX_VALID) ? 1 : 0,
                 s_gps.pps_active ? 1 : 0,
                 (unsigned long)s_gps.rmc_valid_count,
                 (unsigned long)s_gps.rmc_invalid_count,
                 (unsigned long)s_gps.overflow_lines,
                 (unsigned long)s_gps.parse_errors,
                 (unsigned long)s_gps.casbin_errors,
                 (unsigned long)s_gps.bytes_seen);
    }
    return changed;
}

void esp_bms_gps_stop(esp_bms_idf_runtime_t *runtime)
{
    if (s_gps.uart_ready) {
        (void)uart_driver_delete(s_gps.uart);
    }
    if (GPS_PPS_GPIO != GPIO_NUM_NC) {
        (void)gpio_isr_handler_remove(GPS_PPS_GPIO);
    }
    s_gps.uart_ready = false;
    if (runtime) {
        esp_bms_idf_runtime_register_optional_http_handler(runtime, NULL, NULL);
        (void)gps_set_module_state(runtime, ESP_BMS_GPS_MODULE_UNAVAILABLE, "stopped");
    }
}

esp_bms_gps_module_state_t esp_bms_gps_module_state(const esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || runtime->snapshot.gps_module_state > (uint8_t)ESP_BMS_GPS_MODULE_UNAVAILABLE) {
        return ESP_BMS_GPS_MODULE_UNAVAILABLE;
    }
    return (esp_bms_gps_module_state_t)runtime->snapshot.gps_module_state;
}

bool esp_bms_gps_finish_startup_probe(esp_bms_idf_runtime_t *runtime)
{
    if (esp_bms_gps_module_state(runtime) != ESP_BMS_GPS_MODULE_PROBING) {
        return false;
    }
    return gps_set_module_state(runtime, ESP_BMS_GPS_MODULE_UNAVAILABLE, "startup-timeout");
}
