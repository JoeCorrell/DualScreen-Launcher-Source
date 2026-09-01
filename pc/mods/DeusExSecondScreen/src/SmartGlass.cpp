#include "SmartGlass.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <initializer_list>
#include <utility>

#include "Hook.h"
#include "GameActions.h"
#include "Log.h"
#include "Mem.h"

namespace dxs {
namespace {

#if defined(__GNUC__) && defined(__i386__)
#define DXS_SG_THISCALL __attribute__((__fastcall__))
#else
#define DXS_SG_THISCALL __fastcall
#endif

// DXHR Director's Cut 2.0.66.0 preferred-base addresses.  Every code address
// is signature checked by Install before the bridge is allowed to run.
constexpr uintptr_t kServiceGlobalVA = 0x01C8E380;
constexpr uintptr_t kControllerBundleGlobalVA = 0x01C8E5D0;
constexpr uintptr_t kFinalSendVA = 0x006AC590;
constexpr uintptr_t kFlushQueueVA = 0x006ACCA0;

constexpr uintptr_t kGameTouchEnterVA = 0x006B4AF0;
constexpr uintptr_t kGameTouchUpdateVA = 0x006B4AB0;
constexpr uintptr_t kInventoryEnterVA = 0x006B4D00;
constexpr uintptr_t kInventoryUpdateVA = 0x006B58C0;
constexpr uintptr_t kAugmentationEnterVA = 0x006B4660;
constexpr uintptr_t kMapEnterVA = 0x006B68E0;
constexpr uintptr_t kMissionEnterVA = 0x006B78D0;
constexpr uintptr_t kMediaEnterVA = 0x006B7290;

// Shipped SmartGlass action handlers. The original companion app fed parsed
// messages into these same queues. Keeping the handlers intact preserves all
// of the game's affordability, ownership, mission-state and notification
// checks instead of duplicating gameplay rules in the panel.
constexpr uintptr_t kAugActionConstructVA = 0x006B0520;
constexpr uintptr_t kAugActionAppendVA = 0x006AF9A0;
constexpr uintptr_t kAugActionExecuteVA = 0x006AFAE0;
constexpr uintptr_t kAugActionDestroyVA = 0x006B0580;
constexpr uintptr_t kObjectiveActionConstructVA = 0x006B3B50;
constexpr uintptr_t kObjectiveActionAppendVA = 0x005162F0;
constexpr uintptr_t kObjectiveActionExecuteVA = 0x006B3C20;
constexpr uintptr_t kObjectiveActionDestroyVA = 0x006B3BB0;

constexpr uintptr_t kGameTouchVtableVA = 0x00AA9528;
constexpr uintptr_t kInventoryVtableVA = 0x00AA9694;
constexpr uintptr_t kAugmentationVtableVA = 0x00AA9478;
constexpr uintptr_t kMapVtableVA = 0x00AA98E8;
constexpr uintptr_t kMissionVtableVA = 0x00AA9B00;
constexpr uintptr_t kMediaVtableVA = 0x00AA9AA0;

constexpr uint32_t kIncrementalFrames = 60;
constexpr uint32_t kFullFrames = 600;
constexpr size_t kMaxPayloadChars = 256 * 1024;
constexpr size_t kMaxRawMessages = 96;
constexpr size_t kMaxRecords = 512;

constexpr uint8_t kFinalSendSignature[] = {
    0x80, 0x79, 0x15, 0x00, 0x74, 0x0A, 0x8B, 0x49, 0x20};
constexpr uint8_t kFlushSignature[] = {
    0x56, 0x8B, 0xF1, 0x83, 0x7E, 0x04, 0x01};
constexpr uint8_t kGameTouchEnterSignature[] = {
    0x56, 0x8B, 0xF1, 0x8B, 0x0D, 0x80, 0xE3, 0xC8, 0x01};
constexpr uint8_t kGameTouchUpdateSignature[] = {
    0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x04, 0x83, 0xE8, 0x02};
constexpr uint8_t kInventoryEnterSignature[] = {
    0x56, 0x8B, 0xF1, 0x8B, 0x0D, 0x80, 0xE3, 0xC8, 0x01};
constexpr uint8_t kInventoryUpdateSignature[] = {
    0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x04, 0x48, 0x83, 0xF8, 0x03};
constexpr uint8_t kAugmentationEnterSignature[] = {
    0x56, 0x8B, 0xF1, 0x8B, 0x0D, 0x80, 0xE3, 0xC8, 0x01};
constexpr uint8_t kMapEnterSignature[] = {
    0x83, 0xEC, 0x10, 0x56, 0x8B, 0xF1, 0x8B, 0x0D, 0x80, 0xE3, 0xC8, 0x01};
constexpr uint8_t kMissionEnterSignature[] = {
    0x56, 0x8B, 0xF1, 0x8B, 0x0D, 0x80, 0xE3, 0xC8, 0x01};
constexpr uint8_t kMediaEnterSignature[] = {
    0x56, 0x8B, 0xF1, 0x8B, 0x0D, 0x80, 0xE3, 0xC8, 0x01};
constexpr uint8_t kAugActionConstructSignature[] = {
    0x56, 0x8B, 0xF1, 0xE8, 0x98, 0x35, 0x00, 0x00,
    0xC7, 0x06, 0x90, 0x8A, 0xAA, 0x00};
constexpr uint8_t kAugActionAppendSignature[] = {
    0x51, 0x56, 0x8B, 0xF1, 0x8B, 0x06, 0x8B, 0x4E, 0x04, 0x3B, 0xC1};
constexpr uint8_t kAugActionExecuteSignature[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xC0, 0x81, 0xEC,
    0xB4, 0x02, 0x00, 0x00};
constexpr uint8_t kAugActionDestroySignature[] = {
    0x56, 0x8B, 0xF1, 0x57, 0x8B, 0x7E, 0x14,
    0xC7, 0x06, 0x90, 0x8A, 0xAA, 0x00};
constexpr uint8_t kObjectiveActionConstructSignature[] = {
    0x56, 0x8B, 0xF1, 0xE8, 0x68, 0xFF, 0xFF, 0xFF,
    0xC7, 0x06, 0xF4, 0x92, 0xAA, 0x00};
constexpr uint8_t kObjectiveActionAppendSignature[] = {
    0x56, 0x8B, 0xF1, 0x8B, 0x0E, 0x8B, 0x46, 0x04, 0x3B, 0xC8};
constexpr uint8_t kObjectiveActionExecuteSignature[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xC0, 0xA1, 0x80,
    0xE3, 0xC8, 0x01};
constexpr uint8_t kObjectiveActionDestroySignature[] = {
    0x56, 0x8B, 0xF1, 0x57, 0x8B, 0x7E, 0x10,
    0xC7, 0x06, 0xF4, 0x92, 0xAA, 0x00};

struct Signature {
  uintptr_t va;
  const uint8_t* bytes;
  size_t size;
  const char* name;
};

const Signature kRequiredSignatures[] = {
    {kFinalSendVA, kFinalSendSignature, sizeof(kFinalSendSignature), "final send"},
    {kFlushQueueVA, kFlushSignature, sizeof(kFlushSignature), "queue flush"},
    {kGameTouchEnterVA, kGameTouchEnterSignature,
     sizeof(kGameTouchEnterSignature), "HUD entry"},
    {kGameTouchUpdateVA, kGameTouchUpdateSignature,
     sizeof(kGameTouchUpdateSignature), "HUD update"},
    {kInventoryEnterVA, kInventoryEnterSignature,
     sizeof(kInventoryEnterSignature), "inventory entry"},
    {kInventoryUpdateVA, kInventoryUpdateSignature,
     sizeof(kInventoryUpdateSignature), "inventory update"},
    {kAugmentationEnterVA, kAugmentationEnterSignature,
     sizeof(kAugmentationEnterSignature), "augmentation entry"},
    {kMapEnterVA, kMapEnterSignature, sizeof(kMapEnterSignature), "map entry"},
    {kMissionEnterVA, kMissionEnterSignature,
     sizeof(kMissionEnterSignature), "mission entry"},
    {kMediaEnterVA, kMediaEnterSignature, sizeof(kMediaEnterSignature), "media entry"},
    {kAugActionConstructVA, kAugActionConstructSignature,
     sizeof(kAugActionConstructSignature), "augmentation action constructor"},
    {kAugActionAppendVA, kAugActionAppendSignature,
     sizeof(kAugActionAppendSignature), "augmentation action queue"},
    {kAugActionExecuteVA, kAugActionExecuteSignature,
     sizeof(kAugActionExecuteSignature), "augmentation action execute"},
    {kAugActionDestroyVA, kAugActionDestroySignature,
     sizeof(kAugActionDestroySignature), "augmentation action destructor"},
    {kObjectiveActionConstructVA, kObjectiveActionConstructSignature,
     sizeof(kObjectiveActionConstructSignature), "objective action constructor"},
    {kObjectiveActionAppendVA, kObjectiveActionAppendSignature,
     sizeof(kObjectiveActionAppendSignature), "objective action queue"},
    {kObjectiveActionExecuteVA, kObjectiveActionExecuteSignature,
     sizeof(kObjectiveActionExecuteSignature), "objective action execute"},
    {kObjectiveActionDestroyVA, kObjectiveActionDestroySignature,
     sizeof(kObjectiveActionDestroySignature), "objective action destructor"},
};

struct CaptureContext {
  SmartGlassBridge* owner = nullptr;
  std::vector<SmartGlassRawMessage>* messages = nullptr;
};

thread_local CaptureContext* g_capture = nullptr;
std::atomic<SmartGlassBridge*> g_bridge{nullptr};
std::atomic<uint64_t> g_message_sequence{0};
void* g_unused_final_send_gateway = nullptr;

using TransportSendFn = void(DXS_SG_THISCALL*)(void*, void*, const wchar_t*,
                                               const wchar_t*);
using ControllerFn = void(DXS_SG_THISCALL*)(void*, void*);
using ActionConstructFn = void*(DXS_SG_THISCALL*)(void*, void*);
using ActionAppendFn = void(DXS_SG_THISCALL*)(void*, void*, const void*);
using ActionDestroyFn = void*(DXS_SG_THISCALL*)(void*, void*, uint32_t);

bool ReadableProtection(DWORD protect) {
  if ((protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
  protect &= 0xFF;
  return protect == PAGE_READONLY || protect == PAGE_READWRITE ||
      protect == PAGE_WRITECOPY || protect == PAGE_EXECUTE_READ ||
      protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY;
}

std::wstring SafeWideCopy(const wchar_t* source, size_t maximum) {
  std::wstring out;
  if (!source || maximum == 0) return out;
  out.reserve(std::min<size_t>(maximum, 4096));
  const wchar_t* cursor = source;
  while (out.size() < maximum) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(cursor, &mbi, sizeof(mbi)) != sizeof(mbi) ||
        mbi.State != MEM_COMMIT || !ReadableProtection(mbi.Protect)) {
      break;
    }
    const uintptr_t end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    const uintptr_t here = reinterpret_cast<uintptr_t>(cursor);
    if (end <= here) break;
    size_t count = (end - here) / sizeof(wchar_t);
    count = std::min(count, maximum - out.size());
    for (size_t i = 0; i < count; ++i) {
      const wchar_t value = cursor[i];
      if (value == L'\0') return out;
      out.push_back(value);
    }
    cursor += count;
    if (count == 0) break;
  }
  return out;
}

void CapturePayload(const wchar_t* channel, const wchar_t* payload) {
  CaptureContext* capture = g_capture;
  if (!capture || !capture->owner || !capture->messages) return;
  SmartGlassRawMessage message;
  message.sequence = g_message_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
  message.channel = SafeWideCopy(channel, 128);
  message.payload = SafeWideCopy(payload, kMaxPayloadChars);
  if (!message.payload.empty()) capture->messages->push_back(std::move(message));
}

void ForwardToTransport(void* transport, const wchar_t* channel,
                        const wchar_t* payload) {
  if (!transport) return;
  void** vtable = nullptr;
  if (!Read(transport, &vtable) || !vtable || !Readable(vtable + 7, sizeof(void*))) return;
  void* target = vtable[7];
  if (!Executable(target, 1)) return;
  reinterpret_cast<TransportSendFn>(target)(transport, nullptr, channel, payload);
}

// This hook deliberately does not call HookSet's gateway. The first function
// contains a short relative branch, so copying its prologue would make that
// gateway invalid. Reproducing the tiny original function is exact: inactive
// services return, active services tail-call transport vtable slot seven.
void DXS_SG_THISCALL FinalSendHook(void* service, void*, const wchar_t* channel,
                                   const wchar_t* payload) {
  if (g_capture) {
    CapturePayload(channel, payload);
    return;
  }
  uint8_t active = 0;
  if (!service || !Read(static_cast<uint8_t*>(service) + 0x15, &active) || !active) return;
  void* transport = PtrAt(service, 0x20);
  ForwardToTransport(transport, channel, payload);
}

std::wstring Normalize(const std::wstring& input) {
  std::wstring out;
  out.reserve(input.size());
  for (wchar_t c : input) {
    if (std::iswalnum(static_cast<wint_t>(c))) {
      out.push_back(static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c))));
    }
  }
  return out;
}

