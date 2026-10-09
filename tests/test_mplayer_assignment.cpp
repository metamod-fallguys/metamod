#include <extdll.h>
#include "mplayer.h"
#include "mm_test_stubs.h"
#include "test_common.h"

// Explicit template instantiation may name private members. Keep the actual
// declaration private: MSVC includes member access in its mangled symbol name.
struct Assignment
{
    typedef MPlayer& (MPlayer::*type)(const MPlayer&)DLLINTERNAL_NOVIS;
    friend type assignment(Assignment);
};
template <Assignment::type Method>
struct AssignmentAccess
{
    friend Assignment::type assignment(Assignment) { return Method; }
};
template struct AssignmentAccess<&MPlayer::operator= >;

int main()
{
    mm_test_reset();
    MPlayer  player;
    MPlayer& alias  = player;
    auto     assign = assignment(Assignment{});
    TEST("player assignment - empty self-assignment is safe");
    ASSERT_PTR_EQ(&(player.*assign)(alias), &player);
    ASSERT_PTR_NULL(player.is_querying_cvar());
    PASS();
    TEST("player assignment - self-assignment preserves active query");
    player.set_cvar_query("rate");
    ASSERT_PTR_EQ(&(player.*assign)(alias), &player);
    ASSERT_STR(player.is_querying_cvar(), "rate");
    PASS();
    TEST("player assignment - distinct players retain independent copies");
    MPlayer copy;
    copy.set_cvar_query("cl_updaterate");
    (copy.*assign)(player);
    player.clear_cvar_query();
    ASSERT_STR(copy.is_querying_cvar(), "rate");
    ASSERT_PTR_NULL(player.is_querying_cvar());
    PASS();
    return 0;
}
