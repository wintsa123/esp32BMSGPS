#include "host_ble_client.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET host_socket_t;
#define HOST_INVALID_SOCKET INVALID_SOCKET
#define host_socket_close closesocket
#else
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int host_socket_t;
#define HOST_INVALID_SOCKET (-1)
#define host_socket_close close
#endif

#include "esp_bms_ant_protocol.h"
#include "esp_bms_daly_protocol.h"
#include "esp_bms_jbd_protocol.h"
#include "esp_bms_jk_protocol.h"
#include "esp_bms_yanyang_protocol.h"
#include "esp_fardriver_protocol.h"

#define HOST_BLE_RX_CAPACITY 16384U
#define HOST_BLE_LINE_CAPACITY 4096U
#define HOST_BLE_FRAME_CAPACITY 512U
#define HOST_BLE_BMS_POLL_PERIOD_MS 500U
#define HOST_BLE_CONTROLLER_READ_PERIOD_MS 200U
#define HOST_BLE_CONTROLLER_KEEPALIVE_PERIOD_MS 3000U
#define HOST_BLE_BMS_HEARTBEAT_TIMEOUT_MS 30000U
#define HOST_BLE_TX_CAPACITY 8192U

enum {
    HOST_BMS_TYPE_ANT = 0,
    HOST_BMS_TYPE_JK = 1,
    HOST_BMS_TYPE_JBD = 2,
    HOST_BMS_TYPE_DALY = 3,
    HOST_BMS_TYPE_YANYANG = 4,
};

enum {
    HOST_ANT_PROBE_NEW = 0,
    HOST_ANT_PROBE_OLD = 1,
    HOST_ANT_NEW = 2,
    HOST_ANT_OLD = 3,
};

struct esp_bms_host_ble_client {
    host_socket_t socket;
    bool socket_runtime_initialized;
    bool ready;
    bool bms_connected;
    bool controller_connected;
    bool controller_read_polling;
    uint8_t bms_type;
    uint8_t bms_poll_index;
    uint8_t controller_poll_index;
    uint8_t ant_mode;
    uint8_t frame[HOST_BLE_FRAME_CAPACITY];
    size_t frame_len;
    uint8_t controller_frame[ESP_FARDRIVER_FRAME_LEN * 2U];
    size_t controller_frame_len;
    esp_fardriver_state_t controller_state;
    uint32_t bms_poll_elapsed_ms;
    uint32_t bms_telemetry_elapsed_ms;
    uint32_t controller_poll_elapsed_ms;
    char tx[HOST_BLE_TX_CAPACITY];
    size_t tx_len;
    size_t tx_offset;
    char rx[HOST_BLE_RX_CAPACITY];
    size_t rx_len;
};

static void snapshot_flag_set(esp_bms_dashboard_snapshot_t *snapshot,
                              uint32_t flag,
                              bool enabled)
{
    esp_bms_dashboard_snapshot_flag_set(snapshot, flag, enabled);
}

static const char *source_name(esp_bms_host_ble_source_t source)
{
    return source == ESP_BMS_HOST_BLE_SOURCE_CONTROLLER ? "CONTROLLER" : "BMS";
}

