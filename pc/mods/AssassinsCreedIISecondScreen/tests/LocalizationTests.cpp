#include "Localization.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
using ac2ds::DecodeLocalization;
static void Put16(std::vector<uint8_t>& b, size_t at, uint32_t value) { b[at] = uint8_t(value >> 8); b[at + 1] = uint8_t(value); }
static void Put32(std::vector<uint8_t>& b, size_t at, uint32_t value) { Put16(b, at, value >> 16); Put16(b, at + 2, value); }
static std::vector<uint8_t> Fixture() {
    std::vector<uint8_t> b(57);
    Put16(b, 0, 242); Put16(b, 2, 6);
    Put16(b, 8, 'H'); Put16(b, 12, 'i');
    Put16(b, 16, 2); Put16(b, 18, 1); // Visits H, then i.
    Put16(b, 20, 0x03a9); Put16(b, 24, 3); Put16(b, 26, 4);
    Put16(b, 28, 1); Put32(b, 30, 100); Put32(b, 34, 54); Put32(b, 38, 42);
    Put16(b, 42, 2); Put16(b, 44, 1); Put16(b, 46, 2); Put16(b, 48, 2); Put16(b, 50, 5); Put16(b, 52, 3);
    b[54] = 2; b[55] = 3; b[56] = 4;
    return b;
}
int main(int argc, char** argv) {
    auto b = Fixture();
    assert(DecodeLocalization(b, 100) == L"Hi");
    assert(DecodeLocalization(b, 102) == L"\u03a9");
    assert(DecodeLocalization(b, 105) == L"\u03a9Hi");
    for (uint32_t missing : {0, 99, 101, 103, 104, 106, 100 + 65536}) assert(!DecodeLocalization(b, missing));
    for (size_t n = 0; n < b.size(); ++n) {
        auto truncated = b; truncated.resize(n); assert(!DecodeLocalization(truncated, 105));
    }
    auto escaped = b; escaped.resize(57); Put16(escaped, 0, 2);
    Put16(escaped, 42, 0); Put16(escaped, 44, 2); escaped[54] = 2; escaped[55] = 0;
    assert(DecodeLocalization(escaped, 100) == L"Hi");
    Put16(escaped, 44, 3); escaped[54] = 255; escaped[55] = 0; escaped[56] = 2;
    assert(DecodeLocalization(escaped, 100) == L"Hi");
    Put16(escaped, 44, 2); assert(!DecodeLocalization(escaped, 100));
    auto cycle = b; Put16(cycle, 16, 3); Put16(cycle, 18, 3); assert(!DecodeLocalization(cycle, 100));
    auto invalid = b; Put16(invalid, 18, 7); assert(!DecodeLocalization(invalid, 100));
    invalid = b; Put32(invalid, 34, UINT32_MAX); assert(!DecodeLocalization(invalid, 100));
    invalid = b; Put16(invalid, 0, 256); assert(!DecodeLocalization(invalid, 100));
    invalid = b; Put16(invalid, 44, 3); Put16(invalid, 48, 2); assert(!DecodeLocalization(invalid, 102));
    // Optional private integration check. The user's archive bytes are never
    // copied into the repository or payload test fixtures.
    if (argc == 2) {
        std::ifstream input(argv[1], std::ios::binary);
        std::vector<uint8_t> real((std::istreambuf_iterator<char>(input)), {});
        assert(input && !real.empty());
        assert(DecodeLocalization(real, 276513) == L"MEDICINE");
        assert(DecodeLocalization(real, 276590) == L"THROWING KNIVES");
        auto description = DecodeLocalization(real, 282736);
        assert(description && description->size() > 100);
    }
    std::puts("Native localization decode, Unicode, malformed-input and private-asset tests passed");
}
