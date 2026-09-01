#include "PipboyProtocol.h"

#include <ws2tcpip.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <set>
#include <sstream>
#include <unordered_set>

#include "Log.h"

namespace fo4ds {
namespace {

constexpr uint32_t kMaxFrameBytes = 16u * 1024u * 1024u;
constexpr size_t kMaxNodes = 250000;
constexpr size_t kMaxTextBytes = 256u * 1024u;
constexpr size_t kMaxRetainedNodeBytes = 96u * 1024u * 1024u;
constexpr uint32_t kDomainStats = 1u << 0;
constexpr uint32_t kDomainInventory = 1u << 1;
constexpr uint32_t kDomainData = 1u << 2;
constexpr uint32_t kDomainMap = 1u << 3;
constexpr uint32_t kDomainRadio = 1u << 4;
constexpr uint32_t kAllDomains = (1u << 5) - 1;

class Reader {
 public:
  Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

  template <typename T>
  bool Read(T* out) {
    if (!out || Remaining() < sizeof(T)) return false;
    std::memcpy(out, data_ + offset_, sizeof(T));
    offset_ += sizeof(T);
    return true;
  }

  bool String(std::string* out) {
    if (!out) return false;
    const size_t begin = offset_;
    while (offset_ < size_ && data_[offset_] != 0) {
      if (offset_ - begin >= kMaxTextBytes) return false;
      ++offset_;
    }
    if (offset_ >= size_) return false;
    out->assign(reinterpret_cast<const char*>(data_ + begin), offset_ - begin);
    ++offset_;
    return true;
  }

  size_t Remaining() const { return size_ - offset_; }
  size_t Offset() const { return offset_; }