static bool socket_would_block(void)
{
#ifdef _WIN32
    const int error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

static bool socket_set_nonblocking(host_socket_t socket)
{
#ifdef _WIN32
    u_long enabled = 1U;
    return ioctlsocket(socket, FIONBIO, &enabled) == 0;
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static bool flush_tx(esp_bms_host_ble_client_t *client)
{
    if (!client || client->socket == HOST_INVALID_SOCKET) return false;
    while (client->tx_offset < client->tx_len) {
        const int result = send(client->socket,
                                client->tx + client->tx_offset,
                                (int)(client->tx_len - client->tx_offset),
                                0);
        if (result > 0) {
            client->tx_offset += (size_t)result;
            continue;
        }
        if (result < 0 && socket_would_block()) return true;
        return false;
    }
    client->tx_len = 0U;
    client->tx_offset = 0U;
    return true;
}

static bool send_line(esp_bms_host_ble_client_t *client, const char *format, ...)
{
    if (!client || client->socket == HOST_INVALID_SOCKET || !format) {
        return false;
    }
    char line[HOST_BLE_LINE_CAPACITY];
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(line, sizeof(line) - 2U, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= sizeof(line) - 2U) {
        return false;
    }
    size_t total = (size_t)written;
    line[total++] = '\n';
    if (client->tx_offset != 0U) {
        memmove(client->tx, client->tx + client->tx_offset, client->tx_len - client->tx_offset);
        client->tx_len -= client->tx_offset;
        client->tx_offset = 0U;
    }
    if (total > sizeof(client->tx) - client->tx_len) return false;
    memcpy(client->tx + client->tx_len, line, total);
    client->tx_len += total;
    return flush_tx(client);
}

static bool hex_decode(const char *hex, uint8_t *out, size_t out_capacity, size_t *out_len)
{
    if (!hex || !out || !out_len) {
        return false;
    }
    const size_t length = strlen(hex);
    if ((length & 1U) != 0U || length / 2U > out_capacity) {
        return false;
    }
    for (size_t index = 0U; index < length / 2U; ++index) {
        unsigned value = 0U;
        if (sscanf(hex + index * 2U, "%2x", &value) != 1) {
            return false;
        }
        out[index] = (uint8_t)value;
    }
    *out_len = length / 2U;
    return true;
}

static void hex_to_text(const char *hex, char *out, size_t out_capacity)
{
    if (!out || out_capacity == 0U) {
        return;
    }
    size_t length = 0U;
    if (!hex || !hex_decode(hex, (uint8_t *)out, out_capacity - 1U, &length)) {
        out[0] = '\0';
        return;
    }
    out[length] = '\0';
}

static bool send_bytes(esp_bms_host_ble_client_t *client,
                       esp_bms_host_ble_source_t source,
                       const uint8_t *bytes,
                       size_t length)
{
    if (!client || !bytes || length == 0U || length * 2U + 32U > HOST_BLE_LINE_CAPACITY) {
        return false;
    }
    char hex[HOST_BLE_LINE_CAPACITY];
    for (size_t index = 0U; index < length; ++index) {
        (void)snprintf(hex + index * 2U, 3U, "%02X", bytes[index]);
    }
    return send_line(client, "WRITE\t%s\t%s", source_name(source), hex);
}

static char *next_field(char **cursor)
{
    if (!cursor || !*cursor) {
        return NULL;
    }
    char *field = *cursor;
    char *separator = strchr(field, '\t');
    if (separator) {
        *separator = '\0';
        *cursor = separator + 1;
    } else {
        *cursor = NULL;
    }
    return field;
}

static esp_bms_host_ble_source_t parse_source(const char *field)
{
    return field && strcmp(field, "CONTROLLER") == 0
               ? ESP_BMS_HOST_BLE_SOURCE_CONTROLLER
               : ESP_BMS_HOST_BLE_SOURCE_BMS;
}

static void clear_bms_telemetry(esp_bms_dashboard_snapshot_t *snapshot)
{
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_BMS_ONLINE, false);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_PACK_VOLTAGE_VALID, false);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_CURRENT_VALID, false);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_SOC_VALID, false);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_MIN_CELL_VALID, false);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_AVERAGE_CELL_VALID, false);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_MAX_CELL_VALID, false);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_DELTA_CELL_VALID, false);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_TOTAL_CAPACITY_VALID, false);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_CAPACITY_REMAINING_VALID, false);
    snapshot->pack_voltage_mv = 0U;
    snapshot->current_deci_amps = 0;
    snapshot->soc_percent = 0U;
    snapshot->min_cell_voltage_mv = 0U;
    snapshot->average_cell_voltage_mv = 0U;
    snapshot->max_cell_voltage_mv = 0U;
    snapshot->delta_cell_voltage_mv = 0U;
    snapshot->total_capacity_mah = 0U;
    snapshot->capacity_remaining_mah = 0U;
    snapshot->bms_running_time_seconds = 0U;
    snapshot->bms_running_time_valid = false;
    snapshot->bms_cycle_capacity_mah = 0U;
    snapshot->bms_cycle_capacity_valid = false;
    snapshot->bms_protection_count = 0U;
    snapshot->bms_warning_count = 0U;
    snapshot->bms_safety_supported_mask = 0U;
    snapshot->bms_safety_active_mask = 0U;
    memset(snapshot->bms_protection_codes, 0, sizeof(snapshot->bms_protection_codes));
    memset(snapshot->bms_warning_codes, 0, sizeof(snapshot->bms_warning_codes));
    memset(snapshot->bms_temperature_celsius, 0, sizeof(snapshot->bms_temperature_celsius));
    for (uint8_t index = 0U; index < ESP_BMS_BMS_TEMP_MAX_COUNT; ++index) {
        esp_bms_dashboard_snapshot_temperature_valid_set(snapshot, index, false);
    }
    (void)snprintf(snapshot->bms_info_text, sizeof(snapshot->bms_info_text), "BMS OFF");
}

static void clear_controller_telemetry(esp_bms_dashboard_snapshot_t *snapshot)
{
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_CONTROLLER_ONLINE, false);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_CONTROLLER_SPEED_VALID, false);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_CONTROLLER_RPM_VALID, false);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_CONTROLLER_GEAR_VALID, false);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_CONTROLLER_POWER_VALID, false);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_CONTROLLER_TEMP_VALID, false);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_MOTOR_TEMP_VALID, false);
}

