#include "NativePipboyBridge.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Log.h"
#include "PipboyProtocol.h"

namespace fo4ds {
namespace {

constexpr uintptr_t kDataManagerPointerRva = 0x030E8AF8;
constexpr size_t kMaximumNodes = 250000;
constexpr uint32_t kMaximumProtocolEntries =
    (std::numeric_limits<uint16_t>::max)();
constexpr uint32_t kMaximumObjectCapacity = 1u << 20;
constexpr size_t kMaximumStringBytes = 256u * 1024u;
constexpr unsigned kMaximumDepth = 96;
// Event sinks are authoritative for ordinary changes. The audit exists only
// to repair a lost notification or an engine-side generation change, so it
// must not repeatedly traverse the multi-thousand-node Perks and Workshop
// branches every five seconds during play.
constexpr uint32_t kDynamicAuditIntervalMs = 10000;
constexpr uint32_t kOrdinaryAuditIntervalMs = 60000;
constexpr uint32_t kLargeStaticAuditIntervalMs = 300000;
constexpr uint32_t kManagerLossGraceMs = 1000;
constexpr uint32_t kPublishRetryIntervalMs = 1000;
constexpr uintptr_t kRegisterGroupSinkRva = 0x00B3DBA0;
constexpr uintptr_t kUnregisterGroupSinkRva = 0x00B3DBD0;

constexpr uintptr_t kArrayVtableRva = 0x02543EF0;
constexpr uintptr_t kObjectVtableRva = 0x02545928;
constexpr uintptr_t kStringCsVtableRva = 0x02544638;
constexpr uintptr_t kBoolVtableRva = 0x02544670;
constexpr uintptr_t kInt32VtableRva = 0x025446A8;
constexpr uintptr_t kUInt32VtableRva = 0x025446E0;
constexpr uintptr_t kFloatVtableRva = 0x02544718;
constexpr uintptr_t kDynamicStringVtableRva = 0x02545548;
constexpr uintptr_t kLocalizedStringVtableRva = 0x02545580;
constexpr uintptr_t kUInt8VtableRva = 0x025455B8;
constexpr uintptr_t kStringVtableRva = 0x02545C08;
constexpr uintptr_t kInt8VtableRva = 0x02545DA8;

// Rapidly changing values use PipboyPrimitiveThrottledValue<T>. These are the
// primary PipboyValue vtables (the adjacent secondary vtables belong to
// IPipboyThrottledValue) in Fallout4.exe 1.11.240. They retain the ordinary
// primitive payload layout at +0x18. Without these three types the complete
// Stats, Map, and PlayerInfo trees were rejected even though their roots and
// all non-throttled values were valid.
constexpr uintptr_t kThrottledFloatVtableRva = 0x02545730;
constexpr uintptr_t kThrottledUInt8VtableRva = 0x02545778;
constexpr uintptr_t kThrottledBoolVtableRva = 0x025457C0;

struct GroupDescriptor {
  const char* name;
  uintptr_t offset;
  uintptr_t vtable_rva;
  uint32_t interval_ms;
  uint32_t audit_ms;
};

constexpr std::array<GroupDescriptor, 11> kGroups = {{
    {"Stats", 0x0A0, 0x02546398, 100, kDynamicAuditIntervalMs},
    {"Special", 0x1D8, 0x02546208, 1000, kOrdinaryAuditIntervalMs},
    {"Perks", 0x280, 0x02545AA8, 1000, kLargeStaticAuditIntervalMs},
    {"Inventory", 0x358, 0x02544750, 250, kOrdinaryAuditIntervalMs},
    {"Quests", 0x4F0, 0x02545C40, 500, kOrdinaryAuditIntervalMs},
    {"Workshop", 0x5D8, 0x02546868, 1000, kLargeStaticAuditIntervalMs},
    {"Log", 0x718, 0x025449A8, 1000, kLargeStaticAuditIntervalMs},
    {"Map", 0x7F0, 0x02545600, 250, kDynamicAuditIntervalMs},
    {"Radio", 0xA28, 0x025468C8, 500, kOrdinaryAuditIntervalMs},
    {"PlayerInfo", 0xAD8, 0x02545DE0, 100, kDynamicAuditIntervalMs},
    {"Status", 0xBE0, 0x02546668, 100, kDynamicAuditIntervalMs},
}};

struct GroupCache {
  bool valid = false;
  uint32_t root_id = 0;
  uint64_t next_capture_ms = 0;
  uint64_t next_audit_ms = 0;
  std::unordered_map<uint32_t, PipNode> nodes;
};

// PipboyDataGroup contains a BSTEventSource<PipboyValueChangedEvent> at +8.
// A sink is deliberately represented with an explicit two-slot MSVC vtable:
// Fallout calls slot 1 for ProcessEvent, while relying on a compiler-generated
// MinGW vtable here would make that cross-ABI contract implicit and fragile.
struct NativeGroupEventSink {
  const uintptr_t* vtable = nullptr;
  std::atomic<bool> dirty{true};
};

struct NativeEventSinkBank {
  std::array<NativeGroupEventSink, kGroups.size()> sinks;
};

// MSVC places its scalar-deleting destructor in slot zero. The engine does not
// own these sinks and must never delete them, but matching the two-argument
// slot shape avoids an ABI mismatch if it probes the destructor slot.
NativeGroupEventSink* SinkDeletingDestructor(NativeGroupEventSink* sink,
                                              uint32_t) {
  return sink;
}

int32_t SinkProcessEvent(NativeGroupEventSink* sink, const void*, void*) {
  if (sink) sink->dirty.store(true, std::memory_order_release);
  return 0;  // BSEventNotifyControl::kContinue
}

const std::array<uintptr_t, 2> kSinkVtable = {{
    reinterpret_cast<uintptr_t>(&SinkDeletingDestructor),
    reinterpret_cast<uintptr_t>(&SinkProcessEvent),
}};

static_assert(std::is_standard_layout<NativeGroupEventSink>::value,
              "native event sink must have a stable C-compatible layout");
static_assert(offsetof(NativeGroupEventSink, vtable) == 0,
              "native event sink vtable must be its first field");

NativeEventSinkBank* GetEventSinkBank() {
  // A manager generation can disappear before its event source can be safely
  // unregistered. Keep the tiny sink bank alive for the process lifetime so a
  // late callback can only set an atomic flag, never touch a dead worker.
  static NativeEventSinkBank* bank = new NativeEventSinkBank();
  return bank;
}

bool IsReadable(const void* address, size_t bytes) {
  if (!address || bytes == 0) return false;
  const uintptr_t begin = reinterpret_cast<uintptr_t>(address);
  if (begin > (std::numeric_limits<uintptr_t>::max)() - bytes) return false;
  MEMORY_BASIC_INFORMATION information = {};
  if (!VirtualQuery(address, &information, sizeof(information)) ||
      information.State != MEM_COMMIT ||
      (information.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
    return false;
  }
  const DWORD protection = information.Protect & 0xff;
  if (protection != PAGE_READONLY && protection != PAGE_READWRITE &&
      protection != PAGE_WRITECOPY && protection != PAGE_EXECUTE_READ &&
      protection != PAGE_EXECUTE_READWRITE &&
      protection != PAGE_EXECUTE_WRITECOPY) {
    return false;
  }
  const uintptr_t region_begin =
      reinterpret_cast<uintptr_t>(information.BaseAddress);
  if (region_begin > (std::numeric_limits<uintptr_t>::max)() -
                         information.RegionSize) {
    return false;
  }
  const uintptr_t region_end = region_begin + information.RegionSize;
  return begin >= region_begin && begin + bytes <= region_end;
}

bool IsExecutable(uintptr_t address) {
  if (!address) return false;
  MEMORY_BASIC_INFORMATION information = {};
  if (!VirtualQuery(reinterpret_cast<const void*>(address), &information,
                    sizeof(information)) ||
      information.State != MEM_COMMIT ||
      (information.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
    return false;
  }
  const DWORD protection = information.Protect & 0xff;
  return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
         protection == PAGE_EXECUTE_READWRITE ||
         protection == PAGE_EXECUTE_WRITECOPY;
}

template <class T>
bool ReadValue(uintptr_t address, T* output) {
  if (!output || !IsReadable(reinterpret_cast<const void*>(address), sizeof(T)))
    return false;
  std::memcpy(output, reinterpret_cast<const void*>(address), sizeof(T));
  return true;
}

bool ReadBytes(uintptr_t address, size_t size, std::string* output) {
  if (!output || size > kMaximumStringBytes ||
      (size && !IsReadable(reinterpret_cast<const void*>(address), size))) {
    return false;
  }
  output->assign(reinterpret_cast<const char*>(address), size);
  return true;
}

bool ReadPoolString(uintptr_t entry, std::string* output) {
  if (!output) return false;
  output->clear();
  if (!entry) return true;
  for (unsigned hop = 0; hop < 8; ++hop) {
    uint16_t flags = 0;
    if (!ReadValue(entry + 0x08, &flags)) return false;
    if (flags & 0x4000) {
      uintptr_t next = 0;
      if (!ReadValue(entry + 0x10, &next) || !next || next == entry)
        return false;
      entry = next;
      continue;
    }
    uint32_t length = 0;
    if (!ReadValue(entry + 0x10, &length) || length > kMaximumStringBytes)
      return false;
    if (!(flags & 0x8000)) return ReadBytes(entry + 0x18, length, output);
    if (length > kMaximumStringBytes / sizeof(wchar_t) ||
        !IsReadable(reinterpret_cast<const void*>(entry + 0x18),
                    static_cast<size_t>(length) * sizeof(wchar_t))) {
      return false;
    }
    const wchar_t* wide = reinterpret_cast<const wchar_t*>(entry + 0x18);
    const int required = WideCharToMultiByte(CP_UTF8, 0, wide,
                                             static_cast<int>(length), nullptr,
                                             0, nullptr, nullptr);
    if ((length && required <= 0) || required < 0 ||
        static_cast<size_t>(required) > kMaximumStringBytes)
      return false;
    output->resize(static_cast<size_t>(required));
    if (required && WideCharToMultiByte(CP_UTF8, 0, wide,
                                       static_cast<int>(length),
                                       output->data(), required, nullptr,
                                       nullptr) != required) {
      return false;
    }
    return true;
  }
  return false;
}

bool NodeEqual(const PipNode& left, const PipNode& right) {
  if (left.type != right.type) return false;
  switch (left.type) {
    case PipValueType::kBoolean:
      return left.boolean == right.boolean;
    case PipValueType::kInt8:
    case PipValueType::kUInt8:
    case PipValueType::kInt32:
    case PipValueType::kUInt32:
      return left.integer == right.integer;
    case PipValueType::kFloat:
      return std::memcmp(&left.number, &right.number, sizeof(float)) == 0;
    case PipValueType::kString:
      return left.string == right.string;
    case PipValueType::kArray:
      return left.array == right.array;
    case PipValueType::kObject:
      return left.object == right.object;
  }
  return false;
}

bool NodeMapEqual(const std::unordered_map<uint32_t, PipNode>& left,
                  const std::unordered_map<uint32_t, PipNode>& right) {
  if (left.size() != right.size()) return false;
  for (const auto& pair : left) {
    const auto found = right.find(pair.first);
    if (found == right.end() || !NodeEqual(pair.second, found->second))
      return false;
  }
  return true;
}

class NativeTreeReader {
 public:
  explicit NativeTreeReader(uintptr_t module_base) : module_base_(module_base) {}

  bool Copy(uintptr_t value, std::unordered_map<uint32_t, PipNode>* nodes,
            uint32_t* root_id, std::string* error) {
    if (!nodes || !root_id) return false;
    nodes_ = nodes;
    nodes_->clear();
    pointer_ids_.clear();
    visiting_.clear();
    uint32_t id = 0;
    if (!CopyValue(value, 0, &id, error)) return false;
    if (!id) {
      if (error) *error = "native group root used reserved ID zero";
      return false;
    }
    *root_id = id;
    return true;
  }

 private:
  bool CopyValue(uintptr_t address, unsigned depth, uint32_t* output_id,
                 std::string* error) {
    if (!address || !output_id || depth > kMaximumDepth) {
      if (error) *error = "invalid native Pip-Boy tree depth or pointer";
      return false;
    }
    const auto known_pointer = pointer_ids_.find(address);
    if (known_pointer != pointer_ids_.end()) {
      *output_id = known_pointer->second;
      return true;
    }
    if (!visiting_.insert(address).second) {
      if (error) *error = "cycle in native Pip-Boy value tree";
      return false;
    }

    uintptr_t vtable = 0;
    uint32_t id = 0;
    if (!ReadValue(address, &vtable) || !ReadValue(address + 0x08, &id) ||
        id == 0 || nodes_->size() >= kMaximumNodes) {
      if (error) *error = "invalid native Pip-Boy node header";
      visiting_.erase(address);
      return false;
    }
    const auto duplicate_id = nodes_->find(id);
    if (duplicate_id != nodes_->end()) {
      if (error) *error = "duplicate native Pip-Boy node ID";
      visiting_.erase(address);
      return false;
    }

    PipNode node;
    bool ok = true;
    if (vtable == module_base_ + kBoolVtableRva ||
        vtable == module_base_ + kThrottledBoolVtableRva) {
      uint8_t value = 0;
      ok = ReadValue(address + 0x18, &value);
      node.type = PipValueType::kBoolean;
      node.boolean = value != 0;
    } else if (vtable == module_base_ + kInt8VtableRva) {
      int8_t value = 0;
      ok = ReadValue(address + 0x18, &value);
      node.type = PipValueType::kInt8;
      node.integer = value;
    } else if (vtable == module_base_ + kUInt8VtableRva ||
               vtable == module_base_ + kThrottledUInt8VtableRva) {
      uint8_t value = 0;
      ok = ReadValue(address + 0x18, &value);
      node.type = PipValueType::kUInt8;
      node.integer = value;
    } else if (vtable == module_base_ + kInt32VtableRva) {
      int32_t value = 0;
      ok = ReadValue(address + 0x18, &value);
      node.type = PipValueType::kInt32;
      node.integer = value;
    } else if (vtable == module_base_ + kUInt32VtableRva) {
      uint32_t value = 0;
      ok = ReadValue(address + 0x18, &value);
      node.type = PipValueType::kUInt32;
      node.integer = value;
    } else if (vtable == module_base_ + kFloatVtableRva ||
               vtable == module_base_ + kThrottledFloatVtableRva) {
      ok = ReadValue(address + 0x18, &node.number) &&
           std::isfinite(node.number);
      node.type = PipValueType::kFloat;
    } else if (vtable == module_base_ + kStringVtableRva ||
               vtable == module_base_ + kStringCsVtableRva ||
               vtable == module_base_ + kLocalizedStringVtableRva) {
      uintptr_t entry = 0;
      ok = ReadValue(address + 0x18, &entry) &&
           ReadPoolString(entry, &node.string);
      node.type = PipValueType::kString;
    } else if (vtable == module_base_ + kDynamicStringVtableRva) {
      uintptr_t data = 0;
      uint16_t size = 0;
      uint16_t capacity = 0;
      ok = ReadValue(address + 0x18, &data) &&
           ReadValue(address + 0x20, &size) &&
           ReadValue(address + 0x22, &capacity) && size <= capacity &&
           capacity <= kMaximumStringBytes &&
           (!size || (data && ReadBytes(data, size, &node.string)));
      node.type = PipValueType::kString;
    } else if (vtable == module_base_ + kArrayVtableRva) {
      node.type = PipValueType::kArray;
      ok = CopyArray(address, depth, &node, error);
    } else if (vtable == module_base_ + kObjectVtableRva) {
      node.type = PipValueType::kObject;
      ok = CopyObject(address, depth, &node, error);
    } else {
      if (error) {
        char detail[160] = {};
        const uintptr_t relative = vtable >= module_base_
                                       ? vtable - module_base_
                                       : vtable;
        std::snprintf(detail, sizeof(detail),
                      "unknown native Pip-Boy value vtable rva=0x%08llX id=%u",
                      static_cast<unsigned long long>(relative), id);
        *error = detail;
      }
      ok = false;
    }
    if (!ok) {
      if (error && error->empty()) *error = "could not copy native Pip-Boy value";
      visiting_.erase(address);
      return false;
    }

    nodes_->emplace(id, std::move(node));
    pointer_ids_[address] = id;
    visiting_.erase(address);
    *output_id = id;
    return true;
  }

  bool CopyArray(uintptr_t address, unsigned depth, PipNode* node,
                 std::string* error) {
    uintptr_t values = 0;
    uint32_t count = 0;
    if (!ReadValue(address + 0x18, &values) ||
        !ReadValue(address + 0x28, &count) ||
        count > kMaximumProtocolEntries ||
        (count && (!values || !IsReadable(reinterpret_cast<const void*>(values),
                                          static_cast<size_t>(count) *
                                              sizeof(uintptr_t))))) {
      if (error) *error = "invalid native Pip-Boy array";
      return false;
    }
    node->array.reserve(count);
    for (uint32_t index = 0; index < count; ++index) {
      uintptr_t child = 0;
      std::memcpy(&child,
                  reinterpret_cast<const void*>(values + index * sizeof(child)),
                  sizeof(child));
      uint32_t child_id = 0;
      if (!CopyValue(child, depth + 1, &child_id, error)) return false;
      node->array.push_back(child_id);
    }
    return true;
  }

  bool CopyObject(uintptr_t address, unsigned depth, PipNode* node,
                  std::string* error) {
    uint32_t capacity = 0;
    uint32_t free = 0;
    uintptr_t sentinel = 0;
    uintptr_t entries = 0;
    if (!ReadValue(address + 0x24, &capacity) ||
        !ReadValue(address + 0x28, &free) ||
        !ReadValue(address + 0x30, &sentinel) ||
        !ReadValue(address + 0x40, &entries) ||
        capacity > kMaximumObjectCapacity || free > capacity ||
        capacity - free > kMaximumProtocolEntries ||
        (capacity && (capacity & (capacity - 1))) ||
        (capacity && (!entries ||
                      !IsReadable(reinterpret_cast<const void*>(entries),
                                  static_cast<size_t>(capacity) * 0x18)))) {
      if (error) *error = "invalid native Pip-Boy object table";
      return false;
    }
    if (!capacity) {
      if (entries || free) {
        if (error) *error = "invalid empty native Pip-Boy object table";
        return false;
      }
      return true;
    }
    if (!sentinel) {
      if (error) *error = "invalid native Pip-Boy object sentinel";
      return false;
    }
    const uintptr_t entries_end =
        entries + static_cast<size_t>(capacity) * 0x18;
    node->object.reserve(capacity - free);
    std::unordered_set<std::string> keys;
    keys.reserve(capacity - free);
    uint32_t occupied = 0;
    for (uint32_t index = 0; index < capacity; ++index) {
      const uintptr_t entry = entries + static_cast<size_t>(index) * 0x18;
      uintptr_t next = 0;
      std::memcpy(&next, reinterpret_cast<const void*>(entry + 0x10),
                  sizeof(next));
      if (!next) continue;
      if (next != sentinel &&
          (next < entries || next >= entries_end ||
           (next - entries) % 0x18 != 0)) {
        if (error) *error = "invalid native Pip-Boy object chain";
        return false;
      }
      ++occupied;
      uintptr_t key_entry = 0;
      uintptr_t child = 0;
      std::memcpy(&key_entry, reinterpret_cast<const void*>(entry),
                  sizeof(key_entry));
      std::memcpy(&child, reinterpret_cast<const void*>(entry + 0x08),
                  sizeof(child));
      std::string key;
      if (!ReadPoolString(key_entry, &key) || key.empty() ||
          !keys.insert(key).second) {
        if (error) *error = "invalid native Pip-Boy object key";
        return false;
      }
      uint32_t child_id = 0;
      if (!CopyValue(child, depth + 1, &child_id, error)) return false;
      node->object.emplace_back(std::move(key), child_id);
    }
    if (occupied != capacity - free) {
      if (error) *error = "inconsistent native Pip-Boy object occupancy";
      return false;
    }
    std::sort(node->object.begin(), node->object.end(),
              [](const auto& left, const auto& right) {
                return left.first < right.first;
              });
    return true;
  }

  uintptr_t module_base_ = 0;
  std::unordered_map<uint32_t, PipNode>* nodes_ = nullptr;
  std::unordered_map<uintptr_t, uint32_t> pointer_ids_;
  std::unordered_set<uintptr_t> visiting_;
};

class Writer {
 public:
  template <class T>
  void Value(const T& value) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
    data_.insert(data_.end(), bytes, bytes + sizeof(value));
  }

  void String(const std::string& value) {
    data_.insert(data_.end(), value.begin(), value.end());
    data_.push_back(0);
  }

  const std::vector<uint8_t>& data() const { return data_; }

 private:
  std::vector<uint8_t> data_;
};

void BuildPostOrder(uint32_t id,
                    const std::unordered_map<uint32_t, const PipNode*>& nodes,
                    std::unordered_set<uint32_t>* visited,
                    std::vector<uint32_t>* order) {
  if (!visited->insert(id).second) return;
  const auto found = nodes.find(id);
  if (found == nodes.end()) return;
  const PipNode& node = *found->second;
  if (node.type == PipValueType::kArray) {
    for (uint32_t child : node.array)
      BuildPostOrder(child, nodes, visited, order);
  } else if (node.type == PipValueType::kObject) {
    for (const auto& field : node.object)
      BuildPostOrder(field.second, nodes, visited, order);
  }
  order->push_back(id);
}

bool EncodeRecord(uint32_t id, const PipNode& node, const PipNode* previous,
                  Writer* writer, std::string* error) {
  if (!writer) return false;
  writer->Value(static_cast<uint8_t>(node.type));
  writer->Value(id);
  switch (node.type) {
    case PipValueType::kBoolean:
      writer->Value(static_cast<uint8_t>(node.boolean ? 1 : 0));
      break;
    case PipValueType::kInt8:
      writer->Value(static_cast<int8_t>(node.integer));
      break;
    case PipValueType::kUInt8:
      writer->Value(static_cast<uint8_t>(node.integer));
      break;
    case PipValueType::kInt32:
      writer->Value(static_cast<int32_t>(node.integer));
      break;
    case PipValueType::kUInt32:
      writer->Value(static_cast<uint32_t>(node.integer));
      break;
    case PipValueType::kFloat:
      writer->Value(node.number);
      break;
    case PipValueType::kString:
      writer->String(node.string);
      break;
    case PipValueType::kArray: {
      if (node.array.size() > (std::numeric_limits<uint16_t>::max)()) {
        if (error) *error = "native array exceeds companion protocol limit";
        return false;
      }
      writer->Value(static_cast<uint16_t>(node.array.size()));
      for (uint32_t child : node.array) writer->Value(child);
      break;
    }
    case PipValueType::kObject: {
      if (node.object.size() > (std::numeric_limits<uint16_t>::max)()) {
        if (error) *error = "native object exceeds companion protocol limit";
        return false;
      }
      writer->Value(static_cast<uint16_t>(node.object.size()));
      for (const auto& field : node.object) {
        writer->Value(field.second);
        writer->String(field.first);
      }
      std::vector<uint32_t> removed;
      if (previous && previous->type == PipValueType::kObject) {
        size_t old_index = 0;
        size_t new_index = 0;
        while (old_index < previous->object.size()) {
          while (new_index < node.object.size() &&
                 node.object[new_index].first <
                     previous->object[old_index].first) {
            ++new_index;
          }
          if (new_index == node.object.size() ||
              previous->object[old_index].first <
                  node.object[new_index].first) {
            removed.push_back(previous->object[old_index].second);
          }
          ++old_index;
        }
      }
      if (removed.size() > (std::numeric_limits<uint16_t>::max)()) {
        if (error) *error = "native object removal exceeds protocol limit";
        return false;
      }
      writer->Value(static_cast<uint16_t>(removed.size()));
      for (uint32_t child : removed) writer->Value(child);
      break;
    }
  }
  return true;
}

class CriticalSectionTryLock {
 public:
  explicit CriticalSectionTryLock(CRITICAL_SECTION* mutex)
      : mutex_(mutex), locked_(mutex_ && TryEnterCriticalSection(mutex_)) {}

  ~CriticalSectionTryLock() {
    if (locked_) LeaveCriticalSection(mutex_);
  }

  bool locked() const { return locked_; }

  CriticalSectionTryLock(const CriticalSectionTryLock&) = delete;
  CriticalSectionTryLock& operator=(const CriticalSectionTryLock&) = delete;

 private:
  CRITICAL_SECTION* mutex_ = nullptr;
  bool locked_ = false;
};

class BridgeWorker {
 public:
  explicit BridgeWorker(PipboyDatabase* database)
      : database_(database), module_base_(reinterpret_cast<uintptr_t>(
                                  GetModuleHandleW(nullptr))),
        sink_bank_(GetEventSinkBank()) {
    for (NativeGroupEventSink& sink : sink_bank_->sinks) {
      sink.vtable = kSinkVtable.data();
      sink.dirty.store(true, std::memory_order_relaxed);
    }
  }

  ~BridgeWorker() {
    try {
      UnregisterSinks();
    } catch (...) {
      // Destructors must not strand an exception during worker unwinding.
    }
  }

  void Run(const std::atomic<bool>* running) {
    Log(LogLevel::kInfo,
        "[NATIVE] in-process Pip-Boy bridge started manager_ptr_rva=%08llX",
        static_cast<unsigned long long>(kDataManagerPointerRva));
    while (running && running->load()) {
      const uint64_t now = GetTickCount64();
      Capture(now);
      Sleep(25);
    }
    UnregisterSinks();
    Log(LogLevel::kInfo, "[NATIVE] in-process Pip-Boy bridge stopped");
  }

 private:
  void Capture(uint64_t now) {
    uintptr_t manager = 0;
    if (!module_base_ ||
        !ReadValue(module_base_ + kDataManagerPointerRva, &manager) ||
        !manager || !IsReadable(reinterpret_cast<const void*>(manager), 0xD18)) {
      if (!manager_missing_since_) manager_missing_since_ = now;
      if (connected_ && now - manager_missing_since_ >= kManagerLossGraceMs)
        ResetState("Native Pip-Boy data manager unavailable");
      LogRateLimited("native-manager-wait", 5000, LogLevel::kInfo,
                     "[NATIVE] waiting for Fallout 4 Pip-Boy data manager");
      return;
    }
    manager_missing_since_ = 0;
    if (sinks_registered_ && manager != registered_manager_) {
      UnregisterSinks();
      ResetState("Native Pip-Boy data manager changed");
    }
    if (!sinks_registered_ && ManagerGroupsReady(manager)) RegisterSinks(manager);

    bool any_changed = false;
    bool any_valid = false;
    for (size_t index = 0; index < kGroups.size(); ++index) {
      GroupCache& cache = groups_[index];
      any_valid |= cache.valid;
      const bool notified = sink_bank_->sinks[index].dirty.exchange(
          false, std::memory_order_acq_rel);
      const bool audit_due = !cache.valid || now >= cache.next_audit_ms;
      if (!notified && !audit_due) continue;
      if (now < cache.next_capture_ms) {
        if (notified)
          sink_bank_->sinks[index].dirty.store(true,
                                                std::memory_order_release);
        continue;
      }
      cache.next_capture_ms = now + kGroups[index].interval_ms;
      cache.next_audit_ms = now + kGroups[index].audit_ms +
                            index * 1000 / kGroups.size();
      const uint64_t begin = GetTickCount64();
      bool captured = false;
      if (CaptureGroup(manager, index, &captured)) {
        any_changed = true;
      }
      any_valid |= cache.valid;
      if (!captured)
        sink_bank_->sinks[index].dirty.store(true,
                                              std::memory_order_release);
      const uint64_t elapsed = GetTickCount64() - begin;
      if (elapsed >= 10) {
        LogRateLimited((std::string("native-slow-") + kGroups[index].name).c_str(),
                       5000, LogLevel::kWarning,
                       "[PERF] native group=%s capture_ms=%llu nodes=%zu",
                       kGroups[index].name,
                       static_cast<unsigned long long>(elapsed),
                       cache.nodes.size());
      }
    }
    if ((!any_changed && connected_) || (!any_valid && !connected_)) return;
    if (now < next_publish_ms_) return;
    Publish();
  }

  bool CaptureGroup(uintptr_t manager, size_t index, bool* captured) {
    if (captured) *captured = false;
    const GroupDescriptor& descriptor = kGroups[index];
    GroupCache& cache = groups_[index];
    const uintptr_t group = manager + descriptor.offset;
    uintptr_t vtable = 0;
    if (!ReadValue(group, &vtable) ||
        vtable != module_base_ + descriptor.vtable_rva) {
      LogRateLimited((std::string("native-vtable-") + descriptor.name).c_str(),
                     5000, LogLevel::kWarning,
                     "[NATIVE] group=%s not initialized or vtable mismatch",
                     descriptor.name);
      return InvalidateGroup(index);
    }
    std::unordered_map<uint32_t, PipNode> next;
    uint32_t root_id = 0;
    std::string error;
    bool copied = false;
    {
      auto* mutex = reinterpret_cast<CRITICAL_SECTION*>(group + 0x60);
      CriticalSectionTryLock lock(mutex);
      if (!lock.locked()) return false;
      uintptr_t root = 0;
      const bool have_root = ReadValue(group + 0x90, &root) && root;
      if (!have_root) {
        if (captured) *captured = true;
        return InvalidateGroup(index);
      }
      NativeTreeReader reader(module_base_);
      copied = reader.Copy(root, &next, &root_id, &error);
    }
    if (!copied) {
      LogRateLimited((std::string("native-copy-") + descriptor.name).c_str(),
                     5000, LogLevel::kInfo,
                     "[NATIVE] group=%s waiting reason=%.256s",
                     descriptor.name,
                     error.empty() ? "root not populated" : error.c_str());
      return false;
    }
    if (captured) *captured = true;
    const bool changed = !cache.valid || cache.root_id != root_id ||
                         !NodeMapEqual(cache.nodes, next);
    if (!changed) return false;
    cache.valid = true;
    cache.root_id = root_id;
    cache.nodes = std::move(next);
    LogRateLimited((std::string("native-update-") + descriptor.name).c_str(),
                   2000, LogLevel::kInfo,
                   "[STATE] native group=%s root=%u nodes=%zu",
                   descriptor.name, root_id, cache.nodes.size());
    return true;
  }

  bool InvalidateGroup(size_t index) {
    GroupCache& cache = groups_[index];
    if (!cache.valid) return false;
    cache.valid = false;
    cache.root_id = 0;
    cache.nodes.clear();
    return true;
  }

  bool ManagerGroupsReady(uintptr_t manager) const {
    for (const GroupDescriptor& descriptor : kGroups) {
      uintptr_t vtable = 0;
      if (!ReadValue(manager + descriptor.offset, &vtable) ||
          vtable != module_base_ + descriptor.vtable_rva) {
        return false;
      }
    }
    return true;
  }

  void RegisterSinks(uintptr_t manager) {
    const uintptr_t register_address = module_base_ + kRegisterGroupSinkRva;
    const uintptr_t unregister_address = module_base_ + kUnregisterGroupSinkRva;
    if (!IsExecutable(register_address) ||
        !IsExecutable(unregister_address)) {
      LogRateLimited("native-register-address", 5000, LogLevel::kError,
                     "[NATIVE] group sink register/unregister RVA is not executable");
      return;
    }
    using RegisterSink = void (*)(uintptr_t, NativeGroupEventSink*, int32_t);
    const auto register_sink = reinterpret_cast<RegisterSink>(
        register_address);
    for (size_t index = 0; index < sink_bank_->sinks.size(); ++index)
      register_sink(manager, &sink_bank_->sinks[index],
                    static_cast<int32_t>(index));
    registered_manager_ = manager;
    sinks_registered_ = true;
    Log(LogLevel::kInfo,
        "[NATIVE] registered change sinks groups=%zu audit_ms=%u..%u",
        sink_bank_->sinks.size(), kDynamicAuditIntervalMs,
        kLargeStaticAuditIntervalMs);
  }

  void UnregisterSinks() {
    if (!sinks_registered_ || !registered_manager_ || !module_base_) return;
    uintptr_t current_manager = 0;
    if (!ReadValue(module_base_ + kDataManagerPointerRva, &current_manager) ||
        current_manager != registered_manager_ ||
        !ManagerGroupsReady(registered_manager_)) {
      LogRateLimited("native-unregister-stale", 5000, LogLevel::kWarning,
                     "[NATIVE] skipped unregister from stale manager generation");
      sinks_registered_ = false;
      registered_manager_ = 0;
      return;
    }
    const uintptr_t unregister_address = module_base_ + kUnregisterGroupSinkRva;
    if (!IsExecutable(unregister_address)) {
      Log(LogLevel::kError,
          "[NATIVE] group sink unregister RVA is not executable");
      sinks_registered_ = false;
      registered_manager_ = 0;
      return;
    }
    using UnregisterSink = void (*)(uintptr_t, NativeGroupEventSink*, int32_t);
    const auto unregister_sink = reinterpret_cast<UnregisterSink>(
        unregister_address);
    for (size_t index = 0; index < sink_bank_->sinks.size(); ++index)
      unregister_sink(registered_manager_, &sink_bank_->sinks[index],
                      static_cast<int32_t>(index));
    sinks_registered_ = false;
    registered_manager_ = 0;
  }

  void ResetState(const char* reason) {
    for (size_t index = 0; index < groups_.size(); ++index) {
      groups_[index] = {};
      sink_bank_->sinks[index].dirty.store(true, std::memory_order_release);
    }
    published_.clear();
    next_publish_ms_ = 0;
    if (connected_) database_->Reset(reason ? reason : "Native Pip-Boy reset");
    connected_ = false;
  }

  void Publish() {
    auto& current = current_refs_;
    current.clear();
    PipNode root;
    root.type = PipValueType::kObject;
    size_t reserve = 1;
    for (const GroupCache& cache : groups_) reserve += cache.nodes.size();
    current.reserve(reserve);
    for (size_t index = 0; index < groups_.size(); ++index) {
      const GroupCache& cache = groups_[index];
      if (!cache.valid || !cache.root_id) continue;
      root.object.emplace_back(kGroups[index].name, cache.root_id);
      for (const auto& pair : cache.nodes) {
        const auto inserted = current.emplace(pair.first, &pair.second);
        if (!inserted.second &&
            !NodeEqual(*inserted.first->second, pair.second)) {
          Log(LogLevel::kError,
              "[NATIVE] duplicate node ID across groups id=%u", pair.first);
          return;
        }
      }
    }
    std::sort(root.object.begin(), root.object.end(),
              [](const auto& left, const auto& right) {
                return left.first < right.first;
              });
    current.emplace(0, &root);

    auto& visited = visited_scratch_;
    auto& order = order_scratch_;
    visited.clear();
    order.clear();
    visited.reserve(current.size());
    order.reserve(current.size());
    BuildPostOrder(0, current, &visited, &order);
    if (visited.size() != current.size()) {
      LogRateLimited("native-unreachable", 5000, LogLevel::kWarning,
                     "[NATIVE] ignored unreachable values reachable=%zu total=%zu",
                     visited.size(), current.size());
    }

    Writer writer;
    size_t changed = 0;
    std::vector<uint32_t> changed_ids;
    std::string error;
    for (uint32_t id : order) {
      const auto next = current.find(id);
      if (next == current.end()) continue;
      const auto old = published_.find(id);
      if (old != published_.end() && NodeEqual(old->second, *next->second))
        continue;
      if (!EncodeRecord(id, *next->second,
                        old == published_.end() ? nullptr : &old->second,
                        &writer, &error)) {
        Log(LogLevel::kError, "[NATIVE] update encode failed reason=%s",
            error.c_str());
        return;
      }
      ++changed;
      changed_ids.push_back(id);
    }
    if (!changed) return;

    if (!connected_) {
      database_->SetConnection(true, false, "Fallout4 1.11.240 native",
                               "Live in-process Pip-Boy data");
      published_.clear();
      connected_ = true;
      // SetConnection cleared the database. Rebuild the full post-order frame.
      writer = Writer();
      changed = 0;
      changed_ids.clear();
      for (uint32_t id : order) {
        const auto next = current.find(id);
        if (next == current.end() ||
            !EncodeRecord(id, *next->second, nullptr, &writer, &error)) {
          Log(LogLevel::kError,
              "[NATIVE] initial update encode failed reason=%s", error.c_str());
          connected_ = false;
          database_->Reset("Native Pip-Boy initialization failed");
          next_publish_ms_ = GetTickCount64() + kPublishRetryIntervalMs;
          return;
        }
        ++changed;
        changed_ids.push_back(id);
      }
    }

    const uint64_t begin = GetTickCount64();
    if (!database_->ApplyDataUpdate(writer.data().data(), writer.data().size(),
                                    &error)) {
      Log(LogLevel::kError,
          "[NATIVE] database rejected update nodes=%zu bytes=%zu reason=%s",
          changed, writer.data().size(), error.c_str());
      connected_ = false;
      published_.clear();
      database_->Reset("Native Pip-Boy synchronization failed");
      next_publish_ms_ = GetTickCount64() + kPublishRetryIntervalMs;
      return;
    }
    const size_t pruned = database_->PruneUnreachableNodes();
    if (pruned) {
      LogRateLimited("native-prune", 5000, LogLevel::kInfo,
                     "[STATE] native pruned unreachable nodes=%zu", pruned);
    }
    for (auto it = published_.begin(); it != published_.end();) {
      if (current.find(it->first) == current.end()) {
        it = published_.erase(it);
      } else {
        ++it;
      }
    }
    for (uint32_t id : changed_ids) {
      const auto next = current.find(id);
      if (next != current.end()) published_[id] = *next->second;
    }
    next_publish_ms_ = 0;
    const uint64_t elapsed = GetTickCount64() - begin;
    LogRateLimited("native-publish", 2000, LogLevel::kInfo,
                   "[STATE] native publish changed=%zu nodes=%zu bytes=%zu ms=%llu",
                   changed, published_.size(), writer.data().size(),
                   static_cast<unsigned long long>(elapsed));
  }

  PipboyDatabase* database_ = nullptr;
  uintptr_t module_base_ = 0;
  NativeEventSinkBank* sink_bank_ = nullptr;
  std::array<GroupCache, kGroups.size()> groups_;
  std::unordered_map<uint32_t, PipNode> published_;
  std::unordered_map<uint32_t, const PipNode*> current_refs_;
  std::unordered_set<uint32_t> visited_scratch_;
  std::vector<uint32_t> order_scratch_;
  uintptr_t registered_manager_ = 0;
  uint64_t manager_missing_since_ = 0;
  uint64_t next_publish_ms_ = 0;
  bool sinks_registered_ = false;
  bool connected_ = false;
};

}  // namespace

NativePipboyBridge::NativePipboyBridge(PipboyDatabase* database)
    : database_(database) {}

NativePipboyBridge::~NativePipboyBridge() { Stop(); }

bool NativePipboyBridge::Start() {
  if (!database_) return false;
  std::lock_guard<std::mutex> lock(lifecycle_mutex_);
  if (thread_.joinable()) {
    if (running_.load(std::memory_order_acquire)) return true;
    thread_.join();
  }
  running_.store(true, std::memory_order_release);
  try {
    thread_ = std::thread(&NativePipboyBridge::Run, this);
    return true;
  } catch (...) {
    running_ = false;
    return false;
  }
}

void NativePipboyBridge::Stop() {
  std::lock_guard<std::mutex> lock(lifecycle_mutex_);
  running_.store(false, std::memory_order_release);
  if (thread_.joinable()) thread_.join();
}

void NativePipboyBridge::Run() {
  try {
    BridgeWorker worker(database_);
    worker.Run(&running_);
  } catch (const std::exception& error) {
    Log(LogLevel::kError, "[NATIVE] bridge worker exception reason=%.256s",
        error.what());
    database_->Reset("Native Pip-Boy worker failed");
  } catch (...) {
    Log(LogLevel::kError, "[NATIVE] bridge worker unknown exception");
    database_->Reset("Native Pip-Boy worker failed");
  }
  running_.store(false, std::memory_order_release);
}

}  // namespace fo4ds
