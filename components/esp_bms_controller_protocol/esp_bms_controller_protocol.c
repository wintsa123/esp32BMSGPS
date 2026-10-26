#include "esp_bms_controller_protocol.h"

#include <ctype.h>
#include <string.h>

#include "esp_bms_ctrl_apt_a8.h"
#include "esp_bms_ctrl_apt_legacy.h"
#include "esp_bms_ctrl_ausi.h"
#include "esp_bms_ctrl_bluestone.h"
#include "esp_bms_ctrl_datai.h"
#include "esp_bms_ctrl_dunen.h"
#include "esp_bms_ctrl_fardriver.h"
#include "esp_bms_ctrl_hobbywing.h"
#include "esp_bms_ctrl_jh.h"
#include "esp_bms_ctrl_jiazhou.h"
#include "esp_bms_ctrl_lingbo.h"
#include "esp_bms_ctrl_suniu.h"
#include "esp_bms_ctrl_xinda.h"
#include "esp_bms_ctrl_yanxin.h"
#include "esp_bms_ctrl_yuanqu_foc.h"
#include "esp_bms_ctrl_zhike.h"

/* 128 位 UUID 按 NimBLE 的字节序保存（最低位在前），注释里是常规文本形式。 */

/* 00002760-08c2-11e1-9073-0e8ac72e1001 */
static const uint8_t BLUESTONE_SERVICE_UUID[ESP_BMS_CTRL_UUID128_LEN] = {
    0x01, 0x10, 0x2e, 0xc7, 0x8a, 0x0e, 0x73, 0x90,
    0xe1, 0x11, 0xc2, 0x08, 0x60, 0x27, 0x00, 0x00,
};
/* 00002760-08c2-11e1-9073-0e8ac72e0002 */
static const uint8_t BLUESTONE_NOTIFY_UUID[ESP_BMS_CTRL_UUID128_LEN] = {
    0x02, 0x00, 0x2e, 0xc7, 0x8a, 0x0e, 0x73, 0x90,
    0xe1, 0x11, 0xc2, 0x08, 0x60, 0x27, 0x00, 0x00,
};
/* 00002760-08c2-11e1-9073-0e8ac72e0001 */
static const uint8_t BLUESTONE_WRITE_UUID[ESP_BMS_CTRL_UUID128_LEN] = {
    0x01, 0x00, 0x2e, 0xc7, 0x8a, 0x0e, 0x73, 0x90,
    0xe1, 0x11, 0xc2, 0x08, 0x60, 0x27, 0x00, 0x00,
};

#define CTRL_ENTRY(id_text, zh, en, uuid128_flag, svc, ntf, wrt, svc16, ntf16, wrt16, mtu, start_ms, poll_ms, enum_name, brand) \
    {                                                                                                              \
        .type = ESP_BMS_CTRL_TYPE_##enum_name,                                                                          \
        .id = id_text,                                                                                              \
        .label_zh = zh,                                                                                             \
        .label_en = en,                                                                                             \
        .uuid128 = uuid128_flag,                                                                                    \
        .service_uuid = svc,                                                                                        \
        .notify_uuid = ntf,                                                                                         \
        .write_uuid = wrt,                                                                                          \
        .service_uuid16 = svc16,                                                                                    \
        .notify_uuid16 = ntf16,                                                                                     \
        .write_uuid16 = wrt16,                                                                                      \
        .requested_mtu = mtu,                                                                                       \
        .start_retry_ms = start_ms,                                                                                 \
        .poll_period_ms = poll_ms,                                                                                  \
        .reset = esp_bms_ctrl_##brand##_reset,                                                                      \
        .build_start = esp_bms_ctrl_##brand##_build_start,                                                          \
        .build_poll = esp_bms_ctrl_##brand##_build_poll,                                                            \
        .feed = esp_bms_ctrl_##brand##_feed,                                                                        \
        .telemetry = esp_bms_ctrl_##brand##_telemetry,                                                              \
    }

