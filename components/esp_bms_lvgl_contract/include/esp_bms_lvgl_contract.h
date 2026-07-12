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
