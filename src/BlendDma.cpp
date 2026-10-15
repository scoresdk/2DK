#include <tdk/BlendDma.hpp>

#include <tdk/Platform.hpp>
#include <tdk/TDK.hpp>

namespace TDK
{
    namespace
    {
        constexpr Address WidthHeight = 0x880d000cu;
        constexpr Address FillPattern = 0x880d0010u;
        constexpr Address Control1 = 0x880d0014u;
        constexpr Address Status = 0x880d0018u;
        constexpr Address BlendFactor = 0x880d001cu;
        constexpr Address FilterPattern = 0x880d0020u;
        constexpr Address AddressMode = 0x880d0024u;
        constexpr Address Control2 = 0x880d0028u;
        constexpr Address SourceABase = 0x880d0030u;
        constexpr Address SourceAOffset = 0x880d0034u;
        constexpr Address SourceABackground = 0x880d0038u;
        constexpr Address SourceBBase = 0x880d0040u;
        constexpr Address SourceBOffset = 0x880d0044u;
        constexpr Address SourceBBackground = 0x880d0048u;
        constexpr Address DestinationBase = 0x880d0050u;
        constexpr Address DestinationOffset = 0x880d0054u;
        constexpr Address DestinationBackground = 0x880d0058u;
        constexpr Address ClockConfig = 0x88210050u;

        constexpr Word ClockEnable = 3u;
        constexpr Word StartBit = 1u << 24;
        constexpr Word FilterBit = 1u << 16;
        constexpr Word SubtractBit = 1u << 8;
        constexpr Word BusyBit = 1u << 8;
        constexpr Word ModeCopy = 1u;
        constexpr Word ModeBlend = 2u;
        constexpr Word ModeFill = 3u;
        constexpr Word SourceABlock = 1u;
        constexpr Word SourceBBlock = 1u << 8;
        constexpr Word DestinationBlock = 1u << 16;
        constexpr Word AddressMask = 0x0fffffffu;
        constexpr uint32_t BusyLimit = 2000000u;
        constexpr uint32_t IdleReads = 8u;
        constexpr Word StateShift = 16u;
        constexpr Word StateMask = 7u;
        constexpr uint32_t MaxRows = 2047u;

        bool initialized = false;
        bool available = false;

        bool BackgroundWidthCode(uint32_t pixels, Word* code)
        {
            switch (pixels)
            {
            case 256u:
                *code = 0u;
                return true;
            case 320u:
                *code = 1u;
                return true;
            case 512u:
                *code = 2u;
                return true;
            case 640u:
                *code = 3u;
                return true;
            case 1024u:
                *code = 4u;
                return true;
            case 2048u:
                *code = 5u;
                return true;
            default:
                return false;
            }
        }

        bool BackgroundHeightCode(uint32_t rows, Word* code)
        {
            switch (rows)
            {
            case 240u:
                *code = 0u;
                return true;
            case 256u:
                *code = 1u;
                return true;
            case 480u:
                *code = 2u;
                return true;
            case 512u:
                *code = 3u;
                return true;
            case 1024u:
                *code = 4u;
                return true;
            case 2048u:
                *code = 5u;
                return true;
            default:
                return false;
            }
        }

        bool BackgroundCode(const FrameBuffer& buffer, Word* code)
        {
            Word widthCode = 0u;
            Word heightCode = 0u;

            if (!BackgroundWidthCode(static_cast<uint32_t>(buffer.strideWords) * 2u,
                                     &widthCode) ||
                !BackgroundHeightCode(buffer.height, &heightCode))
            {
                return false;
            }
            *code = widthCode | (heightCode << 8);
            return true;
        }

        bool ValidRegion(const FrameBuffer& buffer, uint32_t x, uint32_t y,
                         uint32_t width, uint32_t height)
        {
            return buffer.pixels != nullptr && buffer.physicalAddress != 0u &&
                   (x & 1u) == 0u && width != 0u && height != 0u && height <= MaxRows &&
                   width % BlendDma::AlignmentPixels == 0u &&
                   x + width <= static_cast<uint32_t>(buffer.strideWords) * 2u &&
                   y + height <= buffer.height;
        }

