#include "detours.h"
#include "test_common.h"
#include <cstring>
#include <initializer_list>

static int (*original)();
static int replacement()
{
    return original() + 1;
}

int main()
{
    static_assert(sizeof(void*) == 4, "This probe exercises the supported i686 ABI");
    const unsigned char endbr[] = {0xf3, 0x0f, 0x1e, 0xfb};
    // mov eax, 42; ret. Enough padding for the instruction decoder's lookahead.
    const unsigned char body[] = {0xb8, 42, 0, 0, 0, 0xc3};
    for (int scenario = 0; scenario < 4; scenario++)
    {
        const bool landing_pad   = (scenario & 1) != 0;
        const bool relative_call = (scenario & 2) != 0;
        TEST(landing_pad ? "detour - preserve endbr32" : "detour - ordinary entry");
#ifdef _WIN32
        auto entry = static_cast<unsigned char*>(VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
#else
        auto entry = static_cast<unsigned char*>(mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        ASSERT_TRUE(entry != MAP_FAILED);
#endif
        ASSERT_PTR_NOT_NULL(entry);
        memset(entry, 0x90, 4096);
        const size_t prefix = landing_pad ? sizeof(endbr) : 0;
        if (landing_pad) memcpy(entry, endbr, sizeof(endbr));
        memcpy(entry + prefix, body, sizeof(body));
        if (relative_call)
        {
            // call helper; ret, exercising relocation after the new landing pad.
            constexpr size_t helper_offset = 64;
            entry[prefix]                  = 0xe8;
            const int32_t displacement     = helper_offset - (prefix + 5);
            memcpy(entry + prefix + 1, &displacement, sizeof(displacement));
            memcpy(entry + helper_offset, body, sizeof(body));
        }
        unsigned char saved[32];
        memcpy(saved, entry, sizeof(saved));
        auto target = reinterpret_cast<int (*)()>(entry);
        ASSERT_INT(42, target());
        CDetour* hook = CDetourManager::CreateDetour(reinterpret_cast<void*>(replacement), reinterpret_cast<void**>(&original), entry);
        ASSERT_PTR_NOT_NULL(hook);
        ASSERT_TRUE(!memcmp(endbr, reinterpret_cast<void*>(original), sizeof(endbr)));
        ASSERT_INT(42, original());
        for (int cycle = 0; cycle < 2; cycle++)
        {
            hook->EnableDetour();
            if (landing_pad) ASSERT_TRUE(!memcmp(endbr, entry, sizeof(endbr)));
            ASSERT_INT(0xe9, entry[prefix]);
            ASSERT_INT(43, target());
            ASSERT_INT(42, original());
            hook->DisableDetour();
            ASSERT_TRUE(!memcmp(saved, entry, sizeof(saved)));
            ASSERT_INT(42, target());
        }
        hook->EnableDetour();
        hook->Destroy();
        ASSERT_TRUE(!memcmp(saved, entry, sizeof(saved)));
        ASSERT_INT(42, target());
#ifdef _WIN32
        VirtualFree(entry, 0, MEM_RELEASE);
#else
        munmap(entry, 4096);
#endif
        PASS();
    }
    return 0;
}
