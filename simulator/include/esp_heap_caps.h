#pragma once

#include <stddef.h>

#define MALLOC_CAP_DEFAULT 0U

static inline size_t heap_caps_get_free_size(unsigned caps)
{
    (void)caps;
    return 0U;
}
static inline size_t heap_caps_get_minimum_free_size(unsigned caps)
{
    (void)caps;
    return 0U;
}

static inline size_t heap_caps_get_largest_free_block(unsigned caps)
{
    (void)caps;
    return 0U;
}
