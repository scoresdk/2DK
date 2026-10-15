#pragma once

#include <tdk/Platform.hpp>

namespace TDK
{
    Result Init(const Platform& platform);
    bool IsInitialized();
}
