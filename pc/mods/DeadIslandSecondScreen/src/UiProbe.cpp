// Finding IUIElement::OnRender's vtable slot, at run time, because the binary
// cannot say where it is.
//
// The obvious route fails. `?OnRender@IUIElement@@UEAAXXZ` is exported, and so
// is `??_7IUIElement@@6B@` -- the vtable itself -- so the slot should just be
// the index whose entry equals the function's address. It is not: the base
// implementation is empty, and MSVC's identical COMDAT folding merged it with
// every other empty function in the module. Searching the exported vtable for
// that address returns slots 0, 1, 112, 114 and 116, and no way to choose.
// Derived classes -- the ones that actually draw -- export neither their
// overrides nor their vtables.
//
// So the slot is measured instead of read. Every slot of a live element's
// vtable is replaced with a counting stub, the game runs for a few seconds, and
// the counts are written down. OnRender has an unmistakable signature: called
// once per visible element per frame, so its count tracks the frame counter
// while lifecycle methods sit near zero and per-frame-but-not-drawing methods
// (update, layout) appear alongside it and can be told apart by hooking one at
// a time afterwards.
//
// The stubs are hand-written assembly for the same reason as the Deus Ex slot
// survey: these methods have wildly different signatures, and a C++ thunk of
// the wrong shape corrupts the stack on the first call. A stub that increments
// a counter and jumps preserves every register and every stack byte. `inc`
// disturbs only the flags, which are not live at a call boundary.

#include <windows.h>

#include <atomic>
#include <cstdint>

#include "Engine.h"
#include "Hook.h"
#include "Log.h"
#include "Mem.h"
#include "UiProbe.h"

// x86-64 MinGW emits C symbols without a leading underscore, unlike the 32-bit
// toolchain the Deus Ex mod used.
extern "C" {
uint64_t dis_ui_calls[64];
void* dis_ui_orig[64];
}

