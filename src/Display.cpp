#include <tdk/Display.hpp>

#include <tdk/Interrupts.hpp>
#include <tdk/Platform.hpp>
#include <tdk/TDK.hpp>

namespace TDK
{
    namespace
    {
        constexpr Address DisplayControl = 0x88010000u;
        constexpr Address DisplayEngineControl = 0x88030000u;
        constexpr Address TvFade = 0x8803000cu;
        constexpr Address PpuIrqControl = 0x88010080u;
        constexpr Address PpuIrqStatus = 0x88010084u;
        constexpr uint32_t PpuVBlankStartInterrupt = 53u;
        constexpr Word PpuIrqAllStatusBits = 7u;
        constexpr Word PpuVBlankStartBit = 1u;
        constexpr uint32_t VBlankWaitLimit = 20000000u;
        constexpr Address Text1XPosition = 0x88010020u;
        constexpr Address Text1YPosition = 0x88010024u;
        constexpr Address Text1Attribute = 0x88010028u;
        constexpr Address Text1Control = 0x8801002cu;
        constexpr Address Text1NumberArrayPointer = 0x88010030u;
        constexpr Address Text1BlendLevel = 0x88010038u;
        constexpr Address Text1BitmapBuffer = 0x880100a0u;
        constexpr size_t Text1NumberArrayEntries = 0x200u;

        constexpr Address OutputBuffers[] = {0xa0cf0000u, 0xa0df0000u, 0xa0ef0000u};
        enum class Mode : uint8_t
        {
            Vga,
            Hvga,
            Qvga
        };

        Mode activeMode = Mode::Vga;

        Mode ModeForSize(uint16_t width, uint16_t height)
        {
            if (width == 320u && height == 240u)
            {
                return Mode::Qvga;
            }
            if (width == 640u && height == 240u)
            {
                return Mode::Hvga;
            }
            return Mode::Vga;
        }

        uint32_t OutputBufferWordsFor(Mode mode)
        {
            return mode == Mode::Qvga
                       ? 320u * 240u / 2u
                       : (mode == Mode::Hvga ? 640u * 240u / 2u : 640u * 480u / 2u);
        }

        Result ApplyMode(Mode mode);

        struct RegisterValue
        {
            Address address;
            Word value;
        };

        constexpr RegisterValue PeripheralSetup[] = {
            {0x88210038u, 2u}, {0x88210024u, 0u}, {0x88210020u, 3u}, {0x882100c8u, 3u},
            {0x8809004cu, 1u}, {0x88210034u, 3u}, {0x88210050u, 3u}, {0x88210030u, 3u},
            {0x88030050u, 0u}, {0x88210058u, 3u}};

        constexpr RegisterValue SpriteQueueRegisters[] = {
            {0x88010020u, 0u}, {0x8801003cu, 0u}, {0x88010058u, 0u}, {0x88010024u, 0u},
            {0x88010040u, 0u}, {0x8801005cu, 0u}, {0x88010028u, 0u}, {0x88010044u, 0u},
            {0x88010060u, 0u}, {0x8801002cu, 0u}, {0x88010048u, 0u}, {0x88010064u, 0u},
            {0x88010030u, 0u}, {0x8801004cu, 0u}, {0x88010068u, 0u}, {0x88010034u, 0u},
            {0x88010050u, 0u}, {0x8801006cu, 0u}, {0x88010038u, 0u}, {0x88010054u, 0u},
            {0x88010070u, 0u}};

        constexpr RegisterValue BackendSetup[] = {{0x88090004u, 0xf0u},
                                                  {0x8809003cu, 1u},
                                                  {0x88090058u, 1u},
                                                  {0x880100c4u, 0xa0cf0000u},
                                                  {0x880100c8u, 0xa0df0000u},
                                                  {0x880100ccu, 0xa0ef0000u},
                                                  {DisplayEngineControl, 0x1114u},
                                                  {DisplayControl, 0x1001u},
                                                  {0x88010090u, 100u},
                                                  {0x88010080u, 1u},
                                                  {0x8803000cu, 0xffu},
                                                  {DisplayControl, 0x1001u},
                                                  {DisplayEngineControl, 0x1105u},
                                                  {0x88010004u, 3u},
                                                  {0x88010008u, 0x1ffu},
                                                  {0x8801000cu, 0u},
                                                  {0x88010090u, 0x5au},
                                                  {0x88010080u, 1u},
                                                  {0x88030004u, 0x1cu},
                                                  {0x88030008u, 2u}};

