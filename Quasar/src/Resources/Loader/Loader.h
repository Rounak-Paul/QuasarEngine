#pragma once

#include <qspch.h>

namespace Quasar
{
class Loader {
public:
    virtual ~Loader() = default;

    virtual bool load(const String& path) = 0;
    virtual void unload() = 0;
};
} // namespace Quasar