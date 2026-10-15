#pragma once

#include <tdk/Display.hpp>

namespace TDK
{
    enum class Layer : uint8_t
    {
        Back = 0,
        Middle = 1,
        Front = 2
    };

    struct LayerSetup
    {
        FrameBuffer buffers[2];
        Text1NumberArray numberArray;
        Byte depth;
        bool visible;
    };

    class Layers
    {
    public:
        static constexpr uint32_t Count = 3u;
        static constexpr uint32_t BufferCount = 2u;
        static constexpr Byte MaxDepth = 3u;
        static constexpr Byte MaxBlendLevel = 63u;

        static constexpr uint32_t Mask(Layer layer)
        {
            return 1u << static_cast<uint32_t>(layer);
        }

        static constexpr uint32_t AllMask = 7u;

        static Result Configure(Layer layer, const LayerSetup& setup);
        static Result SetVisible(Layer layer, bool visible);
        static Result SetDepth(Layer layer, Byte depth);
        static Result SetBlend(Layer layer, bool enabled, Byte level);
        static Result SetScroll(Layer layer, int32_t x, int32_t y);
        static Result GetBackBuffer(Layer layer, FrameBuffer* frameBuffer);
        static Result GetShownBuffer(Layer layer, FrameBuffer* frameBuffer);
        static uint32_t BackIndex(Layer layer);
        static Result BeginDraw(Layer layer);
        static Result Flip(uint32_t layerMask);
        static bool IsConfigured(Layer layer);
    };
}
