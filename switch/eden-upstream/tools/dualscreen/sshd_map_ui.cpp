// SPDX-License-Identifier: GPL-3.0-or-later
// Guest AArch64 code for SSHD 1.0.1, build 5441AF3C89F97E1D.
// Runs after m2d's native GUI projection/state setup, including during gameplay.
// All rendering is performed by the original game's layout and icon controllers.
using u8 = unsigned char;
using u16 = unsigned short;
using u32 = unsigned int;
using u64 = unsigned long long;
template<class T> static inline T& at(u64 object, u32 offset) {
    return *reinterpret_cast<T*>(object + offset);
}
template<class R, class... A> static inline R call(u64 address, A... args) {
    return reinterpret_cast<R(*)(A...)>(address)(args...);
}
template<class R, class... A> static inline R virt(u64 object, u32 slot, A... args) {
    return call<R>(at<u64>(at<u64>(object, 0), slot), object, args...);
}
struct UiState {
    u32 request, frames, status, guard;
    u64 owner, texture, stage;
    u32 last_request, idle_frames;
    u32 frame_tick, drawn_tick;
    u32 was_visible, reserved;
    u32 gesture_sequence;
    float gesture_zoom, gesture_pan_x, gesture_pan_y;
    u32 gesture_applied;
    float base_scroll_x, base_scroll_z, base_scale;
    u64 hint_panes[3];
};
static_assert(sizeof(UiState) == 0x78);

// Applying an animation uses the game's binding links and Animate machinery.
// This deliberately avoids the interactive map state machine and its input,
// pause, camera and screen-capture side effects.
static void finish_in_animation(u64 map) {
    const u64 group = map + 0x230 + 46 * 0x70; // map_00_inNoCam, G_inOut_00
    const u64 transform = at<u64>(group, 0x48);
    const u64 nodes = at<u64>(group, 0x40);
    const u64 controller = at<u64>(group, 0x10);
    if (!transform || !nodes || !controller) return;
    const u64 list = nodes + 0x20;
    for (u64 node = at<u64>(list, 0); node != list; node = at<u64>(node, 0)) {
        virt<void>(at<u64>(node, 0x10), 0x80, transform, 0U, 0U);
    }
    const float end = static_cast<float>(at<u16>(at<u64>(transform, 0x18), 8)) - 1.0f;
    at<float>(controller, 0xC) = end;
    at<float>(transform, 0x20) = end;
    const u64 root = at<u64>(map, 0x1A8);
    virt<void>(root, 0x30, 0U);
    for (u64 node = at<u64>(list, 0); node != list; node = at<u64>(node, 0)) {
        virt<void>(at<u64>(node, 0x10), 0x88, transform, 0U);
    }
}

