#include <extdll.h>
#include "metamod.h"
#include "mm_test_stubs.h"
#include "test_common.h"

static bool  fail_name;
static void* allocation;
static bool  released;

extern "C" void* __real_calloc(size_t, size_t);
extern "C" char* __real_strdup(const char*);
extern "C" void  __real_free(void*);

extern "C" void* __wrap_calloc(size_t count, size_t size)
{
    void* ptr = __real_calloc(count, size);
    if (fail_name && count == 1 && size == sizeof(cvar_t)) allocation = ptr;
    return ptr;
}
extern "C" char* __wrap_strdup(const char* name)
{
    if (fail_name)
    {
        errno = ENOMEM;
        return NULL;
    }
    return __real_strdup(name);
}
extern "C" void __wrap_free(void* ptr)
{
    if (ptr && ptr == allocation)
    {
        released   = true;
        allocation = NULL;
    }
    __real_free(ptr);
}

int main()
{
    mm_test_reset();
    MRegCvarList list;
    TEST("cvar registration - failed name allocation releases cvar");
    fail_name        = true;
    MRegCvar* failed = list.add("allocation_failure");
    fail_name        = false;
    ASSERT_PTR_NULL(failed);
    ASSERT_INT(meta_errno, ME_NOMEM);
    ASSERT_TRUE(released);
    ASSERT_PTR_NULL(list.find("allocation_failure"));
    PASS();
    TEST("cvar registration - retry succeeds without a stale entry");
    MRegCvar* added = list.add("allocation_failure");
    ASSERT_PTR_NOT_NULL(added);
    ASSERT_PTR_EQ(list.find("allocation_failure"), added);
    ASSERT_STR(added->data->name, "allocation_failure");
    free(added->data->name);
    free(added->data);
    added->data = NULL;
    PASS();
    return 0;
}