enum class JsonKind { kNull, kBool, kNumber, kString, kObject, kArray };

struct JsonValue {
  JsonKind kind = JsonKind::kNull;
  bool boolean = false;
  double number = 0.0;
  std::wstring string;
  std::vector<std::pair<std::wstring, JsonValue>> object;
  std::vector<JsonValue> array;
};

class JsonParser {
 public:
  explicit JsonParser(const std::wstring& text) : text_(text) {}

  bool Parse(JsonValue* out) {
    if (!out) return false;
    SkipSpace();
    // The game's builder starts some payloads with an apostrophe sentinel.
    if (Peek() == L'\'' && NextSignificant(1) != L'\0' &&
        (NextSignificant(1) == L'{' || NextSignificant(1) == L'[')) {
      ++pos_;
      SkipSpace();
    }
    if (!ParseValue(out, 0)) return false;
    if (out->kind == JsonKind::kString && !out->string.empty()) {
      const wchar_t first = out->string[0];
      if (first == L'{' || first == L'[') {
        JsonParser nested(out->string);
        JsonValue replacement;
        if (nested.Parse(&replacement)) *out = std::move(replacement);
      }
    }
    return true;
  }

 private:
  static constexpr int kMaxDepth = 48;

  wchar_t Peek() const { return pos_ < text_.size() ? text_[pos_] : L'\0'; }

  wchar_t NextSignificant(size_t offset) const {
    size_t p = std::min(pos_ + offset, text_.size());
    while (p < text_.size() && std::iswspace(static_cast<wint_t>(text_[p]))) ++p;
    return p < text_.size() ? text_[p] : L'\0';
  }

  void SkipSpace() {
    while (pos_ < text_.size() &&
           (std::iswspace(static_cast<wint_t>(text_[pos_])) || text_[pos_] == 0xFEFF)) {
      ++pos_;
    }
  }