static void apply_fault_codes(esp_bms_dashboard_snapshot_t *snapshot,
                              uint64_t protection,
                              uint64_t warning)
{
    snapshot->bms_protection_count = 0U;
    snapshot->bms_warning_count = 0U;
    memset(snapshot->bms_protection_codes, 0, sizeof(snapshot->bms_protection_codes));
    memset(snapshot->bms_warning_codes, 0, sizeof(snapshot->bms_warning_codes));
    for (uint8_t bit = 0U; bit < 64U; ++bit) {
        if ((protection & (UINT64_C(1) << bit)) != 0U &&
            snapshot->bms_protection_count < ESP_BMS_BMS_CODE_MAX_COUNT) {
            (void)snprintf(snapshot->bms_protection_codes[snapshot->bms_protection_count++],
                           ESP_BMS_BMS_CODE_TEXT_LEN,
                           "P%02u",
                           (unsigned)bit);
        }
        if ((warning & (UINT64_C(1) << bit)) != 0U &&
            snapshot->bms_warning_count < ESP_BMS_BMS_CODE_MAX_COUNT) {
            (void)snprintf(snapshot->bms_warning_codes[snapshot->bms_warning_count++],
                           ESP_BMS_BMS_CODE_TEXT_LEN,
                           "W%02u",
                           (unsigned)bit);
        }
    }
}

static void apply_bms_telemetry(esp_bms_dashboard_snapshot_t *snapshot,
                                const esp_bms_bms_telemetry_t *telemetry)
{
    if (!snapshot || !telemetry) {
        return;
    }
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_BMS_ONLINE, true);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_PACK_VOLTAGE_VALID,
                      telemetry->pack_voltage_mv != 0U);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_MIN_CELL_VALID,
                      telemetry->min_cell_voltage_mv != 0U);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_AVERAGE_CELL_VALID,
                      telemetry->average_cell_voltage_mv != 0U);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_MAX_CELL_VALID,
                      telemetry->max_cell_voltage_mv != 0U);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_DELTA_CELL_VALID,
                      telemetry->max_cell_voltage_mv != 0U);
    snapshot->pack_voltage_mv = telemetry->pack_voltage_mv;
    snapshot->min_cell_voltage_mv = telemetry->min_cell_voltage_mv;
    snapshot->average_cell_voltage_mv = telemetry->average_cell_voltage_mv;
    snapshot->max_cell_voltage_mv = telemetry->max_cell_voltage_mv;
    snapshot->delta_cell_voltage_mv = telemetry->delta_cell_voltage_mv;
    if (!telemetry->partial) {
        snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_CURRENT_VALID, true);
        snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_SOC_VALID,
                          telemetry->soc_percent <= 100U);
        snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_TOTAL_CAPACITY_VALID,
                          telemetry->total_capacity_mah != 0U);
        snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_CAPACITY_REMAINING_VALID,
                          telemetry->capacity_remaining_mah != 0U);
        snapshot->current_deci_amps = telemetry->current_deci_amps;
        snapshot->soc_percent = telemetry->soc_percent;
        snapshot->total_capacity_mah = telemetry->total_capacity_mah;
        snapshot->capacity_remaining_mah = telemetry->capacity_remaining_mah;
        snapshot->bms_running_time_seconds = telemetry->running_time_seconds;
        snapshot->bms_running_time_valid = telemetry->running_time_valid;
        snapshot->bms_cycle_capacity_mah = telemetry->total_cycle_mah;
        snapshot->bms_cycle_capacity_valid = telemetry->total_cycle_valid;
        apply_fault_codes(snapshot, telemetry->protection_mask, telemetry->warning_mask);
        for (uint8_t index = 0U; index < ESP_BMS_BMS_TEMP_MAX_COUNT; ++index) {
            snapshot->bms_temperature_celsius[index] = telemetry->temperatures_celsius[index];
            esp_bms_dashboard_snapshot_temperature_valid_set(
                snapshot, index, telemetry->temperature_valid[index]);
        }
    }
    snapshot->bms_error_text[0] = '\0';
    (void)snprintf(snapshot->bms_info_text, sizeof(snapshot->bms_info_text), "BMS OK");
}

static void apply_controller_state(esp_bms_dashboard_snapshot_t *snapshot,
                                   esp_fardriver_state_t *state)
{
    state->fallback_wheel_circumference_mm = snapshot->controller_fallback_wheel_circumference_mm;
    state->fallback_gear_ratio_centi = snapshot->controller_fallback_gear_ratio_centi;
    esp_fardriver_refresh_derived(state);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_CONTROLLER_ONLINE, true);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_CONTROLLER_SPEED_VALID, state->speed_valid);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_CONTROLLER_RPM_VALID, state->rpm_valid);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_CONTROLLER_GEAR_VALID, state->gear_valid);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_CONTROLLER_POWER_VALID, state->power_valid);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_CONTROLLER_TEMP_VALID,
                      state->controller_temp_valid);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_MOTOR_TEMP_VALID, state->motor_temp_valid);
    snapshot->controller_speed_deci_units = state->speed_deci_kmh;
    snapshot->controller_rpm = state->rpm;
    snapshot->controller_gear = state->gear;
    snapshot->controller_power_w = state->power_w;
    snapshot->controller_temp_c = state->controller_temp_c;
    snapshot->motor_temp_c = state->motor_temp_c;
    if (state->controller_speed_params_valid) {
        snapshot->controller_tire_rim_inch = state->tire_rim_inch;
        snapshot->controller_tire_aspect_percent = state->tire_aspect_percent;
        snapshot->controller_tire_width_mm = state->tire_width_mm;
        snapshot->controller_wheel_circumference_mm = state->wheel_circumference_mm;
        snapshot->controller_gear_ratio_centi = state->gear_ratio_centi;
    }
}

