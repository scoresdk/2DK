#include <tdk/Controller.hpp>

namespace TDK
{
    namespace
    {
        constexpr Word DelayPulse = 2u;
        constexpr Word DelayLong = 0x4bu;
        constexpr Word DelayOne = 0x14u;
        constexpr Word DelayZero = 6u;

        bool ValidBus(const ControllerBus& bus)
        {
            return bus.setLine != nullptr && bus.readLine != nullptr &&
                   bus.delay != nullptr;
        }

        bool ValidOutputBus(const ControllerBus& bus)
        {
            return bus.setLine != nullptr && bus.delay != nullptr;
        }

        ControllerResult ExchangeFrame(const ControllerBus& bus, ControllerState* state,
                                       const Byte* frame, size_t frameSize,
                                       Word waitIterations, Word waitQuantum,
                                       Byte* response, Byte responseSize)
        {
            if (!ValidBus(bus) || state == nullptr || frame == nullptr ||
                frameSize == 0u || frameSize > 9u ||
                (responseSize != 0u && response == nullptr))
            {
                return ControllerResult::InvalidArgument;
            }
            ControllerResult result = Controller::SendCommand(bus, frame[0]);

            if (result != ControllerResult::Success)
            {
                return result;
            }
            for (size_t index = 1u; index < frameSize; ++index)
            {
                result = Controller::SendByte(bus, frame[index]);
                if (result != ControllerResult::Success)
                {
                    return result;
                }
            }
            for (Word wait = 0u; wait < waitIterations; ++wait)
            {
                bus.delay(bus.context, waitQuantum);
            }
            if (responseSize == 0u)
            {
                return ControllerResult::Success;
            }
            return Controller::ReadBytes(bus, state, 0u, responseSize, response,
                                         responseSize);
        }
    }

    ControllerResult Controller::ClockPulse(const ControllerBus& bus)
    {
        if (!ValidOutputBus(bus))
        {
            return ControllerResult::InvalidArgument;
        }
        if (bus.setLine(bus.context, 0u) != 0)
        {
            return ControllerResult::HardwareError;
        }
        bus.delay(bus.context, DelayPulse);
        return bus.setLine(bus.context, 1u) == 0 ? ControllerResult::Success
                                                 : ControllerResult::HardwareError;
    }

    ControllerResult Controller::SendByte(const ControllerBus& bus, Byte value)
    {
        if (!ValidOutputBus(bus))
        {
            return ControllerResult::InvalidArgument;
        }
        ControllerResult result = ClockPulse(bus);

        if (result != ControllerResult::Success)
        {
            return result;
        }
        for (unsigned int bit = 0u; bit < 8u; ++bit)
        {
            bus.delay(bus.context, (value & 1u) != 0u ? DelayOne : DelayZero);
            result = ClockPulse(bus);
            if (result != ControllerResult::Success)
            {
                return result;
            }
            value >>= 1;
        }
        bus.delay(bus.context, DelayLong);
        return ControllerResult::Success;
    }

    ControllerResult Controller::SendCommand(const ControllerBus& bus, Byte value)
    {
        if (!ValidOutputBus(bus))
        {
            return ControllerResult::InvalidArgument;
        }
        ControllerResult result = ClockPulse(bus);

        if (result != ControllerResult::Success)
        {
            return result;
        }
        bus.delay(bus.context, 0x28u);
        result = ClockPulse(bus);
        if (result != ControllerResult::Success)
        {
            return result;
        }
        for (unsigned int bit = 0u; bit < 7u; ++bit)
        {
            bus.delay(bus.context, (value & 1u) != 0u ? DelayOne : DelayZero);
            result = ClockPulse(bus);
            if (result != ControllerResult::Success)
            {
                return result;
            }
            value >>= 1;
        }
        bus.delay(bus.context, DelayLong);
        return ControllerResult::Success;
    }

    ControllerResult Controller::ReadBit(const ControllerBus& bus, Byte* value)
    {
        Word sampled;

        if (!ValidBus(bus) || value == nullptr)
        {
            return ControllerResult::InvalidArgument;
        }
        if (ClockPulse(bus) != ControllerResult::Success)
        {
            return ControllerResult::HardwareError;
        }
        bus.delay(bus.context, 0x3cu);
        if (bus.readLine(bus.context, &sampled) != 0)
        {
            return ControllerResult::HardwareError;
        }
        bus.delay(bus.context, 10u);
        *value = static_cast<Byte>(sampled != 0u);
        return ControllerResult::Success;
    }

    ControllerResult Controller::Synchronize(const ControllerBus& bus,
                                             ControllerState* state)
    {
        Byte bit;
        Byte window = 0u;

        if (!ValidBus(bus) || state == nullptr)
        {
            return ControllerResult::InvalidArgument;
        }
        for (unsigned int index = 0u; index < 0x18u; ++index)
        {
            if (ReadBit(bus, &bit) != ControllerResult::Success)
            {
                return ControllerResult::HardwareError;
            }
        }
        for (unsigned int index = 0u; index < 16u; ++index)
        {
            window <<= 1;
            if (ReadBit(bus, &bit) != ControllerResult::Success)
            {
                return ControllerResult::HardwareError;
            }
            window |= bit;
            if (window == 0x56u)
            {
                state->invertedPolarity = 0u;
                return ControllerResult::Success;
            }
            if (window == 0xa9u)
            {
                state->invertedPolarity = 1u;
                return ControllerResult::Success;
            }
        }
        return ControllerResult::SynchronizationError;
    }

