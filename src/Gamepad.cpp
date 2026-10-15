#include <tdk/Gamepad.hpp>

#include <tdk/Interrupts.hpp>
#include <tdk/Platform.hpp>
#include <tdk/TDK.hpp>

namespace TDK
{
    namespace
    {
        constexpr Address I2cControl = 0x88130020u;
        constexpr Address I2cInterrupt = 0x88130024u;
        constexpr Address I2cClock = 0x88130028u;
        constexpr Address I2cId = 0x8813002cu;
        constexpr Address I2cAddress = 0x88130030u;
        constexpr Address I2cReadData = 0x88130038u;
        constexpr Address I2cClockConfig = 0x88210094u;
        constexpr Address FunctionSelect = 0x88200004u;
        constexpr Address DeviceSelect = 0x88200000u;
        constexpr Address PortData = 0x88200014u;
        constexpr Address PortOutputEnable = 0x88200018u;
        constexpr Address PortPullUp = 0x8820001cu;
        constexpr Address PortPullDown = 0x88200020u;
        constexpr Word UartSelect = 0x01000000u;
        constexpr Word PortPins = 0xfffffu;

        constexpr Word ClockEnable = 3u;
        constexpr Word I2cSelect = 1u;
        constexpr Word ReadStart = 0x41u;
        constexpr Word InterruptEnable = 2u;
        constexpr Word InterruptFlag = 1u;
        constexpr Word DefaultClockDivider = 600u;
        Word clockDivider = DefaultClockDivider;
        constexpr Word SlaveId = 0xa8u;
        constexpr uint32_t WaitLimit = 400000u;
        constexpr uint32_t RegisterCount = 5u;

        bool clearDeviceSelect = false;

        Result SelectPort(uint32_t port, bool selected, Word* savedDevice)
        {
            Word data = 0u;

            if (clearDeviceSelect && selected)
            {
                if (TDK::ReadRegister(DeviceSelect, savedDevice) != Result::Success ||
                    WriteRegister(DeviceSelect, *savedDevice & UartSelect) !=
                        Result::Success)
                {
                    return Result::HardwareError;
                }
            }
            if (WriteRegister(PortPullUp, PortPins) != Result::Success ||
                WriteRegister(PortPullDown, 0u) != Result::Success ||
                WriteRegister(PortOutputEnable, PortPins) != Result::Success ||
                TDK::ReadRegister(PortData, &data) != Result::Success)
            {
                return Result::HardwareError;
            }
            data = selected ? (data | (1u << port)) : (data & ~(1u << port));
            if (WriteRegister(PortData, data) != Result::Success)
            {
                return Result::HardwareError;
            }
            if (clearDeviceSelect && !selected)
            {
                return WriteRegister(DeviceSelect, *savedDevice);
            }
            return Result::Success;
        }

        constexpr uint32_t StallLimit = 6000u;
        constexpr uint32_t InvalidStreakLimit = 3u;

        struct AsyncRead
        {
            volatile bool active;
            volatile bool completed;
            uint32_t port;
            volatile uint32_t index;
            uint32_t stalled;
            uint32_t invalidStreak;
            Word savedDevice;
            GamepadState state;
        };

        volatile bool transferDone = false;
        volatile Word transferValue = 0u;
        bool initialized = false;
        AsyncRead asyncRead = {};

        void DecodeState(GamepadState* state)
        {
            const uint32_t sum = (static_cast<uint32_t>(state->raw[0]) + state->raw[1] +
                                  state->raw[2] + state->raw[3]) *
                                 4u;

            state->flags = state->raw[4] & 3u;
            state->valid = (state->raw[4] & 0xfcu) == (sum & 0xfcu);
            state->axisX = 0x7fu;
            state->axisY = 0x7fu;
            state->buttons = 0u;
            state->combined = 0u;
            if (!state->valid)
            {
                return;
            }

            state->buttons =
                static_cast<uint16_t>(state->raw[0] | (state->raw[1] << 8));
            state->axisX = state->raw[3];
            state->axisY = state->raw[2];
            state->combined = state->buttons;
            if (state->axisY < Gamepad::AxisLowThreshold)
            {
                state->combined |= Gamepad::AxisYLow;
            }
            else if (state->axisY > Gamepad::AxisHighThreshold)
            {
                state->combined |= Gamepad::AxisYHigh;
            }
            if (state->axisX < Gamepad::AxisLowThreshold)
            {
                state->combined |= Gamepad::AxisXLow;
            }
            else if (state->axisX > Gamepad::AxisHighThreshold)
            {
                state->combined |= Gamepad::AxisXHigh;
            }
        }

