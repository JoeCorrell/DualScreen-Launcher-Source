#include "ActionTransport.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>

namespace fomds {
namespace {

constexpr std::uint64_t kMaxExactJsonInteger = 9007199254740991ULL;
constexpr std::uint64_t kWindowsToUnixEpochMilliseconds = 11644473600000ULL;
std::atomic<std::uint64_t> g_next_request_id{1};

bool IsContinuation(unsigned char value) {
  return (value & 0xc0U) == 0x80U;
}

bool IsValidUtf8(const std::string& value) {
  for (std::size_t index = 0; index < value.size();) {
    const unsigned char first = static_cast<unsigned char>(value[index]);
    if (first <= 0x7fU) {
      ++index;
      continue;
    }
    std::size_t length = 0;
    std::uint32_t code_point = 0;
    if ((first & 0xe0U) == 0xc0U) {
      length = 2;
      code_point = first & 0x1fU;
    } else if ((first & 0xf0U) == 0xe0U) {
      length = 3;
      code_point = first & 0x0fU;
    } else if ((first & 0xf8U) == 0xf0U) {
      length = 4;
      code_point = first & 0x07U;
    } else {
      return false;
    }
    if (index + length > value.size()) return false;
    for (std::size_t offset = 1; offset < length; ++offset) {
      const unsigned char next =
          static_cast<unsigned char>(value[index + offset]);
      if (!IsContinuation(next)) return false;
      code_point = (code_point << 6U) | (next & 0x3fU);
    }
    if ((length == 2 && code_point < 0x80U) ||
        (length == 3 && code_point < 0x800U) ||
        (length == 4 && code_point < 0x10000U) ||
        code_point > 0x10ffffU ||
        (code_point >= 0xd800U && code_point <= 0xdfffU)) {
      return false;
    }
    index += length;
  }
  return true;
}

std::string JsonString(const std::string& value) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string result;
  result.reserve(value.size() + 2);
  result.push_back('"');
  for (const unsigned char character : value) {
    switch (character) {
      case '"': result.append("\\\""); break;
      case '\\': result.append("\\\\"); break;
      case '\b': result.append("\\b"); break;
      case '\f': result.append("\\f"); break;
      case '\n': result.append("\\n"); break;
      case '\r': result.append("\\r"); break;
      case '\t': result.append("\\t"); break;
      default:
        if (character < 0x20U) {
          result.append("\\u00");
          result.push_back(kHex[(character >> 4U) & 0x0fU]);
          result.push_back(kHex[character & 0x0fU]);
        } else {
          result.push_back(static_cast<char>(character));
        }
        break;
    }
  }
  result.push_back('"');
  return result;
}

std::uint64_t NewSequenceSeed() {
  FILETIME time = {};
  GetSystemTimeAsFileTime(&time);
  ULARGE_INTEGER raw = {};
  raw.LowPart = time.dwLowDateTime;
  raw.HighPart = time.dwHighDateTime;
  const std::uint64_t windows_ms = raw.QuadPart / 10000ULL;
  const std::uint64_t unix_ms =
      windows_ms > kWindowsToUnixEpochMilliseconds
          ? windows_ms - kWindowsToUnixEpochMilliseconds
          : 0;
  // A millisecond timestamp plus ten producer bits stays exactly representable
  // by GML's numeric representation for centuries. The per-process request ID
  // provides an additional monotonic discriminator between local instances.
  const std::uint64_t process_bits = GetCurrentProcessId() & 0x3ffULL;
  const std::uint64_t seed = unix_ms * 1024ULL + process_bits;
  return std::min(seed, kMaxExactJsonInteger - 65536ULL);
}

const char* ActionName(ActionKind kind) {
  switch (kind) {
    case ActionKind::kResync: return "resync";
    case ActionKind::kSelectHotbar: return "select_hotbar";
    case ActionKind::kSwapSlots: return "swap_slots";
    case ActionKind::kEquipArmor: return "equip_armor";
    case ActionKind::kUnequipArmor: return "unequip_armor";
    case ActionKind::kConsumeItem: return "consume_item";
    case ActionKind::kPinSpell: return "pin_spell";
    case ActionKind::kUnpinSpell: return "unpin_spell";
    case ActionKind::kCastPinnedSpell: return "cast_pinned_spell";
  }
  return "";
}

bool ValidFingerprint(const std::string& value, bool allow_empty) {
  return (allow_empty || !value.empty()) &&
      value.size() <= kMaxExpectedItemFingerprintBytes && IsValidUtf8(value);
}

bool SessionHasLearnedSpell(const BridgeActionSession& session,
                            std::int64_t spell) {
  if (spell < 0 || spell >= static_cast<std::int64_t>(kMaxBridgeSpells))
    return false;
  for (std::uint32_t i = 0; i < session.learned_spell_count; ++i) {
    if (session.learned_spell_indexes[i] == spell) return true;
  }
  return false;
}

enum class WriteCode { kWritten, kBusy, kError };

struct WriteResult {
  WriteCode code = WriteCode::kError;
  DWORD error = ERROR_GEN_FAILURE;
};

WriteResult AtomicCreateCommand(const std::wstring& directory,
                                const std::wstring& command_path,
                                std::uint64_t producer_nonce,
                                std::uint64_t request_id,
                                const std::string& payload) {
  const DWORD existing = GetFileAttributesW(command_path.c_str());
  if (existing != INVALID_FILE_ATTRIBUTES) {
    return {WriteCode::kBusy, ERROR_ALREADY_EXISTS};
  }
  const DWORD existence_error = GetLastError();
  if (existence_error != ERROR_FILE_NOT_FOUND &&
      existence_error != ERROR_PATH_NOT_FOUND) {
    return {WriteCode::kError, existence_error};
  }

  wchar_t name[128] = {};
  std::swprintf(name, sizeof(name) / sizeof(name[0]),
                L"command.%016llx.%016llx.new.json",
                static_cast<unsigned long long>(producer_nonce),
                static_cast<unsigned long long>(request_id));
  std::wstring temporary = directory;
  temporary.push_back(L'\\');
  temporary.append(name);

  HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return {WriteCode::kError, GetLastError()};
  }

