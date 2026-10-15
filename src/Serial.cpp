#include <tdk/Serial.hpp>

#include <tdk/TDK.hpp>

namespace TDK
{
    namespace
    {
        constexpr Address InterfaceSelect = 0x88200000u;
        constexpr Address UartData = 0x88150000u;
        constexpr Address UartStatus = 0x88150010u;
        constexpr Word UartPortSelect = 1u << 24;
        constexpr Word TransmitEmpty = 1u << 7;
        constexpr Word Busy = 1u << 3;
        constexpr Word TransmitFull = 1u << 5;
        constexpr Word ReceiveEmpty = 1u << 4;
        constexpr Word PollLimit = 0x100000u;

        Result WaitForTransmitReady()
        {
            for (Word attempt = 0u; attempt < PollLimit; ++attempt)
            {
                Word status;

                if (ReadRegister(UartStatus, &status) != Result::Success)
                {
                    return Result::HardwareError;
                }
                if ((status & (TransmitEmpty | Busy | TransmitFull)) == TransmitEmpty)
                {
                    return Result::Success;
                }
            }
            return Result::HardwareError;
        }
    }

    Result Serial::Initialize()
    {
        Word selection;
        Result result = ReadRegister(InterfaceSelect, &selection);

        if (result != Result::Success)
        {
            return result;
        }
        selection |= UartPortSelect;
        return WriteRegister(InterfaceSelect, selection);
    }

    Result Serial::WriteByte(Byte value)
    {
        const Result result = WaitForTransmitReady();

        if (result != Result::Success)
        {
            return result;
        }
        return WriteRegister(UartData, value);
    }

    Result Serial::Write(const char* text)
    {
        if (text == nullptr)
        {
            return Result::InvalidArgument;
        }
        while (*text != '\0')
        {
            const Result result = WriteByte(static_cast<Byte>(*text));

            if (result != Result::Success)
            {
                return result;
            }
            ++text;
        }
        return Result::Success;
    }

    Result Serial::ReadByte(Byte* value)
    {
        if (value == nullptr)
        {
            return Result::InvalidArgument;
        }
        for (Word attempt = 0u; attempt < PollLimit; ++attempt)
        {
            Word status;
            Result result = ReadRegister(UartStatus, &status);

            if (result != Result::Success)
            {
                return result;
            }
            if ((status & ReceiveEmpty) == 0u)
            {
                Word data;

                result = ReadRegister(UartData, &data);
                if (result != Result::Success)
                {
                    return result;
                }
                *value = static_cast<Byte>(data);
                return Result::Success;
            }
        }
        return Result::HardwareError;
    }
}
