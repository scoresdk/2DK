#include <tdk/Layers.hpp>

#include <tdk/Platform.hpp>
#include <tdk/TDK.hpp>

namespace TDK
{
    namespace
    {
        struct LayerRegisters
        {
            Address x;
            Address y;
            Address attribute;
            Address control;
            Address numberPointer;
            Address blendLevel;
            Address buffer;
        };

        constexpr LayerRegisters Registers[Layers::Count] = {
            {0x88010020u, 0x88010024u, 0x88010028u, 0x8801002cu, 0x88010030u,
             0x88010038u, 0x880100a0u},
            {0x8801003cu, 0x88010040u, 0x88010044u, 0x88010048u, 0x8801004cu,
             0x88010054u, 0x880100acu},
            {0x88010058u, 0x8801005cu, 0x88010060u, 0x88010064u, 0x88010068u,
             0x88010070u, 0x880100b8u}};

        constexpr Word ControlBitmap = 0x0001u;
        constexpr Word ControlRegisterMode = 0x0002u;
        constexpr Word ControlEnable = 0x0008u;
        constexpr Word ControlBlend = 0x0100u;
        constexpr Word ControlRgb565 = 0x1000u;
        constexpr Word ControlBase =
            ControlBitmap | ControlRegisterMode | ControlRgb565;
        constexpr Word DepthShift = 13u;
        constexpr size_t NumberArrayEntries = 0x200u;
        constexpr Word ScrollMask = 0xffffu;

        struct LayerState
        {
            LayerSetup setup;
            uint32_t shownIndex;
            bool configured;
            bool visible;
            bool blend;
            Byte depth;
        };

        LayerState layers[Layers::Count] = {};

        bool ValidBuffer(const FrameBuffer& buffer)
        {
            return buffer.pixels != nullptr && buffer.physicalAddress != 0u &&
                   buffer.width != 0u && buffer.height != 0u &&
                   (buffer.width & 1u) == 0u && buffer.strideWords != 0u &&
                   buffer.strideWords <= 0x200u && buffer.height <= 0x200u &&
                   buffer.width <= static_cast<uint32_t>(buffer.strideWords) * 2u &&
                   buffer.capacityWords >=
                       static_cast<size_t>(buffer.strideWords) * buffer.height;
        }

        bool SameGeometry(const FrameBuffer& a, const FrameBuffer& b)
        {
            return a.width == b.width && a.height == b.height &&
                   a.strideWords == b.strideWords;
        }

        Result Write(Address address, Word value)
        {
            return WriteRegister(address, value) == Result::Success
                       ? Result::Success
                       : Result::HardwareError;
        }

        Word ControlFor(const LayerState& state)
        {
            return ControlBase | (state.visible ? ControlEnable : 0u) |
                   (state.blend ? ControlBlend : 0u);
        }

        Result WriteControl(Layer layer)
        {
            const uint32_t index = static_cast<uint32_t>(layer);

            return Write(Registers[index].control, ControlFor(layers[index]));
        }

        void ClearBuffer(const FrameBuffer& buffer)
        {
            const size_t wordsPerRow = buffer.width / 2u;

            for (uint32_t row = 0u; row < buffer.height; ++row)
            {
                const size_t start = static_cast<size_t>(row) * buffer.strideWords;

                for (size_t word = 0u; word < wordsPerRow; ++word)
                {
                    buffer.pixels[start + word] = 0u;
                }
            }
        }

        Result Check(Layer layer)
        {
            const uint32_t index = static_cast<uint32_t>(layer);

            if (index >= Layers::Count)
            {
                return Result::InvalidArgument;
            }
            return layers[index].configured ? Result::Success : Result::NotInitialized;
        }
    }

    Result Layers::Configure(Layer layer, const LayerSetup& setup)
    {
        const uint32_t index = static_cast<uint32_t>(layer);

        if (!Display::IsInitialized())
        {
            return Result::NotInitialized;
        }
        if (index >= Count || setup.depth > MaxDepth ||
            !ValidBuffer(setup.buffers[0]) || !ValidBuffer(setup.buffers[1]) ||
            !SameGeometry(setup.buffers[0], setup.buffers[1]) ||
            setup.numberArray.entries == nullptr ||
            setup.numberArray.physicalAddress == 0u ||
            setup.numberArray.capacityEntries < NumberArrayEntries)
        {
            return Result::InvalidArgument;
        }
        const FrameBuffer* shown = Display::GetFrameBuffer();

        if (shown == nullptr || !SameGeometry(*shown, setup.buffers[0]))
        {
            return Result::InvalidArgument;
        }

        for (size_t entry = 0u; entry < NumberArrayEntries; ++entry)
        {
            setup.numberArray.entries[entry] = static_cast<Word>(entry << 10);
        }
        ClearBuffer(setup.buffers[0]);
        ClearBuffer(setup.buffers[1]);

        LayerState& state = layers[index];

        state.setup = setup;
        state.shownIndex = 0u;
        state.visible = setup.visible;
        state.blend = false;
        state.depth = setup.depth;
        state.configured = false;

        const LayerRegisters& registers = Registers[index];

        if (Write(registers.x, 0u) != Result::Success ||
            Write(registers.y, 0u) != Result::Success ||
            Write(registers.buffer, setup.buffers[0].physicalAddress) !=
                Result::Success ||
            Write(registers.numberPointer, setup.numberArray.physicalAddress) !=
                Result::Success ||
            Write(registers.attribute, static_cast<Word>(setup.depth) << DepthShift) !=
                Result::Success ||
            Write(registers.blendLevel, 0u) != Result::Success ||
            Write(registers.control, ControlFor(state)) != Result::Success)
        {
            return Result::HardwareError;
        }
        state.configured = true;
        return Result::Success;
    }