 private:
  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
  size_t offset_ = 0;
};

struct ParsedRecord {
  uint32_t id = 0;
  PipNode node;
  std::vector<uint32_t> removed;
};

size_t EstimateNodeBytes(const PipNode& node) {
  size_t bytes = sizeof(PipNode) + 64;
  bytes += node.string.capacity() + 1;
  bytes += node.array.capacity() * (sizeof(uint32_t) + 32);
  bytes += node.object.capacity() *
           (sizeof(std::pair<std::string, uint32_t>) + 32);
  for (const auto& field : node.object) bytes += field.first.capacity() + 1;
  return bytes;
}

size_t ProjectedNodeBytes(const PipNode* previous,
                          const ParsedRecord& record) {
  if (record.node.type != PipValueType::kObject || !previous ||
      previous->type != PipValueType::kObject) {
    return EstimateNodeBytes(record.node);
  }
  size_t fields = 0;
  size_t key_bytes = 0;
  for (const auto& field : previous->object) {
    if (std::find(record.removed.begin(), record.removed.end(), field.second) !=
        record.removed.end()) {
      continue;
    }
    ++fields;
    key_bytes += field.first.capacity() + 1;
  }
  for (const auto& added : record.node.object) {
    const auto existing = std::find_if(
        previous->object.begin(), previous->object.end(),
        [&](const auto& field) { return field.first == added.first; });
    const bool removed =
        existing != previous->object.end() &&
        std::find(record.removed.begin(), record.removed.end(),
                  existing->second) != record.removed.end();
    if (existing == previous->object.end() || removed) {
      ++fields;
      key_bytes += added.first.capacity() + 1;
    }
  }
  const size_t capacity = std::max(previous->object.capacity(), fields * 2);
  return sizeof(PipNode) + 64 + capacity *
             (sizeof(std::pair<std::string, uint32_t>) + 32) + key_bytes;
}

bool ParseRecord(Reader* reader, ParsedRecord* record, std::string* error) {
  uint8_t raw_type = 0;
  if (!reader->Read(&raw_type) || !reader->Read(&record->id)) {
    if (error) *error = "truncated update header";
    return false;
  }
  if (raw_type > static_cast<uint8_t>(PipValueType::kObject)) {
    if (error) *error = "unknown value type " + std::to_string(raw_type);
    return false;
  }
  record->node.type = static_cast<PipValueType>(raw_type);
  switch (record->node.type) {
    case PipValueType::kBoolean: {
      uint8_t value = 0;
      if (!reader->Read(&value)) break;
      record->node.boolean = value != 0;
      return true;
    }
    case PipValueType::kInt8: {
      int8_t value = 0;
      if (!reader->Read(&value)) break;
      record->node.integer = value;
      return true;
    }
    case PipValueType::kUInt8: {
      uint8_t value = 0;
      if (!reader->Read(&value)) break;
      record->node.integer = value;
      return true;
    }
    case PipValueType::kInt32: {
      int32_t value = 0;
      if (!reader->Read(&value)) break;
      record->node.integer = value;
      return true;
    }
    case PipValueType::kUInt32: {
      uint32_t value = 0;
      if (!reader->Read(&value)) break;
      record->node.integer = value;
      return true;
    }
    case PipValueType::kFloat: {
      if (!reader->Read(&record->node.number)) break;
      if (!std::isfinite(record->node.number)) {
        if (error) *error = "non-finite float";
        return false;
      }
      return true;
    }
    case PipValueType::kString:
      if (reader->String(&record->node.string)) return true;
      break;
    case PipValueType::kArray: {
      uint16_t count = 0;
      if (!reader->Read(&count) ||
          reader->Remaining() < static_cast<size_t>(count) * sizeof(uint32_t)) {
        break;
      }
      record->node.array.resize(count);
      for (uint32_t& id : record->node.array) reader->Read(&id);
      return true;
    }
    case PipValueType::kObject: {
      uint16_t added = 0;
      if (!reader->Read(&added)) break;
      record->node.object.reserve(added);
      std::unordered_set<std::string> keys;
      for (uint16_t i = 0; i < added; ++i) {
        uint32_t id = 0;
        std::string key;
        if (!reader->Read(&id) || !reader->String(&key) || key.empty() ||
            !keys.insert(key).second) {
          if (error) *error = "invalid object addition";
          return false;
        }
        record->node.object.emplace_back(std::move(key), id);
      }
      uint16_t removed = 0;
      if (!reader->Read(&removed) ||
          reader->Remaining() < static_cast<size_t>(removed) * sizeof(uint32_t)) {
        break;
      }
      record->removed.resize(removed);
      for (uint32_t& id : record->removed) reader->Read(&id);
      return true;
    }
  }
  if (error) *error = "truncated value at byte " + std::to_string(reader->Offset());
  return false;
}

bool IsNumeric(const PipNode* node) {
  if (!node) return false;
  return node->type == PipValueType::kInt8 ||
         node->type == PipValueType::kUInt8 ||
         node->type == PipValueType::kInt32 ||
         node->type == PipValueType::kUInt32 ||
         node->type == PipValueType::kFloat;
}

std::string NarrowStatus(const char* reason) {
  return reason && *reason ? reason : "Disconnected";
}

}  // namespace

void PipboyDatabase::SetConnection(bool connected, bool busy,
                                   const std::string& identity,
                                   const std::string& status) {
  std::lock_guard<std::mutex> lock(mutex_);
  bool changed = connected_ != connected || busy_ != busy ||
                 (!identity.empty() && identity_ != identity) ||
                 (!status.empty() && status_ != status);
  if (connected && !connected_) {
    nodes_.clear();
    parents_.clear();
    local_map_ = {};
    node_bytes_ = 0;
    ++inventory_structure_revision_;
    ++inventory_change_serial_;
    inventory_changes_.clear();
    ++generation_;
    for (uint64_t& revision : domain_revisions_) ++revision;
  }
  connected_ = connected;
  busy_ = busy;
  if (!identity.empty()) identity_ = identity;
  if (!status.empty()) status_ = status;
  if (changed) ++revision_;
}

void PipboyDatabase::Reset(const std::string& reason) {
  std::lock_guard<std::mutex> lock(mutex_);
  nodes_.clear();
  parents_.clear();
  local_map_ = {};
  node_bytes_ = 0;
  ++inventory_structure_revision_;
  ++inventory_change_serial_;
  inventory_changes_.clear();
  connected_ = false;
  busy_ = false;
  status_ = reason;
  ++generation_;
  ++revision_;
  for (uint64_t& revision : domain_revisions_) ++revision;
}

void PipboyDatabase::InvalidateTree(const std::string& reason) {
  std::lock_guard<std::mutex> lock(mutex_);
  nodes_.clear();
  parents_.clear();
  local_map_ = {};
  node_bytes_ = 0;
  ++inventory_structure_revision_;
  ++inventory_change_serial_;
  inventory_changes_.clear();
  status_ = reason.empty() ? "Game lifecycle changed" : reason;
  ++generation_;
  ++revision_;
  for (uint64_t& revision : domain_revisions_) ++revision;
  Log(LogLevel::kInfo, "[STATE] tree invalidated generation=%llu reason=%s",
      static_cast<unsigned long long>(generation_), status_.c_str());
}

void PipboyDatabase::AddParent(uint32_t child, uint32_t parent) {
  auto& list = parents_[child];
  if (std::find(list.begin(), list.end(), parent) == list.end())
    list.push_back(parent);
}

void PipboyDatabase::RemoveParent(uint32_t child, uint32_t parent) {
  auto found = parents_.find(child);
  if (found == parents_.end()) return;
  auto& list = found->second;
  list.erase(std::remove(list.begin(), list.end(), parent), list.end());
  if (list.empty()) parents_.erase(found);
}

uint32_t PipboyDatabase::ClassifyDomain(uint32_t id) const {
  if (id == 0) return kAllDomains;
  const PipNode* root = Resolve(0);
  if (!root || root->type != PipValueType::kObject) return kAllDomains;
  std::vector<uint32_t> queue = {id};
  std::unordered_set<uint32_t> visited;
  for (size_t cursor = 0; cursor < queue.size() && cursor < 4096; ++cursor) {
    const uint32_t current = queue[cursor];
    if (!visited.insert(current).second) continue;
    auto direct = std::find_if(root->object.begin(), root->object.end(),
                               [&](const auto& field) {
                                 return field.second == current;
                               });
    if (direct != root->object.end()) {
      const std::string& name = direct->first;
      if (name == "Inventory") return kDomainInventory;
      if (name == "Quests" || name == "Workshop") return kDomainData;
      if (name == "Map") return kDomainMap;
      if (name == "Radio") return kDomainRadio;
      return kDomainStats;
    }
    auto parents = parents_.find(current);
    if (parents != parents_.end())
      queue.insert(queue.end(), parents->second.begin(), parents->second.end());
  }
  return kAllDomains;
}

uint32_t PipboyDatabase::FindInventoryItemAncestor(uint32_t id) const {
  const PipNode* root = Resolve(0);
  if (!root || root->type != PipValueType::kObject) return 0;
  uint32_t inventory_id = 0;
  for (const auto& field : root->object) {
    if (field.first == "Inventory") {
      inventory_id = field.second;
      break;
    }
  }
  const PipNode* inventory = Resolve(inventory_id);
  if (!inventory || inventory->type != PipValueType::kObject) return 0;
  std::unordered_set<uint32_t> category_arrays;
  for (const auto& field : inventory->object) {
    const PipNode* child = Resolve(field.second);
    if (child && child->type == PipValueType::kArray)
      category_arrays.insert(field.second);
  }
  std::vector<uint32_t> queue = {id};
  std::unordered_set<uint32_t> visited;
  for (size_t cursor = 0; cursor < queue.size() && cursor < 4096; ++cursor) {
    const uint32_t current = queue[cursor];
    if (!visited.insert(current).second) continue;
    auto found = parents_.find(current);
    if (found == parents_.end()) continue;
    for (uint32_t parent : found->second) {
      if (category_arrays.count(parent)) return current;
      if (parent != inventory_id) queue.push_back(parent);
    }
  }
  return 0;
}

bool PipboyDatabase::IsInventoryMetadataNode(uint32_t id) const {
  const PipNode* root = Resolve(0);
  if (!root || root->type != PipValueType::kObject) return false;
  uint32_t inventory_id = 0;
  for (const auto& field : root->object) {
    if (field.first == "Inventory") {
      inventory_id = field.second;
      break;
    }
  }
  auto found = parents_.find(id);
  if (!inventory_id || found == parents_.end() ||
      std::find(found->second.begin(), found->second.end(), inventory_id) ==
          found->second.end()) {
    return false;
  }
  const PipNode* node = Resolve(id);
  return node && node->type != PipValueType::kArray &&
         node->type != PipValueType::kObject;
}

bool PipboyDatabase::ApplyDataUpdate(const uint8_t* data, size_t size,
                                     std::string* error) {
  if ((!data && size) || size > kMaxFrameBytes) {
    if (error) *error = "invalid update buffer";
    return false;
  }
  Reader reader(data, size);
  std::vector<ParsedRecord> records;
  records.reserve(std::min<size_t>(size / 6 + 1, 4096));
  while (reader.Remaining()) {
    ParsedRecord record;
    if (!ParseRecord(&reader, &record, error)) return false;
    records.emplace_back(std::move(record));
    if (records.size() > kMaxNodes) {
      if (error) *error = "record count limit exceeded";
      return false;
    }
  }
  if (records.empty()) return true;

  std::lock_guard<std::mutex> lock(mutex_);
  std::unordered_set<uint32_t> known;
  std::unordered_set<uint32_t> record_ids;
  known.reserve(nodes_.size() + records.size());
  record_ids.reserve(records.size());
  for (const auto& pair : nodes_) known.insert(pair.first);
  for (const ParsedRecord& record : records) {
    if (!record_ids.insert(record.id).second) {
      if (error) *error = "duplicate record ID " + std::to_string(record.id);
      return false;
    }
    if (record.node.type == PipValueType::kArray) {
      for (uint32_t child : record.node.array) {
        if (!known.count(child)) {
          if (error) *error = "dangling array reference " + std::to_string(child);
          return false;
        }
      }
    } else if (record.node.type == PipValueType::kObject) {
      for (const auto& field : record.node.object) {
        if (!known.count(field.second)) {
          if (error) *error = "dangling object reference " + std::to_string(field.second);
          return false;
        }
      }
    }
    known.insert(record.id);
  }
  if (known.size() > kMaxNodes) {
    if (error) *error = "node table limit exceeded";
    return false;
  }

  size_t projected_bytes = node_bytes_;
  for (const ParsedRecord& record : records) {
    auto previous = nodes_.find(record.id);
    const size_t old_bytes = previous == nodes_.end()
                                 ? 0
                                 : EstimateNodeBytes(previous->second);
    const size_t new_bytes = ProjectedNodeBytes(
        previous == nodes_.end() ? nullptr : &previous->second, record);
    if (old_bytes > projected_bytes) {
      if (error) *error = "node memory accounting failure";
      return false;
    }
    projected_bytes -= old_bytes;
    if (new_bytes > kMaxRetainedNodeBytes -
                        std::min(projected_bytes, kMaxRetainedNodeBytes)) {
      if (error) *error = "retained node memory budget exceeded";
      return false;
    }
    projected_bytes += new_bytes;
  }

  bool introduced_root = nodes_.find(0) == nodes_.end();
  uint32_t dirty_domains = 0;
  for (ParsedRecord& record : records) {
    auto previous = nodes_.find(record.id);
    if (previous != nodes_.end() &&
        record.node.type != PipValueType::kObject) {
      if (previous->second.type == PipValueType::kArray) {
        for (uint32_t child : previous->second.array)
          RemoveParent(child, record.id);
      } else if (previous->second.type == PipValueType::kObject) {
        for (const auto& field : previous->second.object)
          RemoveParent(field.second, record.id);
      }
    }
    if (record.node.type == PipValueType::kObject) {
      PipNode& destination = nodes_[record.id];
      if (destination.type != PipValueType::kObject) {
        if (destination.type == PipValueType::kArray) {
          for (uint32_t child : destination.array) RemoveParent(child, record.id);
        }
        destination = {};
      }
      destination.type = PipValueType::kObject;
      if (!record.removed.empty()) {
        destination.object.erase(
            std::remove_if(destination.object.begin(), destination.object.end(),
                           [&](const auto& field) {
                             const bool remove =
                                 std::find(record.removed.begin(), record.removed.end(),
                                           field.second) != record.removed.end();
                             if (remove) RemoveParent(field.second, record.id);
                             return remove;
                           }),
            destination.object.end());
      }
      for (auto& added : record.node.object) {
        auto existing = std::find_if(destination.object.begin(), destination.object.end(),
                                     [&](const auto& field) {
                                       return field.first == added.first;
                                     });
        if (existing == destination.object.end()) {
          destination.object.emplace_back(std::move(added));
          AddParent(destination.object.back().second, record.id);
        } else {
          if (existing->second != added.second)
            RemoveParent(existing->second, record.id);
          existing->second = added.second;
          AddParent(existing->second, record.id);
        }
      }
    } else {
      nodes_[record.id] = std::move(record.node);
      if (nodes_[record.id].type == PipValueType::kArray) {
        for (uint32_t child : nodes_[record.id].array) AddParent(child, record.id);
      }
    }
  }
  bool inventory_structure_changed = false;
  std::unordered_set<uint32_t> changed_inventory_items;
  for (const ParsedRecord& record : records) {
    const uint32_t domains = ClassifyDomain(record.id);
    dirty_domains |= domains;
    if (domains & kDomainInventory) {
      const uint32_t item_id = FindInventoryItemAncestor(record.id);
      if (item_id) {
        changed_inventory_items.insert(item_id);
      } else if (IsInventoryMetadataNode(record.id)) {
        changed_inventory_items.insert(0);
      } else {
        inventory_structure_changed = true;
      }
    }
  }
  if (inventory_structure_changed) {
    ++inventory_structure_revision_;
    ++inventory_change_serial_;
    inventory_changes_.clear();
  } else {
    for (uint32_t item_id : changed_inventory_items) {
      inventory_changes_.push_back({++inventory_change_serial_, item_id});
    }
    constexpr size_t kMaxInventoryChangeLog = 4096;
    if (inventory_changes_.size() > kMaxInventoryChangeLog) {
      inventory_changes_.erase(
          inventory_changes_.begin(),
          inventory_changes_.begin() +
              (inventory_changes_.size() - kMaxInventoryChangeLog));
    }
  }

  if (introduced_root && nodes_.find(0) != nodes_.end()) {
    dirty_domains = kAllDomains;
    status_ = "Live Fallout 4 data";
    Log(LogLevel::kInfo,
        "[STATE] root acquired generation=%llu nodes=%zu records=%zu",
        static_cast<unsigned long long>(generation_), nodes_.size(), records.size());
  }
  for (size_t i = 0; i < domain_revisions_.size(); ++i) {
    if (dirty_domains & (1u << i)) ++domain_revisions_[i];
  }
  node_bytes_ = projected_bytes;
  ++revision_;
  return true;
}

bool PipboyDatabase::ApplyLocalMap(const uint8_t* data, size_t size,
                                   std::string* error) {
  if ((!data && size) || size > kMaxFrameBytes) {
    if (error) *error = "invalid local map buffer";
    return false;
  }
  Reader reader(data, size);
  LocalMapState next;
  if (!reader.Read(&next.width) || !reader.Read(&next.height) ||
      !reader.Read(&next.northwest_x) || !reader.Read(&next.northwest_y) ||
      !reader.Read(&next.northeast_x) || !reader.Read(&next.northeast_y) ||
      !reader.Read(&next.southwest_x) || !reader.Read(&next.southwest_y)) {
    if (error) *error = "truncated local map header";
    return false;
  }
  const uint64_t pixels = static_cast<uint64_t>(next.width) * next.height;
  const bool finite_extents = std::isfinite(next.northwest_x) &&
                              std::isfinite(next.northwest_y) &&
                              std::isfinite(next.northeast_x) &&
                              std::isfinite(next.northeast_y) &&
                              std::isfinite(next.southwest_x) &&
                              std::isfinite(next.southwest_y);
  if (!next.width || !next.height || next.width > 32768 ||
      next.height > 32768 || !finite_extents ||
      pixels > 16u * 1024u * 1024u ||
      reader.Remaining() != pixels) {
    if (error) *error = "invalid local map dimensions/payload";
    return false;
  }
  next.pixels.resize(static_cast<size_t>(pixels));
  for (uint8_t& pixel : next.pixels) reader.Read(&pixel);
  std::lock_guard<std::mutex> lock(mutex_);
  next.revision = local_map_.revision + 1;
  local_map_ = std::move(next);
  ++revision_;
  LogRateLimited("local-map-update", 2000, LogLevel::kInfo,
                 "[MAP] local raster revision=%llu size=%ux%u bytes=%zu",
                 static_cast<unsigned long long>(local_map_.revision),
                 local_map_.width, local_map_.height,
                 local_map_.pixels.size());
  return true;
}

size_t PipboyDatabase::PruneUnreachableNodes() {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto root = nodes_.find(0);
  if (root == nodes_.end()) return 0;

  std::unordered_set<uint32_t> reachable;
  std::vector<uint32_t> pending = {0};
  reachable.reserve(nodes_.size());
  for (size_t cursor = 0; cursor < pending.size(); ++cursor) {
    const uint32_t id = pending[cursor];
    if (!reachable.insert(id).second) continue;
    const auto found = nodes_.find(id);
    if (found == nodes_.end()) continue;
    if (found->second.type == PipValueType::kArray) {
      pending.insert(pending.end(), found->second.array.begin(),
                     found->second.array.end());
    } else if (found->second.type == PipValueType::kObject) {
      for (const auto& field : found->second.object)
        pending.push_back(field.second);
    }
  }

  const size_t before = nodes_.size();
  for (auto it = nodes_.begin(); it != nodes_.end();) {
    if (!reachable.count(it->first)) {
      it = nodes_.erase(it);
    } else {
      ++it;
    }
  }
  const size_t removed = before - nodes_.size();
  if (!removed) return 0;

  parents_.clear();
  node_bytes_ = 0;
  for (const auto& pair : nodes_) {
    node_bytes_ += EstimateNodeBytes(pair.second);
    if (pair.second.type == PipValueType::kArray) {
      for (uint32_t child : pair.second.array) AddParent(child, pair.first);
    } else if (pair.second.type == PipValueType::kObject) {
      for (const auto& field : pair.second.object)
        AddParent(field.second, pair.first);
    }
  }
  return removed;
}

const PipNode* PipboyDatabase::Resolve(uint32_t id) const {
  auto it = nodes_.find(id);
  return it == nodes_.end() ? nullptr : &it->second;
}

const PipNode* PipboyDatabase::Child(const PipNode* object, const char* key) const {
  if (!object || object->type != PipValueType::kObject || !key) return nullptr;
  auto found = std::find_if(object->object.begin(), object->object.end(),
                            [&](const auto& field) { return field.first == key; });
  return found == object->object.end() ? nullptr : Resolve(found->second);
}

const PipNode* PipboyDatabase::RootBranch(const char* key) const {
  return Child(Resolve(0), key);
}

bool PipboyDatabase::Bool(const PipNode* object, const char* key,
                          bool fallback) const {
  const PipNode* node = Child(object, key);
  if (!node) return fallback;
  if (node->type == PipValueType::kBoolean) return node->boolean;
  return IsNumeric(node) ? (node->type == PipValueType::kFloat
                                 ? node->number != 0.0f
                                 : node->integer != 0)
                         : fallback;
}

int64_t PipboyDatabase::Int(const PipNode* object, const char* key,
                            int64_t fallback) const {
  const PipNode* node = Child(object, key);
  if (!node) return fallback;
  if (node->type == PipValueType::kFloat) return static_cast<int64_t>(node->number);
  return IsNumeric(node) ? node->integer : fallback;
}

float PipboyDatabase::Float(const PipNode* object, const char* key,
                            float fallback) const {
  const PipNode* node = Child(object, key);
  if (!node) return fallback;
  if (node->type == PipValueType::kFloat) return node->number;
  return IsNumeric(node) ? static_cast<float>(node->integer) : fallback;
}

std::string PipboyDatabase::String(const PipNode* object, const char* key) const {
  const PipNode* node = Child(object, key);
  return node && node->type == PipValueType::kString ? node->string : std::string();
}

bool PipboyDatabase::BuildInventoryItem(uint32_t id,
                                        InventoryItemState* item) const {
  if (!item) return false;
  const PipNode* entry = Resolve(id);
  if (!entry || entry->type != PipValueType::kObject ||
      !Child(entry, "HandleID")) {
    return false;
  }
  InventoryItemState next;
  next.node_id = id;
  next.form_id = static_cast<uint32_t>(Int(entry, "formID"));
  next.handle_id = static_cast<uint64_t>(Int(entry, "HandleID"));
  next.count = static_cast<int32_t>(Int(entry, "count"));
  next.equip_state = static_cast<int32_t>(Int(entry, "equipState"));
  next.favorite = static_cast<int32_t>(Int(entry, "favorite", -2));
  next.filter_flag = static_cast<uint32_t>(Int(entry, "filterFlag"));
  next.can_favorite = Bool(entry, "canFavorite");
  next.legendary = Bool(entry, "isLegendary");
  next.power_armor = Bool(entry, "isPowerArmorItem");
  next.name = String(entry, "text");
  const PipNode* stacks = Child(entry, "StackID");
  if (stacks && stacks->type == PipValueType::kArray &&
      !stacks->array.empty()) {
    const PipNode* stack = Resolve(stacks->array.front());
    if (stack && IsNumeric(stack)) {
      next.stack_id = static_cast<int32_t>(
          stack->type == PipValueType::kFloat ? stack->number : stack->integer);
    }
  }
  const PipNode* card = Child(entry, "itemCardInfoList");
  if (card && card->type == PipValueType::kArray) {
    for (uint32_t card_id : card->array) {
      const PipNode* card_entry = Resolve(card_id);
      ItemCardField card_field;
      card_field.label = String(card_entry, "text");
      card_field.value_type = static_cast<int>(Int(card_entry, "ValueType"));
      card_field.damage_type = static_cast<int>(Int(card_entry, "damageType"));
      card_field.duration = Float(card_entry, "duration", -1.0f);
      card_field.show_as_percent = Bool(card_entry, "showAsPercent");
      card_field.scale_with_duration = Bool(card_entry, "scaleWithDuration");
      const PipNode* value = Child(card_entry, "Value");
      if (value && value->type == PipValueType::kString) {
        card_field.text_value = value->string;
      } else if (value && IsNumeric(value)) {
        card_field.number_value = value->type == PipValueType::kFloat
                                      ? value->number
                                      : static_cast<float>(value->integer);
      }
      next.card.emplace_back(std::move(card_field));
    }
  }
  const PipNode* components = Child(entry, "components");
  if (components && components->type == PipValueType::kArray) {
    for (uint32_t component_id : components->array) {
      const PipNode* component = Resolve(component_id);
      if (!component || component->type != PipValueType::kObject) continue;
      InventoryComponentState value;
      value.name = String(component, "text");
      value.count = static_cast<int32_t>(
          std::max<int64_t>(0, Int(component, "count")));
      value.tagged_for_search = Bool(component, "taggedForSearch");
      if (!value.name.empty()) next.components.emplace_back(std::move(value));
    }
  }
  *item = std::move(next);
  return true;
}

void PipboyDatabase::BuildSnapshotLocked(PipboySnapshot* out) const {
  const bool update_stats = out->domain_revisions[0] != domain_revisions_[0];
  const bool update_inventory = out->domain_revisions[1] != domain_revisions_[1];
  const bool update_data = out->domain_revisions[2] != domain_revisions_[2];
  const bool update_map = out->domain_revisions[3] != domain_revisions_[3];
  const bool update_radio = out->domain_revisions[4] != domain_revisions_[4];
  out->generation = generation_;
  out->revision = revision_;
  out->domain_revisions = domain_revisions_;
  out->connected = connected_;
  out->busy = busy_;
  const PipNode* root = Resolve(0);
  out->root_ready = root && root->type == PipValueType::kObject;
  out->server_identity = identity_;
  out->status = status_;

  const auto branch_is = [&](const char* name, PipValueType type) {
    const PipNode* branch = RootBranch(name);
    return branch && branch->type == type;
  };
  const PipNode* stats_branch = RootBranch("Stats");
  out->stats_ready = branch_is("PlayerInfo", PipValueType::kObject) &&
                     stats_branch &&
                     stats_branch->type == PipValueType::kObject &&
                     branch_is("Special", PipValueType::kArray);
  out->inventory_ready = branch_is("Inventory", PipValueType::kObject);
  out->data_ready = branch_is("Quests", PipValueType::kArray);
  out->map_ready = branch_is("Map", PipValueType::kObject);
  out->radio_ready = branch_is("Radio", PipValueType::kArray);
  const PipNode* active_effects = Child(stats_branch, "ActiveEffects");
  out->effects_ready = active_effects &&
                       active_effects->type == PipValueType::kArray;
  out->perks_ready = branch_is("Perks", PipValueType::kArray);
  out->general_stats_ready = branch_is("Log", PipValueType::kArray);
  out->status_ready = branch_is("Status", PipValueType::kObject);

  if (update_stats) {
    out->player_name.clear();
    out->hp = out->max_hp = out->ap = out->max_ap = 0.0f;
    out->carry_weight = out->max_weight = out->xp_progress = 0.0f;
    out->caps = out->level = out->perk_points = 0;
    out->stimpaks = out->radaway = 0;
    out->special = {};
    out->special_modifiers = {};
    out->special_names = {};
    out->special_descriptions = {};
    out->limb_condition = {};
    out->total_damage = {};
    out->total_resistance = {};
    out->body_flags = out->head_flags = 0;
    out->effects.clear();
    out->perks.clear();
    out->general_stats.clear();
    out->is_loading = false;
    out->is_player_dead = false;
    out->is_in_dialogue = false;
    out->is_menu_open = false;
    out->is_movement_locked = false;
    out->is_pipboy_locked = false;
    out->is_data_unavailable = false;
    out->is_in_animation = false;
    out->is_in_vats = false;
    out->is_in_vats_playback = false;
    out->is_pipboy_not_equipped = false;

    const PipNode* player = RootBranch("PlayerInfo");
    out->player_name = String(player, "PlayerName");
    out->hp = Float(player, "CurrHP");
    out->max_hp = Float(player, "MaxHP");
    out->ap = Float(player, "CurrAP");
    out->max_ap = Float(player, "MaxAP");
    out->carry_weight = Float(player, "CurrWeight");
    out->max_weight = Float(player, "MaxWeight");
    out->caps =
        static_cast<uint32_t>(std::max<int64_t>(0, Int(player, "Caps")));
    out->level = static_cast<uint32_t>(
        std::max<int64_t>(0, Int(player, "XPLevel")));
    out->xp_progress = Float(player, "XPProgressPct");
    out->perk_points = static_cast<uint32_t>(
        std::max<int64_t>(0, Int(player, "PerkPoints")));

    const auto read_damage_values =
        [&](const PipNode* values, std::array<float, 6>* destination) {
          if (!destination || !values ||
              values->type != PipValueType::kArray) {
            return;
          }
          for (uint32_t id : values->array) {
            const PipNode* entry = Resolve(id);
            const int64_t type = Int(entry, "type");
            if (type < 1 || type > static_cast<int64_t>(destination->size()))
              continue;
            (*destination)[static_cast<size_t>(type - 1)] =
                Float(entry, "Value");
          }
        };
    read_damage_values(Child(player, "TotalDamages"), &out->total_damage);
    read_damage_values(Child(player, "TotalResists"),
                       &out->total_resistance);

    const PipNode* special = RootBranch("Special");
    if (special && special->type == PipValueType::kArray) {
      for (size_t i = 0;
           i < special->array.size() && i < out->special.size(); ++i) {
        const PipNode* entry = Resolve(special->array[i]);
        const char* value_key = Child(entry, "value") ? "value" : "Value";
        const char* modifier_key =
            Child(entry, "modifier") ? "modifier" : "Modifier";
        out->special[i] = static_cast<int32_t>(Int(entry, value_key));
        out->special_modifiers[i] =
            static_cast<int32_t>(Int(entry, modifier_key));
        out->special_names[i] = String(entry, "text");
        if (out->special_names[i].empty())
          out->special_names[i] = String(entry, "Name");
        out->special_descriptions[i] = String(entry, "description");
        if (out->special_descriptions[i].empty())
          out->special_descriptions[i] = String(entry, "Description");
      }
    }

    const PipNode* stats = stats_branch;
    const char* limbs[] = {"HeadCondition", "TorsoCondition",
                           "LArmCondition", "RArmCondition",
                           "LLegCondition", "RLegCondition"};
    for (size_t i = 0; i < out->limb_condition.size(); ++i)
      out->limb_condition[i] = Float(stats, limbs[i]);
    out->body_flags = static_cast<uint32_t>(Int(stats, "BodyFlags"));
    out->head_flags = static_cast<uint32_t>(Int(stats, "HeadFlags"));
    out->stimpaks = static_cast<uint32_t>(
        std::max<int64_t>(0, Int(stats, "StimpakCount")));
    out->radaway = static_cast<uint32_t>(
        std::max<int64_t>(0, Int(stats, "RadawayCount")));

    const PipNode* effects = Child(stats, "ActiveEffects");
    if (effects && effects->type == PipValueType::kArray) {
      for (uint32_t source_id : effects->array) {
        const PipNode* source = Resolve(source_id);
        if (!source || source->type != PipValueType::kObject) continue;
        std::string source_name = String(source, "Source");
        if (source_name.empty()) source_name = String(source, "text");
        const uint32_t effect_type =
            static_cast<uint32_t>(std::max<int64_t>(0, Int(source, "type")));
        const PipNode* effect_list = Child(source, "Effects");
        if (effect_list && effect_list->type == PipValueType::kArray) {
          for (uint32_t effect_id : effect_list->array) {
            const PipNode* entry = Resolve(effect_id);
            if (!entry || entry->type != PipValueType::kObject) continue;
            EffectState effect;
            effect.node_id = effect_id;
            effect.type = effect_type;
            effect.source = source_name;
            effect.name = String(entry, "Name");
            if (effect.name.empty()) effect.name = String(entry, "text");
            effect.magnitude = Float(entry, "Value");
            effect.duration = Float(entry, "duration", -1.0f);
            if (effect.duration < 0.0f)
              effect.duration = Float(entry, "Duration", -1.0f);
            effect.show_as_percent = Bool(entry, "showAsPercent");
            effect.custom_description = Bool(entry, "CustomDesc") ||
                                        Bool(entry, "usesCustomDesc");
            effect.detrimental = Bool(entry, "detrimental") ||
                                 effect.magnitude < 0.0f;
            out->effects.emplace_back(std::move(effect));
          }
        } else {
          // Accept the older flat representation used by some companion
          // captures while preferring Fallout 4's real nested shape above.
          EffectState effect;
          effect.node_id = source_id;
          effect.type = effect_type;
          effect.source = source_name;
          effect.name = String(source, "Name");
          if (effect.name.empty()) effect.name = source_name;
          effect.magnitude = Float(source, "Value");
          effect.duration = Float(source, "duration", -1.0f);
          if (effect.duration < 0.0f)
            effect.duration = Float(source, "Duration", -1.0f);
          effect.show_as_percent = Bool(source, "showAsPercent");
          effect.custom_description = Bool(source, "CustomDesc") ||
                                      Bool(source, "usesCustomDesc");
          effect.detrimental = Bool(source, "detrimental") ||
                               effect.magnitude < 0.0f;
          if (!effect.name.empty()) out->effects.emplace_back(std::move(effect));
        }
      }
    }

    const PipNode* perks = RootBranch("Perks");
    if (perks && perks->type == PipValueType::kArray) {
      for (uint32_t id : perks->array) {
        const PipNode* entry = Resolve(id);
        if (!entry || entry->type != PipValueType::kObject) continue;
        const char* rank_key = Child(entry, "rank") ? "rank" : "Rank";
        const uint32_t rank = static_cast<uint32_t>(
            std::max<int64_t>(0, Int(entry, rank_key)));
        if (!rank || !Bool(entry, "ListVisible", true)) continue;
        PipPerkState perk;
        perk.node_id = id;
        perk.form_id = static_cast<uint32_t>(Int(entry, "formID"));
        if (!perk.form_id)
          perk.form_id = static_cast<uint32_t>(Int(entry, "FormID"));
        perk.clip_name = static_cast<uint32_t>(Int(entry, "clipName"));
        if (!perk.clip_name)
          perk.clip_name = static_cast<uint32_t>(Int(entry, "ClipName"));
        // Bethesda's shipped companion data (including DemoMode.bin) calls
        // this field `Clip`. Without this fallback acquired perks have no
        // artwork key even though the native data is present.
        if (!perk.clip_name)
          perk.clip_name = static_cast<uint32_t>(Int(entry, "Clip"));
        perk.rank = rank;
        const char* max_rank_key =
            Child(entry, "maxRank") ? "maxRank" : "MaxRank";
        perk.max_rank = static_cast<uint32_t>(
            std::max<int64_t>(rank, Int(entry, max_rank_key, rank)));
        perk.name = String(entry, "text");
        if (perk.name.empty()) perk.name = String(entry, "Name");
        const PipNode* descriptions = Child(entry, "descriptions");
        if (!descriptions) descriptions = Child(entry, "Perks");
        if (descriptions && descriptions->type == PipValueType::kArray &&
            !descriptions->array.empty()) {
          const size_t index = std::min<size_t>(
              static_cast<size_t>(rank - 1), descriptions->array.size() - 1);
          const PipNode* description = Resolve(descriptions->array[index]);
          if (description && description->type == PipValueType::kString) {
            perk.description = description->string;
          } else {
            perk.description = String(description, "description");
            if (perk.description.empty())
              perk.description = String(description, "Description");
          }
        }
        if (perk.description.empty()) {
          perk.description = String(entry, "description");
        }
        if (perk.description.empty()) {
          perk.description = String(entry, "Description");
        }
        perk.swf_file = String(entry, "SWFFile");
        if (perk.swf_file.empty())
          perk.swf_file = String(entry, "SwfFile");
        if (!perk.name.empty()) out->perks.emplace_back(std::move(perk));
      }
    }

    const PipNode* log = RootBranch("Log");
    if (log && log->type == PipValueType::kArray) {
      for (uint32_t category_id : log->array) {
        const PipNode* category = Resolve(category_id);
        if (!category || category->type != PipValueType::kObject) continue;
        const std::string category_name = String(category, "text");
        const PipNode* stats_list = Child(category, "statArray");
        if (!stats_list || stats_list->type != PipValueType::kArray) continue;
        for (uint32_t stat_id : stats_list->array) {
          const PipNode* entry = Resolve(stat_id);
          if (!entry || entry->type != PipValueType::kObject) continue;
          PipGeneralStatState stat;
          stat.node_id = stat_id;
          stat.category = category_name;
          stat.name = String(entry, "text");
          stat.value = Float(entry, "Value");
          stat.show_if_zero = Bool(entry, "showIfZero");
          if (!stat.name.empty())
            out->general_stats.emplace_back(std::move(stat));
        }
      }
    }

    const PipNode* status = RootBranch("Status");
    out->is_loading = Bool(status, "IsLoading");
    out->is_player_dead = Bool(status, "IsPlayerDead");
    out->is_in_dialogue = Bool(status, "IsPlayerInDialogue");
    out->is_menu_open = Bool(status, "IsMenuOpen");
    out->is_movement_locked = Bool(status, "IsPlayerMovementLocked");
    out->is_pipboy_locked = Bool(status, "IsPlayerPipboyLocked");
    out->is_data_unavailable = Bool(status, "IsDataUnavailable");
    out->is_in_animation = Bool(status, "IsInAnimation");
    out->is_in_vats = Bool(status, "IsInVats");
    out->is_in_vats_playback = Bool(status, "IsInVatsPlayback");
    out->is_pipboy_not_equipped = Bool(status, "IsPipboyNotEquipped");
  }

  if (update_inventory) {
    const PipNode* inventory = RootBranch("Inventory");
    bool rebuild_all = !out->inventory_initialized ||
                       out->inventory_structure_revision !=
                           inventory_structure_revision_ ||
                       out->inventory_change_serial > inventory_change_serial_;
    if (!rebuild_all && out->inventory_change_serial <
                            inventory_change_serial_) {
      if (inventory_changes_.empty() ||
          inventory_changes_.front().serial >
              out->inventory_change_serial + 1) {
        rebuild_all = true;
      }
    }
    if (!rebuild_all) {
      std::unordered_set<uint32_t> refreshed;
      for (const InventoryChange& change : inventory_changes_) {
        if (change.serial <= out->inventory_change_serial || !change.item_id ||
            !refreshed.insert(change.item_id).second) {
          continue;
        }
        auto existing = std::find_if(
            out->items.begin(), out->items.end(),
            [&](const InventoryItemState& item) {
              return item.node_id == change.item_id;
            });
        InventoryItemState item;
        if (existing == out->items.end() ||
            !BuildInventoryItem(change.item_id, &item)) {
          rebuild_all = true;
          break;
        }
        *existing = std::move(item);
        ++out->inventory_incremental_refreshes;
      }
    }
    if (rebuild_all) {
      ++out->inventory_full_rebuilds;
      out->items.clear();
      std::unordered_set<uint32_t> seen_items;
      if (inventory && inventory->type == PipValueType::kObject) {
        for (const auto& field : inventory->object) {
          const PipNode* list = Resolve(field.second);
          if (!list || list->type != PipValueType::kArray) continue;
          for (uint32_t id : list->array) {
            if (!seen_items.insert(id).second) continue;
            InventoryItemState item;
            if (BuildInventoryItem(id, &item))
              out->items.emplace_back(std::move(item));
          }
        }
      }
    }
    out->inventory_version = static_cast<uint32_t>(
        std::max<int64_t>(0, Int(inventory, "Version")));
    out->inventory_initialized = true;
    out->inventory_structure_revision = inventory_structure_revision_;
    out->inventory_change_serial = inventory_change_serial_;
  }

  if (update_data) {
  out->quests.clear();
  out->workshop_count = 0;
  const PipNode* quests = RootBranch("Quests");
  if (quests && quests->type == PipValueType::kArray) {
    for (uint32_t id : quests->array) {
      const PipNode* entry = Resolve(id);
      if (!entry || entry->type != PipValueType::kObject) continue;
      QuestState quest;
      quest.node_id = id;
      quest.form_id = static_cast<uint32_t>(Int(entry, "formID"));
      quest.instance = static_cast<int32_t>(Int(entry, "instance"));
      quest.type = static_cast<int32_t>(Int(entry, "type"));
      quest.active = Bool(entry, "active");
      quest.enabled = Bool(entry, "enabled");
      quest.name = String(entry, "text");
      quest.description = String(entry, "desc");
      const PipNode* objectives = Child(entry, "objectives");
      if (objectives && objectives->type == PipValueType::kArray) {
        for (uint32_t objective_id : objectives->array) {
          const PipNode* objective = Resolve(objective_id);
          QuestObjectiveState state;
          state.node_id = objective_id;
          state.form_id = static_cast<uint32_t>(Int(objective, "formID"));
          state.instance = static_cast<int32_t>(Int(objective, "instance"));
          state.text = String(objective, "text");
          state.enabled = Bool(objective, "enabled");
          state.completed = Bool(objective, "completed");
          state.failed = Bool(objective, "failed");
          // Real Fallout 4 objective rows omit `active`; tracking lives on the
          // parent quest. Treat its currently displayed, unfinished
          // objectives as tracked unless a producer supplies an explicit
          // objective-level value.
          state.active = Child(objective, "active")
                             ? Bool(objective, "active")
                             : quest.active && state.enabled &&
                                   !state.completed && !state.failed;
          quest.objectives.emplace_back(std::move(state));
        }
      }
      out->quests.emplace_back(std::move(quest));
    }
  }
  const PipNode* workshop = RootBranch("Workshop");
  if (workshop && workshop->type == PipValueType::kArray)
    out->workshop_count = static_cast<uint32_t>(workshop->array.size());
  }

  if (update_map) {
  out->markers.clear();
  out->current_cell.clear();
  out->current_worldspace.clear();
  out->player_x = out->player_y = out->player_rotation = 0.0f;
  out->local_player_valid = false;
  out->local_player_x = out->local_player_y = out->local_player_rotation = 0.0f;
  const PipNode* map = RootBranch("Map");
  out->current_cell = String(map, "CurrCell");
  out->current_worldspace = String(map, "CurrWorldspace");
  const PipNode* world = Child(map, "World");
  const PipNode* world_player = Child(world, "Player");
  out->player_x = Float(world_player, "X");
  out->player_y = Float(world_player, "Y");
  out->player_rotation = Float(world_player, "Rotation");
  const PipNode* local = Child(map, "Local");
  const PipNode* local_player = Child(local, "Player");
  out->local_player_valid = local_player &&
                            Child(local_player, "X") &&
                            Child(local_player, "Y");
  out->local_player_x = Float(local_player, "X");
  out->local_player_y = Float(local_player, "Y");
  out->local_player_rotation = Float(local_player, "Rotation");
  const auto append_markers = [&](const PipNode* container,
                                  MapMarkerState::Kind kind, bool local_map) {
    if (!container) return;
    std::vector<uint32_t> ids;
    if (container->type == PipValueType::kArray) {
      ids = container->array;
    } else if (container->type == PipValueType::kObject) {
      // Custom and power-armor markers are single objects rather than arrays.
      // Their own node ID is not otherwise retained on PipNode, so resolve the
      // ID through their parent field below instead of entering this branch.
      return;
    }
    for (uint32_t id : ids) {
      const PipNode* entry = Resolve(id);
      if (!entry || entry->type != PipValueType::kObject) continue;
      MapMarkerState marker;
      marker.node_id = id;
      marker.form_id = static_cast<uint32_t>(Int(entry, "LocationFormId"));
      if (!marker.form_id)
        marker.form_id = static_cast<uint32_t>(Int(entry, "LocationMarkerFormId"));
      marker.type = static_cast<int32_t>(Int(entry, "type"));
      marker.kind = kind;
      marker.local = local_map;
      marker.on_door = Bool(entry, "OnDoor");
      // Location rows carry an authoritative Visible flag. Quest targets,
      // doors, custom markers, and power-armor records in real companion
      // trees may omit it entirely; their presence is the visibility signal.
      marker.visible = Bool(entry, "Visible",
                            kind != MapMarkerState::Kind::kLocation);
      marker.discovered = Bool(entry, "Discovered");
      marker.cleared = Bool(entry, "ClearedStatus");
      marker.x = Float(entry, "X");
      marker.y = Float(entry, "Y");
      marker.height = Float(entry, "Height");
      marker.name = String(entry, "Name");
      out->markers.emplace_back(std::move(marker));
    }
  };
  const auto append_single_marker = [&](const PipNode* parent, const char* key,
                                        MapMarkerState::Kind kind,
                                        bool local_map) {
    if (!parent) return;
    const PipNode* marker_node = Child(parent, key);
    if (!marker_node || marker_node->type != PipValueType::kObject) return;
    uint32_t id = 0;
    for (const auto& field : parent->object) {
      if (field.first == key) {
        id = field.second;
        break;
      }
    }
    if (!id) return;
    MapMarkerState marker;
    marker.node_id = id;
    marker.kind = kind;
    marker.local = local_map;
    marker.on_door = Bool(marker_node, "OnDoor");
    marker.visible = Bool(marker_node, "Visible",
                          kind != MapMarkerState::Kind::kLocation);
    marker.discovered = Bool(marker_node, "Discovered");
    marker.cleared = Bool(marker_node, "ClearedStatus");
    marker.type = static_cast<int32_t>(Int(marker_node, "type"));
    marker.x = Float(marker_node, "X");
    marker.y = Float(marker_node, "Y");
    marker.height = Float(marker_node, "Height");
    marker.name = String(marker_node, "Name");
    out->markers.emplace_back(std::move(marker));
  };
  append_markers(Child(world, "Locations"), MapMarkerState::Kind::kLocation,
                 false);
  append_markers(Child(world, "Quests"), MapMarkerState::Kind::kQuest, false);
  append_single_marker(world, "Custom", MapMarkerState::Kind::kCustom, false);
  append_single_marker(world, "PowerArmor",
                       MapMarkerState::Kind::kPowerArmor, false);
  append_markers(Child(local, "Doors"), MapMarkerState::Kind::kDoor, true);
  append_markers(Child(local, "Quests"), MapMarkerState::Kind::kQuest, true);
  append_single_marker(local, "Custom", MapMarkerState::Kind::kCustom, true);
  append_single_marker(local, "PowerArmor",
                       MapMarkerState::Kind::kPowerArmor, true);
  }

  if (update_radio) {
  out->radio.clear();
  const PipNode* radio = RootBranch("Radio");
  if (radio && radio->type == PipValueType::kArray) {
    for (uint32_t id : radio->array) {
      const PipNode* entry = Resolve(id);
      RadioState station;
      station.node_id = id;
      station.active = Bool(entry, "active");
      station.in_range = Bool(entry, "inRange");
      station.frequency = Float(entry, "frequency");
      station.name = String(entry, "text");
      out->radio.emplace_back(std::move(station));
    }
  }
  }
}

PipboySnapshot PipboyDatabase::Capture() const {
  const uint64_t begin = GetTickCount64();
  PipboySnapshot snapshot;
  size_t nodes = 0;
  size_t retained = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    BuildSnapshotLocked(&snapshot);
    nodes = nodes_.size();
    retained = node_bytes_ + local_map_.pixels.capacity();
  }
  const uint64_t elapsed = GetTickCount64() - begin;
  if (elapsed >= 10) {
    LogRateLimited("snapshot-full-slow", 5000, LogLevel::kWarning,
                   "[PERF] full snapshot ms=%llu nodes=%zu retained=%zu",
                   static_cast<unsigned long long>(elapsed), nodes, retained);
  }
  return snapshot;
}

