#include <windows.h>

#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <unordered_map>

// Keep the low-level reader and explicit cross-ABI sink in the same translation
// unit so their internal contracts can be host-tested without exporting them.
#include "../src/NativePipboyBridge.cpp"

namespace {

template <class T, size_t N>
void Store(std::array<uint8_t, N>* bytes, size_t offset, const T& value) {
  assert(bytes && offset + sizeof(value) <= bytes->size());
  std::memcpy(bytes->data() + offset, &value, sizeof(value));
}

struct PoolString {
  std::array<uint8_t, 0x30> bytes{};

  explicit PoolString(const char* value) {
    const uint16_t flags = 1;
    const uint32_t length = static_cast<uint32_t>(std::strlen(value));
    Store(&bytes, 0x08, flags);
    Store(&bytes, 0x10, length);
    assert(0x18 + length < bytes.size());
    std::memcpy(bytes.data() + 0x18, value, length + 1);
  }

  uintptr_t address() { return reinterpret_cast<uintptr_t>(bytes.data()); }
};

void TestObjectTableLayoutAndTraversal() {
  const uintptr_t module_base =
      reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  std::array<uint8_t, 0x98> object{};
  std::array<uint8_t, 0x20> child{};
  std::array<uint8_t, 8 * 0x18> entries{};
  PoolString key("Value");

  const uintptr_t object_vtable = module_base + fo4ds::kObjectVtableRva;
  const uintptr_t uint_vtable = module_base + fo4ds::kUInt32VtableRva;
  const uint32_t object_id = 11;
  const uint32_t child_id = 12;
  const uint32_t child_value = 42;
  Store(&object, 0x00, object_vtable);
  Store(&object, 0x08, object_id);
  Store(&child, 0x00, uint_vtable);
  Store(&child, 0x08, child_id);
  Store(&child, 0x18, child_value);

  const uint32_t capacity = 8;
  const uint32_t free = 7;
  const uintptr_t sentinel = 0xEFBEADDEu;
  const uintptr_t entry_array =
      reinterpret_cast<uintptr_t>(entries.data());
  const uintptr_t padding_canary = 1;
  Store(&object, 0x24, capacity);
  Store(&object, 0x28, free);
  Store(&object, 0x30, sentinel);
  // +0x38 is allocator padding. A reader that regresses to the old offset will
  // try to use this deliberately invalid canary instead of the real +0x40 ptr.
  Store(&object, 0x38, padding_canary);
  Store(&object, 0x40, entry_array);

  const uintptr_t key_address = key.address();
  const uintptr_t child_address =
      reinterpret_cast<uintptr_t>(child.data());
  Store(&entries, 0x00, key_address);
  Store(&entries, 0x08, child_address);
  Store(&entries, 0x10, sentinel);

  std::unordered_map<uint32_t, fo4ds::PipNode> nodes;
  uint32_t root_id = 0;
  std::string error;
  fo4ds::NativeTreeReader reader(module_base);
  assert(reader.Copy(reinterpret_cast<uintptr_t>(object.data()), &nodes,
                     &root_id, &error));
  assert(root_id == object_id);
  assert(nodes.size() == 2);
  assert(nodes.at(child_id).type == fo4ds::PipValueType::kUInt32);
  assert(nodes.at(child_id).integer == child_value);
  assert(nodes.at(object_id).object.size() == 1);
  assert(nodes.at(object_id).object[0].first == "Value");
  assert(nodes.at(object_id).object[0].second == child_id);

  const uintptr_t invalid_chain = entry_array + 1;
  Store(&entries, 0x10, invalid_chain);
  assert(!reader.Copy(reinterpret_cast<uintptr_t>(object.data()), &nodes,
                      &root_id, &error));
}

void TestExplicitSinkVtable() {
  fo4ds::NativeGroupEventSink sink;
  sink.vtable = fo4ds::kSinkVtable.data();
  sink.dirty.store(false, std::memory_order_relaxed);

  using Destructor = fo4ds::NativeGroupEventSink* (*)(
      fo4ds::NativeGroupEventSink*, uint32_t);
  using ProcessEvent = int32_t (*)(fo4ds::NativeGroupEventSink*, const void*,
                                   void*);
  const auto destructor = reinterpret_cast<Destructor>(sink.vtable[0]);
  const auto process = reinterpret_cast<ProcessEvent>(sink.vtable[1]);
  assert(destructor(&sink, 0) == &sink);
  assert(process(&sink, nullptr, nullptr) == 0);
  assert(sink.dirty.load(std::memory_order_acquire));
}

void TestThrottledPrimitiveValues() {
  const uintptr_t module_base =
      reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  struct Case {
    uintptr_t vtable_rva;
    fo4ds::PipValueType type;
  };
  constexpr Case cases[] = {
      {fo4ds::kThrottledBoolVtableRva, fo4ds::PipValueType::kBoolean},
      {fo4ds::kThrottledUInt8VtableRva, fo4ds::PipValueType::kUInt8},
      {fo4ds::kThrottledFloatVtableRva, fo4ds::PipValueType::kFloat},
  };
  for (uint32_t index = 0; index < std::size(cases); ++index) {
    std::array<uint8_t, 0x20> value{};
    const uintptr_t vtable = module_base + cases[index].vtable_rva;
    const uint32_t id = 100 + index;
    Store(&value, 0x00, vtable);
    Store(&value, 0x08, id);
    if (cases[index].type == fo4ds::PipValueType::kBoolean) {
      const uint8_t payload = 1;
      Store(&value, 0x18, payload);
    } else if (cases[index].type == fo4ds::PipValueType::kUInt8) {
      const uint8_t payload = 73;
      Store(&value, 0x18, payload);
    } else {
      const float payload = 12.5f;
      Store(&value, 0x18, payload);
    }
    std::unordered_map<uint32_t, fo4ds::PipNode> nodes;
    uint32_t root_id = 0;
    std::string error;
    fo4ds::NativeTreeReader reader(module_base);
    assert(reader.Copy(reinterpret_cast<uintptr_t>(value.data()), &nodes,
                       &root_id, &error));
    assert(root_id == id);
    assert(nodes.at(id).type == cases[index].type);
    if (cases[index].type == fo4ds::PipValueType::kBoolean)
      assert(nodes.at(id).boolean);
    else if (cases[index].type == fo4ds::PipValueType::kUInt8)
      assert(nodes.at(id).integer == 73);
    else
      assert(std::fabs(nodes.at(id).number - 12.5f) < 0.001f);
  }
}

void TestBridgeLifecycleSerialization() {
  fo4ds::PipboyDatabase database;
  fo4ds::NativePipboyBridge bridge(&database);
  for (unsigned iteration = 0; iteration < 20; ++iteration) {
    std::thread first([&] { assert(bridge.Start()); });
    std::thread second([&] { bridge.Stop(); });
    first.join();
    second.join();
    bridge.Stop();
  }
}

}  // namespace

int main() {
  TestObjectTableLayoutAndTraversal();
  TestThrottledPrimitiveValues();
  TestExplicitSinkVtable();
  TestBridgeLifecycleSerialization();
  return 0;
}
