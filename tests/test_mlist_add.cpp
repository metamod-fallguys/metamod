//
// metamod - regression tests for MPluginList::add()
//
// add() copies a plugin into a free slot and has to rebuild the "file" pointer
// so that it addresses the copy. It used to do that by offsetting from
// filename[], but a plugin that has been through MPlugin::resolve() keeps file
// pointing into pathname[] instead, so the offset came from a different array
// and the result addressed neither of the copies.
//

#include <stdlib.h>
#include <string.h>

#include <new>

#include <extdll.h>

#include "mlist.h"

#include "mm_test_stubs.h"
#include "test_common.h"

// MPluginList holds raw MPlugin records and its constructor calls
// MPlugin::free_api_pointers() on every slot, so the storage has to be zeroed
// before the constructor runs. class_metamod_new::operator new() allocates with
// calloc(), which is exactly the state the module's global list starts in.
static MPluginList* make_zeroed_list(void)
{
    return new MPluginList("addons/metamod/plugins.ini");
}

static void free_list(MPluginList* list)
{
    delete list;
}

// Shape of a plugin that has been through resolve(): file points into pathname.
static void make_resolved_plugin(MPlugin* plug, const char* filename, const char* pathname)
{
    char* cp;

    memset(plug, 0, sizeof(*plug));
    plug->index  = 1;
    plug->status = PL_VALID;
    STRNCPY(plug->filename, filename, sizeof(plug->filename));
    STRNCPY(plug->pathname, pathname, sizeof(plug->pathname));
    STRNCPY(plug->desc, "Test Plugin", sizeof(plug->desc));
    cp         = strrchr(plug->pathname, '/');
    plug->file = cp ? cp + 1 : plug->pathname;
}

static int test_add_file_pointer_from_pathname(void)
{
    MPluginList* list = make_zeroed_list();
    MPlugin      padd;
    MPlugin*     iplug;
    int          fail;

    make_resolved_plugin(&padd, "dlls/mm_test_i386.so",
                         "/srv/half-life/cstrike/dlls/mm_test_i386.so");

    TEST("MPluginList::add - file lands inside the copied pathname");
    iplug = list->add(&padd);
    fail  = 0;
    if (!iplug)
        fail = 1;
    else
    {
        fail |= (iplug->file < iplug->pathname) || (iplug->file >= iplug->pathname + sizeof(iplug->pathname));
        fail |= (strcmp(iplug->file, "mm_test_i386.so") != 0);
        fail |= (strcmp(iplug->filename, padd.filename) != 0);
        fail |= (strcmp(iplug->pathname, padd.pathname) != 0);
        fail |= (strcmp(iplug->desc, padd.desc) != 0);
    }
    if (fail)
    {
        printf("FAIL\n    file pointer not rebuilt from the copied pathname\n");
        free_list(list);
        return 1;
    }
    PASS();
    free_list(list);
    return 0;
}

// pathname without a directory component: the fallback is the copied filename.
static int test_add_file_pointer_falls_back_to_filename(void)
{
    MPluginList* list = make_zeroed_list();
    MPlugin      padd;
    MPlugin*     iplug;
    int          fail;

    make_resolved_plugin(&padd, "dlls/mm_test_i386.so", "mm_test_i386.so");

    TEST("MPluginList::add - pathname without a slash falls back to filename");
    iplug = list->add(&padd);
    fail  = 0;
    if (!iplug)
        fail = 1;
    else
    {
        fail |= (iplug->file < iplug->filename) || (iplug->file >= iplug->filename + sizeof(iplug->filename));
        fail |= (strcmp(iplug->file, "mm_test_i386.so") != 0);
    }
    if (fail)
    {
        printf("FAIL\n    file pointer not rebuilt from the copied filename\n");
        free_list(list);
        return 1;
    }
    PASS();
    free_list(list);
    return 0;
}

// Neither name has a directory component: file is the copied filename itself.
static int test_add_file_pointer_without_directory(void)
{
    MPluginList* list = make_zeroed_list();
    MPlugin      padd;
    MPlugin*     iplug;
    int          fail;

    make_resolved_plugin(&padd, "mm_test_i386.so", "mm_test_i386.so");

    TEST("MPluginList::add - names without directory keep file on filename");
    iplug = list->add(&padd);
    fail  = 0;
    if (!iplug)
        fail = 1;
    else
    {
        fail |= (iplug->file != iplug->filename);
        fail |= (strcmp(iplug->file, "mm_test_i386.so") != 0);
    }
    if (fail)
    {
        printf("FAIL\n    file pointer not rebuilt from the copied filename\n");
        free_list(list);
        return 1;
    }
    PASS();
    free_list(list);
    return 0;
}

