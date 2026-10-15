#include <tdk/Watchdog.hpp>

#include <tdk/TDK.hpp>

namespace TDK
{
    namespace
    {
        constexpr Address ModeControl = 0x88170000u;
        constexpr Address CycleSetup = 0x88170004u;
        constexpr Address ClearCommand = 0x88170008u;
        constexpr Address ResetStatus = 0x882100e8u;
        constexpr Word EnableBit = 0x80000000u;
        constexpr Word ClearValue = 0xa0000005u;
    }

    Result Watchdog::Feed()
    {
        return WriteRegister(ClearCommand, ClearValue);
    }

    Result Watchdog::Disable()
    {
        Word mode;

        if (Feed() != Result::Success ||
            ReadRegister(ModeControl, &mode) != Result::Success ||
            WriteRegister(ModeControl, mode & ~EnableBit) != Result::Success)
        {
            return Result::HardwareError;
        }
        return Feed();
    }

    Result Watchdog::Enable(Word cycleCount)
    {
        Word mode;

        if (cycleCount == 0u)
        {
            return Result::InvalidArgument;
        }
        if (WriteRegister(CycleSetup, cycleCount) != Result::Success ||
            Feed() != Result::Success ||
            ReadRegister(ModeControl, &mode) != Result::Success)
        {
            return Result::HardwareError;
        }
        return WriteRegister(ModeControl, mode | EnableBit);
    }

    Result Watchdog::IsEnabled(bool* enabled)
    {
        Word mode;

        if (enabled == nullptr)
        {
            return Result::InvalidArgument;
        }
        if (ReadRegister(ModeControl, &mode) != Result::Success)
        {
            return Result::HardwareError;
        }
        *enabled = (mode & EnableBit) != 0u;
        return Result::Success;
    }

    Result Watchdog::ReadResetStatus(Word* status)
    {
        if (status == nullptr)
        {
            return Result::InvalidArgument;
        }
        return ReadRegister(ResetStatus, status) == Result::Success
                   ? Result::Success
                   : Result::HardwareError;
    }
}
