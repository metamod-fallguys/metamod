# metamod

Independent metamod component of [metamod-fallguys](https://github.com/hzqst/metamod-fallguys).
Component history and existing Sven Co-op binary interfaces are preserved.

## Build

CMake 3.21+, Git, and Windows MSVC Win32 or Linux i386 multilib are required.

```sh
git clone --recursive https://github.com/metamod-fallguys/metamod.git
cmake -S metamod -B build -A Win32
cmake --build build --config Release
cmake --install build --config Release --prefix install
```

On Linux omit `-A Win32` and add `-DCMAKE_BUILD_TYPE=Release`. Debug is also supported.
Libraries install to `addons/metamod/dlls`; Windows installs matching PDBs.

Dependency CMake variables override environment defaults. An empty path fetches a fixed
commit; invalid explicit paths fail. External source trees are read-only inputs.
Plugins use `METAMOD_SOURCE_PATH` and `ASEXT_SOURCE_PATH` for local clones; SDK imports
do not build either dependency's native library. `ANGELSCRIPT_SOURCE_PATH` selects the
custom SDK. FallGuys additionally accepts `BULLET3_SOURCE_PATH` and `CAPSTONE_SOURCE_PATH`.
Metamod accepts `CAPSTONE_SOURCE_PATH` and Linux `PROCMAP_SOURCE_PATH`, using initialized
internal submodules before falling back to FetchContent.

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
