// Exercise failed attachment without changing the production class interface.
#include <extdll.h>
#include <meta_api.h>
#define private public
#include "mplugin.h"
#undef private
#include "metamod.h"
#include "mm_test_stubs.h"
#include "test_common.h"

static int  attach_calls;
static bool received_tables;

extern "C" __attribute__((visibility("default"))) int Meta_Attach(
    PLUG_LOADTIME,
    META_FUNCTIONS*,
    meta_globals_t*,
    gamedll_funcs_t* funcs)
{
    ++attach_calls;
    received_tables = funcs->dllapi_table && funcs->newapi_table && funcs->studio_blend_api;
    return FALSE;
}

static int check_failure(const char* library, META_ERRNO expected)
{
    MPlugin plugin = {};
    plugin.handle  = dlopen(library, RTLD_NOW);
    ASSERT_PTR_NOT_NULL(plugin.handle);
    // Repeated failure must not retain allocations or double-free old tables.
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        ASSERT_FALSE(plugin.attach(PT_ANYTIME));
        ASSERT_INT(meta_errno, expected);
        ASSERT_PTR_NULL(plugin.gamedll_funcs.dllapi_table);
        ASSERT_PTR_NULL(plugin.gamedll_funcs.newapi_table);
        ASSERT_PTR_NULL(plugin.gamedll_funcs.studio_blend_api);
    }
    dlclose(plugin.handle);
    return 0;
}

int main()
{
    mm_test_reset();
    TEST("attach - missing symbol releases all three tables");
    if (check_failure(MMFG_TEST_FAKE_GAMEDLL, ME_DLMISSING)) return 1;
    ASSERT_INT(attach_calls, 0);
    PASS();
    TEST("attach - rejected attachment releases all three tables");
    if (check_failure(NULL, ME_DLERROR)) return 1;
    ASSERT_INT(attach_calls, 2);
    ASSERT_TRUE(received_tables);
    PASS();
    return 0;
}
