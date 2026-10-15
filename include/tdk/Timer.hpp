#pragma once

#include <tdk/Types.hpp>

namespace TDK
{
    class Timer
    {
    public:
        static Result Disable(uint8_t timerNumber);
        static Result DisableAll();
        static Result ReadCounter(uint8_t timerNumber, Word* value);
    };
}
