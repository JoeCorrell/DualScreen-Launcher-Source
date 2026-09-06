#include "plza_inventory_reader.h"
#include <cassert>
#include <iostream>
#include <map>
#include <vector>

using namespace PlzaInventory;
struct Memory {
    std::map<Address, std::vector<std::uint8_t>> regions;
    Address invalidate_at{}, control{};
    unsigned copies{};
    template <typename T> void put(Address address, const T& value) {
        for (auto& [base, bytes] : regions) {
            if (address >= base && address - base <= bytes.size() &&
                sizeof(T) <= bytes.size() - (address - base)) {
                std::memcpy(bytes.data() + address - base, &value, sizeof(T)); return;
            }
        }
        assert(false);
    }
    bool operator()(Address address, void* output, std::size_t size) {
        for (auto& [base, bytes] : regions) {
            if (address >= base && address - base <= bytes.size() &&
                size <= bytes.size() - (address - base)) {
                std::memcpy(output, bytes.data() + address - base, size);
                if (address == invalidate_at && ++copies == 1)
                    put(control + 0x1C, std::uint32_t{99});
                return true;
            }
        }
        return false;
    }
};

int main() {
    for (Address base : {0x100000000ULL, 0x700002000ULL}) {
        constexpr Address object = 0x200000, control = 0x300000, vector = 0x400000;
        Memory memory;
        memory.regions[base + 0x610A000].resize(4096);
        memory.regions[base + 0x291A000].resize(8192);
        memory.regions[base + 0x3D59000].resize(4096);
        memory.regions[object].resize(0x40 + sizeof(Records));
        memory.regions[control].resize(0x20);
        memory.regions[vector].resize(24);
        memory.put(base + Ready, std::uint32_t{1});
        memory.put(base + Components, std::array<Address, 2>{vector, vector + 16});
        memory.put(vector, Address{0x500000}); // unrelated/unmapped component
        memory.put(vector + 8, object);
        memory.put(object + 0x28, base + 0x3D59A50);
        memory.put(base + 0x3D59A50, base + 0x291AF34);
        memory.put(base + 0x291AF34,
                   std::array<std::uint32_t, 3>{0xD0009F00, 0x912C6000, 0xD65F03C0});
        memory.put(object + 0x38, control);
        memory.put(control, Address{0x1000001});
        memory.put(control + 0x10, object + 0x28);
        memory.put(control + 0x1A, std::uint16_t{2});
        memory.put(control + 0x1C, std::uint32_t{3});
        Records records{};
        for (auto& record : records) record.pouch = UINT32_MAX;
        records[17] = {0, 43, 1, 0};
        records[3] = {1, 6, 1, 0};
        memory.put(object + 0x40, records);
        Records output{};
        Address owner{};
        auto read = [&] { return ReadLiveBag(memory, base, output, owner); };
        assert(read() && owner == object && output == records);
        records[17].quantity = 42;
        memory.put(object + 0x40, records);
        assert(read() && output[17].quantity == 42);
        records[17].quantity = 0;
        memory.put(object + 0x40, records);
        assert(read() && output[17].quantity == 0); // last item disappears
        const auto previous = output;
        memory.put(control + 0x1A, std::uint16_t{0});
        assert(!read() && owner == 0 && output == previous);
        memory.put(control + 0x1A, std::uint16_t{2});
        memory.put(base + Ready, std::uint32_t{0});
        assert(!read());
        memory.put(base + Ready, std::uint32_t{1});
        memory.put(base + Components, std::array<Address, 2>{vector, vector + 8 * 257});
        assert(!read());
        memory.put(base + Components, std::array<Address, 2>{vector, vector + 16});
        memory.put(base + 0x291AF34, std::uint32_t{0xD503201F});
        assert(!read());
        memory.put(base + 0x291AF34, std::uint32_t{0xD0009F00});
        records[3].quantity = -1;
        memory.put(object + 0x40, records);
        assert(!read() && output == previous);
        records[3].quantity = 6;
        memory.put(object + 0x40, records);
        memory.invalidate_at = object + 0x40;
        memory.control = control;
        assert(!read() && output == previous); // object replaced during copy
        std::cout << "PASS relocated live bag: ownership, last item, lifecycle, bounds, type, race\n";
    }
}