bool PipboyDatabase::CaptureIfChanged(uint64_t revision,
                                      PipboySnapshot* snapshot) const {
  if (!snapshot) return false;
  const uint64_t begin = GetTickCount64();
  size_t nodes = 0;
  size_t retained = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (revision == revision_) return false;
    BuildSnapshotLocked(snapshot);
    nodes = nodes_.size();
    retained = node_bytes_ + local_map_.pixels.capacity();
  }
  const uint64_t elapsed = GetTickCount64() - begin;
  if (elapsed >= 10) {
    LogRateLimited("snapshot-incremental-slow", 5000, LogLevel::kWarning,
                   "[PERF] incremental snapshot ms=%llu nodes=%zu retained=%zu",
                   static_cast<unsigned long long>(elapsed), nodes, retained);
  }
  return true;
}

LocalMapState PipboyDatabase::CaptureLocalMap() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return local_map_;
}

size_t PipboyDatabase::NodeCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return nodes_.size();
}

size_t PipboyDatabase::RetainedBytes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return node_bytes_ + local_map_.pixels.capacity();
}

PipboyClient::PipboyClient(PipboyDatabase* database, uint16_t port)
    : database_(database), port_(port) {}

PipboyClient::~PipboyClient() { Stop(); }

bool PipboyClient::Start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true)) return true;
  try {
    thread_ = std::thread(&PipboyClient::Run, this);
    return true;
  } catch (...) {
    running_ = false;
    return false;
  }
}

