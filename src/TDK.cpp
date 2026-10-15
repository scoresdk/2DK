#include <tdk/TDK.hpp>

namespace TDK
{
    namespace
    {
        Platform activePlatform = {};
        bool initialized = false;
    }

    Result Init(const Platform& platform)
    {
        if (platform.read32 == nullptr || platform.write32 == nullptr)
        {
            return Result::InvalidArgument;
        }

        activePlatform = platform;
        initialized = true;
        return Result::Success;
    }

    bool IsInitialized()
    {
        return initialized;
    }

    const Platform* GetPlatform()
    {
        return initialized ? &activePlatform : nullptr;
    }

    Result ReadRegister(Address address, Word* value)
    {
        if (!initialized)
        {
            return Result::NotInitialized;
        }
        if (value == nullptr)
        {
            return Result::InvalidArgument;
        }
        return activePlatform.read32(activePlatform.context, address, value)
                   ? Result::Success
                   : Result::HardwareError;
    }

    Result WriteRegister(Address address, Word value)
    {
        if (!initialized)
        {
            return Result::NotInitialized;
        }
        return activePlatform.write32(activePlatform.context, address, value)
                   ? Result::Success
                   : Result::HardwareError;
    }
}
