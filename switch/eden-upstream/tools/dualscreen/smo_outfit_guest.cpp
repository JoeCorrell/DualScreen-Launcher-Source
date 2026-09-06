// SPDX-License-Identifier: GPL-3.0-or-later
// Exact base 1.0.0: native wear setters + same-stage reload. A one-shot native
// PlayerStartInfo preserves the pose BEFORE initPlayer constructs Mario.
// No wardrobe/menu object, per-frame teleport, or manual save-field mutation.
#include "../../src/core/loader/smo_outfit_protocol.h"
using namespace SmoOutfit;
using u64 = uint64_t;
#ifndef SMO_OUTFIT_HOST_TEST
template<class T> static T& at(u64 object, unsigned offset) {
    return *reinterpret_cast<T*>(object + offset);
}
template<class R, class... Args> static R call(u64 address, Args... args) {
    return reinterpret_cast<R(*)(Args...)>(address)(args...);
}
#endif
static bool pointer(u64 p) { return p >= 0x10000 && p < (1ULL << 39) && !(p & 7); }
static bool finite(float f) { return __builtin_isfinite(f); }
static bool validPose(const float* position, const float* rotation) {
    for (unsigned i = 0; i < 3; ++i)
        if (!finite(position[i]) || position[i] < -10000000.f || position[i] > 10000000.f) return false;
    float norm = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (!finite(rotation[i])) return false;
        norm += rotation[i] * rotation[i];
    }
    return norm > .99f && norm < 1.01f;
}
static void finish(Mailbox& m, Result result) {
    m.result = result;
    __atomic_store_n(&m.state, Completed, __ATOMIC_RELEASE);
}
static bool sameSave(u64 base, const Mailbox& m, u64 scene) {
    return pointer(scene) && at<u64>(scene, 0) == base + 0x1d8bb80 &&
        pointer(m.holder) && at<u64>(scene, 0x2d0) == m.holder &&
        pointer(m.save) && at<u64>(m.holder, 0x20) == m.save;
}
static u64 singlePlayer(u64 base, u64 scene) {
    const u64 info = at<u64>(scene, 0x90);
    if (!pointer(info)) return 0;
    const u64 players = at<u64>(info, 0x98);
    if (!pointer(players) || at<int>(players, 12) != 1 || at<int>(players, 8) < 1 ||
        at<int>(players, 8) > 4 || !pointer(at<u64>(players, 0))) return 0;
    const u64 player = at<u64>(at<u64>(players, 0), 0);
    return pointer(player) && at<u64>(player, 0) == base + 0x1d779e8 ? player : 0;
}
static bool sameAppearance(const Mailbox& m, bool changed) {
    return Equal(reinterpret_cast<const char*>(at<u64>(m.save, 0x718)),
                 changed && m.kind == 0 ? m.name : m.expected_outfit) &&
           Equal(reinterpret_cast<const char*>(at<u64>(m.save, 0x770)),
                 changed && m.kind == 1 ? m.name : m.expected_hat);
}
static bool sameDestination(const Mailbox& m, u64 scene) {
    const u64 scenarios = at<u64>(m.save, 0x7d8);
    return Equal(reinterpret_cast<const char*>(at<u64>(scene, 0xe0)), m.stage) &&
        at<int>(m.save, 0x9f0) == m.world && m.world >= 0 && m.world < 20 &&
        pointer(scenarios) && at<int>(scenarios, 4 * m.world) == m.scenario;
}