static const esp_bms_ctrl_protocol_t CONTROLLER_PROTOCOLS[] = {
    CTRL_ENTRY("bluestone", "蓝石 BlueStone", "BlueStone", true, BLUESTONE_SERVICE_UUID, BLUESTONE_NOTIFY_UUID,
               BLUESTONE_WRITE_UUID, 0U, 0U, 0U, 247U, 1000U, 5000U, BLUESTONE, bluestone),
    CTRL_ENTRY("fardriver", "远驱 FarDriver", "FarDriver", false, NULL, NULL, NULL, 0xFFE0U, 0xFFECU, 0xFFECU, 23U,
               1000U, 200U, FARDRIVER, fardriver),
    CTRL_ENTRY("yuanqu_foc", "远驱 YuanQu FOC", "YuanQu FOC", false, NULL, NULL, NULL, 0U, 0U, 0U, 23U, 1000U, 200U,
               YUANQU_FOC, yuanqu_foc),
    CTRL_ENTRY("lingbo", "凌博 Lingbo", "Lingbo", false, NULL, NULL, NULL, 0xFFF0U, 0xFFF1U, 0xFFF2U, 247U, 1000U,
               500U, LINGBO, lingbo),
    CTRL_ENTRY("datai", "大泰 Datai", "Datai", false, NULL, NULL, NULL, 0U, 0U, 0U, 247U, 1000U, 200U, DATAI, datai),
    CTRL_ENTRY("suniu", "速牛 Suniu", "Suniu", false, NULL, NULL, NULL, 0U, 0U, 0U, 512U, 1000U, 200U, SUNIU, suniu),
    CTRL_ENTRY("yanxin", "雁鑫 Yanxin VITMC", "Yanxin VITMC", false, NULL, NULL, NULL, 0U, 0U, 0U, 23U, 1000U, 200U,
               YANXIN, yanxin),
    CTRL_ENTRY("apt_a8", "APT A8", "APT A8", false, NULL, NULL, NULL, 0U, 0U, 0U, 23U, 1000U, 200U, APT_A8, apt_a8),
    CTRL_ENTRY("apt_legacy", "APT 安培旧版", "APT Anpei", false, NULL, NULL, NULL, 0U, 0U, 0U, 23U, 1000U, 200U,
               APT_LEGACY, apt_legacy),
    CTRL_ENTRY("dunen", "顿恩 DN", "DN Dunen", false, NULL, NULL, NULL, 0U, 0U, 0U, 23U, 1000U, 200U, DUNEN, dunen),
    CTRL_ENTRY("zhike", "智科 Zhike", "GEKOO Zhike", false, NULL, NULL, NULL, 0U, 0U, 0U, 23U, 1000U, 200U, ZHIKE, zhike),
    CTRL_ENTRY("jh", "晶汇 JH", "JH", false, NULL, NULL, NULL, 0U, 0U, 0U, 128U, 1000U, 200U, JH, jh),
    CTRL_ENTRY("ausi", "AUSI 玉铖", "AUSI", false, NULL, NULL, NULL, 0U, 0U, 0U, 23U, 1000U, 200U, AUSI, ausi),
    CTRL_ENTRY("jiazhou", "甲胄动力 DS11/DS15", "Jiazhou DS11/DS15", false, NULL, NULL, NULL, 0U, 0U, 0U, 500U, 1500U,
               500U, JIAZHOU, jiazhou),
    CTRL_ENTRY("xinda", "昕达动能", "Xinda", false, NULL, NULL, NULL, 0U, 0U, 0U, 23U, 500U, 200U, XINDA, xinda),
    CTRL_ENTRY("hobbywing", "好盈 SPD", "Hobbywing SPD", false, NULL, NULL, NULL, 0U, 0U, 0U, 247U, 1000U, 200U,
               HOBBYWING, hobbywing),
};

#undef CTRL_ENTRY

size_t esp_bms_ctrl_protocol_count(void)
{
    return sizeof(CONTROLLER_PROTOCOLS) / sizeof(CONTROLLER_PROTOCOLS[0]);
}

const esp_bms_ctrl_protocol_t *esp_bms_ctrl_protocol_at(size_t index)
{
    if (index >= esp_bms_ctrl_protocol_count()) {
        return NULL;
    }
    return &CONTROLLER_PROTOCOLS[index];
}

const esp_bms_ctrl_protocol_t *esp_bms_ctrl_protocol_get(uint8_t type)
{
    for (size_t index = 0U; index < esp_bms_ctrl_protocol_count(); ++index) {
        if (CONTROLLER_PROTOCOLS[index].type == type) {
            return &CONTROLLER_PROTOCOLS[index];
        }
    }
    return NULL;
}

const esp_bms_ctrl_protocol_t *esp_bms_ctrl_protocol_find_by_id(const char *id)
{
    if (!id || id[0] == '\0') {
        return NULL;
    }
    for (size_t index = 0U; index < esp_bms_ctrl_protocol_count(); ++index) {
        if (strcmp(CONTROLLER_PROTOCOLS[index].id, id) == 0) {
            return &CONTROLLER_PROTOCOLS[index];
        }
    }
    return NULL;
}

const char *esp_bms_ctrl_type_config_text(uint8_t type)
{
    const esp_bms_ctrl_protocol_t *protocol = esp_bms_ctrl_protocol_get(type);
    return protocol ? protocol->id : "auto";
}