  bool ParseValue(JsonValue* out, int depth) {
    if (depth > kMaxDepth) return false;
    SkipSpace();
    const wchar_t c = Peek();
    if (c == L'{') return ParseObject(out, depth + 1);
    if (c == L'[') return ParseArray(out, depth + 1);
    if (c == L'"' || c == L'\'') {
      out->kind = JsonKind::kString;
      return ParseString(&out->string);
    }
    if (c == L'-' || c == L'+' || c == L'.' || (c >= L'0' && c <= L'9')) {
      return ParseNumber(out);
    }
    std::wstring identifier;
    if (!ParseIdentifier(&identifier)) return false;
    const std::wstring normalized = Normalize(identifier);
    if (normalized == L"true") {
      out->kind = JsonKind::kBool;
      out->boolean = true;
    } else if (normalized == L"false") {
      out->kind = JsonKind::kBool;
      out->boolean = false;
    } else if (normalized == L"null" || normalized == L"undefined") {
      out->kind = JsonKind::kNull;
    } else {
      out->kind = JsonKind::kString;
      out->string = std::move(identifier);
    }
    return true;
  }

  bool ParseObject(JsonValue* out, int depth) {
    if (Peek() != L'{') return false;
    ++pos_;
    out->kind = JsonKind::kObject;
    SkipSpace();
    while (pos_ < text_.size() && Peek() != L'}') {
      std::wstring key;
      if (Peek() == L'"' || Peek() == L'\'') {
        if (!ParseString(&key)) return false;
      } else if (!ParseIdentifier(&key)) {
        return false;
      }
      SkipSpace();
      if (Peek() != L':' && Peek() != L'=') return false;
      ++pos_;
      JsonValue value;
      if (!ParseValue(&value, depth)) return false;
      out->object.emplace_back(std::move(key), std::move(value));
      SkipSpace();
      if (Peek() == L',' || Peek() == L';') {
        ++pos_;
        SkipSpace();
      } else if (Peek() != L'}') {
        return false;
      }
    }
    if (Peek() != L'}') return false;
    ++pos_;
    return true;
  }

  bool ParseArray(JsonValue* out, int depth) {
    if (Peek() != L'[') return false;
    ++pos_;
    out->kind = JsonKind::kArray;
    SkipSpace();
    while (pos_ < text_.size() && Peek() != L']') {
      JsonValue value;
      if (!ParseValue(&value, depth)) return false;
      out->array.push_back(std::move(value));
      SkipSpace();
      if (Peek() == L',' || Peek() == L';') {
        ++pos_;
        SkipSpace();
      } else if (Peek() != L']') {
        return false;
      }
    }
    if (Peek() != L']') return false;
    ++pos_;
    return true;
  }

  bool ParseString(std::wstring* out) {
    const wchar_t quote = Peek();
    if ((quote != L'"' && quote != L'\'') || !out) return false;
    ++pos_;
    out->clear();
    while (pos_ < text_.size()) {
      wchar_t c = text_[pos_++];
      if (c == quote) return true;
      if (c != L'\\') {
        out->push_back(c);
        continue;
      }
      if (pos_ >= text_.size()) return false;
      c = text_[pos_++];
      switch (c) {
        case L'"': out->push_back(L'"'); break;
        case L'\'': out->push_back(L'\''); break;
        case L'\\': out->push_back(L'\\'); break;
        case L'/': out->push_back(L'/'); break;
        case L'b': out->push_back(L'\b'); break;
        case L'f': out->push_back(L'\f'); break;
        case L'n': out->push_back(L'\n'); break;
        case L'r': out->push_back(L'\r'); break;
        case L't': out->push_back(L'\t'); break;
        case L'u': {
          if (pos_ + 4 > text_.size()) return false;
          unsigned value = 0;
          for (int i = 0; i < 4; ++i) {
            const wchar_t h = text_[pos_++];
            value <<= 4;
            if (h >= L'0' && h <= L'9') value |= static_cast<unsigned>(h - L'0');
            else if (h >= L'a' && h <= L'f') value |= static_cast<unsigned>(h - L'a' + 10);
            else if (h >= L'A' && h <= L'F') value |= static_cast<unsigned>(h - L'A' + 10);
            else return false;
          }
          out->push_back(static_cast<wchar_t>(value));
          break;
        }
        default: out->push_back(c); break;
      }
    }
    return false;
  }

  bool ParseIdentifier(std::wstring* out) {
    if (!out) return false;
    out->clear();
    while (pos_ < text_.size()) {
      const wchar_t c = text_[pos_];
      if (std::iswspace(static_cast<wint_t>(c)) || c == L':' || c == L'=' ||
          c == L',' || c == L';' || c == L'}' || c == L']') {
        break;
      }
      out->push_back(c);
      ++pos_;
    }
    return !out->empty();
  }

  bool ParseNumber(JsonValue* out) {
    const size_t begin = pos_;
    if (Peek() == L'+' || Peek() == L'-') ++pos_;
    bool any = false;
    while (Peek() >= L'0' && Peek() <= L'9') { ++pos_; any = true; }
    if (Peek() == L'.') {
      ++pos_;
      while (Peek() >= L'0' && Peek() <= L'9') { ++pos_; any = true; }
    }
    if (Peek() == L'e' || Peek() == L'E') {
      ++pos_;
      if (Peek() == L'+' || Peek() == L'-') ++pos_;
      while (Peek() >= L'0' && Peek() <= L'9') { ++pos_; any = true; }
    }
    if (!any) return false;
    const std::wstring number_text = text_.substr(begin, pos_ - begin);
    wchar_t* end = nullptr;
    const double value = std::wcstod(number_text.c_str(), &end);
    if (!end || end == number_text.c_str()) return false;
    out->kind = JsonKind::kNumber;
    out->number = value;
    return true;
  }

  const std::wstring& text_;
  size_t pos_ = 0;
};

const JsonValue* Find(const JsonValue& object,
                      std::initializer_list<const wchar_t*> names) {
  if (object.kind != JsonKind::kObject) return nullptr;
  for (const auto& pair : object.object) {
    const std::wstring key = Normalize(pair.first);
    for (const wchar_t* wanted : names) {
      if (key == Normalize(wanted ? std::wstring(wanted) : std::wstring())) {
        return &pair.second;
      }
    }
  }
  return nullptr;
}

double AsNumber(const JsonValue* value, double fallback = -1.0) {
  if (!value) return fallback;
  if (value->kind == JsonKind::kNumber) return value->number;
  if (value->kind == JsonKind::kBool) return value->boolean ? 1.0 : 0.0;
  if (value->kind == JsonKind::kString && !value->string.empty()) {
    wchar_t* end = nullptr;
    const double parsed = std::wcstod(value->string.c_str(), &end);
    if (end && end != value->string.c_str()) return parsed;
  }
  return fallback;
}

int AsInt(const JsonValue* value, int fallback = -1) {
  const double number = AsNumber(value, static_cast<double>(fallback));
  if (!std::isfinite(number)) return fallback;
  return static_cast<int>(number);
}

bool AsBool(const JsonValue* value, bool fallback = false) {
  if (!value) return fallback;
  if (value->kind == JsonKind::kBool) return value->boolean;
  if (value->kind == JsonKind::kNumber) return value->number != 0.0;
  if (value->kind == JsonKind::kString) {
    const std::wstring normalized = Normalize(value->string);
    if (normalized == L"true" || normalized == L"yes" || normalized == L"1") return true;
    if (normalized == L"false" || normalized == L"no" || normalized == L"0") return false;
  }
  return fallback;
}

