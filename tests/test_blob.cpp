#include "blob.h"
#include "test_common.h"

#include <fstream>
#include <iterator>

static void Put(std::vector<unsigned char>& bytes, size_t at, uint32_t value)
{
    for (size_t i = 0; i < 4; ++i)
        bytes[at + i] = (unsigned char)(value >> (i * 8));
}

static std::vector<unsigned char> Fixture()
{
    std::vector<unsigned char> bytes(196, 0);
    Put(bytes, 64, 0x12345678);
    Put(bytes, 72, 1); // The header stores the last section index.
    Put(bytes, 76, 0x10001000 ^ 0x7a32bc85);
    Put(bytes, 80, 0x10000000 ^ 0x49c042d1);
    Put(bytes, 84, 0x10001000 + 12);
    Put(bytes, 88, 0x10002000 ^ 0x872c3d47);
    Put(bytes, 92, 0x10001000);
    Put(bytes, 96, 64);
    Put(bytes, 100, 32);
    Put(bytes, 104, 132);
    Put(bytes, 112, 0x10002000);
    Put(bytes, 116, 64);
    Put(bytes, 120, 32);
    Put(bytes, 124, 164);
    for (size_t i = 132; i < bytes.size(); ++i)
        bytes[i] = (unsigned char)i;
    return bytes;
}

static std::vector<unsigned char> Encrypt(std::vector<unsigned char> bytes)
{
    unsigned char key = 0x57;
    for (size_t i = 68; i < bytes.size(); ++i)
    {
        unsigned char plain = bytes[i];
        bytes[i] ^= key;
        key = (unsigned char)(key + plain + 0x57);
    }
    return bytes;
}

int main(int argc, char** argv)
{
    Blob::Image image;
    TEST("blob - decode header, inclusive section count and initialized code");
    ASSERT_TRUE(Blob::Parse(Encrypt(Fixture()), image));
    ASSERT_INT(image.base, 0x10000000);
    ASSERT_INT(image.size, 0x2040);
    ASSERT_INT(image.sections.size(), 2);
    ASSERT_TRUE(Blob::MatchesCode(image, 0x10001000, image.bytes.data() + 132, 32));
    ASSERT_FALSE(Blob::MatchesCode(image, 0x10001000, image.bytes.data() + 133, 32));
    ASSERT_FALSE(Blob::MatchesCode(image, 0x10001020, image.bytes.data() + 132, 32));
    PASS();

    TEST("blob - reject truncated, overflowing, overlapping and invalid descriptions");
    for (size_t length : {size_t(0), size_t(67), size_t(91), size_t(131), size_t(195)})
    {
        auto bytes = Encrypt(Fixture());
        bytes.resize(length);
        ASSERT_FALSE(Blob::Parse(bytes, image));
    }
    for (auto mutation : {std::pair<size_t, uint32_t>{64, 0}, {72, 0xffff}, {96, 0xffffffff}, {100, 65}, {104, 0xfffffff0}, {112, 0x10001020}, {84, 12}})
    {
        auto bytes = Fixture();
        Put(bytes, mutation.first, mutation.second);
        ASSERT_FALSE(Blob::Parse(Encrypt(bytes), image));
    }
    PASS();

    // Optional real inputs stay outside the repository; no copyrighted binaries
    // are embedded in the test. This verifies parsing, not a running engine.
    for (int i = 1; i < argc; ++i)
    {
        TEST("blob - real engine file");
        std::ifstream file(argv[i], std::ios::binary);
        ASSERT_TRUE(file.good());
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
        ASSERT_TRUE(Blob::Parse(bytes, image));
        ASSERT_INT(image.base, 0x01d00000);
        ASSERT_TRUE(image.size > 0x1000000);
        PASS();
    }
    return tests_passed == tests_run ? 0 : 1;
}
