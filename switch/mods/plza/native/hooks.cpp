#include "host_shim.h"
#include "core/loader/plza_item_code.h"
#include "core/loader/plza_item_protocol.h"
#include <vector>
namespace Loader {
constexpr u32 PlzaTextLocation = 0x00000000;
constexpr u32 PlzaTextFileSize = 0x03163F70;
constexpr u32 PlzaRoDataLocation = 0x03164000;
constexpr u32 PlzaRoDataSize = 0x00A5E000;
constexpr u32 PlzaDataLocation = 0x03BC2000;
constexpr u32 PlzaDataSize = 0x003BC430;
constexpr u32 PlzaBssSize = 0x021FCBD0;
constexpr u32 PlzaFieldPreUpdateHookOffset = 0x0031232C;
constexpr u32 PlzaFieldPreUpdateHookInstruction = 0xD10183FF;
constexpr u32 PlzaBagNativeDispatchOffset = 0x00C150E4;
constexpr u32 PlzaCompanionMailboxPageSize = 0x1000;
constexpr u32 PlzaCompanionBridgePageSize = 0x4000;
constexpr u32 EncodeCompareBranchZero(bool is_64_bit, u32 target_register,
                                      size_t instruction_index, size_t target_index) {
    const s64 delta = static_cast<s64>(target_index) - static_cast<s64>(instruction_index);
    return (is_64_bit ? 0xB4000000U : 0x34000000U) |
           ((static_cast<u32>(delta) & 0x7FFFFU) << 5) | (target_register & 0x1FU);
}
constexpr u32 EncodeConditionalBranch(u32 condition, size_t instruction_index,
                                      size_t target_index) {
    const s64 delta = static_cast<s64>(target_index) - static_cast<s64>(instruction_index);
    return 0x54000000U | ((static_cast<u32>(delta) & 0x7FFFFU) << 5) |
           (condition & 0xFU);
}
constexpr u32 EncodeLocalBranch(size_t instruction_index, size_t target_index) {
    const s64 delta = static_cast<s64>(target_index) - static_cast<s64>(instruction_index);
    return 0x14000000U | (static_cast<u32>(delta) & 0x03FFFFFFU);
}
constexpr u32 EncodeCompareImmediate32(u32 source_register, u32 immediate) {
    return 0x7100001FU | ((immediate & 0xFFFU) << 10) |
           ((source_register & 0x1FU) << 5);
}
constexpr u32 EncodeRelativeBranch(u32 instruction_offset, u32 target_offset) {
    const s64 delta = static_cast<s64>(target_offset) - static_cast<s64>(instruction_offset);
    return 0x14000000U | (static_cast<u32>(delta / 4) & 0x03FFFFFFU);
}
constexpr u32 EncodeRelativeCall(u32 instruction_offset, u32 target_offset) {
    const s64 delta = static_cast<s64>(target_offset) - static_cast<s64>(instruction_offset);
    return 0x94000000U | (static_cast<u32>(delta / 4) & 0x03FFFFFFU);
}
constexpr u32 EncodeAdr(u32 target_register, u32 instruction_offset, u32 target_offset) {
    const s64 delta = static_cast<s64>(target_offset) - static_cast<s64>(instruction_offset);
    const u32 immediate = static_cast<u32>(delta) & 0x1FFFFFU;
    return 0x10000000U | ((immediate & 3U) << 29) |
           (((immediate >> 2) & 0x7FFFFU) << 5) | (target_register & 0x1FU);
}
constexpr u32 EncodeTestBitBranch(bool nonzero, u32 target_register, u32 bit,
                                  size_t instruction_index, size_t target_index) {
    const s64 delta = static_cast<s64>(target_index) - static_cast<s64>(instruction_index);
    return (nonzero ? 0x37000000U : 0x36000000U) | ((bit & 0x20U) << 26) |
           ((bit & 0x1FU) << 19) | ((static_cast<u32>(delta) & 0x3FFFU) << 5) |
           (target_register & 0x1FU);
}
constexpr u32 EncodeAdrp(u32 target_register, u32 instruction_offset, u32 target_offset) {
    const s64 instruction_page = static_cast<s64>(instruction_offset & ~0xFFFU);
    const s64 target_page = static_cast<s64>(target_offset & ~0xFFFU);
    const u64 pages =
        static_cast<u64>((target_page - instruction_page) / 0x1000) & 0x1FFFFFU;
    return 0x90000000U | (static_cast<u32>(pages & 3U) << 29) |
           (static_cast<u32>((pages >> 2) & 0x7FFFFU) << 5) | (target_register & 0x1FU);
}
constexpr u32 EncodeAddPageOffset(u32 target_register, u32 target_offset) {
    return 0x91000000U | ((target_offset & 0xFFFU) << 10) |
           ((target_register & 0x1FU) << 5) | (target_register & 0x1FU);
}
constexpr u32 EncodeLoadUnsigned64(u32 target_register, u32 base_register,
                                   u32 byte_offset) {
    return 0xF9400000U | (((byte_offset / sizeof(u64)) & 0xFFFU) << 10) |
           ((base_register & 0x1FU) << 5) | (target_register & 0x1FU);
}
constexpr u32 EncodeStoreUnsigned64(u32 target_register, u32 base_register,
                                    u32 byte_offset) {
    return 0xF9000000U | (((byte_offset / sizeof(u64)) & 0xFFFU) << 10) |
           ((base_register & 0x1FU) << 5) | (target_register & 0x1FU);
}
constexpr u32 EncodeAddImmediate64(u32 target_register, u32 base_register,
                                   u32 byte_offset) {
    return 0x91000000U | ((byte_offset & 0xFFFU) << 10) |
           ((base_register & 0x1FU) << 5) | (target_register & 0x1FU);
}
constexpr u32 EncodeAddImmediate32(u32 target_register, u32 base_register,
                                   u32 immediate) {
    return 0x11000000U | ((immediate & 0xFFFU) << 10) |
           ((base_register & 0x1FU) << 5) | (target_register & 0x1FU);
}
constexpr u32 EncodeStoreUnsigned8(u32 target_register, u32 base_register,
                                   u32 byte_offset) {
    return 0x39000000U | ((byte_offset & 0xFFFU) << 10) |
           ((base_register & 0x1FU) << 5) | (target_register & 0x1FU);
}
constexpr u32 EncodeLoadUnsigned8(u32 target_register, u32 base_register,
                                  u32 byte_offset) {
    return 0x39400000U | ((byte_offset & 0xFFFU) << 10) |
           ((base_register & 0x1FU) << 5) | (target_register & 0x1FU);
}
constexpr u32 EncodeStoreUnsigned32(u32 target_register, u32 base_register,
                                    u32 byte_offset) {
    return 0xB9000000U | (((byte_offset / sizeof(u32)) & 0xFFFU) << 10) |
           ((base_register & 0x1FU) << 5) | (target_register & 0x1FU);
}
constexpr u32 EncodeLoadUnsigned32(u32 target_register, u32 base_register,
                                   u32 byte_offset) {
    return 0xB9400000U | (((byte_offset / sizeof(u32)) & 0xFFFU) << 10) |
           ((base_register & 0x1FU) << 5) | (target_register & 0x1FU);
}
constexpr u32 EncodeLoadUnsigned16(u32 target_register, u32 base_register,
                                   u32 byte_offset) {
    return 0x79400000U | (((byte_offset / 2U) & 0xFFFU) << 10) |
           ((base_register & 0x1FU) << 5) | (target_register & 0x1FU);
}
constexpr u32 EncodeStoreUnsigned16(u32 target_register, u32 base_register,
                                    u32 byte_offset) {
    return 0x79000000U | (((byte_offset / sizeof(u16)) & 0xFFFU) << 10) |
           ((base_register & 0x1FU) << 5) | (target_register & 0x1FU);
}
constexpr u32 EncodeLoadFloat32(u32 target_register, u32 base_register,
                                u32 byte_offset) {
    return 0xBD400000U | (((byte_offset / sizeof(u32)) & 0xFFFU) << 10) |
           ((base_register & 0x1FU) << 5) | (target_register & 0x1FU);
}
constexpr u32 EncodeStoreFloat32(u32 target_register, u32 base_register,
                                 u32 byte_offset) {
    return 0xBD000000U | (((byte_offset / sizeof(u32)) & 0xFFFU) << 10) |
           ((base_register & 0x1FU) << 5) | (target_register & 0x1FU);
}
constexpr u32 EncodeSubtractRegisters64(u32 target_register, u32 left_register,
                                        u32 right_register) {
    return 0xCB000000U | ((right_register & 0x1FU) << 16) |
           ((left_register & 0x1FU) << 5) | (target_register & 0x1FU);
}
constexpr u32 EncodeMultiplyAdd64(u32 target_register, u32 left_register,
                                  u32 right_register, u32 add_register) {
    return 0x9B000000U | ((right_register & 0x1FU) << 16) |
           ((add_register & 0x1FU) << 10) | ((left_register & 0x1FU) << 5) |
           (target_register & 0x1FU);
}
u32 ReadCodeWord(const std::vector<u8>& image, size_t offset) {
    u32 word{};
    if (offset + sizeof(word) <= image.size()) {
        std::memcpy(&word, image.data() + offset, sizeof(word));
    }
    return word;
}
bool InstallPlzaCompanionItemHook(std::vector<u8>& image, size_t module_start,
                                  u32 mailbox_main_offset, u32 bridge_main_offset,
                                  u32 bridge_size) {
    // The old GOT[3EC6D48]+130 path is a command string, not a presenter.
    // Never execute that writer. A separate field-selector hook processes ball
    // requests using the live model owned by the current native invocation.
    struct Site {
        u32 offset;
        u32 mailbox;
        std::array<u32, 4> signature;
        u32 code_offset{};
    };
    std::array<Site, 5> sites{{
        {PlzaBagNativeDispatchOffset, 0x80, {0xD10203FF, 0xA9067BFD, 0xA9074FF4, 0x910183FD}},
        {0xA5A070, 0x100, {0xA9BD7BFD, 0xF9000BF5, 0xA9024FF4, 0x910003FD}},
        {0xA5C988, 0x180, {0xD10143FF, 0xA9017BFD, 0xF90013F7, 0xA90357F6}},
        {0xA5C51C, 0x200, {0xD101C3FF, 0xA9047BFD, 0xA90557F6, 0xA9064FF4}},
        {0x2BB90C, 0x280, {0xD104C3FF, 0xA90F7BFD, 0xF90083FC, 0xA91157F6}},
    }};
    if ((mailbox_main_offset & 0xFFFU) || (bridge_main_offset & 0xFFFU) ||
        bridge_size != PlzaCompanionBridgePageSize ||
        bridge_main_offset != mailbox_main_offset + PlzaCompanionMailboxPageSize) return false;
    const size_t cave = module_start + bridge_main_offset;
    const size_t field = module_start + PlzaFieldPreUpdateHookOffset;
    if (cave > image.size() || bridge_size > image.size() - cave ||
        field + 4 > image.size() ||
        ReadCodeWord(image, field) != PlzaFieldPreUpdateHookInstruction) return false;
    for (const auto& site : sites) {
        const size_t at = module_start + site.offset;
        if (at > image.size() || 16 > image.size() - at) return false;
        for (u32 i = 0; i < 4; ++i) {
            if (ReadCodeWord(image, at + i * 4) != site.signature[i]) {
                LOG_ERROR(Loader, "[PLZA-ITEM-TRACE] signature mismatch main+{:#x}", site.offset);
                return false;
            }
        }
    }
    for (const auto& signature : PlzaItemPayload::Signatures) {
        const size_t at = module_start + signature.rva;
        if (at > image.size() || 16 > image.size() - at) return false;
        for (u32 i = 0; i < 4; ++i)
            if (ReadCodeWord(image, at + i * 4) != signature.words[i]) return false;
    }
    std::vector<u32> code{PlzaFieldPreUpdateHookInstruction,
        EncodeRelativeBranch(bridge_main_offset + 4, PlzaFieldPreUpdateHookOffset + 4)};
    const auto emit = [&](u32 instruction) { code.push_back(instruction); return code.size() - 1; };
    const auto address = [&](u32 reg, u32 target) {
        emit(EncodeAdrp(reg, bridge_main_offset + static_cast<u32>(code.size() * 4), target));
        emit(EncodeAddPageOffset(reg, target));
    };
    constexpr u32 BallHook = 0x2BB00C;
    const u32 ball_stub = static_cast<u32>(code.size() * 4);
    // Preserve the complete machine context, including SIMD and flags. Native
    // helpers run synchronously on this game's thread; no host-thread calls.
    emit(0xD10C43FF); // sub sp,sp,#0x310
    for (u32 reg = 0; reg < 31; ++reg) emit(EncodeStoreUnsigned64(reg, 31, reg * 8));
    emit(0xD53B4209); // mrs x9,nzcv
    emit(EncodeStoreUnsigned64(9, 31, 0xF8));
    for (u32 reg = 0; reg < 32; ++reg)
        emit(0x3D800000 | (((0x100 + reg * 16) / 16) << 10) | (31 << 5) | reg);
    emit(0xD53B4409); // mrs x9,fpcr
    emit(EncodeStoreUnsigned64(9, 31, 0x300));
    emit(0xD53B4429); // mrs x9,fpsr
    emit(EncodeStoreUnsigned64(9, 31, 0x308));
    emit(0xAA0003E2); // mov x2,x0 (current presenter)
    address(0, 0);
    address(1, mailbox_main_offset);
    emit(EncodeRelativeCall(bridge_main_offset + static_cast<u32>(code.size() * 4),
                            bridge_main_offset + PlzaItemPayload::CodeOffset));
    emit(EncodeLoadUnsigned64(9, 31, 0x300));
    emit(0xD51B4409);
    emit(EncodeLoadUnsigned64(9, 31, 0x308));
    emit(0xD51B4429);
    for (u32 reg = 0; reg < 32; ++reg)
        emit(0x3DC00000 | (((0x100 + reg * 16) / 16) << 10) | (31 << 5) | reg);
    emit(EncodeLoadUnsigned64(9, 31, 0xF8));
    emit(0xD51B4209);
    for (u32 reg = 0; reg < 31; ++reg) emit(EncodeLoadUnsigned64(reg, 31, reg * 8));
    emit(0x910C43FF); // add sp,sp,#0x310
    emit(0xD100C3FF); // displaced native sub sp,sp,#0x30
    emit(EncodeRelativeBranch(bridge_main_offset + static_cast<u32>(code.size() * 4), BallHook + 4));
    for (auto& site : sites) {
        if (site.offset == PlzaBagNativeDispatchOffset) code.resize(0x800 / 4, 0xD503201F);
        site.code_offset = static_cast<u32>(code.size() * 4);
        emit(0xD10083FF); // sub sp,sp,#0x20
        emit(0xA90027E8); // stp x8,x9,[sp]
        emit(0xA9012FEA); // stp x10,x11,[sp,#0x10]
        address(8, mailbox_main_offset);
        emit(EncodeAddImmediate64(9, 8, site.mailbox));
        emit(0x885FFD2A); // ldaxr w10,[x9]
        const auto locked = emit(0);
        emit(0x5280002A);
        emit(0x880BFD2A); // stlxr w11,w10,[x9] -- ONE attempt, never spin
        const auto contended = emit(0);
        if (site.offset == PlzaBagNativeDispatchOffset) {
            emit(EncodeStoreUnsigned64(0, 8, 0x90));
            emit(EncodeStoreUnsigned64(30, 8, 0x98));
            emit(EncodeLoadUnsigned64(10, 8, 0));
            emit(EncodeStoreUnsigned64(10, 8, 0xA0));
            emit(EncodeLoadUnsigned32(10, 0, 0x140));
            emit(EncodeStoreUnsigned32(10, 8, 0xA8));
            for (u32 i = 0; i < 5; ++i) {
                emit(EncodeLoadUnsigned64(10, 0, 0x108 + i * 8));
                emit(EncodeStoreUnsigned64(10, 8, 0xB0 + i * 8));
            }
            emit(EncodeLoadUnsigned64(10, 0, 0x68));
            emit(EncodeStoreUnsigned64(10, 8, 0xD8));
        } else {
            const u32 data = site.mailbox + 0x10;
            for (u32 i = 0; i < 3; ++i) emit(EncodeStoreUnsigned64(i, 8, data + i * 8));
            emit(EncodeStoreUnsigned64(30, 8, data + 0x18));
            emit(EncodeLoadUnsigned64(10, 8, 0));
            emit(EncodeStoreUnsigned64(10, 8, data + 0x20));
            // A5C988 receives valid retained references to adapter and Pokemon.
            // A5C51C receives a valid retained actor reference in x2. We observe
            // them ONLY during the native call, never cache them as action targets.
            if (site.offset == 0xA5C988) {
                for (u32 i = 0; i < 2; ++i) {
                    emit(EncodeLoadUnsigned64(10, i, 0));
                    emit(EncodeStoreUnsigned64(10, 8, data + 0x28 + i * 8));
                    emit(EncodeLoadUnsigned64(10, 10, 0));
                    emit(EncodeStoreUnsigned64(10, 8, data + 0x38 + i * 8));
                }
            } else if (site.offset == 0xA5C51C) {
                emit(EncodeLoadUnsigned64(10, 2, 0));
                emit(EncodeStoreUnsigned64(10, 8, data + 0x28));
                // The native wrapper itself accepts a null actor reference.
                const auto no_actor = emit(0);
                emit(EncodeLoadUnsigned64(10, 10, 0));
                code[no_actor] = EncodeCompareBranchZero(true, 10, no_actor, code.size());
                emit(EncodeStoreUnsigned64(10, 8, data + 0x38));
            }
        }
        emit(EncodeLoadUnsigned64(10, 8, site.mailbox + 8));
        emit(EncodeAddImmediate64(10, 10, 1));
        emit(EncodeAddImmediate64(11, 8, site.mailbox + 8));
        emit(0xC89FFD6A); // stlr x10,[x11]
        emit(0x889FFD3F); // stlr wzr,[x9]
        const auto restore_jump = emit(0);
        const auto skip = code.size();
        emit(0xD5033F5F); // clrex
        const auto restore = code.size();
        emit(0xA9412FEA);
        emit(0xA94027E8);
        emit(0x910083FF);
        emit(site.signature[0]); // original first instruction, exactly once
        emit(EncodeRelativeBranch(bridge_main_offset + static_cast<u32>(code.size() * 4),
                                  site.offset + 4));
        code[locked] = EncodeCompareBranchZero(false, 10, locked, skip) | 0x01000000;
        code[contended] = EncodeCompareBranchZero(false, 11, contended, skip) | 0x01000000;
        code[restore_jump] = EncodeLocalBranch(restore_jump, restore);
    }
    if (code.size() * 4 > PlzaItemPayload::CodeOffset) return false;
    code.resize(PlzaItemPayload::CodeOffset / 4, 0xD503201F);
    code.insert(code.end(), PlzaItemPayload::Code.begin(), PlzaItemPayload::Code.end());
    const auto size = code.size() * 4;
    if (size > bridge_size ||
        std::any_of(image.begin() + static_cast<ptrdiff_t>(cave),
                    image.begin() + static_cast<ptrdiff_t>(cave + size),
                    [](u8 byte) { return byte != 0; })) return false;
    const auto branch_ok = [](u32 from, u32 to) {
        const s64 delta = static_cast<s64>(to) - from;
        return !(delta & 3) && delta >= -(1LL << 27) && delta < (1LL << 27);
    };
    if (!branch_ok(PlzaFieldPreUpdateHookOffset, bridge_main_offset)) return false;
    if (!branch_ok(BallHook, bridge_main_offset + ball_stub)) return false;
    for (const auto& site : sites)
        if (!branch_ok(site.offset, bridge_main_offset + site.code_offset)) return false;
    std::memcpy(image.data() + cave, code.data(), size);
    for (const auto& site : sites) {
        const u32 branch = EncodeRelativeBranch(site.offset, bridge_main_offset + site.code_offset);
        std::memcpy(image.data() + module_start + site.offset, &branch, 4);
    }
    const u32 branch = EncodeRelativeBranch(PlzaFieldPreUpdateHookOffset, bridge_main_offset);
    std::memcpy(image.data() + field, &branch, 4);
    const u32 ball_branch = EncodeRelativeBranch(BallHook, bridge_main_offset + ball_stub);
    std::memcpy(image.data() + module_start + BallHook, &ball_branch, 4);
    PlzaItem::Mailbox mailbox{};
    mailbox.magic = PlzaItem::Magic;
    mailbox.version = PlzaItem::Version;
    std::memcpy(image.data() + module_start + mailbox_main_offset, &mailbox, sizeof(mailbox));
    LOG_INFO(Loader, "[PLZA-ITEM-TRACE] installed {} bytes: dispatch, bag, medicine, target, ball; "
                    "native persistent field-ball actions enabled", size);
    return true;
}
}
