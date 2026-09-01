#include "Hook.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <utility>

#include "Log.h"
#include "Mem.h"

namespace dis {

HookSet::~HookSet() { RemoveAll(); }

bool HookSet::Add(void** vtable, uint32_t slot, void* replacement) {
  if (!vtable || !replacement) return false;
  void** entry = vtable + slot;
  if (!Readable(entry, sizeof(void*))) return false;

  std::lock_guard<std::mutex> guard(lock_);
  for (const Entry& e : entries_) {
    if (e.vtable == vtable && e.slot == slot) return false;
  }
  entries_.reserve(entries_.size() + 1);

  void* original = *entry;
  // Already holding what we were about to write. Installing anyway would make
  // the hook's saved "original" be the hook, and the first call would recurse
  // until the stack ran out.
  if (original == replacement) {
    DIS_WARN("[hook] slot %u of %p already holds %p; not hooking it twice", slot,
             static_cast<void*>(vtable), replacement);
    return false;
  }
  if (!Executable(original, 1)) {
    DIS_WARN("[hook] slot %u of %p does not hold code (%p)", slot,
             static_cast<void*>(vtable), original);
    return false;
  }

  // .rdata is read-only at run time, so the page has to be opened for the one
  // store and closed again. Leaving it writable would be a durable weakening of
  // the process for no gain.
  Unprotect open(entry, sizeof(void*));
  if (!open.ok()) {
    DIS_WARN("[hook] could not unprotect %p", static_cast<void*>(entry));
    return false;
  }
  *entry = replacement;
  Entry saved;
  saved.kind = Entry::Kind::kVTable;
  saved.vtable = vtable;
  saved.slot = slot;
  saved.original = original;
  saved.replacement = replacement;
  entries_.push_back(std::move(saved));
  return true;
}

bool HookSet::AddInline(void* target, const uint8_t* expected, size_t length,
                        void* replacement, void** trampoline) {
  if (!target || !expected || !replacement || !trampoline || length < 5) return false;
  if (!Executable(target, length) || std::memcmp(target, expected, length) != 0) {
    DIS_WARN("[hook] inline signature at %p does not match this build", target);
    return false;
  }

  std::lock_guard<std::mutex> guard(lock_);
  for (const Entry& e : entries_) {
    if (e.kind == Entry::Kind::kInline && e.target == target) return false;
  }
  entries_.reserve(entries_.size() + 1);

  auto* gateway = static_cast<uint8_t*>(VirtualAlloc(
      nullptr, length + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  if (!gateway) {
    DIS_WARN("[hook] could not allocate trampoline for %p", target);
    return false;
  }
  std::memcpy(gateway, target, length);
  gateway[length] = 0xE9;
  const uintptr_t resume = reinterpret_cast<uintptr_t>(target) + length;
  const uintptr_t from_gateway = reinterpret_cast<uintptr_t>(gateway) + length + 5;
  const uint32_t gateway_delta = static_cast<uint32_t>(resume - from_gateway);
  std::memcpy(gateway + length + 1, &gateway_delta, sizeof(gateway_delta));
  FlushInstructionCache(GetCurrentProcess(), gateway, length + 5);
  DWORD gateway_previous = 0;
  if (!VirtualProtect(gateway, length + 5, PAGE_EXECUTE_READ, &gateway_previous)) {
    VirtualFree(gateway, 0, MEM_RELEASE);
    DIS_WARN("[hook] could not seal trampoline for %p", target);
    return false;
  }

  // The replacement may run the instant the target's first byte changes. Its
  // original must therefore be visible before exposing the jump, never after.
  *trampoline = gateway;
  std::atomic_thread_fence(std::memory_order_release);

  Entry saved;
  saved.kind = Entry::Kind::kInline;
  saved.vtable = nullptr;
  saved.slot = 0;
  saved.original = nullptr;
  saved.replacement = replacement;
  saved.target = target;
  saved.trampoline = gateway;
  saved.bytes.assign(expected, expected + length);
  saved.patched_bytes.assign(length, 0x90);
  saved.patched_bytes[0] = 0xE9;
  const uintptr_t from_target = reinterpret_cast<uintptr_t>(target) + 5;
  const uint32_t target_delta = static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(replacement) - from_target);
  std::memcpy(saved.patched_bytes.data() + 1, &target_delta, sizeof(target_delta));

  Unprotect open(target, length);
  if (!open.ok()) {
    *trampoline = nullptr;
    VirtualFree(gateway, 0, MEM_RELEASE);
    DIS_WARN("[hook] could not unprotect inline target %p", target);
    return false;
  }
  auto* patch = static_cast<uint8_t*>(target);
  std::memcpy(patch, saved.patched_bytes.data(), length);
  FlushInstructionCache(GetCurrentProcess(), target, length);

  entries_.push_back(std::move(saved));
  return true;
}

void* HookSet::Original(void** vtable, uint32_t slot) const {
  std::lock_guard<std::mutex> guard(lock_);
  for (const Entry& e : entries_) {
    if (e.kind == Entry::Kind::kVTable && e.vtable == vtable && e.slot == slot) {
      return e.original;
    }
  }
  return nullptr;
}

size_t HookSet::count() const {
  std::lock_guard<std::mutex> guard(lock_);
  return entries_.size();
}

void HookSet::Rollback(size_t keep) {
  std::lock_guard<std::mutex> guard(lock_);
  if (keep > entries_.size()) keep = entries_.size();
  // Reverse order so a slot hooked twice by mistake still ends up holding the
  // value it had before this set touched it.
  for (size_t i = entries_.size(); i-- > keep;) {
    Entry& e = entries_[i];
    if (e.kind == Entry::Kind::kVTable) {
      void** entry = e.vtable + e.slot;
      // Do not overwrite a later hook installed by somebody else.
      if (!Readable(entry, sizeof(void*)) || *entry != e.replacement) {
        DIS_WARN("[hook] vtable slot %u at %p changed after installation; retaining it",
                 e.slot, static_cast<void*>(entry));
        entries_.resize(i + 1);
        return;
      }
      Unprotect open(entry, sizeof(void*));
      if (!open.ok()) {
        DIS_WARN("[hook] could not restore vtable slot %u at %p; retaining it",
                 e.slot, static_cast<void*>(entry));
        entries_.resize(i + 1);
        return;
      }
      *entry = e.original;
    } else {
      if (!Readable(e.target, e.patched_bytes.size()) ||
          std::memcmp(e.target, e.patched_bytes.data(), e.patched_bytes.size()) != 0) {
        DIS_WARN("[hook] inline target %p changed after installation; retaining it",
                 e.target);
        entries_.resize(i + 1);
        return;
      }
      Unprotect open(e.target, e.bytes.size());
      if (!open.ok()) {
        DIS_WARN("[hook] could not restore inline target %p; retaining it",
                 e.target);
        entries_.resize(i + 1);
        return;
      }
      std::memcpy(e.target, e.bytes.data(), e.bytes.size());
      FlushInstructionCache(GetCurrentProcess(), e.target, e.bytes.size());
      // Gateways are intentionally retained. A render thread may already be
      // executing one while the target is restored; freeing 13 bytes here is
      // not worth turning an explicit unload into a use-after-free.
    }
  }
  entries_.resize(keep);
}

void HookSet::RemoveAll() { Rollback(0); }

}  // namespace dis
