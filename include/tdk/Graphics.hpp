#pragma once

#include <tdk/Types.hpp>

namespace TDK
{
    class Graphics
    {
    public:
        static Result Clear(Pixel color);
        static Result DrawPixel(int32_t x, int32_t y, Pixel color);
        static Result FillRectangle(int32_t x, int32_t y, uint32_t width,
                                    uint32_t height, Pixel color);
        static Result DrawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                               Pixel color);
        static Result DrawRectangle(int32_t x, int32_t y, uint32_t width,
                                    uint32_t height, Pixel color);
        static Result DrawSquare(int32_t x, int32_t y, uint32_t size, Pixel color);
    };
}
