#ifndef GAMEDATA_H
#define GAMEDATA_H

#include <gamesymbol.h>

#include <cstdint>
#include <string>
#include <vector>

// Read-only catalog of GoldSrc_VibeSignatures gamedata (trimmed port of
// MetaHookSv's GameData). Every index.json found under the gamedata root is
// loaded and the records of the current platform form a union keyed by
// (CRC-64/XZ of the module file, symbolName). The catalog is built once and
// never reloaded.
namespace GameData
{
// Collect <root> and every nested directory below it that contains an
// index.json, in a stable (sorted, depth-first) order.
std::vector<std::string> CollectIndexDirs(const std::string& root);

// Build the catalog from the given index directories. Returns true when at
// least one index.json was loaded; per-index and per-snapshot failures are
// recorded as diagnostics.
bool Initialize(const std::vector<std::string>& indexDirs);

bool IsAvailable();

// All catalog diagnostics joined with newlines.
std::string GetDiagnostics();

// Look up a record by module CRC-64 and symbolName.
meta_gamesymbol_status_t QueryByCRC64(uint64_t moduleCRC64, const char* symbolName, meta_gamesymbol_kind_t* outKind, uint32_t* outRva, uint32_t* outSize);

// CRC-64/XZ of a file on disk.
meta_gamesymbol_status_t ComputeFileCRC64(const char* path, uint64_t* outCRC64);

struct ImageRange
{
    uint32_t rva;
    uint32_t size;
};

// Internal source registration for headerless BLOB images. The raw file is
// hashed; ranges describe the already loaded image, including its holes.
bool     RegisterModuleFileSource(void* moduleBase, const std::string& path, uint32_t imageSize, const std::vector<ImageRange>& ranges);
void     UnregisterModuleFileSource(void* moduleBase);
uint32_t GetRegisteredImageSize(void* moduleBase);

// Resolve symbolName in the loaded module at moduleBase to moduleBase + rva.
meta_gamesymbol_status_t Resolve(void* moduleBase, const char* symbolName, meta_gamesymbol_kind_t expectedKind, void** outAddress);

const char* GetStatusString(meta_gamesymbol_status_t status);
} // namespace GameData

#endif
