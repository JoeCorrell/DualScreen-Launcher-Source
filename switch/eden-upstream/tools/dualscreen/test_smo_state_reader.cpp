#include "../../src/android/app/src/main/jni/smo_state_reader.h"
#include "../../src/android/app/src/main/jni/smo_moon_reader.h"
#include "../../src/android/app/src/main/jni/smo_marker_reader.h"
#include "../../src/android/app/src/main/jni/smo_collection_reader.h"
#include <cassert>
#include <iostream>
#include <unordered_map>

using namespace SmoCompanion;
struct Memory {
    std::unordered_map<Address, std::uint8_t> data;
    std::size_t bytes{};
    int root_reads{};
    bool change_scene{};
    static constexpr Address Base = 0x100000000;
    template <class T> void Put(Address address, T value) {
        auto source = reinterpret_cast<const std::uint8_t*>(&value);
        for (std::size_t i = 0; i < sizeof(T); ++i) data[address + i] = source[i];
    }
    void Text(Address address, const char* value) {
        std::array<char, 64> text{};
        std::strncpy(text.data(), value, text.size() - 1);
        Put(address, text);
    }
    bool Read(Address address, void* out, std::size_t size) {
        bytes += size;
        if (address == Base + ApplicationInstance && ++root_reads == 2 && change_scene)
            Put<Address>(0x2000050b0, 0x200020000);
        auto destination = static_cast<std::uint8_t*>(out);
        for (std::size_t i = 0; i < size; ++i) {
            auto found = data.find(address + i);
            if (found == data.end()) return false;
            destination[i] = found->second;
        }
        return true;
    }
    Memory() {
        for (auto [address, value] : std::initializer_list<std::pair<Address, Address>>{
            {Base + ApplicationInstance, 0x200000000}, {0x200000028, 0x200001000},
            {0x200001038, 0x200002000}, {0x200002128, 0x200003000},
            {0x200003200, 0x200004000}, {0x200004010, 0x200005000},
            {0x2000050b0, 0x200006000}, {0x2000062d0, 0x200007000},
            {0x2000060a0, 0x200008000}, {0x200008008, 0x200009000},
            {0x200009090, 0x200007000}, {0x200007020, 0x20000a000},
            {0x200006090, 0x20000b000}, {0x20000b098, 0x20000c000},
            {0x20000c000, 0x20000d000}, {0x20000d000, 0x20000e000},
            {0x20000e050, 0x20000f000}, {0x20000a860, 0x200010000},
            {0x20000a718, 0x200011003}, {0x20000a770, 0x200012004},
            {0x20000aa00, 0x200013000}, {0x20000a5e8, 0x200014000},
            {0x2000060e0, 0x200015003},
            {0x200004000, Base + 0x1d99828}, {0x200005000, Base + 0x1d96ff0},
            {0x200006000, Base + 0x1d8bb80}, {0x200007000, Base + 0x1d996c8},
            {0x20000e000, Base + 0x1d779e8}}) Put(address, value);
        Put<std::int32_t>(0x20000c008, 2);
        Put<std::int32_t>(0x20000c00c, 1);
        Put(0x20000f008, std::array<float, 3>{1.f, 2.f, 3.f});
        Put<std::int32_t>(0x20000a5d8, 175);
        Put<std::int32_t>(0x20000a9f0, 0);
        Put<std::int32_t>(0x20000aa24, 1);
        Put<std::int32_t>(0x20001000c, 3);
        Put<std::uint8_t>(0x200010008, 0);
        Put<std::uint8_t>(0x200010010, 0);
        Put<std::uint8_t>(0x200010011, 0);
        Put<std::int32_t>(0x200013000, 10);
        Put<std::int32_t>(0x200014000, 4);
        Text(0x200011003, "Mario");
        Text(0x200012004, "MarioCaptain");
        Text(0x200015003, "CapWorldHomeStage");
    }
    void Map() {
        for (auto [address, value] : std::initializer_list<std::pair<Address, Address>>{
            {0x200007190, 0x200030000}, {0x200030008, 0x200031000},
            {0x200031000, 0x200032000}, {0x200032000, 0x200033003},
            {0x2000071e0, 0x200034000}, {0x200034010, 0x200007000},
            {0x200034008, 0x200035000}, {0x200035000, 0x200036000},
            {0x20000a7d8, 0x200037000}, {0x2000360e0, 0x200038000},
            {0x200038000, 0x200039000}}) Put(address, value);
        Text(0x200033003, "CapWorldHomeStage");
        Put<std::int32_t>(0x200030000, 17);
        // Native strings may have different bytes AFTER their NUL terminator.
        Put<std::uint8_t>(0x200033003 + 60, 73);
        Put<std::int32_t>(0x200034000, 17);
        Put<std::int32_t>(0x200037000, 2); // Different from placement scenario (1).
        Put<std::int32_t>(0x2000360d8, 1);
        for (Address map : {Address{0x200036000}, Address{0x200039000}}) {
            Put<std::uint32_t>(map + 0xb8, 2048);
            Put<std::uint32_t>(map + 0xbc, 2048);
            Put(map, std::array<float, 16>{.001f,0,0,.1f, 0,0,.001f,.2f,
                0,.001f,0,0, 0,0,0,1});
            // Deliberately different projection-only matrix; catch wrong +0x70.
            Put(map + 0x70, std::array<float, 16>{});
        }
        Put<std::int32_t>(0x2000360d0, -1);
        Put<std::int32_t>(0x2000390d0, 2);
    }
    void Moons() {
        Map();
        Put<Address>(0x20000a9a0, 0x200050000);
        Put<std::int32_t>(0x20000a9a8, 17);
        Put<Address>(0x20000a9b0, 0x200042000);
        Put<Address>(0x200042000, 0x200043000);
        Put<std::int32_t>(0x200043000, 3);
        Put<std::int32_t>(0x200043004, 128);
        Put<std::int32_t>(0x20003201c, 4);
        for (int i = 0; i < 1024; ++i) {
            Put<std::int32_t>(0x200050000 + i * 0x238 + 0x1cc, -1);
            Put<std::uint8_t>(0x200050000 + i * 0x238 + 0x1d0, 0);
        }
        for (int i = 0; i < 3; ++i) {
            const Address at = 0x200050000 + i * 0x238;
            Put(at + 0x1cc, std::array<std::uint8_t, 24>{});
            Put<std::uint8_t>(at + 0x1d0, i == 2 ? 1 : 0);
            Put<std::uint8_t>(at + 0x1d1, i == 0 ? 1 : 0);
            Put<std::int32_t>(at + 0x1e0, i == 0 ? 1 : 0);
        }
        Put<Address>(0x200050008, 0x200150003);
        Put<Address>(0x2000500a0, 0x200151005);
        Text(0x200150003, "CapWorldHomeStage");
        Text(0x200151005, "Obj123");
    }
};

