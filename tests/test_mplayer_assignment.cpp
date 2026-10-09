#include <extdll.h>
#define private public
#include "mplayer.h"
#undef private
#include "mm_test_stubs.h"
#include "test_common.h"

int main()
{
    mm_test_reset();
    MPlayer  player;
    MPlayer& alias = player;
    TEST("player assignment - empty self-assignment is safe");
    ASSERT_PTR_EQ(&(player = alias), &player);
    ASSERT_PTR_NULL(player.is_querying_cvar());
    PASS();
    TEST("player assignment - self-assignment preserves active query");
    player.set_cvar_query("rate");
    ASSERT_PTR_EQ(&(player = alias), &player);
    ASSERT_STR(player.is_querying_cvar(), "rate");
    PASS();
    TEST("player assignment - distinct players retain independent copies");
    MPlayer copy;
    copy.set_cvar_query("cl_updaterate");
    copy = player;
    player.clear_cvar_query();
    ASSERT_STR(copy.is_querying_cvar(), "rate");
    ASSERT_PTR_NULL(player.is_querying_cvar());
    PASS();
    return 0;
}