        Result BeginTransfer(uint32_t port, uint32_t index)
        {
            if (WriteRegister(I2cControl, ReadStart) != Result::Success ||
                WriteRegister(I2cInterrupt, InterruptEnable) != Result::Success ||
                WriteRegister(I2cAddress, (port << 4) | index) != Result::Success)
            {
                return Result::HardwareError;
            }
            return Result::Success;
        }

        void FinishAsync()
        {
            SelectPort(asyncRead.port, false, &asyncRead.savedDevice);
            DecodeState(&asyncRead.state);
            if (asyncRead.state.valid)
            {
                asyncRead.invalidStreak = 0u;
            }
            else if (++asyncRead.invalidStreak >= InvalidStreakLimit)
            {
                clearDeviceSelect = !clearDeviceSelect;
                asyncRead.invalidStreak = 0u;
            }
            asyncRead.completed = true;
            asyncRead.active = false;
        }

        void AdvanceAsync(Byte value)
        {
            asyncRead.state.raw[asyncRead.index] = value;
            asyncRead.stalled = 0u;
            transferDone = false;
            if (asyncRead.index + 1u < RegisterCount)
            {
                asyncRead.index = asyncRead.index + 1u;
                if (BeginTransfer(asyncRead.port, asyncRead.index) != Result::Success)
                {
                    FinishAsync();
                }
                return;
            }
            FinishAsync();
        }

        void HandleI2c(uint32_t, void*)
        {
            Word value = 0u;

            WriteRegister(I2cInterrupt, InterruptFlag);
            ReadRegister(I2cReadData, &value);
            transferValue = value & 0xffu;
            transferDone = true;
            if (asyncRead.active)
            {
                AdvanceAsync(static_cast<Byte>(value));
            }
        }
    }

    Result Gamepad::Initialize()
    {
        Word select = 0u;

        if (!IsInitialized())
        {
            return Result::NotInitialized;
        }
        if (Interrupts::Install(InterruptNumber, HandleI2c) != Result::Success ||
            WriteRegister(I2cClockConfig, ClockEnable) != Result::Success ||
            TDK::ReadRegister(FunctionSelect, &select) != Result::Success ||
            WriteRegister(FunctionSelect, select | I2cSelect) != Result::Success ||
            WriteRegister(I2cClock, clockDivider) != Result::Success ||
            WriteRegister(I2cId, SlaveId) != Result::Success ||
            WriteRegister(I2cInterrupt, InterruptFlag) != Result::Success)
        {
            return Result::HardwareError;
        }
        initialized = true;
        return Result::Success;
    }

    Result Gamepad::SetClockDivider(uint32_t divider)
    {
        if (divider == 0u || divider > 1023u)
        {
            return Result::InvalidArgument;
        }
        if (WriteRegister(I2cClock, divider) != Result::Success)
        {
            return Result::HardwareError;
        }
        clockDivider = divider;
        return Result::Success;
    }

    uint32_t Gamepad::GetClockDivider()
    {
        return clockDivider;
    }

