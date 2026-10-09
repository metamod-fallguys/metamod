#include <extdll.h>
#include "metamod.h"
#include "mm_test_stubs.h"
#include "test_common.h"

#ifdef MMFG_TEST_WRAP_STRDUP
static bool      fail_name;
extern "C" char* __real_strdup(const char*);
extern "C" char* __wrap_strdup(const char* name)
{
    if (fail_name) return NULL;
    return __real_strdup(name);
}
#endif

int main()
{
    mm_test_reset();
    MRegMsgList list;
    TEST("usermsg - empty name is completed and owns the caller's text");
    MRegMsg*    empty    = list.add("", 80, -1);
    const char* previous = empty->name;
    char        name[]   = "SomeMessage";
    ASSERT_PTR_EQ(empty, list.add(name, 80, -1));
    name[0] = 'X';
    ASSERT_STR("SomeMessage", empty->name);
    ASSERT_STR("", previous);
    ASSERT_PTR_EQ(empty, list.find("SomeMessage"));
    ASSERT_PTR_EQ(empty, list.find(80));
    const char* saved = empty->name;
    ASSERT_PTR_EQ(empty, list.add("SomeMessage", 80, -1));
    ASSERT_PTR_EQ(saved, empty->name);
    ASSERT_PTR_EQ(empty, list.add("Different", 80, 4));
    ASSERT_PTR_EQ(saved, empty->name);
    ASSERT_INT(-1, empty->size);
    ASSERT_PTR_EQ(empty, list.add("", 80, 0));
    ASSERT_PTR_EQ(saved, empty->name);
    PASS();

    TEST("usermsg - null names and fresh registration");
    MRegMsg* unnamed = list.add(NULL, 81, 2);
    ASSERT_PTR_NOT_NULL(unnamed);
    ASSERT_PTR_NULL(list.find(static_cast<const char*>(NULL)));
    list.show();
    ASSERT_PTR_EQ(unnamed, list.add("Completed", 81, 2));
    ASSERT_STR("Completed", unnamed->name);
    char     fresh[] = "Fresh";
    MRegMsg* added   = list.add(fresh, 82, 3);
    fresh[0]         = 'X';
    ASSERT_STR("Fresh", added->name);
    PASS();

#ifdef MMFG_TEST_WRAP_STRDUP
    TEST("usermsg - allocation failure preserves entries and permits retry");
    MRegMsg* pending = list.add("", 83, 5);
    fail_name        = true;
    ASSERT_PTR_NULL(list.add("Retry", 83, 5));
    ASSERT_INT(ME_NOMEM, meta_errno);
    ASSERT_STR("", pending->name);
    ASSERT_PTR_NULL(list.add("NewRetry", 84, 6));
    ASSERT_INT(ME_NOMEM, meta_errno);
    ASSERT_PTR_NULL(list.find(84));
    fail_name = false;
    ASSERT_PTR_EQ(pending, list.add("Retry", 83, 5));
    ASSERT_PTR_NOT_NULL(list.add("NewRetry", 84, 6));
    PASS();
#endif
    TEST("usermsg - a full registry still completes existing entries");
    {
        MRegMsgList full;
        for (int i = 0; i < MAX_REG_MSGS; i++)
            ASSERT_PTR_NOT_NULL(full.add("", i + 64, -1));
        MRegMsg* first = full.find(64);
        ASSERT_PTR_EQ(first, full.add("FullRegistry", 64, -1));
        ASSERT_STR("FullRegistry", first->name);
        ASSERT_PTR_NULL(full.add("Overflow", MAX_REG_MSGS + 64, -1));
        ASSERT_INT(ME_MAXREACHED, meta_errno);
    }
    PASS();
    return 0;
}
