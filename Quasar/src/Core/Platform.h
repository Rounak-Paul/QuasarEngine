#pragma once

#include <qspch.h>

namespace Quasar
{
QS_API size_t get_current_memory_usage();
QS_API i32 platform_get_processor_count();
QS_API void platform_sleep(u64 ms);
} // namespace Quasar
