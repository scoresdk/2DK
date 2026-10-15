#pragma once

#include <tdk/Types.hpp>

namespace TDK
{
    class Serial
    {
    public:
        static Result Initialize();
        static Result WriteByte(Byte value);
        static Result Write(const char* text);
        static Result ReadByte(Byte* value);
    };
}
