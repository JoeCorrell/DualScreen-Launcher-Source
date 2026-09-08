#include "NativeDatabase.h"
#include <cassert>
#include <cstdio>
#include <map>
using namespace ac2ds::native;
struct Fixture : Memory {
    mutable std::map<uint32_t, uint8_t> bytes;
    mutable size_t calls{};
    size_t fail{};
    mutable uint32_t mutate{};
    bool Copy(uint32_t a, void* out, size_t n) const override {
        if (++calls == fail) return false;
        for (size_t i = 0; i < n; ++i) {
            auto found = bytes.find(a + uint32_t(i)); if (found == bytes.end()) return false;
            static_cast<uint8_t*>(out)[i] = found->second;
        }
        if (a == 0x62038 && mutate) { bytes[mutate] ^= 1; mutate = 0; }
        return true;
    }
    void Word(uint32_t a, uint32_t n) { for (uint32_t i = 0; i < 4; ++i) bytes[a + i] = uint8_t(n >> (i * 8)); }
    void Ref(uint32_t a, uint32_t n) { Word(a, n); Word(a + 4, 0); Word(a + 8, 0x80000000); }
    void Record(uint32_t a, uint32_t type, size_t size) { for (size_t i = 0; i < size; ++i) bytes[a + uint32_t(i)] = 0; Word(a, type); }
    Fixture() {
        Word(0x22553b0, 0x10000); Word(0x10000, 0x1ebb9b8); bytes[0x10162] = 0;
        Word(0x10010, 0x20000); Word(0x10014, 0x20002); Word(0x20000, 0x21000); Word(0x20004, 0x22000);
        Record(0x21000, 0x1ebb8f4, 100); Word(0x21010, 7); Word(0x21014, 2); Word(0x2103c, 101); Word(0x21048, 102); bytes[0x21060] = 1;
        Record(0x22000, 0x1ebb8f4, 100); Word(0x22014, 1); Word(0x2203c, 103); Word(0x22048, 104);
        Word(0x10020, 0x30000); Word(0x10024, 0x20002); Word(0x30000, 0x31000); Word(0x30004, 0x32000);
        Record(0x31000, 0x1eba3ec, 60); Word(0x31014, 201); Word(0x31020, 202); Word(0x31010, 0x33000); Ref(0x33000, 0x32000);
        Word(0x31008, 0xdeadbeef); Word(0x3100c, 0x34000); Ref(0x34000,0); Word(0x3400c,0x12345678);
        Record(0x32000, 0x1eba3ec, 60); Word(0x32014, 203); Word(0x32020, 204);
        Word(0x10018, 0x40000); Word(0x1001c, 0x20002); Word(0x40000, 0x41000); Word(0x40004, 0x42000);
        Record(0x41000, 0x1ebbae8, 52); Word(0x41018, 301); Word(0x41024, 302);
        Record(0x42000, 0x1ebbae8, 52); Word(0x42018, 303); Word(0x42024, 304);
        Word(0x50180, 0x51000); Ref(0x51000, 0x52000); Word(0x5200c, 0x53000); Word(0x52010, 0x20002);
        Word(0x53000, 0x54000); Word(0x53004, 0x55000); Ref(0x54000, 0x61000); Ref(0x55000, 0x62000);
        Word(0x61010, 1); bytes[0x61038] = 0; Word(0x62010, 0); bytes[0x62038] = 0;
    }
};
int main() {
    Fixture f; auto db = ReadDatabase(f, 0x50000); assert(db && db->size() == 3);
    assert((*db)[0].category == 2 && (*db)[0].title == 101 && (*db)[0].description == 102);
    assert((*db)[1].title == 201 && (*db)[1].artworkId == 0x12345678);
    assert((*db)[2].category == 7 && !(*db)[2].decoded);
    for (size_t i = 1; i <= f.calls; ++i) { Fixture broken; broken.fail = i; assert(!ReadDatabase(broken, 0x50000)); }
    for (uint32_t a : {0x22553b0u,0x10010u,0x10018u,0x10020u,0x10162u,0x20000u,0x21060u,0x2103cu,
            0x31039u,0x33000u,0x41018u,0x41031u,0x50180u,0x51000u,0x5200cu,0x53000u,0x54000u,0x61010u,0x61038u}) {
        Fixture changed; changed.mutate = a; assert(!ReadDatabase(changed, 0x50000));
    }
    Fixture special; special.bytes[0x41031] = 1; db = ReadDatabase(special, 0x50000); assert(db && db->size() == 2);
    special.bytes[0x10162] = 0x20; db = ReadDatabase(special, 0x50000); assert(db && db->size() == 3);
    Fixture decode; decode.bytes[0x61038] = 1; db = ReadDatabase(decode, 0x50000); assert(db && (*db)[2].decoded);
    Fixture locked; locked.bytes[0x21060] = 0; locked.bytes[0x31039] = 1;
    db = ReadDatabase(locked, 0x50000); assert(db && db->size() == 2 && (*db)[0].title == 203);
    Fixture unknown; unknown.Word(0x21000, 0x1ebb8f5); assert(!ReadDatabase(unknown, 0x50000));
    Fixture mismatch; mismatch.Word(0x52010, 0x10001); assert(!ReadDatabase(mismatch, 0x50000));
    Fixture overflow; overflow.Word(0x10010, UINT32_MAX - 4); assert(!ReadDatabase(overflow, 0x50000));
    std::puts("Database discovery, letter replacement, live Codex state and transition tests passed");
}
