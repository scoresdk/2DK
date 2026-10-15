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
