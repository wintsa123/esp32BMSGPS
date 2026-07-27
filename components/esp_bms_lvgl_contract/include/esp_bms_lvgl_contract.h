#pragma once

#include "lvgl.h"
#include "sdkconfig.h"

#ifndef LVGL_VERSION_MAJOR
#error "LVGL header version macros are missing"
#endif

#if LVGL_VERSION_MAJOR != 9 || LVGL_VERSION_MINOR != 5 || LVGL_VERSION_PATCH != 0
#error "ESP BMS LVGL components require lvgl/lvgl 9.5.0 headers"
#endif

#ifndef CONFIG_LVGL_VERSION_MAJOR
#error "LVGL Kconfig version is missing; build through the ESP-IDF component graph"
#endif

#if CONFIG_LVGL_VERSION_MAJOR != 9 || CONFIG_LVGL_VERSION_MINOR != 5 || CONFIG_LVGL_VERSION_PATCH != 0
#error "ESP BMS LVGL components require CONFIG_LVGL_VERSION_* = 9.5.0"
#endif

#if !LV_USE_LABEL
#error "ESP BMS LVGL components require CONFIG_LV_USE_LABEL=y"
#endif

#if !LV_USE_CANVAS
#error "ESP BMS LVGL components require CONFIG_LV_USE_CANVAS=y"
#endif

#if !LV_USE_IMAGE
#error "ESP BMS LVGL components require CONFIG_LV_USE_IMAGE=y"
#endif

#if !LV_USE_QRCODE
#error "ESP BMS LVGL components require CONFIG_LV_USE_QRCODE=y"
#endif

#if !LV_USE_ROLLER
#error "ESP BMS LVGL components require CONFIG_LV_USE_ROLLER=y"
#endif

typedef enum {
    ESP_BMS_LVGL_NATIVE_GESTURE_NONE = 0,
    ESP_BMS_LVGL_NATIVE_GESTURE_SWIPE_RIGHT,
    ESP_BMS_LVGL_NATIVE_GESTURE_SWIPE_LEFT,
    ESP_BMS_LVGL_NATIVE_GESTURE_SWIPE_DOWN,
    ESP_BMS_LVGL_NATIVE_GESTURE_SWIPE_UP,
    ESP_BMS_LVGL_NATIVE_GESTURE_DOUBLE_TAP,
    ESP_BMS_LVGL_NATIVE_GESTURE_KEY_PREVIOUS,
    ESP_BMS_LVGL_NATIVE_GESTURE_KEY_NEXT,
    ESP_BMS_LVGL_NATIVE_GESTURE_KEY_CONFIRM,
    ESP_BMS_LVGL_NATIVE_GESTURE_KEY_BACK,
} esp_bms_lvgl_native_gesture_t;
