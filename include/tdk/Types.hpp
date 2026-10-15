#pragma once

#include <stddef.h>
#include <stdint.h>

namespace TDK
{
    using Address = uint32_t;
    using Byte = uint8_t;
    using HalfWord = uint16_t;
    using Word = uint32_t;
    using InterruptHandler = void (*)(uint32_t interruptNumber, void* context);

    enum class Result : uint8_t
    {
        Success,
        InvalidArgument,
        NotInitialized,
        HardwareError,
        Unsupported
    };

    using Pixel = HalfWord;

    struct Color
    {
        static constexpr Pixel RGB565(Byte red, Byte green, Byte blue)
        {
            return static_cast<Pixel>(((red >> 3) << 11) | ((green >> 2) << 5) |
                                      (blue >> 3));
        }

        static constexpr Pixel White()
        {
            return RGB565(255, 255, 255);
        }

        static constexpr Pixel Black()
        {
            return RGB565(0, 0, 1);
        }
    };
}
