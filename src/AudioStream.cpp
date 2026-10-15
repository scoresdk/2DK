#include <tdk/AudioStream.hpp>

#include <tdk/Interrupts.hpp>
#include <tdk/Platform.hpp>
#include <tdk/TDK.hpp>

namespace TDK
{
    namespace
    {
        constexpr Address ClockConfig = 0x8821003cu;
        constexpr Address PllAudioConfig = 0x882100bcu;
        constexpr Address PinConfig = 0x8820009cu;
        constexpr Address DacControl = 0x88051474u;
        constexpr Address MixerControl = 0x88051034u;
        constexpr Address PhaseSoft = 0x88051064u;
        constexpr Address BufferBaseLow = 0x88051080u;
        constexpr Address BufferBaseHigh = 0x88051084u;
        constexpr Address ChannelControl = 0x88051088u;
        constexpr Address SpuStartAddress = 0x88070054u;

        constexpr Word ClockRun = 0x3u;
        constexpr Word PllEnableBits = 0x7u;
        constexpr Word PinEnableBit = 0x100u;
        constexpr Word DacRun = 0xfffffffcu;
        constexpr Word MixerReset = 0xc8u;
        constexpr Word MixerRun = 0x10cbu;
        constexpr Word ChannelRun = 0x4007u;
        constexpr Word ChannelAcknowledge = 0xc007u;
        constexpr Word PendingBit = 0x8000u;
        constexpr uint32_t SpuClockHz = 27000000u;
        constexpr uint32_t MinimumRate = 1000u;
        constexpr uint32_t MaximumRate = 48000u;
        constexpr uint32_t DelayIterations = 200000u;

        AudioStreamConfig activeConfig = {};
        bool running = false;
        uint32_t nextHalf = 0u;

        void FillHalf(uint32_t half)
        {
            activeConfig.fill(activeConfig.buffer +
                                  half * AudioStream::HalfBufferFrames * 2u,
                              AudioStream::HalfBufferFrames, activeConfig.context);
        }

        void HandleInterrupt(uint32_t, void*)
        {
            Word status = 0u;

            if (!running || ReadRegister(ChannelControl, &status) != Result::Success ||
                (status & PendingBit) == 0u)
            {
                return;
            }
            FillHalf(nextHalf);
            nextHalf ^= 1u;
            WriteRegister(ChannelControl, ChannelAcknowledge);
        }

        Result EnableOutput()
        {
            const Platform* platform = GetPlatform();
            Word value = 0u;

            if (WriteRegister(ClockConfig, ClockRun) != Result::Success ||
                ReadRegister(PllAudioConfig, &value) != Result::Success ||
                WriteRegister(PllAudioConfig, value | PllEnableBits) != Result::Success)
            {
                return Result::HardwareError;
            }
            if (platform->delayIterations != nullptr)
            {
                platform->delayIterations(platform->context, DelayIterations);
            }
            if (WriteRegister(DacControl, DacRun) != Result::Success)
            {
                return Result::HardwareError;
            }
            if (platform->delayIterations != nullptr)
            {
                platform->delayIterations(platform->context, DelayIterations);
            }
            if (ReadRegister(PinConfig, &value) != Result::Success ||
                WriteRegister(PinConfig, value | PinEnableBit) != Result::Success)
            {
                return Result::HardwareError;
            }
            return Result::Success;
        }
    }

    Result AudioStream::Start(const AudioStreamConfig& config)
    {
        if (!IsInitialized())
        {
            return Result::NotInitialized;
        }
        if (config.buffer == nullptr || config.fill == nullptr ||
            config.bufferAddress == 0u || (config.bufferAddress & 3u) != 0u ||
            config.sampleRate < MinimumRate || config.sampleRate > MaximumRate)
        {
            return Result::InvalidArgument;
        }
        if (running)
        {
            Stop();
        }

        Result result = Interrupts::Install(InterruptNumber, HandleInterrupt);
        if (result != Result::Success)
        {
            return result;
        }
        result = EnableOutput();
        if (result != Result::Success)
        {
            return result;
        }

        activeConfig = config;
        nextHalf = 0u;
        for (uint32_t index = 0u; index < BufferFrames * 2u; ++index)
        {
            config.buffer[index] = SilenceSample;
        }

        const Word phase = SpuClockHz / config.sampleRate - 1u;
        if (WriteRegister(MixerControl, MixerReset) != Result::Success ||
            WriteRegister(SpuStartAddress, 0u) != Result::Success)
        {
            return Result::HardwareError;
        }
        FillHalf(0u);
        FillHalf(1u);
        running = true;
        if (WriteRegister(ChannelControl, ChannelRun) != Result::Success ||
            WriteRegister(BufferBaseLow, config.bufferAddress & 0xffffu) !=
                Result::Success ||
            WriteRegister(BufferBaseHigh, config.bufferAddress >> 16) !=
                Result::Success ||
            WriteRegister(PhaseSoft, phase) != Result::Success ||
            WriteRegister(MixerControl, MixerRun) != Result::Success)
        {
            running = false;
            return Result::HardwareError;
        }
        return Result::Success;
    }

    Result AudioStream::Stop()
    {
        running = false;
        if (WriteRegister(MixerControl, MixerReset) != Result::Success ||
            WriteRegister(ChannelControl, 0u) != Result::Success ||
            WriteRegister(PhaseSoft, 0u) != Result::Success)
        {
            return Result::HardwareError;
        }
        return Interrupts::Uninstall(InterruptNumber);
    }

    bool AudioStream::IsRunning()
    {
        return running;
    }
}
