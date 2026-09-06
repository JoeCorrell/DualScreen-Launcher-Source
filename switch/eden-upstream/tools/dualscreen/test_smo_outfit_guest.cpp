// Executes the actual guest helper with checked native-call adapters. This is
// a contract test, not a substitute for an on-device native ABI/reload test.
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <type_traits>
#include <vector>
#include "../../src/core/loader/smo_outfit_protocol.h"
using u64 = std::uint64_t;
constexpr u64 Base = 0x1000000, Scene = 0x10000, Holder = 0x11000, Save = 0x12000;
constexpr u64 Info = 0x14000, Players = 0x15000, Array = 0x16000, Player = 0x17000;
constexpr u64 Life = 0x18000, Scenarios = 0x19000, Starts = 0x1b000;
alignas(8) std::array<unsigned char, 0x10000> bytes{};
bool waiting = true, alive = true, owned = true;
float position[3] = {}, rotation[4] = {};
std::vector<u64> mutations;
unsigned lookups = 0, start_constructors = 0;
template<class T> static T& at(u64 object, unsigned offset) {
    assert(object + offset >= Scene && object + offset + sizeof(T) <= Scene + bytes.size());
    return *reinterpret_cast<T*>(bytes.data() + object + offset - Scene);
}
template<class T> u64 argument(T value) {
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<u64>(value);
    else return static_cast<u64>(value);
}
template<class R, class... Args> static R call(u64 address, Args... args) {
    const auto rva = address - Base;
    const std::array<u64, 8> a{argument(args)...};
    if constexpr (std::is_void_v<R>) {
        if (rva == 0x9a3450) { std::memset(reinterpret_cast<void*>(a[0]), 0, 32); return; }
        if constexpr (sizeof...(Args) == 7) {
        if (rva == 0x2e2c48) {
            assert(sizeof...(Args) == 7 && a[2] == 0 && a[4] == 0);
            assert(a[3] && std::strcmp(reinterpret_cast<char*>(a[3]), SmoOutfit::ReloadStartId) == 0);
            auto* start = reinterpret_cast<unsigned char*>(a[0]);
            std::memcpy(start, &a[1], 8);
            std::memcpy(start + 0xb0, reinterpret_cast<void*>(a[5]), 12);
            std::memcpy(start + 0xbc, reinterpret_cast<void*>(a[6]), 16);
            ++start_constructors; return;
        }
        if (rva == 0x248cc0) {
            assert(sizeof...(Args) == 7 && a[1] == Holder && a[4] == 0 && a[5] == u64(-1) && a[6] == 0);
            assert(a[2] && std::strcmp(reinterpret_cast<char*>(a[2]), SmoOutfit::ReloadStartId) == 0);
            assert(a[3] && std::strcmp(reinterpret_cast<char*>(a[3]), reinterpret_cast<char*>(at<u64>(Scene,0xe0))) == 0);
            return;
        }
        }
        assert(rva == 0x52bb5c || rva == 0x52bb90 || rva == 0x52b4ec ||
               rva == 0x52b530 || rva == 0x52f6f4 || rva == 0x4cc73c);
        mutations.push_back(rva);
        if (rva == 0x52b4ec || rva == 0x52b530) {
            assert(a[0] == Holder);
            at<u64>(Save,rva == 0x52b4ec ? 0x718 : 0x770) = a[1];
        }
        if (rva == 0x52f6f4) { assert(a[0] == Holder && a[2] == 0); at<uint8_t>(Holder,0x49) = 1; }
        if (rva == 0x4cc73c) assert(a[0] == Scene);
    } else if constexpr (std::is_same_v<R, bool>) {
        if (rva == 0x959c58) { assert(a[1] == Base + 0x1d78918); return waiting; }
        if (rva == 0x421754) return alive;
        assert(rva == 0x54c498 || rva == 0x54c4a4); assert(a[0] == Holder);
        return owned;
    } else if constexpr (std::is_same_v<R, const float*>) {
        assert(rva == 0x8ee66c || rva == 0x8ef5bc);
        assert(a[0] == at<u64>(Array,0));
        return rva == 0x8ee66c ? position : rotation;
    } else if constexpr (std::is_same_v<R, const char*>) {
        assert(rva == 0x52f83c && a[0] == Holder);
        return reinterpret_cast<const char*>(at<u64>(Scene,0xe0));
    } else {
        assert(rva == 0x2e35e0 && a[0] == Starts); ++lookups;
        return Starts;
    }
}
#define SMO_OUTFIT_HOST_TEST
#include "smo_outfit_guest.cpp"
using namespace SmoOutfit;

