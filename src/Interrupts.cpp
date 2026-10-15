#include <tdk/Interrupts.hpp>

#include <tdk/Platform.hpp>
#include <tdk/TDK.hpp>

namespace TDK
{
    namespace
    {
        struct InterruptEntry
        {
            InterruptHandler handler;
            void* context;
        };

        InterruptEntry interruptTable[Interrupts::Count] = {};

        constexpr Address MaskLow = 0x880a0020u;
        constexpr Address MaskHigh = 0x880a0024u;
        constexpr uint32_t FirstSource = 24u;
        constexpr Word MaskLowAll = 0xffffffffu;
        constexpr Word MaskHighAll = 0xffu;

        Word maskLow = 0u;
        Word maskHigh = 0u;

        Result WriteMasks()
        {
            if (WriteRegister(MaskLow, maskLow) != Result::Success ||
                WriteRegister(MaskHigh, maskHigh) != Result::Success)
            {
                return Result::HardwareError;
            }
            return Result::Success;
        }

        Result SetMasked(uint32_t interruptNumber, bool masked)
        {
            if (interruptNumber < FirstSource || interruptNumber >= Interrupts::Count)
            {
                return Result::InvalidArgument;
            }

            Word* shadow = interruptNumber >= 32u ? &maskLow : &maskHigh;
            const Word bit = interruptNumber >= 32u ? 1u << (63u - interruptNumber)
                                                    : 1u << (31u - interruptNumber);

            *shadow = masked ? (*shadow | bit) : (*shadow & ~bit);
            return WriteMasks();
        }
    }

    Result Interrupts::Install(uint32_t interruptNumber, InterruptHandler handler,
                               void* context)
    {
        if (!IsInitialized())
        {
            return Result::NotInitialized;
        }
        if (interruptNumber >= Count || handler == nullptr)
        {
            return Result::InvalidArgument;
        }

        interruptTable[interruptNumber] = {handler, context};
        return Result::Success;
    }

    Result Interrupts::Uninstall(uint32_t interruptNumber)
    {
        if (!IsInitialized())
        {
            return Result::NotInitialized;
        }
        if (interruptNumber >= Count)
        {
            return Result::InvalidArgument;
        }

        interruptTable[interruptNumber] = {};
        return Result::Success;
    }

    Result Interrupts::Enable()
    {
        const Platform* platform = GetPlatform();

        if (platform == nullptr)
        {
            return Result::NotInitialized;
        }
        if (platform->enableInterrupts == nullptr)
        {
            return Result::Unsupported;
        }
        return platform->enableInterrupts(platform->context) ? Result::Success
                                                             : Result::HardwareError;
    }

    Result Interrupts::Disable()
    {
        const Platform* platform = GetPlatform();

        if (platform == nullptr)
        {
            return Result::NotInitialized;
        }
        if (platform->disableInterrupts == nullptr)
        {
            return Result::Unsupported;
        }
        return platform->disableInterrupts(platform->context) ? Result::Success
                                                              : Result::HardwareError;
    }

    Result Interrupts::Wait()
    {
        const Platform* platform = GetPlatform();

        if (platform == nullptr)
        {
            return Result::NotInitialized;
        }
        if (platform->waitForInterrupt == nullptr)
        {
            return Result::Unsupported;
        }
        return platform->waitForInterrupt(platform->context) ? Result::Success
                                                             : Result::HardwareError;
    }

    Result Interrupts::Mask(uint32_t interruptNumber)
    {
        return SetMasked(interruptNumber, true);
    }

    Result Interrupts::Unmask(uint32_t interruptNumber)
    {
        return SetMasked(interruptNumber, false);
    }

    Result Interrupts::MaskAll()
    {
        maskLow = MaskLowAll;
        maskHigh = MaskHighAll;
        return WriteMasks();
    }

    Result Interrupts::UnmaskAll()
    {
        maskLow = 0u;
        maskHigh = 0u;
        return WriteMasks();
    }

    bool Interrupts::Dispatch(uint32_t interruptNumber)
    {
        if (interruptNumber >= Count ||
            interruptTable[interruptNumber].handler == nullptr)
        {
            return false;
        }

        interruptTable[interruptNumber].handler(
            interruptNumber, interruptTable[interruptNumber].context);
        return true;
    }
}
