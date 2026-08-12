#pragma once

#include <stdint.h>
#include <time.h>

static inline int64_t esp_timer_get_time(void)
{
    struct timespec now = { 0 };
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * INT64_C(1000000) + now.tv_nsec / 1000;
}