void PipboyClient::Stop() {
  running_.store(false);
  const SOCKET active = socket_.load();
  if (active != INVALID_SOCKET) shutdown(active, SD_BOTH);
  if (thread_.joinable()) thread_.join();
  Close("Stopped");
}

bool PipboyClient::SendCommand(uint32_t type, const std::string& json_args) {
  if (!running_ || json_args.empty() || json_args.size() > 65536) return false;
  const uint32_t id = next_command_id_.fetch_add(1);
  std::ostringstream json;
  json << "{\"type\":" << type << ",\"args\":" << json_args
       << ",\"id\":" << id << "}";
  std::lock_guard<std::mutex> lock(command_mutex_);
  if (commands_.size() >= 64) return false;
  commands_.push_back({command_epoch_, json.str()});
  Log(LogLevel::kInfo, "[ACTION] queued request id=%u type=%u", id, type);
  return true;
}

void PipboyClient::DiscardCommands(const char* reason) {
  std::lock_guard<std::mutex> lock(command_mutex_);
  ++command_epoch_;
  if (!commands_.empty()) {
    Log(LogLevel::kWarning,
        "[ACTION] discarded %zu queued request(s) reason=%s",
        commands_.size(), reason && *reason ? reason : "state invalidated");
  }
  commands_.clear();
}

