#include "NativeLocalization.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
using namespace ac2ds::native;
struct Fixture : Memory {
    mutable size_t calls{}, blobReads{};
    size_t fail{};
    mutable std::map<uint32_t, uint8_t> bytes;
    mutable uint32_t mutate{};
    bool Copy(uint32_t address, void* out, size_t size) const override {
        if (++calls == fail) return false;
        auto* result = static_cast<uint8_t*>(out);
        for (size_t i = 0; i < size; ++i) {
            auto found = bytes.find(address + uint32_t(i));
            if (found == bytes.end()) return false;
            result[i] = found->second;
        }
        if (address == 0x50000 || address == 0x60000) {
            ++blobReads;
            if (mutate) { bytes[mutate] ^= 1; mutate = 0; }
        }
        return true;
    }
    void Word(uint32_t address, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) bytes[address + i] = uint8_t(value >> (i * 8));
    }
    void Blob(uint32_t at, char value) {
        // One literal, one group, one base-ID string (ID 100).
        const uint8_t data[]{0,242,0,2, 0,0,0,0, 0,uint8_t(value),0,0,
            0,1, 0,0,0,100, 0,0,0,30, 0,0,0,26, 0,0,0,1, 0};
        for (unsigned i = 0; i < sizeof(data); ++i) bytes[at + i] = data[i];
    }
    Fixture() {
        Word(0x02212914, 0x10000); Word(0x1000c, 0x20000); Word(0x10010, 0x00010001);
        Word(0x20010, 0x30000); Word(0x20014, 0x31000);
        for (uint32_t i = 0; i < 2; ++i) {
            auto h = 0x30000 + i * 0x1000, r = 0x40000 + i * 0x1000;
            Word(h, r); Word(h + 4, 0); Word(h + 8, 0x80000000);
            Word(r, 0x01cacbcc); Word(r + 0x10, 0); Word(r + 0x14, 0);
            Word(r + 0x1c, 0x50000 + i * 0x10000); Word(r + 0x20, 31);
        }
        Blob(0x50000, 'A'); Blob(0x60000, 'B');
    }
};
int main() {
    Fixture f; LocalizationCache cache;
    assert(cache.Refresh(f)); assert(cache.Lookup(100) == L"A"); assert(!cache.Lookup(101));
    const auto reads = f.blobReads, calls = f.calls;
    assert(cache.Refresh(f) && f.blobReads == reads); assert(cache.StillCurrent(f));
    cache.Clear(); assert(!cache.Lookup(100));
    // Fail at every point of the first complete transaction, including chunks
    // and the second provenance pass; no old string may escape afterward.
    for (size_t i = 1; i <= calls; ++i) {
        Fixture broken; broken.fail = i; LocalizationCache c;
        assert(!c.Refresh(broken)); assert(!c.Lookup(100));
    }
    for (uint32_t address : {0x02212914u,0x1000cu,0x10010u,0x20010u,0x30000u,0x40014u,0x4001cu,0x40020u}) {
        Fixture changed; changed.mutate = address; LocalizationCache c;
        assert(!c.Refresh(changed)); assert(!c.Lookup(100));
    }
    Fixture language; LocalizationCache c;
    assert(c.Refresh(language) && c.Lookup(100) == L"A");
    language.Word(0x30008, 0); // The first reference unloads: second language wins.
    assert(c.Refresh(language) && c.Lookup(100) == L"B");
    language.Word(0x41014, 1); // Unknown uncompressed override is never ignored.
    assert(!c.Refresh(language) && !c.Lookup(100));
    for (uint32_t size : {0x200001u, UINT32_MAX}) {
        Fixture oversized; oversized.Word(0x40020, size); assert(!c.Refresh(oversized));
    }
    Fixture wrap; wrap.Word(0x4001c, UINT32_MAX - 8); assert(!c.Refresh(wrap));
    std::puts("Live localization cache, source priority, unload and interrupted-read tests passed");
}
