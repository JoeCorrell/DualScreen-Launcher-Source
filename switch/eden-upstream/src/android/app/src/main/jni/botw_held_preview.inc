// Included inside BotwCompanion. Layout is restricted to BotwBuildId, verified against
// ModelNW::getBoneName (F97DAC), getBoneParent (F96238), copyBoneWorldMatrix (F961C4),
// and live 121-bone Player / two-ingredient CarryBox captures. Never writes guest memory.
std::string GetHeldPreview(EmulationSession& session) {
    [[maybe_unused]] auto access = session.LockSystemAccess();
    constexpr auto unavailable = R"({"status":"unavailable"})";
    constexpr auto empty = R"({"status":"ok","items":[]})";
    if (!session.IsRunning()) return unavailable;
    auto& system = session.System();
    if (system.GetApplicationProcessProgramID() != BotwTitleId || !IsExpectedBuild(system))
        return unavailable;
    const auto [base, size] = system.GetApplicationProcessMainModule();
    if (!base || size <= 0x1E13ED0) return unavailable;
    auto& memory = system.ApplicationMemory();
    auto ptr = [&](VAddr address) { return Read<u64>(memory, address).value_or(0); };
    auto link = [&](VAddr address) -> VAddr {
        const auto data = ptr(address);
        const auto id = Read<u32>(memory, address + 8);
        if (!data || !id || *id == 0xffffffff || Read<u32>(memory, data + 0x40) != id)
            return 0;
        return ptr(data + 0x48);
    };
    const auto info = ptr(base + 0x1E13ED0);
    const auto player = info ? ptr(info + 0x60) : 0;
    if (!player || Read<u8>(memory, player + 0x64) != 1) return unavailable;
    const auto player_id = Read<u32>(memory, player + 0x60);
    const auto holding = Read<u8>(memory, player + 0xD11);
    if (!holding) return unavailable;
    if (!*holding) return empty;
    const auto manager = ptr(base + 0x1E0C828);
    if (!manager) return unavailable;
    const auto carry = link(manager + 0x300);
    const auto data = ptr(manager + 0x310);
    if (!carry || !data || link(data + 0x708) != carry ||
        ptr(player + 0x88) != carry || ptr(carry + 0x80) != player ||
        Read<u8>(memory, carry + 0x92C) != 1 || Read<u32>(memory, carry + 0x970) != 2)
        return unavailable;
    const auto count = Read<s32>(memory, data + 0x638);
    if (!count || *count < 1 || *count > 5) return unavailable;
    const auto slots = ptr(data + 0x640);
    const auto model = ptr(player + 0x4E0);
    if (!slots || !model || ptr(model) != base + 0x1D0FA48 ||
        Read<u16>(memory, model + 0xA4) != 1) return unavailable;
    const auto units = ptr(model + 0x30);
    const auto unit = units ? ptr(units) : 0;
    if (!unit || ptr(unit) != base + 0x1D10738) return unavailable;
    const auto skeleton = ptr(unit + 0x118);
    const auto resource = skeleton ? ptr(skeleton) : 0;
    const auto bone_count = resource ? Read<u16>(memory, resource + 0x3C) : std::nullopt;
    if (!bone_count || *bone_count < 1 || *bone_count > 256) return unavailable;
    const auto bones = ptr(resource + 0x18);
    const auto worlds = ptr(skeleton + 0x20);
    if (!bones || !worlds) return unavailable;
    using Matrix = std::array<float, 16>;
    const auto read_matrix = [&](VAddr address, bool row_major) -> std::optional<Matrix> {
        Matrix result{};
        if (row_major) {
            const auto raw = Read<std::array<float, 12>>(memory, address);
            if (!raw) return std::nullopt;
            for (size_t r = 0; r < 3; ++r)
                for (size_t c = 0; c < 4; ++c) result[c * 4 + r] = (*raw)[r * 4 + c];
        } else {
            const auto raw = Read<Matrix>(memory, address);
            if (!raw) return std::nullopt;
            result = *raw;
        }
        result[3] = result[7] = result[11] = 0;
        result[15] = 1;
        for (float value : result) if (!std::isfinite(value)) return std::nullopt;
        return result;
    };
    const auto root = read_matrix(model + 0x58, true);
    if (!root) return unavailable;
    std::vector<Matrix> matrices(*bone_count), verify(*bone_count);
    const auto byte_count = matrices.size() * sizeof(Matrix);
    if (!memory.IsValidVirtualAddressRange(worlds, byte_count) ||
        !memory.ReadBlockUnsafe(worlds, matrices.data(), byte_count)) return unavailable;
    std::ostringstream out;
    out << std::setprecision(9);
    const auto matrix_json = [&](Matrix value) {
        value[3] = value[7] = value[11] = 0;
        value[15] = 1;
        out << '[';
        for (size_t i = 0; i < value.size(); ++i) {
            if (i) out << ',';
            out << value[i];
        }
        out << ']';
    };
    out << R"({"status":"ok","player":)" << player_id.value_or(0) << R"(,"root":)";
    matrix_json(*root);
    out << R"(,"bones":[)";
    bool found_hand = false;
    for (u16 i = 0; i < *bone_count; ++i) {
        const auto name_pointer = ptr(bones + i * 0x50);
        const auto name = name_pointer ? ReadCString(memory, name_pointer + 2) : std::nullopt;
        const auto parent = Read<u16>(memory, bones + i * 0x50 + 0x1A);
        if (!name || !parent || (*parent != 0xffff && *parent >= i)) return unavailable;
        for (float value : matrices[i]) if (!std::isfinite(value)) return unavailable;
        found_hand |= *name == "Weapon_R";
        if (i) out << ',';
        out << '[' << JsonString("Armature_" + *name) << ','
            << (*parent == 0xffff ? -1 : static_cast<int>(*parent)) << ',';
        matrix_json(matrices[i]);
        out << ']';
    }
    if (!found_hand) return unavailable;
    out << R"(],"items":[)";
    std::array<VAddr, 5> actors{};
    std::array<u32, 5> ids{};
    std::array<Matrix, 5> item_matrices{};
    std::array<std::array<float, 6>, 5> item_scales{};
    for (s32 i = 0; i < *count; ++i) {
        const auto slot = ptr(slots + i * 8);
        const auto actor = slot ? link(slot + 0x30) : 0;
        if (!actor || ptr(slot + 0x118) != data) return unavailable;
        const auto id = Read<u32>(memory, actor + 0x60);
        const auto name_pointer = ptr(actor + 0x10);
        const auto name = name_pointer ? ReadCString(memory, name_pointer) : std::nullopt;
        const auto item_model = ptr(actor + 0x4E0);
        const auto transform = item_model ? read_matrix(item_model + 0x58, true) : std::nullopt;
        const auto scale = item_model ? Read<std::array<float, 6>>(memory, item_model + 0x88) : std::nullopt;
        if (!id || !name || !transform || !scale ||
            !std::all_of(scale->begin(), scale->end(), [](float v) {
                return std::isfinite(v) && v > 0 && v <= 100;
            }) ||
            !(name->starts_with("Item_") || name->starts_with("Animal_Insect_"))) return unavailable;
        actors[i] = actor;
        ids[i] = *id;
        item_matrices[i] = *transform;
        item_scales[i] = *scale;
        // Native gsys::Model F7849C..F784D0 sends the rigid +58 matrix and
        // component-wise (+88 * +94) scale separately to ModelUnit::setMtx.
        // Include that same scale once in the exported basis, not its translation.
        // CarryBox shrinks actors while holding; the rigid matrix alone loses it.
        auto rendered = *transform;
        for (size_t column = 0; column < 3; ++column)
            for (size_t row = 0; row < 3; ++row)
                rendered[column * 4 + row] *= (*scale)[column] * (*scale)[column + 3];
        if (i) out << ',';
        out << R"({"id":)" << *id << R"(,"actor":)" << JsonString(*name) << R"(,"world":)";
        matrix_json(rendered);
        out << '}';
    }
    // Sampling does not pause gameplay. Reject a concurrent pose/collection update rather
    // than publish mixed animation frames or dereference cached actors on a later frame.
    if (!memory.ReadBlockUnsafe(worlds, verify.data(), byte_count) || matrices != verify ||
        read_matrix(model + 0x58, true) != root || ptr(info + 0x60) != player ||
        Read<u32>(memory, player + 0x60) != player_id || ptr(manager + 0x310) != data ||
        Read<s32>(memory, data + 0x638) != count || Read<u8>(memory, player + 0xD11) != 1)
        return unavailable;
    for (s32 i = 0; i < *count; ++i) {
        const auto slot = ptr(slots + i * 8);
        const auto item_model = ptr(actors[i] + 0x4E0);
        if (!slot || link(slot + 0x30) != actors[i] ||
            Read<u32>(memory, actors[i] + 0x60) != ids[i] || !item_model ||
            Read<std::array<float, 6>>(memory, item_model + 0x88) !=
                std::optional<std::array<float, 6>>{item_scales[i]} ||
            read_matrix(item_model + 0x58, true) != std::optional<Matrix>{item_matrices[i]})
            return unavailable;
    }
    out << "]}";
    return out.str();
}
