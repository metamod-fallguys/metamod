// BLOB description decoding follows MetaHookSv's LoadBlob.cpp. No imports,
// entry points or engine code are executed here.
#include "blob.h"

#include <algorithm>
#include <cstring>

namespace
{
constexpr size_t   kInfoSize    = 68;
constexpr size_t   kHeaderSize  = 24;
constexpr size_t   kSectionSize = 20;
constexpr uint32_t kAlgorithm   = 0x12345678;

uint32_t Read32(const std::vector<unsigned char>& bytes, size_t offset)
{
    return uint32_t(bytes[offset]) | (uint32_t(bytes[offset + 1]) << 8) |
        (uint32_t(bytes[offset + 2]) << 16) | (uint32_t(bytes[offset + 3]) << 24);
}
} // namespace

bool Blob::Parse(const std::vector<unsigned char>& file, Image& image)
{
    image = Image{};
    if (file.size() < kInfoSize + kHeaderSize || Read32(file, 64) != kAlgorithm)
        return false;
    Image result;
    result.bytes      = file;
    unsigned char key = 0x57;
    for (size_t i = kInfoSize; i < result.bytes.size(); ++i)
    {
        result.bytes[i] ^= key;
        key = (unsigned char)(key + result.bytes[i] + 0x57);
    }
    const auto&  bytes    = result.bytes;
    const size_t count    = size_t(bytes[72] | (uint16_t(bytes[73]) << 8)) + 1;
    const size_t tableEnd = kInfoSize + kHeaderSize + count * kSectionSize;
    if (tableEnd > bytes.size())
        return false;
    result.base = Read32(bytes, 80) ^ 0x49c042d1;
    if (!result.base || (result.base & 0xfff))
        return false;
    for (size_t i = 0; i < count; ++i)
    {
        const size_t offset = kInfoSize + kHeaderSize + i * kSectionSize;
        Section      section{Read32(bytes, offset), Read32(bytes, offset + 4), Read32(bytes, offset + 8), Read32(bytes, offset + 12)};
        if (!section.size || section.address < result.base || section.size > UINT32_MAX - section.address ||
            section.dataSize > section.size || section.dataOffset < tableEnd ||
            section.dataOffset > bytes.size() || section.dataSize > bytes.size() - section.dataOffset)
            return false;
        for (const auto& other : result.sections)
        {
            if (section.address < other.address + other.size && other.address < section.address + section.size)
                return false;
        }
        result.size = std::max(result.size, section.address + section.size - result.base);
        result.sections.push_back(section);
    }
    const auto inImage = [&](uint32_t address) {
        for (const auto& section : result.sections)
            if (address >= section.address && address - section.address < section.size)
                return true;
        return false;
    };
    const uint32_t entry = Read32(bytes, 84);
    if (entry < 12 || !inImage(entry - 12) || !inImage(Read32(bytes, 76) ^ 0x7a32bc85) ||
        !inImage(Read32(bytes, 88) ^ 0x872c3d47))
        return false;
    image = std::move(result);
    return true;
}

bool Blob::MatchesCode(const Image& image, uintptr_t address, const void* bytes, size_t size)
{
    if (!bytes || !size || image.sections.empty())
        return false;
    const auto& code = image.sections.front();
    if (address < code.address || address - code.address >= code.dataSize || size > code.dataSize - (address - code.address))
        return false;
    return memcmp(image.bytes.data() + code.dataOffset + (address - code.address), bytes, size) == 0;
}
