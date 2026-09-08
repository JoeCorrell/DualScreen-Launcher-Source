#pragma once
#include <cstdint>

namespace dishonored {
// Addresses are RVAs for the SHA-256 below, recovered from the installed PE.
// No compatibility with another executable or DLC player class is implied.
constexpr char ExecutableSha256[] = "67ced86b1675b8d27c742a84be41eaa7eba41c748a7f4ec1fe05d107d4f91ae5";
constexpr uint32_t ExecutableBytes = 18008064;
constexpr uint32_t Timestamp = 0x5212f0d7;
constexpr uint32_t ImageSize = 0x011fd000;
constexpr uintptr_t ControllerVtable = 0x00d0db58;
constexpr uintptr_t ControllerTick = 0x006b6ab0;
constexpr uintptr_t ControllerGlobal = 0x01052dd4;
constexpr uintptr_t PawnGlobal = 0x01052de8;
constexpr uintptr_t PawnClassGlobal = 0x01052de0;
constexpr uintptr_t PawnVtable = 0x00d0f7d0;
constexpr uintptr_t TickSlot = 0x19c;
constexpr uintptr_t ControllerPawn = 0x248;
constexpr uintptr_t ObjectClass = 0x30;
constexpr uintptr_t PawnHealth = 0x344; // OnSetPlayerHealth, 00aa4530.
constexpr uintptr_t PawnHealthMax = 0x348;
constexpr uintptr_t PawnMana = 0xa60; // Native setter, 00ab17c0.
constexpr uintptr_t PawnManaMax = 0xa64; // OnSetPlayerMana, 00aa4580.
constexpr uintptr_t PawnActivePowers = 0xa40; // Dis_SelectPower, 00ab4460.
constexpr uintptr_t PawnCollectedCharms = 0xb38; // Equip/remove, 00ab8f60/00ab5ce0.
constexpr uintptr_t EngineGlobal = 0x0103b20c, EngineVtable = 0x00cd0bf8;
constexpr uintptr_t EngineInputGate = 0x000d8910; // Verified xor eax,eax;ret override.
constexpr uintptr_t SelectPower = 0x006b4460, EquipItem = 0x006b4850;
constexpr uintptr_t EquipCharm = 0x006b8f60, RemoveCharm = 0x006b5ce0;
constexpr uintptr_t PawnTeardownSlot = 0x728, PawnTeardown = 0x006b5100;
constexpr uintptr_t PawnRestoreSlot = 0x118, PawnRestore = 0x006b8790;
constexpr uintptr_t AmmoOptionRequest = 0x008190d0, AmmoOptionParameters = 0x00d16220;
constexpr uintptr_t ConsumeElixir = 0x006aea90;
constexpr uintptr_t TaskMarkerVtable = 0x00d58588, TaskMarkerRenderSlot = 0x14;
constexpr uintptr_t TaskMarkerRender = 0x007c5830; // ECX receiver, six 32-bit stack arguments, ret18.
constexpr uintptr_t NoteVtable = 0x00cd7b48, NoteInteractSlot = 0x3c0;
constexpr uintptr_t NoteInteract = 0x00636050; // ECX note, one pawn stack argument, EAX result, ret4.
constexpr uintptr_t ViewportWindowProcedure = 0x005ca440; // WNDCLASSEX.lpfnWndProc set at 009ca746; registered at 009ca7a3.
}