static bool feed_ant(esp_bms_host_ble_client_t *client,
                     const uint8_t *chunk,
                     size_t chunk_len,
                     esp_bms_bms_telemetry_t *telemetry)
{
    const bool old_start = chunk_len >= 3U && chunk[0] == 0xAAU && chunk[1] == 0x55U &&
                           chunk[2] == 0xAAU;
    const bool old_stream = client->frame_len >= 3U && client->frame[0] == 0xAAU &&
                            client->frame[1] == 0x55U && client->frame[2] == 0xAAU;
    if (old_start) {
        client->ant_mode = HOST_ANT_PROBE_OLD;
    }
    if (old_start || old_stream) {
        if (esp_bms_ant_protocol_old_feed(client->frame,
                                           &client->frame_len,
                                           sizeof(client->frame),
                                           chunk,
                                           chunk_len,
                                           telemetry)) {
            client->ant_mode = HOST_ANT_OLD;
            return true;
        }
        return false;
    }
    if (chunk_len >= 2U && chunk[0] == 0x7EU && chunk[1] == 0xA1U) {
        client->frame_len = 0U;
    } else if (client->frame_len == 0U) {
        return false;
    }
    if (client->frame_len + chunk_len > sizeof(client->frame)) {
        client->frame_len = 0U;
        return false;
    }
    memcpy(client->frame + client->frame_len, chunk, chunk_len);
    client->frame_len += chunk_len;
    if (client->frame_len < 10U || client->frame[client->frame_len - 2U] != 0xAAU ||
        client->frame[client->frame_len - 1U] != 0x55U) {
        return false;
    }
    bool device_info = false;
    const bool decoded = esp_bms_ant_protocol_decode(
        client->frame, client->frame_len, telemetry, &device_info);
    client->frame_len = 0U;
    if (decoded) {
        client->ant_mode = HOST_ANT_NEW;
    }
    return decoded;
}

static bool feed_bms(esp_bms_host_ble_client_t *client,
                     const uint8_t *chunk,
                     size_t chunk_len,
                     esp_bms_dashboard_snapshot_t *snapshot)
{
    esp_bms_bms_telemetry_t telemetry = { 0 };
    bool decoded = false;
    if (client->bms_type == HOST_BMS_TYPE_ANT) {
        decoded = feed_ant(client, chunk, chunk_len, &telemetry);
    } else if (client->bms_type == HOST_BMS_TYPE_JK) {
        decoded = esp_bms_jk_feed(client->frame, &client->frame_len, sizeof(client->frame),
                                  chunk, chunk_len, &telemetry);
    } else if (client->bms_type == HOST_BMS_TYPE_JBD) {
        decoded = esp_bms_jbd_feed(client->frame, &client->frame_len, sizeof(client->frame),
                                   chunk, chunk_len, &telemetry);
    } else if (client->bms_type == HOST_BMS_TYPE_DALY) {
        decoded = esp_bms_daly_feed(client->frame, &client->frame_len, sizeof(client->frame),
                                    chunk, chunk_len, &telemetry);
    } else if (client->bms_type == HOST_BMS_TYPE_YANYANG) {
        decoded = esp_bms_yanyang_feed(client->frame, &client->frame_len, sizeof(client->frame),
                                       chunk, chunk_len, &telemetry);
    }
    if (decoded) {
        client->bms_telemetry_elapsed_ms = 0U;
        apply_bms_telemetry(snapshot, &telemetry);
    }
    return decoded;
}

