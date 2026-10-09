#include <unistd.h>
#include <extdll.h>
#define private public
#include "conf_meta.h"
#undef private
#include "mm_test_stubs.h"
#include "test_common.h"

int main()
{
    mm_test_reset();
    char path[] = "/tmp/mmfg-config-reload-XXXXXX";
    int  fd     = mkstemp(path);
    ASSERT_TRUE(fd >= 0);
    close(fd);
    MConfig  config;
    option_t options[] = {{NULL, CF_INT, NULL, NULL}};
    config.init(options);
    TEST("config reload - repeated loads release previous filenames");
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        ASSERT_TRUE(config.load(path));
        ASSERT_STR(config.filename, path);
    }
    PASS();
    TEST("config reload - failed open preserves previous filename");
    unlink(path);
    ASSERT_FALSE(config.load(path));
    ASSERT_STR(config.filename, path);
    PASS();
    // MConfig lives for the process lifetime in production. Release its final
    // filename here so LeakSanitizer detects only the overwritten allocations.
    free(config.filename);
    config.filename = NULL;
    return 0;
}
