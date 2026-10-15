#pragma once

#include <tdk/Types.hpp>

namespace TDK
{
    struct Platform
    {
        void* context;
        bool (*read32)(void* context, Address address, Word* value);
        bool (*write32)(void* context, Address address, Word value);
        bool (*enableInterrupts)(void* context);
        bool (*disableInterrupts)(void* context);
        bool (*waitForInterrupt)(void* context);
        bool (*waitForVBlank)(void* context);
        void (*delayIterations)(void* context, uint32_t iterations);
    };

    const Platform* GetPlatform();
    Result ReadRegister(Address address, Word* value);
    Result WriteRegister(Address address, Word value);
}
