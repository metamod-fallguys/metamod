#include <vector>
#include <elf.h>
#include <unistd.h>
#include <extdll.h>
#include "osdep.h"
#include "osdep_p.h"
#include "mm_test_stubs.h"
#include "test_common.h"

struct TemporaryElf
{
    char path[64] = "/tmp/mmfg-elf-boundary-XXXXXX";
    int  fd       = mkstemp(path);
    ~TemporaryElf()
    {
        if (fd >= 0)
        {
            close(fd);
            unlink(path);
        }
    }
    bool save(const std::vector<unsigned char>& bytes, size_t size)
    {
        return fd >= 0 && ftruncate(fd, 0) == 0 &&
            pwrite(fd, bytes.data(), size, 0) == (ssize_t)size;
    }
};

static int check_boundaries()
{
    FILE* fp = fopen(MMFG_TEST_FAKE_GAMEDLL, "rb");
    ASSERT_PTR_NOT_NULL(fp);
    fseek(fp, 0, SEEK_END);
    long length = ftell(fp);
    rewind(fp);
    ASSERT_TRUE(length >= (long)sizeof(Elf32_Ehdr));
    std::vector<unsigned char> bytes(length);
    size_t                     read = fread(bytes.data(), 1, bytes.size(), fp);
    fclose(fp);
    ASSERT_INT(read, bytes.size());
    Elf32_Ehdr header;
    memcpy(&header, bytes.data(), sizeof(header));
    ASSERT_INT(header.e_ident[EI_CLASS], ELFCLASS32);
    size_t end = header.e_shoff + header.e_shnum * sizeof(Elf32_Shdr);
    ASSERT_TRUE(end <= bytes.size());
    TemporaryElf fixture;
    TEST("ELF - section table ending exactly at EOF is accepted");
    ASSERT_TRUE(fixture.save(bytes, end));
    ASSERT_TRUE(is_gamedll(fixture.path));
    PASS();
    TEST("ELF - one-byte truncated section table is rejected");
    ASSERT_TRUE(fixture.save(bytes, end - 1));
    ASSERT_FALSE(is_gamedll(fixture.path));
    PASS();
    TEST("ELF - truncated ELF header is rejected");
    ASSERT_TRUE(fixture.save(bytes, sizeof(header) - 1));
    ASSERT_FALSE(is_gamedll(fixture.path));
    PASS();
    TEST("ELF - section table beyond EOF is rejected");
    header.e_shoff = end + 1;
    memcpy(bytes.data(), &header, sizeof(header));
    ASSERT_TRUE(fixture.save(bytes, end));
    ASSERT_FALSE(is_gamedll(fixture.path));
    PASS();
    TEST("ELF - extreme section offset does not wrap the range check");
    header.e_shoff = 0xfffffff0U;
    memcpy(bytes.data(), &header, sizeof(header));
    ASSERT_TRUE(fixture.save(bytes, end));
    ASSERT_FALSE(is_gamedll(fixture.path));
    PASS();
    return 0;
}

int main()
{
    mm_test_reset();
    return check_boundaries();
}
