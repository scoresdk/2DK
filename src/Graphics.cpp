#include <tdk/Graphics.hpp>

#include <tdk/Display.hpp>

namespace TDK
{
    namespace
    {
        Result WritePixel(const FrameBuffer& frameBuffer, uint32_t x, uint32_t y,
                          HalfWord color)
        {
            volatile HalfWord* pixels = reinterpret_cast<volatile HalfWord*>(
                const_cast<Word*>(frameBuffer.pixels));
            const size_t pixelIndex =
                static_cast<size_t>(y) * frameBuffer.strideWords * 2u + x;

            pixels[pixelIndex] = color;
            return Result::Success;
        }
    }

    Result Graphics::Clear(Pixel color)
    {
        const FrameBuffer* frameBuffer = Display::GetFrameBuffer();
        const HalfWord pair = color;
        const Word packedColor =
            static_cast<Word>(pair) | (static_cast<Word>(pair) << 16);

        if (frameBuffer == nullptr)
        {
            return Result::NotInitialized;
        }
        const uint32_t visibleWordsPerRow =
            (static_cast<uint32_t>(frameBuffer->width) + 1u) / 2u;
        for (uint32_t row = 0u; row < frameBuffer->height; ++row)
        {
            const size_t rowStart = static_cast<size_t>(row) * frameBuffer->strideWords;
            const uint32_t fullWords = frameBuffer->width / 2u;
            for (uint32_t column = 0u; column < fullWords; ++column)
            {
                frameBuffer->pixels[rowStart + column] = packedColor;
            }
            if (visibleWordsPerRow > fullWords)
            {
                WritePixel(*frameBuffer, frameBuffer->width - 1u, row, color);
            }
        }
        return Result::Success;
    }

    Result Graphics::DrawPixel(int32_t x, int32_t y, Pixel color)
    {
        const FrameBuffer* frameBuffer = Display::GetFrameBuffer();

        if (frameBuffer == nullptr)
        {
            return Result::NotInitialized;
        }
        if (x < 0 || y < 0 || x >= frameBuffer->width || y >= frameBuffer->height)
        {
            return Result::InvalidArgument;
        }
        return WritePixel(*frameBuffer, static_cast<uint32_t>(x),
                          static_cast<uint32_t>(y), color);
    }

    Result Graphics::FillRectangle(int32_t x, int32_t y, uint32_t width,
                                   uint32_t height, Pixel color)
    {
        const FrameBuffer* frameBuffer = Display::GetFrameBuffer();

        if (frameBuffer == nullptr)
        {
            return Result::NotInitialized;
        }
        if (x < 0 || y < 0)
        {
            return Result::InvalidArgument;
        }
        if (width == 0u || height == 0u)
        {
            return Result::Success;
        }
        if (static_cast<uint32_t>(x) >= frameBuffer->width ||
            static_cast<uint32_t>(y) >= frameBuffer->height ||
            width > frameBuffer->width - static_cast<uint32_t>(x) ||
            height > frameBuffer->height - static_cast<uint32_t>(y))
        {
            return Result::InvalidArgument;
        }

        const Word packedColor =
            static_cast<Word>(color) | (static_cast<Word>(color) << 16);
        const uint32_t left = static_cast<uint32_t>(x);
        const uint32_t right = left + width;

        for (uint32_t row = 0u; row < height; ++row)
        {
            const uint32_t pixelRow = static_cast<uint32_t>(y) + row;
            const size_t rowStart =
                static_cast<size_t>(pixelRow) * frameBuffer->strideWords;
            uint32_t column = left;

            if ((column & 1u) != 0u)
            {
                WritePixel(*frameBuffer, column, pixelRow, color);
                ++column;
            }
            for (; column + 1u < right; column += 2u)
            {
                frameBuffer->pixels[rowStart + column / 2u] = packedColor;
            }
            if (column < right)
            {
                WritePixel(*frameBuffer, column, pixelRow, color);
            }
        }
        return Result::Success;
    }

    Result Graphics::DrawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                              Pixel color)
    {
        const FrameBuffer* frameBuffer = Display::GetFrameBuffer();

        if (frameBuffer == nullptr)
        {
            return Result::NotInitialized;
        }

        const int32_t deltaX = x1 > x0 ? x1 - x0 : x0 - x1;
        const int32_t deltaY = y1 > y0 ? y0 - y1 : y1 - y0;
        const int32_t stepX = x0 < x1 ? 1 : -1;
        const int32_t stepY = y0 < y1 ? 1 : -1;
        int32_t error = deltaX + deltaY;

        for (;;)
        {
            if (x0 >= 0 && y0 >= 0 && x0 < frameBuffer->width &&
                y0 < frameBuffer->height)
            {
                WritePixel(*frameBuffer, static_cast<uint32_t>(x0),
                           static_cast<uint32_t>(y0), color);
            }
            if (x0 == x1 && y0 == y1)
            {
                break;
            }
            const int32_t doubled = error * 2;
            if (doubled >= deltaY)
            {
                error += deltaY;
                x0 += stepX;
            }
            if (doubled <= deltaX)
            {
                error += deltaX;
                y0 += stepY;
            }
        }
        return Result::Success;
    }

    Result Graphics::DrawRectangle(int32_t x, int32_t y, uint32_t width,
                                   uint32_t height, Pixel color)
    {
        Result result;

        if (width == 0u || height == 0u)
        {
            return Result::Success;
        }
        result = FillRectangle(x, y, width, 1u, color);
        if (result != Result::Success || height == 1u)
        {
            return result;
        }
        result =
            FillRectangle(x, y + static_cast<int32_t>(height) - 1, width, 1u, color);
        if (result != Result::Success || width == 1u)
        {
            return result;
        }
        result = FillRectangle(x, y + 1, 1u, height - 2u, color);
        if (result != Result::Success || height == 2u)
        {
            return result;
        }
        return FillRectangle(x + static_cast<int32_t>(width) - 1, y + 1, 1u,
                             height - 2u, color);
    }

    Result Graphics::DrawSquare(int32_t x, int32_t y, uint32_t size, Pixel color)
    {
        return FillRectangle(x, y, size, size, color);
    }
}
