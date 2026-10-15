#pragma once

#include <tdk/Types.hpp>

namespace TDK
{
    class Checksum
    {
    public:
        static Word SumWords(const volatile Word* words, size_t wordCount);
        static bool VerifyLoadedImage(const volatile Word* codeWords,
                                      size_t codeWordCount,
                                      const volatile Word* assetWords,
                                      size_t assetWordCount, Word expectedChecksum,
                                      int overrideStatus);
    };
}
