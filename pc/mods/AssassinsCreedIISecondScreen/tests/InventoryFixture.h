#pragma once
#include "NativeInventory.h"
#include <cassert>
#include <map>
#include <cstdio>
using namespace ac2ds::native;
struct MemoryFixture final : Memory {
    std::map<uint32_t, uint8_t> bytes;
    mutable unsigned reads{};
    unsigned failAt = UINT32_MAX;
    uint32_t mutateAddress{};
    mutable unsigned matchingReads{};
    bool Copy(uint32_t address, void* output, size_t size) const override {
        if (++reads == failAt) return false;
        auto* target = static_cast<uint8_t*>(output);
        for (size_t i = 0; i < size; ++i) {
            auto found = bytes.find(address + uint32_t(i));
            if (found == bytes.end()) return false;
            target[i] = found->second;
        }
        if (mutateAddress && address == mutateAddress && ++matchingReads > 1) target[0] ^= 1;
        return true;
    }
    void Word(uint32_t address, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) bytes[address + i] = uint8_t(value >> (i * 8));
    }
    void Array(uint32_t address, uint32_t data, uint32_t size) {
        Word(address, data); Word(address + 4, (size << 16) | size);
    }
    MemoryFixture() {
        Word(0x10000, 0x1e57e40); Array(0x10004, 0x11000, 2);
        Word(0x223f4f4, 0x20000); Array(0x20050, 0x21000, 2);
        for (uint32_t i = 0; i < 2; ++i) {
            uint32_t container = 0x12000 + i * 0x100, category = i ? 2 : 7;
            Word(0x11000 + i * 4, container);
            for (uint32_t j = 0; j < 12; ++j) Word(container + j * 4, 0);
            Word(container, i ? 0x1e58098 : 0x1e790d8);
            Word(container + 8, category); Word(container + 0xc, 1);
            Word(container + 0x10, i ? 99 : 8); // Slot containers must ignore this scalar.
            Array(container + 0x18, 0x13000, i ? 3 : 1);
            uint32_t settings = 0x22000 + i * 0x100, level = 0x24000 + i * 0x100;
            Word(0x21000 + i * 4, settings); Word(settings + 8, category);
            Array(settings + 0x10, 0x23000 + i * 4, 1); Word(0x23000 + i * 4, level);
            Word(level + 8, 1); Word(level + 0xc, i ? 20 : 10);
        }
    }
};
struct OwnedFixture final {
    MemoryFixture memory;
    OwnedFixture() {
        memory.Word(0x30000, 0x01e797c8); memory.Word(0x30004, 0x01e797c0);
        memory.Word(0x3000c, 0x01e57e40); memory.Array(0x30010, 0x11000, 2);
        memory.Word(0x400d5, 0x0103); memory.Word(0x400f4, 3);
        memory.Array(0x40074, 0x41000, 2); memory.Word(0x41004, 0x42000);
        memory.Word(0x42000, 0x01e9cd48); memory.Word(0x42008, 0x40000); memory.Word(0x42010, 0x43000);
        memory.Word(0x43020, 0x43000); memory.Array(0x43058, 0x44000, 1); memory.Word(0x4400c, 0x30000);
    }
};
