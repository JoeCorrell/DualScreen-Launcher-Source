// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cinttypes>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

#include "common/common_funcs.h"
#include "common/hex_util.h"
#include "common/logging.h"
#include "common/lz4_compression.h"
#include "common/settings.h"
#include "common/swap.h"
#include "core/core.h"
#include "core/file_sys/patch_manager.h"
#include "core/hle/kernel/code_set.h"
#include "core/hle/kernel/k_page_table.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_thread.h"
#include "core/loader/nso.h"
#include "core/companion/external_module.h"
#include "core/loader/sshd_companion_payload.h"
#include "core/loader/sshd_map_ui_code.h"
#include "core/loader/smo_outfit_code.h"
#include "core/loader/smo_outfit_protocol.h"
#include "core/loader/xcx_companion_payload.h"
#include "core/memory.h"

#ifdef HAS_NCE
#include "core/arm/nce/patcher.h"
#endif

namespace Loader {
namespace {
struct MODHeader {
    u32_le magic;
    u32_le dynamic_offset;
    u32_le bss_start_offset;
    u32_le bss_end_offset;
    u32_le eh_frame_hdr_start_offset;
    u32_le eh_frame_hdr_end_offset;
    u32_le module_offset; // Offset to runtime-generated module object. typically equal to .bss base
};
static_assert(sizeof(MODHeader) == 0x1c, "MODHeader has incorrect size.");

constexpr u32 PageAlignSize(u32 size) {
    return static_cast<u32>((size + Core::Memory::YUZU_PAGEMASK) & ~Core::Memory::YUZU_PAGEMASK);
}

#if defined(BOTW_DUALSCREEN_COMPANION)




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






constexpr u64 Mk8dCompanionTitleId = 0x0100152000022000ULL;
constexpr std::array<u8, 0x20> Mk8dCompanionBuildId{
    0xFE, 0x94, 0x1E, 0xD5, 0xBA, 0x14, 0xBE, 0x5D,
    0x50, 0x56, 0x98, 0xDA, 0x1B, 0xBF, 0x4F, 0xE7,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr u32 Mk8dTextLocation = 0x00000000;
constexpr u32 Mk8dTextFileSize = 0x00B510C8;
constexpr u32 Mk8dRoDataLocation = 0x00B52000;
constexpr u32 Mk8dRoDataSize = 0x00654C68;
constexpr u32 Mk8dDataLocation = 0x011A7000;
constexpr u32 Mk8dDataSize = 0x0016ABE0;
constexpr u32 Mk8dBssSize = 0x00051420;
constexpr u32 Mk8dItemInputHookOffset = 0x0003F42C;
constexpr u32 Mk8dItemInputHookInstruction = 0xF9402E88;


constexpr u32 Mk8dItemBridgeOffset = Mk8dTextFileSize;
constexpr u32 Mk8dItemBridgeSize = Mk8dRoDataLocation - Mk8dItemBridgeOffset;
constexpr u32 Mk8dCompanionMailboxPageSize = 0x1000;



constexpr u32 SshdTextLocation = 0x00000000;
constexpr u32 SshdTextFileSize = 0x0105C950;
constexpr u32 SshdRoDataLocation = 0x0105D000;
constexpr u32 SshdRoDataSize = 0x005010B0;
constexpr u32 SshdDataLocation = 0x0155F000;
constexpr u32 SshdDataSize = 0x002BDCE0;
constexpr u32 SshdBssSize = 0x2B816320;
// dStage installs main+0xE23900 as its render callback. Hook its prologue after
// the caller link register has been saved, but before the callback can take any
// of its many early returns. Hooking the native map block at 0xE26E2C coupled
// mailbox initialization to that conditional block and left the Android client
// waiting forever whenever the block was skipped.
constexpr u32 SshdMapCaptureHookOffset = 0x00E2391C;
constexpr u32 SshdMapCaptureHookInstruction = 0x910183FD; // add x29, sp, #0x60
// The renderer has restored temporary texture-cache ownership at this point.
// Observe command-recording completion here, before the native UI can request
// another render. This is not a GPU fence; GPU consumers use the GPU scheduler.
constexpr u32 SshdMapCompleteHookOffset = 0x00E280B4;
constexpr u32 SshdMapCompleteHookInstruction = 0x3915891F; // strb wzr, [x8, #0x562]
constexpr u32 SshdMapUiHookOffset = 0x00012CC8;
constexpr u32 SshdMapUiHookInstruction = 0xB927C69F; // str wzr, [x20, #0x27c4]
constexpr u32 SshdMapInitializeOffset = 0x00C448E0;
constexpr u32 SshdMapInitializeInstruction = 0xD10303FF; // sub sp, sp, #0xc0
// The native resource initializer also enters map-camera/input mode here.
// Companion initialization skips only this side-effect block; normal map entry is unchanged.
constexpr u32 SshdMapCameraHookOffset = 0x00C46C20;
constexpr u32 SshdMapCameraHookInstruction = 0x90004FE9;
constexpr u32 SshdMapCameraResumeOffset = 0x00C46C4C;
constexpr u32 SshdTelemetryBridgeOffset = SshdTextFileSize;
constexpr u32 SshdTelemetryBridgeSize = SshdRoDataLocation - SshdTelemetryBridgeOffset;
constexpr u32 SshdCompanionMailboxPageSize = 0x1000;
constexpr u32 SshdCompanionBridgePageSize = 0x1000;

constexpr u32 XcxTextLocation = 0x00000000;
constexpr u32 XcxTextFileSize = 0x01735050;
constexpr u32 XcxRoDataLocation = 0x01736000;
constexpr u32 XcxRoDataSize = 0x004E7000;
constexpr u32 XcxDataLocation = 0x01C1D000;
constexpr u32 XcxDataSize = 0x0019F848;
constexpr u32 XcxBssSize = 0x02A027B8;
constexpr u32 XcxPartyManagerUpdateHookOffset = 0x002FB350;
constexpr u32 XcxPartyManagerUpdateHookInstruction = 0xA9BD7BFD;
constexpr u32 XcxQueueCharacterSwitchOffset = 0x002FB338;
constexpr u32 XcxMapJumpOffset = 0x00131358;
constexpr u32 XcxIsEventSceneOffset = 0x000CC394;
constexpr u32 XcxPauseManagerGotOffset = 0x01D54EC0;
constexpr u32 XcxBattleRegistryOffset = 0x01D557A8;
constexpr u32 XcxCollectBattleActorsOffset = 0x001F5750;
constexpr u32 XcxSelectedActorRootOffset = 0x026D7228;
constexpr u32 XcxCompanionMailboxPageSize = 0x1000;
constexpr u32 XcxCompanionBridgePageSize = 0x1000;

// These are raw main-NSO-text-relative addresses. The reverse-engineering
// symbols use a 0x7100004000 image base, whereas Eden's main_base starts at
// NSO text, so their 0x4000 MOD0/image bias has already been removed here.
// They remain beside the exact executable fingerprint so they are never used
// with an unverified build.
constexpr u32 SshdCurrentStageNameOffset = 0x02BF98D8;
constexpr u32 SshdStageRoomManagerPointerOffset = 0x02BF9D90;
constexpr u32 SshdMapGlobalPointerOffset = 0x0183C440;
// The 1.0.1 dLytMap_c owner embeds dLytMapMain at +0x510. Its global is at
// dLytMapMain+0x10D38 (owner+0x11248), while dLytMapCapture starts at
// dLytMapMain+0x19E0 (owner+0x1EF0). Keep this as a relative lookup so the
// bridge does not retain a stale actor pointer across UI lifetimes.
constexpr u32 SshdMapGlobalToCaptureDelta = 0xF358;
constexpr u32 SshdMapCaptureWidthOffset = 0xA0;
constexpr u32 SshdMapCaptureHeightOffset = 0xA8;
constexpr u32 SshdMapGlobalScrollOffset = 0x00;
constexpr u32 SshdMapGlobalRotationCenterOffset = 0x14;
constexpr u32 SshdMapGlobalRotationOffset = 0x3C;
constexpr u32 SshdMapGlobalScaleOffset = 0x44;
constexpr u32 SshdMapGlobalFloorOffset = 0x48;
constexpr u32 SshdMapGlobalZoomOffset = 0x64;
constexpr u32 SshdMapFloorTableOffset = 0x368;
constexpr u32 SshdMapFloorStride = 0x1C;
constexpr u32 SshdMapScrollOffset = 0x43C;
constexpr u32 SshdMapScaleOffset = 0x448;
constexpr u32 SshdMapRotationCenterOffset = 0x454;
constexpr u32 SshdMapZoomOffset = 0x460;
constexpr u32 SshdMapRotationOffset = 0x55A;
constexpr u32 SshdMapRendererTextureOffset = 0x550;
constexpr u32 SshdMapRendererFloorOffset = 0x561;
constexpr u32 SshdMapRendererRequestOffset = 0x562;
constexpr u32 SshdMapRendererAllocationOffset = 0x564;
constexpr u32 SshdCpuTextureWidthOffset = 0x08;
constexpr u32 SshdCpuTextureHeightOffset = 0x0A;
constexpr u32 SshdCpuTextureFormatOffset = 0x0D;
constexpr u32 SshdCpuTextureDataOffset = 0x18;

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

bool IsExactBotwCompanionMain(const NSOHeader& header, std::string_view name) {
    return name == "main" &&
           header.build_id == BotwCompanionBuildId &&
           header.segments[0].location == BotwTextLocation &&
           header.segments[0].size == BotwTextFileSize &&
           header.segments[1].location == BotwRoDataLocation &&
           header.segments[1].size == BotwRoDataSize &&
           header.segments[2].location == BotwDataLocation &&
           header.segments[2].size == BotwDataSize &&
           header.segments[2].bss_size == BotwBssSize;
}

bool IsExactMk8dCompanionMain(const NSOHeader& header, std::string_view name) {
    return name == "main" &&
           header.build_id == Mk8dCompanionBuildId &&
           header.segments[0].location == Mk8dTextLocation &&
           header.segments[0].size == Mk8dTextFileSize &&
           header.segments[1].location == Mk8dRoDataLocation &&
           header.segments[1].size == Mk8dRoDataSize &&
           header.segments[2].location == Mk8dDataLocation &&
           header.segments[2].size == Mk8dDataSize &&
           header.segments[2].bss_size == Mk8dBssSize;
}



bool IsExactSshdCompanionMain(const NSOHeader& header, std::string_view name) {
    return name == "main" &&
           header.build_id == SshdCompanionPayload::BuildId &&
           header.segments[0].location == SshdTextLocation &&
           header.segments[0].size == SshdTextFileSize &&
           header.segments[1].location == SshdRoDataLocation &&
           header.segments[1].size == SshdRoDataSize &&
           header.segments[2].location == SshdDataLocation &&
           header.segments[2].size == SshdDataSize &&
           header.segments[2].bss_size == SshdBssSize;
}

bool IsExactSmoCompanionMain(const NSOHeader& header, std::string_view name) {
    return name == "main" && std::equal(header.build_id.begin(), header.build_id.end(), SmoOutfit::BuildId) &&
           header.segments[0].location == 0 && header.segments[0].size == 0xbf1838 &&
           header.segments[1].location == 0xbf2000 && header.segments[1].size == 0x105b1f8 &&
           header.segments[2].location == 0x1c4e000 && header.segments[2].size == 0x213400 &&
           header.segments[2].bss_size == 0x746c00;
}

bool IsExactXcxCompanionMain(const NSOHeader& header, std::string_view name) {
    return name == "main" &&
           header.build_id == XcxCompanionPayload::BuildId &&
           header.segments[0].location == XcxTextLocation &&
           header.segments[0].size == XcxTextFileSize &&
           header.segments[1].location == XcxRoDataLocation &&
           header.segments[1].size == XcxRoDataSize &&
           header.segments[2].location == XcxDataLocation &&
           header.segments[2].size == XcxDataSize &&
           header.segments[2].bss_size == XcxBssSize;
}

u32 ReadCodeWord(const std::vector<u8>& image, size_t offset) {
    u32 word{};
    if (offset + sizeof(word) <= image.size()) {
        std::memcpy(&word, image.data() + offset, sizeof(word));
    }
    return word;
}




struct CompanionMemoryReservation {
    u32 mailbox_offset{};
    u32 bridge_offset{};
    u32 bridge_size{};
    bool external{};
};

std::optional<CompanionMemoryReservation> ReserveCompanionMailbox(
    const NSOHeader& header, std::string_view name, Kernel::CodeSet& codeset, u32& image_size) {
    const bool botw = EnableBotwCompanion && IsExactBotwCompanionMain(header, name);
    const bool mk8d = IsExactMk8dCompanionMain(header, name);
    DsNsoInfo module_info{};
    std::copy(header.build_id.begin(),header.build_id.end(),module_info.build_id);
    module_info.text_address=header.segments[0].location; module_info.text_size=header.segments[0].size;
    module_info.ro_address=header.segments[1].location; module_info.ro_size=header.segments[1].size;
    module_info.data_address=header.segments[2].location; module_info.data_size=header.segments[2].size;
    module_info.bss_size=header.segments[2].bss_size;
    const auto external=ExternalCompanion::Probe(module_info,std::string(name));
    const bool sshd = IsExactSshdCompanionMain(header, name);
    const bool xcx = IsExactXcxCompanionMain(header, name);
    const bool smo = IsExactSmoCompanionMain(header, name);
    const u32 bridge_size = botw ? BotwCompanionBridgePageSize
                                 : external ? external->bridge_size
                                 : sshd ? SshdCompanionBridgePageSize
                                 : xcx ? XcxCompanionBridgePageSize : smo ? 0x4000 : 0;
    const u32 mailbox_size = mk8d ? Mk8dCompanionMailboxPageSize
                                  : external ? external->mailbox_size
                                  : sshd ? SshdCompanionMailboxPageSize
                                  : xcx ? XcxCompanionMailboxPageSize : smo ? 0x1000
                                         : BotwCompanionMailboxPageSize;
    if ((!botw && !mk8d && !external && !sshd && !xcx && !smo) ||
        bridge_size > std::numeric_limits<u32>::max() - mailbox_size ||
        image_size > std::numeric_limits<u32>::max() - bridge_size - mailbox_size) {
        return std::nullopt;
    }
    if ((image_size & Core::Memory::YUZU_PAGEMASK) != 0 ||
        codeset.DataSegment().addr + codeset.DataSegment().size != image_size) {
        LOG_ERROR(Loader,
                  "Dual-screen companion memory reservation rejected: image/data boundary "
                  "is not page aligned or contiguous");
        return std::nullopt;
    }
    const u32 mailbox_offset = image_size;
    const u32 bridge_offset = mailbox_offset + mailbox_size;
    codeset.DataSegment().size += mailbox_size + bridge_size;
    image_size += mailbox_size + bridge_size;
    codeset.memory.resize(image_size);
    if (bridge_size != 0) {
        auto& injected = codeset.InjectedCodeSegment();
        injected.addr = bridge_offset;
        injected.offset = bridge_offset;
        injected.size = bridge_size;
    }
    return CompanionMemoryReservation{mailbox_offset, bridge_offset, bridge_size, external.has_value()};
}

bool InstallSmoOutfitPayload(std::vector<u8>& image, size_t module_start,
                            u32 mailbox_offset, u32 bridge_offset, u32 bridge_size) {
    constexpr u32 Hook = 0x4cd7b8;
    // This compiled PIC blob is linked at page offset zero. ADRP is page-relative,
    // not byte-relative: placing it directly after the 128-byte trampolines made
    // literal reads address instructions instead of the pose-validation constants.
    constexpr u32 CodeOffset = SmoOutfitPayload::CodeOffset;
    static_assert(CodeOffset >= 128 && CodeOffset % SmoOutfitPayload::CodeAlignment == 0);
    if ((bridge_offset % SmoOutfitPayload::CodeAlignment) != 0 ||
        CodeOffset + sizeof(SmoOutfitPayload::Code) > bridge_size ||
        module_start + bridge_offset + bridge_size > image.size() ||
        module_start + mailbox_offset + sizeof(SmoOutfit::Mailbox) > image.size() ||
        bridge_offset < Hook || bridge_offset - Hook >= 0x8000000) return false;
    for (const auto& signature : SmoOutfitPayload::Signatures) {
        for (size_t i = 0; i < signature.words.size(); ++i) {
            if (ReadCodeWord(image, module_start + signature.rva + 4 * i) != signature.words[i]) {
                LOG_ERROR(Loader, "[SMO] Wardrobe bridge rejected modified native function {:X}", signature.rva);
                return false;
            }
        }
    }
    // Function-entry trampoline: exePlay has only one argument (Scene*).
    // AAPCS caller-saved registers may be clobbered; preserve x0 and caller LR.
    // On success the native scene was killed, so return instead of executing it.
    std::array<u32,32> stub{
        0xA9BF7BE0, 0xAA0003E2, 0xAA1F03E3, 0xAA1F03E4,
        EncodeAdrp(0, bridge_offset + 16, 0), EncodeAddPageOffset(0, 0),
        EncodeAdrp(1, bridge_offset + 24, mailbox_offset), EncodeAddPageOffset(1, mailbox_offset),
        EncodeRelativeCall(bridge_offset + 32, bridge_offset + CodeOffset),
        0x34000060, 0xA8C17BE0, 0xD65F03C0, 0xA8C17BE0,
        0xD10403FF, EncodeRelativeBranch(bridge_offset + 56, Hook + 4), 0xD503201F,
        // StageScene::init's one start lookup. x19 is Scene*, x0/1 are
        // PlayerStartInfoHolder*/start ID. Preserve the BL return address.
        0xA9BF7BFF, 0xAA0003E3, 0xAA0103E4, 0xAA1303E2,
        EncodeAdrp(0, bridge_offset + 80, 0), EncodeAddPageOffset(0, 0),
        EncodeAdrp(1, bridge_offset + 88, mailbox_offset), EncodeAddPageOffset(1, mailbox_offset),
        EncodeRelativeCall(bridge_offset + 96, bridge_offset + CodeOffset),
        0xA8C17BFF, 0xD65F03C0, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F};
    SmoOutfit::Mailbox mailbox{};
    mailbox.magic = SmoOutfit::Magic; mailbox.version = SmoOutfit::Version;
    mailbox.size = sizeof(mailbox);
    std::memcpy(image.data() + module_start + mailbox_offset, &mailbox, sizeof(mailbox));
    std::memcpy(image.data() + module_start + bridge_offset, stub.data(), sizeof(stub));
    std::memcpy(image.data() + module_start + bridge_offset + CodeOffset,
                SmoOutfitPayload::Code.data(), sizeof(SmoOutfitPayload::Code));
    const u32 branch = EncodeRelativeBranch(Hook, bridge_offset);
    std::memcpy(image.data() + module_start + Hook, &branch, sizeof(branch));
    const u32 start_call = EncodeRelativeCall(0x4c9b30, bridge_offset + 64);
    std::memcpy(image.data() + module_start + 0x4c9b30, &start_call, sizeof(start_call));
    LOG_INFO(Loader, "[SMO] Location-preserving wardrobe bridge installed, mailbox={:X} code={:X}",
             mailbox_offset, bridge_offset + CodeOffset);
    return true;
}

bool InstallSshdCompanionTelemetryPayload(std::vector<u8>& image, size_t module_start,
                                          u32 mailbox_main_offset,
                                          u32 bridge_main_offset, u32 bridge_size) {
    using Payload = SshdCompanionPayload::Mailbox;

    std::vector<u32> code;
    code.reserve(192);
    const auto emit = [&](u32 instruction) {
        const size_t index = code.size();
        code.push_back(instruction);
        return index;
    };
    const auto emit_address = [&](u32 target_register, u32 target_offset) {
        const auto instruction_offset =
            bridge_main_offset + static_cast<u32>(code.size() * sizeof(u32));
        emit(EncodeAdrp(target_register, instruction_offset, target_offset));
        emit(EncodeAddPageOffset(target_register, target_offset));
    };
    const auto emit_load_address = [&](u32 target_register, u32 target_offset) {
        const auto instruction_offset =
            bridge_main_offset + static_cast<u32>(code.size() * sizeof(u32));
        emit(EncodeAdrp(target_register, instruction_offset, target_offset));
        emit(EncodeLoadUnsigned64(target_register, target_register, target_offset & 0xFFFU));
    };
    const auto emit_store_pair64 = [&](u32 first, u32 second, u32 offset) {
        emit(0xA9000000U | (((offset / 8U) & 0x7FU) << 15) |
             ((second & 0x1FU) << 10) | (31U << 5) | (first & 0x1FU));
    };
    const auto emit_load_pair64 = [&](u32 first, u32 second, u32 offset) {
        emit(0xA9400000U | (((offset / 8U) & 0x7FU) << 15) |
             ((second & 0x1FU) << 10) | (31U << 5) | (first & 0x1FU));
    };
    const auto emit_move_wide = [&](u32 target_register, u16 value, bool high, bool keep) {
        const u32 base = keep ? 0x72800000U : 0x52800000U;
        emit(base | (high ? 0x00200000U : 0U) | (static_cast<u32>(value) << 5) |
             (target_register & 0x1FU));
    };
    const auto emit_compare_registers = [&](bool is_64_bit, u32 left, u32 right) {
        emit((is_64_bit ? 0xEB00001FU : 0x6B00001FU) |
             ((right & 0x1FU) << 16) | ((left & 0x1FU) << 5));
    };

    constexpr u32 StackSize = 0x2B0;
    emit(0xD1000000U | (StackSize << 10) | (31U << 5) | 31U);
    emit_store_pair64(9, 10, 0x00);
    emit_store_pair64(11, 12, 0x10);
    emit_store_pair64(13, 14, 0x20);
    emit_store_pair64(15, 30, 0x30);
    emit(0xD53B4200U | 17U); // mrs x17, nzcv
    emit(EncodeStoreUnsigned64(17, 31, 0x40));
    // Native initialization is an AAPCS call. Preserve all volatile integer
    // registers and all 128 bits of every SIMD register, including the upper
    // halves of q8-q15 which the callee need not preserve.
    emit_store_pair64(0, 1, 0x50);
    emit_store_pair64(2, 3, 0x60);
    emit_store_pair64(4, 5, 0x70);
    emit_store_pair64(6, 7, 0x80);
    emit_store_pair64(8, 18, 0x90);
    emit(0xD53B4400U | 17U); // mrs x17, fpcr
    emit(EncodeStoreUnsigned64(17, 31, 0xA0));
    emit(0xD53B4420U | 17U); // mrs x17, fpsr
    emit(EncodeStoreUnsigned64(17, 31, 0xA8));
    for (u32 reg = 0; reg < 32; ++reg) {
        emit(0x3D800000U | (((0xB0U + reg * 16U) / 16U) << 10) | (31U << 5) | reg);
    }

    // Both hook sites enter the same register-preserving island. Replay each
    // displaced instruction before inspecting the mailbox. The prologue's SP
    // is above both the island's 0x10 bytes and this bridge's saved context.
    emit_address(16, SshdMapUiHookOffset + sizeof(u32));
    emit_compare_registers(true, 30, 16);
    const auto branch_ui_entry = emit(0);
    emit_address(16, SshdMapCompleteHookOffset + sizeof(u32));
    emit_compare_registers(true, 30, 16);
    const auto branch_completion_entry = emit(0);
    emit(0x91000000U | ((StackSize + 0x10U + 0x60U) << 10) | (31U << 5) | 29U);
    emit_address(9, mailbox_main_offset);
    emit(EncodeLoadUnsigned32(10, 9, offsetof(Payload, ui_frame_tick)));
    emit(EncodeAddImmediate32(10, 10, 1));
    emit(EncodeStoreUnsigned32(10, 9, offsetof(Payload, ui_frame_tick)));
    const auto branch_entry_ready = emit(0);
    const auto completion_entry = code.size();
    emit(SshdMapCompleteHookInstruction);
    const auto entry_ready = code.size();
    code[branch_completion_entry] =
        EncodeConditionalBranch(0, branch_completion_entry, completion_entry);
    code[branch_entry_ready] = EncodeLocalBranch(branch_entry_ready, entry_ready);

    emit_address(9, mailbox_main_offset);
    emit_move_wide(11, static_cast<u16>(SshdCompanionPayload::Magic & 0xFFFFU), false, false);
    emit_move_wide(11, static_cast<u16>(SshdCompanionPayload::Magic >> 16), true, true);
    emit(EncodeStoreUnsigned32(11, 9, offsetof(Payload, magic)));
    emit_move_wide(11, static_cast<u16>(SshdCompanionPayload::Version), false, false);
    emit(EncodeStoreUnsigned32(11, 9, offsetof(Payload, version)));
    emit_move_wide(11, static_cast<u16>(sizeof(Payload)), false, false);
    emit(EncodeStoreUnsigned32(11, 9, offsetof(Payload, size)));

    emit(EncodeLoadUnsigned32(10, 9, offsetof(Payload, map_state)));
    emit(EncodeCompareImmediate32(10,
                                  static_cast<u32>(SshdCompanionPayload::MapCaptureState::Pending)));
    const auto branch_state_pending = emit(0);
    emit(EncodeCompareImmediate32(10,
                                  static_cast<u32>(SshdCompanionPayload::MapCaptureState::Ready)));
    const auto branch_state_ready = emit(0);
    emit(EncodeCompareImmediate32(
        10, static_cast<u32>(SshdCompanionPayload::MapCaptureState::Rejected)));
    const auto branch_state_rejected = emit(0);

    const auto idle = code.size();
    emit(EncodeLoadUnsigned32(15, 9, offsetof(Payload, map_request)));
    const auto branch_no_request = emit(0);
    emit(EncodeLoadUnsigned32(13, 9, offsetof(Payload, map_completed)));
    emit_compare_registers(false, 15, 13);
    const auto branch_request_completed = emit(0);
    emit(EncodeStoreUnsigned32(15, 9, offsetof(Payload, map_in_flight)));
    emit(EncodeStoreUnsigned32(31, 9, offsetof(Payload, map_reserved)));
    emit_load_address(11, SshdStageRoomManagerPointerOffset);
    const auto branch_request_no_stage = emit(0);
    emit(EncodeStoreUnsigned64(11, 9, offsetof(Payload, map_stage)));
    emit(EncodeStoreUnsigned32(31, 9, offsetof(Payload, map_result)));

    // Reproduce the final state written by dLytMapCapture's native capture
    // request. Setting dStage+0x562 alone leaves the scroll/scale fields at
    // their gameplay defaults and collapses the captured local map to a thin
    // stripe. The real path uses dLytMapGlobal for the active floor, camera,
    // rotation and zoom, plus dLytMapCapture's pane size for the aspect ratio.
    emit_load_address(12, SshdMapGlobalPointerOffset);
    const auto branch_request_no_map_global = emit(0);
    emit_move_wide(13, static_cast<u16>(SshdMapGlobalToCaptureDelta), false, false);
    emit(EncodeSubtractRegisters64(13, 12, 13)); // capture = global - 0xF358
    emit(EncodeLoadUnsigned32(17, 13, SshdMapCaptureWidthOffset));
    const auto branch_map_initialized = emit(0);
    // RenderingWait's native initializer selects the current stage resources,
    // discovered geometry, floor bounds, and map transforms. Invoke it on the
    // game thread with the already-created map controller, without opening or
    // advancing the interactive map state machine.
    emit_load_address(17, 0x02BF9940); // current Link actor
    const auto branch_initialize_no_player = emit(0);
    emit_move_wide(16, 0x0D38, false, false);
    emit_move_wide(16, 1, true, true);
    emit(EncodeSubtractRegisters64(0, 12, 16)); // MapMain = global - 0x10D38
    emit(EncodeLoadUnsigned64(17, 0, 0x1A8)); // initialized layout root
    const auto branch_initialize_no_layout = emit(0);
    emit_move_wide(10, 2, false, false);
    emit(EncodeStoreUnsigned32(10, 9, offsetof(Payload, ui_guard)));
    emit_address(16, SshdMapInitializeOffset);
    emit(0xD63F0200U); // blr x16
    emit_address(9, mailbox_main_offset);
    emit(EncodeStoreUnsigned32(31, 9, offsetof(Payload, ui_guard)));
    emit(EncodeLoadUnsigned32(10, 9, offsetof(Payload, ui_reserved)));
    emit(EncodeAddImmediate32(10, 10, 1));
    emit(EncodeStoreUnsigned32(10, 9, offsetof(Payload, ui_reserved)));
    // MapMain, dStage and even the native texture cache key can be reused
    // across areas. RenderingWait resets the picture/layout despite identical
    // pointers. Rebind and finish the noninteractive layout animation for this
    // initialization, otherwise only the independently updated pins remain.
    emit(EncodeStoreUnsigned64(31, 9, offsetof(Payload, ui_owner)));
    emit(EncodeStoreUnsigned32(31, 9, offsetof(Payload, ui_frames)));
    emit_load_address(11, SshdStageRoomManagerPointerOffset);
    emit_load_address(12, SshdMapGlobalPointerOffset);
    emit_move_wide(13, static_cast<u16>(SshdMapGlobalToCaptureDelta), false, false);
    emit(EncodeSubtractRegisters64(13, 12, 13));
    const auto map_initialized = code.size();
    emit(EncodeLoadUnsigned32(17, 12, SshdMapGlobalScaleOffset));
    const auto branch_invalid_map_scale = emit(0);
    emit(EncodeLoadUnsigned32(17, 13, SshdMapCaptureWidthOffset));
    const auto branch_invalid_capture_width = emit(0);
    emit(EncodeLoadUnsigned32(17, 13, SshdMapCaptureHeightOffset));
    const auto branch_invalid_capture_height = emit(0);

    emit(EncodeLoadUnsigned32(10, 12, SshdMapGlobalFloorOffset));
    emit(EncodeAddImmediate32(14, 10, 3));
    emit(EncodeCompareImmediate32(14, 7));
    const auto branch_invalid_map_floor = emit(0);
    emit_move_wide(15, static_cast<u16>(SshdMapFloorStride), false, false);
    emit(EncodeMultiplyAdd64(14, 14, 15, 11));
    emit(EncodeLoadUnsigned8(17, 14, SshdMapFloorTableOffset));
    const auto branch_missing_map_floor = emit(0);
    emit(EncodeStoreUnsigned8(10, 11, SshdMapRendererFloorOffset));

    for (u32 component = 0; component < 3; ++component) {
        emit(EncodeLoadUnsigned32(14, 12, SshdMapGlobalScrollOffset + component * 4));
        emit(EncodeStoreUnsigned32(14, 11, SshdMapScrollOffset + component * 4));
    }
    emit(EncodeLoadFloat32(0, 12, SshdMapGlobalScaleOffset));
    emit(0x1E2E1001U); // fmov s1, #1.0
    emit(0x1E201821U); // fdiv s1, s1, s0
    emit(EncodeLoadFloat32(0, 13, SshdMapCaptureWidthOffset));
    emit(EncodeLoadFloat32(2, 13, SshdMapCaptureHeightOffset));
    emit(0x1E210800U); // fmul s0, s0, s1
    emit(0x1E220821U); // fmul s1, s1, s2
    emit(EncodeStoreFloat32(0, 11, SshdMapScaleOffset));
    emit(0x1E2E1002U); // fmov s2, #1.0
    emit(EncodeStoreFloat32(2, 11, SshdMapScaleOffset + 4));
    emit(EncodeStoreFloat32(1, 11, SshdMapScaleOffset + 8));
    for (u32 component = 0; component < 3; ++component) {
        emit(EncodeLoadUnsigned32(
            14, 12, SshdMapGlobalRotationCenterOffset + component * 4));
        emit(EncodeStoreUnsigned32(
            14, 11, SshdMapRotationCenterOffset + component * 4));
    }
    emit(EncodeLoadUnsigned32(14, 12, SshdMapGlobalZoomOffset));
    emit(EncodeStoreUnsigned32(14, 11, SshdMapZoomOffset));
    emit(EncodeLoadUnsigned16(17, 12, SshdMapGlobalRotationOffset));
    emit(0x4B1103F1U); // neg w17, w17
    emit(EncodeStoreUnsigned16(17, 11, SshdMapRotationOffset));

    // Keep the native GX texture allocated, then let the verified stage map
    // renderer draw the local map and its discovered-state overlays. It clears
    // +0x562 only after the image is complete.
    // +0x564 is a bitfield shared with the game's own render modes, so preserve
    // every native bit and only enable the allocation bit owned by this capture.
    emit(EncodeLoadUnsigned8(13, 11, SshdMapRendererAllocationOffset));
    emit(0x320001ADU); // orr w13, w13, #1
    emit(EncodeStoreUnsigned8(13, 11, SshdMapRendererAllocationOffset));
    emit_move_wide(13, 1, false, false);
    emit(EncodeStoreUnsigned8(13, 11, SshdMapRendererRequestOffset));
    emit(0xD5033BBFU); // dmb ish
    emit_move_wide(
        13, static_cast<u16>(SshdCompanionPayload::MapCaptureState::Pending), false, false);
    emit(EncodeStoreUnsigned32(13, 9, offsetof(Payload, map_state)));
    const auto branch_request_exit = emit(0);

    const auto pending = code.size();
    emit_load_address(11, SshdStageRoomManagerPointerOffset);
    const auto branch_pending_no_stage = emit(0);
    emit(EncodeLoadUnsigned64(12, 9, offsetof(Payload, map_stage)));
    emit_compare_registers(true, 11, 12);
    const auto branch_stage_changed = emit(0);
    emit(EncodeLoadUnsigned8(13, 11, SshdMapRendererRequestOffset));
    emit(EncodeCompareImmediate32(13, 0));
    const auto branch_pending_busy = emit(0);
    emit(EncodeLoadUnsigned64(12, 11, SshdMapRendererTextureOffset));
    const auto branch_pending_no_texture = emit(0);
    emit(EncodeLoadUnsigned16(13, 12, SshdCpuTextureWidthOffset));
    const auto branch_invalid_width = emit(0);
    emit(EncodeLoadUnsigned16(14, 12, SshdCpuTextureHeightOffset));
    const auto branch_invalid_height = emit(0);
    emit(EncodeLoadUnsigned8(10, 12, SshdCpuTextureFormatOffset));
    emit(EncodeLoadUnsigned64(15, 12, SshdCpuTextureDataOffset));
    const auto branch_invalid_data = emit(0);

    emit(EncodeLoadUnsigned32(17, 9, offsetof(Payload, map_metadata_sequence)));
    emit(EncodeAddImmediate32(17, 17, 1));
    emit(EncodeStoreUnsigned32(17, 9, offsetof(Payload, map_metadata_sequence)));
    emit(0xD5033BBFU); // dmb ish
    emit(EncodeStoreUnsigned64(11, 9, offsetof(Payload, map_stage)));
    emit(EncodeStoreUnsigned64(12, 9, offsetof(Payload, map_texture)));
    emit(EncodeStoreUnsigned64(15, 9, offsetof(Payload, map_data)));
    emit(EncodeStoreUnsigned16(13, 9, offsetof(Payload, map_width)));
    emit(EncodeStoreUnsigned16(14, 9, offsetof(Payload, map_height)));
    emit(EncodeStoreUnsigned8(10, 9, offsetof(Payload, map_format)));
    emit(EncodeLoadUnsigned8(17, 11, SshdMapRendererFloorOffset));
    emit(EncodeStoreUnsigned8(17, 9, offsetof(Payload, map_floor)));
    emit_address(17, SshdCurrentStageNameOffset);
    emit(EncodeLoadUnsigned64(17, 17, 0));
    emit(EncodeStoreUnsigned64(17, 9, offsetof(Payload, map_stage_name)));
    emit(EncodeStoreUnsigned32(31, 9, offsetof(Payload, map_result)));
    emit(0xD5033BBFU); // dmb ish
    emit(EncodeLoadUnsigned32(17, 9, offsetof(Payload, map_metadata_sequence)));
    emit(EncodeAddImmediate32(17, 17, 1));
    emit(EncodeStoreUnsigned32(17, 9, offsetof(Payload, map_metadata_sequence)));
    emit(EncodeLoadUnsigned32(15, 9, offsetof(Payload, map_in_flight)));
    emit(EncodeStoreUnsigned32(15, 9, offsetof(Payload, map_completed)));
    emit(0xD5033BBFU); // dmb ish
    emit_move_wide(13, static_cast<u16>(SshdCompanionPayload::MapCaptureState::Ready),
                   false, false);
    emit(EncodeStoreUnsigned32(13, 9, offsetof(Payload, map_state)));
    const auto branch_ready_exit = emit(0);

    const auto pending_no_texture = code.size();
    emit_move_wide(10, static_cast<u16>(SshdCompanionPayload::MapCaptureResult::NoTexture),
                   false, false);
    emit(EncodeStoreUnsigned32(10, 9, offsetof(Payload, map_result)));
    const auto branch_pending_no_texture_wait = emit(0);
    const auto pending_zero_width = code.size();
    emit_move_wide(
        10, static_cast<u16>(SshdCompanionPayload::MapCaptureResult::TextureWidthZero),
        false, false);
    emit(EncodeStoreUnsigned32(10, 9, offsetof(Payload, map_result)));
    const auto branch_pending_zero_width_wait = emit(0);
    const auto pending_zero_height = code.size();
    emit_move_wide(
        10, static_cast<u16>(SshdCompanionPayload::MapCaptureResult::TextureHeightZero),
        false, false);
    emit(EncodeStoreUnsigned32(10, 9, offsetof(Payload, map_result)));
    const auto branch_pending_zero_height_wait = emit(0);
    const auto pending_missing_data = code.size();
    emit_move_wide(
        10, static_cast<u16>(SshdCompanionPayload::MapCaptureResult::TextureDataMissing),
        false, false);
    emit(EncodeStoreUnsigned32(10, 9, offsetof(Payload, map_result)));

    const auto pending_not_ready = code.size();
    emit(EncodeLoadUnsigned32(17, 9, offsetof(Payload, map_reserved)));
    emit(EncodeAddImmediate32(17, 17, 1));
    emit(EncodeStoreUnsigned32(17, 9, offsetof(Payload, map_reserved)));
    emit(EncodeCompareImmediate32(17, 120));
    const auto branch_pending_retry = emit(0);
    emit(EncodeLoadUnsigned32(10, 9, offsetof(Payload, map_result)));
    const auto branch_pending_retry_reject = emit(0);

    const auto wait_ack = code.size();
    emit(EncodeLoadUnsigned32(13, 9, offsetof(Payload, map_acknowledged)));
    emit(EncodeLoadUnsigned32(14, 9, offsetof(Payload, map_completed)));
    emit_compare_registers(false, 13, 14);
    const auto branch_ack_pending = emit(0);
    emit(EncodeStoreUnsigned32(31, 9, offsetof(Payload, map_state)));
    const auto branch_ack_exit = emit(0);

    const auto reject_no_stage = code.size();
    emit_move_wide(10, static_cast<u16>(SshdCompanionPayload::MapCaptureResult::NoStage),
                   false, false);
    const auto branch_no_stage_reject = emit(0);
    const auto reject_stage_changed = code.size();
    emit_move_wide(
        10, static_cast<u16>(SshdCompanionPayload::MapCaptureResult::StageChanged), false,
        false);
    const auto branch_stage_changed_reject = emit(0);
    const auto reject_no_texture = code.size();
    emit_move_wide(10, static_cast<u16>(SshdCompanionPayload::MapCaptureResult::NoTexture),
                   false, false);
    const auto branch_no_texture_reject = emit(0);
    const auto reject_no_map_global = code.size();
    emit_move_wide(
        10, static_cast<u16>(SshdCompanionPayload::MapCaptureResult::NoMapGlobal), false,
        false);
    const auto branch_no_map_global_reject = emit(0);
    const auto reject_invalid_map_scale = code.size();
    emit_move_wide(
        10, static_cast<u16>(SshdCompanionPayload::MapCaptureResult::InvalidMapScale), false,
        false);
    const auto branch_invalid_map_scale_reject = emit(0);
    const auto reject_invalid_capture_width = code.size();
    emit_move_wide(
        10, static_cast<u16>(SshdCompanionPayload::MapCaptureResult::InvalidCaptureWidth),
        false, false);
    const auto branch_invalid_capture_width_reject = emit(0);
    const auto reject_invalid_capture_height = code.size();
    emit_move_wide(
        10, static_cast<u16>(SshdCompanionPayload::MapCaptureResult::InvalidCaptureHeight),
        false, false);
    const auto branch_invalid_capture_height_reject = emit(0);
    const auto reject_invalid_map_floor = code.size();
    emit_move_wide(
        10, static_cast<u16>(SshdCompanionPayload::MapCaptureResult::InvalidMapFloor), false,
        false);
    const auto branch_invalid_map_floor_reject = emit(0);
    const auto reject_missing_map_floor = code.size();
    emit_move_wide(
        10, static_cast<u16>(SshdCompanionPayload::MapCaptureResult::MissingMapFloor), false,
        false);

    const auto reject = code.size();
    emit(EncodeLoadUnsigned32(17, 9, offsetof(Payload, map_metadata_sequence)));
    emit(EncodeAddImmediate32(17, 17, 1));
    emit(EncodeStoreUnsigned32(17, 9, offsetof(Payload, map_metadata_sequence)));
    emit(0xD5033BBFU); // dmb ish
    emit(EncodeStoreUnsigned64(31, 9, offsetof(Payload, map_stage)));
    emit(EncodeStoreUnsigned64(31, 9, offsetof(Payload, map_texture)));
    emit(EncodeStoreUnsigned64(31, 9, offsetof(Payload, map_data)));
    emit(EncodeStoreUnsigned16(31, 9, offsetof(Payload, map_width)));
    emit(EncodeStoreUnsigned16(31, 9, offsetof(Payload, map_height)));
    emit(EncodeStoreUnsigned8(31, 9, offsetof(Payload, map_format)));
    emit(EncodeStoreUnsigned8(31, 9, offsetof(Payload, map_floor)));
    emit(EncodeStoreUnsigned64(31, 9, offsetof(Payload, map_stage_name)));
    emit(EncodeStoreUnsigned32(10, 9, offsetof(Payload, map_result)));
    emit(0xD5033BBFU); // dmb ish
    emit(EncodeLoadUnsigned32(17, 9, offsetof(Payload, map_metadata_sequence)));
    emit(EncodeAddImmediate32(17, 17, 1));
    emit(EncodeStoreUnsigned32(17, 9, offsetof(Payload, map_metadata_sequence)));
    emit(EncodeLoadUnsigned32(13, 9, offsetof(Payload, map_in_flight)));
    emit(EncodeStoreUnsigned32(13, 9, offsetof(Payload, map_completed)));
    emit(0xD5033BBFU); // dmb ish
    emit_move_wide(13, static_cast<u16>(SshdCompanionPayload::MapCaptureState::Rejected),
                   false, false);
    emit(EncodeStoreUnsigned32(13, 9, offsetof(Payload, map_state)));

    const auto exit = code.size();
    for (u32 reg = 0; reg < 32; ++reg) {
        emit(0x3DC00000U | (((0xB0U + reg * 16U) / 16U) << 10) | (31U << 5) | reg);
    }
    emit(EncodeLoadUnsigned64(17, 31, 0xA0));
    emit(0xD51B4400U | 17U); // msr fpcr, x17
    emit(EncodeLoadUnsigned64(17, 31, 0xA8));
    emit(0xD51B4420U | 17U); // msr fpsr, x17
    emit_load_pair64(8, 18, 0x90);
    emit_load_pair64(6, 7, 0x80);
    emit_load_pair64(4, 5, 0x70);
    emit_load_pair64(2, 3, 0x60);
    emit_load_pair64(0, 1, 0x50);
    emit(EncodeLoadUnsigned64(17, 31, 0x40));
    emit(0xD51B4200U | 17U); // msr nzcv, x17
    emit_load_pair64(15, 30, 0x30);
    emit_load_pair64(13, 14, 0x20);
    emit_load_pair64(11, 12, 0x10);
    emit_load_pair64(9, 10, 0x00);
    emit(0x91000000U | (StackSize << 10) | (31U << 5) | 31U); // add sp, sp, #0x80
    emit(EncodeLoadUnsigned64(16, 31, 0));
    emit(EncodeLoadUnsigned64(17, 31, 8));
    emit(0x910043FFU); // add sp, sp, #0x10
    emit(0xD65F03C0U); // ret

    code[branch_state_pending] =
        EncodeConditionalBranch(0, branch_state_pending, pending);
    code[branch_state_ready] = EncodeConditionalBranch(0, branch_state_ready, wait_ack);
    code[branch_state_rejected] =
        EncodeConditionalBranch(0, branch_state_rejected, wait_ack);
    code[branch_no_request] =
        EncodeCompareBranchZero(false, 15, branch_no_request, exit);
    code[branch_request_completed] =
        EncodeConditionalBranch(0, branch_request_completed, exit);
    code[branch_request_no_stage] =
        EncodeCompareBranchZero(true, 11, branch_request_no_stage, reject_no_stage);
    code[branch_request_no_map_global] = EncodeCompareBranchZero(
        true, 12, branch_request_no_map_global, reject_no_map_global);
    code[branch_map_initialized] = EncodeCompareBranchZero(
        false, 17, branch_map_initialized, map_initialized) | 0x01000000U;
    code[branch_initialize_no_player] = EncodeCompareBranchZero(
        true, 17, branch_initialize_no_player, reject_no_map_global);
    code[branch_initialize_no_layout] = EncodeCompareBranchZero(
        true, 17, branch_initialize_no_layout, reject_no_map_global);
    code[branch_invalid_map_scale] = EncodeCompareBranchZero(
        false, 17, branch_invalid_map_scale, reject_invalid_map_scale);
    code[branch_invalid_capture_width] = EncodeCompareBranchZero(
        false, 17, branch_invalid_capture_width, reject_invalid_capture_width);
    code[branch_invalid_capture_height] = EncodeCompareBranchZero(
        false, 17, branch_invalid_capture_height, reject_invalid_capture_height);
    code[branch_invalid_map_floor] = EncodeConditionalBranch(
        8, branch_invalid_map_floor, reject_invalid_map_floor); // b.hi
    code[branch_missing_map_floor] = EncodeCompareBranchZero(
        false, 17, branch_missing_map_floor, reject_missing_map_floor);
    code[branch_request_exit] = EncodeLocalBranch(branch_request_exit, exit);
    code[branch_pending_no_stage] =
        EncodeCompareBranchZero(true, 11, branch_pending_no_stage, reject_no_stage);
    code[branch_stage_changed] =
        EncodeConditionalBranch(1, branch_stage_changed, reject_stage_changed);
    code[branch_pending_busy] = EncodeConditionalBranch(1, branch_pending_busy, exit);
    code[branch_pending_no_texture] =
        EncodeCompareBranchZero(true, 12, branch_pending_no_texture, pending_no_texture);
    code[branch_invalid_width] =
        EncodeCompareBranchZero(false, 13, branch_invalid_width, pending_zero_width);
    code[branch_invalid_height] =
        EncodeCompareBranchZero(false, 14, branch_invalid_height, pending_zero_height);
    code[branch_invalid_data] =
        EncodeCompareBranchZero(true, 15, branch_invalid_data, pending_missing_data);
    code[branch_ready_exit] = EncodeLocalBranch(branch_ready_exit, exit);
    code[branch_pending_no_texture_wait] =
        EncodeLocalBranch(branch_pending_no_texture_wait, pending_not_ready);
    code[branch_pending_zero_width_wait] =
        EncodeLocalBranch(branch_pending_zero_width_wait, pending_not_ready);
    code[branch_pending_zero_height_wait] =
        EncodeLocalBranch(branch_pending_zero_height_wait, pending_not_ready);
    code[branch_pending_retry] =
        EncodeConditionalBranch(3, branch_pending_retry, exit); // b.lo
    code[branch_pending_retry_reject] =
        EncodeLocalBranch(branch_pending_retry_reject, reject);
    code[branch_ack_pending] = EncodeConditionalBranch(1, branch_ack_pending, exit);
    code[branch_ack_exit] = EncodeLocalBranch(branch_ack_exit, exit);
    code[branch_no_stage_reject] = EncodeLocalBranch(branch_no_stage_reject, reject);
    code[branch_stage_changed_reject] =
        EncodeLocalBranch(branch_stage_changed_reject, reject);
    code[branch_no_texture_reject] =
        EncodeLocalBranch(branch_no_texture_reject, reject);
    code[branch_no_map_global_reject] =
        EncodeLocalBranch(branch_no_map_global_reject, reject);
    code[branch_invalid_map_scale_reject] =
        EncodeLocalBranch(branch_invalid_map_scale_reject, reject);
    code[branch_invalid_capture_width_reject] =
        EncodeLocalBranch(branch_invalid_capture_width_reject, reject);
    code[branch_invalid_capture_height_reject] =
        EncodeLocalBranch(branch_invalid_capture_height_reject, reject);
    code[branch_invalid_map_floor_reject] =
        EncodeLocalBranch(branch_invalid_map_floor_reject, reject);

    const auto ui_entry = code.size();
    code[branch_ui_entry] = EncodeConditionalBranch(0, branch_ui_entry, ui_entry);
    emit(SshdMapUiHookInstruction);
    // m2d setup runs on multiple NVN channels. The companion owns channel zero;
    // another channel's projection/state is not a valid context for its pass.
    emit_address(16, 0x04CBDBA0);
    emit_compare_registers(true, 20, 16);
    const auto branch_wrong_ui_channel = emit(0);
    code[branch_wrong_ui_channel] = EncodeConditionalBranch(1, branch_wrong_ui_channel, exit);
    emit_load_address(2, SshdMapGlobalPointerOffset);
    emit_move_wide(16, 0x0D38, false, false);
    emit_move_wide(16, 1, true, true);
    emit(0xCB100042U); // sub x2, x2, x16: MapMain = MapGlobal - 0x10d38
    emit_move_wide(3, 0, false, false); // native GUI channel 0
    emit_address(0, 0); // relocated main base
    emit_address(1, mailbox_main_offset);
    const auto ui_call = emit(0);
    const auto ui_exit = emit(0);
    code[ui_exit] = EncodeLocalBranch(ui_exit, exit);
    const auto ui_code = code.size();
    code[ui_call] = EncodeRelativeCall(bridge_main_offset + ui_call * 4,
                                      bridge_main_offset + ui_code * 4);
    code.insert(code.end(), SshdCompanionPayload::MapUiCode.begin(),
                SshdCompanionPayload::MapUiCode.end());
    const size_t code_size = code.size() * sizeof(u32);
    const std::array<u32, 16> island{{
        0xD10043FFU, // sub sp, sp, #0x10
        0xA90047F0U, // stp x16, x17, [sp]
        EncodeAdrp(16, SshdTelemetryBridgeOffset + 2 * sizeof(u32), bridge_main_offset),
        EncodeAddPageOffset(16, bridge_main_offset),
        0xD61F0200U, // br x16
        // Camera gate, entered by B: preserve LR and NZCV as well as scratch registers.
        0xD10043FFU,
        0xA90047F0U,
        EncodeAdrp(16, SshdTelemetryBridgeOffset + 7 * 4, mailbox_main_offset),
        EncodeAddPageOffset(16, mailbox_main_offset),
        EncodeLoadUnsigned32(16, 16, offsetof(Payload, ui_guard)),
        0x36000000U | (1U << 19) | (3U << 5) | 16U, // tbz w16,#1,normal
        0xA8C147F0U, // ldp x16,x17,[sp],#16
        EncodeRelativeCall(SshdTelemetryBridgeOffset + 12 * 4,
                           SshdMapCameraResumeOffset) & ~0x80000000U,
        0xA8C147F0U,
        EncodeAdrp(9, SshdTelemetryBridgeOffset + 14 * 4, 0x01642000),
        EncodeRelativeCall(SshdTelemetryBridgeOffset + 15 * 4,
                           SshdMapCameraHookOffset + 4) & ~0x80000000U,
    }};
    const size_t island_size = island.size() * sizeof(u32);
    if ((mailbox_main_offset & 0xFFFU) != 0 || (bridge_main_offset & 0xFFFU) != 0 ||
        bridge_size != SshdCompanionBridgePageSize ||
        bridge_main_offset != mailbox_main_offset + SshdCompanionMailboxPageSize ||
        code_size > bridge_size || island_size > SshdTelemetryBridgeSize ||
        module_start > image.size()) {
        LOG_ERROR(Loader, "[SSHD-MAP] bridge install rejected: invalid RW/RX layout");
        return false;
    }
    const size_t hook = module_start + SshdMapCaptureHookOffset;
    const size_t complete_hook = module_start + SshdMapCompleteHookOffset;
    const size_t ui_hook = module_start + SshdMapUiHookOffset;
    const size_t island_cave = module_start + SshdTelemetryBridgeOffset;
    const size_t bridge_cave = module_start + bridge_main_offset;
    if (hook + sizeof(u32) > image.size() || complete_hook + sizeof(u32) > image.size() ||
        island_cave + island_size > image.size() ||
        bridge_cave + code_size > image.size() ||
        ReadCodeWord(image, hook) != SshdMapCaptureHookInstruction ||
        ReadCodeWord(image, complete_hook) != SshdMapCompleteHookInstruction ||
        ReadCodeWord(image, ui_hook) != SshdMapUiHookInstruction ||
        ReadCodeWord(image, module_start + SshdMapCameraHookOffset) != SshdMapCameraHookInstruction ||
        ReadCodeWord(image, module_start + SshdMapInitializeOffset) != SshdMapInitializeInstruction ||
        !std::all_of(image.begin() + static_cast<ptrdiff_t>(island_cave),
                     image.begin() + static_cast<ptrdiff_t>(island_cave + island_size),
                     [](u8 value) { return value == 0; }) ||
        !std::all_of(image.begin() + static_cast<ptrdiff_t>(bridge_cave),
                     image.begin() + static_cast<ptrdiff_t>(bridge_cave + code_size),
                     [](u8 value) { return value == 0; })) {
        LOG_ERROR(Loader,
                  "[SSHD-MAP] bridge install rejected: signature or empty-page mismatch");
        return false;
    }
    const s64 branch_delta = static_cast<s64>(SshdTelemetryBridgeOffset) -
                             static_cast<s64>(SshdMapCaptureHookOffset);
    if ((branch_delta & 3) != 0 || branch_delta < -(1LL << 27) ||
        branch_delta >= (1LL << 27)) {
        return false;
    }

    const u32 hook_call =
        EncodeRelativeCall(SshdMapCaptureHookOffset, SshdTelemetryBridgeOffset);
    const u32 complete_call =
        EncodeRelativeCall(SshdMapCompleteHookOffset, SshdTelemetryBridgeOffset);
    std::memcpy(image.data() + island_cave, island.data(), island_size);
    std::memcpy(image.data() + bridge_cave, code.data(), code_size);
    std::memcpy(image.data() + hook, &hook_call, sizeof(hook_call));
    std::memcpy(image.data() + complete_hook, &complete_call, sizeof(complete_call));
    const u32 ui_hook_call = EncodeRelativeCall(SshdMapUiHookOffset, SshdTelemetryBridgeOffset);
    std::memcpy(image.data() + ui_hook, &ui_hook_call, sizeof(ui_hook_call));
    const u32 camera_gate = EncodeRelativeCall(SshdMapCameraHookOffset,
        SshdTelemetryBridgeOffset + 5 * 4) & ~0x80000000U;
    std::memcpy(image.data() + module_start + SshdMapCameraHookOffset, &camera_gate, sizeof(camera_gate));
    LOG_INFO(Loader,
             "[SSHD-MAP] installed NCE-safe game-thread capture bridge: version={} bytes={} "
             "mailbox=main+{:#x} island=main+{:#x} bridge=main+{:#x} hook=main+{:#x}",
             SshdCompanionPayload::Version, code_size, mailbox_main_offset,
             SshdTelemetryBridgeOffset, bridge_main_offset, SshdMapCaptureHookOffset);
    return true;
}

// No native calls or game-state writes in this trampoline. Preserve all registers except w8
// (the displaced load's result), NZCV, and SP. The original cbnz performs the transition through
// CarryBox::enterHeld -> Grab::enter -> its native bone-bound held action. Release/drop/cooking
// continue through the unmodified native children. Only the Normal action reaches this hook.
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

bool InstallXcxCompanionActionHook(std::vector<u8>& image, size_t module_start,
                                   u32 mailbox_main_offset, u32 bridge_main_offset,
                                   u32 bridge_size) {
    using Payload = XcxCompanionPayload::Mailbox;
    constexpr u32 SavedStackSize = 0x530;
    constexpr u32 BattleBufferOffset = 0x120;
    constexpr u32 BattleCountOffset = BattleBufferOffset + 0x400;

    std::vector<u32> code;
    code.reserve(192);
    const auto emit = [&](u32 instruction) {
        const size_t index = code.size();
        code.push_back(instruction);
        return index;
    };
    const auto emit_address = [&](u32 target_register, u32 target_offset) {
        const auto instruction_offset =
            bridge_main_offset + static_cast<u32>(code.size() * sizeof(u32));
        emit(EncodeAdrp(target_register, instruction_offset, target_offset));
        emit(EncodeAddPageOffset(target_register, target_offset));
    };
    const auto emit_load_address = [&](u32 target_register, u32 target_offset) {
        const auto instruction_offset =
            bridge_main_offset + static_cast<u32>(code.size() * sizeof(u32));
        emit(EncodeAdrp(target_register, instruction_offset, target_offset));
        emit(EncodeLoadUnsigned64(target_register, target_register, target_offset & 0xFFFU));
    };
    const auto emit_call = [&](u32 target_offset) {
        const auto instruction_offset =
            bridge_main_offset + static_cast<u32>(code.size() * sizeof(u32));
        emit(EncodeRelativeCall(instruction_offset, target_offset));
    };
    const auto emit_store_pair64 = [&](u32 first, u32 second, u32 offset) {
        emit(0xA9000000U | (((offset / 8U) & 0x7FU) << 15) |
             ((second & 0x1FU) << 10) | (31U << 5) | (first & 0x1FU));
    };
    const auto emit_load_pair64 = [&](u32 first, u32 second, u32 offset) {
        emit(0xA9400000U | (((offset / 8U) & 0x7FU) << 15) |
             ((second & 0x1FU) << 10) | (31U << 5) | (first & 0x1FU));
    };
    const auto emit_store_pair128 = [&](u32 first, u32 second, u32 offset) {
        emit(0xAD000000U | (((offset / 16U) & 0x7FU) << 15) |
             ((second & 0x1FU) << 10) | (31U << 5) | (first & 0x1FU));
    };
    const auto emit_load_pair128 = [&](u32 first, u32 second, u32 offset) {
        emit(0xAD400000U | (((offset / 16U) & 0x7FU) << 15) |
             ((second & 0x1FU) << 10) | (31U << 5) | (first & 0x1FU));
    };
    const auto emit_move_wide = [&](u32 target_register, u16 value, bool high, bool keep) {
        const u32 base = keep ? 0x72800000U : 0x52800000U;
        emit(base | (high ? 0x00200000U : 0U) | (static_cast<u32>(value) << 5) |
             (target_register & 0x1FU));
    };

    // Replay the exact displaced party-manager prologue before doing any work.
    emit(XcxPartyManagerUpdateHookInstruction);
    emit_address(15, mailbox_main_offset);
    emit_move_wide(10, static_cast<u16>(XcxCompanionPayload::Magic & 0xFFFFU), false, false);
    emit_move_wide(10, static_cast<u16>(XcxCompanionPayload::Magic >> 16), true, true);
    emit(EncodeStoreUnsigned32(10, 15, offsetof(Payload, magic)));
    emit_move_wide(10, XcxCompanionPayload::Version, false, false);
    emit(EncodeStoreUnsigned16(10, 15, offsetof(Payload, version)));
    emit_move_wide(10, static_cast<u16>(sizeof(Payload)), false, false);
    emit(EncodeStoreUnsigned16(10, 15, offsetof(Payload, size)));
    emit(EncodeAddImmediate64(9, 15, offsetof(Payload, state)));
    emit(0x88DFFC00U | (9U << 5) | 10U); // ldar w10, [x9]
    emit(EncodeCompareImmediate32(10, static_cast<u32>(XcxCompanionPayload::State::Queued)));
    const auto branch_not_queued = emit(0);

    emit(0xD1000000U | (SavedStackSize << 10) | (31U << 5) | 31U);
    emit_store_pair64(0, 1, 0x00);
    emit_store_pair64(2, 3, 0x10);
    emit_store_pair64(4, 5, 0x20);
    emit_store_pair64(6, 7, 0x30);
    emit_store_pair64(8, 9, 0x40);
    emit_store_pair64(10, 11, 0x50);
    emit_store_pair64(12, 13, 0x60);
    emit_store_pair64(14, 15, 0x70);
    emit_store_pair64(16, 17, 0x80);
    emit_store_pair64(18, 30, 0x90);
    emit_store_pair128(0, 1, 0xA0);
    emit_store_pair128(2, 3, 0xC0);
    emit_store_pair128(4, 5, 0xE0);
    emit_store_pair128(6, 7, 0x100);

    emit_move_wide(10, static_cast<u16>(XcxCompanionPayload::State::Running), false, false);
    emit(0x889FFC00U | (9U << 5) | 10U); // stlr w10, [x9]

    // These checks run on the game thread immediately before dispatch. They close the race
    // between the host snapshot and native execution during battles, cutscenes, and menus.
    emit_call(XcxIsEventSceneOffset);
    const auto branch_event_scene = emit(0);
    emit_load_address(8, XcxPauseManagerGotOffset);
    const auto branch_no_pause_root = emit(0);
    emit(EncodeLoadUnsigned64(8, 8, 0));
    const auto branch_no_pause_manager = emit(0);
    emit(EncodeLoadUnsigned8(9, 8, 0xB0));
    const auto branch_paused = emit(0);

    emit_load_address(0, XcxBattleRegistryOffset);
    const auto branch_no_battle_registry = emit(0);
    emit(EncodeAddImmediate64(1, 31, BattleBufferOffset));
    emit_move_wide(2, 0x20, false, false);
    emit_call(XcxCollectBattleActorsOffset);
    emit(EncodeLoadUnsigned64(9, 31, BattleCountOffset));
    const auto branch_in_combat = emit(0);

    emit_address(15, mailbox_main_offset);
    emit(EncodeLoadUnsigned32(10, 15, offsetof(Payload, action)));
    emit(EncodeCompareImmediate32(
        10, static_cast<u32>(XcxCompanionPayload::Action::SwitchCharacter)));
    const auto branch_switch = emit(0);
    emit(EncodeCompareImmediate32(10,
                                  static_cast<u32>(XcxCompanionPayload::Action::FastTravel)));
    const auto branch_fast_travel = emit(0);
    const auto reject_invalid_action = code.size();
    emit_move_wide(11, static_cast<u16>(XcxCompanionPayload::Result::InvalidAction), false,
                   false);
    const auto branch_invalid_action = emit(0);

    const auto switch_character = code.size();
    emit(EncodeLoadUnsigned32(1, 15, offsetof(Payload, argument)));
    emit(EncodeCompareImmediate32(1, 1));
    const auto branch_character_below_range = emit(0);
    emit(EncodeCompareImmediate32(1, 23));
    const auto branch_character_above_range = emit(0);
    emit_address(8, XcxSelectedActorRootOffset);
    emit(EncodeLoadUnsigned64(8, 8, 0));
    const auto branch_no_selected_actor_owner = emit(0);
    emit(EncodeLoadUnsigned64(8, 8, 0x630));
    const auto branch_no_selected_actor = emit(0);
    emit(EncodeLoadUnsigned8(9, 8, 0xB4));
    emit(EncodeCompareImmediate32(9, 2));
    const auto branch_in_skell = emit(0);
    emit(EncodeLoadUnsigned64(0, 31, 0));
    const auto branch_no_party_manager = emit(0);
    emit_call(XcxQueueCharacterSwitchOffset);
    const auto branch_switch_rejected = emit(0);
    emit(0x2A0003ECU); // mov w12, w0
    const auto branch_switch_success = emit(0);

    const auto fast_travel = code.size();
    emit(EncodeLoadUnsigned32(0, 15, offsetof(Payload, argument)));
    const auto branch_zero_formation = emit(0);
    emit(EncodeCompareImmediate32(0, 2048));
    const auto branch_formation_above_range = emit(0);
    emit_move_wide(1, 0, false, false);
    emit_call(XcxMapJumpOffset);
    emit_move_wide(12, 1, false, false);

    const auto publish_success = code.size();
    emit_address(15, mailbox_main_offset);
    emit(EncodeStoreUnsigned32(12, 15, offsetof(Payload, native_result)));
    emit_move_wide(11, static_cast<u16>(XcxCompanionPayload::Result::Success), false, false);
    emit(EncodeStoreUnsigned32(11, 15, offsetof(Payload, result)));
    emit(EncodeLoadUnsigned64(11, 15, offsetof(Payload, request)));
    emit(EncodeStoreUnsigned64(11, 15, offsetof(Payload, completed)));
    emit(EncodeStoreUnsigned64(31, 15, offsetof(Payload, request)));
    emit(EncodeAddImmediate64(9, 15, offsetof(Payload, state)));
    emit_move_wide(10, static_cast<u16>(XcxCompanionPayload::State::Succeeded), false, false);
    emit(0x889FFC00U | (9U << 5) | 10U); // stlr w10, [x9]
    const auto branch_success_restore = emit(0);

    const auto reject_invalid_argument = code.size();
    emit_move_wide(11, static_cast<u16>(XcxCompanionPayload::Result::InvalidArgument), false,
                   false);
    const auto branch_invalid_argument = emit(0);
    const auto reject_event_scene = code.size();
    emit_move_wide(11, static_cast<u16>(XcxCompanionPayload::Result::EventScene), false, false);
    const auto branch_event_reject = emit(0);
    const auto reject_paused = code.size();
    emit_move_wide(11, static_cast<u16>(XcxCompanionPayload::Result::PausedOrMenu), false,
                   false);
    const auto branch_paused_reject = emit(0);
    const auto reject_missing_context = code.size();
    emit_move_wide(11, static_cast<u16>(XcxCompanionPayload::Result::MissingNativeContext),
                   false, false);
    const auto branch_context_reject = emit(0);
    const auto reject_native = code.size();
    emit_move_wide(11, static_cast<u16>(XcxCompanionPayload::Result::NativeRejected), false,
                   false);
    const auto branch_native_reject = emit(0);
    const auto reject_combat = code.size();
    emit_move_wide(11, static_cast<u16>(XcxCompanionPayload::Result::Combat), false, false);
    const auto branch_combat_reject = emit(0);
    const auto reject_in_skell = code.size();
    emit_move_wide(11, static_cast<u16>(XcxCompanionPayload::Result::InSkell), false, false);

    const auto publish_rejection = code.size();
    emit_address(15, mailbox_main_offset);
    emit(EncodeStoreUnsigned32(31, 15, offsetof(Payload, native_result)));
    emit(EncodeStoreUnsigned32(11, 15, offsetof(Payload, result)));
    emit(EncodeLoadUnsigned64(12, 15, offsetof(Payload, request)));
    emit(EncodeStoreUnsigned64(12, 15, offsetof(Payload, completed)));
    emit(EncodeStoreUnsigned64(31, 15, offsetof(Payload, request)));
    emit(EncodeAddImmediate64(9, 15, offsetof(Payload, state)));
    emit_move_wide(10, static_cast<u16>(XcxCompanionPayload::State::Rejected), false, false);
    emit(0x889FFC00U | (9U << 5) | 10U); // stlr w10, [x9]

    const auto restore = code.size();
    emit_load_pair128(6, 7, 0x100);
    emit_load_pair128(4, 5, 0xE0);
    emit_load_pair128(2, 3, 0xC0);
    emit_load_pair128(0, 1, 0xA0);
    emit_load_pair64(18, 30, 0x90);
    emit_load_pair64(16, 17, 0x80);
    emit_load_pair64(14, 15, 0x70);
    emit_load_pair64(12, 13, 0x60);
    emit_load_pair64(10, 11, 0x50);
    emit_load_pair64(8, 9, 0x40);
    emit_load_pair64(6, 7, 0x30);
    emit_load_pair64(4, 5, 0x20);
    emit_load_pair64(2, 3, 0x10);
    emit_load_pair64(0, 1, 0x00);
    emit(0x91000000U | (SavedStackSize << 10) | (31U << 5) | 31U);

    const auto passthrough = code.size();
    const auto return_offset =
        bridge_main_offset + static_cast<u32>(code.size() * sizeof(u32));
    emit(EncodeRelativeBranch(return_offset,
                              XcxPartyManagerUpdateHookOffset + sizeof(u32)));

    code[branch_not_queued] = EncodeConditionalBranch(1, branch_not_queued, passthrough);
    code[branch_event_scene] =
        EncodeCompareBranchZero(false, 0, branch_event_scene, reject_event_scene) |
        0x01000000U;
    code[branch_no_pause_root] =
        EncodeCompareBranchZero(true, 8, branch_no_pause_root, reject_missing_context);
    code[branch_no_pause_manager] =
        EncodeCompareBranchZero(true, 8, branch_no_pause_manager, reject_missing_context);
    code[branch_paused] = EncodeCompareBranchZero(false, 9, branch_paused, reject_paused) |
                          0x01000000U;
    code[branch_no_battle_registry] = EncodeCompareBranchZero(
        true, 0, branch_no_battle_registry, reject_missing_context);
    code[branch_in_combat] =
        EncodeCompareBranchZero(true, 9, branch_in_combat, reject_combat) | 0x01000000U;
    code[branch_switch] = EncodeConditionalBranch(0, branch_switch, switch_character);
    code[branch_fast_travel] = EncodeConditionalBranch(0, branch_fast_travel, fast_travel);
    code[branch_invalid_action] =
        EncodeLocalBranch(branch_invalid_action, publish_rejection);
    code[branch_character_below_range] =
        EncodeConditionalBranch(3, branch_character_below_range, reject_invalid_argument);
    code[branch_character_above_range] =
        EncodeConditionalBranch(8, branch_character_above_range, reject_invalid_argument);
    code[branch_no_selected_actor_owner] = EncodeCompareBranchZero(
        true, 8, branch_no_selected_actor_owner, reject_missing_context);
    code[branch_no_selected_actor] = EncodeCompareBranchZero(
        true, 8, branch_no_selected_actor, reject_missing_context);
    code[branch_in_skell] = EncodeConditionalBranch(0, branch_in_skell, reject_in_skell);
    code[branch_no_party_manager] = EncodeCompareBranchZero(
        true, 0, branch_no_party_manager, reject_missing_context);
    code[branch_switch_rejected] = EncodeCompareBranchZero(
        false, 0, branch_switch_rejected, reject_native);
    code[branch_switch_success] =
        EncodeLocalBranch(branch_switch_success, publish_success);
    code[branch_zero_formation] = EncodeCompareBranchZero(
        false, 0, branch_zero_formation, reject_invalid_argument);
    code[branch_formation_above_range] =
        EncodeConditionalBranch(2, branch_formation_above_range, reject_invalid_argument);
    code[branch_success_restore] = EncodeLocalBranch(branch_success_restore, restore);
    code[branch_invalid_argument] =
        EncodeLocalBranch(branch_invalid_argument, publish_rejection);
    code[branch_event_reject] = EncodeLocalBranch(branch_event_reject, publish_rejection);
    code[branch_paused_reject] = EncodeLocalBranch(branch_paused_reject, publish_rejection);
    code[branch_context_reject] = EncodeLocalBranch(branch_context_reject, publish_rejection);
    code[branch_native_reject] = EncodeLocalBranch(branch_native_reject, publish_rejection);
    code[branch_combat_reject] = EncodeLocalBranch(branch_combat_reject, publish_rejection);

    if ((mailbox_main_offset & 0xFFFU) != 0 || (bridge_main_offset & 0xFFFU) != 0 ||
        bridge_size != XcxCompanionBridgePageSize ||
        bridge_main_offset != mailbox_main_offset + XcxCompanionMailboxPageSize) {
        LOG_ERROR(Loader, "[XCX-ACTION] bridge install rejected: invalid RW/RX layout");
        return false;
    }
    const size_t code_size = code.size() * sizeof(u32);
    const size_t hook = module_start + XcxPartyManagerUpdateHookOffset;
    const size_t switch_dispatch = module_start + XcxQueueCharacterSwitchOffset;
    const size_t map_jump = module_start + XcxMapJumpOffset;
    const size_t event_check = module_start + XcxIsEventSceneOffset;
    const size_t battle_collect = module_start + XcxCollectBattleActorsOffset;
    const size_t cave = module_start + bridge_main_offset;
    if (hook + sizeof(u32) > image.size() || switch_dispatch + sizeof(u32) > image.size() ||
        map_jump + sizeof(u32) > image.size() || event_check + sizeof(u32) > image.size() ||
        battle_collect + sizeof(u32) > image.size() || cave > image.size() ||
        code_size > image.size() - cave || code_size > bridge_size) {
        LOG_ERROR(Loader, "[XCX-ACTION] bridge install rejected: code range is outside the image");
        return false;
    }
    if (ReadCodeWord(image, hook) != XcxPartyManagerUpdateHookInstruction ||
        ReadCodeWord(image, switch_dispatch) != 0xB9401008U ||
        ReadCodeWord(image, map_jump) != 0xD101C3FFU ||
        ReadCodeWord(image, event_check) != 0xA9BF7BFDU ||
        ReadCodeWord(image, battle_collect) != 0xAA0003E8U) {
        LOG_ERROR(Loader,
                  "[XCX-ACTION] bridge install rejected: exact native signature mismatch");
        return false;
    }
    const auto cave_mismatch = std::find_if(
        image.begin() + static_cast<ptrdiff_t>(cave),
        image.begin() + static_cast<ptrdiff_t>(cave + code_size),
        [](u8 value) { return value != 0; });
    if (cave_mismatch != image.begin() + static_cast<ptrdiff_t>(cave + code_size)) {
        LOG_ERROR(Loader, "[XCX-ACTION] bridge install rejected: executable page is not empty");
        return false;
    }
    const s64 hook_delta = static_cast<s64>(bridge_main_offset) -
                           static_cast<s64>(XcxPartyManagerUpdateHookOffset);
    if ((hook_delta & 3) != 0 || hook_delta < -(1LL << 27) || hook_delta >= (1LL << 27)) {
        LOG_ERROR(Loader, "[XCX-ACTION] bridge install rejected: branch target is out of range");
        return false;
    }
    const u32 hook_branch =
        EncodeRelativeBranch(XcxPartyManagerUpdateHookOffset, bridge_main_offset);
    std::memcpy(image.data() + cave, code.data(), code_size);
    std::memcpy(image.data() + hook, &hook_branch, sizeof(hook_branch));
    LOG_INFO(Loader,
             "[XCX-ACTION] installed native party/map action bridge: {} bytes, "
             "mailbox=main+{:#x}, bridge=main+{:#x}, hook=main+{:#x}",
             code_size, mailbox_main_offset, bridge_main_offset,
             XcxPartyManagerUpdateHookOffset);
    return true;
}


bool InstallMk8dCompanionItemHook(std::vector<u8>& image, size_t module_start,
                                  u32 mailbox_main_offset) {
    std::vector<u32> code;
    code.reserve(96);
    const auto emit = [&](u32 instruction) {
        const size_t index = code.size();
        code.push_back(instruction);
        return index;
    };

    emit(EncodeAdrp(9, Mk8dItemBridgeOffset, mailbox_main_offset));
    emit(EncodeAddPageOffset(9, mailbox_main_offset));
    emit(0xB940112AU);
    emit(0x7100055FU);
    const auto branch_not_queued = emit(0);
    emit(0xF940268AU);
    const auto branch_no_proxy = emit(0);
    emit(0xF940254AU);
    const auto branch_no_vehicle = emit(0);
    emit(0x3943414BU);
    const auto branch_not_local = emit(0);
    emit(0xD5033BBFU);
    emit(0xF940012BU);
    const auto branch_empty_request = emit(0);
    emit(0xF100097FU);
    const auto branch_bad_slot = emit(0);
    emit(0x5280004AU);
    emit(0xB900112AU);
    emit(0x51000575U);
    emit(0x8B150E8AU);
    emit(0xF940314AU);
    const auto branch_null_slot = emit(0);
    emit(0xF9002E8AU);
    emit(0x1280000AU);
    emit(0xB900728AU);
    emit(0x52800038U);
    emit(0xF900052BU);
    emit(0xF900013FU);
    emit(0x5280006AU);
    emit(0xD5033BBFU);
    emit(0xB900112AU);
    const auto finish = code.size();
    emit(Mk8dItemInputHookInstruction);
    emit(0xD65F03C0U);
    const auto reject = code.size();
    emit(0xF900052BU);
    emit(0xF900013FU);
    emit(0x5280008AU);
    emit(0xD5033BBFU);
    emit(0xB900112AU);
    const auto branch_finish = emit(0);

    code[branch_not_queued] = EncodeConditionalBranch(1, branch_not_queued, finish);
    code[branch_no_proxy] = EncodeCompareBranchZero(true, 10, branch_no_proxy, finish);
    code[branch_no_vehicle] =
        EncodeCompareBranchZero(true, 10, branch_no_vehicle, finish);
    code[branch_not_local] =
        EncodeCompareBranchZero(false, 11, branch_not_local, finish);
    code[branch_empty_request] =
        EncodeCompareBranchZero(true, 11, branch_empty_request, reject);
    code[branch_bad_slot] = EncodeConditionalBranch(8, branch_bad_slot, reject);
    code[branch_null_slot] =
        EncodeCompareBranchZero(true, 10, branch_null_slot, reject);
    code[branch_finish] = EncodeLocalBranch(branch_finish, finish);

    const size_t code_size = code.size() * sizeof(u32);
    static_assert((Mk8dItemBridgeOffset & 3U) == 0);
    static_assert(Mk8dItemBridgeOffset >= Mk8dTextFileSize);
    if (code_size > Mk8dItemBridgeSize || module_start > image.size() ||
        Mk8dItemInputHookOffset > image.size() - module_start ||
        Mk8dItemBridgeOffset > image.size() - module_start) {
        return false;
    }
    const size_t item_hook = module_start + Mk8dItemInputHookOffset;
    const size_t cave = module_start + Mk8dItemBridgeOffset;
    if (sizeof(u32) > image.size() - item_hook ||
        code_size > image.size() - cave ||
        ReadCodeWord(image, item_hook) != Mk8dItemInputHookInstruction ||
        !std::all_of(image.begin() + static_cast<ptrdiff_t>(cave),
                     image.begin() + static_cast<ptrdiff_t>(cave + code_size),
                     [](u8 value) { return value == 0; })) {
        return false;
    }

    const u32 item_call = EncodeRelativeCall(Mk8dItemInputHookOffset, Mk8dItemBridgeOffset);
    std::memcpy(image.data() + cave, code.data(), code_size);
    std::memcpy(image.data() + item_hook, &item_call, sizeof(item_call));
    return true;
}

#endif

} // Anonymous namespace

bool NSOHeader::IsSegmentCompressed(size_t segment_num) const {
    ASSERT_MSG(segment_num < 3, "Invalid segment {}", segment_num);
    return ((flags >> segment_num) & 1) != 0;
}

AppLoader_NSO::AppLoader_NSO(FileSys::VirtualFile file_) : AppLoader(std::move(file_)) {}

FileType AppLoader_NSO::IdentifyType(const FileSys::VirtualFile& in_file) {
    u32 magic = 0;
    if (in_file->ReadObject(&magic) != sizeof(magic)) {
        return FileType::Error;
    }

    if (Common::MakeMagic('N', 'S', 'O', '0') != magic) {
        return FileType::Error;
    }

    return FileType::NSO;
}

std::optional<VAddr> AppLoader_NSO::LoadModule(Kernel::KProcess& process, Core::System& system, const FileSys::VfsFile& nso_file, VAddr load_base, bool should_pass_arguments, bool load_into_process, std::optional<FileSys::PatchManager> pm, std::vector<Core::NCE::Patcher>* patches, s32 patch_index) {
    if (nso_file.GetSize() < sizeof(NSOHeader))
        return std::nullopt;
    NSOHeader nso_header{};
    if (sizeof(NSOHeader) != nso_file.ReadObject(&nso_header))
        return std::nullopt;
    if (nso_header.magic != Common::MakeMagic('N', 'S', 'O', '0'))
        return std::nullopt;
    if (nso_header.segments.empty())
        return std::nullopt;

    // Allocate some space at the beginning if we are patching in PreText mode.
    const size_t module_start = [&]() -> size_t {
#ifdef HAS_NCE
        if (patches && load_into_process) {
            auto* patch = &patches->operator[](patch_index);
            if (patch->GetPatchMode() == Core::NCE::PatchMode::PreText) {
                return patch->GetSectionSize();
            } else if (patch->GetPatchMode() == Core::NCE::PatchMode::Split) {
                return patch->GetPreSectionSize();
            }
        }
#endif
        return 0;
    }();

    auto const last_segment_it = &nso_header.segments[nso_header.segments.size() - 1];
    // Build program image directly in codeset memory :)
    Kernel::CodeSet codeset;
    codeset.memory.resize(module_start + last_segment_it->location + last_segment_it->size);
    {
        std::vector<u8> compressed_data(*std::ranges::max_element(nso_header.segments_compressed_size));
        std::vector<u8> decompressed_size(std::ranges::max_element(nso_header.segments, [](auto const& a, auto const& b) {
            return a.size < b.size;
        })->size);
        for (std::size_t i = 0; i < nso_header.segments.size(); ++i) {
            nso_file.Read(compressed_data.data(), nso_header.segments_compressed_size[i], nso_header.segments[i].offset);
            if (nso_header.IsSegmentCompressed(i)) {
                int r = Common::Compression::DecompressDataLZ4(decompressed_size.data(), nso_header.segments[i].size, compressed_data.data(), nso_header.segments_compressed_size[i]);
                ASSERT(r == int(nso_header.segments[i].size));
                std::memcpy(codeset.memory.data() + module_start + nso_header.segments[i].location, decompressed_size.data(), nso_header.segments[i].size);
            } else {
                std::memcpy(codeset.memory.data() + module_start + nso_header.segments[i].location, compressed_data.data(), nso_header.segments[i].size);
            }
            codeset.segments[i].addr = module_start + nso_header.segments[i].location;
            codeset.segments[i].offset = module_start + nso_header.segments[i].location;
            codeset.segments[i].size = nso_header.segments[i].size;
        }
    }

    if (should_pass_arguments && !Settings::values.program_args.GetValue().empty()) {
        const auto arg_data{Settings::values.program_args.GetValue()};

        codeset.DataSegment().size += NSO_ARGUMENT_DATA_ALLOCATION_SIZE;
        NSOArgumentHeader args_header{NSO_ARGUMENT_DATA_ALLOCATION_SIZE, static_cast<u32_le>(arg_data.size()), {}};
        const auto end_offset = codeset.memory.size();
        codeset.memory.resize(u32(codeset.memory.size()) + NSO_ARGUMENT_DATA_ALLOCATION_SIZE);
        std::memcpy(codeset.memory.data() + end_offset, &args_header, sizeof(NSOArgumentHeader));
        std::memcpy(codeset.memory.data() + end_offset + sizeof(NSOArgumentHeader), arg_data.data(), arg_data.size());
    }

    codeset.DataSegment().size += nso_header.segments[2].bss_size;
    u32 image_size = PageAlignSize(u32(codeset.memory.size()) + nso_header.segments[2].bss_size);
    codeset.memory.resize(image_size);

    for (std::size_t i = 0; i < nso_header.segments.size(); ++i) {
        codeset.segments[i].size = PageAlignSize(codeset.segments[i].size);
    }

    // Apply patches if necessary
    const auto name = nso_file.GetName();
    if (pm && (pm->HasNSOPatch(nso_header.build_id, name) || Settings::values.dump_nso)) {
        std::span<u8> patchable_section(codeset.memory.data() + module_start, codeset.memory.size() - module_start);
        std::vector<u8> pi_header(sizeof(NSOHeader) + patchable_section.size());
        std::memcpy(pi_header.data(), &nso_header, sizeof(NSOHeader));
        std::memcpy(pi_header.data() + sizeof(NSOHeader), patchable_section.data(),
                    patchable_section.size());

        pi_header = pm->PatchNSO(pi_header, name);

        std::copy(pi_header.begin() + sizeof(NSOHeader), pi_header.end(), patchable_section.data());
    }

    u64 companion_mailbox_address{};
#if defined(BOTW_DUALSCREEN_COMPANION)
    const auto companion_memory =
        ReserveCompanionMailbox(nso_header, name, codeset, image_size);
    bool companion_hook_installed{};
    if (companion_memory) {


        if (module_start <= companion_memory->mailbox_offset &&
            companion_memory->mailbox_offset - module_start <= std::numeric_limits<u32>::max()) {
            const auto mailbox_main_offset =
                static_cast<u32>(companion_memory->mailbox_offset - module_start);
            if (IsExactMk8dCompanionMain(nso_header, name)) {
                companion_hook_installed = InstallMk8dCompanionItemHook(
                    codeset.memory, module_start, mailbox_main_offset);
            } else if (IsExactSmoCompanionMain(nso_header, name)) {
                if (module_start <= companion_memory->bridge_offset &&
                    companion_memory->bridge_offset - module_start <= std::numeric_limits<u32>::max()) {
                    companion_hook_installed = InstallSmoOutfitPayload(
                        codeset.memory, module_start, mailbox_main_offset,
                        static_cast<u32>(companion_memory->bridge_offset - module_start),
                        companion_memory->bridge_size);
                }
            } else if (IsExactXcxCompanionMain(nso_header, name)) {
                if (module_start <= companion_memory->bridge_offset &&
                    companion_memory->bridge_offset - module_start <=
                        std::numeric_limits<u32>::max()) {
                    companion_hook_installed = InstallXcxCompanionActionHook(
                        codeset.memory, module_start, mailbox_main_offset,
                        static_cast<u32>(companion_memory->bridge_offset - module_start),
                        companion_memory->bridge_size);
                }
            } else if (IsExactSshdCompanionMain(nso_header, name)) {
                if (module_start <= companion_memory->bridge_offset &&
                    companion_memory->bridge_offset - module_start <=
                        std::numeric_limits<u32>::max()) {
                    companion_hook_installed = InstallSshdCompanionTelemetryPayload(
                        codeset.memory, module_start, mailbox_main_offset,
                        static_cast<u32>(companion_memory->bridge_offset - module_start),
                        companion_memory->bridge_size);
                }
            } else if (companion_memory->external) {
                if (module_start <= companion_memory->bridge_offset &&
                    companion_memory->bridge_offset - module_start <=
                        std::numeric_limits<u32>::max()) {
                    companion_hook_installed = ExternalCompanion::Install(
                        codeset.memory, module_start, mailbox_main_offset,
                        static_cast<u32>(companion_memory->bridge_offset - module_start),
                        companion_memory->bridge_size);
                }
            } else if (EnableBotwCompanion && IsExactBotwCompanionMain(nso_header, name)) {
                if (module_start <= companion_memory->bridge_offset &&
                    companion_memory->bridge_offset - module_start <=
                        std::numeric_limits<u32>::max()) {
                    companion_hook_installed = InstallBotwCompanionEquipHook(
                        codeset.memory, module_start, mailbox_main_offset,
                        static_cast<u32>(companion_memory->bridge_offset - module_start),
                        companion_memory->bridge_size);
                }
            }
        }
        if (load_into_process && companion_hook_installed && pm &&
            (pm->GetTitleID() == Mk8dCompanionTitleId ||
             pm->GetTitleID() == ExternalCompanion::TitleId() ||
             pm->GetTitleID() == XcxCompanionPayload::TitleId ||
             pm->GetTitleID() == SshdCompanionPayload::TitleId ||
             pm->GetTitleID() == SmoOutfit::TitleId ||
             (EnableBotwCompanion && pm->GetTitleID() == BotwCompanionTitleId))) {
            companion_mailbox_address = load_base + companion_memory->mailbox_offset;
        }
        if (load_into_process && !companion_hook_installed) {
            LOG_ERROR(Loader,
                      "Dual-screen companion bridge could not be installed in the load-pass "
                      "main NSO; refusing to run without its validated game-thread bridge");
            return std::nullopt;
        }
    }
#endif

#ifdef HAS_NCE
    // If we are computing the process code layout and using nce backend, patch.
    const auto& code = codeset.CodeSegment();
    auto* patch = patches ? &patches->operator[](patch_index) : nullptr;
    if (patch && !load_into_process) {
        //Set module ID using build_id from the NSO header
        patch->SetModuleID(nso_header.build_id);
        // Patch SVCs and MRS calls in the guest code
        while (!patch->PatchText(codeset.memory, code)) {
            patch = &patches->emplace_back();
            patch->SetModuleID(nso_header.build_id);  // In case the patcher is changed for big modules, the new patcher should also have the build_id
        }
    } else if (patch) {
        // Relocate code patch and copy to the program image.
        // Save size before RelocateAndCopy (which may resize)
        const size_t size_before_relocate = codeset.memory.size();
        if (patch->RelocateAndCopy(load_base, code, codeset.memory, &process.GetPostHandlers())) {
            // Update patch section.
            auto& patch_segment = codeset.PatchSegment();
            auto& post_patch_segment = codeset.PostPatchSegment();
            const auto patch_mode = patch->GetPatchMode();
            if (patch_mode == Core::NCE::PatchMode::PreText) {
                patch_segment.addr = 0;
                patch_segment.size = static_cast<u32>(patch->GetSectionSize());
            } else if (patch_mode == Core::NCE::PatchMode::Split) {
                // For Split-mode, we are using pre-patch buffer at start, post-patch buffer at end
                patch_segment.addr = 0;
                patch_segment.size = static_cast<u32>(patch->GetPreSectionSize());
                post_patch_segment.addr = size_before_relocate;
                post_patch_segment.size = static_cast<u32>(patch->GetSectionSize());
            } else {
                patch_segment.addr = image_size;
                patch_segment.size = static_cast<u32>(patch->GetSectionSize());
            }
        }

        // Refresh image_size to take account the patch section if it was added by RelocateAndCopy
        image_size = static_cast<u32>(codeset.memory.size());
    }
#endif

    // If we aren't actually loading (i.e. just computing the process code layout), we are done
    if (!load_into_process) {
#ifdef HAS_NCE
        // Ok, so for Split mode, we need to account for pre-patch and post-patch space
        // which will be added during RelocateAndCopy in the second pass. Where it crashed
        // in Android Studio at PreText. May be a better way. Works for now.
        if (patch && patch->GetPatchMode() == Core::NCE::PatchMode::Split) {
            return load_base + patch->GetPreSectionSize() + image_size + patch->GetSectionSize();
        } else if (patch && patch->GetPatchMode() == Core::NCE::PatchMode::PreText) {
            return load_base + patch->GetSectionSize() + image_size;
        } else if (patch && patch->GetPatchMode() == Core::NCE::PatchMode::PostData) {
            return load_base + image_size + patch->GetSectionSize();
        }
#endif
        return load_base + image_size;
    }


    const bool is_application_main = pm && name == "main";
    if (pm) {
        const auto cheats = pm->CreateCheatList(nso_header.build_id);
        if (!cheats.empty()) {
            system.RegisterCheatList(cheats, nso_header.build_id, load_base, image_size);
        }
    }

    // Load codeset for current process
    process.LoadModule(system.Kernel(), std::move(codeset), load_base);
    if (is_application_main) {


        system.SetApplicationProcessBuildID(nso_header.build_id);
        system.SetApplicationProcessMainModule(load_base + module_start, image_size - module_start);
        system.SetApplicationProcessCompanionMailbox(companion_mailbox_address);
    }
    return load_base + image_size;
}

AppLoader_NSO::LoadResult AppLoader_NSO::Load(Kernel::KProcess& process, Core::System& system) {
    if (is_loaded) {
        return {ResultStatus::ErrorAlreadyLoaded, {}};
    }

    modules.clear();

    // Load module
    const VAddr base_address = GetInteger(process.GetEntryPoint());
    if (!LoadModule(process, system, *file, base_address, true, true)) {
        return {ResultStatus::ErrorLoadingNSO, {}};
    }

    modules.insert_or_assign(base_address, file->GetName());
    LOG_DEBUG(Loader, "loaded module {} @ {:#x}", file->GetName(), base_address);

    is_loaded = true;
    return {ResultStatus::Success, LoadParameters{Kernel::KThread::DefaultThreadPriority,
                                                  Core::Memory::DEFAULT_STACK_SIZE}};
}

ResultStatus AppLoader_NSO::ReadNSOModules(Modules& out_modules) {
    out_modules = this->modules;
    return ResultStatus::Success;
}

} // namespace Loader
