// Trimmed port of MetaHookSv's GameData (MetaHook/src/GameData.cpp).
//
// Differences from the launcher implementation:
//  - records of the current platform are loaded ("windows" or "linux");
//  - only function and global records are typed, other kinds are kept as
//    unsupported so a query reports UNSUPPORTED_KIND instead of NOT_FOUND;
//  - BLOB engines register their original file and mapped ranges internally;
//  - every index.json under the gamedata root is optional and merged.

// Include Chocobo1Hash before <windows.h>: the Windows min/max macros would
// otherwise clobber std::min/std::max used inside these headers.
#include <crc_64_xz.h>
#include <sha2_256.h>

#include "gamedata.h"

#include <rapidjson/document.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <unordered_map>

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#else
#    include <dlfcn.h>
#endif

namespace
{
// Gamedata contract versions emitted by the upstream dataset generator;
// keep in sync with scripts/sync-gamedata.py.
constexpr int kIndexSchemaVersion            = 4;
constexpr int kSnapshotSchemaVersion         = 5;
constexpr int kSnapshotContractVersion       = 8;
constexpr int kAnalysisOutputContractVersion = 3;

constexpr size_t kCrcChunkSize = 1024 * 1024;

#ifdef _WIN32
constexpr const char* kPlatform = "windows";
#else
constexpr const char* kPlatform = "linux";
#endif

struct GameSymbolRecord
{
    meta_gamesymbol_kind_t kind            = META_GAMESYMBOL_KIND_UNKNOWN;
    uint32_t               rva             = 0;
    uint32_t               size            = 0;
    bool                   unsupportedKind = false;
    bool                   conflicted      = false;
};

struct ModuleCatalog
{
    std::string                                       moduleName;
    std::unordered_map<std::string, GameSymbolRecord> symbols;
};

struct SnapshotDeclaration
{
    std::string url;
    std::string sha256;
};

struct GameDataCatalog
{
    bool                                                              available = false;
    std::unordered_map<uint64_t, ModuleCatalog>                       modules;
    std::unordered_map<std::string, std::vector<SnapshotDeclaration>> declarations;
    std::vector<std::string>                                          diagnostics;
};

GameDataCatalog g_catalog;

std::mutex                          g_moduleCrcMutex;
std::unordered_map<void*, uint64_t> g_moduleCrc;
struct ModuleFileSource
{
    std::string                       path;
    uint32_t                          imageSize;
    std::vector<GameData::ImageRange> ranges;
};
std::unordered_map<void*, ModuleFileSource> g_moduleSources;

void AddDiagnostic(const char* fmt, ...)
{
    char    buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    g_catalog.diagnostics.push_back(buf);
}

int HexDigit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

bool ParseHexU64(const char* s, uint64_t& out)
{
    if (!s || !*s)
        return false;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;
    if (!*s)
        return false;

    uint64_t v = 0;
    for (; *s; ++s)
    {
        int d = HexDigit(*s);
        if (d < 0 || v > (UINT64_MAX - (uint64_t)d) / 16)
            return false;
        v = v * 16 + (uint64_t)d;
    }
    out = v;
    return true;
}

bool ParseHexU32(const char* s, uint32_t& out)
{
    uint64_t v;
    if (!ParseHexU64(s, v) || v > UINT32_MAX)
        return false;
    out = (uint32_t)v;
    return true;
}

bool IsLowerHex(const std::string& s, size_t len)
{
    if (s.size() != len)
        return false;
    for (char c : s)
    {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    }
    return true;
}

bool IsSafeRelativeFilename(const std::string& s)
{
    if (s.empty() || s == "." || s == "..")
        return false;
    for (char c : s)
    {
        if (c == '/' || c == '\\' || c == ':' || (unsigned char)c < 0x20)
            return false;
    }
    return true;
}

std::string JoinPath(const std::string& dir, const std::string& name)
{
    if (dir.empty())
        return name;
    if (dir.back() == '/' || dir.back() == '\\')
        return dir + name;
    return dir + "/" + name;
}

bool ReadFileBytes(const std::string& path, std::vector<char>& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    f.seekg(0, std::ios::end);
    std::streamoff end = f.tellg();
    if (end < 0)
        return false;
    f.seekg(0, std::ios::beg);
    out.resize((size_t)end);
    if (end > 0)
        f.read(out.data(), end);
    return f.good() || f.eof();
}

std::string ComputeSha256Hex(const void* data, size_t size)
{
    Chocobo1::SHA2_256 hasher;
    hasher.addData(data, size);
    hasher.finalize();
    auto digest = hasher.toArray();

    static const char hexdigits[] = "0123456789abcdef";
    std::string       hex;
    hex.reserve(digest.size() * 2);
    for (uint8_t b : digest)
    {
        hex.push_back(hexdigits[b >> 4]);
        hex.push_back(hexdigits[b & 0x0F]);
    }
    return hex;
}

const rapidjson::Value* FindMember(const rapidjson::Value& obj, const char* name)
{
    if (!obj.IsObject())
        return nullptr;
    auto it = obj.FindMember(name);
    return (it != obj.MemberEnd()) ? &it->value : nullptr;
}

const char* FindString(const rapidjson::Value& obj, const char* name)
{
    const rapidjson::Value* v = FindMember(obj, name);
    return (v && v->IsString()) ? v->GetString() : nullptr;
}

bool GetSizeField(const rapidjson::Value& v, uint64_t& out)
{
    if (v.IsUint64())
    {
        out = v.GetUint64();
        return true;
    }
    if (v.IsInt64() && v.GetInt64() >= 0)
    {
        out = (uint64_t)v.GetInt64();
        return true;
    }
    return false;
}

bool NormalizeFunction(const rapidjson::Value& payload, GameSymbolRecord& rec, std::string& error)
{
    const char* funcRva  = FindString(payload, "func_rva");
    const char* funcSize = FindString(payload, "func_size");
    if (!funcRva || !funcSize)
    {
        error = "function payload is missing func_rva/func_size";
        return false;
    }
    if (!ParseHexU32(funcRva, rec.rva))
    {
        error = "invalid func_rva";
        return false;
    }
    if (!ParseHexU32(funcSize, rec.size))
    {
        error = "invalid func_size";
        return false;
    }
    rec.kind = META_GAMESYMBOL_KIND_FUNCTION;
    return true;
}

// A global resolves to the address of the variable itself (gv_rva); the
// consumer decides whether that variable is a pointer to dereference.
bool NormalizeGlobal(const rapidjson::Value& payload, GameSymbolRecord& rec, std::string& error)
{
    const char* gvRva = FindString(payload, "gv_rva");
    if (!gvRva)
    {
        error = "global payload is missing gv_rva";
        return false;
    }
    if (!ParseHexU32(gvRva, rec.rva))
    {
        error = "invalid gv_rva";
        return false;
    }
    rec.kind = META_GAMESYMBOL_KIND_GLOBAL;
    rec.size = 0;
    return true;
}

void AddRecord(uint64_t crc64, const char* moduleName, const char* gameVersion, const char* symbolName, const GameSymbolRecord& rec)
{
    ModuleCatalog& mc = g_catalog.modules[crc64];
    if (mc.moduleName.empty())
        mc.moduleName = moduleName;
    else if (mc.moduleName != moduleName)
        AddDiagnostic("crc64 %016llx is referenced by both module '%s' and '%s'", (unsigned long long)crc64, mc.moduleName.c_str(), moduleName);

    auto it = mc.symbols.find(symbolName);
    if (it == mc.symbols.end())
    {
        mc.symbols.emplace(symbolName, rec);
        return;
    }

    GameSymbolRecord& existing = it->second;
    if (existing.kind != rec.kind || existing.rva != rec.rva || existing.size != rec.size || existing.unsupportedKind != rec.unsupportedKind)
    {
        existing.conflicted = true;
        AddDiagnostic("symbol '%s' (crc64 %016llx, %s) has conflicting duplicate records", symbolName, (unsigned long long)crc64, gameVersion);
    }
}

void LoadSnapshot(const std::string& indexDir, const char* gameVersion, const char* url, const char* expectedSha256, uint64_t expectedSize)
{
    std::string       path = JoinPath(indexDir, url);
    std::vector<char> data;
    if (!ReadFileBytes(path, data))
    {
        AddDiagnostic("snapshot '%s': file missing or unreadable: %s", gameVersion, path.c_str());
        return;
    }
    if (data.size() != expectedSize)
    {
        AddDiagnostic("snapshot '%s': size mismatch (expected %llu, got %zu): %s", gameVersion, (unsigned long long)expectedSize, data.size(), path.c_str());
        return;
    }
    std::string sha256 = ComputeSha256Hex(data.data(), data.size());
    if (sha256 != expectedSha256)
    {
        AddDiagnostic("snapshot '%s': sha256 mismatch (expected %s, got %s): %s", gameVersion, expectedSha256, sha256.c_str(), path.c_str());
        return;
    }

    rapidjson::Document doc;
    if (doc.Parse(data.data(), data.size()).HasParseError() || !doc.IsObject())
    {
        AddDiagnostic("snapshot '%s': malformed json: %s", gameVersion, path.c_str());
        return;
    }

    const rapidjson::Value* schemaVersion = FindMember(doc, "schemaVersion");
    if (!schemaVersion || !schemaVersion->IsInt() || schemaVersion->GetInt() != kSnapshotSchemaVersion)
    {
        AddDiagnostic("snapshot '%s': unsupported schemaVersion", gameVersion);
        return;
    }
    const rapidjson::Value* source = FindMember(doc, "source");
    if (!source || !source->IsObject())
    {
        AddDiagnostic("snapshot '%s': missing source object", gameVersion);
        return;
    }
    const rapidjson::Value* sourceSchema = FindMember(*source, "snapshotSchemaVersion");
    if (!sourceSchema || !sourceSchema->IsInt() || sourceSchema->GetInt() != kSnapshotContractVersion)
    {
        AddDiagnostic("snapshot '%s': unsupported source.snapshotSchemaVersion", gameVersion);
        return;
    }
    const rapidjson::Value* analysisContract = FindMember(*source, "analysisOutputContractVersion");
    if (!analysisContract || !analysisContract->IsInt() || analysisContract->GetInt() != kAnalysisOutputContractVersion)
    {
        AddDiagnostic("snapshot '%s': unsupported source.analysisOutputContractVersion", gameVersion);
        return;
    }

    const rapidjson::Value* binaries = FindMember(doc, "binaries");
    if (!binaries || !binaries->IsObject())
    {
        AddDiagnostic("snapshot '%s': missing binaries object", gameVersion);
        return;
    }

    std::unordered_map<std::string, uint64_t> moduleCrc64;
    for (auto it = binaries->MemberBegin(); it != binaries->MemberEnd(); ++it)
    {
        const rapidjson::Value* platformEntry = FindMember(it->value, kPlatform);
        if (!platformEntry || !platformEntry->IsObject())
            continue;
        const char* crc64 = FindString(*platformEntry, "crc64");
        uint64_t    value = 0;
        if (!crc64 || !ParseHexU64(crc64, value))
        {
            AddDiagnostic("snapshot '%s': invalid %s crc64 for module '%s'", gameVersion, kPlatform, it->name.GetString());
            continue;
        }
        moduleCrc64[it->name.GetString()] = value;
    }

    const rapidjson::Value* records = FindMember(doc, "records");
    if (!records || !records->IsArray())
    {
        AddDiagnostic("snapshot '%s': missing records array", gameVersion);
        return;
    }

    for (const auto& rec : records->GetArray())
    {
        const char* platform = FindString(rec, "platform");
        if (!platform)
        {
            AddDiagnostic("snapshot '%s': a records entry is missing platform", gameVersion);
            continue;
        }
        if (strcmp(platform, kPlatform) != 0)
            continue;

        const char* module     = FindString(rec, "module");
        const char* symbolName = FindString(rec, "symbolName");
        const char* kind       = FindString(rec, "kind");
        if (!module || !symbolName || !kind)
        {
            AddDiagnostic("snapshot '%s': a records entry is missing module/symbolName/kind", gameVersion);
            continue;
        }

        auto crcIt = moduleCrc64.find(module);
        if (crcIt == moduleCrc64.end())
        {
            AddDiagnostic("snapshot '%s': record '%s' references unknown module '%s'", gameVersion, symbolName, module);
            continue;
        }

        const rapidjson::Value* payload = FindMember(rec, "payload");
        GameSymbolRecord        record;
        std::string             error;
        bool                    ok = true;
        if (!strcmp(kind, "function"))
            ok = payload && NormalizeFunction(*payload, record, error);
        else if (!strcmp(kind, "global"))
            ok = payload && NormalizeGlobal(*payload, record, error);
        else
            record.unsupportedKind = true;

        if (!ok)
        {
            AddDiagnostic("snapshot '%s': symbol '%s': %s", gameVersion, symbolName, error.empty() ? "payload must be an object" : error.c_str());
            continue;
        }

        AddRecord(crcIt->second, module, gameVersion, symbolName, record);
    }
}

bool LoadIndexDir(const std::string& indexDir)
{
    std::string       indexPath = JoinPath(indexDir, "index.json");
    std::vector<char> data;
    if (!ReadFileBytes(indexPath, data))
    {
        AddDiagnostic("index.json missing or unreadable: %s", indexPath.c_str());
        return false;
    }

    rapidjson::Document index;
    if (index.Parse(data.data(), data.size()).HasParseError() || !index.IsObject())
    {
        AddDiagnostic("index.json is malformed: %s", indexPath.c_str());
        return false;
    }

    const rapidjson::Value* schemaVersion = FindMember(index, "schemaVersion");
    if (!schemaVersion || !schemaVersion->IsInt() || schemaVersion->GetInt() != kIndexSchemaVersion)
    {
        AddDiagnostic("index.json: unsupported schemaVersion (expected %d): %s", kIndexSchemaVersion, indexPath.c_str());
        return false;
    }

    const rapidjson::Value* versions = FindMember(index, "versions");
    if (!versions || !versions->IsArray())
    {
        AddDiagnostic("index.json: 'versions' must be an array: %s", indexPath.c_str());
        return false;
    }

    for (const auto& v : versions->GetArray())
    {
        const char*             gameVersion = FindString(v, "gameVersion");
        const char*             url         = FindString(v, "url");
        const char*             sha256      = FindString(v, "sha256");
        const rapidjson::Value* sizeValue   = FindMember(v, "size");
        uint64_t                size        = 0;
        if (!gameVersion || !*gameVersion || !url || !sha256 || !sizeValue || !GetSizeField(*sizeValue, size))
        {
            AddDiagnostic("index.json: a versions entry is missing gameVersion/url/sha256/size: %s", indexPath.c_str());
            continue;
        }
        if (!IsSafeRelativeFilename(url))
        {
            AddDiagnostic("index.json: unsafe url '%s' for '%s': %s", url, gameVersion, indexPath.c_str());
            continue;
        }
        if (!IsLowerHex(sha256, 64))
        {
            AddDiagnostic("index.json: invalid sha256 for '%s': %s", gameVersion, indexPath.c_str());
            continue;
        }

        // Several indexes may declare the same gameVersion (one per plugin);
        // every distinct file is merged, a byte-identical one is skipped.
        auto& declarations = g_catalog.declarations[gameVersion];
        bool  duplicate    = std::any_of(declarations.begin(), declarations.end(), [&](const SnapshotDeclaration& d) {
            return d.url == url && d.sha256 == sha256;
        });
        if (duplicate)
            continue;
        declarations.push_back(SnapshotDeclaration{url, sha256});

        LoadSnapshot(indexDir, gameVersion, url, sha256, size);
    }

    return true;
}

bool GetModulePath(void* moduleBase, std::string& outPath)
{
    auto source = g_moduleSources.find(moduleBase);
    if (source != g_moduleSources.end())
    {
        outPath = source->second.path;
        return true;
    }
#ifdef _WIN32
    char  path[MAX_PATH];
    DWORD len = GetModuleFileNameA((HMODULE)moduleBase, path, sizeof(path));
    if (len == 0 || len >= sizeof(path))
        return false;
    outPath.assign(path, len);
    return true;
#else
    Dl_info info;
    memset(&info, 0, sizeof(info));
    if (!dladdr(moduleBase, &info) || !info.dli_fname || info.dli_fbase != moduleBase)
        return false;
    outPath = info.dli_fname;
    return true;
#endif
}

// Size of the mapped image, or 0 when unknown. Only PE headers carry a
// reliable SizeOfImage; ELF images are not bounds-checked.
uint32_t GetModuleImageSize(void* moduleBase)
{
    auto source = g_moduleSources.find(moduleBase);
    if (source != g_moduleSources.end())
        return source->second.imageSize;
#ifdef _WIN32
    auto dos = (const IMAGE_DOS_HEADER*)moduleBase;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;
    auto nt = (const IMAGE_NT_HEADERS*)((const char*)moduleBase + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return 0;
    return nt->OptionalHeader.SizeOfImage;
#else
    (void)moduleBase;
    return 0;
#endif
}

meta_gamesymbol_status_t GetModuleCRC64(void* moduleBase, uint64_t& outCRC64)
{
    std::lock_guard<std::mutex> lock(g_moduleCrcMutex);

    auto cached = g_moduleCrc.find(moduleBase);
    if (cached != g_moduleCrc.end())
    {
        outCRC64 = cached->second;
        return META_GAMESYMBOL_OK;
    }

    std::string path;
    if (!GetModulePath(moduleBase, path))
        return META_GAMESYMBOL_MODULE_PATH_UNAVAILABLE;

    uint64_t                 crc64  = 0;
    meta_gamesymbol_status_t status = GameData::ComputeFileCRC64(path.c_str(), &crc64);
    if (status != META_GAMESYMBOL_OK)
        return status;

    g_moduleCrc[moduleBase] = crc64;
    outCRC64                = crc64;
    return META_GAMESYMBOL_OK;
}

void CollectIndexDirsRecursive(const std::filesystem::path& dir, std::vector<std::string>& out)
{
    std::error_code ec;
    if (std::filesystem::is_regular_file(dir / "index.json", ec))
        out.push_back(dir.string());

    std::vector<std::filesystem::path> children;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        if (it->is_directory(ec) && !it->is_symlink(ec))
            children.push_back(it->path());
    }
    std::sort(children.begin(), children.end());
    for (const auto& child : children)
        CollectIndexDirsRecursive(child, out);
}
} // namespace

