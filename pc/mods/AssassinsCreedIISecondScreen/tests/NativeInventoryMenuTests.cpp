#include "NativeInventoryMenu.h"
#include <cassert>
#include <cstdio>
#include <map>
using namespace ac2ds::native;
struct MenuFixture final : Memory {
    std::map<uint32_t, uint8_t> bytes;
    mutable unsigned reads{}, matchingReads{};
    unsigned failAt = UINT32_MAX;
    uint32_t mutateAddress{};
    bool Copy(uint32_t address, void* output, size_t size) const override {
        if (++reads == failAt) return false;
        auto* target = static_cast<uint8_t*>(output);
        for (size_t i = 0; i < size; ++i) {
            auto found = bytes.find(address + uint32_t(i));
            if (found == bytes.end()) return false;
            target[i] = found->second;
        }
        if (mutateAddress == address && ++matchingReads > 1) target[0] ^= 1;
        return true;
    }
    void Word(uint32_t address, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) bytes[address + i] = uint8_t(value >> (i * 8));
    }
    void Array(uint32_t address, uint32_t data, uint32_t count) { Word(address, data); Word(address + 4, count << 16 | count); }
    void Reference(uint32_t address, uint32_t value) { Word(address, value); Word(address + 4, 1); Word(address + 8, 0x80000000); }
    MenuFixture() {
        Word(0x223f81c, 0x10000); Array(0x1000c, 0x11000, 1); Word(0x11000, 0x12000);
        Word(0x12000, 0x01e610c8); Array(0x12008, 0x13000, 1); Word(0x13000, 0x14000);
        Reference(0x14000, 0x15000); Word(0x15010, 3); Array(0x15008, 0x16000, 5);
        for (uint32_t i = 0; i < 5; ++i) {
            uint32_t entry = 0x20000 + i * 0x100, definition = 0x30000 + i * 0x100;
            Word(0x16000 + i * 4, 0x17000 + i * 16); Reference(0x17000 + i * 16, entry);
            for (uint32_t j = 0; j < 12; ++j) Word(entry + j * 4, 0);
            Word(entry, 0x01e61020); Word(entry + 8, 0x18000 + i * 16);
            Reference(0x18000 + i * 16, definition); Word(entry + 0x14, 10 + i);
            Word(entry + 0x24, 280000 + i); Word(entry + 0x18, 270000 + i);
            Word(definition, 0x01e699bc); Word(definition + 0xc, ReloadCategory(10 + i));
            Word(definition + 0x30, i == 4 ? 1 : 0);
        }
    }
};
int main() {
    MenuFixture fixture;
    auto value = ReadReloadDefinitions(fixture);
    assert(value && value->size() == 5 && value->back().locked && !value->front().locked);
    assert(value->front().titleId == 270000 && value->front().descriptionId == 280000);
    assert((*value)[1].category == 8 && (*value)[4].category == 7);
    auto successfulReads = fixture.reads;
    for (unsigned i = 1; i <= successfulReads; ++i) {
        MenuFixture interrupted; interrupted.failAt = i; assert(!ReadReloadDefinitions(interrupted));
    }
    for (uint32_t address : {0x223f81c, 0x1000c, 0x11000, 0x12008, 0x13000, 0x14000, 0x15008,
            0x15010, 0x16000, 0x17000, 0x18000, 0x20000, 0x30030}) {
        MenuFixture transition; transition.mutateAddress = address; assert(!ReadReloadDefinitions(transition));
    }
    MenuFixture unknown; unknown.Word(0x30000, 0x01e61020); assert(!ReadReloadDefinitions(unknown));
    MenuFixture wrongCategory; wrongCategory.Word(0x3000c, 1); assert(!ReadReloadDefinitions(wrongCategory));
    MenuFixture duplicate; duplicate.Word(0x20114, 10); duplicate.Word(0x3010c, 5); assert(!ReadReloadDefinitions(duplicate));
    MenuFixture unresolved; unresolved.Word(0x18008, 0); auto hidden = ReadReloadDefinitions(unresolved);
    assert(hidden && hidden->size() == 4); // An unresolved item never becomes an unlocked row.
    std::puts("Native inventory definitions, lock flags and transition tests passed");
}