void PipboyClient::Run() {
  WSADATA winsock = {};
  if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
    database_->SetConnection(false, false, {}, "Winsock initialization failed");
    running_ = false;
    return;
  }
  Log(LogLevel::kInfo, "[PIPBOY] client thread started target=127.0.0.1:%u",
      port_);
  try {
    while (running_) {
      if (socket_.load() == INVALID_SOCKET && !Connect()) {
        for (int i = 0; i < 8 && running_; ++i) Sleep(250);
        continue;
      }
      const SOCKET active = socket_.load();
      if (active == INVALID_SOCKET) continue;
      fd_set readable;
      FD_ZERO(&readable);
      FD_SET(active, &readable);
      timeval timeout = {};
      timeout.tv_usec = 100000;
      int ready = select(0, &readable, nullptr, nullptr, &timeout);
      if (ready == SOCKET_ERROR || (ready > 0 && !ReceiveOne())) {
        Close("Companion connection lost");
        continue;
      }
      DrainCommands();
    }
  } catch (const std::exception& exception) {
    Log(LogLevel::kError, "[PIPBOY] client exception: %.512s", exception.what());
    database_->SetConnection(false, false, {}, "Companion client exception");
  } catch (...) {
    Log(LogLevel::kError, "[PIPBOY] unknown client exception");
    database_->SetConnection(false, false, {}, "Companion client exception");
  }
  Close("Stopped");
  WSACleanup();
  Log(LogLevel::kInfo, "[PIPBOY] client thread stopped");
}