namespace GameData
{
bool RegisterModuleFileSource(void* moduleBase, const std::string& path, uint32_t imageSize, const std::vector<ImageRange>& ranges)
{
    if (!moduleBase || path.empty() || !imageSize || ranges.empty() || uintptr_t(moduleBase) > UINTPTR_MAX - imageSize)
        return false;
    for (const auto& range : ranges)
        if (!range.size || range.rva >= imageSize || range.size > imageSize - range.rva)
            return false;
    std::lock_guard<std::mutex> lock(g_moduleCrcMutex);
    g_moduleCrc.erase(moduleBase);
    g_moduleSources[moduleBase] = ModuleFileSource{path, imageSize, ranges};
    return true;
}

void UnregisterModuleFileSource(void* moduleBase)
{
    std::lock_guard<std::mutex> lock(g_moduleCrcMutex);
    g_moduleCrc.erase(moduleBase);
    g_moduleSources.erase(moduleBase);
}

uint32_t GetRegisteredImageSize(void* moduleBase)
{
    std::lock_guard<std::mutex> lock(g_moduleCrcMutex);
    auto                        source = g_moduleSources.find(moduleBase);
    return source == g_moduleSources.end() ? 0 : source->second.imageSize;
}

std::vector<std::string> CollectIndexDirs(const std::string& root)
{
    std::vector<std::string> dirs;
    std::error_code          ec;
    if (!root.empty() && std::filesystem::is_directory(root, ec))
        CollectIndexDirsRecursive(std::filesystem::path(root), dirs);
    return dirs;
}

bool Initialize(const std::vector<std::string>& indexDirs)
{
    g_catalog = GameDataCatalog{};
    {
        std::lock_guard<std::mutex> lock(g_moduleCrcMutex);
        g_moduleCrc.clear();
    }

    bool anyLoaded = false;
    for (const auto& dir : indexDirs)
    {
        if (LoadIndexDir(dir))
            anyLoaded = true;
    }
    if (!anyLoaded)
        AddDiagnostic("no valid gamedata index.json was found");

    g_catalog.available = anyLoaded;
    return anyLoaded;
}

bool IsAvailable()
{
    return g_catalog.available;
}

std::string GetDiagnostics()
{
    std::string result;
    for (const auto& diagnostic : g_catalog.diagnostics)
    {
        if (!result.empty())
            result += '\n';
        result += diagnostic;
    }
    return result;
}

meta_gamesymbol_status_t QueryByCRC64(uint64_t moduleCRC64, const char* symbolName, meta_gamesymbol_kind_t* outKind, uint32_t* outRva, uint32_t* outSize)
{
    if (!symbolName || !*symbolName || !outKind || !outRva || !outSize)
        return META_GAMESYMBOL_INVALID_ARGUMENT;
    if (!g_catalog.available)
        return META_GAMESYMBOL_GAMEDATA_UNAVAILABLE;

    auto moduleIt = g_catalog.modules.find(moduleCRC64);
    if (moduleIt == g_catalog.modules.end())
        return META_GAMESYMBOL_MODULE_NOT_FOUND;

    auto symbolIt = moduleIt->second.symbols.find(symbolName);
    if (symbolIt == moduleIt->second.symbols.end())
        return META_GAMESYMBOL_SYMBOL_NOT_FOUND;

    const GameSymbolRecord& rec = symbolIt->second;
    if (rec.conflicted)
        return META_GAMESYMBOL_CATALOG_CONFLICT;
    if (rec.unsupportedKind)
        return META_GAMESYMBOL_UNSUPPORTED_KIND;

    *outKind = rec.kind;
    *outRva  = rec.rva;
    *outSize = rec.size;
    return META_GAMESYMBOL_OK;
}

meta_gamesymbol_status_t ComputeFileCRC64(const char* path, uint64_t* outCRC64)
{
    if (!path || !outCRC64)
        return META_GAMESYMBOL_INVALID_ARGUMENT;

    std::ifstream f(path, std::ios::binary);
    if (!f)
        return META_GAMESYMBOL_MODULE_HASH_FAILED;

    Chocobo1::CRC_64_XZ crc;
    std::vector<char>   buf(kCrcChunkSize);
    while (f)
    {
        f.read(buf.data(), (std::streamsize)buf.size());
        std::streamsize got = f.gcount();
        if (got > 0)
            crc.addData(buf.data(), (size_t)got);
    }
    if (f.bad())
        return META_GAMESYMBOL_MODULE_HASH_FAILED;

    crc.finalize();
    *outCRC64 = static_cast<uint64_t>(crc);
    return META_GAMESYMBOL_OK;
}

meta_gamesymbol_status_t Resolve(void* moduleBase, const char* symbolName, meta_gamesymbol_kind_t expectedKind, void** outAddress)
{
    if (!outAddress)
        return META_GAMESYMBOL_INVALID_ARGUMENT;
    *outAddress = nullptr;

    if (!moduleBase || !symbolName || !*symbolName)
        return META_GAMESYMBOL_INVALID_ARGUMENT;
    if (expectedKind != META_GAMESYMBOL_KIND_FUNCTION && expectedKind != META_GAMESYMBOL_KIND_GLOBAL)
        return META_GAMESYMBOL_INVALID_ARGUMENT;
    if (!g_catalog.available)
        return META_GAMESYMBOL_GAMEDATA_UNAVAILABLE;

    uint64_t                 crc64  = 0;
    meta_gamesymbol_status_t status = GetModuleCRC64(moduleBase, crc64);
    if (status != META_GAMESYMBOL_OK)
        return status;

    meta_gamesymbol_kind_t kind = META_GAMESYMBOL_KIND_UNKNOWN;
    uint32_t               rva  = 0;
    uint32_t               size = 0;
    status                      = QueryByCRC64(crc64, symbolName, &kind, &rva, &size);
    if (status != META_GAMESYMBOL_OK)
        return status;
    if (kind != expectedKind)
        return META_GAMESYMBOL_KIND_MISMATCH;

    std::lock_guard<std::mutex> lock(g_moduleCrcMutex);
    uint32_t                    imageSize = GetModuleImageSize(moduleBase);
    if (imageSize != 0 && (rva >= imageSize || size > imageSize - rva))
        return META_GAMESYMBOL_RVA_OUT_OF_RANGE;
    auto source = g_moduleSources.find(moduleBase);
    if (source != g_moduleSources.end())
    {
        const uint32_t width     = size ? size : (kind == META_GAMESYMBOL_KIND_GLOBAL ? sizeof(uint32_t) : 1);
        bool           contained = false;
        for (const auto& range : source->second.ranges)
            if (rva >= range.rva && rva - range.rva < range.size && width <= range.size - (rva - range.rva))
                contained = true;
        if (!contained)
            return META_GAMESYMBOL_RVA_OUT_OF_RANGE;
    }

    *outAddress = (char*)moduleBase + rva;
    return META_GAMESYMBOL_OK;
}

const char* GetStatusString(meta_gamesymbol_status_t status)
{
    switch (status)
    {
        case META_GAMESYMBOL_OK: return "success";
        case META_GAMESYMBOL_INVALID_ARGUMENT: return "invalid argument";
        case META_GAMESYMBOL_GAMEDATA_UNAVAILABLE: return "gamedata is not available";
        case META_GAMESYMBOL_MODULE_PATH_UNAVAILABLE: return "module file path is unavailable";
        case META_GAMESYMBOL_MODULE_HASH_FAILED: return "failed to hash the module file";
        case META_GAMESYMBOL_MODULE_NOT_FOUND: return "module was not found in the gamedata";
        case META_GAMESYMBOL_SYMBOL_NOT_FOUND: return "symbol was not found in the matched gamedata snapshot";
        case META_GAMESYMBOL_UNSUPPORTED_KIND: return "symbol kind is not supported";
        case META_GAMESYMBOL_KIND_MISMATCH: return "symbol kind does not match the expected kind";
        case META_GAMESYMBOL_RVA_OUT_OF_RANGE: return "symbol rva is out of the module image bounds";
        case META_GAMESYMBOL_CATALOG_CONFLICT: return "symbol has conflicting duplicate records";
        default: return "unknown game symbol status";
    }
}
} // namespace GameData