  DWORD written = 0;
  const bool size_valid = payload.size() <=
      static_cast<std::size_t>(std::numeric_limits<DWORD>::max());
  const BOOL write_ok = size_valid &&
      WriteFile(file, payload.data(), static_cast<DWORD>(payload.size()),
                &written, nullptr);
  const BOOL flush_ok = write_ok &&
      static_cast<std::size_t>(written) == payload.size() &&
      FlushFileBuffers(file);
  DWORD error = flush_ok ? ERROR_SUCCESS : GetLastError();
  if (!CloseHandle(file) && error == ERROR_SUCCESS) error = GetLastError();
  if (!flush_ok || static_cast<std::size_t>(written) != payload.size() ||
      error != ERROR_SUCCESS) {
    DeleteFileW(temporary.c_str());
    return {WriteCode::kError,
            error == ERROR_SUCCESS ? ERROR_WRITE_FAULT : error};
  }

  // MOVEFILE_REPLACE_EXISTING is intentionally absent. A command already
  // waiting for the GML bridge must never be overwritten.
  if (!MoveFileExW(temporary.c_str(), command_path.c_str(),
                   MOVEFILE_WRITE_THROUGH)) {
    error = GetLastError();
    DeleteFileW(temporary.c_str());
    if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS) {
      return {WriteCode::kBusy, error};
    }
    return {WriteCode::kError, error};
  }
  return {WriteCode::kWritten, ERROR_SUCCESS};
}

}  // namespace

ActionTransport::ActionTransport(ActionTransportConfig config)
    : config_(config),
      next_command_sequence_(NewSequenceSeed()),
      producer_nonce_(NewSequenceSeed() ^
                      g_next_request_id.fetch_add(1,
                                                  std::memory_order_relaxed)) {
  config_.max_queue = std::clamp<std::size_t>(config_.max_queue, 1, 64);
  config_.min_write_interval_ms =
      std::clamp<std::uint64_t>(config_.min_write_interval_ms, 25, 1000);
  config_.acknowledgement_timeout_ms = std::clamp<std::uint64_t>(
      config_.acknowledgement_timeout_ms, 250, 30000);
}

