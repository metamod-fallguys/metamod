//
// metamod - regression tests for the linux dlsym linkent replacement
//
// is_code_trampoline_jmp_opcode() decides whether a function pointer is an
// "FF 25" indirect-jump forwarder. It used a logical OR, so any address whose
// first byte was 0xff or whose second byte was 0x25 passed, and the caller in
// init_linkent_replacement() then dereferenced whatever followed as if it were a
// jump target.
//

#include <stdio.h>
#include <string.h>

#include <dlfcn.h>

#include <extdll.h>

#include "metamod.h"

#include "mm_test_stubs.h"
#include "test_common.h"

// The functions under test have internal linkage; include the production source
// so the tests drive the shipped implementation.
#include "osdep_linkent_linux.cpp"

static void fake_trampoline_target(void) {}

// Build "jmp dword ptr [slot]" the way the linker does: the four bytes after the
// opcode hold the address of a slot that holds the final address.
static void make_indirect_jump(unsigned char* code, size_t code_size, void* final_target)
{
    static void* slots[8];
    static int   next_slot;
    void**       slot = &slots[next_slot++ % 8];

    memset(code, 0x90, code_size);
    *slot   = final_target;
    code[0] = 0xff;
    code[1] = 0x25;
    memcpy(code + 2, &slot, sizeof(slot));
}

static int test_trampoline_accepts_ff25(void)
{
    unsigned char code[8];

    TEST("is_code_trampoline_jmp_opcode - accepts an FF 25 indirect jump");
    make_indirect_jump(code, sizeof(code), (void*)&fake_trampoline_target);

    ASSERT_TRUE(is_code_trampoline_jmp_opcode(code));
    ASSERT_PTR_EQ(extract_function_pointer_from_trampoline_jmp(code),
                  (void*)&fake_trampoline_target);
    PASS();
    return 0;
}

static int test_trampoline_rejects_partial_matches(void)
{
    // Every one of these matched the old "||" check.
    static const unsigned char cases[][2] = {
        {0xff, 0x00}, // 0xff alone is an "inc/dec" or indirect call group opcode
        {0xff, 0xe0}, // jmp eax
        {0x00, 0x25}, // 0x25 alone is "and eax, imm32"
        {0xe9, 0x25}, // relative jump whose displacement starts with 0x25
        {0x90, 0x25}, // nop followed by "and"
    };
    char msg[80];

    for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        unsigned char code[4] = {0, 0, 0, 0};
        snprintf(msg, sizeof(msg), "is_code_trampoline_jmp_opcode - rejects %02x %02x",
                 cases[i][0], cases[i][1]);
        TEST(msg);
        memcpy(code, cases[i], 2);
        ASSERT_FALSE(is_code_trampoline_jmp_opcode(code));
        PASS();
    }
    return 0;
}

// The caller walks the forwarder chain until the opcode stops matching. With the
// old check a plain instruction beginning with 0xff was treated as a forwarder
// and the walk dereferenced the following bytes as an address.
static int test_trampoline_walk_does_not_follow_plain_code(void)
{
    unsigned char code[8] = {0xff, 0x00, 0xde, 0xad, 0xbe, 0xef, 0x00, 0x00};
    void*         walker  = code;

    TEST("trampoline walk - stops on code that is not an FF 25 jump");
    while (is_code_trampoline_jmp_opcode(walker)) walker = extract_function_pointer_from_trampoline_jmp(walker);

    ASSERT_PTR_EQ(walker, (void*)code);
    PASS();
    return 0;
}

// Two chained forwarders still resolve to the final target.
static int test_trampoline_walk_follows_chained_forwarders(void)
{
    unsigned char first[8];
    unsigned char second[8];
    void*         walker;

    TEST("trampoline walk - follows genuine forwarders to the target");
    make_indirect_jump(second, sizeof(second), (void*)&fake_trampoline_target);
    make_indirect_jump(first, sizeof(first), (void*)&second);

    walker = first;
    while (is_code_trampoline_jmp_opcode(walker)) walker = extract_function_pointer_from_trampoline_jmp(walker);

    ASSERT_PTR_EQ(walker, (void*)&fake_trampoline_target);
    PASS();
    return 0;
}

static int test_construct_jmp_instruction(void)
{
    unsigned char code[BYTES_SIZE];
    char          place[16];
    unsigned long place_addr;
    unsigned long target_addr;
    unsigned long encoded;

    TEST("construct_jmp_instruction - encodes a relative jump");
    memset(code, 0, sizeof(code));
    place_addr  = (unsigned long)place;
    target_addr = place_addr + 0x1000;

    construct_jmp_instruction(code, (void*)place_addr, (void*)target_addr);

    ASSERT_INT(code[0], 0xe9);
    memcpy(&encoded, code + 1, sizeof(encoded));
    ASSERT_TRUE(encoded == target_addr - (place_addr + BYTES_SIZE));
    PASS();
    return 0;
}

int main(void)
{
    int fail = 0;

    mm_test_reset();

    fail |= test_trampoline_accepts_ff25();
    fail |= test_trampoline_rejects_partial_matches();
    fail |= test_trampoline_walk_does_not_follow_plain_code();
    fail |= test_trampoline_walk_follows_chained_forwarders();
    fail |= test_construct_jmp_instruction();

    printf("\n%d/%d checks passed\n", tests_passed, tests_run);
    return fail;
}