    Result Layers::SetVisible(Layer layer, bool visible)
    {
        const Result checked = Check(layer);

        if (checked != Result::Success)
        {
            return checked;
        }
        layers[static_cast<uint32_t>(layer)].visible = visible;
        return WriteControl(layer);
    }

    Result Layers::SetDepth(Layer layer, Byte depth)
    {
        const Result checked = Check(layer);

        if (checked != Result::Success)
        {
            return checked;
        }
        if (depth > MaxDepth)
        {
            return Result::InvalidArgument;
        }
        const uint32_t index = static_cast<uint32_t>(layer);

        layers[index].depth = depth;
        return Write(Registers[index].attribute, static_cast<Word>(depth)
                                                     << DepthShift);
    }

    Result Layers::SetBlend(Layer layer, bool enabled, Byte level)
    {
        const Result checked = Check(layer);

        if (checked != Result::Success)
        {
            return checked;
        }
        if (level > MaxBlendLevel)
        {
            return Result::InvalidArgument;
        }
        const uint32_t index = static_cast<uint32_t>(layer);
        const bool changed = layers[index].blend != enabled;

        layers[index].blend = enabled;
        if (Write(Registers[index].blendLevel, level) != Result::Success)
        {
            return Result::HardwareError;
        }
        return changed ? WriteControl(layer) : Result::Success;
    }

    Result Layers::SetScroll(Layer layer, int32_t x, int32_t y)
    {
        const Result checked = Check(layer);

        if (checked != Result::Success)
        {
            return checked;
        }
        const uint32_t index = static_cast<uint32_t>(layer);

        if (Write(Registers[index].x, static_cast<Word>(x) & ScrollMask) !=
                Result::Success ||
            Write(Registers[index].y, static_cast<Word>(y) & ScrollMask) !=
                Result::Success)
        {
            return Result::HardwareError;
        }
        return Result::Success;
    }

    Result Layers::GetBackBuffer(Layer layer, FrameBuffer* frameBuffer)
    {
        const Result checked = Check(layer);

        if (checked != Result::Success)
        {
            return checked;
        }
        if (frameBuffer == nullptr)
        {
            return Result::InvalidArgument;
        }
        const LayerState& state = layers[static_cast<uint32_t>(layer)];

        *frameBuffer = state.setup.buffers[state.shownIndex ^ 1u];
        return Result::Success;
    }

    Result Layers::GetShownBuffer(Layer layer, FrameBuffer* frameBuffer)
    {
        const Result checked = Check(layer);

        if (checked != Result::Success)
        {
            return checked;
        }
        if (frameBuffer == nullptr)
        {
            return Result::InvalidArgument;
        }
        const LayerState& state = layers[static_cast<uint32_t>(layer)];

        *frameBuffer = state.setup.buffers[state.shownIndex];
        return Result::Success;
    }

    uint32_t Layers::BackIndex(Layer layer)
    {
        const uint32_t index = static_cast<uint32_t>(layer);

        return index < Count ? layers[index].shownIndex ^ 1u : 0u;
    }

    Result Layers::BeginDraw(Layer layer)
    {
        FrameBuffer back = {};
        const Result result = GetBackBuffer(layer, &back);

        if (result != Result::Success)
        {
            return result;
        }
        return Display::SetDrawBuffer(back);
    }

    Result Layers::Flip(uint32_t layerMask)
    {
        if ((layerMask & ~AllMask) != 0u)
        {
            return Result::InvalidArgument;
        }
        for (uint32_t index = 0u; index < Count; ++index)
        {
            if ((layerMask & (1u << index)) != 0u && !layers[index].configured)
            {
                return Result::NotInitialized;
            }
        }
        if (layerMask == 0u)
        {
            return Result::Success;
        }

        const Result waited = Display::WaitForVBlank();

        if (waited != Result::Success)
        {
            return waited;
        }
        for (uint32_t index = 0u; index < Count; ++index)
        {
            if ((layerMask & (1u << index)) == 0u)
            {
                continue;
            }
            LayerState& state = layers[index];
            const uint32_t next = state.shownIndex ^ 1u;

            if (Write(Registers[index].buffer,
                      state.setup.buffers[next].physicalAddress) != Result::Success)
            {
                return Result::HardwareError;
            }
            state.shownIndex = next;
        }
        return Result::Success;
    }

    bool Layers::IsConfigured(Layer layer)
    {
        return Check(layer) == Result::Success;
    }
}
