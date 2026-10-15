#pragma once

#include <tdk/Types.hpp>

namespace TDK
{
    class Watchdog
    {
    public:
        static Result Feed();
        static Result Disable();
        static Result Enable(Word cycleCount);
        static Result IsEnabled(bool* enabled);
        static Result ReadResetStatus(Word* status);
    };
}
