#pragma once

#include <tdk/Types.hpp>

namespace TDK
{
    struct GamepadState
    {
        bool valid;
        uint16_t buttons;
        uint8_t axisX;
        uint8_t axisY;
        uint8_t flags;
        uint32_t combined;
        uint8_t raw[5];
    };

    class Gamepad
    {
    public:
        static constexpr uint32_t MaxPorts = 4u;
        static constexpr uint32_t InterruptNumber = 39u;
        static constexpr uint32_t ButtonCount = 16u;
        static constexpr uint32_t AxisXLow = 0x20000u;
        static constexpr uint32_t AxisXHigh = 0x10000u;
        static constexpr uint32_t AxisYLow = 0x80000u;
        static constexpr uint32_t AxisYHigh = 0x40000u;
        static constexpr uint8_t AxisLowThreshold = 0x50u;
        static constexpr uint8_t AxisHighThreshold = 0xa0u;

        static Result Initialize();
        static Result SetClockDivider(uint32_t divider);
        static uint32_t GetClockDivider();
        static Result ReadPortRegister(uint32_t port, uint32_t index, Byte* value);
        static Result Read(uint32_t port, GamepadState* state);
        static Result StartRead(uint32_t port);
        static void Service();
        static bool IsReading();
        static bool TakeResult(GamepadState* state);

    private:
        static Result ReadOnce(uint32_t port, GamepadState* state);
    };
}
