#pragma once

#include <tdk/Types.hpp>

namespace TDK
{
    struct DiscRequest
    {
        Word byteCount;
        Byte transferMode;
        Byte deviceId;
        Byte transferLength;
        Byte command;
        Word dataLength;
        HalfWord blockCount;
        Word transferFlags;
        Word dataValue;
    };

    struct DiscCommandState
    {
        Word submissionContext;
        DiscRequest request;
        Byte response[16];
    };

    struct DiscCommandBus
    {
        void* context;
        int (*submit)(void* context, Address submitRoutine, const DiscRequest* request,
                      Byte response[16], Word submissionContext, Word argument,
                      Word blockCount);
    };

    enum class DiscResult : uint8_t
    {
        Success,
        InvalidArgument,
        HardwareError
    };

    class Disc
    {
    public:
        static DiscResult Read(const DiscCommandBus& bus, DiscCommandState* state,
                               Byte deviceId, Word argument);
        static DiscResult Write(const DiscCommandBus& bus, DiscCommandState* state,
                                Byte deviceId, Word argument);
        static DiscResult Status(const DiscCommandBus& bus, DiscCommandState* state,
                                 Byte deviceId, Word argument);
        static DiscResult ReadResult(const DiscCommandBus& bus, DiscCommandState* state,
                                     Byte deviceId, Word responseBuffer,
                                     int32_t* result);
        static DiscResult Reset(const DiscCommandBus& bus, DiscCommandState* state,
                                Byte deviceId);
        static DiscResult TransferBlocks(const DiscCommandBus& bus,
                                         DiscCommandState* state, Byte deviceId,
                                         Word argument, int32_t blockCount,
                                         Word dataValue);
        static DiscResult ReceiveBlocks(const DiscCommandBus& bus,
                                        DiscCommandState* state, Byte deviceId,
                                        Word argument, int32_t blockCount,
                                        Word dataValue);
        static DiscResult Parameter(const DiscCommandBus& bus, DiscCommandState* state,
                                    Byte deviceId, Word argument, Byte command,
                                    Word dataValue);
    };
}