        Word OffsetWord(uint32_t x, uint32_t y)
        {
            return (x / 2u) | (y << 16);
        }

        Result Write(Address address, Word value)
        {
            return WriteRegister(address, value) == Result::Success
                       ? Result::Success
                       : Result::HardwareError;
        }

        Result SetPlane(Address base, Address offset, Address background,
                        const FrameBuffer& buffer, uint32_t x, uint32_t y)
        {
            Word code = 0u;

            if (!BackgroundCode(buffer, &code))
            {
                return Result::InvalidArgument;
            }
            if (Write(base, buffer.physicalAddress & AddressMask) != Result::Success ||
                Write(offset, OffsetWord(x, y)) != Result::Success ||
                Write(background, code) != Result::Success)
            {
                return Result::HardwareError;
            }
            return Result::Success;
        }

        Result Start(Word addressMode, uint32_t width, uint32_t height, Word control)
        {
            if (Write(AddressMode, addressMode) != Result::Success ||
                Write(Control2, 0u) != Result::Success ||
                Write(WidthHeight, (width / 2u) | (height << 16)) != Result::Success)
            {
                return Result::HardwareError;
            }
            return Write(Control1, control | StartBit);
        }
    }

    Result BlendDma::Initialize()
    {
        if (!IsInitialized())
        {
            return Result::NotInitialized;
        }
        if (Write(ClockConfig, ClockEnable) != Result::Success)
        {
            return Result::HardwareError;
        }
        initialized = true;
        return Result::Success;
    }

    bool BlendDma::IsBusy()
    {
        Word status = 0u;
        Word state = 0u;

        return ReadRegister(Status, &status) == Result::Success &&
               ReadRegister(Control2, &state) == Result::Success &&
               ((status & BusyBit) != 0u || ((state >> StateShift) & StateMask) != 0u);
    }

    Result BlendDma::Wait()
    {
        uint32_t idle = 0u;

        for (uint32_t spin = 0u; spin < BusyLimit; ++spin)
        {
            Word status = 0u;
            Word state = 0u;

            if (ReadRegister(Status, &status) != Result::Success ||
                ReadRegister(Control2, &state) != Result::Success)
            {
                return Result::HardwareError;
            }
            if ((status & BusyBit) == 0u && ((state >> StateShift) & StateMask) == 0u)
            {
                if (++idle >= IdleReads)
                {
                    return Result::Success;
                }
            }
            else
            {
                idle = 0u;
            }
        }
        return Result::HardwareError;
    }

    Result BlendDma::Fill(const FrameBuffer& destination, uint32_t x, uint32_t y,
                          uint32_t width, uint32_t height, Pixel color)
    {
        if (!initialized)
        {
            return Result::NotInitialized;
        }
        if (!ValidRegion(destination, x, y, width, height))
        {
            return Result::InvalidArgument;
        }

        Result result = Wait();

        if (result == Result::Success)
        {
            result = SetPlane(DestinationBase, DestinationOffset, DestinationBackground,
                              destination, x, y);
        }
        if (result == Result::Success &&
            Write(FillPattern, static_cast<Word>(color) |
                                   (static_cast<Word>(color) << 16)) != Result::Success)
        {
            result = Result::HardwareError;
        }
        if (result == Result::Success)
        {
            result = Start(DestinationBlock, width, height, ModeFill);
        }
        return result;
    }

    Result BlendDma::Copy(const FrameBuffer& source, uint32_t sourceX, uint32_t sourceY,
                          const FrameBuffer& destination, uint32_t destinationX,
                          uint32_t destinationY, uint32_t width, uint32_t height,
                          bool useColorKey, Pixel colorKey)
    {
        if (!initialized)
        {
            return Result::NotInitialized;
        }
        if (!ValidRegion(source, sourceX, sourceY, width, height) ||
            !ValidRegion(destination, destinationX, destinationY, width, height))
        {
            return Result::InvalidArgument;
        }

        Result result = Wait();

        if (result == Result::Success)
        {
            result = SetPlane(SourceABase, SourceAOffset, SourceABackground, source,
                              sourceX, sourceY);
        }
        if (result == Result::Success)
        {
            result = SetPlane(DestinationBase, DestinationOffset, DestinationBackground,
                              destination, destinationX, destinationY);
        }
        if (result == Result::Success && useColorKey &&
            Write(FilterPattern, colorKey) != Result::Success)
        {
            result = Result::HardwareError;
        }
        if (result == Result::Success)
        {
            result = Start(SourceABlock | DestinationBlock, width, height,
                           ModeCopy | (useColorKey ? FilterBit : 0u));
        }
        return result;
    }

