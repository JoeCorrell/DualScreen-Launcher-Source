#include "InventoryFixture.h"
int main() {
    MemoryFixture fixture;
    auto value = ReadInventory(fixture, 0x10000);
    assert(value && value->containers.size() == 2);
    assert(value->containers[0].rechargeable && value->containers[0].quantity == 8 && value->containers[0].capacity == 10);
    assert(!value->containers[1].rechargeable && value->containers[1].quantity == 3 && value->containers[1].capacity == 20);
    const auto successfulReads = fixture.reads;
    for (unsigned i = 1; i <= successfulReads; ++i) {
        MemoryFixture interrupted; interrupted.failAt = i;
        assert(!ReadInventory(interrupted, 0x10000));
    }
    for (uint32_t address : {0x10004, 0x12000, 0x22010, 0x223f4f4}) {
        MemoryFixture transition; transition.mutateAddress = address;
        assert(!ReadInventory(transition, 0x10000));
    }
    fixture.Word(0x12010, 11); assert(!ReadInventory(fixture, 0x10000)); // Torn count/capacity.
    fixture.Word(0x12010, 8); fixture.Word(0x24008, 2);
    assert(!ReadInventory(fixture, 0x10000)); // Unknown current pouch level.
    fixture.Word(0x24008, 1); fixture.Word(0x12100, 0x1e00000);
    assert(!ReadInventory(fixture, 0x10000)); // Untraced subclass.
    fixture.Word(0x12100, 0x1e58098); fixture.Array(0x10004, 0xfffffff0, 10);
    assert(!ReadInventory(fixture, 0x10000)); // Wrapped pointer table.
    fixture.Array(0x10004, 0x11000, 129); assert(!ReadInventory(fixture, 0x10000));

    OwnedFixture owned;
    auto playerInventory = ReadPlayerInventory(owned.memory, 0x40000);
    assert(playerInventory && playerInventory->identity == 0x3000c && playerInventory->containers.size() == 2);
    const auto ownedReads = owned.memory.reads;
    for (unsigned i = 1; i <= ownedReads; ++i) {
        OwnedFixture interrupted; interrupted.memory.failAt = i;
        assert(!ReadPlayerInventory(interrupted.memory, 0x40000));
    }
    for (uint32_t address : {0x400d5, 0x400f4, 0x40074, 0x42008, 0x42010, 0x43020, 0x43058, 0x4400c, 0x30000}) {
        OwnedFixture transition; transition.memory.mutateAddress = address;
        assert(!ReadPlayerInventory(transition.memory, 0x40000));
    }
    for (uint32_t cache : {0xfe03, 0xff03, 0x0203, 0x0104}) {
        OwnedFixture unresolved; unresolved.memory.Word(0x400d5, cache);
        assert(!ReadPlayerInventory(unresolved.memory, 0x40000));
    }
    OwnedFixture wrongPlayer; wrongPlayer.memory.Word(0x42008, 0x50000);
    assert(!ReadPlayerInventory(wrongPlayer.memory, 0x40000));
    OwnedFixture wrongType; wrongType.memory.Word(0x30000, 0x01e797c0);
    assert(!ReadPlayerInventory(wrongType.memory, 0x40000));
    OwnedFixture empty; empty.memory.Array(0x43058, 0x44000, 0);
    assert(!ReadPlayerInventory(empty.memory, 0x40000));
    OwnedFixture overflow; overflow.memory.Word(0x4400c, UINT32_MAX - 4);
    assert(!ReadPlayerInventory(overflow.memory, 0x40000));
    assert(!ReadPlayerInventory(owned.memory, UINT32_MAX - 0x70));
    std::puts("Native inventory quantity/capacity and transition tests passed");
}