std::wstring AsString(const JsonValue* value) {
  if (!value) return {};
  if (value->kind == JsonKind::kString) return value->string;
  if (value->kind == JsonKind::kBool) return value->boolean ? L"true" : L"false";
  if (value->kind == JsonKind::kNumber) {
    wchar_t buffer[64]{};
    std::swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%.8g", value->number);
    return buffer;
  }
  return {};
}

void FillRecord(const JsonValue& object, SmartGlassRecord* record) {
  if (!record || object.kind != JsonKind::kObject) return;
  for (const auto& pair : object.object) {
    const std::wstring key = Normalize(pair.first);
    if (key.empty()) continue;
    switch (pair.second.kind) {
      case JsonKind::kNumber: record->numbers[key] = pair.second.number; break;
      case JsonKind::kBool: record->flags[key] = pair.second.boolean; break;
      case JsonKind::kString: record->text[key] = pair.second.string; break;
      default: break;
    }
  }
}

void MergeItem(std::vector<SmartGlassInventoryItem>* items,
               const SmartGlassInventoryItem& incoming) {
  if (!items) return;
  auto match = items->end();
  if (incoming.item_index >= 0) {
    match = std::find_if(items->begin(), items->end(), [&](const auto& item) {
      return item.item_index == incoming.item_index;
    });
  }
  if (match == items->end() && incoming.pickup_id >= 0 && incoming.x >= 0) {
    match = std::find_if(items->begin(), items->end(), [&](const auto& item) {
      return item.pickup_id == incoming.pickup_id && item.x == incoming.x &&
          item.y == incoming.y;
    });
  }
  if (match == items->end()) {
    items->push_back(incoming);
    return;
  }
  SmartGlassInventoryItem merged = incoming;
  if (merged.pickup_id < 0) merged.pickup_id = match->pickup_id;
  if (merged.item_index < 0) merged.item_index = match->item_index;
  if (merged.x < 0) merged.x = match->x;
  if (merged.y < 0) merged.y = match->y;
  if (merged.name.empty()) merged.name = match->name;
  if (merged.subtitle.empty()) merged.subtitle = match->subtitle;
  if (merged.description.empty()) merged.description = match->description;
  if (merged.beauty_shot_image.empty()) merged.beauty_shot_image = match->beauty_shot_image;
  if (merged.beauty_shot_id < 0) merged.beauty_shot_id = match->beauty_shot_id;
  if (merged.combine_ids.empty()) merged.combine_ids = match->combine_ids;
  if (merged.stats.empty()) merged.stats = match->stats;
  merged.is_weapon = merged.is_weapon || match->is_weapon;
  *match = std::move(merged);
}

void MergeGlobals(const JsonValue& object, SmartGlassSnapshot* snapshot) {
  if (!snapshot || object.kind != JsonKind::kObject) return;
  for (const auto& pair : object.object) {
    const std::wstring key = Normalize(pair.first);
    if (key.empty()) continue;
    if (pair.second.kind == JsonKind::kNumber) snapshot->numbers[key] = pair.second.number;
    else if (pair.second.kind == JsonKind::kBool) snapshot->flags[key] = pair.second.boolean;
    else if (pair.second.kind == JsonKind::kString) snapshot->text[key] = pair.second.string;
  }

  auto number = [&](std::initializer_list<const wchar_t*> names, double* target) {
    const JsonValue* value = Find(object, names);
    if (value) *target = AsNumber(value, *target);
  };
  number({L"health", L"currentHealth", L"healthCurrent"}, &snapshot->status.health);
  number({L"maxHealth", L"healthMax"}, &snapshot->status.max_health);
  number({L"energy", L"currentEnergy", L"energyCurrent"}, &snapshot->status.energy);
  number({L"maxEnergy", L"energyMax"}, &snapshot->status.max_energy);
  const JsonValue* value = nullptr;
  if ((value = Find(object, {L"TotalXP"}))) snapshot->status.total_xp = AsInt(value);
  if ((value = Find(object, {L"NextPraxis"}))) snapshot->status.next_praxis = AsInt(value);
  if ((value = Find(object, {L"NumCredits", L"Credits"}))) snapshot->status.credits = AsInt(value);
  if ((value = Find(object, {L"PraxisLeft", L"praxisPoints"}))) {
    snapshot->status.praxis_points = AsInt(value);
  }
}

void ParseQuickSlots(const JsonValue& object, SmartGlassSnapshot* snapshot) {
  const JsonValue* slots = Find(object, {L"slotItems"});
  if (!slots || slots->kind != JsonKind::kArray || !snapshot) return;
  const JsonValue* types = Find(object, {L"slotItemTypes"});
  snapshot->quick_slots.clear();
  for (size_t i = 0; i < slots->array.size(); ++i) {
    SmartGlassQuickSlot slot;
    slot.slot = static_cast<int>(i);
    const JsonValue& value = slots->array[i];
    if (value.kind == JsonKind::kObject) {
      slot.pickup_id = AsInt(Find(value, {L"pickupID", L"id"}));
      slot.item_index = AsInt(Find(value, {L"itemIndex"}));
      slot.amount = AsInt(Find(value, {L"amount", L"stackCount"}), 0);
      slot.name = AsString(Find(value, {L"name"}));
    } else {
      slot.pickup_id = AsInt(&value);
    }
    if (types && types->kind == JsonKind::kArray && i < types->array.size()) {
      slot.type = AsString(&types->array[i]);
    }
    snapshot->quick_slots.push_back(std::move(slot));
  }
}

bool ContainsInventoryMessageType(const JsonValue& value, int wanted_type) {
  if (value.kind == JsonKind::kArray) {
    return std::any_of(value.array.begin(), value.array.end(),
                       [wanted_type](const JsonValue& child) {
      return ContainsInventoryMessageType(child, wanted_type);
    });
  }
  if (value.kind != JsonKind::kObject) return false;

  if (AsInt(Find(value, {L"invMsgType"}), -1) == wanted_type) return true;
  return std::any_of(value.object.begin(), value.object.end(),
                     [wanted_type](const auto& pair) {
    return ContainsInventoryMessageType(pair.second, wanted_type);
  });
}