bool ActionTransport::Initialize(const std::wstring& bridge_directory) {
  std::lock_guard<std::mutex> guard(mutex_);
  ClearSessionLocked();
  initialized_ = false;
  directory_.clear();
  command_path_.clear();
  if (bridge_directory.empty() || bridge_directory.size() >= 30000) return false;
  const DWORD attributes = GetFileAttributesW(bridge_directory.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES ||
      !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
      (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
    return false;
  }
  directory_ = bridge_directory;
  while (directory_.size() > 3 &&
         (directory_.back() == L'\\' || directory_.back() == L'/')) {
    directory_.pop_back();
  }
  command_path_ = directory_ + L"\\command.json";
  initialized_ = true;
  return true;
}

bool ActionTransport::IsSessionValid(const BridgeActionSession& session) {
  if (!(session.validated && session.protocol == kBridgeProtocol &&
      session.schema == kBridgeSchema && session.in_game &&
      !session.epoch.empty() && session.epoch.size() <= 256 &&
      IsValidUtf8(session.epoch) && session.state_sequence > 0 &&
      session.state_sequence <= kMaxExactJsonInteger &&
      session.inventory_revision > 0 &&
      session.inventory_revision <= kMaxExactJsonInteger &&
      session.hotbar_revision > 0 &&
      session.hotbar_revision <= kMaxExactJsonInteger &&
      session.command_sequence_floor <= kMaxExactJsonInteger &&
      session.inventory_size <= kMaxBridgeInventorySlots &&
      session.armor_size <= kMaxBridgeArmorSlots &&
      session.selected_hotbar_index >= -1 &&
      (session.selected_hotbar_index < 0 ||
       static_cast<std::uint32_t>(session.selected_hotbar_index) <
           session.inventory_size) &&
      session.learned_spell_count <= kMaxBridgeSpells &&
      session.pinned_spell_index >= -1)) return false;

  const bool needs_armor = session.can_equip_armor || session.can_unequip_armor;
  if (needs_armor &&
      (session.armor_revision == 0 ||
       session.armor_revision > kMaxExactJsonInteger)) return false;
  const bool needs_magic = session.can_pin_spell || session.can_unpin_spell ||
      session.can_cast_pinned_spell;
  if (needs_magic &&
      (session.magic_revision == 0 ||
       session.magic_revision > kMaxExactJsonInteger)) return false;
  for (std::uint32_t i = 0; i < session.learned_spell_count; ++i) {
    const std::int32_t spell = session.learned_spell_indexes[i];
    if (spell < 0 || spell >= static_cast<std::int32_t>(kMaxBridgeSpells))
      return false;
    for (std::uint32_t j = 0; j < i; ++j) {
      if (session.learned_spell_indexes[j] == spell) return false;
    }
  }
  return session.pinned_spell_index < 0 ||
      SessionHasLearnedSpell(session, session.pinned_spell_index);
}

std::string ActionTransport::BuildCommand(
    const BridgeActionSession& session, const Request& request,
    std::uint64_t sequence) {
  char numbers[256] = {};
  std::string json = "{\"schema\":1,\"epoch\":";
  json.append(JsonString(session.epoch));
  std::snprintf(numbers, sizeof(numbers),
                ",\"sequence\":%llu,\"action\":\"%s\"",
                static_cast<unsigned long long>(sequence),
                ActionName(request.kind));
  json.append(numbers);
  if (request.kind == ActionKind::kResync) {
    std::snprintf(numbers, sizeof(numbers), "}");
  } else if (request.kind == ActionKind::kSelectHotbar) {
    std::snprintf(numbers, sizeof(numbers), ",\"index\":%u}",
                  static_cast<unsigned>(request.first));
  } else if (request.kind == ActionKind::kSwapSlots) {
    std::snprintf(numbers, sizeof(numbers),
                  ",\"first\":%u,\"second\":%u}",
                  static_cast<unsigned>(request.first),
                  static_cast<unsigned>(request.second));
  } else if (request.kind == ActionKind::kEquipArmor) {
    std::snprintf(numbers, sizeof(numbers),
                  ",\"inventory_index\":%u,\"armor_index\":%u,\"expected_item\":",
                  static_cast<unsigned>(request.first),
                  static_cast<unsigned>(request.second));
    json.append(numbers);
    json.append(JsonString(request.expected_item));
    json.append(",\"expected_equipped_item\":");
    json.append(JsonString(request.expected_other_item));
    json.push_back('}');
    return json;
  } else if (request.kind == ActionKind::kUnequipArmor) {
    std::snprintf(numbers, sizeof(numbers),
                  ",\"armor_index\":%u,\"inventory_index\":%u,\"expected_item\":",
                  static_cast<unsigned>(request.first),
                  static_cast<unsigned>(request.second));
    json.append(numbers);
    json.append(JsonString(request.expected_item));
    json.append(",\"expected_destination_item\":");
    json.append(JsonString(request.expected_other_item));
    json.push_back('}');
    return json;
  } else if (request.kind == ActionKind::kConsumeItem) {
    std::snprintf(numbers, sizeof(numbers),
                  ",\"index\":%u,\"expected_item\":",
                  static_cast<unsigned>(request.first));
    json.append(numbers);
    json.append(JsonString(request.expected_item));
    json.push_back('}');
    return json;
  } else {
    std::snprintf(numbers, sizeof(numbers), ",\"spell\":%u}",
                  static_cast<unsigned>(request.first));
  }
  json.append(numbers);
  return json;
}

bool ActionTransport::UpdateSession(const BridgeActionSession& session) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (!initialized_ || !IsSessionValid(session)) {
    ClearSessionLocked();
    return false;
  }
  if (session_valid_ && session.epoch == session_.epoch &&
      session.state_sequence < session_.state_sequence) {
    ClearSessionLocked();
    return false;
  }
  if (!session_valid_ || session.epoch != session_.epoch) {
    queue_.clear();
    outstanding_ = {};
    attempted_ = false;
    next_command_sequence_ =
        std::max(NewSequenceSeed(), session.command_sequence_floor);
  } else {
    next_command_sequence_ =
        std::max(next_command_sequence_, session.command_sequence_floor);
  }
  session_ = session;
  session_valid_ = true;
  return true;
}

