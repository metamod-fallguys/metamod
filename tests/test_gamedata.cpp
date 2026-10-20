// Tests for the gamedata catalog (src/gamedata.cpp).
//
// Fixture catalogs are written into the test working directory at run time.
// The "self" module of every fixture carries the CRC-64 of this executable, so
// GameData::Resolve runs its real path: module file lookup, hashing, catalog
// query and bounds check.

#include <sha2_256.h>

#include "gamedata.h"
#include "test_common.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#else
#    include <dlfcn.h>
#endif

namespace fs = std::filesystem;

namespace
{
#ifdef _WIN32
const char* const kPlatform      = "windows";
const char* const kOtherPlatform = "linux";
#else
const char* const kPlatform      = "linux";
const char* const kOtherPlatform = "windows";
#endif

// Larger than any test executable, so a symbol at this RVA is out of bounds.
const char* const kOutOfImageRva = "0x7ffffff0";

std::string g_selfCrc64;

std::string Hex64(uint64_t value)
{
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)value);
    return buf;
}

std::string Sha256Hex(const std::string& data)
{
    Chocobo1::SHA2_256 hasher;
    hasher.addData(data.data(), data.size());
    hasher.finalize();
    return hasher.toString();
}

void WriteFile(const fs::path& path, const std::string& data)
{
    fs::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(data.data(), (std::streamsize)data.size());
}

std::string Record(const char* platform, const char* module, const char* symbolName, const char* kind, const std::string& payload)
{
    return std::string("{\"platform\":\"") + platform + "\",\"module\":\"" + module + "\",\"symbolName\":\"" + symbolName + "\",\"kind\":\"" + kind +
        "\",\"payload\":" + payload + "}";
}

std::string Function(const char* rva, const char* size)
{
    return std::string("{\"func_rva\":\"") + rva + "\",\"func_size\":\"" + size + "\"}";
}

std::string Global(const char* rva)
{
    return std::string("{\"gv_rva\":\"") + rva + "\"}";
}

std::string Snapshot(const std::vector<std::string>& records, int snapshotSchemaVersion = 5)
{
    std::string json = "{\"schemaVersion\":" + std::to_string(snapshotSchemaVersion) +
        ",\"source\":{\"snapshotSchemaVersion\":8,\"analysisOutputContractVersion\":3},"
        "\"binaries\":{\"self\":{\"" +
        std::string(kPlatform) + "\":{\"crc64\":\"" + g_selfCrc64 + "\"},\"" + kOtherPlatform +
        "\":{\"crc64\":\"00000000000000aa\"}},"
        "\"other\":{\"" +
        kPlatform + "\":{\"crc64\":\"00000000000000bb\"}}},\"records\":[";
    for (size_t i = 0; i < records.size(); ++i)
    {
        if (i)
            json += ",";
        json += records[i];
    }
    return json + "]}";
}

// Writes <dir>/index.json declaring one snapshot file per entry.
struct SnapshotFile
{
    std::string gameVersion;
    std::string fileName;
    std::string content;
    bool        corruptSha256 = false;
};

void WriteIndex(const fs::path& dir, const std::vector<SnapshotFile>& snapshots)
{
    std::string index = "{\"schemaVersion\":4,\"versions\":[";
    for (size_t i = 0; i < snapshots.size(); ++i)
    {
        const SnapshotFile& s = snapshots[i];
        WriteFile(dir / s.fileName, s.content);
        std::string sha256 = Sha256Hex(s.content);
        if (s.corruptSha256)
            sha256[0] = (sha256[0] == '0') ? '1' : '0';
        if (i)
            index += ",";
        index += "{\"gameVersion\":\"" + s.gameVersion + "\",\"url\":\"" + s.fileName + "\",\"sha256\":\"" + sha256 +
            "\",\"size\":" + std::to_string(s.content.size()) + "}";
    }
    WriteFile(dir / "index.json", index + "]}");
}

std::string SelfPath()
{
#ifdef _WIN32
    char  path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, path, sizeof(path));
    return std::string(path, len);
#else
    return fs::canonical("/proc/self/exe").string();
#endif
}

void* SelfBase()
{
#ifdef _WIN32
    return GetModuleHandleA(NULL);
#else
    Dl_info info;
    if (!dladdr((void*)&SelfBase, &info))
        return NULL;
    return info.dli_fbase;
#endif
}

bool HasDiagnostic(const char* needle)
{
    return GameData::GetDiagnostics().find(needle) != std::string::npos;
}