void Visit(const JsonValue& value, SmartGlassSnapshot* snapshot) {
  if (!snapshot) return;
  if (value.kind == JsonKind::kArray) {
    for (const JsonValue& child : value.array) Visit(child, snapshot);
    return;
  }
  if (value.kind != JsonKind::kObject) return;

  MergeGlobals(value, snapshot);
  const JsonValue* inv_type = Find(value, {L"invMsgType"});
  const int inv_msg_type = AsInt(inv_type, -1);
  if (inv_msg_type == 5) ParseQuickSlots(value, snapshot);
  SmartGlassRecord record;
  FillRecord(value, &record);
  if (!record.numbers.empty() || !record.flags.empty() || !record.text.empty()) {
    snapshot->records.push_back(std::move(record));
  }

  const JsonValue* pickup = Find(value, {L"pickupID"});
  // Full live-slot records use `index`; incremental records use itemIndex.
  // Catalog (type 1), grid metadata (type 0), and quick slots (type 5) are not
  // owned inventory items and must never be promoted into the UI.
  const JsonValue* item_index = inv_msg_type == 2
      ? Find(value, {L"index", L"itemIndex"})
      : Find(value, {L"itemIndex", L"index"});
  if ((inv_msg_type == 2 || inv_msg_type == 4) &&
      pickup && item_index && AsInt(pickup, -1) >= 0 &&
      AsInt(item_index, -1) >= 0) {
    SmartGlassInventoryItem item;
    item.pickup_id = AsInt(pickup);
    item.item_index = AsInt(item_index);
    item.amount = AsInt(Find(value, {L"amount"}), 0);
    item.stack_count = AsInt(Find(value, {L"stackCount"}), item.amount);
    item.x = AsInt(Find(value, {L"posX", L"x"}));
    item.y = AsInt(Find(value, {L"posY", L"y"}));
    item.width = std::max(1, AsInt(Find(value, {L"width"}), 1));
    item.height = std::max(1, AsInt(Find(value, {L"height"}), 1));
    item.beauty_shot_id = AsInt(Find(value, {L"beautyShotId"}));
    item.flipped = AsBool(Find(value, {L"flipped", L"rotated"}));
    item.equipped = AsBool(Find(value, {L"isEquipped", L"equipped"}));
    item.can_use = AsBool(Find(value, {L"canUse", L"enableUse"}));
    item.can_discard = AsBool(Find(value, {L"canDiscard", L"enableDrop"}));
    item.can_combine = AsBool(Find(value, {L"canCombine", L"enableCombine"}));
    item.is_weapon = AsBool(Find(value, {L"isWeapon"}));
    item.name = AsString(Find(value, {L"name"}));
    item.subtitle = AsString(Find(value, {L"subtitle"}));
    item.description = AsString(Find(value, {L"description"}));
    item.beauty_shot_image = AsString(Find(value, {L"beautyShotImage"}));
    const JsonValue* combine = Find(value, {L"CombineIDs", L"combinableItems"});
    if (combine && combine->kind == JsonKind::kArray) {
      for (const JsonValue& id : combine->array) item.combine_ids.push_back(AsInt(&id));
    }
    for (const auto& pair : value.object) {
      const std::wstring key = Normalize(pair.first);
      if (pair.second.kind == JsonKind::kNumber &&
          (key.find(L"damage") != std::wstring::npos ||
           key.find(L"ammo") != std::wstring::npos ||
           key.find(L"reload") != std::wstring::npos ||
           key.find(L"rateoffire") != std::wstring::npos)) {
        item.stats[key] = pair.second.number;
        item.is_weapon = true;
      }
    }
    MergeItem(&snapshot->inventory_items, item);
  }

  // slotItems contains nested pickup objects. They belong to the quick-slot
  // mapping already parsed above, not to inventory storage.
  if (inv_msg_type == 5) return;

  if (Find(value, {L"augID", L"augName"})) {
    SmartGlassAugmentation aug;
    aug.aug_id = AsInt(Find(value, {L"augID"}));
    const JsonValue* node_index = Find(value, {L"nodeIndex", L"gridPos"});
    aug.node_index = node_index ? AsInt(node_index) : -1;
    aug.node_count = AsInt(Find(value, {L"nodeCount"}), 0);
    aug.learned_count = AsInt(
        Find(value, {L"installedCount", L"learnedCount"}), 0);
    aug.praxis_cost = AsInt(Find(value, {L"praxisCost", L"AugCost"}), 0);
    aug.installed = AsBool(Find(value, {L"isInstalled"}));
    aug.learned = AsBool(Find(value, {L"isLearned"}));
    aug.buyable = AsBool(Find(value, {L"isBuyable"}));
    aug.name = AsString(Find(value, {L"augName", L"upgradeName", L"name"}));
    aug.description = AsString(Find(value, {L"Description", L"description"}));
    aug.image_name = AsString(Find(value, {L"imageName"}));
    aug.type = AsString(Find(value, {L"augType"}));
    auto it = std::find_if(snapshot->augmentations.begin(), snapshot->augmentations.end(),
                           [&](const auto& old) {
      return aug.aug_id >= 0 && old.aug_id == aug.aug_id && old.node_index == aug.node_index;
    });
    if (it == snapshot->augmentations.end()) snapshot->augmentations.push_back(std::move(aug));
    else *it = std::move(aug);
  }

  if (Find(value, {L"objectiveID", L"shortDesc", L"MissionLogStatus"})) {
    SmartGlassObjective objective;
    objective.mission_id = AsInt(Find(value, {L"missionID"}));
    objective.objective_id = AsInt(Find(value, {L"objectiveID"}));
    objective.state = AsInt(Find(value, {L"objectiveState", L"activationState"}));
    objective.primary = AsBool(Find(value, {L"isPrimary"}));
    objective.title = AsString(Find(value, {L"missionTitle", L"title"}));
    objective.short_description = AsString(Find(value, {L"shortDesc"}));
    objective.long_description = AsString(Find(value, {L"longDesc"}));
    auto it = std::find_if(snapshot->objectives.begin(), snapshot->objectives.end(),
                           [&](const auto& old) {
      return old.mission_id == objective.mission_id &&
          old.objective_id == objective.objective_id;
    });
    if (it == snapshot->objectives.end()) snapshot->objectives.push_back(std::move(objective));
    else *it = std::move(objective);
  }

  // The mission action handler's acknowledgement uses the historical key
  // "missionID" for the objective ID and "activationState" for its new
  // tracked state. It is distinct from the mission-list records above.
  if (const JsonValue* tracked = Find(value, {L"activationState"})) {
    const int objective_id = AsInt(Find(value, {L"missionID"}));
    if (objective_id >= 0) {
      for (SmartGlassObjective& objective : snapshot->objectives) {
        if (objective.objective_id == objective_id) {
          objective.tracked = AsBool(tracked);
        }
      }
    }
  }

  if (Find(value, {L"mediaID", L"MediaType", L"fromAddress", L"toAddress"})) {
    SmartGlassMediaItem media;
    media.media_id = AsInt(Find(value, {L"mediaID"}));
    media.item_id = AsInt(Find(value, {L"itemID"}));
    media.media_type = AsInt(Find(value, {L"MediaType"}));
    media.title = AsString(Find(value, {L"title"}));
    media.from = AsString(Find(value, {L"fromAddress"}));
    media.to = AsString(Find(value, {L"toAddress"}));
    media.content = AsString(Find(value, {L"data", L"additionalContent", L"content"}));
    auto it = std::find_if(snapshot->media.begin(), snapshot->media.end(),
                           [&](const auto& old) {
      return old.media_id == media.media_id && old.item_id == media.item_id;
    });
    if (it == snapshot->media.end()) snapshot->media.push_back(std::move(media));
    else *it = std::move(media);
  }

  const JsonValue* x_ratio = Find(value, {L"xRatio"});
  const JsonValue* y_ratio = Find(value, {L"yRatio"});
  if (x_ratio && y_ratio) {
    SmartGlassMapMarker marker;
    marker.x = AsNumber(x_ratio, 0.0);
    marker.y = AsNumber(y_ratio, 0.0);
    marker.orientation = AsNumber(Find(value, {L"orientation"}), 0.0);
    marker.mission_index = AsInt(Find(value, {L"missionIndex"}));
    marker.main_mission = AsBool(Find(value, {L"isMainMission"}));
    marker.name = AsString(Find(value, {L"markerName"}));
    marker.icon = AsString(Find(value, {L"iconName"}));
    marker.mission_title = AsString(Find(value, {L"missionTitle"}));
    marker.mission_description = AsString(Find(value, {L"missionShortDescription"}));
    snapshot->map.markers.push_back(std::move(marker));
  }
  const JsonValue* map_image = Find(value, {L"mapImage"});
  if (map_image) snapshot->map.image = AsString(map_image);
  const JsonValue* map_value = nullptr;
  if ((map_value = Find(value, {L"mapWidth"}))) snapshot->map.width = AsInt(map_value, 0);
  if ((map_value = Find(value, {L"mapHeight"}))) snapshot->map.height = AsInt(map_value, 0);
  if ((map_value = Find(value, {L"mapLevel"}))) snapshot->map.level = AsInt(map_value, 0);
  if ((map_value = Find(value, {L"locationName"}))) snapshot->map.location = AsString(map_value);
  if ((map_value = Find(value, {L"sublocationName"}))) snapshot->map.sublocation = AsString(map_value);

  for (const auto& pair : value.object) Visit(pair.second, snapshot);
}

