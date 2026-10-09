//
// metamod - regression tests for autodetect_gamedll()
//
// full_gamedir_path() resolves through realpath(), which writes up to PATH_MAX
// bytes whatever room the caller has. The stack buffers in autodetect_gamedll()
// used to be 256 bytes, so a game directory that *resolves* to a longer path
// overran them. The test builds exactly that: a short path that is a symlink
// into a deep directory.
//

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <extdll.h>

#include "metamod.h"
#include "game_autodetect.h"

#include "mm_test_stubs.h"
#include "test_common.h"

// CMake passes the built fixture path; the environment variable allows running
// the same test against a fixture from another build directory.
#ifndef MMFG_TEST_FAKE_GAMEDLL
#    define MMFG_TEST_FAKE_GAMEDLL "mmfg_fake_gamedll.so"
#endif

static const char* fake_gamedll_path(void)
{
    const char* env = getenv("MMFG_TEST_FAKE_GAMEDLL");
    return (env && env[0]) ? env : MMFG_TEST_FAKE_GAMEDLL;
}

// Long enough that ten nested components push the resolved path well past the
// 256 byte buffers the old code used, while staying inside PATH_MAX.
#define DEEP_COMPONENT_LEN 40

static char temp_root[PATH_MAX];
static char deep_dir[PATH_MAX];

static int make_dir(const char* path)
{
    return mkdir(path, 0755) == 0 || errno == EEXIST;
}

static int copy_file(const char* from, const char* to)
{
    char   buf[4096];
    FILE*  in  = fopen(from, "rb");
    FILE*  out = NULL;
    size_t n;

    if (!in)
        return 0;
    out = fopen(to, "wb");
    if (!out)
    {
        fclose(in);
        return 0;
    }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
    {
        if (fwrite(buf, 1, n, out) != n)
        {
            fclose(in);
            fclose(out);
            return 0;
        }
    }
    fclose(in);
    fclose(out);
    return 1;
}

static void remove_tree(const char* path)
{
    DIR*           dir = opendir(path);
    struct dirent* ent;

    if (dir)
    {
        while ((ent = readdir(dir)) != NULL)
        {
            char child[PATH_MAX];
            if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
                continue;
            if (snprintf(child, sizeof(child), "%s/%s", path, ent->d_name) < (int)sizeof(child))
                remove_tree(child);
        }
        closedir(dir);
    }
    unlink(path);
    rmdir(path);
}

// Create <temp_root>/deep/<ten long components>/dlls and a link to it.
static int build_deep_tree(void)
{
    char component[DEEP_COMPONENT_LEN + 1];
    int  i;

    memset(temp_root, 0, sizeof(temp_root));
    snprintf(temp_root, sizeof(temp_root), "/tmp/mmfg_autodetect_XXXXXX");
    if (!mkdtemp(temp_root))
        return 0;

    memset(component, 'd', sizeof(component) - 1);
    component[DEEP_COMPONENT_LEN] = '\0';

    snprintf(deep_dir, sizeof(deep_dir), "%s/deep", temp_root);
    if (!make_dir(deep_dir))
        return 0;

    for (i = 0; i < 10; i++)
    {
        int len = strlen(deep_dir);
        if (snprintf(deep_dir + len, sizeof(deep_dir) - len, "/%s", component) >= (int)(sizeof(deep_dir) - len))
            return 0;
        if (!make_dir(deep_dir))
            return 0;
    }

    {
        char linkpath[PATH_MAX];
        snprintf(linkpath, sizeof(linkpath), "%s/link", temp_root);
        if (symlink(deep_dir, linkpath) != 0)
            return 0;
    }
    return 1;
}

static const char* deep_link(void)
{
    static char linkpath[PATH_MAX];
    snprintf(linkpath, sizeof(linkpath), "%s/link", temp_root);
    return linkpath;
}

static int install_gamedll(const char* gamedir, const char* name)
{
    char dlls[PATH_MAX];
    char dest[PATH_MAX];

    snprintf(dlls, sizeof(dlls), "%s/dlls", gamedir);
    if (!make_dir(dlls))
        return 0;
    snprintf(dest, sizeof(dest), "%s/%s", dlls, name);
    return copy_file(fake_gamedll_path(), dest);
}

static const char* autodetect(const char* gamedir, const char* knownfn)
{
    gamedll_t gd;

    memset(&gd, 0, sizeof(gd));
    STRNCPY(gd.gamedir, gamedir, sizeof(gd.gamedir));
    STRNCPY(GameDLL.gamedir, gamedir, sizeof(GameDLL.gamedir));
    return autodetect_gamedll(&gd, knownfn);
}