void ActionTransport::ClearSessionLocked() {
  session_valid_ = false;
  session_ = {};
  queue_.clear();
  outstanding_ = {};
  attempted_ = false;
}

void ActionTransport::ClearSession() {
  std::lock_guard<std::mutex> guard(mutex_);
  ClearSessionLocked();
}

bool ActionTransport::HasValidSession() const {
  std::lock_guard<std::mutex> guard(mutex_);
  return initialized_ && session_valid_;
}

SubmitResult ActionTransport::QueueLocked(Request request) {
  if (queue_.size() >= config_.max_queue) return {SubmitCode::kQueueFull, 0};
  request.request_id =
      g_next_request_id.fetch_add(1, std::memory_order_relaxed);
  if (request.request_id == 0) {
    request.request_id =
        g_next_request_id.fetch_add(1, std::memory_order_relaxed);
  }
  queue_.push_back(request);
  return {SubmitCode::kQueued, request.request_id};
}

SubmitResult ActionTransport::QueueResync() {
  std::lock_guard<std::mutex> guard(mutex_);
  if (!initialized_ || !session_valid_) {
    return {SubmitCode::kNoValidSession, 0};
  }
  if (outstanding_.active &&
      outstanding_.request.kind == ActionKind::kResync) {
    return {SubmitCode::kQueued, outstanding_.request.request_id};
  }
  for (const Request& pending : queue_) {
    if (pending.kind == ActionKind::kResync)
      return {SubmitCode::kQueued, pending.request_id};
  }
  Request request;
  request.kind = ActionKind::kResync;
  return QueueLocked(request);
}

SubmitResult ActionTransport::QueueSelectHotbar(
    std::int64_t index, std::uint64_t inventory_revision,
    std::uint64_t hotbar_revision) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (!initialized_ || !session_valid_) {
    return {SubmitCode::kNoValidSession, 0};
  }
  if (!session_.can_select_hotbar) {
    return {SubmitCode::kCapabilityUnavailable, 0};
  }
  if (index < 0 || index >= session_.inventory_size) {
    return {SubmitCode::kInvalidArgument, 0};
  }
  if (inventory_revision != session_.inventory_revision ||
      hotbar_revision != session_.hotbar_revision) {
    return {SubmitCode::kStaleView, 0};
  }
  Request request;
  request.kind = ActionKind::kSelectHotbar;
  request.inventory_revision = inventory_revision;
  request.hotbar_revision = hotbar_revision;
  request.first = static_cast<std::uint32_t>(index);
  return QueueLocked(request);
}

