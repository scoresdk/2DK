#include <tdk/Audio.hpp>

#include <tdk/TDK.hpp>

namespace TDK
{
    namespace
    {
        constexpr Address ClockConfig = 0x8821003cu;
        constexpr Address FifoLow = 0x88051080u;
        constexpr Address FifoHigh = 0x88051084u;
        constexpr Address SampleClock = 0x88051064u;
        constexpr Address InterruptStatus = 0x88051088u;
        constexpr Address ModeControl1 = 0x88051474u;
        constexpr Address ModeControl2 = 0x88051034u;
        constexpr Word ClockEnable = 0x00000001u;
        constexpr Word LeftChannelEnable = 0x00000002u;
        constexpr Word RightChannelEnable = 0x00000001u;
        constexpr Word VolumeMask = 0x000000c0u;
    }

    Result Audio::Initialize(const AudioConfig& config)
    {
        if (!IsInitialized() || config.fifoLowAddress == 0u ||
            config.fifoHighAddress == 0u || (config.fifoLowAddress & 3u) != 0u ||
            (config.fifoHighAddress & 3u) != 0u)
        {
            return IsInitialized() ? Result::InvalidArgument : Result::NotInitialized;
        }

        if (WriteRegister(ClockConfig, ClockEnable) != Result::Success ||
            WriteRegister(FifoLow, config.fifoLowAddress) != Result::Success ||
            WriteRegister(FifoHigh, config.fifoHighAddress) != Result::Success ||
            WriteRegister(SampleClock, config.sampleClock) != Result::Success ||
            WriteRegister(ModeControl1, config.modeControl1) != Result::Success ||
            WriteRegister(ModeControl2, config.modeControl2) != Result::Success ||
            WriteRegister(InterruptStatus, config.interruptControl) != Result::Success)
        {
            return Result::HardwareError;
        }
        return Result::Success;
    }

    Result Audio::SetSampleClock(Word value)
    {
        return WriteRegister(SampleClock, value);
    }

    Result Audio::SetVolume(uint8_t level)
    {
        Word control;

        if (level > 3u)
        {
            return Result::InvalidArgument;
        }
        if (ReadRegister(ModeControl2, &control) != Result::Success)
        {
            return Result::HardwareError;
        }
        control = (control & ~VolumeMask) | (static_cast<Word>(level) << 6);
        return WriteRegister(ModeControl2, control);
    }

    Result Audio::EnableChannels(bool left, bool right)
    {
        Word control;

        if (ReadRegister(ModeControl2, &control) != Result::Success)
        {
            return Result::HardwareError;
        }
        control &= ~(LeftChannelEnable | RightChannelEnable);
        if (left)
        {
            control |= LeftChannelEnable;
        }
        if (right)
        {
            control |= RightChannelEnable;
        }
        return WriteRegister(ModeControl2, control);
    }

    Result Audio::Stop()
    {
        Word control;

        if (ReadRegister(ModeControl1, &control) != Result::Success ||
            WriteRegister(ModeControl1, control | 3u) != Result::Success ||
            WriteRegister(InterruptStatus, 0u) != Result::Success ||
            WriteRegister(ModeControl2, 0u) != Result::Success)
        {
            return Result::HardwareError;
        }
        return Result::Success;
    }
}