// The path handed to autodetect_gamedll() is short; what realpath() resolves it
// to is longer than the old 256 byte stack buffers.
static int test_autodetect_gamedir_resolving_past_256_bytes(void)
{
    const char* result;

    TEST("autodetect_gamedll - gamedir resolving past 256 bytes");
    mm_test_reset();
    if (!build_deep_tree())
    {
        printf("SKIP (no temp tree)\n");
        tests_run--;
        return 0;
    }
    if (strlen(deep_dir) <= 256)
    {
        printf("SKIP (temp tree too short)\n");
        tests_run--;
        remove_tree(temp_root);
        return 0;
    }
    if (!install_gamedll(deep_dir, "game_i386.so"))
    {
        printf("FAIL\n    could not place the fake gamedll\n");
        remove_tree(temp_root);
        return 1;
    }

    result = autodetect(deep_link(), "nonexistent.so");
    ASSERT_PTR_NOT_NULL(result);
    ASSERT_STR(result, "game_i386.so");
    PASS();
    remove_tree(temp_root);
    return 0;
}

// Same deep tree, but the known file name already exists: nothing to autodetect.
static int test_autodetect_deep_gamedir_knownfn(void)
{
    const char* result;

    TEST("autodetect_gamedll - deep gamedir with a valid known file name");
    mm_test_reset();
    if (!build_deep_tree())
    {
        printf("SKIP (no temp tree)\n");
        tests_run--;
        return 0;
    }
    if (!install_gamedll(deep_dir, "game_i386.so"))
    {
        printf("FAIL\n    could not place the fake gamedll\n");
        remove_tree(temp_root);
        return 1;
    }

    result = autodetect(deep_link(), "game_i386.so");
    ASSERT_PTR_NULL(result);
    PASS();
    remove_tree(temp_root);
    return 0;
}

static int test_autodetect_short_gamedir(void)
{
    char  dir[PATH_MAX];
    char  linkpath[PATH_MAX];
    char* result;

    TEST("autodetect_gamedll - finds the gamedll next to a short gamedir");
    mm_test_reset();
    memset(temp_root, 0, sizeof(temp_root));
    snprintf(temp_root, sizeof(temp_root), "/tmp/mmfg_autodetect_XXXXXX");
    if (!mkdtemp(temp_root))
    {
        printf("SKIP (no temp dir)\n");
        tests_run--;
        return 0;
    }
    snprintf(dir, sizeof(dir), "%s/game", temp_root);
    if (!make_dir(dir) || !install_gamedll(dir, "game_i386.so"))
    {
        printf("FAIL\n    could not prepare the fake gamedll\n");
        remove_tree(temp_root);
        return 1;
    }

    result = (char*)autodetect(dir, "nonexistent.so");
    ASSERT_PTR_NOT_NULL(result);
    ASSERT_STR(result, "game_i386.so");

    snprintf(linkpath, sizeof(linkpath), "%s/game", temp_root);
    result = (char*)autodetect(linkpath, "game_i386.so");
    ASSERT_PTR_NULL(result);
    PASS();
    remove_tree(temp_root);
    return 0;
}

static int test_autodetect_missing_directory(void)
{
    TEST("autodetect_gamedll - missing dlls directory reports nothing");
    mm_test_reset();
    ASSERT_PTR_NULL(autodetect("/tmp/mmfg_no_such_gamedir_0123456789", NULL));
    PASS();
    return 0;
}

static int test_autodetect_ignores_non_gamedlls(void)
{
    char        dir[PATH_MAX];
    const char* result;
    char        path[PATH_MAX];
    FILE*       fp;

    TEST("autodetect_gamedll - ignores metamod, bots and non-ELF files");
    mm_test_reset();
    memset(temp_root, 0, sizeof(temp_root));
    snprintf(temp_root, sizeof(temp_root), "/tmp/mmfg_autodetect_XXXXXX");
    if (!mkdtemp(temp_root))
    {
        printf("SKIP (no temp dir)\n");
        tests_run--;
        return 0;
    }
    snprintf(dir, sizeof(dir), "%s/game", temp_root);
    if (!make_dir(dir) || !install_gamedll(dir, "metamod_i386.so") || !install_gamedll(dir, "bot_i386.so") ||
        !install_gamedll(dir, "readme.txt"))
    {
        printf("FAIL\n    could not prepare the dlls directory\n");
        remove_tree(temp_root);
        return 1;
    }
    snprintf(path, sizeof(path), "%s/dlls/invalid.so", dir);
    fp = fopen(path, "wb");
    if (fp)
    {
        fputs("not an ELF file", fp);
        fclose(fp);
    }

    result = autodetect(dir, "nonexistent.so");
    ASSERT_PTR_NULL(result);
    PASS();
    remove_tree(temp_root);
    return 0;
}

int main(void)
{
    int fail = 0;

    mm_test_reset();

    fail |= test_autodetect_gamedir_resolving_past_256_bytes();
    fail |= test_autodetect_deep_gamedir_knownfn();
    fail |= test_autodetect_short_gamedir();
    fail |= test_autodetect_missing_directory();
    fail |= test_autodetect_ignores_non_gamedlls();

    printf("\n%d/%d checks passed\n", tests_passed, tests_run);
    return fail;
}
