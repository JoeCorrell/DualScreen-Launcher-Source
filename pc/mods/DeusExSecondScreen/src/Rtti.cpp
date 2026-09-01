#include "Rtti.h"

#include <windows.h>

#include <algorithm>

#include "Log.h"
#include "Mem.h"

namespace dxs {
namespace {

// The MSVC 32-bit shapes. On 32-bit these fields are real pointers; the
// image-base-relative form only appears in 64-bit builds, which is the one
// detail that would silently produce garbage if this were ported.
struct TypeDescriptor {
  void* vfptr;
  void* spare;
  char name[1];
};

struct CompleteObjectLocator {
  uint32_t signature;  // 0 on 32-bit
  uint32_t offset;     // this-pointer offset of the base this vtable serves
  uint32_t cdOffset;
  TypeDescriptor* type_descriptor;
  void* class_descriptor;
};

struct Section {
  uint8_t* begin = nullptr;
  uint8_t* end = nullptr;
  bool Contains(const void* p) const {
    auto* c = static_cast<const uint8_t*>(p);
    return c >= begin && c < end;
  }
};

Section g_text, g_rdata, g_data;

bool ResolveSections() {
  if (g_text.begin) return true;
  auto base = reinterpret_cast<uint8_t*>(ModuleBase());
  if (!base) return false;
  auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
  auto* sec = IMAGE_FIRST_SECTION(nt);
  for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
    char name[9] = {};
    memcpy(name, sec[i].Name, 8);
    Section s{base + sec[i].VirtualAddress,
              base + sec[i].VirtualAddress +
                  (std::max)(sec[i].Misc.VirtualSize, sec[i].SizeOfRawData)};
    if (!strcmp(name, ".text")) g_text = s;
    else if (!strcmp(name, ".rdata")) g_rdata = s;
    else if (!strcmp(name, ".data")) g_data = s;
  }
  return g_text.begin && g_rdata.begin;
}

bool LooksLikeCode(const void* p) { return g_text.Contains(p); }

const char* MangledFromLocator(const CompleteObjectLocator* col) {
  if (!Readable(col, sizeof(*col))) return nullptr;
  if (col->signature != 0) return nullptr;
  const TypeDescriptor* td = col->type_descriptor;
  if (!g_data.Contains(td) && !g_rdata.Contains(td)) return nullptr;
  if (!Readable(td, sizeof(void*) * 2 + 4)) return nullptr;
  const char* name = SafeCStr(td->name, 512);
  if (!name || name[0] != '.' || name[1] != '?' || name[2] != 'A') return nullptr;
  return name;
}

}  // namespace

std::string Demangle(const char* mangled) {
  if (!mangled) return {};
  std::string s(mangled);
  if (s.size() < 6 || s.compare(0, 3, ".?A") != 0) return s;
  // .?AV / .?AU / .?AW then the @-separated scope, innermost first.
  size_t start = 4;
  size_t end = s.find("@@", start);
  if (end == std::string::npos) return s;
  std::vector<std::string> parts;
  size_t at = start;
  while (at < end) {
    size_t next = s.find('@', at);
    if (next == std::string::npos || next > end) next = end;
    if (next > at) parts.emplace_back(s.substr(at, next - at));
    at = next + 1;
  }
  if (parts.empty()) return s;
  std::string out;
  for (size_t i = parts.size(); i-- > 0;) {
    out += parts[i];
    if (i) out += "::";
  }
  return out;
}

uint32_t VTableLength(void** vtable, uint32_t limit) {
  if (!vtable) return 0;
  uint32_t n = 0;
  for (; n < limit; ++n) {
    void* fn = nullptr;
    if (!Read(vtable + n, &fn)) break;
    if (!LooksLikeCode(fn)) break;
  }
  return n;
}

bool RttiIndex::Build() {
  if (built_) return true;
  if (!ResolveSections()) {
    DXS_ERR("[rtti] could not resolve the executable's sections");
    return false;
  }

  // A vtable is preceded by its locator, so the shape being searched for is a
  // pair: a word pointing at a plausible locator, immediately followed by a
  // word pointing at code. Scanning for that is cheap and has no false
  // positives worth worrying about -- a random pointer into .rdata that also
  // has signature 0 and a ".?A" name behind it is not a thing that happens.
  auto* p = reinterpret_cast<void**>(g_rdata.begin);
  auto* limit = reinterpret_cast<void**>(g_rdata.end) - 2;
  size_t skipped = 0;

  for (; p < limit; ++p) {
    auto* col = static_cast<const CompleteObjectLocator*>(*p);
    if (!g_rdata.Contains(col)) continue;
    if (!LooksLikeCode(*(p + 1))) continue;
    const char* mangled = MangledFromLocator(col);
    if (!mangled) continue;

    void** vtable = p + 1;
    std::string name = Demangle(mangled);

    // Multiple inheritance gives one class several vtables, one per base. The
    // primary is the one whose locator reports offset 0; the others are real
    // and useful but must not take the name, or a lookup by name returns a
    // secondary base and every slot index computed from it is wrong.
    auto existing = by_name_.find(name);
    if (existing != by_name_.end()) {
      if (col->offset != 0) {
        by_vtable_.emplace(vtable, existing->second);
        ++skipped;
        continue;
      }
    }

    RttiClass entry;
    entry.name = name;
    entry.mangled = mangled;
    entry.vtable = vtable;
    entry.method_count = VTableLength(vtable);

    size_t index = classes_.size();
    classes_.push_back(std::move(entry));
    by_vtable_[vtable] = index;
    if (existing == by_name_.end() || col->offset == 0) by_name_[name] = index;
  }

  built_ = true;
  DXS_INFO("[rtti] %u classes, %u secondary-base vtables, .text %p-%p",
           static_cast<unsigned>(by_name_.size()), static_cast<unsigned>(skipped),
           static_cast<void*>(g_text.begin), static_cast<void*>(g_text.end));
  return !classes_.empty();
}

const RttiClass* RttiIndex::Find(const std::string& name) const {
  auto it = by_name_.find(name);
  return it == by_name_.end() ? nullptr : &classes_[it->second];
}

const char* RttiIndex::ClassOfVTable(const void* vtable) const {
  auto it = by_vtable_.find(vtable);
  return it == by_vtable_.end() ? nullptr : classes_[it->second].name.c_str();
}

const char* RttiIndex::ClassOf(const void* object) const {
  if (!object) return nullptr;
  void* vtable = nullptr;
  if (!Read(object, &vtable)) return nullptr;
  if (const char* known = ClassOfVTable(vtable)) return known;

  // Not in the index: either a class whose vtable the scan did not pair, or a
  // runtime-generated one. Fall back to reading the locator directly so the
  // answer is still a name rather than "unknown".
  void* col_ptr = nullptr;
  if (!Read(static_cast<void**>(vtable) - 1, &col_ptr)) return nullptr;
  const char* mangled =
      MangledFromLocator(static_cast<const CompleteObjectLocator*>(col_ptr));
  return mangled;  // mangled form; caller may demangle for display
}

}  // namespace dxs