static bool send_bms_poll(esp_bms_host_ble_client_t *client)
{
    if (client->bms_type == HOST_BMS_TYPE_YANYANG) {
        uint8_t request[8];
        if (!esp_bms_yanyang_poll_request(client->bms_poll_index, request)) return false;
        client->bms_poll_index = (uint8_t)((client->bms_poll_index + 1U) % 4U);
        return send_bytes(client, ESP_BMS_HOST_BLE_SOURCE_BMS, request, sizeof(request));
    }
    if (client->bms_type == HOST_BMS_TYPE_JK) {
        uint8_t request[20];
        if (!esp_bms_jk_poll_request(client->bms_poll_index, request)) return false;
        client->bms_poll_index = (uint8_t)((client->bms_poll_index + 1U) % 2U);
        return send_bytes(client, ESP_BMS_HOST_BLE_SOURCE_BMS, request, sizeof(request));
    }
    if (client->bms_type == HOST_BMS_TYPE_JBD) {
        uint8_t request[7];
        if (!esp_bms_jbd_poll_request(client->bms_poll_index, request)) return false;
        client->bms_poll_index = (uint8_t)((client->bms_poll_index + 1U) % 2U);
        return send_bytes(client, ESP_BMS_HOST_BLE_SOURCE_BMS, request, sizeof(request));
    }
    if (client->bms_type == HOST_BMS_TYPE_DALY) {
        uint8_t request[8];
        if (!esp_bms_daly_poll_request(client->bms_poll_index, request)) return false;
        client->bms_poll_index = (uint8_t)((client->bms_poll_index + 1U) % 4U);
        return send_bytes(client, ESP_BMS_HOST_BLE_SOURCE_BMS, request, sizeof(request));
    }
    static const uint8_t new_request[] = { 0x7E, 0xA1, 0x01, 0x00, 0x00,
                                           0xBE, 0x18, 0x55, 0xAA, 0x55 };
    uint8_t old_request[6];
    const bool use_old = client->ant_mode == HOST_ANT_PROBE_OLD ||
                         client->ant_mode == HOST_ANT_OLD;
    if (use_old && !esp_bms_ant_protocol_old_poll_request(old_request)) return false;
    if (client->ant_mode == HOST_ANT_PROBE_NEW) client->ant_mode = HOST_ANT_PROBE_OLD;
    else if (client->ant_mode == HOST_ANT_PROBE_OLD) client->ant_mode = HOST_ANT_PROBE_NEW;
    return send_bytes(client, ESP_BMS_HOST_BLE_SOURCE_BMS,
                      use_old ? old_request : new_request,
                      use_old ? sizeof(old_request) : sizeof(new_request));
}

static bool send_controller_poll(esp_bms_host_ble_client_t *client)
{
    if (client->controller_read_polling) {
        uint8_t address = 0U;
        uint8_t request[ESP_FARDRIVER_READ_REQUEST_LEN];
        const size_t count = esp_fardriver_poll_address_count();
        if (count == 0U ||
            !esp_fardriver_poll_address(client->controller_poll_index % count, &address) ||
            !esp_fardriver_build_read_request(address, request)) {
            return false;
        }
        client->controller_poll_index =
            (uint8_t)((client->controller_poll_index + 1U) % count);
        return send_bytes(client, ESP_BMS_HOST_BLE_SOURCE_CONTROLLER, request, sizeof(request));
    }
    uint8_t command[ESP_FARDRIVER_COMMAND_LEN];
    return esp_fardriver_build_keepalive_command(command) &&
           send_bytes(client, ESP_BMS_HOST_BLE_SOURCE_CONTROLLER, command, sizeof(command));
}

static void add_scan_candidate(esp_bms_dashboard_snapshot_t *snapshot,
                               esp_bms_host_ble_source_t source,
                               const char *mac,
                               int rssi,
                               const char *name_hex)
{
    uint8_t *count = source == ESP_BMS_HOST_BLE_SOURCE_BMS
                         ? &snapshot->bms_scan_candidate_count
                         : &snapshot->controller_scan_candidate_count;
    esp_bms_bms_scan_candidate_t *candidates =
        source == ESP_BMS_HOST_BLE_SOURCE_BMS ? snapshot->bms_scan_candidates
                                              : snapshot->controller_scan_candidates;
    uint8_t index = 0U;
    while (index < *count && strcmp(candidates[index].mac, mac) != 0) ++index;
    if (index == *count) {
        if (*count >= ESP_BMS_BMS_SCAN_MAX_CANDIDATES) return;
        ++(*count);
    }
    esp_bms_bms_scan_candidate_t *candidate = &candidates[index];
    memset(candidate, 0, sizeof(*candidate));
    (void)snprintf(candidate->mac, sizeof(candidate->mac), "%s", mac);
    candidate->rssi = (int8_t)(rssi < -128 ? -128 : rssi > 127 ? 127 : rssi);
    hex_to_text(name_hex, candidate->name, sizeof(candidate->name));
    candidate->has_name = candidate->name[0] != '\0';
    if (source == ESP_BMS_HOST_BLE_SOURCE_CONTROLLER) ++snapshot->controller_scan_revision;
}

