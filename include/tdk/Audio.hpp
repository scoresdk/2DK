#pragma once

#include <tdk/Types.hpp>

namespace TDK
{
    struct AudioConfig
    {
        Address fifoLowAddress;
        Address fifoHighAddress;
        Word sampleClock;
        Word modeControl1;
        Word modeControl2;
        Word interruptControl;
    };

    class Audio
    {
    public:
        static Result Initialize(const AudioConfig& config);
        static Result SetSampleClock(Word value);
        static Result SetVolume(uint8_t level);
        static Result EnableChannels(bool left, bool right);
        static Result Stop();
    };
}