// Every add() into a fresh slot rebuilds the pointer for that slot.
static int test_add_rebuilds_pointer_per_slot(void)
{
    MPluginList* list = make_zeroed_list();
    MPlugin      first;
    MPlugin      second;
    MPlugin*     ifirst;
    MPlugin*     isecond;
    int          fail = 0;

    make_resolved_plugin(&first, "dlls/mm_one_i386.so",
                         "/srv/half-life/cstrike/dlls/mm_one_i386.so");
    make_resolved_plugin(&second, "dlls/mm_two_i386.so",
                         "/srv/half-life/valve/dlls/mm_two_i386.so");

    TEST("MPluginList::add - each slot gets its own file pointer");
    ifirst  = list->add(&first);
    isecond = list->add(&second);
    if (!ifirst || !isecond)
        fail = 1;
    else
    {
        fail |= (ifirst == isecond);
        fail |= (strcmp(ifirst->file, "mm_one_i386.so") != 0);
        fail |= (strcmp(isecond->file, "mm_two_i386.so") != 0);
        fail |= (ifirst->file < ifirst->pathname) || (ifirst->file >= ifirst->pathname + sizeof(ifirst->pathname));
        fail |= (isecond->file < isecond->pathname) || (isecond->file >= isecond->pathname + sizeof(isecond->pathname));
        fail |= (list->endlist != 2);
    }
    if (fail)
    {
        printf("FAIL\n    expected two distinct slots with rebuilt file pointers\n");
        free_list(list);
        return 1;
    }
    PASS();
    free_list(list);
    return 0;
}

// The copied record decides where file points. A source record whose file does
// not address its own filename (never set, or pointing somewhere else) must not
// steer the result.
static int test_add_ignores_stale_source_pointer(void)
{
    MPluginList* list = make_zeroed_list();
    MPlugin      padd;
    MPlugin*     iplug;
    int          fail;

    make_resolved_plugin(&padd, "dlls/mm_test_i386.so",
                         "/srv/half-life/cstrike/dlls/mm_test_i386.so");
    padd.file = padd.filename; // stale: the offset from filename is meaningless

    TEST("MPluginList::add - stale source file pointer is ignored");
    iplug = list->add(&padd);
    fail  = 0;
    if (!iplug)
        fail = 1;
    else
    {
        fail |= (iplug->file < iplug->pathname) || (iplug->file >= iplug->pathname + sizeof(iplug->pathname));
        fail |= (strcmp(iplug->file, "mm_test_i386.so") != 0);
    }
    if (fail)
    {
        printf("FAIL\n    file pointer followed the source record instead of the copied pathname\n");
        free_list(list);
        return 1;
    }
    PASS();
    free_list(list);
    return 0;
}

// A record whose file pointer was never set must still yield a usable copy.
static int test_add_without_source_file_pointer(void)
{
    MPluginList* list = make_zeroed_list();
    MPlugin      padd;
    MPlugin*     iplug;
    int          fail;

    make_resolved_plugin(&padd, "dlls/mm_test_i386.so",
                         "/srv/half-life/cstrike/dlls/mm_test_i386.so");
    padd.file = NULL;

    TEST("MPluginList::add - record without a file pointer still resolves");
    iplug = list->add(&padd);
    fail  = 0;
    if (!iplug)
        fail = 1;
    else
    {
        fail |= (iplug->file < iplug->pathname) || (iplug->file >= iplug->pathname + sizeof(iplug->pathname));
        fail |= (strcmp(iplug->file, "mm_test_i386.so") != 0);
    }
    if (fail)
    {
        printf("FAIL\n    file pointer not rebuilt when the source had none\n");
        free_list(list);
        return 1;
    }
    PASS();
    free_list(list);
    return 0;
}

int main(void)
{
    int fail = 0;

    mm_test_reset();

    fail |= test_add_file_pointer_from_pathname();
    fail |= test_add_file_pointer_falls_back_to_filename();
    fail |= test_add_file_pointer_without_directory();
    fail |= test_add_rebuilds_pointer_per_slot();
    fail |= test_add_ignores_stale_source_pointer();
    fail |= test_add_without_source_file_pointer();

    printf("\n%d/%d checks passed\n", tests_passed, tests_run);
    return fail;
}