static bool handle_line(esp_bms_host_ble_client_t *client,
                        char *line,
                        esp_bms_dashboard_snapshot_t *snapshot)
{
    char *cursor = line;
    const char *event = next_field(&cursor);
    if (!event) return false;
    if (strcmp(event, "READY") == 0) {
        client->ready = true;
        snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_BLUETOOTH_ENABLED, true);
        return true;
    }
    const esp_bms_host_ble_source_t source = parse_source(next_field(&cursor));
    if (strcmp(event, "SCAN_CLEAR") == 0) {
        if (source == ESP_BMS_HOST_BLE_SOURCE_BMS) {
            snapshot->bms_scan_candidate_count = 0U;
            memset(snapshot->bms_scan_candidates, 0, sizeof(snapshot->bms_scan_candidates));
        } else {
            snapshot->controller_scan_candidate_count = 0U;
            snapshot->controller_scan_active = 1U;
            ++snapshot->controller_scan_revision;
            memset(snapshot->controller_scan_candidates, 0,
                   sizeof(snapshot->controller_scan_candidates));
        }
        return true;
    }
    if (strcmp(event, "SCAN_RESULT") == 0) {
        const char *mac = next_field(&cursor);
        const char *rssi_text = next_field(&cursor);
        const char *name_hex = next_field(&cursor);
        if (mac && rssi_text) add_scan_candidate(snapshot, source, mac, atoi(rssi_text), name_hex);
        return true;
    }
    if (strcmp(event, "SCAN_DONE") == 0) {
        if (source == ESP_BMS_HOST_BLE_SOURCE_BMS) {
            if (strcmp(snapshot->bms_info_text, "BMS SCAN") == 0) {
                snapshot->bms_info_text[0] = '\0';
            }
        } else {
            snapshot->controller_scan_active = 0U;
            ++snapshot->controller_scan_revision;
        }
        return true;
    }
    if (strcmp(event, "CONNECTED") == 0) {
        char name[ESP_BMS_BMS_SCAN_NAME_LEN + 1U];
        hex_to_text(next_field(&cursor), name, sizeof(name));
        const char *profile = next_field(&cursor);
        if (source == ESP_BMS_HOST_BLE_SOURCE_BMS) {
            client->bms_connected = true;
            client->bms_poll_elapsed_ms = HOST_BLE_BMS_POLL_PERIOD_MS;
            (void)snprintf(snapshot->bms_bound_name, sizeof(snapshot->bms_bound_name), "%s", name);
            (void)snprintf(snapshot->bms_info_text, sizeof(snapshot->bms_info_text), "BMS ON");
        } else {
            client->controller_connected = true;
            client->controller_read_polling = !profile || strcmp(profile, "FFE0") != 0;
            client->controller_poll_elapsed_ms = 0U;
            (void)snprintf(snapshot->controller_bound_name,
                           sizeof(snapshot->controller_bound_name), "%s", name);
            if (client->controller_read_polling) {
                client->controller_poll_elapsed_ms = HOST_BLE_CONTROLLER_READ_PERIOD_MS;
            } else {
                uint8_t command[ESP_FARDRIVER_COMMAND_LEN];
                if (esp_fardriver_build_open_command(command)) {
                    (void)send_bytes(client, source, command, sizeof(command));
                }
            }
        }
        return true;
    }
    if (strcmp(event, "DISCONNECTED") == 0) {
        if (source == ESP_BMS_HOST_BLE_SOURCE_BMS) {
            client->bms_connected = false;
            clear_bms_telemetry(snapshot);
        } else {
            client->controller_connected = false;
            clear_controller_telemetry(snapshot);
        }
        return true;
    }
    if (strcmp(event, "NOTIFY") == 0) {
        uint8_t bytes[HOST_BLE_FRAME_CAPACITY];
        size_t length = 0U;
        if (!hex_decode(next_field(&cursor), bytes, sizeof(bytes), &length)) return false;
        if (source == ESP_BMS_HOST_BLE_SOURCE_BMS) {
            return feed_bms(client, bytes, length, snapshot);
        }
        if (length > sizeof(client->controller_frame) - client->controller_frame_len) {
            client->controller_frame_len = 0U;
        }
        if (length > sizeof(client->controller_frame)) return false;
        memcpy(client->controller_frame + client->controller_frame_len, bytes, length);
        client->controller_frame_len += length;
        bool changed = false;
        size_t consumed = 0U;
        while (client->controller_frame_len - consumed >= ESP_FARDRIVER_FRAME_LEN) {
            changed = esp_fardriver_parse_frame(&client->controller_state,
                                                client->controller_frame + consumed,
                                                ESP_FARDRIVER_FRAME_LEN) || changed;
            consumed += ESP_FARDRIVER_FRAME_LEN;
        }
        if (consumed != 0U) {
            memmove(client->controller_frame,
                    client->controller_frame + consumed,
                    client->controller_frame_len - consumed);
            client->controller_frame_len -= consumed;
        }
        if (changed && esp_fardriver_has_instrument_telemetry(&client->controller_state)) {
            apply_controller_state(snapshot, &client->controller_state);
        }
        return changed;
    }
    if (strcmp(event, "ERROR") == 0) {
        char message[sizeof(snapshot->bms_error_text)];
        hex_to_text(next_field(&cursor), message, sizeof(message));
        if (source == ESP_BMS_HOST_BLE_SOURCE_BMS) {
            client->bms_connected = false;
            clear_bms_telemetry(snapshot);
            (void)snprintf(snapshot->bms_info_text, sizeof(snapshot->bms_info_text), "BMS FAIL");
        } else {
            client->controller_connected = false;
            clear_controller_telemetry(snapshot);
        }
        (void)snprintf(snapshot->bms_error_text, sizeof(snapshot->bms_error_text), "%s", message);
        return true;
    }
    return false;
}