meta_gamesymbol_status_t Query(const char* symbolName, meta_gamesymbol_kind_t* kind, uint32_t* rva, uint32_t* size)
{
    uint64_t crc64 = 0;
    sscanf(g_selfCrc64.c_str(), "%llx", (unsigned long long*)&crc64);
    return GameData::QueryByCRC64(crc64, symbolName, kind, rva, size);
}
} // namespace

int main()
{
    const fs::path root = fs::current_path() / "gamedata_fixture";
    fs::remove_all(root);

    TEST("gamedata - CRC-64/XZ matches the reference check value");
    {
        const fs::path check = root / "crc_check.bin";
        WriteFile(check, "123456789");
        uint64_t crc64 = 0;
        ASSERT_INT(GameData::ComputeFileCRC64(check.string().c_str(), &crc64), META_GAMESYMBOL_OK);
        const std::string hex = Hex64(crc64);
        ASSERT_STR(hex.c_str(), "995dc9bbdf1939fa");
    }
    PASS();

    TEST("gamedata - missing file fails to hash");
    {
        uint64_t crc64 = 0;
        ASSERT_INT(GameData::ComputeFileCRC64((root / "missing.bin").string().c_str(), &crc64), META_GAMESYMBOL_MODULE_HASH_FAILED);
        ASSERT_INT(GameData::ComputeFileCRC64(NULL, &crc64), META_GAMESYMBOL_INVALID_ARGUMENT);
    }
    PASS();

    uint64_t selfCrc64 = 0;
    if (GameData::ComputeFileCRC64(SelfPath().c_str(), &selfCrc64) != META_GAMESYMBOL_OK)
    {
        printf("cannot hash the test executable\n");
        return 1;
    }
    g_selfCrc64 = Hex64(selfCrc64);

    // Plugin "a": two game versions, one of them also declared by plugin "b".
    WriteIndex(root / "catalog" / "a",
               {
                   {"v1", "v1.json",
                    Snapshot({
                        Record(kPlatform, "self", "Func", "function", Function("0x1000", "0x20")),
                        Record(kOtherPlatform, "self", "Func", "function", Function("0x2000", "0x20")),
                        Record(kOtherPlatform, "self", "OtherPlatformOnly", "function", Function("0x3000", "0x10")),
                        Record(kPlatform, "self", "Var", "global", Global("0x1100")),
                        Record(kPlatform, "self", "VTable", "vtable", "{}"),
                        Record(kPlatform, "self", "OutOfImage", "function", Function(kOutOfImageRva, "0x10")),
                        Record(kPlatform, "self", "Dup", "function", Function("0x1200", "0x10")),
                        Record(kPlatform, "self", "Conflict", "function", Function("0x1300", "0x10")),
                        Record(kPlatform, "self", "BadPayload", "function", "{\"func_rva\":\"zz\",\"func_size\":\"0x10\"}"),
                        Record(kPlatform, "unknown", "UnknownModule", "function", Function("0x1400", "0x10")),
                        Record(kPlatform, "other", "OtherModuleFunc", "function", Function("0x1500", "0x10")),
                    })},
                   {"v2", "v2.json", Snapshot({Record(kPlatform, "self", "BadSha", "function", Function("0x1600", "0x10"))}), true},
               });
    WriteIndex(root / "catalog" / "b",
               {
                   {"v1", "v1.json",
                    Snapshot({
                        Record(kPlatform, "self", "Dup", "function", Function("0x1200", "0x10")),
                        Record(kPlatform, "self", "Conflict", "function", Function("0x1310", "0x10")),
                        Record(kPlatform, "self", "PluginB", "global", Global("0x1700")),
                    })},
                   {"v3", "v3.json", Snapshot({Record(kPlatform, "self", "WrongSchema", "function", Function("0x1800", "0x10"))}, 4)},
               });
    // A directory without index.json is skipped, its children are still scanned.
    WriteIndex(root / "catalog" / "nested" / "c",
               {{"v1", "v1.json", Snapshot({Record(kPlatform, "self", "Nested", "function", Function("0x1900", "0x10"))})}});

    TEST("gamedata - index directories are collected recursively in order");
    std::vector<std::string> dirs = GameData::CollectIndexDirs((root / "catalog").string());
    ASSERT_INT(dirs.size(), 3);
    {
        // ASSERT_STR keeps the pointer, so the names must outlive the macro.
        const std::string first  = fs::path(dirs[0]).filename().string();
        const std::string second = fs::path(dirs[1]).filename().string();
        const std::string third  = fs::path(dirs[2]).filename().string();
        ASSERT_STR(first.c_str(), "a");
        ASSERT_STR(second.c_str(), "b");
        ASSERT_STR(third.c_str(), "c");
    }
    ASSERT_INT(GameData::CollectIndexDirs((root / "absent").string()).size(), 0);
    PASS();

    TEST("gamedata - catalog without any index is unavailable");
    {
        ASSERT_FALSE(GameData::Initialize({(root / "absent").string()}));
        ASSERT_FALSE(GameData::IsAvailable());
        void* address = (void*)1;
        ASSERT_INT(GameData::Resolve(SelfBase(), "Func", META_GAMESYMBOL_KIND_FUNCTION, &address), META_GAMESYMBOL_GAMEDATA_UNAVAILABLE);
        ASSERT_PTR_NULL(address);
    }
    PASS();

    TEST("gamedata - fixture catalog loads with diagnostics");
    ASSERT_TRUE(GameData::Initialize(dirs));
    ASSERT_TRUE(GameData::IsAvailable());
    ASSERT_TRUE(HasDiagnostic("snapshot 'v2': sha256 mismatch"));
    ASSERT_TRUE(HasDiagnostic("snapshot 'v3': unsupported schemaVersion"));
    ASSERT_TRUE(HasDiagnostic("symbol 'BadPayload': invalid func_rva"));
    ASSERT_TRUE(HasDiagnostic("record 'UnknownModule' references unknown module 'unknown'"));
    ASSERT_TRUE(HasDiagnostic("symbol 'Conflict'"));
    ASSERT_FALSE(HasDiagnostic("symbol 'Dup'"));
    PASS();

    meta_gamesymbol_kind_t kind = META_GAMESYMBOL_KIND_UNKNOWN;
    uint32_t               rva  = 0;
    uint32_t               size = 0;

    TEST("gamedata - query returns the current platform's record");
    ASSERT_INT(Query("Func", &kind, &rva, &size), META_GAMESYMBOL_OK);
    ASSERT_INT(kind, META_GAMESYMBOL_KIND_FUNCTION);
    ASSERT_INT(rva, 0x1000);
    ASSERT_INT(size, 0x20);
    ASSERT_INT(Query("OtherPlatformOnly", &kind, &rva, &size), META_GAMESYMBOL_SYMBOL_NOT_FOUND);
    PASS();

    TEST("gamedata - globals, nested and merged plugin catalogs");
    ASSERT_INT(Query("Var", &kind, &rva, &size), META_GAMESYMBOL_OK);
    ASSERT_INT(kind, META_GAMESYMBOL_KIND_GLOBAL);
    ASSERT_INT(rva, 0x1100);
    ASSERT_INT(size, 0);
    ASSERT_INT(Query("PluginB", &kind, &rva, &size), META_GAMESYMBOL_OK);
    ASSERT_INT(rva, 0x1700);
    ASSERT_INT(Query("Nested", &kind, &rva, &size), META_GAMESYMBOL_OK);
    ASSERT_INT(rva, 0x1900);
    ASSERT_INT(Query("Dup", &kind, &rva, &size), META_GAMESYMBOL_OK);
    ASSERT_INT(rva, 0x1200);
    PASS();

    TEST("gamedata - rejected records report a precise status");
    ASSERT_INT(Query("Conflict", &kind, &rva, &size), META_GAMESYMBOL_CATALOG_CONFLICT);
    ASSERT_INT(Query("VTable", &kind, &rva, &size), META_GAMESYMBOL_UNSUPPORTED_KIND);
    ASSERT_INT(Query("BadPayload", &kind, &rva, &size), META_GAMESYMBOL_SYMBOL_NOT_FOUND);
    ASSERT_INT(Query("BadSha", &kind, &rva, &size), META_GAMESYMBOL_SYMBOL_NOT_FOUND);
    ASSERT_INT(Query("WrongSchema", &kind, &rva, &size), META_GAMESYMBOL_SYMBOL_NOT_FOUND);
    ASSERT_INT(Query("", &kind, &rva, &size), META_GAMESYMBOL_INVALID_ARGUMENT);
    ASSERT_INT(GameData::QueryByCRC64(0xbb, "OtherModuleFunc", &kind, &rva, &size), META_GAMESYMBOL_OK);
    ASSERT_INT(GameData::QueryByCRC64(0xaa, "Func", &kind, &rva, &size), META_GAMESYMBOL_MODULE_NOT_FOUND);
    PASS();

    void* base = SelfBase();
    ASSERT_PTR_NOT_NULL(base);

    TEST("gamedata - resolve adds the rva to the module base");
    {
        void* address = NULL;
        ASSERT_INT(GameData::Resolve(base, "Func", META_GAMESYMBOL_KIND_FUNCTION, &address), META_GAMESYMBOL_OK);
        ASSERT_PTR_EQ(address, (char*)base + 0x1000);
        ASSERT_INT(GameData::Resolve(base, "Var", META_GAMESYMBOL_KIND_GLOBAL, &address), META_GAMESYMBOL_OK);
        ASSERT_PTR_EQ(address, (char*)base + 0x1100);
    }
    PASS();

    TEST("gamedata - resolve failures leave the output null");
    {
        void* address = (void*)1;
        ASSERT_INT(GameData::Resolve(base, "Func", META_GAMESYMBOL_KIND_GLOBAL, &address), META_GAMESYMBOL_KIND_MISMATCH);
        ASSERT_PTR_NULL(address);
        ASSERT_INT(GameData::Resolve(base, "Missing", META_GAMESYMBOL_KIND_FUNCTION, &address), META_GAMESYMBOL_SYMBOL_NOT_FOUND);
        ASSERT_INT(GameData::Resolve(base, "Func", META_GAMESYMBOL_KIND_UNKNOWN, &address), META_GAMESYMBOL_INVALID_ARGUMENT);
        ASSERT_INT(GameData::Resolve(NULL, "Func", META_GAMESYMBOL_KIND_FUNCTION, &address), META_GAMESYMBOL_INVALID_ARGUMENT);
        ASSERT_INT(GameData::Resolve(base, "Func", META_GAMESYMBOL_KIND_FUNCTION, NULL), META_GAMESYMBOL_INVALID_ARGUMENT);
#ifdef _WIN32
        // Only PE images carry a reliable SizeOfImage to check against.
        ASSERT_INT(GameData::Resolve(base, "OutOfImage", META_GAMESYMBOL_KIND_FUNCTION, &address), META_GAMESYMBOL_RVA_OUT_OF_RANGE);
        ASSERT_PTR_NULL(address);
#endif
    }
    PASS();

    TEST("gamedata - headerless source registration, holes and cache invalidation");
    {
        std::vector<char>                       mapped(0x2000);
        void*                                   blob    = mapped.data();
        void*                                   address = nullptr;
        const std::vector<GameData::ImageRange> ranges{{0x1000, 0x20}, {0x1100, 4}};
        ASSERT_TRUE(GameData::RegisterModuleFileSource(blob, SelfPath(), 0x2000, ranges));
        ASSERT_INT(GameData::GetRegisteredImageSize(blob), 0x2000);
        ASSERT_INT(GameData::Resolve(blob, "Func", META_GAMESYMBOL_KIND_FUNCTION, &address), META_GAMESYMBOL_OK);
        ASSERT_PTR_EQ(address, mapped.data() + 0x1000);
        ASSERT_INT(GameData::Resolve(blob, "Var", META_GAMESYMBOL_KIND_GLOBAL, &address), META_GAMESYMBOL_OK);
        ASSERT_INT(GameData::Resolve(blob, "Dup", META_GAMESYMBOL_KIND_FUNCTION, &address), META_GAMESYMBOL_RVA_OUT_OF_RANGE);
        ASSERT_PTR_NULL(address);
        ASSERT_INT(GameData::Resolve(blob, "OutOfImage", META_GAMESYMBOL_KIND_FUNCTION, &address), META_GAMESYMBOL_RVA_OUT_OF_RANGE);
        ASSERT_FALSE(GameData::RegisterModuleFileSource(blob, SelfPath(), 0x1000, ranges));
        ASSERT_TRUE(GameData::RegisterModuleFileSource(blob, (root / "crc_check.bin").string(), 0x2000, ranges));
        ASSERT_INT(GameData::Resolve(blob, "Func", META_GAMESYMBOL_KIND_FUNCTION, &address), META_GAMESYMBOL_MODULE_NOT_FOUND);
        ASSERT_TRUE(GameData::RegisterModuleFileSource(blob, SelfPath(), 0x2000, {{0x1000, 0x10}}));
        ASSERT_INT(GameData::Resolve(blob, "Func", META_GAMESYMBOL_KIND_FUNCTION, &address), META_GAMESYMBOL_RVA_OUT_OF_RANGE);
        GameData::UnregisterModuleFileSource(blob);
        ASSERT_INT(GameData::GetRegisteredImageSize(blob), 0);
    }
    PASS();

    TEST("gamedata - every status has a description");
    for (int status = META_GAMESYMBOL_OK; status <= META_GAMESYMBOL_CATALOG_CONFLICT; ++status)
        ASSERT_PTR_NOT_NULL(GameData::GetStatusString((meta_gamesymbol_status_t)status));
    ASSERT_STR(GameData::GetStatusString(META_GAMESYMBOL_OK), "success");
    PASS();

    fs::remove_all(root);
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
