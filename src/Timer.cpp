#include <tdk/Timer.hpp>

#include <tdk/TDK.hpp>

namespace TDK
{
    namespace
    {
        constexpr Address TimerBase[] = {0x88160000u, 0x88161000u, 0x88162000u,
                                         0x88163000u, 0x88164000u, 0x88165000u};
        constexpr Word InterruptFlag = 0x04000000u;
        constexpr Word InterruptEnable = 0x08000000u;
    }

    Result Timer::Disable(uint8_t timerNumber)
    {
        Word control;

        if (timerNumber >= 6u)
        {
            return Result::InvalidArgument;
        }
        if (ReadRegister(TimerBase[timerNumber], &control) != Result::Success)
        {
            return Result::HardwareError;
        }
        control &= ~(InterruptFlag | InterruptEnable);
        return WriteRegister(TimerBase[timerNumber], control);
    }

    Result Timer::DisableAll()
    {
        Result overall = Result::Success;

        for (uint8_t timerNumber = 0u; timerNumber < 6u; ++timerNumber)
        {
            const Result result = Disable(timerNumber);

            if (result != Result::Success && overall == Result::Success)
            {
                overall = result;
            }
        }
        return overall;
    }

    Result Timer::ReadCounter(uint8_t timerNumber, Word* value)
    {
        if (timerNumber >= 6u)
        {
            return Result::InvalidArgument;
        }
        return ReadRegister(TimerBase[timerNumber] + 0x10u, value);
    }
}
