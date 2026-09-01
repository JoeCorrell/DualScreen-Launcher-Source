// Reading the game's memory without trusting any of it.
//
// Two problems this solves. The executable has ASLR on -- dllcharacteristics
// 0x8140 -- so every address recovered by static analysis is an offset from a
// base that is not known until the process exists. And a mod that guesses wrong
// about a pointer takes the game down with it, which on a handheld means a lost
// session rather than a debugger.
//
// Everything here is therefore expressed as an RVA against the preferred base
// of 0x400000, and every read is checked against the page's actual protection
// before it happens.

#pragma once

#include <windows.h>
#include <cstdint>
#include <cstring>

namespace dis {

// The base DXHRDC.exe was linked for. Static analysis produces addresses in
// this space; Rebase turns them into addresses in this process.
constexpr uintptr_t kPreferredBase = 0x00400000;

// Base and extent of the main executable, resolved once.
uintptr_t ModuleBase();
size_t ModuleSize();

inline uintptr_t Rebase(uintptr_t preferred_va) {
  return ModuleBase() + (preferred_va - kPreferredBase);
}

inline uintptr_t Unbase(uintptr_t actual) {
  return kPreferredBase + (actual - ModuleBase());
}

inline bool InModule(uintptr_t p) {
  return p >= ModuleBase() && p < ModuleBase() + ModuleSize();
}

// True when the whole range is committed and readable.
bool Readable(const void* p, size_t n);

// True when the whole range is committed and writable without changing its
// protection.
bool Writable(const void* p, size_t n);

// True when the range is committed and holds executable code.
bool Executable(const void* p, size_t n);

// Reads that answer false rather than raising, for walking structures whose
// shape is inferred rather than known.
template <typename T>
inline bool Read(const void* p, T* out) {
  if (!Readable(p, sizeof(T))) return false;
  std::memcpy(out, p, sizeof(T));
  return true;
}

inline bool ReadPtr(const void* p, void** out) { return Read(p, out); }

inline void* PtrAt(const void* base, size_t offset) {
  void* v = nullptr;
  return Read(static_cast<const uint8_t*>(base) + offset, &v) ? v : nullptr;
}

// A NUL-terminated ASCII string that is checked byte by byte, so a pointer into
// the middle of nowhere yields nullptr instead of walking off a page.
const char* SafeCStr(const void* p, size_t max_len = 512);

// Makes a page writable for the duration of a scope, then puts it back.
class Unprotect {
 public:
  Unprotect(void* address, size_t size);
  ~Unprotect();
  bool ok() const { return ok_; }

 private:
  void* address_;
  size_t size_;
  DWORD previous_ = 0;
  bool ok_ = false;
};

}  // namespace dis
