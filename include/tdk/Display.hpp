#pragma once

#include <tdk/Types.hpp>

namespace TDK
{
    struct FrameBuffer
    {
        volatile Word* pixels;
        size_t capacityWords;
        Address physicalAddress;
        uint16_t width;
        uint16_t height;
        uint16_t strideWords;
    };

    struct Text1NumberArray
    {
        volatile Word* entries;
        size_t capacityEntries;
        Address physicalAddress;
    };

    class Display
    {
    public:
        static Result Initialize(const FrameBuffer& frameBuffer,
                                 const Text1NumberArray& text1NumberArray);
        static Result WaitForVBlank();
        static Word GetFrameCount();
        static Result SetFade(Byte level);
        static Result FadeIn();
        static Result Shutdown();
        static Result SetDrawBuffer(const FrameBuffer& frameBuffer);
        static Result ShowBuffer(const FrameBuffer& frameBuffer);
        static bool IsInitialized();
        static const FrameBuffer* GetFrameBuffer();
    };
}