    Result BlendDma::Blend(const FrameBuffer& sourceA, uint32_t aX, uint32_t aY,
                           const FrameBuffer& sourceB, uint32_t bX, uint32_t bY,
                           const FrameBuffer& destination, uint32_t destinationX,
                           uint32_t destinationY, uint32_t width, uint32_t height,
                           Byte factorA, Byte factorB, bool subtract)
    {
        if (!initialized)
        {
            return Result::NotInitialized;
        }
        if (factorA > MaxFactor || factorB > MaxFactor ||
            !ValidRegion(sourceA, aX, aY, width, height) ||
            !ValidRegion(sourceB, bX, bY, width, height) ||
            !ValidRegion(destination, destinationX, destinationY, width, height))
        {
            return Result::InvalidArgument;
        }

        Result result = Wait();

        if (result == Result::Success)
        {
            result = SetPlane(SourceABase, SourceAOffset, SourceABackground, sourceA,
                              aX, aY);
        }
        if (result == Result::Success)
        {
            result = SetPlane(SourceBBase, SourceBOffset, SourceBBackground, sourceB,
                              bX, bY);
        }
        if (result == Result::Success)
        {
            result = SetPlane(DestinationBase, DestinationOffset, DestinationBackground,
                              destination, destinationX, destinationY);
        }
        if (result == Result::Success &&
            Write(BlendFactor, factorA | (static_cast<Word>(factorB) << 8)) !=
                Result::Success)
        {
            result = Result::HardwareError;
        }
        if (result == Result::Success)
        {
            result = Start(SourceABlock | SourceBBlock | DestinationBlock, width,
                           height, ModeBlend | (subtract ? SubtractBit : 0u));
        }
        return result;
    }

    Result BlendDma::SelfTest(const FrameBuffer& scratch)
    {
        constexpr Pixel pattern = 0x1234u;
        constexpr uint32_t rows = 4u;
        constexpr uint32_t wordsPerRow = 8u;

        available = false;
        if (!initialized)
        {
            return Result::NotInitialized;
        }
        if (scratch.height < 2u * rows || scratch.width < 2u * wordsPerRow * 2u)
        {
            return Result::InvalidArgument;
        }

        for (uint32_t row = 0u; row < 2u * rows; ++row)
        {
            for (uint32_t word = 0u; word < wordsPerRow * 2u; ++word)
            {
                scratch.pixels[row * scratch.strideWords + word] = 0u;
            }
        }
        Result result = Fill(scratch, 0u, 0u, wordsPerRow * 2u, rows, pattern);

        if (result == Result::Success)
        {
            result = Wait();
        }
        if (result == Result::Success)
        {
            result = Copy(scratch, 0u, 0u, scratch, 0u, rows, wordsPerRow * 2u, rows);
        }
        if (result == Result::Success)
        {
            result = Wait();
        }
        if (result != Result::Success)
        {
            return result;
        }

        const Word expected =
            static_cast<Word>(pattern) | (static_cast<Word>(pattern) << 16);

        for (uint32_t row = 0u; row < 2u * rows; ++row)
        {
            for (uint32_t word = 0u; word < wordsPerRow; ++word)
            {
                if (scratch.pixels[row * scratch.strideWords + word] != expected)
                {
                    return Result::HardwareError;
                }
            }
            if (scratch.pixels[row * scratch.strideWords + wordsPerRow] != 0u)
            {
                return Result::HardwareError;
            }
        }
        available = true;
        return Result::Success;
    }

    bool BlendDma::IsAvailable()
    {
        return available;
    }
}
