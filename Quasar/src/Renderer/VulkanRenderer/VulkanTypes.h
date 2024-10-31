#pragma once

#define VK_CHECK(expr)                  \
{                                      \
    assert(expr == VK_SUCCESS);        \
} 