bool SignatureMatches(const Signature& signature) {
  const void* address = reinterpret_cast<const void*>(Rebase(signature.va));
  return PreferredCodeMatches(address, signature.bytes, signature.size);
}

bool ReadGlobalPointer(uintptr_t va, void** out) {
  return out && Read(reinterpret_cast<const void*>(Rebase(va)), out) && *out;
}

bool InvokeController(void* bundle, size_t offset, uintptr_t expected_vtable_va,
                      uintptr_t function_va) {
  if (!bundle || !Readable(static_cast<uint8_t*>(bundle) + offset, sizeof(void*))) return false;
  void* object = PtrAt(bundle, offset);
  if (!object || !Readable(object, sizeof(void*))) return false;
  void* vtable = PtrAt(object, 0);
  if (vtable != reinterpret_cast<void*>(Rebase(expected_vtable_va))) return false;
  void* function = reinterpret_cast<void*>(Rebase(function_va));
  if (!Executable(function, 1)) return false;
  reinterpret_cast<ControllerFn>(function)(object, nullptr);
  return true;
}

}  // namespace

SmartGlassBridge& TheSmartGlass() {
  static SmartGlassBridge bridge;
  return bridge;
}

bool SmartGlassBridge::Install(HookSet* hooks) {
  if (installed()) return true;
  if (!hooks) return false;
  for (const Signature& signature : kRequiredSignatures) {
    if (!SignatureMatches(signature)) {
      DXS_ERR("[sgui] %s signature does not match DXHRDC 2.0.66.0", signature.name);
      return false;
    }
  }
  if (!Readable(reinterpret_cast<void*>(Rebase(kServiceGlobalVA)), sizeof(void*)) ||
      !Readable(reinterpret_cast<void*>(Rebase(kControllerBundleGlobalVA)), sizeof(void*))) {
    DXS_ERR("[sgui] controller globals are not readable");
    return false;
  }

  SmartGlassBridge* expected = nullptr;
  if (!g_bridge.compare_exchange_strong(expected, this, std::memory_order_acq_rel) &&
      expected != this) {
    DXS_ERR("[sgui] another SmartGlass bridge is already active");
    return false;
  }
  const size_t checkpoint = hooks->count();
  // Nine exact bytes are replaced at the function entry. The generated
  // gateway is intentionally unused; see FinalSendHook above.
  if (!hooks->AddInline(reinterpret_cast<void*>(Rebase(kFinalSendVA)),
                        kFinalSendSignature, sizeof(kFinalSendSignature),
                        reinterpret_cast<void*>(&FinalSendHook),
                        &g_unused_final_send_gateway)) {
    hooks->Rollback(checkpoint);
    g_bridge.store(nullptr, std::memory_order_release);
    DXS_ERR("[sgui] could not install the local SmartGlass transport hook");
    return false;
  }

  hooks_ = hooks;
  force_refresh_.store(true, std::memory_order_release);
  installed_.store(true, std::memory_order_release);
  DXS_INFO("[sgui] native second-screen serializers attached (HUD/map/missions/augs/media); guarded inventory snapshot active");
  return true;
}

void SmartGlassBridge::Shutdown() {
  installed_.store(false, std::memory_order_release);
  SmartGlassBridge* expected = this;
  g_bridge.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
  hooks_ = nullptr;
  game_thread_id_ = 0;
  was_loaded_ = false;
  native_service_active_ = false;
  polling_suspended_logged_ = false;
  augmentation_seeded_ = false;
  force_refresh_.store(true, std::memory_order_release);
  Clear();
}

void SmartGlassBridge::SetPage(int page) {
  if (page < 1 || page > 5) page = 3;
  if (selected_page_.exchange(page, std::memory_order_acq_rel) != page) {
    force_refresh_.store(true, std::memory_order_release);
  }
}

int SmartGlassBridge::page() const {
  return selected_page_.load(std::memory_order_acquire);
}

void SmartGlassBridge::ForceRefresh() {
  force_refresh_.store(true, std::memory_order_release);
}

bool SmartGlassBridge::QueuePurchaseAugmentation(int aug_id, int node_index) {
  if (!installed() || aug_id < 0 || node_index < -1) return false;
  {
    std::lock_guard<std::mutex> guard(lock_);
    const auto match = std::find_if(
        snapshot_.augmentations.begin(), snapshot_.augmentations.end(),
        [aug_id, node_index](const SmartGlassAugmentation& augmentation) {
          return augmentation.aug_id == aug_id &&
              (node_index < 0 || augmentation.node_index == node_index);
        });
    if (match == snapshot_.augmentations.end()) return false;
  }

  const PendingAction request{PendingAction::Kind::kPurchaseAugmentation,
                              aug_id, node_index};
  std::lock_guard<std::mutex> guard(action_lock_);
  const auto duplicate = std::find_if(
      pending_actions_.begin(), pending_actions_.end(),
      [&request](const PendingAction& action) {
        return action.kind == request.kind && action.id == request.id &&
            action.secondary == request.secondary;
      });
  if (duplicate != pending_actions_.end()) return true;
  if (pending_actions_.size() >= 32) return false;
  pending_actions_.push_back(request);
  return true;
}

bool SmartGlassBridge::QueueToggleObjectiveTracking(int objective_id) {
  if (!installed() || objective_id < 0) return false;
  {
    std::lock_guard<std::mutex> guard(lock_);
    const auto match = std::find_if(
        snapshot_.objectives.begin(), snapshot_.objectives.end(),
        [objective_id](const SmartGlassObjective& objective) {
          return objective.objective_id == objective_id;
        });
    if (match == snapshot_.objectives.end()) return false;
  }

  const PendingAction request{PendingAction::Kind::kToggleObjective,
                              objective_id, -1};
  std::lock_guard<std::mutex> guard(action_lock_);
  const auto duplicate = std::find_if(
      pending_actions_.begin(), pending_actions_.end(),
      [&request](const PendingAction& action) {
        return action.kind == request.kind && action.id == request.id;
      });
  if (duplicate != pending_actions_.end()) return true;
  if (pending_actions_.size() >= 32) return false;
  pending_actions_.push_back(request);
  return true;
}

void SmartGlassBridge::Clear() {
  {
    std::lock_guard<std::mutex> guard(lock_);
    const uint64_t next_revision = snapshot_.revision + 1;
    snapshot_ = SmartGlassSnapshot{};
    snapshot_.revision = next_revision;
    snapshot_.selected_page = page();
    inventory_replace_pending_ = false;
    augmentation_seeded_ = false;
  }
  std::lock_guard<std::mutex> action_guard(action_lock_);
  pending_actions_.clear();
}

SmartGlassSnapshot SmartGlassBridge::Snapshot() const {
  std::lock_guard<std::mutex> guard(lock_);
  return snapshot_;
}

void SmartGlassBridge::Capture(const wchar_t* channel, const wchar_t* payload) {
  CapturePayload(channel, payload);
}

