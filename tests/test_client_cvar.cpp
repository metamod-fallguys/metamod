#include <extdll.h>
#include "metamod.h"
#include "reg_support.h"
#include "mm_test_stubs.h"
#include "test_common.h"

static int            calls;
static const edict_t* queried_player;
static const char*    queried_name;

static int  player_index(const edict_t*) { return 1; }
static void query(const edict_t* player, const char* name)
{
    ++calls;
    queried_player = player;
    queried_name   = name;
}

int main()
{
    mm_test_reset();
    edict_t player             = {};
    g_engfuncs.pfnIndexOfEdict = player_index;
    TEST("client cvar - available callback receives query");
    g_engfuncs.pfnQueryClientCvarValue = query;
    meta_QueryClientCvarValue(&player, "rate");
    ASSERT_INT(calls, 1);
    ASSERT_PTR_EQ(queried_player, &player);
    ASSERT_STR(queried_name, "rate");
    ASSERT_STR(g_Players.is_querying_cvar(&player), "rate");
    PASS();
    TEST("client cvar - callback cleared at runtime is safe");
    g_engfuncs.pfnQueryClientCvarValue = NULL;
    meta_QueryClientCvarValue(&player, "cl_updaterate");
    ASSERT_INT(calls, 1);
    ASSERT_STR(g_Players.is_querying_cvar(&player), "cl_updaterate");
    g_Players.clear_all_cvar_queries();
    PASS();
    return 0;
}