static bool consume_socket(esp_bms_host_ble_client_t *client,
                           esp_bms_dashboard_snapshot_t *snapshot)
{
    if (!client || client->socket == HOST_INVALID_SOCKET) return false;
    bool changed = false;
    bool transport_closed = false;
    for (;;) {
        if (client->rx_len == sizeof(client->rx)) client->rx_len = 0U;
        const int result = recv(client->socket,
                                client->rx + client->rx_len,
                                (int)(sizeof(client->rx) - client->rx_len),
                                0);
        if (result > 0) {
            client->rx_len += (size_t)result;
            continue;
        }
        if (result == 0) {
            transport_closed = true;
        } else if (!socket_would_block()) {
            transport_closed = true;
        }
        break;
    }
    size_t consumed = 0U;
    for (size_t index = 0U; index < client->rx_len; ++index) {
        if (client->rx[index] != '\n') continue;
        client->rx[index] = '\0';
        if (index > consumed && client->rx[index - 1U] == '\r') client->rx[index - 1U] = '\0';
        changed = handle_line(client, client->rx + consumed, snapshot) || changed;
        consumed = index + 1U;
    }
    if (consumed != 0U) {
        memmove(client->rx, client->rx + consumed, client->rx_len - consumed);
        client->rx_len -= consumed;
    }
    if (transport_closed) {
        host_socket_close(client->socket);
        client->socket = HOST_INVALID_SOCKET;
        client->ready = false;
        client->bms_connected = false;
        client->controller_connected = false;
        clear_bms_telemetry(snapshot);
        clear_controller_telemetry(snapshot);
        snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_BLUETOOTH_ENABLED, false);
        changed = true;
    }
    return changed;
}

esp_bms_host_ble_client_t *esp_bms_host_ble_client_create(const char *endpoint)
{
    if (!endpoint || endpoint[0] == '\0') return NULL;
    esp_bms_host_ble_client_t *client = calloc(1U, sizeof(*client));
    if (!client) return NULL;
    client->socket = HOST_INVALID_SOCKET;
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        free(client);
        return NULL;
    }
    client->socket_runtime_initialized = true;
#endif
    char host[256];
    char port[16];
    const char *separator = strrchr(endpoint, ':');
    if (!separator || separator == endpoint || strlen(separator + 1U) >= sizeof(port) ||
        (size_t)(separator - endpoint) >= sizeof(host)) {
        esp_bms_host_ble_client_destroy(client);
        return NULL;
    }
    memcpy(host, endpoint, (size_t)(separator - endpoint));
    host[separator - endpoint] = '\0';
    (void)snprintf(port, sizeof(port), "%s", separator + 1U);
    struct addrinfo hints = { 0 };
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *addresses = NULL;
    if (getaddrinfo(host, port, &hints, &addresses) != 0) {
        esp_bms_host_ble_client_destroy(client);
        return NULL;
    }
    for (const struct addrinfo *address = addresses; address; address = address->ai_next) {
        client->socket = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (client->socket == HOST_INVALID_SOCKET) continue;
        if (connect(client->socket, address->ai_addr, (int)address->ai_addrlen) == 0) break;
        host_socket_close(client->socket);
        client->socket = HOST_INVALID_SOCKET;
    }
    freeaddrinfo(addresses);
    if (client->socket == HOST_INVALID_SOCKET || !socket_set_nonblocking(client->socket)) {
        esp_bms_host_ble_client_destroy(client);
        return NULL;
    }
    return client;
}

void esp_bms_host_ble_client_destroy(esp_bms_host_ble_client_t *client)
{
    if (!client) return;
    if (client->socket != HOST_INVALID_SOCKET) {
        (void)send_line(client, "QUIT");
        (void)flush_tx(client);
        host_socket_close(client->socket);
    }
#ifdef _WIN32
    if (client->socket_runtime_initialized) WSACleanup();
#endif
    free(client);
}

bool esp_bms_host_ble_client_is_ready(const esp_bms_host_ble_client_t *client)
{
    return client && client->ready;
}

void esp_bms_host_ble_client_prepare_snapshot(esp_bms_host_ble_client_t *client,
                                              esp_bms_dashboard_snapshot_t *snapshot)
{
    if (!client || !snapshot) return;
    clear_bms_telemetry(snapshot);
    clear_controller_telemetry(snapshot);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_BLUETOOTH_ENABLED, false);
    snapshot->bms_scan_candidate_count = 0U;
    snapshot->controller_scan_candidate_count = 0U;
    snapshot->controller_scan_active = 0U;
    snapshot->bms_bound_name[0] = '\0';
    snapshot->controller_bound_name[0] = '\0';
}