        FrameBuffer activeFrameBuffer = {};
        FrameBuffer shownFrameBuffer = {};
        bool initialized = false;
        volatile Word frameCount = 0u;

        void HandleVBlank(uint32_t, void*)
        {
            Word status = 0u;

            WriteRegister(PpuIrqStatus, PpuIrqAllStatusBits);
            frameCount = frameCount + 1u;
            for (uint32_t attempt = 0u; attempt < 4u; ++attempt)
            {
                if (ReadRegister(PpuIrqStatus, &status) != Result::Success ||
                    (status & PpuVBlankStartBit) == 0u)
                {
                    break;
                }
                WriteRegister(PpuIrqStatus, PpuVBlankStartBit);
            }
        }

        void PollVBlank()
        {
            Word status = 0u;

            if (ReadRegister(PpuIrqStatus, &status) == Result::Success &&
                (status & PpuVBlankStartBit) != 0u)
            {
                WriteRegister(PpuIrqStatus, PpuIrqAllStatusBits);
                frameCount = frameCount + 1u;
            }
        }

        Result WriteDisplayRegister(Address address, Word value)
        {
            const Result result = WriteRegister(address, value);

            return result == Result::Success ? Result::Success : Result::HardwareError;
        }

        Result WriteSequence(const RegisterValue* registers, size_t count)
        {
            for (size_t index = 0u; index < count; ++index)
            {
                const Result result = WriteDisplayRegister(registers[index].address,
                                                           registers[index].value);
                if (result != Result::Success)
                {
                    return result;
                }
            }
            return Result::Success;
        }

        Result WaitFrames(Word count)
        {
            const Platform* platform = GetPlatform();

            for (Word frame = 0u; frame < count; ++frame)
            {
                const Word startFrame = frameCount;
                uint32_t spin = 0u;

                if (platform->enableInterrupts != nullptr)
                {
                    platform->enableInterrupts(platform->context);
                }
                WriteRegister(PpuIrqControl, 1u);

                while (frameCount == startFrame)
                {
                    PollVBlank();
                    if (++spin >= VBlankWaitLimit)
                    {
                        return Result::HardwareError;
                    }
                }
            }
            return Result::Success;
        }

        Result ApplyMode(Mode mode)
        {
            const Word engine =
                mode == Mode::Qvga ? 0x1100u : (mode == Mode::Hvga ? 0x1109u : 0x1105u);
            const Word control =
                mode == Mode::Qvga ? 0x1000u : (mode == Mode::Hvga ? 0x1002u : 0x1001u);
            const Result result = WriteDisplayRegister(DisplayEngineControl, engine);

            if (result != Result::Success)
            {
                return result;
            }
            return WriteDisplayRegister(DisplayControl, control);
        }

