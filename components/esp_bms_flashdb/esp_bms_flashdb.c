#include "esp_bms_flashdb.h"

#include "esp_log.h"
#include "flashdb.h"
#include "fal.h"
#include "nvs.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define FLASHDB_SECTOR_SIZE 4096U
#define FLASHDB_SAMPLES_PER_SECTOR 84U
#define FLASHDB_FAULT_VERSION 1U

static const char *TAG = "flashdb";
static struct fdb_tsdb s_history[ESP_BMS_FLASHDB_MAX_SESSIONS];
static struct fdb_tsdb s_faults;
static uint64_t s_ids[ESP_BMS_FLASHDB_MAX_SESSIONS];
static uint64_t s_anchor_utc[ESP_BMS_FLASHDB_MAX_SESSIONS];
static uint32_t s_anchor_elapsed[ESP_BMS_FLASHDB_MAX_SESSIONS];
static size_t s_counts[ESP_BMS_FLASHDB_MAX_SESSIONS];
static bool s_present[ESP_BMS_FLASHDB_MAX_SESSIONS];
static size_t s_capacity;
static uint8_t s_current;
static bool s_ready;
static bool s_full;

static fdb_time_t flashdb_now(void) { return 0; }

static const char *const s_id_keys[] = {"h0_id", "h1_id", "h2_id"};
static const char *const s_utc_keys[] = {"h0_utc", "h1_utc", "h2_utc"};
static const char *const s_rel_keys[] = {"h0_rel", "h1_rel", "h2_rel"};