bool PipboyClient::Connect() {
  SOCKET candidate = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (candidate == INVALID_SOCKET) return false;
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_port = htons(port_);
  inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
  if (connect(candidate, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ==
      SOCKET_ERROR) {
    const int error = WSAGetLastError();
    closesocket(candidate);
    LogRateLimited("pipboy-connect", 5000, LogLevel::kWarning,
                   "[PIPBOY] connect pending error=%d; enable companion service",
                   error);
    database_->SetConnection(false, false, {}, "Waiting for Pip-Boy data service");
    return false;
  }
  const DWORD io_timeout_ms = 2000;
  if (setsockopt(candidate, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&io_timeout_ms),
                 sizeof(io_timeout_ms)) == SOCKET_ERROR ||
      setsockopt(candidate, SOL_SOCKET, SO_SNDTIMEO,
                 reinterpret_cast<const char*>(&io_timeout_ms),
                 sizeof(io_timeout_ms)) == SOCKET_ERROR) {
    const int error = WSAGetLastError();
    closesocket(candidate);
    Log(LogLevel::kWarning,
        "[PIPBOY] could not set socket deadlines error=%d", error);
    database_->SetConnection(false, false, {}, "Companion deadline setup failed");
    return false;
  }
  socket_.store(candidate);
  database_->SetConnection(true, false, {}, "Connected; waiting for root data");
  Log(LogLevel::kInfo, "[PIPBOY] TCP connection established");
  return true;
}