Mailbox setup(const char* stage = "SandWorldHomeStage") {
    bytes.fill(0); mutations.clear(); waiting = alive = owned = true;
    lookups = start_constructors = 0;
    position[0] = 333.5f; position[1] = 125.f; position[2] = -8540.25f;
    rotation[0] = 0; rotation[1] = 0.6f; rotation[2] = 0; rotation[3] = 0.8f;
    at<u64>(Scene,0) = Base + 0x1d8bb80;
    at<u64>(Scene,0x2d0) = Holder; at<u64>(Holder,0x20) = Save;
    at<u64>(Scene,0xe0) = reinterpret_cast<u64>(stage);
    at<u64>(Save,0x718) = reinterpret_cast<u64>("Mario");
    at<u64>(Save,0x770) = reinterpret_cast<u64>("MarioCaptain");
    at<u64>(Scene,0x90) = Info; at<u64>(Info,0x98) = Players;
    at<u64>(Players,0) = Array; at<int>(Players,8) = 4; at<int>(Players,12) = 1;
    at<u64>(Array,0) = Player; at<u64>(Player,0) = Base + 0x1d779e8;
    at<u64>(Save,0x860) = Life; at<int>(Life,12) = 3;
    at<u64>(Save,0x7d8) = Scenarios; at<int>(Save,0x9f0) = 2; at<int>(Scenarios,8) = 5;
    Mailbox m{}; m.magic = Magic; m.version = Version; m.size = sizeof(m);
    m.state = Pending; m.ticket = 1; m.scene = Scene; m.holder = Holder;
    m.save = Save; m.player = Player;
    std::strcpy(m.name,"MarioTailCoat"); std::strcpy(m.expected_outfit,"Mario");
    std::strcpy(m.expected_hat,"MarioCaptain");
    return m;
}
u64 run(Mailbox& m) { return smo_outfit_guest(Base,reinterpret_cast<u64>(&m),Scene,0,nullptr); }
u64 spawn(Mailbox& m, const char* id = ReloadStartId) {
    return smo_outfit_guest(Base,reinterpret_cast<u64>(&m),Scene,Starts,id);
}
void reject(Mailbox& m, Result expected) {
    assert(run(m) == 0 && m.state == Completed && m.result == expected && mutations.empty());
}
void nativeInitPlayer(Mailbox& m) {
    assert(spawn(m) == reinterpret_cast<u64>(m.start_info));
    assert(m.spawn_count == 1 && m.state == SpawnProvided && start_constructors == 1);
    assert(*reinterpret_cast<u64*>(m.start_info) == reinterpret_cast<u64>(m.placement));
    std::memcpy(position,m.start_info + 0xb0,12);
    std::memcpy(rotation,m.start_info + 0xbc,16);
    // The game can allocate a different player. Do not use m.player after kill.
    at<u64>(Array,0) = Player + 0x100; at<u64>(Player + 0x100,0) = Base + 0x1d779e8;
    at<uint8_t>(Holder,0x49) = 0;
}
int main() {
    for (const char* stage : {"HomeShipInsideStage","SandWorldHomeStage","ForestWorldHomeStage","SandWorldShopStage","TestSubStage"}) {
        for (unsigned kind : {0U,1U}) {
            auto m = setup(stage); m.kind = kind;
            assert(run(m) == 1 && m.state == Reloading && Busy(m.state));
            assert((mutations == std::vector<u64>{kind ? 0x52bb90U : 0x52bb5cU,
                kind ? 0x52b530U : 0x52b4ecU,0x52f6f4,0x4cc73c}));
            assert(run(m) == 0 && m.state == Reloading && mutations.size() == 4);
            nativeInitPlayer(m);
            assert(spawn(m) == Starts && start_constructors == 1); // Exactly one pose handoff.
            assert(run(m) == 0 && m.state == Completed && m.result == Success);
            assert(position[0] == 333.5f && position[2] == -8540.25f && rotation[1] == .6f);
            assert(run(m) == 0 && mutations.size() == 4 && at<int>(Life,12) == 3);
        }
    }
    auto m = setup(); assert(spawn(m,"Closet") == Starts && lookups == 1 && mutations.empty());
    m = setup(); owned = false; reject(m,NotOwned);
    m = setup(); waiting = false; reject(m,PlayerBusy);
    m = setup(); alive = false; reject(m,PlayerBusy);
    m = setup(); at<int>(Life,12) = 0; reject(m,PlayerBusy);
    for (unsigned flag : {0x458U,0xa2cU}) { m = setup(); at<uint8_t>(Save,flag) = 1; reject(m,PlayerBusy); }
    m = setup(); at<uint8_t>(Holder,0x49) = 1; reject(m,PlayerBusy);
    m = setup(); ++m.scene; reject(m,StaleIdentity);
    m = setup(); m.save = Save + 8; reject(m,StaleIdentity);
    m = setup(); m.player = Player + 8; reject(m,StaleIdentity);
    m = setup(); at<u64>(Player,0) = Base; reject(m,StaleIdentity);
    m = setup(); at<u64>(Save,0x718) = reinterpret_cast<u64>("MarioPoncho"); reject(m,StaleIdentity);
    m = setup(); at<int>(Players,12) = 2; reject(m,StaleIdentity);
    m = setup(); std::strcpy(m.name,"Mario"); reject(m,AlreadyEquipped);
    m = setup(); std::memset(m.name,'A',64); reject(m,InvalidRequest);
    m = setup(); std::strcpy(m.name,"../invalid"); reject(m,InvalidRequest);
    m = setup(); m.kind = 2; reject(m,InvalidRequest);
    m = setup(); position[0] = std::numeric_limits<float>::quiet_NaN(); reject(m,InvalidPose);
    m = setup(); rotation[3] = 0; reject(m,InvalidPose);
    m = setup(); run(m); nativeInitPlayer(m); position[0] += 100; run(m); assert(m.result == RestoreMismatch);
    m = setup(); run(m); nativeInitPlayer(m); rotation[1] = -.6f; run(m); assert(m.result == RestoreMismatch);
    m = setup(); run(m); at<int>(Scenarios,8) = 6; assert(spawn(m) == Starts && start_constructors == 0);
    m = setup(); run(m); at<u64>(Holder,0x20) = Save + 8; assert(spawn(m) == Starts && start_constructors == 0);
    m = setup(); run(m); at<u64>(Scene,0xe0) = reinterpret_cast<u64>("OtherStage"); assert(spawn(m) == Starts && start_constructors == 0);
    for (auto state : {Idle,Writing,Processing,Completed,Cancelled}) {
        m = setup(); m.state = state; assert(run(m) == 0 && mutations.empty());
    }
    m = setup(); m.magic = 0; assert(run(m) == 0 && mutations.empty());
    assert(!Busy(Idle) && !Busy(Completed) && !Busy(Cancelled) && Busy(SpawnProvided));
    std::cout << "SMO anywhere wardrobe contracts passed (native runtime verification still required)\n";
}
