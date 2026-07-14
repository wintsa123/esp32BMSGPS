#pragma once

#include <stdarg.h>
#include <stdio.h>

static inline void host_compat_log(const char *level, const char *tag, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    fprintf(stderr, "%s (%s): ", level, tag);
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
    va_end(args);
}

#define ESP_LOGE(tag, ...) host_compat_log("E", tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) host_compat_log("W", tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) host_compat_log("I", tag, __VA_ARGS__)
