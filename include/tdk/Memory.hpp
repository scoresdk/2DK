#pragma once

#include <tdk/Types.hpp>

namespace TDK
{
    class Memory
    {
    public:
        static void* Copy(void* destination, const void* source, size_t byteCount);
        static void* Set(void* destination, Byte value, size_t byteCount);
        static int Compare(const void* left, const void* right, size_t byteCount);
        static size_t StringLength(const char* text);
    };
}
