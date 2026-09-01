// Recovering class identity from a stripped executable.
//
// DXHRDC.exe has no symbols, but it was built with MSVC RTTI enabled and that
// was never stripped: 1,874 type descriptors survive, and 1,758 of them can be
// tied back to a vtable. Which matters because the interface is not one HUD --
// it is eighty-eight separate Ns*MovieController classes, each named, each
// owning the movie for one element. Identity by name is what makes a routing
// table possible at all; the alternative is hard-coded pointers into a build.
//
// Two directions are needed and they cost very different amounts:
//
//   ClassOf(object)   O(1)  -- read the vtable, read the locator behind it.
//                             Called per movie per frame, so it caches.
//   Find(name)        O(1)  after one scan of .rdata at startup.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace dxs {

struct RttiClass {
  std::string name;        // demangled, e.g. "NsHealthBarMovieController"
  std::string mangled;     // e.g. ".?AVNsHealthBarMovieController@@"
  void** vtable = nullptr;
  uint32_t method_count = 0;
};

class RttiIndex {
 public:
  // Scans .rdata for complete-object-locator/vtable pairs. Safe to call twice.
  bool Build();

  size_t size() const { return by_name_.size(); }
  const std::vector<RttiClass>& classes() const { return classes_; }

  const RttiClass* Find(const std::string& name) const;

  // The class of a live object, or nullptr when the pointer does not look like
  // a polymorphic object of a class this binary declares.
  const char* ClassOf(const void* object) const;

  // The class a vtable belongs to. Used when the object is reached through a
  // secondary base -- multiple inheritance means the pointer handed around is
  // not always the start of the object.
  const char* ClassOfVTable(const void* vtable) const;

 private:
  std::vector<RttiClass> classes_;
  std::unordered_map<std::string, size_t> by_name_;
  std::unordered_map<const void*, size_t> by_vtable_;
  bool built_ = false;
};

// `.?AVFoo@Bar@@` -> `Bar::Foo`. Left alone when it does not parse, because a
// name that survives verbatim is more useful in a log than an empty string.
std::string Demangle(const char* mangled);

// How many entries a vtable has before it stops pointing at code.
//
// Only meaningful for classes belonging to the main executable: "code" here
// means the game's own .text, so this answers 0 for a vtable in any system DLL.
// That is not a bug to be fixed by widening the test -- a COM interface's slot
// count is not discoverable by inspection and guessing it is how a hook ends up
// patching past the end of the array. Use QueryInterface to decide what a
// system object supports, and this only for the game's own classes.
uint32_t VTableLength(void** vtable, uint32_t limit = 256);

}  // namespace dxs