bool PipboyClient::ReceiveExact(void* output, size_t size) {
  uint8_t* target = static_cast<uint8_t*>(output);
  size_t received = 0;
  while (received < size && running_) {
    const SOCKET active = socket_.load();
    if (active == INVALID_SOCKET) return false;
    int chunk = recv(active, reinterpret_cast<char*>(target + received),
                     static_cast<int>(std::min<size_t>(size - received, INT_MAX)), 0);
    if (chunk <= 0) return false;
    received += static_cast<size_t>(chunk);
  }
  return received == size;
}

bool PipboyClient::ReceiveOne() {
  uint32_t size = 0;
  uint8_t type = 0;
  if (!ReceiveExact(&size, sizeof(size)) || !ReceiveExact(&type, sizeof(type))) return false;
  if (size > kMaxFrameBytes) {
    Log(LogLevel::kError, "[PIPBOY] rejected oversized frame type=%u bytes=%u",
        type, size);
    return false;
  }
  std::vector<uint8_t> payload(size);
  if (size && !ReceiveExact(payload.data(), payload.size())) return false;
  if (type == 0) return SendFrame(0, nullptr, 0);
  if (type == 1) {
    std::string identity(payload.begin(), payload.end());
    database_->SetConnection(true, false, identity, "Connected; waiting for root data");
    Log(LogLevel::kInfo, "[PIPBOY] server identity %.512s", identity.c_str());
    return true;
  }
  if (type == 2) {
    database_->SetConnection(true, true, {}, "Pip-Boy data service is busy");
    Log(LogLevel::kWarning, "[PIPBOY] server busy; another client is connected");
    return true;
  }
  if (type == 3) {
    const uint64_t begin = GetTickCount64();
    std::string error;
    const bool ok = database_->ApplyDataUpdate(payload.data(), payload.size(), &error);
    const uint64_t elapsed = GetTickCount64() - begin;
    if (!ok) {
      Log(LogLevel::kError, "[PIPBOY] data frame rejected bytes=%zu reason=%s",
          payload.size(), error.c_str());
      return false;
    }
    if (elapsed >= 10)
      LogRateLimited("data-update-slow", 5000, LogLevel::kWarning,
                     "[PERF] data update bytes=%zu nodes=%zu retained=%zu ms=%llu",
                     payload.size(), database_->NodeCount(),
                     database_->RetainedBytes(),
                     static_cast<unsigned long long>(elapsed));
    return true;
  }
  if (type == 4) {
    std::string error;
    if (!database_->ApplyLocalMap(payload.data(), payload.size(), &error)) {
      Log(LogLevel::kError, "[MAP] local update rejected: %s", error.c_str());
      return false;
    }
    return true;
  }
  if (type == 6) {
    std::string response(payload.begin(), payload.end());
    Log(LogLevel::kInfo, "[ACTION] response %.1024s", response.c_str());
    return true;
  }
  LogRateLimited("pipboy-unknown-message", 5000, LogLevel::kWarning,
                 "[PIPBOY] ignored message type=%u bytes=%u", type, size);
  return true;
}

