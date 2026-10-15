#pragma once

#include <tdk/Types.hpp>

namespace TDK
{
    class Interrupts
    {
    public:
        static constexpr uint32_t Count = 64u;

        static Result Install(uint32_t interruptNumber, InterruptHandler handler,
                              void* context = nullptr);
        static Result Uninstall(uint32_t interruptNumber);
        static Result Enable();
        static Result Disable();
        static Result Wait();
        static bool Dispatch(uint32_t interruptNumber);
    };
}
