# Hook cache semantics and verification

This ports the per-API filtering in upstream `4f81e29` to all four API groups,
including Studio Blending. Each group has pre/post lists containing running
plugins with a non-null table, in plugin-slot order. Individual function slots
are still checked at dispatch time.

`MPluginList::HookIterator` adapts the safety requirements of `8d1d42e` and
`c442d44` using a generation per list owner and a snapshot per cursor instead
of a shared boolean. Every rebuild attempt advances the generation. Before
reading cached storage again, each suspended cursor independently notices the
change and resumes strictly after its last plugin slot. Removing/reusing that
slot does not repeat it; enabling an earlier slot takes effect in the next
phase or dispatch. Nested calls cannot consume the outer cursor's notification.
The original return-value handling and `PublicMetaGlobals` save/restore remain
unchanged. These lists retain the existing single-engine-thread assumption;
the generation is not a concurrency primitive.

Load, unload, pause, unpause, and resetting a running slot rebuild the lists.
Bulk lifecycle operations reach those same methods. Post cursors are created
after the original engine/GameDLL call, so changes there also take effect.

If allocation fails, the old allocation remains owned but is not used for
dispatch. Existing and new cursors scan current plugin slots until a later
successful rebuild. This avoids both stale-pointer access and silently skipped
hooks. Empty lists release their allocation. The destructor uses the platform
calling convention, not the internal regparm convention.

## Regression coverage

`mmfg_test_api_hook` links the real dispatcher and API metadata. It checks all
eight lists, null tables/functions, non-running states, pre/post order, return
and MRES behavior, full capacity, pause/unpause-all, and changes to current,
earlier, and later slots. Same-group and cross-group nested calls rebuild lists
while outer cursors are suspended. A loadable fixture exercises real load,
unload, failed attach/detach, retry and reload, plus the existing PT_NEVER guard.

Linux uses linker wrapping to force realloc to move or fail, including failure
inside nested callbacks and real load/unload operations. Tests check scan
fallback and recovery during an active dispatch. ASan/UBSan and leak detection
cover this path; the Windows suite exercises lifecycle/reentry without those
Linux-specific fault-injection hooks. The companion Studio cleanup regression
checks that clear can be repeated without leaked tables or a dangling copy.

Validated: Linux i686 and Windows Win32 Release builds, 12 Linux tests with
ASan/UBSan and leak detection, 5 Windows tests, and format-check. No live Sven
Co-op server validation was performed.

## Reproduce the comparison (Linux, from the component root)

The optional baseline target changes only the dispatcher translation unit;
the harness, support code and compiler flags are shared. Keep sanitizers off
for timing; use the normal sanitized build for correctness.

```sh
mkdir -p build-tests/hook-benchmark
git show 0d4ff98:src/api_hook.cpp > build-tests/hook-benchmark/baseline_api_hook.cpp
cmake -S . -B build-tests/hook-benchmark \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32 \
  -DMMFG_METAMOD_BUILD_TESTS=ON -DMMFG_METAMOD_TESTS_SANITIZE=OFF \
  -DMMFG_METAMOD_HOOK_BASELINE_SOURCE="$PWD/build-tests/hook-benchmark/baseline_api_hook.cpp"
cmake --build build-tests/hook-benchmark \
  --target mmfg_test_api_hook mmfg_test_api_hook_baseline -j 6
ctest --test-dir build-tests/hook-benchmark -R api_hook --output-on-failure
python3 tests/benchmark_hook_dispatch.py build-tests/hook-benchmark
```

The script pins both processes to one available CPU, alternates the binaries
five times, and prints median/min/max. Each case dispatches one million void
GameDLL calls with pre/post phases and one original call. All slots are running;
non-providers have null tables. Providers implement the selected function in
both phases. Recording is disabled during timing; lightweight callback state
bookkeeping is identical in both binaries. Cache rebuilds occur outside the
timed loop. This measures dispatcher overhead, not plugin workload or server FPS.

Reference run: WSL Ubuntu 24.04, GCC 13.3, i9-13900K, i386 Release test targets
(`-m32 -O3 -DNDEBUG`, sanitizers off). Median nanoseconds per dispatch:

| Plugin slots | Table providers | Original | Cached |
| ---: | ---: | ---: | ---: |
| 3 | 3 | 24.81 | 23.99 |
| 32 | 3 | 42.46 | 23.99 |
| 32 | 32 | 216.89 | 191.14 |
| 256 | 3 | 227.53 | 24.06 |
| 256 | 256 | 1517.78 | 1431.11 |

Sparse tables benefit most. Small-list results can vary by a few nanoseconds
with code layout and CPU conditions; earlier runs also showed small regressions.
No universal speedup or timing threshold is asserted. Lifecycle rebuild cost
and live-server performance are outside this microbenchmark.
