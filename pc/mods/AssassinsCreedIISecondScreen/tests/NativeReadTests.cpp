#include "NativeRead.h"
#include <cassert>
#include <map>
#include <cstdio>
using namespace ac2ds::native;
struct TestMemory : Memory {
    std::map<uint32_t, uint8_t> bytes;
    mutable unsigned reads{};
    unsigned failAfter = UINT32_MAX;
    bool Copy(uint32_t address, void* output, size_t count) const override {
        if (++reads > failAfter) return false;
        auto* target = static_cast<uint8_t*>(output);
        for (size_t i = 0; i < count; ++i) {
            auto it = bytes.find(address + uint32_t(i)); if (it == bytes.end()) return false;
            target[i] = it->second;
        }
        return true;
    }
    void Put(uint32_t address, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) bytes[address + i] = uint8_t(value >> (8 * i));
    }
    void Ref(uint32_t address, uint32_t object, uint32_t flag = 0x80000000) {
        Put(address, object); Put(address + 4, 1); Put(address + 8, flag);
    }
};
int main() {
    TestMemory memory;
    memory.Put(0x2212490, 0x10000); memory.Put(0x2212570, 0x11000); memory.Put(0x223f814, 0x12000);
    memory.Put(0x110d0, UINT32_MAX); memory.Put(0x1004c, 0);
    Roots roots{}; assert(ReadRoots(memory, roots) && !roots.world && !roots.player);
    memory.Put(0x1004c, 0xffc00000); memory.Put(0xffc0053c, 0x13000);
    memory.Put(0x13004, 0x14000); memory.Put(0x2214d00, 0x15000);
    memory.Put(0x15024, 0); memory.Put(0x14000, 0x16000);
    assert(ReadRoots(memory, roots) && roots.world == 0xffc00000 && roots.player == 0x16000);
    memory.Put(0x15024, UINT32_MAX); assert(!ReadRoots(memory, roots));
    memory.Put(0x15024, 0); memory.Put(0x110d0, 0); memory.Put(0x110c4, 0x17000);
    memory.Ref(0x17000, 0xffc00000, 1); // An unresolved handle must not expose its stale object.
    assert(ReadRoots(memory, roots) && !roots.world && !roots.player);
    memory.Ref(0x17000, 0xffc00000); assert(ReadRoots(memory, roots) && roots.player == 0x16000);
    memory.Put(0x12170, 0x18000); memory.Ref(0x18000, 0x19000);
    memory.Put(0x1900c, 0x1a000); memory.Put(0x19012, 0x4003);
    for (uint32_t i = 0; i < 3; ++i) {
        memory.Put(0x1a000 + i * 4, 0x1b000 + i * 16);
        memory.Ref(0x1b000 + i * 16, 0x1c000 + i * 32);
        memory.Put(0x1c008 + i * 32, i + 1);
        memory.Put(0x1c010 + i * 32, i);
    }
    auto value = ReadCollection(memory, 0x12000, 0x170);
    assert(value && value->completed == 1 && value->total == 3 && value->completedValue == 3);
    memory.Ref(0x1b010, 0x1c020, 1); assert(!ReadCollection(memory, 0x12000, 0x170));
    memory.Ref(0x1b010, 0x1c020);
    memory.reads = 0; memory.failAfter = 12;
    assert(!ReadCollection(memory, 0x12000, 0x170)); // Mid-copy destruction does not become a partial total.
    memory.failAfter = UINT32_MAX; memory.Put(0x19012, 5000);
    assert(!ReadCollection(memory, 0x12000, 0x170));
    uint32_t ignored{}; assert(!memory.Field(0xfffffff0, 0x170, ignored));
    std::puts("Native root/collection lifetime and bounds tests passed");
}
