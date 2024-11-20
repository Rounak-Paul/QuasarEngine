#pragma once

#include <qspch.h>

namespace Quasar
{
    size_t get_current_memory_usage();
    i32 platform_get_processor_count();
    void platform_sleep(u64 ms);
} // namespace Quasar
