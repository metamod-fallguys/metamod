#ifndef MM_BLOB_H
#define MM_BLOB_H

#include <cstddef>
#include <cstdint>
#include <vector>

// Description only: Metamod observes an engine already loaded by the launcher.
namespace Blob
{
struct Section
{
    uint32_t address;
    uint32_t size;
    uint32_t dataSize;
    uint32_t dataOffset;
};

struct Image
{
    uint32_t                   base = 0;
    uint32_t                   size = 0;
    std::vector<Section>       sections;
    std::vector<unsigned char> bytes;
};

bool Parse(const std::vector<unsigned char>& file, Image& image);
bool MatchesCode(const Image& image, uintptr_t address, const void* bytes, size_t size);
} // namespace Blob
#endif
