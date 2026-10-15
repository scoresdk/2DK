#pragma once

#include <tdk/Display.hpp>

namespace TDK
{
    class BlendDma
    {
    public:
        static constexpr uint32_t AlignmentPixels = 16u;
        static constexpr Byte MaxFactor = 63u;

        static Result Initialize();
        static Result Fill(const FrameBuffer& destination, uint32_t x, uint32_t y,
                           uint32_t width, uint32_t height, Pixel color);
        static Result Copy(const FrameBuffer& source, uint32_t sourceX,
                           uint32_t sourceY, const FrameBuffer& destination,
                           uint32_t destinationX, uint32_t destinationY, uint32_t width,
                           uint32_t height, bool useColorKey = false,
                           Pixel colorKey = 0u);
        static Result Blend(const FrameBuffer& sourceA, uint32_t aX, uint32_t aY,
                            const FrameBuffer& sourceB, uint32_t bX, uint32_t bY,
                            const FrameBuffer& destination, uint32_t destinationX,
                            uint32_t destinationY, uint32_t width, uint32_t height,
                            Byte factorA, Byte factorB, bool subtract = false);
        static bool IsBusy();
        static Result Wait();
        static Result SelfTest(const FrameBuffer& scratch);
        static bool IsAvailable();
    };
}