    ControllerResult Controller::DecodeByte(const ControllerBus& bus,
                                            ControllerState* state, Byte index,
                                            Byte* value)
    {
        Byte decoded = 0u;
        Byte oneCount = 0u;
        bool valid = true;

        if (!ValidBus(bus) || state == nullptr || value == nullptr)
        {
            return ControllerResult::InvalidArgument;
        }
        for (unsigned int bit = 0u; bit < 8u; ++bit)
        {
            Byte first;
            Byte second;

            if (ReadBit(bus, &first) != ControllerResult::Success ||
                ReadBit(bus, &second) != ControllerResult::Success)
            {
                return ControllerResult::HardwareError;
            }
            decoded >>= 1;
            if (first == 1u && second == 0u)
            {
                decoded |= 0x80u;
                ++oneCount;
            }
            else if (first != 0u || second != 1u)
            {
                valid = false;
            }
        }

        Byte parityA;
        Byte parityB;
        if (ReadBit(bus, &parityA) != ControllerResult::Success ||
            ReadBit(bus, &parityB) != ControllerResult::Success)
        {
            return ControllerResult::HardwareError;
        }
        if (parityA == parityB)
        {
            valid = false;
        }
        if (state->invertedPolarity != 0u)
        {
            decoded = static_cast<Byte>(~decoded);
            oneCount = static_cast<Byte>(8u - oneCount);
            parityA = static_cast<Byte>(parityA == 0u);
        }
        state->receivedBytes[index] = decoded;
        *value = decoded;
        return valid && ((oneCount & 1u) != parityA) ? ControllerResult::Success
                                                     : ControllerResult::DataError;
    }

    ControllerResult Controller::ReadBytes(const ControllerBus& bus,
                                           ControllerState* state, Byte startIndex,
                                           Byte count, Byte* destination,
                                           size_t capacity)
    {
        if (!ValidBus(bus) || state == nullptr || destination == nullptr ||
            count > capacity)
        {
            return ControllerResult::InvalidArgument;
        }
        ControllerResult result = Synchronize(bus, state);

        if (result != ControllerResult::Success)
        {
            return result;
        }
        for (unsigned int index = 0u; index < count; ++index)
        {
            Byte value;
            result =
                DecodeByte(bus, state, static_cast<Byte>(startIndex + index), &value);
            if (result != ControllerResult::Success)
            {
                return result;
            }
            destination[index] = value;
        }
        return ControllerResult::Success;
    }

    ControllerResult Controller::ReadDeviceBytes(const ControllerBus& bus,
                                                 Byte startAddress, Byte count,
                                                 Byte* destination, size_t capacity)
    {
        if (bus.readByte == nullptr || destination == nullptr || count > capacity)
        {
            return ControllerResult::InvalidArgument;
        }
        for (unsigned int index = 0u; index < count; ++index)
        {
            unsigned int attempt;
            const Byte address = static_cast<Byte>(startAddress + index);

            for (attempt = 0u; attempt < 16u; ++attempt)
            {
                if (bus.readByte(bus.context, address, &destination[index]) != 0)
                {
                    break;
                }
            }
            if (attempt == 16u)
            {
                return ControllerResult::DataError;
            }
        }
        return ControllerResult::Success;
    }

    ControllerResult Controller::CheckPresence(const ControllerBus& bus,
                                               ControllerState* state,
                                               Word responseWaitQuantum, bool* present)
    {
        const Byte command = 0x26u;
        Byte response[2];

        if (present == nullptr)
        {
            return ControllerResult::InvalidArgument;
        }
        *present = false;
        const ControllerResult result = ExchangeFrame(
            bus, state, &command, 1u, 2000u, responseWaitQuantum, response, 2u);

        if (result != ControllerResult::Success)
        {
            return result;
        }
        *present = response[0] == 0u && response[1] == 0u;
        return ControllerResult::Success;
    }

