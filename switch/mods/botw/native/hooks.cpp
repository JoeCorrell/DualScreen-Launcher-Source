#include "host_shim.h"
#include <vector>
#include <limits>
#include <span>
namespace Loader {
constexpr u64 BotwCompanionTitleId = 0x01007EF00011E000ULL;

constexpr bool EnableBotwCompanion = true;
constexpr std::array<u8, 0x20> BotwCompanionBuildId{
    0xCD, 0x57, 0xB2, 0x3F, 0xA4, 0xBB, 0xAD, 0x65,
    0x80, 0x3D, 0x97, 0x88, 0xC0, 0x18, 0x21, 0xEE,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

constexpr u32 BotwTextLocation = 0x00000000;
constexpr u32 BotwTextFileSize = 0x015A1C64;
constexpr u32 BotwRoDataLocation = 0x015A2000;
constexpr u32 BotwRoDataSize = 0x005D7330;
constexpr u32 BotwDataLocation = 0x01B7A000;
constexpr u32 BotwDataSize = 0x002381D8;
constexpr u32 BotwBssSize = 0x000E5E28;

constexpr u32 BotwEquipHookOffset = 0x00B24570;
constexpr u32 BotwEquipHookInstruction = 0x39422008;



constexpr u32 BotwCompanionBridgePageSize = 0x1000;
constexpr u32 BotwEquipmentCompletionAttemptLimit = 240;
constexpr u32 BotwActionDiagnosticNativeResultOffset = 0xF4;
constexpr u32 BotwActionDiagnosticGuestQuantityAfterOffset = 0x14C;
constexpr u32 BotwActionDiagnosticGuestInInventoryAfterOffset = 0x150;
constexpr u32 BotwPouchSingletonGotOffset = 0x01D67AF0;
constexpr u32 BotwContextVtableGotOffset = 0x01D66260;




constexpr u32 BotwActionContextNameOffset = 0x015CCFC9;
constexpr std::string_view BotwActionContextName = "MainShortCut_00Screen";
constexpr u32 BotwPouchLockOffset = 0x00EAFF08;
constexpr u32 BotwPouchUnlockOffset = 0x00EAFF18;
constexpr u32 BotwQuickEquipOffset = 0x00D9A4E0;
constexpr u32 BotwAutoEquipOffset = 0x00D93168;
constexpr u32 BotwUnequipOffset = 0x00D9A590;
constexpr u32 BotwGetDefaultEquipmentOffset = 0x00D940C4;
constexpr u32 BotwRequestArmorOffset = 0x00B252C4;
constexpr u32 BotwHoldItemOffset = 0x00D9F0E8;
constexpr u32 BotwCanGrabAnotherItemOffset = 0x00D9A63C;
constexpr u32 BotwEatGameplayEntryOffset = 0x00E3EDE4;
constexpr u32 BotwEatGameplayExitHookOffset = 0x00E3F0B8;
constexpr u32 BotwEatGameplayExitHookInstruction = 0xF95B1660;
constexpr u32 BotwEatGameplayEpilogueOffset = 0x00E3ED98;
// Player::updateCookEffects copies the queued values written by the pouch consume path into
// the live effect timers/modifiers.  The vanilla pause flow reaches this when gameplay resumes;
// a companion consume has no pause transition, so it must run explicitly.
constexpr u32 BotwUpdateCookEffectsOffset = 0x00CF05F0;
constexpr u32 BotwEatNotifyOffset = 0x00D4279C;
constexpr u32 BotwEatNotifyStorageOffset = 0x01E15FB8;
constexpr u32 BotwEatScreenRegistryOffset = 0x01E374B8;
constexpr u32 BotwActorMessageSendOffset = 0x0154C61C;
constexpr u32 BotwCapturePlayerAccessorOffset = 0x00CC12F0;
constexpr u32 BotwGetActorMessageIdOffset = 0x01070E58;
constexpr u32 BotwReleaseCapturedActorOffset = 0x01126464;
// CarryBox::calc_'s Normal -> Held predicate. The custom pouch path can finish Player's
// deferred attachment without a pending grab message on CarryBox. Test that native
// attachment here, on CarryBox's own update, just as its Grab child AI does at main+94A1B0.
constexpr u32 BotwCarryGrabReadyHookOffset = 0x00806D20;
constexpr u32 BotwCarryGrabReadyHookInstruction = 0x3941A268; // ldrb w8, [x19, #0x68]
constexpr u32 BotwHoldMessageTypeOffset = 0x016E6B50;
constexpr u32 BotwHoldCallerCapturePointerOffset = 0x00DA47C4;
constexpr u32 BotwHoldCallerSenderOffset = 0x00DA47D0;
constexpr u32 BotwHoldControllerGotLoadOffset = 0x00E47B10;
constexpr u32 BotwHoldControllerObjectLoadOffset = 0x00E47B14;
// The persistent controller transceiver sends 08000003 to Player (pouch upper/face animation).
// This is NOT CarryBox adoption: PMDM's native spawn path sends 08000016 / 01800004 separately.
constexpr u32 BotwHoldControllerMessageSenderOffset = 0x150;
constexpr u32 BotwHoldControllerMessageDispatcherOffset =
    BotwHoldControllerMessageSenderOffset + 0x08;
constexpr u32 BotwHoldControllerMailboxOffset = 0xC8;
constexpr u32 BotwEatTargetReadOffset = 0x00E3EE40;
constexpr u32 BotwHoldCaptureScratchOffset = 0x160;
constexpr u32 BotwHoldMessageScratchOffset = 0x180;



constexpr u32 BotwRequestEquippedItemOffset = 0x00E88FB0;
constexpr u32 BotwEquipmentActorsReadyOffset = 0x00B249D4;
constexpr u32 BotwPlayerInfoStorageOffset = 0x01E13ED0;
constexpr u32 BotwGetPlayerOffset = 0x00CC6FD0;
constexpr u32 BotwSwitchEquipmentCalcOffset = 0x00766E68;
constexpr u32 BotwSwitchEquipmentApplyOffset = 0x00766F14;
constexpr u32 BotwPlayerSwitchEquipmentOffset = 0x00CBE4D0;



constexpr u32 BotwRuneManagerStaticOffset = 0x01E0CD40;
constexpr u32 BotwRuneSetCurrentOffset = 0x00B33D6C;



constexpr u32 BotwFastTravelEventOffset = 0x00E81A30;
constexpr u32 BotwCompanionMailboxPageSize = 0x4000;






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




std::vector<u32> BuildBotwCarryGrabReadyHook(u32 stub_offset) {
    std::vector<u32> code;
    const auto emit = [&](u32 instruction) {
        const size_t index = code.size();
        code.push_back(instruction);
        return index;
    };
    std::vector<size_t> reject_zero;
    std::vector<size_t> reject_nonzero;
    emit(BotwCarryGrabReadyHookInstruction);
    const auto native_pending = emit(0);
    emit(0xA9BF2BE9U); // stp x9, x10, [sp, #-16]!
    emit(EncodeAdrp(9, stub_offset + static_cast<u32>(code.size() * 4),
                    BotwPlayerInfoStorageOffset));
    emit(EncodeLoadUnsigned64(9, 9, BotwPlayerInfoStorageOffset & 0xFFFU));
    reject_zero.push_back(emit(0)); // no PlayerInfo
    emit(EncodeLoadUnsigned64(9, 9, 0x60));
    reject_zero.push_back(emit(0)); // no live Player
    emit(EncodeLoadUnsigned64(10, 21, 0x80)); // CarryBox calc parent
    emit(0xCA09014AU); // eor x10, x10, x9 (preserve NZCV)
    reject_nonzero.push_back(emit(0));
    emit(EncodeLoadUnsigned64(10, 9, 0x88)); // reciprocal Player calc child
    emit(0xCA15014AU); // eor x10, x10, x21
    reject_nonzero.push_back(emit(0));
    emit(EncodeLoadUnsigned8(10, 9, 0xD11)); // Player carrying pouch
    const auto not_carrying = emit(0);
    emit(EncodeLoadUnsigned8(10, 21, 0x92C)); // CarryBox grab helper +BC: attached
    const auto not_attached = emit(0);
    emit(EncodeLoadUnsigned32(10, 21, 0x970)); // helper +100: attachment state
    emit(0x521F014AU); // eor w10, w10, #2 (preserve NZCV)
    const auto not_held = emit(0);
    emit(0x52800028U); // mov w8, #1: allow the ORIGINAL native Held transition
    const auto restore = code.size();
    emit(0xA8C12BE9U); // ldp x9, x10, [sp], #16
    const auto return_index = code.size();
    emit(EncodeRelativeBranch(stub_offset + static_cast<u32>(return_index * 4),
                              BotwCarryGrabReadyHookOffset + 4));
    code[native_pending] =
        EncodeCompareBranchZero(false, 8, native_pending, return_index) | 0x01000000U;
    for (const size_t index : reject_zero) {
        code[index] = EncodeCompareBranchZero(true, 9, index, restore);
    }
    for (const size_t index : reject_nonzero) {
        code[index] = EncodeCompareBranchZero(true, 10, index, restore) | 0x01000000U;
    }
    code[not_carrying] = EncodeCompareBranchZero(false, 10, not_carrying, restore);
    code[not_attached] = EncodeCompareBranchZero(false, 10, not_attached, restore);
    code[not_held] = EncodeCompareBranchZero(false, 10, not_held, restore) | 0x01000000U;
    return code;
}

bool InstallBotwCompanionEquipHook(std::vector<u8>& image, size_t module_start,
                                   u32 mailbox_main_offset, u32 bridge_main_offset,
                                   u32 bridge_size) {
    std::vector<u32> code;
    code.reserve(bridge_size / sizeof(u32));
    const auto emit = [&](u32 instruction) {
        const size_t index = code.size();
        code.push_back(instruction);
        return index;
    };
    const auto emit_address = [&](u32 target_register, u32 target_offset) {
        const auto instruction_index = code.size();
        const auto instruction_offset =
            bridge_main_offset + static_cast<u32>(instruction_index * sizeof(u32));
        emit(EncodeAdrp(target_register, instruction_offset, target_offset));
        emit(EncodeAddPageOffset(target_register, target_offset));
    };
    const auto emit_page_address = [&](u32 target_register, u32 target_offset) {
        const auto instruction_index = code.size();
        const auto instruction_offset =
            bridge_main_offset + static_cast<u32>(instruction_index * sizeof(u32));
        emit(EncodeAdrp(target_register, instruction_offset, target_offset));
    };
    const auto emit_load_address = [&](u32 target_register, u32 target_offset) {
        const auto instruction_index = code.size();
        const auto instruction_offset =
            bridge_main_offset + static_cast<u32>(instruction_index * sizeof(u32));
        emit(EncodeAdrp(target_register, instruction_offset, target_offset));
        emit(EncodeLoadUnsigned64(target_register, target_register, target_offset & 0xFFFU));
    };
    const auto emit_stage = [&](u32 stage) {
        ASSERT_MSG(stage <= 0xFFFFU, "BOTW companion stage must fit MOVZ immediate");
        emit_page_address(9, mailbox_main_offset);
        emit(0x52800008U | (stage << 5));
        emit(0xB9002528U);
    };


    const auto emit_call = [&](u32 target_offset) {
        const auto instruction_offset =
            bridge_main_offset + static_cast<u32>(code.size() * sizeof(u32));
        const s64 delta =
            static_cast<s64>(target_offset) - static_cast<s64>(instruction_offset);
        ASSERT_MSG((delta & 3) == 0 && delta >= -(1LL << 27) && delta < (1LL << 27),
                   "BOTW companion direct-call target is outside AArch64 BL range");
        emit(EncodeRelativeCall(instruction_offset, target_offset));
    };

    std::vector<size_t> unlock_calls;
    const auto emit_unlock = [&] {
        unlock_calls.push_back(emit(0));
    };

    emit_page_address(9, mailbox_main_offset);
    emit(0x9100412AU);
    const auto claim = code.size();
    emit(0x885FFD4BU);
    emit(0x7100097FU);
    const auto branch_running = emit(0);
    emit(0x7100057FU);
    const auto branch_not_queued = emit(0);

    emit(0x528000ACU);
    emit(0x880DFD4CU);
    const auto branch_claim_retry = emit(0);
    emit(0xB940152CU);
    emit_stage(0x01);
    emit(0xF940012BU);
    const auto branch_no_target = emit(0);
    emit(0xA9BB7BE0U);
    emit(0xF90023EBU);
    emit(0xB9004BECU);
    emit(0x3100059FU);
    const auto branch_rune = emit(0);
    emit(0x3100099FU);
    const auto branch_fast_travel = emit(0);
    emit_load_address(12, BotwPouchSingletonGotOffset);
    const auto branch_null_got = emit(0);
    emit(0xF940018CU);
    const auto branch_null_manager = emit(0);
    emit_stage(0x02);
    emit(0xF90013ECU);
    emit(0x9100A180U);
    emit_call(BotwPouchLockOffset);
    emit(0xF94013ECU);
    emit(0xF94023EBU);
    emit(0x9101A18EU);
    emit(0xB940798FU);
    emit(0x710691FFU);
    const auto branch_bad_count_high = emit(0);
    emit(0xF940398DU);

    const auto loop = code.size();
    emit(0xEB0E01BFU);
    const auto branch_end_of_list = emit(0);
    emit(0xD10021B0U);
    emit(0xEB0B021FU);
    const auto branch_found = emit(0);
    emit(0xF9400A0DU);
    emit(0x710005EFU);
    const auto branch_continue_loop = emit(0);

    const auto unlock_reject = code.size();
    emit_unlock();
    const auto branch_unlocked_reject = emit(0);

    const auto found = code.size();
    emit(0x3940956DU);
    const auto branch_not_usable = emit(0);
    emit(0xB940196DU);
    emit_stage(0x03);

    emit(0xB9404BF1U);
    emit(0x3100123FU);
    const auto branch_eat = emit(0);
    emit(0x31000E3FU);
    const auto branch_hold = emit(0);
    emit(0x3100163FU);
    const auto branch_unequip = emit(0);
    emit(0x6B0D023FU);
    const auto branch_wrong_equip_type = emit(0);
    emit(0x71000DBFU);
    emit_load_address(8, BotwContextVtableGotOffset);
    emit(0x91004108U);
    emit_address(9, BotwActionContextNameOffset);
    emit(0xA90127E8U);
    const auto branch_weapon_type = emit(0);
    emit(0x710019BFU);
    const auto branch_bad_armor_type = emit(0);

    emit_stage(0x10);
    emit(0xAA0C03E0U);
    emit(0xAA0B03E1U);
    emit(0xAA0E03E2U);
    emit_call(BotwAutoEquipOffset);
    emit_unlock();
    emit(0xF94003E0U);
    emit(0xF94023EBU);
    emit(0x9100A161U);
    emit(0xB9402162U);
    emit(0x910043E3U);
    emit_call(BotwRequestArmorOffset);
    emit_stage(0x11);
    const auto branch_armor_staged = emit(0);

    const auto weapon = code.size();
    emit_stage(0x12);
    emit_unlock();
    emit(0xF94013E0U);
    emit(0xF94023E1U);
    emit_call(BotwQuickEquipOffset);
    emit(0xF94023E0U);
    emit(0x910043E1U);
    emit_call(BotwRequestEquippedItemOffset);
    const auto branch_weapon_reject = emit(0);
    emit_stage(0x13);
    const auto branch_weapon_staged = emit(0);

    const auto hold = code.size();
    emit(0x71001DBFU);
    const auto branch_hold_wrong_type = emit(0);
    emit(0xB9402168U);
    emit(0x7100051FU);
    const auto branch_hold_empty = emit(0);
    emit_stage(0x30);
    emit_unlock();
    emit_page_address(14, mailbox_main_offset);
    emit(EncodeLoadUnsigned64(0, 14, BotwHoldControllerMailboxOffset));
    const auto branch_hold_no_controller = emit(0);
    emit(EncodeLoadUnsigned64(8, 0, BotwHoldControllerMessageSenderOffset));
    const auto branch_hold_message_sender_unavailable = emit(0);
    emit(EncodeLoadUnsigned64(8, 0, BotwHoldControllerMessageDispatcherOffset));
    const auto branch_hold_message_dispatcher_unavailable = emit(0);
    emit(0xF94013E0U);
    emit_call(BotwCanGrabAnotherItemOffset);
    const auto branch_hold_capacity_race = emit(0);
    emit(0xF94013E0U);
    emit(0xF94023E1U);
    emit_call(BotwHoldItemOffset);
    emit_page_address(14, mailbox_main_offset);
    emit(0xF94023EBU);
    emit(0xB9402168U);
    emit(EncodeStoreUnsigned32(8, 14, BotwActionDiagnosticGuestQuantityAfterOffset));
    emit(0x39409569U);
    emit(EncodeStoreUnsigned32(9, 14, BotwActionDiagnosticGuestInInventoryAfterOffset));
    emit(EncodeLoadUnsigned32(9, 14, 0xE0));
    emit(0x51000529U);
    emit(0x6B09011FU);
    const auto branch_hold_quantity_mismatch = emit(0);
    emit(EncodeAddImmediate64(0, 14, BotwHoldCaptureScratchOffset));
    emit(EncodeStoreUnsigned8(31, 0, 0));
    emit(EncodeStoreUnsigned64(31, 0, 8));
    emit(EncodeStoreUnsigned8(31, 0, 0x10));
    emit_call(BotwCapturePlayerAccessorOffset);
    emit_page_address(14, mailbox_main_offset);
    emit(EncodeAddImmediate64(5, 14, BotwHoldCaptureScratchOffset));
    emit(EncodeLoadUnsigned64(8, 5, 8));
    const auto branch_hold_no_captured_actor = emit(0);
    emit(0xAA0503E0U);
    emit_call(BotwGetActorMessageIdOffset);
    emit(0xAA0003E1U);
    emit_page_address(14, mailbox_main_offset);
    emit(EncodeLoadUnsigned64(0, 14, BotwHoldControllerMailboxOffset));
    const auto branch_hold_no_sender_after_mutation = emit(0);
    emit(EncodeAddImmediate64(0, 0, BotwHoldControllerMessageSenderOffset));
    emit_page_address(14, mailbox_main_offset);
    emit(EncodeAddImmediate64(2, 14, BotwHoldMessageScratchOffset));
    emit(0x52800068U);
    emit(0x72A10008U);
    emit(EncodeStoreUnsigned32(8, 2, 0));
    emit(0xAA1F03E3U);
    emit(0x52800024U);
    emit_call(BotwActorMessageSendOffset);
    emit_page_address(0, mailbox_main_offset);
    emit(EncodeAddImmediate64(0, 0, BotwHoldCaptureScratchOffset));
    emit_call(BotwReleaseCapturedActorOffset);
    emit_page_address(14, mailbox_main_offset);
    emit(0x52800028U);
    emit(EncodeStoreUnsigned32(8, 14, BotwActionDiagnosticNativeResultOffset));
    emit_stage(0x31);
    const auto branch_hold_success = emit(0);

    const auto hold_no_captured_actor = code.size();
    emit_stage(0x32);
    emit_page_address(0, mailbox_main_offset);
    emit(EncodeAddImmediate64(0, 0, BotwHoldCaptureScratchOffset));
    emit_call(BotwReleaseCapturedActorOffset);
    emit_page_address(14, mailbox_main_offset);
    emit(0x52800028U);
    emit(EncodeStoreUnsigned32(8, 14, BotwActionDiagnosticNativeResultOffset));
    const auto branch_hold_no_captured_actor_success = emit(0);

    const auto hold_native_rejected = code.size();
    emit_stage(0x33);
    const auto branch_hold_native_rejected_publish = emit(0);

    const auto eat = code.size();
    emit(EncodeCompareImmediate32(13, 8));
    const auto branch_eat_food = emit(0);
    emit(EncodeCompareImmediate32(13, 7));
    const auto branch_eat_wrong_type = emit(0);
    emit(EncodeLoadUnsigned32(8, 11, 0x1C));
    emit(EncodeCompareImmediate32(8, 10));
    const auto branch_eat_wrong_use = emit(0);
    const auto eat_type_allowed = code.size();
    emit(0xB9402168U);
    emit(0x7100051FU);
    const auto branch_eat_empty = emit(0);
    emit_stage(0x40);
    emit_unlock();
    emit_page_address(14, mailbox_main_offset);
    emit(0xF94023EBU);
    emit(0xF91B25CBU);
    emit(0xF94013ECU);
    emit(0xF91B15CCU);
    emit_load_address(0, BotwPlayerInfoStorageOffset);
    const auto branch_eat_no_player_info = emit(0);
    emit_call(BotwGetPlayerOffset);
    const auto branch_eat_no_player = emit(0);
    emit_load_address(8, BotwEatScreenRegistryOffset);
    const auto branch_eat_no_registry = emit(0);
    emit(0xF9401908U);
    const auto branch_eat_no_registry_array = emit(0);
    emit(0xD10203FFU);
    const auto eat_return_adr = emit(0);
    emit(0xA9027BFDU);
    emit(0xA9036FFCU);
    emit(0xA90467FAU);
    emit(0xA9055FF8U);
    emit(0xA90657F6U);
    emit(0xA9074FF4U);
    emit(0x910083FDU);
    emit(0xB94019D4U);
    emit(0xB94021D6U);
    emit(0xAA0E03F3U);
    emit(0x2A1F03FAU);
    emit(0x1280001CU);
    const auto eat_gameplay_jump = code.size();
    emit(
        EncodeRelativeBranch(bridge_main_offset + static_cast<u32>(eat_gameplay_jump * sizeof(u32)),
                             BotwEatGameplayEntryOffset));

    const auto eat_return = code.size();
    emit_load_address(0, BotwPlayerInfoStorageOffset);
    const auto branch_eat_refresh_no_player_info = emit(0);
    emit_call(BotwGetPlayerOffset);
    const auto branch_eat_refresh_no_player = emit(0);
    emit(0x2A1F03E1U);
    emit_call(BotwUpdateCookEffectsOffset);
    const auto eat_effect_refresh_done = code.size();
    emit(0xF94023EBU);
    emit_page_address(14, mailbox_main_offset);
    emit(0xB9402168U);
    emit(EncodeStoreUnsigned32(8, 14, BotwActionDiagnosticGuestQuantityAfterOffset));
    emit(0x39409568U);
    emit(EncodeStoreUnsigned32(8, 14, BotwActionDiagnosticGuestInInventoryAfterOffset));
    emit(0x52800028U);
    emit(EncodeStoreUnsigned32(8, 14, BotwActionDiagnosticNativeResultOffset));
    emit(0x9100A16AU);
    emit(0xF9400148U);
    emit(0xAA0A03E0U);
    emit(0xF9400D08U);
    emit(0xD63F0100U);
    emit(0xF94023EBU);
    emit(0xF9401961U);
    emit_load_address(0, BotwEatNotifyStorageOffset);
    const auto branch_eat_no_notifier = emit(0);
    emit_call(BotwEatNotifyOffset);
    const auto eat_notify_done = code.size();
    emit_stage(0x41);
    const auto branch_eat_success = emit(0);

    const auto unequip = code.size();
    emit(0x39409168U);
    const auto branch_unequip_not_equipped = emit(0);
    emit(0x710019BFU);
    const auto branch_unequip_bad_high = emit(0);
    emit(0x710009BFU);
    const auto branch_unequip_arrow = emit(0);
    emit_stage(0x50);
    emit_unlock();
    emit_load_address(0, BotwPlayerInfoStorageOffset);
    const auto branch_unequip_no_player_info = emit(0);
    emit_call(BotwGetPlayerOffset);
    const auto branch_unequip_no_player = emit(0);
    emit(0xF9001BE0U);
    emit(0xF94013E0U);
    emit(0xF94023E1U);
    emit_call(BotwUnequipOffset);
    emit_stage(0x51);
    emit(0xF94013E0U);
    emit(0xF94023EBU);
    emit(0xB9401961U);
    emit_call(BotwGetDefaultEquipmentOffset);
    emit(0xAA0003E1U);
    emit(0xF9401BE0U);
    emit(0x528003C2U);
    emit(0x12800003U);
    emit(0xF9001FFFU);
    emit(0x9100E3E4U);
    emit_call(BotwPlayerSwitchEquipmentOffset);
    emit_stage(0x52);
    emit(0xF94023EBU);
    emit(0x39409168U);
    const auto branch_unequip_still_equipped = emit(0);
    emit_stage(0x53);
    const auto branch_unequip_success = emit(0);



    const auto fast_travel = code.size();
    emit_stage(0x60);
    emit(0x91010120U);
    emit(0x91020121U);
    emit(0xAA1F03E2U);
    emit_call(BotwFastTravelEventOffset);
    emit_stage(0x61);
    const auto branch_fast_travel_success = emit(0);




    const auto rune = code.size();
    emit_stage(0x70);
    emit(0x51000561U);
    emit(0x7100143FU);
    const auto branch_rune_bad = emit(0);
    emit_load_address(0, BotwRuneManagerStaticOffset);
    const auto branch_rune_null = emit(0);
    emit_call(BotwRuneSetCurrentOffset);
    emit_stage(0x71);





    const auto success_stack = code.size();
    emit(0xA8C57BE0U);
    emit(0x5280006AU);
    const auto branch_success_publish = emit(0);

    const auto reject_stack = code.size();
    emit(0xA8C57BE0U);
    const auto reject_early = code.size();
    emit(0x5280008AU);
    const auto publish_address = code.size();
    emit_page_address(9, mailbox_main_offset);
    const auto publish_common = code.size();
    emit(0xF940012BU);
    emit(0xA9002D3FU);
    emit(0xF9000D3FU);
    emit(0x91004129U);
    emit(0x889FFD2AU);

    const auto passthrough = code.size();
    emit(BotwEquipHookInstruction);
    const auto return_index = code.size();
    emit(EncodeRelativeBranch(
        bridge_main_offset + static_cast<u32>(return_index * sizeof(u32)),
        BotwEquipHookOffset + sizeof(u32)));

    const auto eat_exit_stub = code.size();
    const auto branch_eat_custom_exit = emit(0);
    emit(BotwEatGameplayExitHookInstruction);
    const auto eat_exit_vanilla_return = code.size();
    emit(EncodeRelativeBranch(
        bridge_main_offset + static_cast<u32>(eat_exit_vanilla_return * sizeof(u32)),
        BotwEatGameplayExitHookOffset + sizeof(u32)));
    const auto eat_custom_exit = code.size();
    emit(EncodeRelativeBranch(
        bridge_main_offset + static_cast<u32>(eat_custom_exit * sizeof(u32)),
        BotwEatGameplayEpilogueOffset));

    const auto staged = code.size();
    emit(0xA8C57BE0U);
    const auto branch_staged_release = emit(0);

    const auto running_claim = code.size();
    emit(0x528000CCU);
    emit(0x880DFD4CU);
    const auto branch_running_claim_failed = emit(0);

    const auto running = code.size();
    emit(0x52800408U);
    emit(0xB9002528U);
    emit(0xF940012BU);
    const auto branch_running_no_request = emit(0);
    emit(0xB9401D2AU);
    emit(0x1100054AU);
    emit(0xB9001D2AU);
    emit(EncodeCompareImmediate32(10, BotwEquipmentCompletionAttemptLimit));
    const auto branch_running_timeout = emit(0);
    emit(0xB940152CU);
    emit(0x7100199FU);
    const auto branch_running_bad_type = emit(0);
    emit(0x39409168U);
    const auto branch_running_target_not_equipped = emit(0);
    emit(0x5282040DU);
    emit(0x72A0A86DU);
    emit(0x531E758EU);
    emit(0x1ACE25ADU);
    emit(0x12000DADU);
    emit(0x52800D0EU);
    emit(0x9BAE01AEU);
    emit(0x9103A1CEU);
    emit(0xB94001C8U);
    const auto branch_running_idle = emit(0);
    emit(0x7100051FU);
    const auto branch_running_loading = emit(0);
    emit(0x7100091FU);
    const auto branch_running_bad_status = emit(0);
    emit(0x52800448U);
    emit(0xB9002528U);
    emit(0xD100C3FFU);
    emit(0xA9017BE0U);
    emit_call(BotwEquipmentActorsReadyOffset);
    const auto branch_running_wait_actors = emit(0);
    emit_page_address(9, mailbox_main_offset);
    emit(0x52800468U);
    emit(0xB9002528U);
    emit_load_address(0, BotwPlayerInfoStorageOffset);
    const auto branch_running_wait_player_info = emit(0);
    emit_call(BotwGetPlayerOffset);
    const auto branch_running_wait_player = emit(0);
    emit_page_address(9, mailbox_main_offset);
    emit(0x52800488U);
    emit(0xB9002528U);
    emit(0xF90003E0U);
    emit(0x91316000U);
    emit_call(BotwPouchLockOffset);
    emit(0xF94003EBU);
    emit(0xB94C9968U);
    emit(0x321B0108U);
    emit(0xB90C9968U);
    emit(0x91316160U);
    emit_call(BotwPouchUnlockOffset);
    emit_page_address(9, mailbox_main_offset);
    emit(0x528004A8U);
    emit(0xB9002528U);

    const auto running_success = code.size();
    emit(0xA9417BE0U);
    emit(0x9100C3FFU);
    emit(0x5280006AU);
    const auto branch_running_success_publish = emit(0);

    const auto running_wait_actors = code.size();
    emit_page_address(9, mailbox_main_offset);
    emit(0x52800448U);
    emit(0xB9002528U);
    const auto branch_running_wait_actors_restore = emit(0);

    const auto running_wait_player_info = code.size();
    emit_page_address(9, mailbox_main_offset);
    emit(0x528004C8U);
    emit(0xB9002528U);
    const auto branch_running_wait_player_info_restore = emit(0);

    const auto running_wait_player = code.size();
    emit_page_address(9, mailbox_main_offset);
    emit(0x528004E8U);
    emit(0xB9002528U);
    const auto branch_running_wait_player_restore = emit(0);

    const auto running_wait_restore = code.size();
    emit(0xA9417BE0U);
    emit(0x9100C3FFU);
    const auto branch_running_wait_release = emit(0);

    const auto running_wait_status = code.size();
    emit(0x52800428U);
    emit(0xB9002528U);
    const auto branch_running_wait_status_release = emit(0);

    const auto running_reject_no_request = code.size();
    emit(0x52801028U);
    emit(0xB9002528U);
    const auto branch_running_reject_no_request = emit(0);

    const auto running_reject_timeout = code.size();
    emit(0x52801048U);
    emit(0xB9002528U);
    const auto branch_running_reject_timeout = emit(0);

    const auto running_reject_type = code.size();
    emit(0x52801068U);
    emit(0xB9002528U);
    const auto branch_running_reject_type = emit(0);

    const auto running_reject_idle = code.size();
    emit(0x52801088U);
    emit(0xB9002528U);
    const auto branch_running_reject_idle = emit(0);

    const auto running_reject_status = code.size();
    emit(0x528010A8U);
    emit(0xB9002528U);
    const auto branch_running_reject_status = emit(0);

    const auto running_reject_target = code.size();
    emit(0x528010C8U);
    emit(0xB9002528U);
    const auto branch_running_reject_target = emit(0);

    const auto running_release = code.size();
    emit_address(9, mailbox_main_offset + 0x10);
    emit(0x5280004AU);
    emit(0x889FFD2AU);
    const auto branch_running_release_passthrough = emit(0);

    const auto unlock_helper = code.size();
    emit(0xF94013ECU);
    emit(0x9100A180U);
    const auto unlock_helper_tail = code.size();
    emit(EncodeRelativeBranch(
        bridge_main_offset + static_cast<u32>(unlock_helper_tail * sizeof(u32)),
        BotwPouchUnlockOffset));

    for (const auto call : unlock_calls) {
        const auto instruction_offset =
            bridge_main_offset + static_cast<u32>(call * sizeof(u32));
        const auto target_offset =
            bridge_main_offset + static_cast<u32>(unlock_helper * sizeof(u32));
        code[call] = EncodeRelativeCall(instruction_offset, target_offset);
    }

    code[branch_running] = EncodeConditionalBranch(0, branch_running, running_claim);
    code[branch_not_queued] = EncodeConditionalBranch(1, branch_not_queued, passthrough);
    code[branch_claim_retry] =
        EncodeCompareBranchZero(false, 13, branch_claim_retry, claim) | 0x01000000U;
    code[branch_no_target] =
        EncodeCompareBranchZero(true, 11, branch_no_target, reject_early);
    code[branch_rune] = EncodeConditionalBranch(0, branch_rune, rune);
    code[branch_fast_travel] =
        EncodeConditionalBranch(0, branch_fast_travel, fast_travel);
    code[branch_null_got] =
        EncodeCompareBranchZero(true, 12, branch_null_got, reject_stack);
    code[branch_null_manager] =
        EncodeCompareBranchZero(true, 12, branch_null_manager, reject_stack);
    code[branch_bad_count_high] =
        EncodeConditionalBranch(8, branch_bad_count_high, unlock_reject);
    code[branch_end_of_list] =
        EncodeConditionalBranch(0, branch_end_of_list, unlock_reject);
    code[branch_found] = EncodeConditionalBranch(0, branch_found, found);
    code[branch_continue_loop] =
        EncodeConditionalBranch(1, branch_continue_loop, loop);
    code[branch_not_usable] =
        EncodeCompareBranchZero(false, 13, branch_not_usable, unlock_reject);
    code[branch_eat] = EncodeConditionalBranch(0, branch_eat, eat);
    code[branch_hold] = EncodeConditionalBranch(0, branch_hold, hold);
    code[branch_unequip] = EncodeConditionalBranch(0, branch_unequip, unequip);
    code[branch_wrong_equip_type] =
        EncodeConditionalBranch(1, branch_wrong_equip_type, unlock_reject);
    code[branch_weapon_type] = EncodeConditionalBranch(9, branch_weapon_type, weapon);
    code[branch_bad_armor_type] = EncodeConditionalBranch(8, branch_bad_armor_type, unlock_reject);
    code[branch_armor_staged] = EncodeLocalBranch(branch_armor_staged, staged);
    code[branch_weapon_reject] =
        EncodeCompareBranchZero(false, 0, branch_weapon_reject, reject_stack);
    code[branch_weapon_staged] = EncodeLocalBranch(branch_weapon_staged, staged);
    code[branch_hold_wrong_type] =
        EncodeConditionalBranch(1, branch_hold_wrong_type, unlock_reject);
    code[branch_hold_empty] = EncodeConditionalBranch(0xB, branch_hold_empty, unlock_reject);
    code[branch_hold_no_controller] =
        EncodeCompareBranchZero(true, 0, branch_hold_no_controller, reject_stack);
    code[branch_hold_message_sender_unavailable] =
        EncodeCompareBranchZero(true, 8, branch_hold_message_sender_unavailable, reject_stack);
    code[branch_hold_message_dispatcher_unavailable] =
        EncodeCompareBranchZero(true, 8, branch_hold_message_dispatcher_unavailable, reject_stack);
    code[branch_hold_capacity_race] =
        EncodeCompareBranchZero(false, 0, branch_hold_capacity_race, reject_stack);
    code[branch_hold_quantity_mismatch] =
        EncodeConditionalBranch(1, branch_hold_quantity_mismatch, hold_native_rejected);
    code[branch_hold_no_captured_actor] =
        EncodeCompareBranchZero(true, 8, branch_hold_no_captured_actor, hold_no_captured_actor);
    code[branch_hold_no_sender_after_mutation] = EncodeCompareBranchZero(
        true, 0, branch_hold_no_sender_after_mutation, hold_no_captured_actor);
    code[branch_hold_success] = EncodeLocalBranch(branch_hold_success, success_stack);
    code[branch_hold_no_captured_actor_success] =
        EncodeLocalBranch(branch_hold_no_captured_actor_success, success_stack);
    code[branch_hold_native_rejected_publish] =
        EncodeLocalBranch(branch_hold_native_rejected_publish, reject_stack);
    code[branch_eat_food] = EncodeConditionalBranch(0, branch_eat_food, eat_type_allowed);
    code[branch_eat_wrong_type] = EncodeConditionalBranch(1, branch_eat_wrong_type, unlock_reject);
    code[branch_eat_wrong_use] = EncodeConditionalBranch(1, branch_eat_wrong_use, unlock_reject);
    code[branch_eat_empty] = EncodeConditionalBranch(0xB, branch_eat_empty, unlock_reject);
    code[branch_eat_no_player_info] =
        EncodeCompareBranchZero(true, 0, branch_eat_no_player_info, reject_stack);
    code[branch_eat_no_player] =
        EncodeCompareBranchZero(true, 0, branch_eat_no_player, reject_stack);
    code[branch_eat_refresh_no_player_info] = EncodeCompareBranchZero(
        true, 0, branch_eat_refresh_no_player_info, eat_effect_refresh_done);
    code[branch_eat_refresh_no_player] =
        EncodeCompareBranchZero(true, 0, branch_eat_refresh_no_player, eat_effect_refresh_done);
    code[branch_eat_no_notifier] =
        EncodeCompareBranchZero(true, 0, branch_eat_no_notifier, eat_notify_done);
    code[branch_eat_no_registry] =
        EncodeCompareBranchZero(true, 8, branch_eat_no_registry, reject_stack);
    code[branch_eat_no_registry_array] =
        EncodeCompareBranchZero(true, 8, branch_eat_no_registry_array, reject_stack);
    code[eat_return_adr] =
        EncodeAdr(30, bridge_main_offset + static_cast<u32>(eat_return_adr * sizeof(u32)),
                  bridge_main_offset + static_cast<u32>(eat_return * sizeof(u32)));
    code[branch_eat_success] = EncodeLocalBranch(branch_eat_success, success_stack);
    code[branch_unequip_not_equipped] =
        EncodeCompareBranchZero(false, 8, branch_unequip_not_equipped, unlock_reject);
    code[branch_unequip_bad_high] =
        EncodeConditionalBranch(8, branch_unequip_bad_high, unlock_reject);
    code[branch_unequip_arrow] = EncodeConditionalBranch(0, branch_unequip_arrow, unlock_reject);
    code[branch_unequip_no_player_info] = EncodeCompareBranchZero(
        true, 0, branch_unequip_no_player_info, reject_stack);
    code[branch_unequip_no_player] = EncodeCompareBranchZero(
        true, 0, branch_unequip_no_player, reject_stack);
    code[branch_unequip_still_equipped] = EncodeCompareBranchZero(
        false, 8, branch_unequip_still_equipped, reject_stack);
    code[branch_unequip_success] =
        EncodeLocalBranch(branch_unequip_success, success_stack);
    code[branch_fast_travel_success] =
        EncodeLocalBranch(branch_fast_travel_success, success_stack);
    code[branch_rune_bad] = EncodeConditionalBranch(8, branch_rune_bad, reject_stack);
    code[branch_rune_null] =
        EncodeCompareBranchZero(true, 0, branch_rune_null, reject_stack);
    code[branch_unlocked_reject] = EncodeLocalBranch(branch_unlocked_reject, reject_stack);
    code[branch_success_publish] = EncodeLocalBranch(branch_success_publish, publish_address);
    code[branch_eat_custom_exit] = EncodeTestBitBranch(
        true, 28, 31, branch_eat_custom_exit, eat_custom_exit);
    code[branch_staged_release] =
        EncodeLocalBranch(branch_staged_release, running_release);
    code[branch_running_claim_failed] =
        EncodeCompareBranchZero(false, 13, branch_running_claim_failed, claim) | 0x01000000U;
    code[branch_running_no_request] = EncodeCompareBranchZero(
        true, 11, branch_running_no_request, running_reject_no_request);
    code[branch_running_timeout] = EncodeConditionalBranch(
        2, branch_running_timeout, running_reject_timeout);
    code[branch_running_bad_type] = EncodeConditionalBranch(
        8, branch_running_bad_type, running_reject_type);
    code[branch_running_target_not_equipped] = EncodeCompareBranchZero(
        false, 8, branch_running_target_not_equipped, running_reject_target);
    code[branch_running_idle] = EncodeCompareBranchZero(
        false, 8, branch_running_idle, running_reject_idle);
    code[branch_running_loading] = EncodeConditionalBranch(
        0, branch_running_loading, running_wait_status);
    code[branch_running_bad_status] = EncodeConditionalBranch(
        1, branch_running_bad_status, running_reject_status);
    code[branch_running_wait_actors] = EncodeCompareBranchZero(
        false, 0, branch_running_wait_actors, running_wait_actors);
    code[branch_running_wait_player_info] = EncodeCompareBranchZero(
        true, 0, branch_running_wait_player_info, running_wait_player_info);
    code[branch_running_wait_player] = EncodeCompareBranchZero(
        true, 0, branch_running_wait_player, running_wait_player);
    code[branch_running_success_publish] =
        EncodeLocalBranch(branch_running_success_publish, publish_address);
    code[branch_running_wait_actors_restore] =
        EncodeLocalBranch(branch_running_wait_actors_restore, running_wait_restore);
    code[branch_running_wait_player_info_restore] =
        EncodeLocalBranch(branch_running_wait_player_info_restore, running_wait_restore);
    code[branch_running_wait_player_restore] =
        EncodeLocalBranch(branch_running_wait_player_restore, running_wait_restore);
    code[branch_running_wait_release] =
        EncodeLocalBranch(branch_running_wait_release, running_release);
    code[branch_running_wait_status_release] =
        EncodeLocalBranch(branch_running_wait_status_release, running_release);
    code[branch_running_reject_no_request] =
        EncodeLocalBranch(branch_running_reject_no_request, reject_early);
    code[branch_running_reject_timeout] =
        EncodeLocalBranch(branch_running_reject_timeout, reject_early);
    code[branch_running_reject_type] =
        EncodeLocalBranch(branch_running_reject_type, reject_early);
    code[branch_running_reject_idle] =
        EncodeLocalBranch(branch_running_reject_idle, reject_early);
    code[branch_running_reject_status] =
        EncodeLocalBranch(branch_running_reject_status, reject_early);
    code[branch_running_reject_target] =
        EncodeLocalBranch(branch_running_reject_target, reject_early);
    code[branch_running_release_passthrough] =
        EncodeLocalBranch(branch_running_release_passthrough, passthrough);

    if ((mailbox_main_offset & 0xFFFU) != 0 || (bridge_main_offset & 0xFFFU) != 0 ||
        bridge_size != BotwCompanionBridgePageSize ||
        bridge_main_offset != mailbox_main_offset + BotwCompanionMailboxPageSize) {
        LOG_ERROR(Loader,
                  "[BOTW-ITEM-ACTION] bridge install rejected: invalid RW/RX layout "
                  "mailbox=main+{:#x} bridge=main+{:#x} bridgeSize={:#x}",
                  mailbox_main_offset, bridge_main_offset, bridge_size);
        return false;
    }
    static_assert((BotwRuneManagerStaticOffset & 7U) == 0);
    static_assert(BotwEquipmentCompletionAttemptLimit <= 0xFFFU);
    static_assert(BotwRuneManagerStaticOffset >= BotwDataLocation);
    static_assert(BotwRuneManagerStaticOffset <
                  BotwDataLocation + BotwDataSize + BotwBssSize);
    const u32 carry_grab_stub_offset =
        bridge_main_offset + static_cast<u32>(code.size() * sizeof(u32));
    const auto carry_grab_code = BuildBotwCarryGrabReadyHook(carry_grab_stub_offset);
    code.insert(code.end(), carry_grab_code.begin(), carry_grab_code.end());
    const size_t code_size = code.size() * sizeof(u32);
    if (code_size > bridge_size) {
        LOG_ERROR(Loader,
                  "BOTW companion bridge generated {} bytes but its executable page holds "
                  "only {} bytes",
                  code_size, bridge_size);
        return false;
    }
    if (module_start > image.size()) {
        LOG_ERROR(Loader,
                  "[BOTW-ITEM-ACTION] bridge install rejected: module start {:#x} exceeds "
                  "image size {:#x}",
                  module_start, image.size());
        return false;
    }
    const size_t hook = module_start + BotwEquipHookOffset;
    const size_t eat_exit_hook = module_start + BotwEatGameplayExitHookOffset;
    const size_t carry_grab_hook = module_start + BotwCarryGrabReadyHookOffset;
    const size_t cave = module_start + bridge_main_offset;
    const size_t context_name = module_start + BotwActionContextNameOffset;
    if (cave > image.size() || code_size > image.size() - cave) {
        LOG_ERROR(Loader,
                  "[BOTW-ITEM-ACTION] bridge install rejected: cave range {:#x}+{:#x} "
                  "exceeds image size {:#x}",
                  cave, code_size, image.size());
        return false;
    }
    if (context_name > image.size() ||
        BotwActionContextName.size() + 1 > image.size() - context_name ||
        !std::equal(BotwActionContextName.begin(), BotwActionContextName.end(),
                    image.begin() + static_cast<ptrdiff_t>(context_name)) ||
        image[context_name + BotwActionContextName.size()] != 0) {
        LOG_ERROR(Loader,
                  "[BOTW-ITEM-ACTION] bridge install rejected: action-context signature "
                  "mismatch at main+{:#x}",
                  BotwActionContextNameOffset);
        return false;
    }
    struct CodeSignature {
        u32 offset;
        u32 expected;
        std::string_view name;
    };
    const CodeSignature signatures[] = {
        {BotwEquipHookOffset, BotwEquipHookInstruction, "dispatcher hook"},
        {BotwQuickEquipOffset, 0xB5000041U, "quick equip"},
        {BotwAutoEquipOffset, 0xB9401828U, "armor equip"},
        {BotwUnequipOffset, 0xB5000041U, "unequip"},
        {BotwGetDefaultEquipmentOffset, 0xD0007E89U, "default equipment"},
        {BotwHoldItemOffset, 0xD10203FFU, "hold item"},
        {BotwCanGrabAnotherItemOffset, 0x91411008U, "hold capacity"},
        {BotwEatGameplayEntryOffset, 0x90007968U, "eat gameplay entry"},
        {BotwEatGameplayEpilogueOffset, 0xA9474FF4U, "eat gameplay epilogue"},
        {BotwUpdateCookEffectsOffset, 0xFC1C0FE8U, "live cook-effect update"},
        {BotwEatNotifyOffset, 0xD10243FFU, "eat animation event"},
        {BotwActorMessageSendOffset, 0xA9BC7BFDU, "actor message send"},
        {BotwCapturePlayerAccessorOffset, 0xD0008A88U, "player accessor capture"},
        {BotwGetActorMessageIdOffset, 0xA9BE7BFDU, "actor message id"},
        {BotwReleaseCapturedActorOffset, 0xA9BE7BFDU, "captured actor release"},
        {BotwHoldMessageTypeOffset, 0x08000003U, "hold message type"},
        {BotwHoldCallerCapturePointerOffset, 0xF9400BE8U, "hold capture pointer"},
        {BotwHoldCallerSenderOffset, 0x91054276U, "hold sender"},
        {BotwHoldControllerGotLoadOffset, 0xF942A908U, "hold controller GOT"},
        {BotwHoldControllerObjectLoadOffset, 0xF9400100U, "hold controller object"},
        {BotwEatTargetReadOffset, 0xF95B267AU, "eat target read"},
        {BotwEatGameplayExitHookOffset, BotwEatGameplayExitHookInstruction, "eat exit hook"},
        {BotwCarryGrabReadyHookOffset, BotwCarryGrabReadyHookInstruction, "carry grab-ready hook"},
        {BotwCarryGrabReadyHookOffset - 8, 0x39546EA8U, "carry grabbed flag"},
        {BotwCarryGrabReadyHookOffset + 4, 0x35001B68U, "carry native held branch"},
        {0x00807094, 0x97FFFE16U, "carry native held transition"},
        {0x0056B27C, 0x9121C000U, "carry grab-helper layout"},
        {BotwRequestEquippedItemOffset, 0xD10103FFU, "equipment request"},
        {BotwEquipmentActorsReadyOffset, 0x39422008U, "equipment actor state"},
        {BotwGetPlayerOffset, 0xF9403001U, "player lookup"},
        {BotwSwitchEquipmentCalcOffset, 0xA9BD7BFDU, "equipment calculation"},
        {BotwSwitchEquipmentApplyOffset, 0xB94C9A88U, "equipment application"},
        {BotwSwitchEquipmentApplyOffset + 8, 0x321B0108U, "equipment apply flags"},
        {BotwSwitchEquipmentApplyOffset + 12, 0xB90C9A88U, "equipment apply store"},
        {BotwPlayerSwitchEquipmentOffset, 0xD101C3FFU, "player equipment switch"},
        {BotwRequestArmorOffset, 0xD10183FFU, "armor request"},
        {BotwRuneSetCurrentOffset, 0xA9BD7BFDU, "rune selection"},
        {BotwRuneSetCurrentOffset + 0x24, 0xB9024293U, "rune selection store"},
        {BotwFastTravelEventOffset, 0xB40004A0U, "fast travel event"},
        {BotwPouchLockOffset, 0x91008000U, "pouch lock"},
        {BotwPouchUnlockOffset, 0x91008000U, "pouch unlock"},
    };
    for (const auto& signature : signatures) {
        const size_t available = image.size() - module_start;
        if (signature.offset > available || sizeof(u32) > available - signature.offset) {
            LOG_ERROR(Loader,
                      "[BOTW-ITEM-ACTION] bridge install rejected: {} signature at "
                      "main+{:#x} is outside image bounds",
                      signature.name, signature.offset);
            return false;
        }
        const u32 actual = ReadCodeWord(image, module_start + signature.offset);
        if (actual != signature.expected) {
            LOG_ERROR(Loader,
                      "[BOTW-ITEM-ACTION] bridge install rejected: {} signature mismatch at "
                      "main+{:#x}; expected {:#010x}, actual {:#010x}",
                      signature.name, signature.offset, signature.expected, actual);
            return false;
        }
    }
    const auto cave_mismatch = std::find_if(
        image.begin() + static_cast<ptrdiff_t>(cave),
        image.begin() + static_cast<ptrdiff_t>(cave + code_size),
        [](u8 value) { return value != 0; });
    if (cave_mismatch != image.begin() + static_cast<ptrdiff_t>(cave + code_size)) {
        const size_t mismatch = static_cast<size_t>(cave_mismatch - image.begin());
        LOG_ERROR(Loader,
                  "[BOTW-ITEM-ACTION] bridge install rejected: executable page byte at "
                  "main+{:#x} is nonzero ({:#04x})",
                  mismatch - module_start, image[mismatch]);
        return false;
    }

    const auto direct_branch_fits = [](u32 source, u32 target) {
        const s64 delta = static_cast<s64>(target) - static_cast<s64>(source);
        return (delta & 3) == 0 && delta >= -(1LL << 27) && delta < (1LL << 27);
    };
    const u32 eat_exit_stub_offset =
        bridge_main_offset + static_cast<u32>(eat_exit_stub * sizeof(u32));
    if (!direct_branch_fits(BotwEquipHookOffset, bridge_main_offset) ||
        !direct_branch_fits(BotwEatGameplayExitHookOffset, eat_exit_stub_offset) ||
        !direct_branch_fits(BotwCarryGrabReadyHookOffset, carry_grab_stub_offset) ||
        !direct_branch_fits(carry_grab_stub_offset +
                                static_cast<u32>((carry_grab_code.size() - 1) * sizeof(u32)),
                            BotwCarryGrabReadyHookOffset + sizeof(u32))) {
        LOG_ERROR(Loader,
                  "[BOTW-ITEM-ACTION] bridge install rejected: injected page is outside "
                  "AArch64 branch range");
        return false;
    }
    const u32 branch = EncodeRelativeBranch(BotwEquipHookOffset, bridge_main_offset);
    const u32 eat_exit_branch = EncodeRelativeBranch(
        BotwEatGameplayExitHookOffset, eat_exit_stub_offset);
    const u32 carry_grab_branch =
        EncodeRelativeBranch(BotwCarryGrabReadyHookOffset, carry_grab_stub_offset);
    std::memcpy(image.data() + cave, code.data(), code_size);
    std::memcpy(image.data() + hook, &branch, sizeof(branch));
    std::memcpy(image.data() + eat_exit_hook, &eat_exit_branch, sizeof(eat_exit_branch));
    std::memcpy(image.data() + carry_grab_hook, &carry_grab_branch, sizeof(carry_grab_branch));
    LOG_INFO(Loader,
             "[BOTW-ITEM-ACTION] installed menu-independent action bridge: {} bytes, "
             "mailbox=main+{:#x}, bridge=main+{:#x}, bridgePage={} bytes, "
             "eat-entry=main+{:#x}, carry-grab-ready=main+{:#x}",
             code_size, mailbox_main_offset, bridge_main_offset, bridge_size,
             BotwEatGameplayEntryOffset, carry_grab_stub_offset);
    return true;
}

}
