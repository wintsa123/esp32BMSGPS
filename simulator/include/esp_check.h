#pragma once

#include "esp_err.h"
#include "esp_log.h"

#define ESP_RETURN_ON_FALSE(condition, error_code, tag, ...) \
    do { \
        if (!(condition)) { \
            ESP_LOGE(tag, __VA_ARGS__); \
            return (error_code); \
        } \
    } while (0)
#define ESP_RETURN_ON_ERROR(expression, tag, ...) \
    do { \
        const esp_err_t host_compat_error_ = (expression); \
        if (host_compat_error_ != ESP_OK) { \
            ESP_LOGE(tag, __VA_ARGS__); \
            return host_compat_error_; \
        } \
    } while (0)