        Result InitializeBackend()
        {
            const Platform* platform = GetPlatform();
            if (platform->delayIterations == nullptr)
            {
                return Result::Unsupported;
            }

            Result result = Interrupts::Install(PpuVBlankStartInterrupt, HandleVBlank);
            if (result != Result::Success)
            {
                return result;
            }
            result = WriteDisplayRegister(TvFade, 0xffu);
            if (result != Result::Success)
            {
                return result;
            }
            result = WriteSequence(PeripheralSetup, sizeof(PeripheralSetup) /
                                                        sizeof(PeripheralSetup[0]));
            if (result != Result::Success)
            {
                return result;
            }

            platform->delayIterations(platform->context, 200000u);
            result = WriteDisplayRegister(DisplayControl, 0u);
            if (result != Result::Success)
            {
                return result;
            }
            for (const Address buffer : OutputBuffers)
            {
                for (uint32_t word = 0u; word < OutputBufferWordsFor(activeMode);
                     ++word)
                {
                    result = WriteDisplayRegister(buffer + word * sizeof(Word), 0u);
                    if (result != Result::Success)
                    {
                        return result;
                    }
                }
            }

            result = WriteDisplayRegister(0x88070044u, 0xa0cf0000u);
            if (result != Result::Success)
            {
                return result;
            }
            result = WriteDisplayRegister(0x88070048u, 0xa0df0000u);
            if (result != Result::Success)
            {
                return result;
            }
            result = WriteDisplayRegister(0x88070064u, 0xa0ef0000u);
            if (result != Result::Success)
            {
                return result;
            }

            result = WriteSequence(SpriteQueueRegisters,
                                   sizeof(SpriteQueueRegisters) /
                                       sizeof(SpriteQueueRegisters[0]));
            if (result != Result::Success)
            {
                return result;
            }
            for (Address address = 0x88014000u; address < 0x88015000u;
                 address += sizeof(Word))
            {
                result = WriteDisplayRegister(address, 0u);
                if (result != Result::Success)
                {
                    return result;
                }
            }
            for (Address address = 0x88012000u; address < 0x88012800u;
                 address += sizeof(Word))
            {
                result = WriteDisplayRegister(address, 0u);
                if (result != Result::Success)
                {
                    return result;
                }
            }

            result = WriteSequence(BackendSetup,
                                   sizeof(BackendSetup) / sizeof(BackendSetup[0]));
            if (result != Result::Success)
            {
                return result;
            }
            result = ApplyMode(activeMode);
            if (result != Result::Success)
            {
                return result;
            }
            return WaitFrames(2u);
        }
    }

    Result Display::Initialize(const FrameBuffer& frameBuffer,
                               const Text1NumberArray& text1NumberArray)
    {
        if (!TDK::IsInitialized())
        {
            return Result::NotInitialized;
        }
        if (frameBuffer.pixels == nullptr || text1NumberArray.entries == nullptr ||
            frameBuffer.physicalAddress == 0u ||
            text1NumberArray.physicalAddress == 0u || frameBuffer.width == 0u ||
            frameBuffer.height == 0u ||
            frameBuffer.width > static_cast<uint32_t>(frameBuffer.strideWords) * 2u ||
            frameBuffer.strideWords == 0u || frameBuffer.height > 0x200u ||
            frameBuffer.strideWords > 0x200u ||
            frameBuffer.capacityWords <
                static_cast<size_t>(frameBuffer.strideWords) * frameBuffer.height ||
            text1NumberArray.capacityEntries < Text1NumberArrayEntries ||
            (frameBuffer.width & 1u) != 0u)
        {
            return Result::InvalidArgument;
        }

        initialized = false;
        activeMode = ModeForSize(frameBuffer.width, frameBuffer.height);
        const Result backendResult = InitializeBackend();
        if (backendResult != Result::Success)
        {
            return backendResult;
        }

        for (size_t index = 0u; index < Text1NumberArrayEntries; ++index)
        {
            text1NumberArray.entries[index] = static_cast<Word>(index << 10);
        }
        const size_t visibleWordsPerRow = frameBuffer.width / 2u;
        for (uint32_t row = 0u; row < frameBuffer.height; ++row)
        {
            const size_t rowStart = static_cast<size_t>(row) * frameBuffer.strideWords;
            for (size_t column = 0u; column < visibleWordsPerRow; ++column)
            {
                frameBuffer.pixels[rowStart + column] = 0u;
            }
        }

        const Address frameAddress = frameBuffer.physicalAddress;
        if (WriteDisplayRegister(Text1XPosition, 0u) != Result::Success ||
            WriteDisplayRegister(Text1YPosition, 0u) != Result::Success ||
            WriteDisplayRegister(Text1BitmapBuffer, frameAddress) != Result::Success ||
            WriteDisplayRegister(Text1NumberArrayPointer,
                                 text1NumberArray.physicalAddress) != Result::Success ||
            WriteDisplayRegister(Text1Attribute, 0u) != Result::Success ||
            WriteDisplayRegister(Text1Control, 0x100bu) != Result::Success ||
            WriteDisplayRegister(Text1BlendLevel, 0u) != Result::Success)
        {
            initialized = false;
            return Result::HardwareError;
        }

        if (WaitFrames(2u) != Result::Success)
        {
            initialized = false;
            return Result::HardwareError;
        }

        activeFrameBuffer = frameBuffer;
        shownFrameBuffer = frameBuffer;
        initialized = true;
        return Result::Success;
    }