static esp_err_t load_metadata(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open("esp_bms", NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (ret != ESP_OK) return ret;
    for (size_t i = 0; i < ESP_BMS_FLASHDB_MAX_SESSIONS; ++i) {
        (void)nvs_get_u64(handle, s_id_keys[i], &s_ids[i]);
        (void)nvs_get_u64(handle, s_utc_keys[i], &s_anchor_utc[i]);
        (void)nvs_get_u32(handle, s_rel_keys[i], &s_anchor_elapsed[i]);
    }
    nvs_close(handle);
    return ESP_OK;
}

static esp_err_t save_slot_metadata(size_t slot)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open("esp_bms", NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;
    ret = nvs_set_u64(handle, s_id_keys[slot], s_ids[slot]);
    if (ret == ESP_OK) ret = nvs_set_u64(handle, s_utc_keys[slot], s_anchor_utc[slot]);
    if (ret == ESP_OK) ret = nvs_set_u32(handle, s_rel_keys[slot], s_anchor_elapsed[slot]);
    if (ret == ESP_OK) ret = nvs_commit(handle);
    nvs_close(handle);
    return ret;
}

static esp_err_t next_id(uint64_t *id)
{
    if (!id) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t ret = nvs_open("esp_bms", NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;
    uint64_t value = 0;
    ret = nvs_get_u64(handle, "hist_seq", &value);
    if (ret == ESP_ERR_NVS_NOT_FOUND) ret = ESP_OK;
    if (ret == ESP_OK && value < UINT32_MAX) {
        value++;
        ret = nvs_set_u64(handle, "hist_seq", value);
    } else if (ret == ESP_OK) {
        ret = ESP_ERR_INVALID_SIZE;
    }
    if (ret == ESP_OK) ret = nvs_commit(handle);
    nvs_close(handle);
    if (ret == ESP_OK) *id = value;
    return ret;
}

static const char *slot_name(size_t slot)
{
    static const char *const names[] = {"history0", "history1", "history2"};
    return slot < ESP_BMS_FLASHDB_MAX_SESSIONS ? names[slot] : NULL;
}

static size_t slot_capacity(const struct fal_partition *part)
{
    if (!part) return 0;
    size_t capacity = (part->len / FLASHDB_SECTOR_SIZE) * FLASHDB_SAMPLES_PER_SECTOR;
    return capacity > ESP_BMS_FLASHDB_MAX_SAMPLES ? ESP_BMS_FLASHDB_MAX_SAMPLES : capacity;
}

static esp_err_t init_db(struct fdb_tsdb *db, const char *name, size_t payload, bool rollover)
{
    size_t sector_size = FLASHDB_SECTOR_SIZE;
    fdb_tsdb_control(db, FDB_TSDB_CTRL_SET_SEC_SIZE, &sector_size);
    if (fdb_tsdb_init(db, name, name, flashdb_now, payload, NULL) != FDB_NO_ERR) return ESP_FAIL;
    fdb_tsdb_control(db, FDB_TSDB_CTRL_SET_ROLLOVER, &rollover);
    return ESP_OK;
}

static size_t find_session(uint64_t session_id)
{
    for (size_t i = 0; i < ESP_BMS_FLASHDB_MAX_SESSIONS; ++i) {
        if (s_present[i] && s_ids[i] == session_id && session_id != 0U) return i;
    }
    return SIZE_MAX;
}

static uint64_t session_start_utc(size_t slot)
{
    if (!s_anchor_utc[slot] || s_anchor_utc[slot] < s_anchor_elapsed[slot]) return 0;
    return s_anchor_utc[slot] - s_anchor_elapsed[slot];
}

static uint64_t session_time(size_t slot, uint32_t elapsed_s)
{
    const uint64_t start = session_start_utc(slot);
    return start ? start + elapsed_s : elapsed_s;
}

esp_err_t esp_bms_flashdb_init(void)
{
    if (s_ready) return ESP_OK;
    if (fal_init() <= 0) return ESP_ERR_NOT_FOUND;
    const struct fal_partition *first = fal_partition_find("history0");
    const struct fal_partition *faults = fal_partition_find("faults");
    if (!first || !faults || first->len < FLASHDB_SECTOR_SIZE || faults->len < FLASHDB_SECTOR_SIZE)
        return ESP_ERR_INVALID_SIZE;
    s_capacity = nor_flash0.len;
    esp_err_t ret = load_metadata();
    if (ret != ESP_OK) return ret;
    for (size_t i = 0; i < ESP_BMS_FLASHDB_MAX_SESSIONS; ++i) {
        const char *name = slot_name(i);
        const struct fal_partition *part = fal_partition_find(name);
        if (!part) continue;
        s_present[i] = true;
        if (init_db(&s_history[i], name, sizeof(esp_bms_flashdb_sample_t), false) != ESP_OK)
            return ESP_FAIL;
        s_counts[i] = fdb_tsl_query_count(&s_history[i], 0, INT64_MAX, FDB_TSL_WRITE);
        if (!s_counts[i]) {
            s_ids[i] = 0;
            s_anchor_utc[i] = 0;
            s_anchor_elapsed[i] = 0;
        }
    }
    if (init_db(&s_faults, "faults", sizeof(esp_bms_flashdb_fault_t), true) != ESP_OK)
        return ESP_FAIL;
    s_current = 0;
    for (size_t i = 1; i < ESP_BMS_FLASHDB_MAX_SESSIONS; ++i)
        if (s_present[i] && s_ids[i] > s_ids[s_current]) s_current = (uint8_t)i;
    s_full = s_counts[s_current] >= slot_capacity(fal_partition_find(slot_name(s_current)));
    s_ready = true;
    ESP_LOGI(TAG, "ready: partition=%u slots=%u current_capacity=%u faults=%u",
             (unsigned)s_capacity, (unsigned)esp_bms_flashdb_session_count(),
             (unsigned)slot_capacity(fal_partition_find(slot_name(s_current))), (unsigned)faults->len);
    return ESP_OK;
}

bool esp_bms_flashdb_ready(void) { return s_ready; }

esp_err_t esp_bms_flashdb_start_session(uint64_t *session_id)
{
    if (!s_ready || !session_id) return ESP_ERR_INVALID_STATE;
    size_t selected = SIZE_MAX;
    for (size_t i = 0; i < ESP_BMS_FLASHDB_MAX_SESSIONS; ++i)
        if (s_present[i] && s_counts[i] == 0U) { selected = i; break; }
    if (selected == SIZE_MAX) {
        for (size_t i = 0; i < ESP_BMS_FLASHDB_MAX_SESSIONS; ++i)
            if (s_present[i] && (selected == SIZE_MAX || s_ids[i] < s_ids[selected])) selected = i;
        if (selected == SIZE_MAX) return ESP_ERR_INVALID_SIZE;
        fdb_tsl_clean(&s_history[selected]);
        s_counts[selected] = 0;
    }
    uint64_t id = 0;
    esp_err_t ret = next_id(&id);
    if (ret != ESP_OK) return ret;
    s_ids[selected] = id;
    s_anchor_utc[selected] = 0;
    s_anchor_elapsed[selected] = 0;
    ret = save_slot_metadata(selected);
    if (ret != ESP_OK) return ret;
    s_current = (uint8_t)selected;
    s_full = false;
    *session_id = id;
    return ESP_OK;
}

esp_err_t esp_bms_flashdb_resume_session(uint64_t session_id, size_t *sample_count)
{
    if (!s_ready || !session_id) return ESP_ERR_INVALID_STATE;
    const size_t slot = find_session(session_id);
    if (slot == SIZE_MAX) return ESP_ERR_NOT_FOUND;
    s_current = (uint8_t)slot;
    s_full = s_counts[slot] >= slot_capacity(fal_partition_find(slot_name(slot)));
    if (sample_count) *sample_count = s_counts[slot];
    return ESP_OK;
}

esp_err_t esp_bms_flashdb_set_session_anchor(uint64_t session_id, uint32_t elapsed_s, uint64_t utc_s)
{
    if (!s_ready || !session_id || !utc_s || utc_s < elapsed_s) return ESP_ERR_INVALID_ARG;
    const size_t slot = find_session(session_id);
    if (slot == SIZE_MAX) return ESP_ERR_NOT_FOUND;
    if (s_anchor_utc[slot]) return ESP_OK;
    s_anchor_utc[slot] = utc_s;
    s_anchor_elapsed[slot] = elapsed_s;
    return save_slot_metadata(slot);
}

esp_err_t esp_bms_flashdb_append_sample(uint64_t timestamp, const esp_bms_flashdb_sample_t *sample)
{
    if (!s_ready || !sample || sample->version != ESP_BMS_FLASHDB_SAMPLE_VERSION)
        return ESP_ERR_INVALID_STATE;
    const size_t capacity = slot_capacity(fal_partition_find(slot_name(s_current)));
    if (s_full || s_counts[s_current] >= capacity || s_counts[s_current] >= ESP_BMS_FLASHDB_MAX_SAMPLES) {
        s_full = true;
        return ESP_ERR_NO_MEM;
    }
    struct fdb_blob blob;
    fdb_blob_make(&blob, sample, sizeof(*sample));
    if (fdb_tsl_append_with_ts(&s_history[s_current], &blob, (fdb_time_t)timestamp) != FDB_NO_ERR) {
        s_full = true;
        return ESP_ERR_NO_MEM;
    }
    if (++s_counts[s_current] >= capacity) s_full = true;
    return ESP_OK;
}

typedef struct {
    size_t slot, limit, count;
    esp_bms_flashdb_sample_cb_t callback;
    void *ctx;
} sample_query_t;

static bool sample_cb(fdb_tsl_t tsl, void *arg)
{
    sample_query_t *q = arg;
    if (q->count >= q->limit) return false;
    esp_bms_flashdb_sample_t sample = {0};
    struct fdb_blob blob;
    fdb_blob_make(&blob, &sample, sizeof(sample));
    fdb_tsl_to_blob(tsl, &blob);
    if (sample.version != ESP_BMS_FLASHDB_SAMPLE_VERSION) return true;
    ++q->count;
    return q->callback(session_time(q->slot, sample.elapsed_s), &sample, q->ctx);
}

esp_err_t esp_bms_flashdb_query_session_samples(uint64_t session_id, uint64_t from, uint64_t to,
                                                size_t limit, esp_bms_flashdb_sample_cb_t callback,
                                                void *ctx, size_t *returned)
{
    if (!s_ready || !callback || !limit || from > to) return ESP_ERR_INVALID_ARG;
    const size_t slot = find_session(session_id);
    if (slot == SIZE_MAX || !s_counts[slot]) return ESP_ERR_NOT_FOUND;
    const uint64_t start = session_start_utc(slot);
    uint64_t rel_from = from;
    uint64_t rel_to = to;
    if (start) {
        if (to < start) { if (returned) *returned = 0; return ESP_OK; }
        rel_from = from <= start ? 0 : from - start;
        rel_to = to == UINT64_MAX ? UINT32_MAX : to - start;
    }
    if (rel_from > UINT32_MAX) { if (returned) *returned = 0; return ESP_OK; }
    if (rel_to > UINT32_MAX) rel_to = UINT32_MAX;
    const uint64_t base = session_id << 32;
    sample_query_t query = {.slot = slot, .limit = limit, .callback = callback, .ctx = ctx};
    fdb_tsl_iter_by_time(&s_history[slot], (fdb_time_t)(base | rel_from),
                         (fdb_time_t)(base | rel_to), sample_cb, &query);
    if (returned) *returned = query.count;
    return ESP_OK;
}

esp_err_t esp_bms_flashdb_query_samples(uint64_t from, uint64_t to, size_t limit,
                                        esp_bms_flashdb_sample_cb_t callback, void *ctx,
                                        size_t *returned)
{
    return esp_bms_flashdb_query_session_samples(s_ids[s_current], from, to, limit,
                                                 callback, ctx, returned);
}

esp_err_t esp_bms_flashdb_append_fault(const esp_bms_flashdb_fault_t *fault)
{
    if (!s_ready || !fault || fault->flags != FLASHDB_FAULT_VERSION ||
        find_session(fault->session_id) == SIZE_MAX) return ESP_ERR_INVALID_STATE;
    struct fdb_blob blob;
    fdb_blob_make(&blob, fault, sizeof(*fault));
    return fdb_tsl_append_with_ts(&s_faults, &blob, (fdb_time_t)fault->timestamp) == FDB_NO_ERR
               ? ESP_OK : ESP_ERR_NO_MEM;
}

typedef struct {
    uint64_t session_id, from, to;
    size_t limit, count;
    esp_bms_flashdb_fault_cb_t callback;
    void *ctx;
} fault_query_t;

static bool fault_cb(fdb_tsl_t tsl, void *arg)
{
    fault_query_t *q = arg;
    if (q->count >= q->limit) return false;
    esp_bms_flashdb_fault_t fault = {0};
    struct fdb_blob blob;
    fdb_blob_make(&blob, &fault, sizeof(fault));
    fdb_tsl_to_blob(tsl, &blob);
    if (fault.flags != FLASHDB_FAULT_VERSION) return true;
    const size_t slot = find_session(fault.session_id);
    if (slot == SIZE_MAX || (q->session_id && fault.session_id != q->session_id)) return true;
    fault.timestamp = session_time(slot, fault.elapsed_s);
    if (fault.timestamp < q->from || fault.timestamp > q->to) return true;
    ++q->count;
    return q->callback(&fault, q->ctx);
}

esp_err_t esp_bms_flashdb_query_faults(uint64_t session_id, uint64_t from, uint64_t to, size_t limit,
                                       esp_bms_flashdb_fault_cb_t callback, void *ctx,
                                       size_t *returned)
{
    if (!s_ready || !callback || !limit || from > to) return ESP_ERR_INVALID_ARG;
    if (session_id && find_session(session_id) == SIZE_MAX) return ESP_ERR_NOT_FOUND;
    fault_query_t query = {.session_id = session_id, .from = from, .to = to, .limit = limit,
                           .callback = callback, .ctx = ctx};
    fdb_tsl_iter(&s_faults, fault_cb, &query);
    if (returned) *returned = query.count;
    return ESP_OK;
}

size_t esp_bms_flashdb_sample_count(void) { return s_ready ? s_counts[s_current] : 0; }
size_t esp_bms_flashdb_capacity_bytes(void) { return s_capacity; }

size_t esp_bms_flashdb_session_count(void)
{
    size_t count = 0;
    for (size_t i = 0; i < ESP_BMS_FLASHDB_MAX_SESSIONS; ++i)
        if (s_present[i] && s_counts[i] && s_ids[i]) ++count;
    return count;
}

bool esp_bms_flashdb_has_session(uint64_t session_id)
{
    return s_ready && find_session(session_id) != SIZE_MAX;
}

esp_err_t esp_bms_flashdb_get_session(size_t index, esp_bms_flashdb_session_t *session)
{
    if (!s_ready || !session || index >= ESP_BMS_FLASHDB_MAX_SESSIONS || !s_present[index] ||
        !s_counts[index] || !s_ids[index]) return ESP_ERR_NOT_FOUND;
    memset(session, 0, sizeof(*session));
    session->session_id = s_ids[index];
    session->sample_count = (uint32_t)s_counts[index];
    session->capacity_samples = (uint32_t)slot_capacity(fal_partition_find(slot_name(index)));
    session->elapsed_seconds = session->sample_count - 1U;
    session->calibrated = session_start_utc(index) != 0U;
    session->start_time_s = session->calibrated ? session_start_utc(index) : 0U;
    session->end_time_s = session->calibrated ? session->start_time_s + session->elapsed_seconds : 0U;
    session->truncated = session->capacity_samples < ESP_BMS_FLASHDB_MAX_SAMPLES;
    session->capacity_reached = s_counts[index] >= session->capacity_samples;
    return ESP_OK;
}

bool esp_bms_flashdb_session_full(void) { return s_full; }
