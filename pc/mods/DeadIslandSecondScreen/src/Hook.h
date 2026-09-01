// Reversible hooks used by the renderer bridge.
//
// Vtable replacement remains the default: one aligned x86 store is both atomic
// and independent of function length.  DXHR's queued render-command executor
// is not virtual, however, so the two exact, signature-checked engine seams
// needed to carry Scaleform identity across the queue use small trampolines.
// Inline hooks are deliberately unavailable without an expected byte string
// and a caller-supplied whole-instruction length.
//
// Hooking a vtable hooks every instance of that class at once. That is the
// intent: there is one Scaleform renderer and eighty-eight movie controllers,
// and the mod wants all of them.

#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

namespace dis {

class HookSet {
 public:
  ~HookSet();

  // Replaces vtable[slot], remembering what was there. Returns false when the
  // slot is out of range, unwritable, or already hooked by this set.
  bool Add(void** vtable, uint32_t slot, void* replacement);

  // Replaces the first `length` bytes of a function with a near jump and
  // returns a trampoline containing those bytes followed by a jump back.
  // `expected` must describe every overwritten byte, and length must end on an
  // instruction boundary. Stolen instructions must not contain PC-relative
  // operands. The hook is refused if the live image differs.
  bool AddInline(void* target, const uint8_t* expected, size_t length,
                 void* replacement, void** trampoline);

  // What was in the slot before Add. nullptr when it was never hooked.
  void* Original(void** vtable, uint32_t slot) const;

  // Puts every slot back. Called on unload, and on any failure during setup so
  // a half-installed mod does not leave the game running with a partial patch.
  void RemoveAll();

  // A setup phase can remember count(), install several related hooks, and
  // roll just that phase back if any required seam is missing. Hooks installed
  // by an earlier phase (the DXGI bootstrap, for example) stay in place.
  void Rollback(size_t keep);

  size_t count() const;

 private:
  struct Entry {
    enum class Kind { kVTable, kInline } kind = Kind::kVTable;
    void** vtable;
    uint32_t slot;
    void* original;
    void* replacement = nullptr;
    void* target = nullptr;
    void* trampoline = nullptr;
    std::vector<uint8_t> bytes;
    std::vector<uint8_t> patched_bytes;
  };
  mutable std::mutex lock_;
  std::vector<Entry> entries_;
};

}  // namespace dis
