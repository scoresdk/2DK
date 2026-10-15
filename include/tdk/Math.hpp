#pragma once

#include <tdk/Types.hpp>

namespace TDK
{
    class Math
    {
    public:
        static constexpr int32_t One = 4096;
        static constexpr uint32_t AngleSteps = 256u;

        static int32_t Sin(int32_t angle);
        static int32_t Cos(int32_t angle);
    };
}
