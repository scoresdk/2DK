#include <tdk/Disc.hpp>

namespace TDK
{
    namespace
    {
        constexpr Address SubmitRoutine = 0xa001f5c0u;
        constexpr Address ResetRoutine = 0xa001f750u;
        constexpr Address ReceiveRoutine = 0xa001f830u;

        void SetCommand(DiscRequest* request, Word byteCount, Byte transferMode,
                        Byte transferLength, Byte command)
        {
            request->byteCount = byteCount;
            request->transferMode = transferMode;
            request->transferLength = transferLength;
            request->command = command;
            request->dataLength = 0u;
            request->blockCount = 0u;
            request->transferFlags = 0u;
            request->dataValue = 0u;
        }

        DiscResult Submit(const DiscCommandBus& bus, DiscCommandState* state,
                          Address routine, Word argument, Word blockCount)
        {
            if (bus.submit == nullptr || state == nullptr)
            {
                return DiscResult::InvalidArgument;
            }
            return bus.submit(bus.context, routine, &state->request, state->response,
                              state->submissionContext, argument, blockCount) == 0
                       ? DiscResult::Success
                       : DiscResult::HardwareError;
        }
    }

    DiscResult Disc::Read(const DiscCommandBus& bus, DiscCommandState* state,
                          Byte deviceId, Word argument)
    {
        if (state == nullptr)
        {
            return DiscResult::InvalidArgument;
        }
        SetCommand(&state->request, 0x24u, 0x80u, 6u, 0x12u);
        state->request.dataLength = 0x2400u;
        state->request.deviceId = deviceId;
        return Submit(bus, state, SubmitRoutine, argument, 0u);
    }

    DiscResult Disc::Write(const DiscCommandBus& bus, DiscCommandState* state,
                           Byte deviceId, Word argument)
    {
        if (state == nullptr)
        {
            return DiscResult::InvalidArgument;
        }
        SetCommand(&state->request, 0xfcu, 0x80u, 10u, 0x23u);
        state->request.transferFlags = 0xfcu;
        state->request.deviceId = deviceId;
        return Submit(bus, state, SubmitRoutine, argument, 0u);
    }

    DiscResult Disc::Status(const DiscCommandBus& bus, DiscCommandState* state,
                            Byte deviceId, Word argument)
    {
        if (state == nullptr)
        {
            return DiscResult::InvalidArgument;
        }
        SetCommand(&state->request, 8u, 0x80u, 10u, 0x25u);
        state->request.deviceId = deviceId;
        return Submit(bus, state, SubmitRoutine, argument, 0u);
    }

    DiscResult Disc::ReadResult(const DiscCommandBus& bus, DiscCommandState* state,
                                Byte deviceId, Word responseBuffer, int32_t* result)
    {
        if (state == nullptr || result == nullptr)
        {
            return DiscResult::InvalidArgument;
        }
        SetCommand(&state->request, 0x12u, 0x80u, 0x0cu, 3u);
        state->request.dataLength = 0x1200u;
        state->request.deviceId = deviceId;
        const DiscResult status = Submit(bus, state, SubmitRoutine, responseBuffer, 0u);

        if (status != DiscResult::Success)
        {
            *result = 1;
            return status;
        }
        *result =
            static_cast<int32_t>(static_cast<int8_t>(state->response[0x0du])) +
            static_cast<int32_t>(static_cast<int8_t>(state->response[0x0cu])) * 0x100 +
            static_cast<int32_t>(static_cast<int8_t>(state->response[0x02u])) * 0x10000;
        return DiscResult::Success;
    }

    DiscResult Disc::Reset(const DiscCommandBus& bus, DiscCommandState* state,
                           Byte deviceId)
    {
        if (state == nullptr)
        {
            return DiscResult::InvalidArgument;
        }
        SetCommand(&state->request, 0u, 0u, 6u, 0u);
        state->request.deviceId = deviceId;
        return Submit(bus, state, ResetRoutine, 0u, 0u);
    }

    DiscResult Disc::TransferBlocks(const DiscCommandBus& bus, DiscCommandState* state,
                                    Byte deviceId, Word argument, int32_t blockCount,
                                    Word dataValue)
    {
        if (state == nullptr || blockCount < 0 || blockCount > 0xffff)
        {
            return DiscResult::InvalidArgument;
        }
        SetCommand(&state->request, static_cast<Word>(blockCount) << 9, 0x80u, 10u,
                   0x28u);
        state->request.deviceId = deviceId;
        state->request.blockCount = static_cast<HalfWord>(blockCount);
        state->request.transferFlags = static_cast<HalfWord>(blockCount);
        state->request.dataValue = dataValue;
        return Submit(bus, state, SubmitRoutine, argument,
                      static_cast<Word>(blockCount));
    }

    DiscResult Disc::ReceiveBlocks(const DiscCommandBus& bus, DiscCommandState* state,
                                   Byte deviceId, Word argument, int32_t blockCount,
                                   Word dataValue)
    {
        if (state == nullptr || blockCount < 0 || blockCount > 0xffff)
        {
            return DiscResult::InvalidArgument;
        }
        SetCommand(&state->request, static_cast<Word>(blockCount) << 9, 0u, 10u, 0x2au);
        state->request.deviceId = deviceId;
        state->request.blockCount = static_cast<HalfWord>(blockCount);
        state->request.transferFlags = static_cast<HalfWord>(blockCount);
        state->request.dataValue = dataValue;
        return Submit(bus, state, ReceiveRoutine, argument,
                      static_cast<Word>(blockCount));
    }

    DiscResult Disc::Parameter(const DiscCommandBus& bus, DiscCommandState* state,
                               Byte deviceId, Word argument, Byte command,
                               Word dataValue)
    {
        if (state == nullptr)
        {
            return DiscResult::InvalidArgument;
        }
        SetCommand(&state->request, 0xc0u, 0x80u, 6u, command);
        state->request.deviceId = deviceId;
        state->request.dataValue = dataValue;
        return Submit(bus, state, SubmitRoutine, argument, 0u);
    }
}
