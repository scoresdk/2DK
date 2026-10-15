#pragma once

#include <tdk/Types.hpp>

namespace TDK
{
    using AudioFillCallback = void (*)(volatile uint16_t* frames, uint32_t frameCount,
                                       void* context);

    struct AudioStreamConfig
    {
        volatile uint16_t* buffer;
        Address bufferAddress;
        uint32_t sampleRate;
        AudioFillCallback fill;
        void* context;
    };

    class AudioStream
    {
    public:
        static constexpr uint32_t BufferFrames = 2048u;
        static constexpr uint32_t HalfBufferFrames = BufferFrames / 2u;
        static constexpr uint32_t InterruptNumber = 63u;
        static constexpr uint16_t SilenceSample = 0x8000u;

        static Result Start(const AudioStreamConfig& config);
        static Result Stop();
        static bool IsRunning();

        static constexpr uint16_t ToSample(int32_t value)
        {
            const int32_t clamped =
                value > 32767 ? 32767 : (value < -32768 ? -32768 : value);
            return static_cast<uint16_t>(clamped + 0x8000);
        }
    };
}