bool esp_bms_ctrl_type_from_config_text(const char *text, uint8_t *out_type)
{
    if (!out_type) {
        return false;
    }
    if (!text || text[0] == '\0' || strcmp(text, "auto") == 0) {
        *out_type = (uint8_t)ESP_BMS_CTRL_TYPE_AUTO;
        return true;
    }
    const esp_bms_ctrl_protocol_t *protocol = esp_bms_ctrl_protocol_find_by_id(text);
    if (!protocol) {
        return false;
    }
    *out_type = protocol->type;
    return true;
}

static bool ctrl_name_contains(const char *name, const char *needle)
{
    if (!name || !needle || needle[0] == '\0') {
        return false;
    }
    const size_t needle_len = strlen(needle);
    for (const char *cursor = name; *cursor != '\0'; ++cursor) {
        size_t matched = 0U;
        while (matched < needle_len && cursor[matched] != '\0' &&
               tolower((unsigned char)cursor[matched]) == tolower((unsigned char)needle[matched])) {
            ++matched;
        }
        if (matched == needle_len) {
            return true;
        }
    }
    return false;
}

static bool ctrl_uuid128_equals_any(const uint8_t *candidate, const uint8_t *expected)
{
    return candidate && expected && memcmp(candidate, expected, ESP_BMS_CTRL_UUID128_LEN) == 0;
}

const esp_bms_ctrl_protocol_t *esp_bms_ctrl_protocol_detect(const char *name,
                                                            const uint8_t *service_uuid128,
                                                            uint16_t service_uuid16)
{
    if (service_uuid128) {
        for (size_t index = 0U; index < esp_bms_ctrl_protocol_count(); ++index) {
            const esp_bms_ctrl_protocol_t *protocol = &CONTROLLER_PROTOCOLS[index];
            if (protocol->uuid128 && ctrl_uuid128_equals_any(service_uuid128, protocol->service_uuid)) {
                return protocol;
            }
        }
    }
    if (service_uuid16 != 0U && service_uuid16 != 0xFFFFU) {
        for (size_t index = 0U; index < esp_bms_ctrl_protocol_count(); ++index) {
            const esp_bms_ctrl_protocol_t *protocol = &CONTROLLER_PROTOCOLS[index];
            if (!protocol->uuid128 && protocol->service_uuid16 != 0U &&
                protocol->service_uuid16 == service_uuid16) {
                return protocol;
            }
        }
    }
    if (ctrl_name_contains(name, "bluestone") || ctrl_name_contains(name, "bst-") ||
        ctrl_name_contains(name, "蓝石")) {
        return esp_bms_ctrl_protocol_get((uint8_t)ESP_BMS_CTRL_TYPE_BLUESTONE);
    }
    if (ctrl_name_contains(name, "LBMC-") || ctrl_name_contains(name, "lingbo") ||
        ctrl_name_contains(name, "凌博")) {
        return esp_bms_ctrl_protocol_get((uint8_t)ESP_BMS_CTRL_TYPE_LINGBO);
    }
    if (ctrl_name_contains(name, "dunen") || ctrl_name_contains(name, "顿恩")) {
        return esp_bms_ctrl_protocol_get((uint8_t)ESP_BMS_CTRL_TYPE_DUNEN);
    }
    if (ctrl_name_contains(name, "yuanqu") || ctrl_name_contains(name, "远驱 FOC")) {
        return esp_bms_ctrl_protocol_get((uint8_t)ESP_BMS_CTRL_TYPE_YUANQU_FOC);
    }
    if (ctrl_name_contains(name, "far") || ctrl_name_contains(name, "远驱")) {
        return esp_bms_ctrl_protocol_get((uint8_t)ESP_BMS_CTRL_TYPE_FARDRIVER);
    }
    return NULL;
}

void esp_bms_ctrl_reset(const esp_bms_ctrl_protocol_t *protocol)
{
    if (protocol && protocol->reset) {
        protocol->reset();
    }
}

void esp_bms_ctrl_refresh_derived(esp_bms_ctrl_telemetry_t *state)
{
    if (!state) {
        return;
    }
    if (state->voltage_valid && state->current_valid) {
        state->power_valid = true;
        state->power_w = ((int32_t)state->voltage_deci_v * state->current_centi_a) / 1000;
    }
    const uint16_t circumference = state->controller_speed_params_valid
                                       ? state->wheel_circumference_mm
                                       : state->fallback_wheel_circumference_mm;
    const uint16_t ratio = state->controller_speed_params_valid ? state->gear_ratio_centi
                                                               : state->fallback_gear_ratio_centi;
    if (!state->rpm_valid || circumference == 0U || ratio == 0U) {
        return;
    }
    const uint64_t speed_deci_kmh = ((uint64_t)state->rpm * circumference * 60000ULL) /
                                    ((uint64_t)ratio * 1000000ULL);
    if (speed_deci_kmh <= UINT16_MAX) {
        state->speed_valid = true;
        state->speed_deci_kmh = (uint16_t)speed_deci_kmh;
    }
}
