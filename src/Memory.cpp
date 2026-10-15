#include <tdk/Memory.hpp>

#include <stdint.h>

namespace TDK
{
    void* Memory::Copy(void* destination, const void* source, size_t byteCount)
    {
        auto* output = static_cast<Byte*>(destination);
        const auto* input = static_cast<const Byte*>(source);

        if (output == input || byteCount == 0u)
        {
            return destination;
        }
        const uintptr_t destinationAddress = reinterpret_cast<uintptr_t>(output);
        const uintptr_t sourceAddress = reinterpret_cast<uintptr_t>(input);
        if (destinationAddress < sourceAddress ||
            destinationAddress - sourceAddress >= byteCount)
        {
            for (size_t index = 0u; index < byteCount; ++index)
            {
                output[index] = input[index];
            }
        }
        else
        {
            while (byteCount != 0u)
            {
                --byteCount;
                output[byteCount] = input[byteCount];
            }
        }
        return destination;
    }

    void* Memory::Set(void* destination, Byte value, size_t byteCount)
    {
        auto* output = static_cast<Byte*>(destination);

        for (size_t index = 0u; index < byteCount; ++index)
        {
            output[index] = value;
        }
        return destination;
    }

    int Memory::Compare(const void* left, const void* right, size_t byteCount)
    {
        const auto* first = static_cast<const Byte*>(left);
        const auto* second = static_cast<const Byte*>(right);

        for (size_t index = 0u; index < byteCount; ++index)
        {
            if (first[index] != second[index])
            {
                return first[index] < second[index] ? -1 : 1;
            }
        }
        return 0;
    }

    size_t Memory::StringLength(const char* text)
    {
        size_t length = 0u;

        if (text == nullptr)
        {
            return 0u;
        }
        while (text[length] != '\0')
        {
            ++length;
        }
        return length;
    }
}
