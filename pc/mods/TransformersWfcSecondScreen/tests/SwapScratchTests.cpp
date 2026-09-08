#include "SwapScratch.h"
#include "WeaponRoleJournal.h"
#include <array>
#include <cassert>
#include <cstdio>
using namespace wfcds;
struct Snapshot {
  std::array<InventoryWeaponRole,64> weapons{};
  int slot=-1;
  unsigned generation=7;
};
static Snapshot snapshot;
int main(){
  bool busy=false;
  for(unsigned pass=0;pass<2000;++pass){
    SwapScratchLease outer(busy);assert(outer && busy);
    snapshot.slot=2;snapshot.weapons[63].object_index=91;
    {SwapScratchLease nested(busy);assert(!nested && busy);}
    assert(busy && snapshot.slot==2 && snapshot.weapons[63].object_index==91);
    ResetSwapSnapshot(snapshot);
    assert(snapshot.slot==-1 && snapshot.generation==7);
    for(const auto& w:snapshot.weapons)assert(!w.weapon && !w.object_index && !w.changed && !w.retention.captured);
  }
  assert(!busy);
  {SwapScratchLease later(busy);assert(later && busy);}
  assert(!busy);
  std::puts("SwapScratchTests passed: 2000 in-place resets, preserved nonzero defaults, nested rejection and lease release");
}
