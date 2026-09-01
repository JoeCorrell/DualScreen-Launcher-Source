#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace fo4ds {

// Exact addresses for the signed Fallout4.exe 1.11.240 image accepted by
// IsNativeActionImageHash().  Keep them out of generic protocol/UI code.
namespace native_action_rva {
inline constexpr uintptr_t kMessageQueueProcessTask = 0x00C45150;
inline constexpr uintptr_t kUseInventoryItem = 0x00AE0CF0;
inline constexpr uintptr_t kInventoryVersion = 0x00B41340;
inline constexpr uintptr_t kHandleValid = 0x00347DC0;
inline constexpr uintptr_t kResolveInventoryHandle = 0x00347C10;
inline constexpr uintptr_t kInventoryStackCount = 0x0034BB60;
inline constexpr uintptr_t kResolveQuest = 0x00B60430;
inline constexpr uintptr_t kToggleQuestActive = 0x00B707A0;
inline constexpr uintptr_t kQuestActive = 0x0066ADE0;
inline constexpr uintptr_t kFindRadioStation = 0x00B709F0;
inline constexpr uintptr_t kEnablePlayerRadio = 0x00BDAE00;
// Exact call made after writing PipboyManager+0x1AC; the recovered binary does
// not provide a trustworthy source-level name for this PlayerCharacter method.
inline constexpr uintptr_t kPlayerRadioUpdate = 0x00D773C0;

inline constexpr uintptr_t kPipboyDataManagerPointer = 0x030E8AF8;
inline constexpr uintptr_t kInventoryInterfacePointer = 0x030EC470;
inline constexpr uintptr_t kPipboyManagerPointer = 0x031F0888;
inline constexpr uintptr_t kPlayerCharacterPointer = 0x032DD370;

inline constexpr uintptr_t kInventoryGroupOffset = 0x358;
inline constexpr uintptr_t kRadioGroupOffset = 0xA28;
inline constexpr uintptr_t kInventoryGroupVtable = 0x02544750;
inline constexpr uintptr_t kRadioGroupVtable = 0x025468C8;
inline constexpr uintptr_t kPipboyObjectVtable = 0x02545928;
inline constexpr uintptr_t kPipboyBoolVtable = 0x02544670;
inline constexpr uintptr_t kPipboyFloatVtable = 0x02544718;
}  // namespace native_action_rva

enum class NativeActionKind : uint8_t {
  kUseInventoryItem,
  kToggleQuestActive,
  kToggleRadioStation,
  kRadioOff,
};

struct NativeItemAction {
  uint32_t handle_id = 0;
  uint32_t stack_id = 0;
  uint32_t inventory_version = 0;
};

struct NativeQuestAction {
  uint32_t form_id = 0;
  uint32_t instance = 0;
  uint32_t type = 0;
};

struct NativeRadioAction {
  uint32_t node_id = 0;
};

struct NativeActionRequest {
  uint64_t token = 0;
  NativeActionKind kind = NativeActionKind::kUseInventoryItem;
  NativeItemAction item;
  NativeQuestAction quest;
  NativeRadioAction radio;
};

enum class NativeActionResultCode : uint8_t {
  kSucceeded,
  kStaleInventoryVersion,
  kInvalidInventoryItem,
  kInvalidInventoryStack,
  kPipboyTransitionInProgress,
  kInvalidQuest,
  kStaleQuestInstance,
  kInvalidRadioStation,
  kMalformedRadioStation,
  kRejectedByEngine,
  kEngineUnavailable,
};

struct NativeActionResult {
  uint64_t token = 0;
  NativeActionKind kind = NativeActionKind::kUseInventoryItem;
  NativeActionResultCode code = NativeActionResultCode::kEngineUnavailable;
  // Quest/radio: resulting active state. Item: Fallout's success flag.
  bool state = false;
};

inline constexpr size_t kNativeActionQueueCapacity = 64;

// Accepted requests reserve a result slot until Poll() consumes it, so the
// game-thread producer never allocates and cannot overflow the result ring.
class NativeActionQueue final {
 public:
  bool Enqueue(const NativeActionRequest& request) noexcept;
  bool Take(NativeActionRequest* request) noexcept;
  bool Complete(const NativeActionResult& result) noexcept;
  bool Poll(NativeActionResult* result) noexcept;
  size_t Outstanding() const noexcept;

 private:
  mutable std::mutex mutex_;
  std::array<NativeActionRequest, kNativeActionQueueCapacity> requests_{};
  std::array<NativeActionResult, kNativeActionQueueCapacity> results_{};
  size_t request_head_ = 0;
  size_t request_size_ = 0;
  size_t result_head_ = 0;
  size_t result_size_ = 0;
  size_t in_flight_ = 0;
  size_t outstanding_ = 0;
};

enum class NativeActionInstallStatus : uint8_t {
  kNotInstalled,
  kDisabledByConfiguration,
  kInstalling,
  kReady,
  kUnsupportedImage,
  kPrologueMismatch,
  kInvalidAddress,
  kTrampolineAllocationFailed,
  kThreadTransactionFailed,
  kPatchFailed,
};

bool IsNativeActionImageHash(const char* sha256) noexcept;
bool NativeActionPumpPrologueMatches(const void* bytes, size_t size) noexcept;
bool BuildNativeActionPumpPatch(
    uintptr_t source, uintptr_t relay,
    std::array<uint8_t, 7>* patch) noexcept;
bool RelocateNativeActionPumpPc(uintptr_t patch, uintptr_t trampoline,
                                uintptr_t pc, uintptr_t* relocated) noexcept;
const char* NativeActionInstallStatusLabel(
    NativeActionInstallStatus status) noexcept;
const char* NativeActionResultCodeLabel(NativeActionResultCode code) noexcept;

// Startup-only. Installs the private pump only after all fail-closed gates
// succeed.
NativeActionInstallStatus InstallNativeActions(
    uintptr_t fallout_module, const char* executable_sha256) noexcept;
// Configuration-aware startup entry point. When disabled, this must not touch
// the game image or alter the backend's pristine not-installed state.
NativeActionInstallStatus ConfigureNativeActions(
    bool enabled, uintptr_t fallout_module,
    const char* executable_sha256) noexcept;
NativeActionInstallStatus GetNativeActionInstallStatus() noexcept;
bool NativeActionsReady() noexcept;

bool QueueNativeAction(const NativeActionRequest& request) noexcept;
bool PollNativeActionResult(NativeActionResult* result) noexcept;

}  // namespace fo4ds