extern "C" __attribute__((section(".text.entry"), used))
u64 smo_outfit_guest(u64 base, u64 mailbox, u64 scene, u64 starts, const char* start_id) {
    auto& m = *reinterpret_cast<Mailbox*>(mailbox);
    const bool valid = m.magic == Magic && m.version == Version && m.size == sizeof(m);
    // Only the BL at StageScene::init+0x1514 (0x4c9b30) is intercepted, not
    // general start-info searches. Unrelated game loads call the original.
    if (starts) {
        if (valid && __atomic_load_n(&m.state, __ATOMIC_ACQUIRE) == Reloading &&
            Equal(start_id, ReloadStartId) && sameSave(base, m, scene) &&
            sameDestination(m, scene) && sameAppearance(m, true) &&
            m.spawn_count == 0 && validPose(m.position, m.rotation)) {
            // StageScene copies PlacementInfo and then +0xb0/+0xbc pose into
            // PlayerInitInfo. ActorInitInfo still comes from the actual stage
            // PlayerStart placement, not a fabricated actor/socket reference.
            call<void>(base + 0x9a3450, m.placement);
            call<void>(base + 0x2e2c48, m.start_info, m.placement, u64{0},
                       ReloadStartId, u64{0}, m.position, m.rotation);
            ++m.spawn_count;
            __atomic_store_n(&m.state, SpawnProvided, __ATOMIC_RELEASE);
            return reinterpret_cast<u64>(m.start_info);
        }
        return call<u64>(base + 0x2e35e0, starts, start_id);
    }
    if (!valid) return 0;
    const auto state = __atomic_load_n(&m.state, __ATOMIC_ACQUIRE);
    if (state == Reloading) return 0; // No acknowledgement from the dying scene.
    if (state == SpawnProvided) {
        // Acknowledge only after the new scene reaches gameplay, not when a
        // SaveString changes. Never reuse the old player pointer.
        if (!sameSave(base, m, scene) || !sameDestination(m, scene)) {
            finish(m, StaleIdentity); return 0;
        }
        const u64 player = singlePlayer(base, scene);
        bool restored = state == SpawnProvided && m.spawn_count == 1 && player && sameAppearance(m, true);
        if (player) {
            const auto* pos = call<const float*>(base + 0x8ee66c, player);
            const auto* quat = call<const float*>(base + 0x8ef5bc, player);
            for (unsigned i = 0; i < 3; ++i) {
                m.observed_position[i] = pos[i];
                const float delta = pos[i] - m.position[i];
                restored &= finite(delta) && delta > -1.f && delta < 1.f;
            }
            float dot = 0;
            for (unsigned i = 0; i < 4; ++i) dot += quat[i] * m.rotation[i];
            restored &= validPose(pos, quat) && (dot > .99f || dot < -.99f);
        }
        finish(m, restored ? Success : RestoreMismatch);
        return 0;
    }
    uint32_t expected = Pending;
    if (!__atomic_compare_exchange_n(&m.state, &expected, Processing, false,
                                     __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return 0;
    if (m.kind > 1 || !Identifier(m.name) || !Identifier(m.expected_outfit) ||
        !Identifier(m.expected_hat)) { finish(m, InvalidRequest); return 0; }
    if (scene != m.scene || !sameSave(base, m, scene) || !sameAppearance(m, false)) {
        finish(m, StaleIdentity); return 0;
    }
    const u64 player = singlePlayer(base, scene);
    if (!player || player != m.player) { finish(m, StaleIdentity); return 0; }
    // Native Wait + isEnableDemo exclude captures, 2D, death, airborne and
    // incompatible actions. exePlay excludes menus/cutscenes. No location gate.
    if (!call<bool>(base + 0x959c58, player, base + 0x1d78918) ||
        !call<bool>(base + 0x421754, player) || at<uint8_t>(m.holder, 0x49) ||
        at<uint8_t>(m.save, 0x458) || at<uint8_t>(m.save, 0xa2c)) {
        finish(m, PlayerBusy); return 0;
    }
    const u64 life = at<u64>(m.save, 0x860);
    if (!pointer(life) || at<int>(life, 12) <= 0) { finish(m, PlayerBusy); return 0; }
    const char* stage = call<const char*>(base + 0x52f83c, m.holder);
    if (!Identifier(stage) || !Equal(stage, reinterpret_cast<const char*>(at<u64>(scene, 0xe0)))) {
        finish(m, WrongScene); return 0;
    }
    for (unsigned i = 0; i < 64; ++i) { m.stage[i] = stage[i]; if (!stage[i]) break; }
    m.world = at<int>(m.save, 0x9f0);
    const u64 scenarios = at<u64>(m.save, 0x7d8);
    if (m.world < 0 || m.world >= 20 || !pointer(scenarios)) {
        finish(m, StaleIdentity); return 0;
    }
    m.scenario = at<int>(scenarios, 4 * m.world);
    if (!call<bool>(base + (m.kind == 0 ? 0x54c498 : 0x54c4a4), m.holder, m.name)) {
        finish(m, NotOwned); return 0;
    }
    if (Equal(m.name, m.kind == 0 ? m.expected_outfit : m.expected_hat)) {
        finish(m, AlreadyEquipped); return 0;
    }
    const auto* pos = call<const float*>(base + 0x8ee66c, player);
    const auto* quat = call<const float*>(base + 0x8ef5bc, player);
    for (unsigned i = 0; i < 3; ++i) m.position[i] = pos[i];
    for (unsigned i = 0; i < 4; ++i) m.rotation[i] = quat[i];
    if (!validPose(m.position, m.rotation)) { finish(m, InvalidPose); return 0; }
    // Same ChangeStageInfo constructor/options as the closet, but an isolated
    // one-shot start ID. -1 preserves the current scenario (no story advance).
    alignas(8) uint8_t change[0x278];
    call<void>(base + 0x248cc0, change, m.holder, ReloadStartId, m.stage, false, -1, 0);
    call<void>(base + (m.kind == 0 ? 0x52bb5c : 0x52bb90), scene + 0x20);
    call<void>(base + (m.kind == 0 ? 0x52b4ec : 0x52b530), m.holder, m.name);
    m.spawn_count = 0;
    __atomic_store_n(&m.state, Reloading, __ATOMIC_RELEASE);
    call<void>(base + 0x52f6f4, m.holder, change, 0);
    call<void>(base + 0x4cc73c, scene);
    return 1; // Skip exePlay on the scene the game just killed.
}
