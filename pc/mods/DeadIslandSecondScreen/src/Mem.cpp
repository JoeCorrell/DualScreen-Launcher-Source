#include "Mem.h"

namespace dis {
namespace {

uintptr_t g_base = 0;
size_t g_size = 0;

void Resolve() {
  if (g_base) return;
  HMODULE self = GetModuleHandleW(nullptr);
  auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(self);
  if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return;
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(
      reinterpret_cast<uint8_t*>(self) + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return;
  g_base = reinterpret_cast<uintptr_t>(self);
  g_size = nt->OptionalHeader.SizeOfImage;
}

// A page that can be read at all. PAGE_GUARD is excluded deliberately: touching
// a guard page is how a stack grows, and probing one from a worker thread is a
// way to corrupt somebody else's stack rather than a way to learn anything.
bool ReadableProtect(DWORD p) {
  if (p & (PAGE_GUARD | PAGE_NOACCESS)) return false;
  return (p & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
               PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
               PAGE_EXECUTE_WRITECOPY)) != 0;
}

bool WritableProtect(DWORD p) {
  if (p & (PAGE_GUARD | PAGE_NOACCESS)) return false;
  return (p & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE |
               PAGE_EXECUTE_WRITECOPY)) != 0;
}

bool ExecutableProtect(DWORD p) {
  if (p & (PAGE_GUARD | PAGE_NOACCESS)) return false;
  return (p & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
               PAGE_EXECUTE_WRITECOPY)) != 0;
}

// Walks every region the range spans, because a structure that begins on a
// committed page can still run off the end of it.
bool RangeIs(const void* p, size_t n, bool (*test)(DWORD)) {
  if (!p || n == 0) return false;
  auto start = reinterpret_cast<uintptr_t>(p);
  if (start + n < start) return false;  // wrapped
  uintptr_t at = start;
  const uintptr_t end = start + n;
  while (at < end) {
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(reinterpret_cast<void*>(at), &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT || !test(mbi.Protect)) return false;
    uintptr_t region_end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    if (region_end <= at) return false;
    at = region_end;
  }
  return true;
}

}  // namespace

uintptr_t ModuleBase() { Resolve(); return g_base; }
size_t ModuleSize() { Resolve(); return g_size; }

bool Readable(const void* p, size_t n) { return RangeIs(p, n, ReadableProtect); }
bool Writable(const void* p, size_t n) { return RangeIs(p, n, WritableProtect); }
bool Executable(const void* p, size_t n) { return RangeIs(p, n, ExecutableProtect); }

const char* SafeCStr(const void* p, size_t max_len) {
  if (!p) return nullptr;
  auto* s = static_cast<const char*>(p);
  for (size_t i = 0; i < max_len; ++i) {
    if (!Readable(s + i, 1)) return nullptr;
    if (s[i] == '\0') return i ? s : nullptr;
  }
  return nullptr;
}

Unprotect::Unprotect(void* address, size_t size) : address_(address), size_(size) {
  ok_ = VirtualProtect(address_, size_, PAGE_EXECUTE_READWRITE, &previous_) != 0;
}

Unprotect::~Unprotect() {
  if (!ok_) return;
  DWORD ignored = 0;
  VirtualProtect(address_, size_, previous_, &ignored);
}

}  // namespace dis