bool PipboyClient::SendFrame(uint8_t type, const void* data, uint32_t size) {
  const SOCKET active = socket_.load();
  if (active == INVALID_SOCKET) return false;
  std::vector<uint8_t> frame(sizeof(size) + sizeof(type) + size);
  std::memcpy(frame.data(), &size, sizeof(size));
  frame[sizeof(size)] = type;
  if (size) std::memcpy(frame.data() + sizeof(size) + sizeof(type), data, size);
  size_t sent = 0;
  while (sent < frame.size()) {
    int chunk = send(active, reinterpret_cast<const char*>(frame.data() + sent),
                     static_cast<int>(frame.size() - sent), 0);
    if (chunk <= 0) return false;
    sent += static_cast<size_t>(chunk);
  }
  return true;
}

void PipboyClient::DrainCommands() {
  std::vector<QueuedCommand> pending;
  {
    std::lock_guard<std::mutex> lock(command_mutex_);
    pending.swap(commands_);
  }
  for (const QueuedCommand& command : pending) {
    bool sent = false;
    bool stale = false;
    uint64_t current_epoch = 0;
    {
      // Serialize the final epoch check with DiscardCommands. A lifecycle
      // invalidation that wins this lock makes every already-swapped request
      // stale before any bytes are submitted to the companion channel.
      std::lock_guard<std::mutex> lock(command_mutex_);
      current_epoch = command_epoch_;
      stale = command.epoch != current_epoch;
      if (!stale) {
        sent = SendFrame(5, command.json.data(),
                         static_cast<uint32_t>(command.json.size()));
      }
    }
    if (stale) {
      Log(LogLevel::kWarning,
          "[ACTION] skipped stale request epoch=%llu current=%llu",
          static_cast<unsigned long long>(command.epoch),
          static_cast<unsigned long long>(current_epoch));
      continue;
    }
    if (!sent) {
      Log(LogLevel::kError, "[ACTION] send failed");
      Close("Action channel failed");
      return;
    }
  }
}

void PipboyClient::Close(const char* reason) {
  const SOCKET old = socket_.exchange(INVALID_SOCKET);
  if (old != INVALID_SOCKET) closesocket(old);
  DiscardCommands("disconnect");
  if (database_) database_->SetConnection(false, false, {}, NarrowStatus(reason));
}

}  // namespace fo4ds