int main() {
    {
        Memory memory;
        Snapshot state; Identity owner;
        assert(Reader(memory, Memory::Base).Read(state, owner));
        const Address list = 0x200090000, strings = 0x200092000, catalog = 0x200094000;
        memory.Put<Address>(owner.save+0x6f8, list);
        memory.Put<Address>(list+8, strings); memory.Text(strings, "Mario");
        memory.Put<Address>(list+0x58+8, strings+0x100); memory.Text(strings+0x100, "");
        memory.Put<std::int32_t>(owner.holder+0x110, 2);
        memory.Put<Address>(owner.holder+0x118, catalog);
        memory.Put<Address>(catalog, catalog+0x100); memory.Text(catalog+0x104, "Mario");
        memory.Put<Address>(catalog+8, catalog+0x200); memory.Text(catalog+0x204, "MarioPoncho");
        CollectionSnapshot result;
        assert(CollectionReader(memory).Read(Memory::Base, owner, state, CollectionKind::Outfit, result));
        assert(result.count == 2 && result.entries[0].owned == 1 && result.entries[1].owned == 0);
        assert(std::string(result.entries[0].name.data()) == "Mario");
        assert((result.entries[1].name == std::array<char,64>{})); // No locked-name leak.
        memory.Text(strings, "MarioPoncho");
        assert(CollectionReader(memory).Read(Memory::Base, owner, state, CollectionKind::Outfit, result));
        assert(!result.entries[0].owned && result.entries[1].owned);
        memory.Put<std::int32_t>(owner.holder+0x110, 65);
        CollectionSnapshot invalid;
        assert(!CollectionReader(memory).Read(Memory::Base, owner, state, CollectionKind::Outfit, invalid));
        assert(invalid.world == -1);
        memory.Put<Address>(owner.save+0x7c8, list);
        memory.Text(strings, "Frog");
        memory.Put<Address>(list+0x98+8, strings+0x100);
        assert(CollectionReader(memory).Read(Memory::Base, owner, state, CollectionKind::Capture, result));
        assert(result.count == 52 && result.entries[0].owned && !result.entries[1].owned);
        assert(std::string(result.entries[0].name.data()) == "Frog");
        memory.Text(strings, "../bad");
        assert(!CollectionReader(memory).Read(Memory::Base, owner, state, CollectionKind::Capture, invalid));
    }
    {
        Memory memory;
        Snapshot state; Identity owner;
        assert(Reader(memory, Memory::Base).Read(state, owner));
        const Address catalog = 0x200090000, records = 0x200091000, strings = 0x200092000;
        memory.Put<Address>(Memory::Base+0x1e4ecf8, catalog+0x100);
        memory.Put<std::int32_t>(catalog+0x100, 2);
        memory.Put<Address>(Memory::Base+0x1e4ed00, catalog);
        memory.Put<Address>(catalog, strings); memory.Text(strings, "BgmTest");
        memory.Put<Address>(catalog+8, 0);
        memory.Put<Address>(catalog+0x28, strings); memory.Put<Address>(catalog+0x30, strings+0x100);
        memory.Text(strings+0x100, "Retro");
        memory.Put<std::int32_t>(owner.save+0x830, 1);
        memory.Put<std::int32_t>(owner.save+0x834, 2);
        memory.Put<Address>(owner.save+0x838, records);
        memory.Put<Address>(records, records+0x100);
        memory.Put<Address>(records+0x100, strings);
        memory.Put<Address>(records+0x108, strings+0x100);
        memory.Put<std::uint8_t>(records+0x110, 1);
        CollectionSnapshot result;
        assert(CollectionReader(memory).Read(Memory::Base, owner, state, CollectionKind::Music, result));
        assert(result.count == 2 && !result.entries[0].owned && result.entries[1].owned);
        assert(std::string(result.entries[1].variant.data()) == "Retro");
        memory.Put<std::uint8_t>(records+0x110, 0);
        assert(CollectionReader(memory).Read(Memory::Base, owner, state, CollectionKind::Music, result));
        assert(!result.entries[1].owned && !result.entries[1].name[0]);
    }
    {
        Memory memory;
        Snapshot frame; Identity identity;
        assert(Reader(memory, Memory::Base).Read(frame, identity));
        assert(frame.status == Status::Active && frame.coins == 175 && frame.regional_coins == 6);
        assert(frame.health == 3 && frame.max_health == 3);
        assert(frame.position[2] == 3.f && std::string(frame.hat.data()) == "MarioCaptain");
        assert(memory.bytes < 4096);
    }
    for (int failure = 0; failure < 7; ++failure) {
        Memory memory;
        switch (failure) {
        case 0: memory.Put<Address>(Memory::Base + ApplicationInstance, 0); break;
        case 1: memory.Put<Address>(0x200006000, Memory::Base + 0x1234); break;
        case 2: memory.Put<std::int32_t>(0x20000c00c, 0); break;
        case 3: memory.Put<std::int32_t>(0x20000a9f0, 17); break;
        case 4: memory.change_scene = true; break;
        case 5: memory.Put<Address>(0x200009090, 0x200019000); break;
        case 6: memory.Put<std::int32_t>(0x200014000, 20); break;
        }
        Snapshot frame; Identity identity;
        assert(!Reader(memory, Memory::Base).Read(frame, identity));
        assert(frame.status == Status::Loading && frame.world == -1 && frame.coins == 0);
    }
    for (int bits = 0; bits < 8; ++bits) {
        Memory memory;
        memory.Put<std::uint8_t>(0x200010008, bits & 1);
        memory.Put<std::uint8_t>(0x200010010, (bits >> 1) & 1);
        memory.Put<std::uint8_t>(0x200010011, (bits >> 2) & 1);
        Snapshot frame; Identity identity;
        assert(Reader(memory, Memory::Base).Read(frame, identity));
        assert(frame.max_health == ((bits & 4) ? 3 : ((bits & 1) ? 6 : 3) + ((bits & 2) ? 3 : 0)));
    }
    for (int mode = 0; mode < 7; ++mode) {
        Memory memory; memory.Map();
        if (mode == 1) memory.Put<std::int32_t>(0x200037000, 3);
        if (mode == 2) memory.Text(0x200015003, "CapWorldTowerStage");
        if (mode == 3) memory.Put<float>(0x200039000, std::nanf(""));
        if (mode == 4) memory.Put(0x200039000, std::array<float, 16>{});
        if (mode == 5) memory.Put<std::int32_t>(0x2000360d8, 100000);
        if (mode == 6) memory.Put<Address>(0x200034010, 0x200031000);
        Snapshot frame; Identity identity;
        Reader reader(memory, Memory::Base);
        assert(reader.Read(frame, identity));
        assert(frame.map_valid == (mode < 2 ? 1u : 0u));
        if (mode < 2) {
            assert(std::string(frame.map_texture.data()) ==
                (mode == 0 ? "CapWorldHomeStage2" : "CapWorldHomeStage"));
            assert(frame.map_view_projection[3] == .1f);
            assert(frame.map_position == frame.position);
        }
        assert(memory.bytes < 4096);
        // A reused reader must reset its budget, not fail after several polls.
        for (int i = 0; i < 20; ++i) assert(reader.Read(frame, identity));
    }
    {
        Memory memory;
        memory.Put<Address>(0x20000a858, 0x200040000);
        std::array<std::int32_t, 20> counts{};
        counts[0] = 8; counts[19] = 3;
        memory.Put(0x200040000, counts);
        Snapshot frame; Identity identity;
        assert(Reader(memory, Memory::Base).Read(frame, identity));
        assert(frame.world_moons == 8 && frame.total_moons == 11);
        counts[19] = -1;
        memory.Put(0x200040000, counts);
        assert(Reader(memory, Memory::Base).Read(frame, identity));
        assert(frame.world_moons == -1 && frame.total_moons == -1);
    }
    {
        Memory memory; memory.Moons();
        Snapshot state; Identity identity;
        assert(Reader(memory, Memory::Base).Read(state, identity));
        state.generation = 7;
        MoonReader reader; MoonSnapshot moons;
        assert(reader.Read(memory, Memory::Base, identity, state, moons));
        assert(moons.count == 2 && moons.generation == 7 && moons.world == 0);
        assert(moons.entries[0].flags == 3);
        assert(std::string(moons.entries[0].placement.data()) == "Obj123");
        assert(moons.entries[1].stage[0] == 0 && moons.entries[1].placement[0] == 0);
        const auto bytes_before = memory.bytes;
        assert(reader.Read(memory, Memory::Base, identity, state, moons));
        assert(memory.bytes - bytes_before < 4096); // Metadata was indexed once.
        memory.Put<std::int32_t>(0x200037000, 4);
        assert(reader.Read(memory, Memory::Base, identity, state, moons));
        assert(moons.count == 3 && moons.entries[2].flags == 4);
        memory.Put<std::int32_t>(0x2000501e0, 0);
        assert(reader.Read(memory, Memory::Base, identity, state, moons));
        // Vanilla list exposes collected names even without the separate unlock flag.
        assert(moons.entries[0].flags == 3);
        assert(std::string(moons.entries[0].placement.data()) == "Obj123");
        memory.Put<std::uint8_t>(0x2000501d1, 0);
        assert(reader.Read(memory, Memory::Base, identity, state, moons));
        assert(moons.entries[0].stage[0] == 0); // Even previously cached names stay hidden.
        memory.Put<std::int32_t>(0x2000501cc, 3);
        MoonSnapshot unavailable;
        assert(!reader.Read(memory, Memory::Base, identity, state, unavailable));
        assert(unavailable.world == -1 && unavailable.count == 0);
        memory.Put<std::int32_t>(0x2000501cc, 0);
        const Address extra = 0x200050000 + 3 * 0x238;
        memory.Put(extra + 0x1cc, std::array<std::uint8_t, 24>{});
        memory.Put<std::int32_t>(0x200043000, 4); // Native registration signal changed.
        assert(reader.Read(memory, Memory::Base, identity, state, moons));
        assert(moons.count == 4 && moons.entries[3].index == 3);
    }
    {
        Memory memory; memory.Map();
        constexpr Address save = 0x20000a000, records = 0x200080000;
        memory.Put<std::int32_t>(save + 0xa44, 0);
        memory.Put<Address>(save + 0xa30, 0x200070000);
        memory.Put<std::int32_t>(0x2000701d0, 0);
        memory.Put<std::int32_t>(save + 0xa28, -1);
        memory.Put<Address>(save + 0x330, 0x200015003);
        memory.Put<Address>(save + 0x9e8, 0x200071000);
        memory.Put<Address>(0x200071000, records);
        for (int i = 0; i < 17; ++i) {
            memory.Put<std::uint32_t>(records + i * 0x148 + 0x130, 0);
            memory.Put<std::uint8_t>(records + i * 0x148 + 0x140, 0);
        }
        // Physical record 2 is ordinal 1: filtered record 1 must not shift identity.
        for (int i : {0, 2}) {
            memory.Put<std::uint32_t>(records + i * 0x148 + 0x130, 2);
            memory.Put<std::uint8_t>(records + i * 0x148 + 0x140, i == 2);
        }
        memory.Put<std::uint32_t>(records + 0x148 + 0x130, 4);
        memory.Put<std::uint8_t>(records + 0x148 + 0x140, 1);
        memory.Put<Address>(records + 2 * 0x148 + 8, 0x200015003);
        memory.Put<Address>(records + 2 * 0x148 + 0xa0, 0x200072000);
        memory.Text(0x200072000, "obj655");
        memory.Put(records + 2 * 0x148 + 0x134, std::array<float,3>{2,3,4});
        memory.Put<Address>(save + 0xa58, 0x200073000);
        memory.Put<Address>(0x200073008, 0x200074000); memory.Text(0x200074000, "");
        memory.Put<Address>(save + 0xa68, 0x200075000);
        for (int i = 0; i < 4; ++i) memory.Put<std::uint8_t>(0x20007500c + i * 0x68, 0);
        memory.Put<Address>(save + 0x6a8, 0x200076000);
        memory.Put<Address>(0x200076020, 0x200077000);
        memory.Put<Address>(0x200076048, 0x200078000);
        memory.Put<std::int32_t>(0x200076034, 1);
        memory.Put<std::uint8_t>(0x20007700e, 0);
        memory.Put<std::uint8_t>(0x20007800e, 1);
        memory.Put<std::uint8_t>(0x200077000, 1);
        memory.Put(save + 0xa7c, std::array<float,3>{8,9,10});
        Snapshot state; Identity identity;
        assert(Reader(memory, Memory::Base).Read(state, identity));
        state.generation = 7;
        MarkerSnapshot markers;
        assert(MarkerReader(memory).Read(Memory::Base, identity, state, markers));
        assert(markers.count == 2 && markers.entries[0].id == 1);
        assert(markers.entries[0].kind == MarkerKind::Checkpoint);
        assert(std::strcmp(markers.entries[0].label.data(), "obj655") == 0);
        assert(markers.entries[1].kind == MarkerKind::Odyssey);
        assert(markers.generation == 7);
        memory.Put<std::int32_t>(save + 0xa44, 1);
        assert(MarkerReader(memory).Read(Memory::Base, identity, state, markers));
        assert(markers.count == 0); // Native race suppression.
        memory.Put<std::int32_t>(save + 0xa44, 0);
        memory.Put<std::int32_t>(save + 0xa28, 32);
        MarkerSnapshot unavailable;
        assert(!MarkerReader(memory).Read(Memory::Base, identity, state, unavailable));
        assert(unavailable.world == -1);
    }
    std::cout << "PASS: stable snapshots, invalid transitions, health modes, map variants and discovered markers\n";
}
