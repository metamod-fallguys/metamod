# metamod

Independent metamod component of [metamod-fallguys](https://github.com/hzqst/metamod-fallguys).
Component history and existing Sven Co-op binary interfaces are preserved.

## Build

CMake 3.21+, Git, and Windows MSVC Win32 or Linux i386 multilib are required.

```sh
git clone --recursive https://github.com/metamod-fallguys/metamod.git
cd metamod
cmake -S . -B build/x86/Release -A Win32
cmake --build build/x86/Release --config Release
cmake --install build/x86/Release --config Release
```

On Linux omit `-A Win32` and add `-DCMAKE_BUILD_TYPE=Release`.
For Debug use `build/x86/Debug` and `--config Debug` on Windows, or
`-DCMAKE_BUILD_TYPE=Debug` on Linux. Configure a separate tree for each configuration.
Libraries install to `install/x86/<Config>/addons/metamod/dlls`; Windows installs
matching PDBs. An explicit `CMAKE_INSTALL_PREFIX` or `cmake --install --prefix`
overrides the default. Existing build trees retain their cached install prefix.

Metamod loads every `index.json` under `addons/metamod/gamedata/` before plugins attach
and exposes the merged [GoldSrc_VibeSignatures](https://github.com/hlnd2t/GoldSrc_VibeSignatures)
catalog through `pfnResolveGameSymbol` (interface 5:17). Symbols are looked up by the CRC-64
of the loaded module file and their `symbolName`. `scripts/sync-gamedata.py` is the shared
build-time synchronizer used by plugins through `mmfg_add_gamedata()`.

Dependency CMake variables override environment defaults. An empty path fetches a fixed
commit; invalid explicit paths fail. External source trees are read-only inputs.
Plugins use `METAMOD_SOURCE_PATH` and `ASEXT_SOURCE_PATH` for local clones; SDK imports
do not build either dependency's native library. `ANGELSCRIPT_SOURCE_PATH` selects the
custom SDK. FallGuys additionally accepts `BULLET3_SOURCE_PATH`.
Metamod accepts `CAPSTONE_SOURCE_PATH`, `RAPIDJSON_SOURCE_PATH`, `CHOCOBO1HASH_SOURCE_PATH`
and Linux `PROCMAP_SOURCE_PATH`, using initialized internal submodules before falling back
to FetchContent.

## Formatting

```sh
python -m pip install clang-format==23.1.3
cmake -S . -B build/format -DFORMAT_VALIDATION_ONLY=ON
cmake --build build/format --target format-check
cmake --build build/format --target format
```

`FORMAT_VALIDATION_SOURCE_PATH` may select a local shared tooling clone. Normal builds
do not run formatting or install Python tools. HLSDK and vendor sources are excluded.
The generated `.clang-format` is for editor integration and must not be committed.
