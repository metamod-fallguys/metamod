#ifndef GAMESYMBOL_H
#define GAMESYMBOL_H

// Game symbol catalog types shared by metamod and its plugins.
//
// The catalog is built from GoldSrc_VibeSignatures gamedata installed under
// <gamedir>/addons/metamod/gamedata/ and is keyed by
// (CRC-64/XZ of the module file, symbolName). Values match MetaHookSv's
// mh_gamesymbol_kind_t / mh_gamesymbol_status_t.

typedef enum meta_gamesymbol_kind_e
{
    META_GAMESYMBOL_KIND_UNKNOWN  = 0,
    META_GAMESYMBOL_KIND_FUNCTION = 1,
    META_GAMESYMBOL_KIND_GLOBAL   = 2,
} meta_gamesymbol_kind_t;

typedef enum meta_gamesymbol_status_e
{
    META_GAMESYMBOL_OK                      = 0,
    META_GAMESYMBOL_INVALID_ARGUMENT        = 1,
    META_GAMESYMBOL_GAMEDATA_UNAVAILABLE    = 3,
    META_GAMESYMBOL_MODULE_PATH_UNAVAILABLE = 4,
    META_GAMESYMBOL_MODULE_HASH_FAILED      = 5,
    META_GAMESYMBOL_MODULE_NOT_FOUND        = 6,
    META_GAMESYMBOL_SYMBOL_NOT_FOUND        = 7,
    META_GAMESYMBOL_UNSUPPORTED_KIND        = 8,
    META_GAMESYMBOL_KIND_MISMATCH           = 9,
    META_GAMESYMBOL_RVA_OUT_OF_RANGE        = 10,
    META_GAMESYMBOL_CATALOG_CONFLICT        = 11,
} meta_gamesymbol_status_t;

#endif