    Result Gamepad::ReadPortRegister(uint32_t port, uint32_t index, Byte* value)
    {
        if (!initialized)
        {
            return Result::NotInitialized;
        }
        if (port >= MaxPorts || index > 0xfu || value == nullptr)
        {
            return Result::InvalidArgument;
        }
        if (asyncRead.active)
        {
            return Result::HardwareError;
        }

        transferDone = false;
        if (WriteRegister(I2cControl, ReadStart) != Result::Success ||
            WriteRegister(I2cInterrupt, InterruptEnable) != Result::Success ||
            WriteRegister(I2cAddress, (port << 4) | index) != Result::Success)
        {
            return Result::HardwareError;
        }

        for (uint32_t spin = 0u; spin < WaitLimit && !transferDone; ++spin)
        {
            Word status = 0u;

            if (TDK::ReadRegister(I2cInterrupt, &status) == Result::Success &&
                (status & InterruptFlag) != 0u && !transferDone)
            {
                HandleI2c(0u, nullptr);
            }
        }
        if (!transferDone)
        {
            WriteRegister(I2cInterrupt, InterruptFlag);
            return Result::HardwareError;
        }
        *value = static_cast<Byte>(transferValue);
        return Result::Success;
    }

    Result Gamepad::Read(uint32_t port, GamepadState* state)
    {
        const Result result = ReadOnce(port, state);

        if (result != Result::Success || state->valid || clearDeviceSelect)
        {
            return result;
        }
        clearDeviceSelect = true;
        const Result retry = ReadOnce(port, state);

        if (retry != Result::Success || !state->valid)
        {
            clearDeviceSelect = false;
        }
        return retry;
    }

    Result Gamepad::ReadOnce(uint32_t port, GamepadState* state)
    {
        if (state == nullptr)
        {
            return Result::InvalidArgument;
        }
        *state = {};
        state->axisX = 0x7fu;
        state->axisY = 0x7fu;
        if (asyncRead.active)
        {
            return Result::HardwareError;
        }

        Word savedDevice = 0u;
        Result result = SelectPort(port, true, &savedDevice);

        for (uint32_t index = 0u; result == Result::Success && index < RegisterCount;
             ++index)
        {
            result = ReadPortRegister(port, index, &state->raw[index]);
        }
        const Result release = SelectPort(port, false, &savedDevice);

        if (result != Result::Success)
        {
            return result;
        }
        if (release != Result::Success)
        {
            return release;
        }

        DecodeState(state);
        return Result::Success;
    }

    Result Gamepad::StartRead(uint32_t port)
    {
        if (!initialized)
        {
            return Result::NotInitialized;
        }
        if (port >= MaxPorts)
        {
            return Result::InvalidArgument;
        }
        if (asyncRead.active)
        {
            return Result::Success;
        }

        asyncRead.state = {};
        asyncRead.port = port;
        asyncRead.index = 0u;
        asyncRead.stalled = 0u;
        transferDone = false;
        if (SelectPort(port, true, &asyncRead.savedDevice) != Result::Success)
        {
            return Result::HardwareError;
        }
        asyncRead.active = true;
        if (BeginTransfer(port, 0u) != Result::Success)
        {
            asyncRead.active = false;
            SelectPort(port, false, &asyncRead.savedDevice);
            return Result::HardwareError;
        }
        return Result::Success;
    }

    void Gamepad::Service()
    {
        Word status = 0u;

        if (!asyncRead.active)
        {
            return;
        }
        Interrupts::Disable();
        if (asyncRead.active)
        {
            if (TDK::ReadRegister(I2cInterrupt, &status) == Result::Success &&
                (status & InterruptFlag) != 0u)
            {
                HandleI2c(0u, nullptr);
            }
            else if (++asyncRead.stalled >= StallLimit)
            {
                WriteRegister(I2cInterrupt, InterruptFlag);
                SelectPort(asyncRead.port, false, &asyncRead.savedDevice);
                asyncRead.active = false;
            }
        }
        Interrupts::Enable();
    }

    bool Gamepad::IsReading()
    {
        return asyncRead.active;
    }

    bool Gamepad::TakeResult(GamepadState* state)
    {
        bool fresh = false;

        if (state == nullptr || !asyncRead.completed)
        {
            return false;
        }
        Interrupts::Disable();
        if (asyncRead.completed)
        {
            *state = asyncRead.state;
            asyncRead.completed = false;
            fresh = true;
        }
        Interrupts::Enable();
        return fresh;
    }
}
