// Optional integration probe with real BLOB inputs and published gamedata.
// Inputs are supplied externally and are never embedded in the repository.
#include "blob.h"
#include "gamedata.h"
#include "test_common.h"

#include <filesystem>
#include <fstream>
#include <iterator>

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        printf("usage: mmfg_test_blob_catalog <VibeSignatures root> <catalog dir> <catalog dir>\n");
        return 77;
    }
    ASSERT_TRUE(GameData::Initialize({argv[2], argv[3]}));
    struct Fixture
    {
        const char* tag;
        uint64_t    crc;
        uint32_t    rvas[5];
    };
    const Fixture fixtures[] = {
        {"hl-3248", 0x7e07685c707cb53eULL, {0xac6d0, 0x56970, 0x105e0b8, 0xb0dee8, 0x8069bc}},
        {"hl-3266", 0x8d3b22546891b7a8ULL, {0xac6d0, 0x56950, 0x105e0b8, 0xb0dee8, 0x8069bc}},
        {"hl-3329", 0xbcd73e6d3ca715a4ULL, {0xabd40, 0x566b0, 0x102a9d8, 0xada808, 0x7d32dc}},
        {"hl-3647", 0xa4f5787a83b57d53ULL, {0xaad40, 0x567e0, 0x1029858, 0xad9688, 0x7d215c}},
    };
    const char* names[] = {"SV_BeginFileDownload_f", "SV_DropClient", "cmd_source", "host_client", "sv_player"};
    for (const auto& fixture : fixtures)
    {
        TEST(fixture.tag);
        auto          path = std::filesystem::path(argv[1]) / "bin" / fixture.tag / "engine" / "hw.dll";
        std::ifstream file(path, std::ios::binary);
        ASSERT_TRUE(file.good());
        const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
        Blob::Image                      image;
        ASSERT_TRUE(Blob::Parse(bytes, image));
        uint64_t crc = 0;
        ASSERT_INT(GameData::ComputeFileCRC64(path.string().c_str(), &crc), META_GAMESYMBOL_OK);
        ASSERT_TRUE(fixture.crc == crc);
        std::vector<char>                 mapped(image.size);
        std::vector<GameData::ImageRange> ranges;
        for (const auto& section : image.sections)
        {
            const uint32_t offset = section.address - image.base;
            memcpy(mapped.data() + offset, image.bytes.data() + section.dataOffset, section.dataSize);
            ranges.push_back({offset, section.size});
        }
        ASSERT_TRUE(GameData::RegisterModuleFileSource(mapped.data(), path.string(), image.size, ranges));
        for (size_t i = 0; i < 5; ++i)
        {
            void* address = nullptr;
            ASSERT_INT(GameData::Resolve(mapped.data(), names[i], i < 2 ? META_GAMESYMBOL_KIND_FUNCTION : META_GAMESYMBOL_KIND_GLOBAL, &address), META_GAMESYMBOL_OK);
            ASSERT_PTR_EQ(address, mapped.data() + fixture.rvas[i]);
        }
        GameData::UnregisterModuleFileSource(mapped.data());
        PASS();
    }
    return tests_run == tests_passed ? 0 : 1;
}