    ControllerResult Controller::QueryIdentity(const ControllerBus& bus,
                                               ControllerState* state,
                                               Word responseWaitQuantum,
                                               Byte identity[4], size_t capacity)
    {
        Byte frame[9] = {0x78u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
        Byte response[8];

        if (identity == nullptr || capacity < 4u)
        {
            return ControllerResult::InvalidArgument;
        }
        const HalfWord crc = CRC16(frame, 7u, 7u);
        frame[7] = static_cast<Byte>(crc);
        frame[8] = static_cast<Byte>(crc >> 8);
        const ControllerResult result =
            ExchangeFrame(bus, state, frame, sizeof(frame), 2000u, responseWaitQuantum,
                          response, sizeof(response));

        if (result != ControllerResult::Success)
        {
            return result;
        }
        const HalfWord responseCRC = CRC16(response, 6u, 8u);
        if (!CRCMatches(response, 6u, responseCRC))
        {
            return ControllerResult::DataError;
        }
        for (unsigned int index = 0u; index < 4u; ++index)
        {
            identity[index] = response[index + 2u];
        }
        return ControllerResult::Success;
    }

    bool Controller::WriteAndVerifyBytes(const ControllerBus& bus, Byte startAddress,
                                         const Byte* source, Byte count)
    {
        if (count == 0u)
        {
            return true;
        }
        if (bus.writeByte == nullptr || source == nullptr)
        {
            return false;
        }
        for (unsigned int index = 0u; index < count; ++index)
        {
            unsigned int attempt;
            const Byte address = static_cast<Byte>(startAddress + index);

            for (attempt = 0u; attempt < 16u; ++attempt)
            {
                if (bus.writeByte(bus.context, address, source[index]) != 0)
                {
                    break;
                }
            }
            if (attempt == 16u)
            {
                return false;
            }
        }
        return true;
    }

    ControllerResult Controller::WriteParameterAndConfirm(
        const ControllerBus& bus, ControllerState* state, Byte parameter,
        const Byte context[4], Word responseWaitQuantum, Byte* acceptedParameter)
    {
        Byte frame[9];
        Byte response[4];

        if (context == nullptr || acceptedParameter == nullptr)
        {
            return ControllerResult::InvalidArgument;
        }
        frame[0] = 1u;
        frame[1] = parameter;
        frame[2] = 0u;
        for (unsigned int index = 0u; index < 4u; ++index)
        {
            frame[index + 3u] = context[index];
        }
        const HalfWord crc = CRC16(frame, 7u, 7u);
        frame[7] = static_cast<Byte>(crc);
        frame[8] = static_cast<Byte>(crc >> 8);
        const ControllerResult result =
            ExchangeFrame(bus, state, frame, sizeof(frame), 2000u, responseWaitQuantum,
                          response, sizeof(response));

        if (result != ControllerResult::Success)
        {
            return result;
        }
        const HalfWord responseCRC = CRC16(response, 2u, 8u);
        if (!CRCMatches(response, 2u, responseCRC))
        {
            return ControllerResult::DataError;
        }
        *acceptedParameter = parameter;
        return ControllerResult::Success;
    }

    ControllerResult Controller::WriteRegisterAndConfirm(const ControllerBus& bus,
                                                         ControllerState* state,
                                                         Byte address, Byte value,
                                                         const Byte context[4],
                                                         Word responseWaitQuantum)
    {
        Byte frame[9];
        Byte response[4];

        if (!ValidBus(bus) || state == nullptr || context == nullptr)
        {
            return ControllerResult::InvalidArgument;
        }
        frame[0] = 0x53u;
        frame[1] = address;
        frame[2] = value;
        for (unsigned int index = 0u; index < 4u; ++index)
        {
            frame[index + 3u] = context[index];
        }
        const HalfWord frameCRC = CRC16(frame, 7u, 7u);
        frame[7] = static_cast<Byte>(frameCRC);
        frame[8] = static_cast<Byte>(frameCRC >> 8);
        const ControllerResult result =
            ExchangeFrame(bus, state, frame, sizeof(frame), 0x1450u,
                          responseWaitQuantum, response, sizeof(response));

        if (result != ControllerResult::Success)
        {
            return result;
        }
        const HalfWord responseCRC = CRC16(response, 2u, 8u);
        return CRCMatches(response, 2u, responseCRC) ? ControllerResult::Success
                                                     : ControllerResult::DataError;
    }

    HalfWord Controller::CRC16(const Byte* data, Byte byteCount, Byte firstByteBitCount)
    {
        HalfWord crc = 0xffffu;

        if (data == nullptr || byteCount == 0u)
        {
            return 0u;
        }
        for (unsigned int byte = 0u; byte < byteCount; ++byte)
        {
            Byte value = data[byte];
            const unsigned int bitCount = byte == 0u ? firstByteBitCount : 8u;

            for (unsigned int bit = 0u; bit < bitCount; ++bit)
            {
                const HalfWord previous = crc;

                crc >>= 1;
                if ((value & 1u) != 0u)
                {
                    crc = static_cast<HalfWord>((crc ^ 0x8408u) | 0x8000u);
                }
                if ((previous & 1u) != 0u)
                {
                    crc ^= 0x8408u;
                }
                value >>= 1;
            }
        }
        return static_cast<HalfWord>(~crc);
    }

    bool Controller::CRCMatches(const Byte* data, Byte crcLowIndex,
                                HalfWord expectedCRC)
    {
        if (data == nullptr || crcLowIndex >= 0xffu)
        {
            return false;
        }
        const HalfWord storedCRC =
            static_cast<HalfWord>(data[crcLowIndex]) |
            static_cast<HalfWord>(static_cast<HalfWord>(data[crcLowIndex + 1u]) << 8);
        return storedCRC == expectedCRC;
    }
}