#define DIS_UI_STUB(n)                                     \
  extern "C" void dis_ui_stub_##n(void);                   \
  asm(".globl dis_ui_stub_" #n "\n"                        \
      "dis_ui_stub_" #n ":\n"                              \
      "  incq dis_ui_calls+" #n "*8(%rip)\n"               \
      "  jmp  *dis_ui_orig+" #n "*8(%rip)\n");

DIS_UI_STUB(0)  DIS_UI_STUB(1)  DIS_UI_STUB(2)  DIS_UI_STUB(3)
DIS_UI_STUB(4)  DIS_UI_STUB(5)  DIS_UI_STUB(6)  DIS_UI_STUB(7)
DIS_UI_STUB(8)  DIS_UI_STUB(9)  DIS_UI_STUB(10) DIS_UI_STUB(11)
DIS_UI_STUB(12) DIS_UI_STUB(13) DIS_UI_STUB(14) DIS_UI_STUB(15)
DIS_UI_STUB(16) DIS_UI_STUB(17) DIS_UI_STUB(18) DIS_UI_STUB(19)
DIS_UI_STUB(20) DIS_UI_STUB(21) DIS_UI_STUB(22) DIS_UI_STUB(23)
DIS_UI_STUB(24) DIS_UI_STUB(25) DIS_UI_STUB(26) DIS_UI_STUB(27)
DIS_UI_STUB(28) DIS_UI_STUB(29) DIS_UI_STUB(30) DIS_UI_STUB(31)
DIS_UI_STUB(32) DIS_UI_STUB(33) DIS_UI_STUB(34) DIS_UI_STUB(35)
DIS_UI_STUB(36) DIS_UI_STUB(37) DIS_UI_STUB(38) DIS_UI_STUB(39)
DIS_UI_STUB(40) DIS_UI_STUB(41) DIS_UI_STUB(42) DIS_UI_STUB(43)
DIS_UI_STUB(44) DIS_UI_STUB(45) DIS_UI_STUB(46) DIS_UI_STUB(47)
DIS_UI_STUB(48) DIS_UI_STUB(49) DIS_UI_STUB(50) DIS_UI_STUB(51)
DIS_UI_STUB(52) DIS_UI_STUB(53) DIS_UI_STUB(54) DIS_UI_STUB(55)
DIS_UI_STUB(56) DIS_UI_STUB(57) DIS_UI_STUB(58) DIS_UI_STUB(59)
DIS_UI_STUB(60) DIS_UI_STUB(61) DIS_UI_STUB(62) DIS_UI_STUB(63)

namespace dis {
namespace {

void* StubFor(int n) {
  static void* const kStubs[64] = {
    (void*)&dis_ui_stub_0,  (void*)&dis_ui_stub_1,  (void*)&dis_ui_stub_2,  (void*)&dis_ui_stub_3,
    (void*)&dis_ui_stub_4,  (void*)&dis_ui_stub_5,  (void*)&dis_ui_stub_6,  (void*)&dis_ui_stub_7,
    (void*)&dis_ui_stub_8,  (void*)&dis_ui_stub_9,  (void*)&dis_ui_stub_10, (void*)&dis_ui_stub_11,
    (void*)&dis_ui_stub_12, (void*)&dis_ui_stub_13, (void*)&dis_ui_stub_14, (void*)&dis_ui_stub_15,
    (void*)&dis_ui_stub_16, (void*)&dis_ui_stub_17, (void*)&dis_ui_stub_18, (void*)&dis_ui_stub_19,
    (void*)&dis_ui_stub_20, (void*)&dis_ui_stub_21, (void*)&dis_ui_stub_22, (void*)&dis_ui_stub_23,
    (void*)&dis_ui_stub_24, (void*)&dis_ui_stub_25, (void*)&dis_ui_stub_26, (void*)&dis_ui_stub_27,
    (void*)&dis_ui_stub_28, (void*)&dis_ui_stub_29, (void*)&dis_ui_stub_30, (void*)&dis_ui_stub_31,
    (void*)&dis_ui_stub_32, (void*)&dis_ui_stub_33, (void*)&dis_ui_stub_34, (void*)&dis_ui_stub_35,
    (void*)&dis_ui_stub_36, (void*)&dis_ui_stub_37, (void*)&dis_ui_stub_38, (void*)&dis_ui_stub_39,
    (void*)&dis_ui_stub_40, (void*)&dis_ui_stub_41, (void*)&dis_ui_stub_42, (void*)&dis_ui_stub_43,
    (void*)&dis_ui_stub_44, (void*)&dis_ui_stub_45, (void*)&dis_ui_stub_46, (void*)&dis_ui_stub_47,
    (void*)&dis_ui_stub_48, (void*)&dis_ui_stub_49, (void*)&dis_ui_stub_50, (void*)&dis_ui_stub_51,
    (void*)&dis_ui_stub_52, (void*)&dis_ui_stub_53, (void*)&dis_ui_stub_54, (void*)&dis_ui_stub_55,
    (void*)&dis_ui_stub_56, (void*)&dis_ui_stub_57, (void*)&dis_ui_stub_58, (void*)&dis_ui_stub_59,
    (void*)&dis_ui_stub_60, (void*)&dis_ui_stub_61, (void*)&dis_ui_stub_62, (void*)&dis_ui_stub_63,
  };
  return (n >= 0 && n < 64) ? kStubs[n] : nullptr;
}

void** g_probed_vtable = nullptr;
int g_probed_slots = 0;
std::atomic<bool> g_installed{false};

}  // namespace

// Installs counting stubs on every slot of this element's vtable.
//
// One element, not all of them: the vtable is shared by every instance of that
// class, so one is enough to measure, and hooking a single vtable keeps the
// blast radius small if a stub turns out to be wrong.
bool UiProbeInstall(void* element, HookSet* hooks) {
  if (!element || !hooks || g_installed.exchange(true)) return false;

  void* vt_raw = nullptr;
  if (!Read(element, &vt_raw)) return false;
  auto** vt = static_cast<void**>(vt_raw);

  int installed = 0;
  for (int i = 0; i < 64; ++i) {
    void* original = nullptr;
    if (!Read(vt + i, &original)) break;
    // Stop at the first entry that is not code: past the end of the vtable is
    // whatever happens to follow it in .rdata, and patching that corrupts data
    // rather than behaviour.
    if (!Executable(original, 1)) break;
    dis_ui_orig[i] = original;
    if (hooks->Add(vt, i, StubFor(i))) ++installed;
    g_probed_slots = i + 1;
  }
  g_probed_vtable = vt;
  DIS_INFO("[probe] counting %d of %d slots on element vtable %p", installed,
           g_probed_slots, static_cast<void*>(vt));
  return installed > 0;
}

void UiProbeReset() {
  for (int i = 0; i < 64; ++i) {
    dis_ui_calls[i] = 0;
    dis_ui_orig[i] = nullptr;
  }
  g_probed_vtable = nullptr;
  g_probed_slots = 0;
  g_installed.store(false, std::memory_order_release);
}

int UiProbeSlotCount() { return g_probed_slots; }

void** UiProbeVtable() { return g_probed_vtable; }

void* UiProbeOriginal(int slot) {
  return (slot >= 0 && slot < g_probed_slots) ? dis_ui_orig[slot] : nullptr;
}

// Writes the counts, with the frame count alongside so the per-frame methods
// stand out. OnRender is the one whose count tracks frames.
void UiProbeReport(uint32_t frames) {
  if (!g_probed_vtable) return;
  DIS_INFO("[probe] slot call counts after %u frames "
           "(a slot at ~1x frames is called once per element per frame):", frames);
  for (int i = 0; i < g_probed_slots; ++i) {
    const uint64_t n = dis_ui_calls[i];
    if (n == 0) continue;
    const double per_frame = frames ? static_cast<double>(n) / frames : 0.0;
    DIS_INFO("[probe]   slot %-2d  calls=%-9llu  %.2f per frame%s", i,
             static_cast<unsigned long long>(n), per_frame,
             (per_frame > 0.5 && per_frame < 4.0) ? "   <-- candidate" : "");
  }
}

}  // namespace dis
