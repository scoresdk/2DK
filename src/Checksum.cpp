#include <tdk/Checksum.hpp>

namespace TDK
{
    Word Checksum::SumWords(const volatile Word* words, size_t wordCount)
    {
        Word sum = 0u;

        while (wordCount != 0u)
        {
            sum += *words++;
            --wordCount;
        }
        return sum;
    }

    bool Checksum::VerifyLoadedImage(const volatile Word* codeWords,
                                     size_t codeWordCount,
                                     const volatile Word* assetWords,
                                     size_t assetWordCount, Word expectedChecksum,
                                     int overrideStatus)
    {
        Word checksum = SumWords(codeWords, codeWordCount);
        checksum += SumWords(assetWords, assetWordCount);
        checksum = ~checksum;

        return checksum == expectedChecksum || overrideStatus < 0;
    }
}