bool esp_bms_host_ble_client_start_scan(esp_bms_host_ble_client_t *client,
                                        esp_bms_host_ble_source_t source,
                                        esp_bms_dashboard_snapshot_t *snapshot)
{
    if (!client || !snapshot) return false;
    if (source == ESP_BMS_HOST_BLE_SOURCE_BMS) {
        snapshot->bms_scan_candidate_count = 0U;
        (void)snprintf(snapshot->bms_info_text,
                       sizeof(snapshot->bms_info_text),
                       "BMS SCAN");
    } else {
        snapshot->controller_scan_candidate_count = 0U;
        snapshot->controller_scan_active = 1U;
        ++snapshot->controller_scan_revision;
    }
    return send_line(client, "SCAN\t%s", source_name(source));
}

bool esp_bms_host_ble_client_connect(esp_bms_host_ble_client_t *client,
                                     esp_bms_host_ble_source_t source,
                                     uint8_t bms_type,
                                     const char *mac,
                                     esp_bms_dashboard_snapshot_t *snapshot)
{
    if (!client || !mac || !snapshot) return false;
    if (source == ESP_BMS_HOST_BLE_SOURCE_BMS) {
        client->bms_type = bms_type;
        client->bms_poll_index = 0U;
        client->bms_telemetry_elapsed_ms = 0U;
        client->ant_mode = HOST_ANT_PROBE_NEW;
        client->frame_len = 0U;
        clear_bms_telemetry(snapshot);
        (void)snprintf(snapshot->bms_info_text, sizeof(snapshot->bms_info_text), "CONNECTING");
    } else {
        memset(&client->controller_state, 0, sizeof(client->controller_state));
        client->controller_poll_index = 0U;
        client->controller_frame_len = 0U;
        clear_controller_telemetry(snapshot);
    }
    return send_line(client, "CONNECT\t%s\t%u\t%s", source_name(source),
                     (unsigned)bms_type, mac);
}

bool esp_bms_host_ble_client_disconnect(esp_bms_host_ble_client_t *client,
                                        esp_bms_host_ble_source_t source,
                                        esp_bms_dashboard_snapshot_t *snapshot)
{
    if (!client || !snapshot) return false;
    if (source == ESP_BMS_HOST_BLE_SOURCE_BMS) {
        client->bms_connected = false;
        clear_bms_telemetry(snapshot);
    } else {
        client->controller_connected = false;
        clear_controller_telemetry(snapshot);
    }
    return send_line(client, "DISCONNECT\t%s", source_name(source));
}

bool esp_bms_host_ble_client_tick(esp_bms_host_ble_client_t *client,
                                  esp_bms_dashboard_snapshot_t *snapshot,
                                  uint32_t elapsed_ms)
{
    if (!client || !snapshot) return false;
    bool changed = consume_socket(client, snapshot);
    if (client->socket != HOST_INVALID_SOCKET && !flush_tx(client)) {
        host_socket_close(client->socket);
        client->socket = HOST_INVALID_SOCKET;
        client->ready = false;
        client->bms_connected = false;
        client->controller_connected = false;
        clear_bms_telemetry(snapshot);
        clear_controller_telemetry(snapshot);
        snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_BLUETOOTH_ENABLED, false);
        return true;
    }
    if (client->bms_connected) {
        client->bms_poll_elapsed_ms += elapsed_ms;
        if (client->bms_poll_elapsed_ms >= HOST_BLE_BMS_POLL_PERIOD_MS) {
            (void)send_bms_poll(client);
            client->bms_poll_elapsed_ms = 0U;
        }
        if (esp_bms_dashboard_snapshot_flag_get(snapshot,
                                                ESP_BMS_DASHBOARD_FLAG_BMS_ONLINE)) {
            if (elapsed_ms >= HOST_BLE_BMS_HEARTBEAT_TIMEOUT_MS -
                                  client->bms_telemetry_elapsed_ms) {
                client->bms_connected = false;
                clear_bms_telemetry(snapshot);
                (void)snprintf(snapshot->bms_info_text,
                               sizeof(snapshot->bms_info_text),
                               "BMS TIMEOUT");
                (void)send_line(client, "DISCONNECT\tBMS");
                changed = true;
            } else {
                client->bms_telemetry_elapsed_ms += elapsed_ms;
            }
        }
    }
    if (client->controller_connected) {
        client->controller_poll_elapsed_ms += elapsed_ms;
        const uint32_t period = client->controller_read_polling
                                    ? HOST_BLE_CONTROLLER_READ_PERIOD_MS
                                    : HOST_BLE_CONTROLLER_KEEPALIVE_PERIOD_MS;
        if (client->controller_poll_elapsed_ms >= period) {
            (void)send_controller_poll(client);
            client->controller_poll_elapsed_ms = 0U;
        }
    }
    return changed;
}
