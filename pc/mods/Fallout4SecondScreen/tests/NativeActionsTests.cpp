#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstring>
#include <cstdint>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include "NativeActions.h"

namespace {

fo4ds::NativeActionRequest Request(uint64_t token) {
  fo4ds::NativeActionRequest request;
  request.token = token;
  request.kind = fo4ds::NativeActionKind::kUseInventoryItem;
  request.item.handle_id = static_cast<uint32_t>(token);
  request.item.stack_id = static_cast<uint32_t>(token % 3);
  request.item.inventory_version = 42;
  return request;
}

fo4ds::NativeActionResult Result(const fo4ds::NativeActionRequest& request) {
  fo4ds::NativeActionResult result;
  result.token = request.token;
  result.kind = request.kind;
  result.code = fo4ds::NativeActionResultCode::kSucceeded;
  result.state = true;
  return result;
}

void TestExactImageAndPrologueGates() {
  constexpr char upper[] =
      "061C1AB3738612DBC46D748F02C27795A1003501C90C15D1E649D8A07FCBBB4E";
  constexpr char lower[] =
      "061c1ab3738612dbc46d748f02c27795a1003501c90c15d1e649d8a07fcbbb4e";
  assert(fo4ds::IsNativeActionImageHash(upper));
  assert(fo4ds::IsNativeActionImageHash(lower));
  assert(!fo4ds::IsNativeActionImageHash(nullptr));
  assert(!fo4ds::IsNativeActionImageHash("061C"));
  std::array<char, sizeof(upper)> wrong = {};
  std::copy(std::begin(upper), std::end(upper), wrong.begin());
  wrong[0] = '1';
  assert(!fo4ds::IsNativeActionImageHash(wrong.data()));

  const std::array<uint8_t, 7> prologue = {{
      0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08,
  }};
  assert(fo4ds::NativeActionPumpPrologueMatches(prologue.data(),
                                                prologue.size()));
  assert(!fo4ds::NativeActionPumpPrologueMatches(prologue.data(), 6));
  auto detoured = prologue;
  detoured[0] = 0xE9;
  assert(!fo4ds::NativeActionPumpPrologueMatches(detoured.data(),
                                                 detoured.size()));

  std::array<uint8_t, 7> patch = {};
  assert(fo4ds::BuildNativeActionPumpPatch(0x10000000, 0x10001000,
                                           &patch));
  assert(patch[0] == 0xE9 && patch[5] == 0x90 && patch[6] == 0x90);
  int32_t displacement = 0;
  std::memcpy(&displacement, patch.data() + 1, sizeof(displacement));
  assert(displacement == 0xFFB);
  assert(!fo4ds::BuildNativeActionPumpPatch(
      0x10000000, static_cast<uintptr_t>(0x10000000ull + 0x80000005ull),
      &patch));
  assert(!fo4ds::BuildNativeActionPumpPatch(0, 0, nullptr));

  uintptr_t relocated = 0;
  assert(fo4ds::RelocateNativeActionPumpPc(0x1000, 0x2000, 0x1000,
                                           &relocated));
  assert(relocated == 0x2000);
  assert(fo4ds::RelocateNativeActionPumpPc(0x1000, 0x2000, 0x1003,
                                           &relocated));
  assert(relocated == 0x2003);
  assert(!fo4ds::RelocateNativeActionPumpPc(0x1000, 0x2000, 0x1001,
                                            &relocated));
  assert(!fo4ds::RelocateNativeActionPumpPc(0x1000, 0x2000, 0x1007,
                                            &relocated));
  assert(!fo4ds::RelocateNativeActionPumpPc(0x1000, 0x2000, 0x1000,
                                            nullptr));
}

void TestLabels() {
  assert(std::string(fo4ds::NativeActionInstallStatusLabel(
             fo4ds::NativeActionInstallStatus::kDisabledByConfiguration)) ==
         "disabled-by-configuration");
  assert(std::string(fo4ds::NativeActionInstallStatusLabel(
             fo4ds::NativeActionInstallStatus::kReady)) == "ready");
  assert(std::string(fo4ds::NativeActionInstallStatusLabel(
             static_cast<fo4ds::NativeActionInstallStatus>(255))) == "unknown");
  assert(std::string(fo4ds::NativeActionResultCodeLabel(
             fo4ds::NativeActionResultCode::kStaleInventoryVersion)) ==
         "stale-inventory-version");
  assert(std::string(fo4ds::NativeActionResultCodeLabel(
             static_cast<fo4ds::NativeActionResultCode>(255))) == "unknown");
}

void TestQueueCapacityAndFifo() {
  fo4ds::NativeActionQueue queue;
  for (size_t index = 0; index < fo4ds::kNativeActionQueueCapacity; ++index)
    assert(queue.Enqueue(Request(index + 1)));
  assert(!queue.Enqueue(Request(1000)));
  assert(queue.Outstanding() == fo4ds::kNativeActionQueueCapacity);

  for (size_t index = 0; index < fo4ds::kNativeActionQueueCapacity; ++index) {
    fo4ds::NativeActionRequest request;
    assert(queue.Take(&request));
    assert(request.token == index + 1);
    assert(queue.Complete(Result(request)));
  }
  fo4ds::NativeActionRequest no_request;
  assert(!queue.Take(&no_request));
  assert(!queue.Complete(Result(Request(2000))));
  assert(!queue.Enqueue(Request(1001)));  // results still reserve all slots

  for (size_t index = 0; index < fo4ds::kNativeActionQueueCapacity; ++index) {
    fo4ds::NativeActionResult result;
    assert(queue.Poll(&result));
    assert(result.token == index + 1);
    assert(result.code == fo4ds::NativeActionResultCode::kSucceeded);
  }
  fo4ds::NativeActionResult no_result;
  assert(!queue.Poll(&no_result));
  assert(queue.Outstanding() == 0);
  assert(queue.Enqueue(Request(1002)));
}

void TestConcurrentQueue() {
  fo4ds::NativeActionQueue queue;
  constexpr uint64_t producer_count = 4;
  constexpr uint64_t per_producer = 250;
  constexpr uint64_t total = producer_count * per_producer;
  std::atomic<bool> start{false};
  std::atomic<uint64_t> producers_done{0};
  std::vector<std::thread> producers;
  producers.reserve(producer_count);
  for (uint64_t producer = 0; producer < producer_count; ++producer) {
    producers.emplace_back([&, producer] {
      while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
      for (uint64_t index = 0; index < per_producer; ++index) {
        const uint64_t token = producer * per_producer + index + 1;
        while (!queue.Enqueue(Request(token))) std::this_thread::yield();
      }
      producers_done.fetch_add(1, std::memory_order_release);
    });
  }

  std::array<bool, total + 1> seen{};
  std::thread pump([&] {
    while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
    uint64_t completed = 0;
    while (completed < total) {
      fo4ds::NativeActionRequest request;
      if (queue.Take(&request)) {
        assert(queue.Complete(Result(request)));
        ++completed;
      } else {
        std::this_thread::yield();
      }
    }
  });
  start.store(true, std::memory_order_release);

  uint64_t polled = 0;
  while (polled < total) {
    fo4ds::NativeActionResult result;
    if (!queue.Poll(&result)) {
      std::this_thread::yield();
      continue;
    }
    assert(result.token > 0 && result.token <= total);
    assert(!seen[result.token]);
    seen[result.token] = true;
    ++polled;
  }
  for (std::thread& producer : producers) producer.join();
  pump.join();
  assert(producers_done.load(std::memory_order_acquire) == producer_count);
  assert(queue.Outstanding() == 0);
  for (uint64_t token = 1; token <= total; ++token) assert(seen[token]);
}

void TestBackendRemainsDisabledWithoutExactHost() {
  assert(fo4ds::GetNativeActionInstallStatus() ==
         fo4ds::NativeActionInstallStatus::kNotInstalled);
  assert(!fo4ds::NativeActionsReady());
  assert(!fo4ds::QueueNativeAction(Request(1)));
  const auto disabled = fo4ds::ConfigureNativeActions(
      false, (std::numeric_limits<uintptr_t>::max)(), "not-a-hash");
  assert(disabled ==
         fo4ds::NativeActionInstallStatus::kDisabledByConfiguration);
  assert(fo4ds::GetNativeActionInstallStatus() ==
         fo4ds::NativeActionInstallStatus::kNotInstalled);
  assert(!fo4ds::NativeActionsReady());
  const auto status = fo4ds::ConfigureNativeActions(
      true,
      reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), "not-a-hash");
  assert(status == fo4ds::NativeActionInstallStatus::kUnsupportedImage);
  assert(!fo4ds::NativeActionsReady());
  assert(!fo4ds::QueueNativeAction(Request(2)));
}

}  // namespace

int main() {
  TestExactImageAndPrologueGates();
  TestLabels();
  TestQueueCapacityAndFifo();
  TestConcurrentQueue();
  TestBackendRemainsDisabledWithoutExactHost();
  return 0;
}
