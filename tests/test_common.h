//
// metamod - shared macro helpers for the component test suite
//
// The macro set mirrors the conventions used by the upstream Metamod-P test
// suite so that tests read the same way across both trees. Everything here is
// plain C++ with no third-party dependencies.
//

#ifndef MMFG_TEST_COMMON_H
#define MMFG_TEST_COMMON_H

#include <stdio.h>
#include <string.h>
#include <math.h>

#if defined(_MSC_VER)
#    define MM_TEST_UNUSED
#else
#    define MM_TEST_UNUSED __attribute__((unused))
#endif

static int tests_run    = 0;
static int tests_passed = 0;

#define TEST(name)                \
    do                            \
    {                             \
        tests_run++;              \
        printf("  %-60s ", name); \
    } while (0)

#define PASS()          \
    do                  \
    {                   \
        tests_passed++; \
        printf("OK\n"); \
    } while (0)

#define ASSERT_STR(actual, expected)                                     \
    do                                                                   \
    {                                                                    \
        const char* _a = (actual);                                       \
        const char* _e = (expected);                                     \
        if (!_a || !_e || strcmp(_a, _e) != 0)                           \
        {                                                                \
            printf("FAIL\n    expected: \"%s\"\n    got:      \"%s\"\n", \
                   _e ? _e : "(null)", _a ? _a : "(null)");              \
            return 1;                                                    \
        }                                                                \
    } while (0)

#define ASSERT_INT(actual, expected)                                          \
    do                                                                        \
    {                                                                         \
        long long _a = (long long)(actual);                                   \
        long long _e = (long long)(expected);                                 \
        if (_a != _e)                                                         \
        {                                                                     \
            printf("FAIL\n    expected: %lld\n    got:      %lld\n", _e, _a); \
            return 1;                                                         \
        }                                                                     \
    } while (0)

#define ASSERT_PTR_EQ(actual, expected)                                   \
    do                                                                    \
    {                                                                     \
        const void* _a = (const void*)(actual);                           \
        const void* _e = (const void*)(expected);                         \
        if (_a != _e)                                                     \
        {                                                                 \
            printf("FAIL\n    expected: %p\n    got:      %p\n", _e, _a); \
            return 1;                                                     \
        }                                                                 \
    } while (0)

#define ASSERT_PTR_NULL(actual)                                         \
    do                                                                  \
    {                                                                   \
        const void* _a = (const void*)(actual);                         \
        if (_a != NULL)                                                 \
        {                                                               \
            printf("FAIL\n    expected: NULL\n    got:      %p\n", _a); \
            return 1;                                                   \
        }                                                               \
    } while (0)

#define ASSERT_PTR_NOT_NULL(actual)                                       \
    do                                                                    \
    {                                                                     \
        const void* _a = (const void*)(actual);                           \
        if (_a == NULL)                                                   \
        {                                                                 \
            printf("FAIL\n    expected: non-NULL\n    got:      NULL\n"); \
            return 1;                                                     \
        }                                                                 \
    } while (0)

#define ASSERT_TRUE(cond)                                     \
    do                                                        \
    {                                                         \
        if (!(cond))                                          \
        {                                                     \
            printf("FAIL\n    condition false: %s\n", #cond); \
            return 1;                                         \
        }                                                     \
    } while (0)

#define ASSERT_FALSE(cond)                                              \
    do                                                                  \
    {                                                                   \
        if ((cond))                                                     \
        {                                                               \
            printf("FAIL\n    condition should be false: %s\n", #cond); \
            return 1;                                                   \
        }                                                               \
    } while (0)

#endif // MMFG_TEST_COMMON_H