void SmartGlassBridge::Tick(uint32_t frame, bool game_loaded) {
  if (!installed()) return;
  if (!game_loaded) {
    if (was_loaded_) Clear();
    was_loaded_ = false;
    native_service_active_ = false;
    polling_suspended_logged_ = false;
    force_refresh_.store(true, std::memory_order_release);
    return;
  }

  // The shipped SmartGlass state machines assume InventoryPlayer's transient
  // interface is fully constructed. The gameplay movie becomes visible a few
  // seconds before that is guaranteed. A successful guarded native snapshot
  // is the deterministic readiness signal; entering sooner caused
  // DXHRDC.exe+0x26A954 to dereference a stale interface during save loading.
  if (!NativeInventorySnapshotReady()) return;

  const uint32_t thread = GetCurrentThreadId();
  if (!game_thread_id_) game_thread_id_ = thread;
  if (thread != game_thread_id_) {
    if (!warned_thread_) {
      warned_thread_ = true;
      DXS_WARN("[sgui] refusing refresh away from the primary Present thread");
    }
    return;
  }

  // These controllers are owned by the game's optional Xbox SmartGlass
  // service.  On PC (and under WEMU) that service normally exists but remains
  // inactive.  Entering its controller state machines in that state emits no
  // messages, yet used to happen every 60 frames and falsely increment the
  // snapshot revision.  Panel then rebuilt and uploaded its full 1280x720
  // chrome texture once a second on the Present thread.  More importantly,
  // repeatedly entering dormant native state machines is outside their
  // lifecycle contract and is a plausible source of delayed heap/SEH damage.
  void* native_service = nullptr;
  uint8_t native_active = 0;
  const bool service_active =
      ReadGlobalPointer(kServiceGlobalVA, &native_service) &&
      Readable(native_service, 0x24) &&
      Read(static_cast<uint8_t*>(native_service) + 0x15, &native_active) &&
      native_active != 0;
  if (!service_active) {
    if (!polling_suspended_logged_) {
      polling_suspended_logged_ = true;
      DXS_INFO("[sgui] native SmartGlass service is inactive; serializer polling suspended");
    }
    native_service_active_ = false;
    // Treat the gameplay session as observed so an unload still clears any
    // snapshot retained from an earlier active-service transition.  A later
    // inactive->active transition forces the initial refresh explicitly.
    was_loaded_ = true;
    last_update_frame_ = frame;
    last_full_frame_ = frame;
    force_refresh_.store(false, std::memory_order_release);
    return;
  }
  if (!native_service_active_) {
    native_service_active_ = true;
    polling_suspended_logged_ = false;
    augmentation_seeded_ = false;
    force_refresh_.store(true, std::memory_order_release);
    DXS_INFO("[sgui] native SmartGlass service became active; serializer polling resumed");
  }

  // Touch/render callbacks only enqueue requests. All gameplay mutations run
  // here, on the same thread the shipped SmartGlass handlers normally use and
  // only after the native service's own active lifecycle gate is satisfied.
  if (ExecutePendingActions(frame)) {
    force_refresh_.store(true, std::memory_order_release);
  }

  const bool forced = force_refresh_.load(std::memory_order_acquire);
  const bool first = !was_loaded_;
  const bool full = forced || first || static_cast<uint32_t>(frame - last_full_frame_) >= kFullFrames;
  const bool incremental = static_cast<uint32_t>(frame - last_update_frame_) >= kIncrementalFrames;
  if (!full && !incremental) return;

  // The augmentation entry point owns a persistent JSON builder and allocates
  // it on entry. Re-entering it on a timer would leak that builder. Refresh it
  // on load/page selection (and explicit ForceRefresh) while the stateless map,
  // mission and media controllers may safely receive periodic full refreshes.
  const bool refresh_selected = forced || first || (full && page() != 4);
  if (Refresh(frame, full, refresh_selected)) {
    last_update_frame_ = frame;
    if (full) last_full_frame_ = frame;
    was_loaded_ = true;
    force_refresh_.store(false, std::memory_order_release);
  }
}

void SmartGlassBridge::RequeueActions(std::vector<PendingAction>* actions) {
  if (!actions || actions->empty()) return;
  std::lock_guard<std::mutex> guard(action_lock_);
  const size_t available = pending_actions_.size() < 32
      ? 32 - pending_actions_.size()
      : 0;
  const size_t count = std::min(available, actions->size());
  pending_actions_.insert(pending_actions_.begin(), actions->begin(),
                          actions->begin() + static_cast<ptrdiff_t>(count));
  actions->clear();
}

bool SmartGlassBridge::ExecutePendingActions(uint32_t frame) {
  std::vector<PendingAction> actions;
  {
    std::lock_guard<std::mutex> guard(action_lock_);
    if (pending_actions_.empty()) return false;
    actions.swap(pending_actions_);
  }

  void* service = nullptr;
  void* allocator_owner = nullptr;
  if (!ReadGlobalPointer(kServiceGlobalVA, &service) ||
      !ReadGlobalPointer(0x019ED7B0, &allocator_owner) ||
      !Readable(allocator_owner, 0x4C)) {
    RequeueActions(&actions);
    return false;
  }
  if (!Readable(service, 0x24)) {
    RequeueActions(&actions);
    return false;
  }
  uint8_t service_active = 0;
  if (!Read(static_cast<uint8_t*>(service) + 0x15, &service_active) ||
      !service_active) {
    // The action handlers and queue flush belong to the optional native
    // SmartGlass service lifecycle just like the serializers. Never construct
    // or execute them against the dormant PC service object.
    RequeueActions(&actions);
    return false;
  }

  if (g_capture) {
    RequeueActions(&actions);
    return false;
  }

  std::vector<SmartGlassRawMessage> messages;
  messages.reserve(actions.size() * 2 + 4);
  CaptureContext capture{this, &messages};
  g_capture = &capture;

  bool invoked = false;
  bool augmentation_invoked = false;
  bool objective_invoked = false;
  const bool has_augmentation = std::any_of(
      actions.begin(), actions.end(), [](const PendingAction& action) {
        return action.kind == PendingAction::Kind::kPurchaseAugmentation;
      });
  if (has_augmentation) {
    // SmartGlassAugmentationMessageHandler is 0x18 bytes. Its queue entries
    // are {message type, augmentation DTP ID, upgrade-node index}; type 0 is
    // a base purchase and type 1 is an upgrade purchase.
    alignas(16) uint8_t handler[0x18] = {};
    auto construct = reinterpret_cast<ActionConstructFn>(Rebase(kAugActionConstructVA));
    auto append = reinterpret_cast<ActionAppendFn>(Rebase(kAugActionAppendVA));
    auto execute = reinterpret_cast<ControllerFn>(Rebase(kAugActionExecuteVA));
    auto destroy = reinterpret_cast<ActionDestroyFn>(Rebase(kAugActionDestroyVA));
    if (construct(handler, nullptr)) {
      void* response_builder = nullptr;
      std::memcpy(&response_builder, handler + 0x14, sizeof(response_builder));
      if (response_builder) {
        for (const PendingAction& action : actions) {
          if (action.kind != PendingAction::Kind::kPurchaseAugmentation) continue;
          const int message[3] = {
              action.secondary < 0 ? 0 : 1, action.id, action.secondary};
          append(handler + 4, nullptr, message);
        }
        execute(handler, nullptr);
        augmentation_invoked = true;
      }
      destroy(handler, nullptr, 0);
    }
  }

  const bool has_objective = std::any_of(
      actions.begin(), actions.end(), [](const PendingAction& action) {
        return action.kind == PendingAction::Kind::kToggleObjective;
      });
  if (has_objective) {
    // SmartGlassMissionLogMessageHandler is 0x14 bytes. Message type 2 is the
    // game's tracking toggle; the handler resolves the live objective and
    // calls its normal add/remove-locator paths.
    alignas(16) uint8_t handler[0x14] = {};
    auto construct = reinterpret_cast<ActionConstructFn>(
        Rebase(kObjectiveActionConstructVA));
    auto append = reinterpret_cast<ActionAppendFn>(Rebase(kObjectiveActionAppendVA));
    auto execute = reinterpret_cast<ControllerFn>(Rebase(kObjectiveActionExecuteVA));
    auto destroy = reinterpret_cast<ActionDestroyFn>(
        Rebase(kObjectiveActionDestroyVA));
    if (construct(handler, nullptr)) {
      void* response_builder = nullptr;
      std::memcpy(&response_builder, handler + 0x10, sizeof(response_builder));
      if (response_builder) {
        for (const PendingAction& action : actions) {
          if (action.kind != PendingAction::Kind::kToggleObjective) continue;
          const int message[2] = {2, action.id};
          append(handler + 4, nullptr, message);
        }
        execute(handler, nullptr);
        objective_invoked = true;
      }
      destroy(handler, nullptr, 0);
    }
  }

  invoked = augmentation_invoked || objective_invoked;

  if (invoked) {
    reinterpret_cast<ControllerFn>(Rebase(kFlushQueueVA))(service, nullptr);
  }

  g_capture = nullptr;

  std::vector<PendingAction> deferred;
  for (const PendingAction& action : actions) {
    if ((action.kind == PendingAction::Kind::kPurchaseAugmentation &&
         !augmentation_invoked) ||
        (action.kind == PendingAction::Kind::kToggleObjective &&
         !objective_invoked)) {
      deferred.push_back(action);
    }
  }
  RequeueActions(&deferred);
  if (!invoked) return false;
  if (!messages.empty()) {
    MergeCaptured(frame, false, false, page(), &messages);
  }
  return true;
}

