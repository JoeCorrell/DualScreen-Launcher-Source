#pragma once
#include <new>
#include <type_traits>
namespace wfcds {
// These snapshots own no engine resources. Construct defaults directly in
// their persistent storage, without materializing a second large stack value.
template<class T> void ResetSwapSnapshot(T& value) noexcept {
  static_assert(std::is_trivially_copyable<T>::value &&
                std::is_trivially_destructible<T>::value,
                "Owned engine resources require explicit lifetime management");
  value.~T();
  ::new (static_cast<void*>(&value)) T{};
}
// Callers check the game thread before borrowing. No allocation, global game
// lock or per-frame work; recursive callbacks must not overwrite an outer read.
class SwapScratchLease {
 public:
  explicit SwapScratchLease(bool& busy) noexcept : flag_(busy ? nullptr:&busy) {
    if(flag_)*flag_=true;
  }
  ~SwapScratchLease(){if(flag_)*flag_=false;}
  SwapScratchLease(const SwapScratchLease&)=delete;
  SwapScratchLease& operator=(const SwapScratchLease&)=delete;
  explicit operator bool() const noexcept {return flag_!=nullptr;}
 private:
  bool* flag_;
};
}