SubmitResult ActionTransport::QueueSwapSlots(
    std::int64_t first, std::int64_t second,
    std::uint64_t inventory_revision) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (!initialized_ || !session_valid_) {
    return {SubmitCode::kNoValidSession, 0};
  }
  if (!session_.can_swap_slots) {
    return {SubmitCode::kCapabilityUnavailable, 0};
  }
  if (first < 0 || second < 0 || first == second ||
      first >= session_.inventory_size || second >= session_.inventory_size) {
    return {SubmitCode::kInvalidArgument, 0};
  }
  if (inventory_revision != session_.inventory_revision) {
    return {SubmitCode::kStaleView, 0};
  }
  Request request;
  request.kind = ActionKind::kSwapSlots;
  request.inventory_revision = inventory_revision;
  request.first = static_cast<std::uint32_t>(first);
  request.second = static_cast<std::uint32_t>(second);
  return QueueLocked(request);
}

SubmitResult ActionTransport::QueueEquipArmor(
    std::int64_t inventory_index, std::int64_t armor_index,
    const std::string& expected_item,
    const std::string& expected_equipped_item,
    std::uint64_t inventory_revision, std::uint64_t armor_revision) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (!initialized_ || !session_valid_)
    return {SubmitCode::kNoValidSession, 0};
  if (!session_.can_equip_armor)
    return {SubmitCode::kCapabilityUnavailable, 0};
  if (inventory_index < 0 || armor_index < 0 ||
      inventory_index >= session_.inventory_size ||
      armor_index >= session_.armor_size ||
      !ValidFingerprint(expected_item, false) ||
      !ValidFingerprint(expected_equipped_item, true))
    return {SubmitCode::kInvalidArgument, 0};
  if (inventory_revision != session_.inventory_revision ||
      armor_revision != session_.armor_revision)
    return {SubmitCode::kStaleView, 0};
  Request request;
  request.kind = ActionKind::kEquipArmor;
  request.inventory_revision = inventory_revision;
  request.armor_revision = armor_revision;
  request.first = static_cast<std::uint32_t>(inventory_index);
  request.second = static_cast<std::uint32_t>(armor_index);
  request.expected_item = expected_item;
  request.expected_other_item = expected_equipped_item;
  return QueueLocked(std::move(request));
}

SubmitResult ActionTransport::QueueUnequipArmor(
    std::int64_t armor_index, std::int64_t inventory_index,
    const std::string& expected_item,
    const std::string& expected_destination_item,
    std::uint64_t inventory_revision, std::uint64_t armor_revision) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (!initialized_ || !session_valid_)
    return {SubmitCode::kNoValidSession, 0};
  if (!session_.can_unequip_armor)
    return {SubmitCode::kCapabilityUnavailable, 0};
  if (armor_index < 0 || inventory_index < 0 ||
      armor_index >= session_.armor_size ||
      inventory_index >= session_.inventory_size ||
      !ValidFingerprint(expected_item, false) ||
      !expected_destination_item.empty())
    return {SubmitCode::kInvalidArgument, 0};
  if (inventory_revision != session_.inventory_revision ||
      armor_revision != session_.armor_revision)
    return {SubmitCode::kStaleView, 0};
  Request request;
  request.kind = ActionKind::kUnequipArmor;
  request.inventory_revision = inventory_revision;
  request.armor_revision = armor_revision;
  request.first = static_cast<std::uint32_t>(armor_index);
  request.second = static_cast<std::uint32_t>(inventory_index);
  request.expected_item = expected_item;
  request.expected_other_item = expected_destination_item;
  return QueueLocked(std::move(request));
}

SubmitResult ActionTransport::QueueConsumeItem(
    std::int64_t index, const std::string& expected_item,
    std::uint64_t inventory_revision, std::uint64_t hotbar_revision) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (!initialized_ || !session_valid_)
    return {SubmitCode::kNoValidSession, 0};
  if (!session_.can_consume_item)
    return {SubmitCode::kCapabilityUnavailable, 0};
  if (index < 0 || index >= session_.inventory_size ||
      index != session_.selected_hotbar_index ||
      !ValidFingerprint(expected_item, false))
    return {SubmitCode::kInvalidArgument, 0};
  if (inventory_revision != session_.inventory_revision ||
      hotbar_revision != session_.hotbar_revision)
    return {SubmitCode::kStaleView, 0};
  Request request;
  request.kind = ActionKind::kConsumeItem;
  request.inventory_revision = inventory_revision;
  request.hotbar_revision = hotbar_revision;
  request.first = static_cast<std::uint32_t>(index);
  request.expected_item = expected_item;
  return QueueLocked(std::move(request));
}