    Result Display::SetFade(Byte level)
    {
        if (!initialized)
        {
            return Result::NotInitialized;
        }
        return WriteDisplayRegister(TvFade, level);
    }

    Result Display::Shutdown()
    {
        if (!initialized)
        {
            return Result::NotInitialized;
        }
        initialized = false;
        WriteDisplayRegister(PpuIrqControl, 0u);
        WriteDisplayRegister(PpuIrqStatus, PpuIrqAllStatusBits);
        WriteDisplayRegister(Text1Control, 0u);
        WriteDisplayRegister(DisplayControl, 0u);
        Interrupts::Uninstall(PpuVBlankStartInterrupt);
        return Result::Success;
    }

    Result Display::FadeIn()
    {
        if (!initialized)
        {
            return Result::NotInitialized;
        }
        Result result = WaitFrames(2u);
        if (result != Result::Success)
        {
            return result;
        }
        for (int level = 0xff; level > 0; level -= 0x10)
        {
            result = WriteDisplayRegister(TvFade, level);
            if (result != Result::Success)
            {
                return result;
            }
            result = WaitFrames(1u);
            if (result != Result::Success)
            {
                return result;
            }
        }
        return WriteDisplayRegister(TvFade, 0u);
    }

    Result Display::WaitForVBlank()
    {
        const Platform* platform = GetPlatform();

        if (!initialized)
        {
            return Result::NotInitialized;
        }
        if (platform->waitForVBlank != nullptr)
        {
            return platform->waitForVBlank(platform->context) ? Result::Success
                                                              : Result::HardwareError;
        }

        return WaitFrames(1u);
    }

    Word Display::GetFrameCount()
    {
        return frameCount;
    }

    namespace
    {
        bool MatchesShownGeometry(const FrameBuffer& frameBuffer)
        {
            return frameBuffer.pixels != nullptr && frameBuffer.physicalAddress != 0u &&
                   frameBuffer.width == shownFrameBuffer.width &&
                   frameBuffer.height == shownFrameBuffer.height &&
                   frameBuffer.strideWords == shownFrameBuffer.strideWords &&
                   frameBuffer.capacityWords >=
                       static_cast<size_t>(frameBuffer.strideWords) *
                           frameBuffer.height;
        }
    }

    Result Display::SetDrawBuffer(const FrameBuffer& frameBuffer)
    {
        if (!initialized)
        {
            return Result::NotInitialized;
        }
        if (!MatchesShownGeometry(frameBuffer))
        {
            return Result::InvalidArgument;
        }
        activeFrameBuffer = frameBuffer;
        return Result::Success;
    }

    Result Display::ShowBuffer(const FrameBuffer& frameBuffer)
    {
        if (!initialized)
        {
            return Result::NotInitialized;
        }
        if (!MatchesShownGeometry(frameBuffer))
        {
            return Result::InvalidArgument;
        }

        const Result waited = WaitFrames(1u);
        if (waited != Result::Success)
        {
            return waited;
        }
        const Result written =
            WriteDisplayRegister(Text1BitmapBuffer, frameBuffer.physicalAddress);
        if (written != Result::Success)
        {
            return written;
        }
        shownFrameBuffer = frameBuffer;
        return Result::Success;
    }

    bool Display::IsInitialized()
    {
        return initialized;
    }

    const FrameBuffer* Display::GetFrameBuffer()
    {
        return initialized ? &activeFrameBuffer : nullptr;
    }
}