bool SmartGlassBridge::Refresh(uint32_t frame, bool full, bool refresh_selected) {
  void* service = nullptr;
  void* bundle = nullptr;
  if (!ReadGlobalPointer(kServiceGlobalVA, &service) ||
      !ReadGlobalPointer(kControllerBundleGlobalVA, &bundle)) {
    return false;
  }
  if (!Readable(service, 0x24)) {
    return false;
  }
  uint8_t active = 0;
  if (!Read(static_cast<uint8_t*>(service) + 0x15, &active) || !active) {
    return false;
  }

  if (g_capture) return false;

  std::vector<SmartGlassRawMessage> messages;
  messages.reserve(full ? 128 : 24);
  CaptureContext capture{this, &messages};
  g_capture = &capture;

  // Capture only while the native service is genuinely active. Setting
  // service+0x15 ourselves without running its constructor left service+0x18
  // null; DXHR's concurrent service tick then entered 0x006AD350 and crashed at
  // 0x006AD360 while walking that slot table. Keep capture passive and never
  // mutate the service lifecycle or transport.

  bool invoked = false;
  if (full) {
    invoked |= InvokeController(bundle, 0x00, kGameTouchVtableVA,
                                kGameTouchEnterVA);
    invoked |= InvokeController(bundle, 0x0C, kInventoryVtableVA,
                                kInventoryEnterVA);
  }
  invoked |= InvokeController(bundle, 0x00, kGameTouchVtableVA,
                              kGameTouchUpdateVA);
  invoked |= InvokeController(bundle, 0x0C, kInventoryVtableVA,
                              kInventoryUpdateVA);

  const int selected = page();
  if (full && refresh_selected) {
    // Populate the four inventory-page ability shortcuts once the save has
    // survived its first guarded refresh. Doing this on the first loaded frame
    // widened the same initialization window that previously crashed status
    // capture, while doing it on every full refresh leaks the controller's
    // persistent builder. A direct visit to the Augmentations page still
    // refreshes it immediately.
    if (selected != 4 && was_loaded_ && !augmentation_seeded_) {
      const bool seeded = InvokeController(bundle, 0x18,
                                           kAugmentationVtableVA,
                                           kAugmentationEnterVA);
      invoked |= seeded;
      augmentation_seeded_ = seeded;
    }
    switch (selected) {
      case 1:
        invoked |= InvokeController(bundle, 0x24, kMapVtableVA, kMapEnterVA);
        break;
      case 2:
        invoked |= InvokeController(bundle, 0x28, kMissionVtableVA, kMissionEnterVA);
        break;
      case 4:
        {
          const bool seeded = InvokeController(bundle, 0x18,
                                               kAugmentationVtableVA,
                                               kAugmentationEnterVA);
          invoked |= seeded;
          augmentation_seeded_ = augmentation_seeded_ || seeded;
        }
        break;
      case 5:
        invoked |= InvokeController(bundle, 0x30, kMediaVtableVA, kMediaEnterVA);
        break;
      default:
        break;
    }
  }

  if (invoked) {
    void* flush = reinterpret_cast<void*>(Rebase(kFlushQueueVA));
    reinterpret_cast<ControllerFn>(flush)(service, nullptr);
  }

  g_capture = nullptr;

  if (!invoked) return false;
  // An invoked controller is not evidence that its serializer produced new
  // state.  Do not clear live lists or bump the revision on an empty capture;
  // that revision used to trigger an expensive full panel redraw/upload.
  if (!messages.empty()) {
    MergeCaptured(frame, full, refresh_selected, selected, &messages);
  }
  return true;
}

void SmartGlassBridge::MergeCaptured(uint32_t frame, bool full,
                                     bool refreshed_selected, int selected,
                                     std::vector<SmartGlassRawMessage>* messages) {
  if (!messages) return;
  std::lock_guard<std::mutex> guard(lock_);
  if (full) {
    // InventoryEnter starts a two-phase refresh. Its first flush contains the
    // type-1 pickup catalog but no owned slots; the type-2 live-slot batch is
    // emitted by a later InventoryUpdate. Keep the last complete inventory in
    // view until that batch arrives instead of flashing an empty inventory for
    // roughly one refresh interval. ParseQuickSlots already replaces its list
    // atomically when a type-5 message is received, so those remain intact too.
    inventory_replace_pending_ = true;
    if (refreshed_selected && selected == 1) snapshot_.map.markers.clear();
    if (refreshed_selected && selected == 2) snapshot_.objectives.clear();
    if (refreshed_selected && selected == 4) snapshot_.augmentations.clear();
    if (refreshed_selected && selected == 5) snapshot_.media.clear();
  }

  for (SmartGlassRawMessage& message : *messages) {
    JsonParser parser(message.payload);
    JsonValue root;
    if (parser.Parse(&root)) {
      if (inventory_replace_pending_ &&
          ContainsInventoryMessageType(root, 2)) {
        snapshot_.inventory_items.clear();
        snapshot_.weapons.clear();
        inventory_replace_pending_ = false;
      }
      Visit(root, &snapshot_);
    }
    snapshot_.raw_messages.push_back(std::move(message));
  }
  if (snapshot_.raw_messages.size() > kMaxRawMessages) {
    snapshot_.raw_messages.erase(
        snapshot_.raw_messages.begin(),
        snapshot_.raw_messages.begin() +
            static_cast<ptrdiff_t>(snapshot_.raw_messages.size() - kMaxRawMessages));
  }
  if (snapshot_.records.size() > kMaxRecords) {
    snapshot_.records.erase(
        snapshot_.records.begin(),
        snapshot_.records.begin() +
            static_cast<ptrdiff_t>(snapshot_.records.size() - kMaxRecords));
  }

  snapshot_.weapons.clear();
  for (const SmartGlassInventoryItem& item : snapshot_.inventory_items) {
    if (item.is_weapon || !item.stats.empty()) snapshot_.weapons.push_back(item);
  }
  snapshot_.selected_page = selected;
  snapshot_.captured_frame = frame;
  snapshot_.valid = snapshot_.valid || !messages->empty();
  ++snapshot_.revision;
}

}  // namespace dxs