SubmitResult ActionTransport::QueuePinSpell(
    std::int64_t spell, std::uint64_t magic_revision) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (!initialized_ || !session_valid_)
    return {SubmitCode::kNoValidSession, 0};
  if (!session_.can_pin_spell)
    return {SubmitCode::kCapabilityUnavailable, 0};
  if (!SessionHasLearnedSpell(session_, spell))
    return {SubmitCode::kInvalidArgument, 0};
  if (magic_revision != session_.magic_revision)
    return {SubmitCode::kStaleView, 0};
  Request request;
  request.kind = ActionKind::kPinSpell;
  request.magic_revision = magic_revision;
  request.first = static_cast<std::uint32_t>(spell);
  return QueueLocked(request);
}

SubmitResult ActionTransport::QueueUnpinSpell(
    std::int64_t spell, std::uint64_t magic_revision) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (!initialized_ || !session_valid_)
    return {SubmitCode::kNoValidSession, 0};
  if (!session_.can_unpin_spell)
    return {SubmitCode::kCapabilityUnavailable, 0};
  if (!SessionHasLearnedSpell(session_, spell) ||
      spell != session_.pinned_spell_index)
    return {SubmitCode::kInvalidArgument, 0};
  if (magic_revision != session_.magic_revision)
    return {SubmitCode::kStaleView, 0};
  Request request;
  request.kind = ActionKind::kUnpinSpell;
  request.magic_revision = magic_revision;
  request.first = static_cast<std::uint32_t>(spell);
  return QueueLocked(request);
}

SubmitResult ActionTransport::QueueCastPinnedSpell(
    std::int64_t spell, std::uint64_t magic_revision) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (!initialized_ || !session_valid_)
    return {SubmitCode::kNoValidSession, 0};
  if (!session_.can_cast_pinned_spell)
    return {SubmitCode::kCapabilityUnavailable, 0};
  if (!SessionHasLearnedSpell(session_, spell) ||
      spell != session_.pinned_spell_index)
    return {SubmitCode::kInvalidArgument, 0};
  if (magic_revision != session_.magic_revision)
    return {SubmitCode::kStaleView, 0};
  Request request;
  request.kind = ActionKind::kCastPinnedSpell;
  request.magic_revision = magic_revision;
  request.first = static_cast<std::uint32_t>(spell);
  return QueueLocked(request);
}

bool ActionTransport::IsRequestCurrent(const Request& request) const {
  switch (request.kind) {
    case ActionKind::kResync:
      return session_valid_;
    case ActionKind::kSelectHotbar:
      return request.inventory_revision == session_.inventory_revision &&
          request.hotbar_revision == session_.hotbar_revision &&
          request.first < session_.inventory_size && session_.can_select_hotbar;
    case ActionKind::kSwapSlots:
      return request.inventory_revision == session_.inventory_revision &&
          request.first < session_.inventory_size &&
          request.second < session_.inventory_size &&
          request.first != request.second && session_.can_swap_slots;
    case ActionKind::kEquipArmor:
      return request.inventory_revision == session_.inventory_revision &&
          request.armor_revision == session_.armor_revision &&
          request.first < session_.inventory_size &&
          request.second < session_.armor_size && session_.can_equip_armor;
    case ActionKind::kUnequipArmor:
      return request.inventory_revision == session_.inventory_revision &&
          request.armor_revision == session_.armor_revision &&
          request.first < session_.armor_size &&
          request.second < session_.inventory_size &&
          session_.can_unequip_armor;
    case ActionKind::kConsumeItem:
      return request.inventory_revision == session_.inventory_revision &&
          request.hotbar_revision == session_.hotbar_revision &&
          request.first < session_.inventory_size &&
          static_cast<std::int32_t>(request.first) ==
              session_.selected_hotbar_index && session_.can_consume_item;
    case ActionKind::kPinSpell:
      return request.magic_revision == session_.magic_revision &&
          session_.can_pin_spell &&
          SessionHasLearnedSpell(session_, request.first);
    case ActionKind::kUnpinSpell:
      return request.magic_revision == session_.magic_revision &&
          session_.can_unpin_spell &&
          static_cast<std::int32_t>(request.first) ==
              session_.pinned_spell_index &&
          SessionHasLearnedSpell(session_, request.first);
    case ActionKind::kCastPinnedSpell:
      return request.magic_revision == session_.magic_revision &&
          session_.can_cast_pinned_spell &&
          static_cast<std::int32_t>(request.first) ==
              session_.pinned_spell_index &&
          SessionHasLearnedSpell(session_, request.first);
  }
  return false;
}

