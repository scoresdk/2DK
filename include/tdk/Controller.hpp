#pragma once

#include <tdk/Types.hpp>

namespace TDK
{
    struct ControllerBus
    {
        void* context;
        int (*setLine)(void* context, Word level);
        int (*readLine)(void* context, Word* level);
        void (*delay)(void* context, Word iterations);
        int (*readByte)(void* context, Byte address, Byte* value);
        int (*writeByte)(void* context, Byte address, Byte value);
    };

    struct ControllerState
    {
        Byte invertedPolarity;
        Byte receivedBytes[256];
    };

    enum class ControllerResult : uint8_t
    {
        Success,
        InvalidArgument,
        HardwareError,
        SynchronizationError,
        DataError
    };

    class Controller
    {
    public:
        static ControllerResult ClockPulse(const ControllerBus& bus);
        static ControllerResult SendByte(const ControllerBus& bus, Byte value);
        static ControllerResult SendCommand(const ControllerBus& bus, Byte value);
        static ControllerResult ReadBit(const ControllerBus& bus, Byte* value);
        static ControllerResult Synchronize(const ControllerBus& bus,
                                            ControllerState* state);
        static ControllerResult DecodeByte(const ControllerBus& bus,
                                           ControllerState* state, Byte index,
                                           Byte* value);
        static ControllerResult ReadBytes(const ControllerBus& bus,
                                          ControllerState* state, Byte startIndex,
                                          Byte count, Byte* destination,
                                          size_t capacity);
        static ControllerResult ReadDeviceBytes(const ControllerBus& bus,
                                                Byte startAddress, Byte count,
                                                Byte* destination, size_t capacity);
        static ControllerResult CheckPresence(const ControllerBus& bus,
                                              ControllerState* state,
                                              Word responseWaitQuantum, bool* present);
        static ControllerResult QueryIdentity(const ControllerBus& bus,
                                              ControllerState* state,
                                              Word responseWaitQuantum,
                                              Byte identity[4], size_t capacity);
        static bool WriteAndVerifyBytes(const ControllerBus& bus, Byte startAddress,
                                        const Byte* source, Byte count);
        static ControllerResult
        WriteParameterAndConfirm(const ControllerBus& bus, ControllerState* state,
                                 Byte parameter, const Byte context[4],
                                 Word responseWaitQuantum, Byte* acceptedParameter);
        static ControllerResult WriteRegisterAndConfirm(const ControllerBus& bus,
                                                        ControllerState* state,
                                                        Byte address, Byte value,
                                                        const Byte context[4],
                                                        Word responseWaitQuantum);
        static HalfWord CRC16(const Byte* data, Byte byteCount, Byte firstByteBitCount);
        static bool CRCMatches(const Byte* data, Byte crcLowIndex,
                               HalfWord expectedCRC);
    };
}
