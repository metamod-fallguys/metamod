//
// metamod - regression tests for setup_gamedll()
//
// The Linux branch that strips the "_i386" part from a known DLL name built its
// scratch copy in a block-local buffer while the pointer to it escaped through
// strippedfn -> usedfn -> knownfn and stayed live until the end of the function
// (autodetect_gamedll() and the pathname formatting both read it). The buffer is
// now at function scope; with AddressSanitizer the old layout reports
// stack-use-after-scope.
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
#include "conf_meta.h"
#include "game_support.h"

#include "mm_test_stubs.h"
#include "test_common.h"

static char    temp_root[PATH_MAX];
static MConfig config;

static int make_dir(const char* path)
{
    return mkdir(path, 0755) == 0 || errno == EEXIST;
}

static int write_file(const char* path, const char* content)
{
    FILE* fp = fopen(path, "wb");

    if (!fp)
        return 0;
    fputs(content, fp);
    fclose(fp);
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

// Create /tmp/<unique>/cstrike/dlls and point GameDLL/Config at it.
static int prepare_gamedir(char* gamedir, size_t gamedir_size)
{
    char path[PATH_MAX];

    memset(temp_root, 0, sizeof(temp_root));
    snprintf(temp_root, sizeof(temp_root), "/tmp/mmfg_gamesupport_XXXXXX");
    if (!mkdtemp(temp_root))
        return 0;

    snprintf(gamedir, gamedir_size, "%s/cstrike", temp_root);
    if (!make_dir(gamedir))
        return 0;

    snprintf(path, sizeof(path), "%s/dlls", gamedir);
    if (!make_dir(path))
        return 0;

    mm_test_reset();

    memset(&config, 0, sizeof(config));
    config.autodetect = 1;
    config.gamedll    = NULL;
    Config            = &config;

    STRNCPY(GameDLL.gamedir, gamedir, sizeof(GameDLL.gamedir));
    return 1;
}

// The known DLL for "cstrike" is cs_i386.so. When the engine's stripped name
// (cs.so) exists, setup_gamedll() has to keep using it after the block that
// produced it has ended.
static int test_setup_gamedll_stripped_name_lifetime(void)
{
    char      gamedir[PATH_MAX];
    char      path[PATH_MAX];
    gamedll_t gd;

    TEST("setup_gamedll - stripped known DLL name stays usable");
    if (!prepare_gamedir(gamedir, sizeof(gamedir)))
    {
        printf("SKIP (no temp dir)\n");
        tests_run--;
        return 0;
    }

    snprintf(path, sizeof(path), "%s/dlls/cs.so", gamedir);
    if (!write_file(path, "game dll placeholder"))
    {
        printf("FAIL\n    could not create the stripped DLL file\n");
        remove_tree(temp_root);
        return 1;
    }

    memset(&gd, 0, sizeof(gd));
    STRNCPY(gd.name, "cstrike", sizeof(gd.name));
    STRNCPY(gd.gamedir, gamedir, sizeof(gd.gamedir));

    ASSERT_TRUE(setup_gamedll(&gd));
    ASSERT_STR(gd.file, "cs.so");
    snprintf(path, sizeof(path), "%s/dlls/cs.so", gamedir);
    ASSERT_STR(gd.pathname, path);
    ASSERT_STR(gd.desc, "Counter-Strike");
    PASS();
    remove_tree(temp_root);
    return 0;
}

// Without the stripped file the known name is used, and it is static storage.
static int test_setup_gamedll_known_name_fallback(void)
{
    char      gamedir[PATH_MAX];
    char      path[PATH_MAX];
    gamedll_t gd;

    TEST("setup_gamedll - falls back to the known DLL name");
    if (!prepare_gamedir(gamedir, sizeof(gamedir)))
    {
        printf("SKIP (no temp dir)\n");
        tests_run--;
        return 0;
    }

    memset(&gd, 0, sizeof(gd));
    STRNCPY(gd.name, "cstrike", sizeof(gd.name));
    STRNCPY(gd.gamedir, gamedir, sizeof(gd.gamedir));

    ASSERT_TRUE(setup_gamedll(&gd));
    ASSERT_STR(gd.file, "cs_i386.so");
    snprintf(path, sizeof(path), "%s/dlls/cs_i386.so", gamedir);
    ASSERT_STR(gd.pathname, path);
    ASSERT_STR(gd.desc, "Counter-Strike");
    PASS();
    remove_tree(temp_root);
    return 0;
}

// An explicit override in config.ini wins over the known list.
static int test_setup_gamedll_override(void)
{
    char      gamedir[PATH_MAX];
    gamedll_t gd;

    TEST("setup_gamedll - config.ini override wins over the known list");
    if (!prepare_gamedir(gamedir, sizeof(gamedir)))
    {
        printf("SKIP (no temp dir)\n");
        tests_run--;
        return 0;
    }
    config.gamedll = (char*)"dlls/custom.so";

    memset(&gd, 0, sizeof(gd));
    STRNCPY(gd.name, "cstrike", sizeof(gd.name));
    STRNCPY(gd.gamedir, gamedir, sizeof(gd.gamedir));

    ASSERT_TRUE(setup_gamedll(&gd));
    ASSERT_STR(gd.file, "custom.so");
    ASSERT_STR(gd.pathname, "dlls/custom.so");
    ASSERT_STR(gd.desc, "custom.so (override)");
    PASS();
    remove_tree(temp_root);
    return 0;
}

int main(void)
{
    int fail = 0;

    mm_test_reset();

    fail |= test_setup_gamedll_stripped_name_lifetime();
    fail |= test_setup_gamedll_known_name_fallback();
    fail |= test_setup_gamedll_override();

    printf("\n%d/%d checks passed\n", tests_passed, tests_run);
    return fail;
}