extern "C" __attribute__((section(".text.entry"), used))
void sshd_map_ui(u64 base, u64 mailbox, u64 map, u32 draw_argument) {
    volatile auto& state = at<UiState>(mailbox, 0x90);
    if (state.guard || !state.request) return;
    // Only a primary-target GUI setup may host this pass. Map/blur/postprocess
    // targets also invoke the setup routine and must retain their ownership.
    if (at<u8>(base, 0x4CBDB6D) || state.frame_tick == state.drawn_tick) return;
    if (state.last_request != state.request) {
        state.last_request = state.request;
        state.idle_frames = 0;
    } else if (++state.idle_frames > 120) return;
    state.status = 1;
    const u64 global = at<u64>(base, 0x183C440);
    const u64 stage = at<u64>(base, 0x2BF9D90);
    const u64 player = at<u64>(base, 0x2BF9940);
    if (!global || global != map + 0x10D38 || !stage || !player ||
        !at<u64>(map, 0x1A8) || !at<u64>(stage, 0x550) ||
        at<float>(map, 0x1A80) <= 0.0f) return;
    state.guard = 1;
    state.drawn_tick = state.frame_tick;
    state.status = 2;
    const u8 visible = at<u8>(map, 0x10D27);
    const bool new_owner = state.owner != map || state.stage != stage;
    if (new_owner) {
        state.owner = map;
        state.stage = stage;
        state.frames = 0;
        // Real Map2D/map_00.brlyt branches: back/zoom/reset controls, the A
        // prompt beside the beacon count, and the rotate/name controller hint.
        // These are children of the priority groups, so native draw ordering
        // can still toggle its groups without restoring these hint branches.
        static constexpr char names[3][16] = {
            "N_backZoom_00", "N_input_03", "N_nameRotate_00"
        };
        for (u32 i = 0; i < 3; ++i) {
            state.hint_panes[i] = virt<u64>(at<u64>(map, 0x1A8), 0x70, names[i], 1U);
        }
    }
    // RenderingWait's follow-up at C48150 binds the stage image to P_map_00.
    // Reproduce its resource-only C48174..C48244 block; the following code
    // transitions into the interactive In state and must not run here.
    const u64 stage_texture = at<u64>(stage, 0x550);
    const u64 stage_key = at<u64>(stage_texture, 0x18);
    if (new_owner || at<u32>(map, 0x1A60) != static_cast<u32>(stage_key >> 32) ||
        at<u32>(map, 0x1A78) != static_cast<u32>(stage_key)) {
        const u32 dimensions = at<u32>(stage_texture, 8);
        const u8 format = at<u8>(stage_texture, 0xD);
        for (u32 i = 0; i < 8; ++i) at<u32>(map, 0x1A60 + i * 4) = 0;
        at<u32>(map, 0x1A60) = static_cast<u32>(stage_key >> 32);
        at<u32>(map, 0x1A64) = dimensions;
        at<u32>(map, 0x1A68) = format | (static_cast<u32>(at<u16>(stage_texture, 0xE)) << 8);
        at<u16>(map, 0x1A74) = at<u16>(stage_texture, 0x10);
        at<u32>(map, 0x1A78) = static_cast<u32>(stage_key);
        at<float>(map, 0x1A8C) = static_cast<float>(at<u16>(stage_texture, 8));
        at<float>(map, 0x1A90) = static_cast<float>(at<u16>(stage_texture, 0xA));
        const u64 map_picture = at<u64>(map, 0x1A58);
        at<u64>(map_picture, 0x78) = at<u64>(map, 0x1A8C);
        const u64 material = virt<u64>(map_picture, 0xC8);
        const u64 tex_map = at<u64>(material, 0x50);
        at<u64>(tex_map, 0) = stage_key;
        at<u32>(tex_map, 0x10) = dimensions;
        at<u32>(tex_map, 0x20) = (at<u32>(tex_map, 0x20) & ~15U) | (format & 15U);
    }
    if (new_owner || visible) {
        state.base_scroll_x = at<float>(global, 0);
        state.base_scroll_z = at<float>(global, 8);
        state.base_scale = at<float>(global, 0x44);
        state.gesture_applied = state.gesture_sequence;
    }
    if (!visible && (new_owner || state.was_visible)) finish_in_animation(map);
    state.was_visible = visible;
    state.status = 3;
    if (!state.texture) {
        state.texture = call<u64>(base + 0xEF4B8C,
            static_cast<int>(at<u32>(base, 0x16CAC94)),
            static_cast<int>(at<u32>(base, 0x16CAC98)), 6, 1U);
    }
    if (!state.texture) { state.guard = 0; return; }
    const u64 all_pane = at<u64>(map, 0x10798);
    const u64 picture = at<u64>(map, 0x1A58);
    const u8 all_flags = at<u8>(all_pane, 0xEB);
    const u8 picture_flags = at<u8>(picture, 0xEB);
    at<u8>(all_pane, 0xEB) |= 1;
    if (at<u32>(map, 0x10B10) - 3U < 2) at<u8>(picture, 0xEB) |= 1;
    if (!visible) {
        const u32 gesture = state.gesture_sequence;
        const float zoom = state.gesture_zoom;
        const float pan_x = state.gesture_pan_x;
        const float pan_y = state.gesture_pan_y;
        __asm__ volatile("dmb ish" ::: "memory");
        if (!(gesture & 1) && gesture != state.gesture_applied &&
            gesture == state.gesture_sequence && zoom >= 1.0f && zoom <= 4.0f) {
            const float scale = state.base_scale * zoom;
            const float unit = scale * at<float>(global, 0x40);
            const float aspect = at<float>(global, 0x28);
            if (unit > 0.0f && aspect > 0.0f) {
                // Exact inverse of dLytMapGlobal::projectOntoMap, using the
                // game's own interpolated trigonometric table and UI extent.
                const float angle = static_cast<float>(at<u16>(global, 0x3C)) / 256.0f;
                const u32 index = static_cast<u32>(angle) & 255;
                const float fraction = angle - static_cast<float>(index);
                const u64 trig = base + 0x13A34CC + index * 16;
                const float sine = at<float>(trig, 0) + fraction * at<float>(trig, 8);
                const float cosine = at<float>(trig, 4) + fraction * at<float>(trig, 12);
                const float dx = -pan_x * at<float>(map, 0x1B8) / (unit * aspect);
                const float dz = pan_y * at<float>(map, 0x1BC) / unit;
                at<float>(global, 0x44) = scale;
                at<float>(global, 0) = state.base_scroll_x + cosine * dx + sine * dz;
                at<float>(global, 8) = state.base_scroll_z + cosine * dz - sine * dx;
                // Queue the original stage-map renderer with the changed
                // transform. The next stage pass updates the resident image.
                at<u8>(map, 0x1A94) = 1;
                at<u8>(map, 0x1A95) = 0;
                call<void>(base + 0xC69C34, map + 0x19E0);
                state.gesture_applied = gesture;
            }
        }
        // Feed the same global position and marker rotation fields populated
        // by MapMain::updatePlayerPos; the native builder owns the projection.
        for (u32 i = 0; i < 3; ++i) {
            at<float>(global, 0x30 + i * 4) = at<float>(player, 0x144 + i * 4);
            at<u16>(map, 0x3790 + 0xA3C + i * 2) = at<u16>(player, 0x13C + i * 2);
        }
        at<u32>(global, 0x5C) = at<u32>(map, 0x10B10);
        at<u32>(global, 0x60) = at<u32>(map, 0x10B10);
        at<u32>(map, 0x10B14) = at<u32>(map, 0x10B10);
        call<void>(base + 0xC853A4, map + 0x3790);
        call<void>(base + 0xC5CAA0, map + 0x2790);
        call<void>(base + 0xC57418, map + 0x2790, at<float>(global, 0x64));
        at<u8>(map, 0x214) |= 1;
        at<float>(map, 0x1F8) = at<float>(map, 0x1B8) * -0.5f;
        at<float>(map, 0x1FC) = at<float>(map, 0x1BC) * 0.5f;
        at<float>(map, 0x200) = at<float>(map, 0x1B8) * 0.5f;
        at<float>(map, 0x204) = at<float>(map, 0x1BC) * -0.5f;
        virt<void>(at<u64>(map, 0x1A8), 0x18, map + 0x1C0);
    }
    state.status = 4;
    // Build the same GXTexObj used by the game's render-target adapter.
    // The key is an opaque native cache identity, never CPU pixel memory.
    u32 gx[8]{};
    const u64 key = at<u64>(state.texture, 0x18);
    gx[0] = static_cast<u32>(key >> 32);
    gx[1] = at<u32>(state.texture, 8);
    gx[2] = at<u8>(state.texture, 0xD) |
            (static_cast<u32>(at<u16>(state.texture, 0xE)) << 8);
    gx[5] = at<u16>(state.texture, 0x10);
    gx[6] = static_cast<u32>(key);
    call<void>(base + 0x10448E0, 0U, gx);
    // Native ClearColor, as used by the stage renderer at main+E241AC.
    // This target is retained across frames. Its translucent parchment and
    // moving icons must start from transparent black instead of accumulating.
    // The game's main GUI pass disables alpha writes (main+13300). An Android
    // transparent surface needs the map's actual alpha, not inherited zero alpha
    // with nonzero RGB, which appears additive/washed out when composited.
    const u8 rgb_write = at<u8>(base, 0x4CC03A9);
    const u8 alpha_write = at<u8>(base, 0x4CC03AA);
    at<u8>(base, 0x4CC03A9) = 1;
    at<u8>(base, 0x4CC03AA) = 1;
    call<void>(at<u64>(base, 0x2C8ED70), base + 0x4CC0924, 0U, 1U, 1U, 1U, 1U);
    call<void>(at<u64>(base, 0x2C8EFD0), at<u64>(base, 0x4CC0960), base + 0x4CC0924);
    const float clear[4]{};
    call<void>(at<u64>(base, 0x2C8F100), at<u64>(base, 0x4CC0960), 0U, clear, 15U);
    state.status = 5;
    const u8 alpha = at<u8>(global, 0x4E);
    at<u8>(global, 0x4E) = 255;
    at<u8>(map, 0x10D27) = 1;
    u8 hint_flags[3]{};
    for (u32 i = 0; i < 3; ++i) {
        if (const u64 pane = state.hint_panes[i]) {
            hint_flags[i] = at<u8>(pane, 0xEB);
            at<u8>(pane, 0xEB) &= ~1U;
        }
    }
    call<void>(base + 0xC642F4, map, draw_argument);
    for (u32 i = 0; i < 3; ++i) {
        if (const u64 pane = state.hint_panes[i]) at<u8>(pane, 0xEB) = hint_flags[i];
    }
    at<u8>(map, 0x10D27) = visible;
    at<u8>(global, 0x4E) = alpha;
    at<u8>(all_pane, 0xEB) = all_flags;
    at<u8>(picture, 0xEB) = picture_flags;
    at<u8>(base, 0x4CC03A9) = rgb_write;
    at<u8>(base, 0x4CC03AA) = alpha_write;
    call<void>(at<u64>(base, 0x2C8ED70), base + 0x4CC0924, 0U,
               static_cast<u32>(rgb_write), static_cast<u32>(rgb_write),
               static_cast<u32>(rgb_write), static_cast<u32>(alpha_write));
    call<void>(at<u64>(base, 0x2C8EFD0), at<u64>(base, 0x4CC0960), base + 0x4CC0924);
    state.status = 6;
    call<void>(base + 0x10448E0, 0U, static_cast<u32*>(nullptr));
    ++state.frames;
    state.status = 7;
    state.guard = 0;
}