PumpResult ActionTransport::Pump(std::uint64_t now_ms) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (!initialized_ || !session_valid_) {
    return {PumpCode::kNoValidSession};
  }
  if (outstanding_.active) {
    if (now_ms >= outstanding_.written_at_ms &&
        now_ms - outstanding_.written_at_ms >=
        config_.acknowledgement_timeout_ms) {
      const PumpResult result{PumpCode::kAcknowledgementTimedOut,
                              outstanding_.request.request_id,
                              outstanding_.command_sequence};
      ClearSessionLocked();
      return result;
    }
    return {PumpCode::kWaitingForAcknowledgement,
            outstanding_.request.request_id,
            outstanding_.command_sequence};
  }
  if (queue_.empty()) return {PumpCode::kIdle};
  if (attempted_ && now_ms - last_attempt_ms_ < config_.min_write_interval_ms) {
    return {PumpCode::kRateLimited, queue_.front().request_id};
  }
  attempted_ = true;
  last_attempt_ms_ = now_ms;

  const Request request = queue_.front();
  if (!IsRequestCurrent(request)) {
    queue_.pop_front();
    return {PumpCode::kDroppedStaleView, request.request_id};
  }
  if (next_command_sequence_ >= kMaxExactJsonInteger) {
    ClearSessionLocked();
    return {PumpCode::kNoValidSession};
  }
  const std::uint64_t command_sequence = ++next_command_sequence_;
  const std::string payload = BuildCommand(session_, request, command_sequence);
  const WriteResult write = AtomicCreateCommand(
      directory_, command_path_, producer_nonce_, request.request_id, payload);
  if (write.code == WriteCode::kBusy) {
    return {PumpCode::kCommandPathBusy, request.request_id, 0, write.error};
  }
  if (write.code == WriteCode::kError) {
    return {PumpCode::kIoError, request.request_id, 0, write.error};
  }
  queue_.pop_front();
  outstanding_.active = true;
  outstanding_.request = request;
  outstanding_.command_sequence = command_sequence;
  outstanding_.written_at_ms = now_ms;
  return {PumpCode::kWritten, request.request_id, command_sequence};
}

AckCode ActionTransport::HandleAcknowledgement(
    const BridgeActionAck& acknowledgement) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (!outstanding_.active) return AckCode::kNoOutstandingCommand;
  if (!acknowledgement.validated ||
      acknowledgement.protocol != kBridgeProtocol ||
      acknowledgement.schema != kBridgeSchema ||
      acknowledgement.epoch != session_.epoch ||
      acknowledgement.state_sequence == 0 ||
      acknowledgement.state_sequence > kMaxExactJsonInteger ||
      acknowledgement.command_sequence == 0 ||
      acknowledgement.command_sequence > kMaxExactJsonInteger ||
      acknowledgement.code.empty() || acknowledgement.code.size() > 64 ||
      !IsValidUtf8(acknowledgement.code)) {
    return AckCode::kInvalid;
  }
  if (acknowledgement.command_sequence != outstanding_.command_sequence ||
      acknowledgement.action != ActionName(outstanding_.request.kind)) {
    return AckCode::kDoesNotMatch;
  }
  outstanding_ = {};
  return AckCode::kAccepted;
}

std::size_t ActionTransport::PendingCount() const {
  std::lock_guard<std::mutex> guard(mutex_);
  return queue_.size();
}

bool ActionTransport::HasOutstandingCommand() const {
  std::lock_guard<std::mutex> guard(mutex_);
  return outstanding_.active;
}

}  // namespace fomds
