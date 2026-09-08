#include "NativeState.h"
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>
#include "Log.h"

namespace isaacds {
namespace {
bool Copy(uintptr_t address, void* output, size_t size) {
  if (address < 0x10000 || size > 256 * 1024 || address + size < address) return false;
  SIZE_T copied = 0;
  return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address),
                           output, size, &copied) && copied == size;
}
template<class T> bool Read(uintptr_t address, T& output) {
  return Copy(address, &output, sizeof(output));
}
bool Roots(uint32_t& game, uint32_t& manager) {
  const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  int state = 0;
  // J460: Manager::AchievementUnlocksDisallowed 009595e0, not RGON offsets.
  return Read(base + 0x871678, game) && Read(base + 0x87169c, manager) &&
         game && manager && Read(manager + 8, state) && state == 2;
}
template<size_t N> bool Bits(uintptr_t address, std::string& out) {
  std::array<uint8_t,N> bytes{};
  if (!Copy(address, bytes.data(), bytes.size())) return false;
  for (auto value : bytes) {
    if (value > 1) return false;
    out += value ? '1' : '0';
  }
  return true;
}
bool NativeString(uintptr_t address, std::string& out) {
  std::array<uint32_t,6> value{};
  if(!Read(address,value) || value[4]>1024 || value[5]<value[4]) return false;
  out.resize(value[4]);
  return out.empty() || Copy(value[5]>15 ? value[0] : address,out.data(),out.size());
}

bool __fastcall HasGameplayMods(void* mods, void*) {
  std::array<uint32_t,2> range{};
  if(!Read(reinterpret_cast<uintptr_t>(mods),range) || range[1]<range[0] ||
     range[1]-range[0]>4096*4 || (range[1]-range[0])%4) return true;
  for(uint32_t cursor=range[0];cursor<range[1];cursor+=4) {
    uint32_t entry=0; uint8_t loaded=0;
    if(!Read(cursor,entry) || !Read(entry+0xfd,loaded)) return true;
    if(!loaded) continue;
    std::string directory,name;
    if(!NativeString(entry,directory) || !NativeString(entry+0x18,name)) return true;
    const auto separator=directory.find_last_of("/\\");
    if(separator!=std::string::npos) directory.erase(0,separator+1);
    for(char& ch:directory) if(ch>='A' && ch<='Z') ch+= 'a'-'A';
    if(directory!="dualscreenisaac" || name!="Dual Screen Isaac") return true;
  }
  return false;
}

struct HistoryEntry { int time; uint32_t trinket; int id, stage, stage_type, room_type, pool; };
static_assert(sizeof(HistoryEntry)==0x1c);
// Fastcall with an unused EDX argument preserves the x86 thiscall stack ABI.
using TakeDamage = bool (__fastcall *)(void*, void*, float, uint64_t, void*, int);
TakeDamage realDamage = nullptr;
volatile LONG totalHits = 0;
thread_local int damageDepth = 0;

bool __fastcall ObserveDamage(void* player, void*, float amount, uint64_t flags,
                              void* source, int countdown) {
  uint32_t game=0,manager=0,players=0,primary=0;
  const bool track = damageDepth == 0 && Roots(game,manager) &&
      Read(game+0x1baa8,players) && Read(players,primary) &&
      primary == reinterpret_cast<uintptr_t>(player);
  ++damageDepth;
  const bool applied = realDamage(player,nullptr,amount,flags,source,countdown);
  --damageDepth;
  if (track && applied && amount > 0 && !(flags & (uint64_t{1} << 21)))
    InterlockedIncrement(&totalHits);
  return applied;
}

bool InstallDamageObserver() {
  if (realDamage) return true;
  const uintptr_t base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  auto* slot=reinterpret_cast<PVOID volatile*>(base+0x76bdf0);
  uint32_t original=0;
  // J460 RTTI Entity_Player vtable slot 8. Ghidra references to TakeDamage
  // contain this vtable entry only; calls dispatch through the virtual method.
  if(!Read(reinterpret_cast<uintptr_t>(slot),original)||original!=base+0x3729d0)return false;
  std::memcpy(&realDamage,&original,sizeof(original));
  DWORD previous=0;
  if(!VirtualProtect(const_cast<PVOID*>(slot),sizeof(void*),PAGE_READWRITE,&previous)) {
    realDamage=nullptr;return false;
  }
  void* replacement=nullptr;auto hook=&ObserveDamage;
  std::memcpy(&replacement,&hook,sizeof(replacement));
  void* found=InterlockedCompareExchangePointer(slot,replacement,reinterpret_cast<void*>(original));
  DWORD ignored=0;VirtualProtect(const_cast<PVOID*>(slot),sizeof(void*),previous,&ignored);
  if(found!=reinterpret_cast<void*>(original)){realDamage=nullptr;return false;}
  Log(LogLevel::kInfo,"[Native] J460 player damage observer installed; original result preserved");
  return true;
}
}

bool InstallCompanionAchievementCompatibility() {
  static bool installed=false;
  if(installed) return true;
  const uintptr_t base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  // Only the enabled-mod predicate in Manager::AchievementUnlocksDisallowed.
  // Debug console, seeds, network restrictions and all unlock/save code remain native.
  const uintptr_t site=base+0x559611;
  const std::array<uint8_t,7> expected={0xe8,0xba,0xf6,0xf9,0xff,0x84,0xc0};
  std::array<uint8_t,7> actual{};
  if(!Read(site,actual) || actual!=expected) return false;
  auto hook=&HasGameplayMods; uintptr_t target=0;
  std::memcpy(&target,&hook,sizeof(target));
  const uint32_t displacement=static_cast<uint32_t>(target-site-5);
  DWORD previous=0;
  if(!VirtualProtect(reinterpret_cast<void*>(site),5,PAGE_EXECUTE_READWRITE,&previous)) return false;
  // Installed on the game thread at Lua file loading, before a run starts.
  std::memcpy(reinterpret_cast<void*>(site+1),&displacement,4);
  DWORD ignored=0;VirtualProtect(reinterpret_cast<void*>(site),5,previous,&ignored);
  FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(site),5);
  installed=true;
  Log(LogLevel::kInfo,"[Native] achievement gate excludes only DualScreenIsaac; native restrictions preserved");
  return true;
}

int NativeHitCount(int restore) {
  uint32_t game=0,manager=0;
  if(!Roots(game,manager)||!InstallDamageObserver())return -1;
  if(restore>=0)InterlockedExchange(&totalHits,restore);
  return static_cast<int>(InterlockedCompareExchange(&totalHits,0,0));
}

std::string ReadNativeRunState() {
  uint32_t game=0,manager=0,first=0,last=0,player=0;
  if (!Roots(game,manager) || !Read(game+0x1baa8,first) || !Read(game+0x1baac,last) ||
      last<=first || last-first>64 || (last-first)%4 || !Read(first,player) || !player) return {};
  std::array<uint32_t,3> history{};
  // Native AddCollectible/ChangePlayerType call History::AddHistoryItem on +1dec.
  if (!Read(player+0x1dec,history) || history[1]<history[0] || history[2]<history[1] ||
      history[1]-history[0]>0x1c*4096 || (history[1]-history[0])%0x1c) return {};
  std::vector<HistoryEntry> items((history[1]-history[0])/0x1c);
  if (!items.empty() && !Copy(history[0],items.data(),items.size()*sizeof(HistoryEntry))) return {};
  std::string out;
  for (const auto& item : items) {
    if ((item.trinket & 0xff)>1 || item.time<0 || item.id<0 || item.id>65535) return {};
    if ((item.trinket & 0xff) || !item.id) continue;
    if (!out.empty()) out+=';';
    out+=std::to_string(item.id)+","+std::to_string(item.time);
  }
  const uintptr_t persistent=manager+0x14;
  out+='|'; if(!Bits<642>(persistent+0x38,out))return {};
  out+='|'; if(!Bits<46>(persistent+0xe6f,out))return {};
  out+='|';
  std::array<int,523> events{};
  if(!Read(persistent+0x2bc,events))return {};
  for(size_t i=0;i<events.size();i++) {if(i)out+=',';out+=std::to_string(events[i]);}
  out+='|';if(!Bits<104>(persistent+0xe07,out))return {};
  uint8_t readonly=0;uint32_t slot=0;
  if(!Read(persistent+1,readonly)||readonly>1||!Read(manager+0x10,slot)||slot>3)return {};
  out+='|';out+=std::to_string(readonly);out+='|';out+=std::to_string(slot);
  out+='|';
  // CompletionWidget::Update (00425130) indexes the native character/event
  // table. Zero entries belong to alternate forms without a separate menu record.
  constexpr int marks[]={0,1,2,3,4,5,6,7,9,12,13,14};
  const uintptr_t base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  for(int character=0;character<41;character++) for(int mark:marks) {
    int event=0;
    if(!Read(base+0x835ed0+character*0x90+mark*8,event) || event<0 || event>=523) return {};
    if(character || mark) out+=',';
    out+=std::to_string(events[event]&15);
  }
  return out;
}

std::string ReadNativeRoomFlags() {
  uint32_t game=0,manager=0,indices=0;
  if(!Roots(game,manager)||!Read(game+0x182c8,indices))return {};
  std::array<int,169> grid{};
  if(!Read(indices,grid))return {};
  std::string out;
  for(int i=0;i<169;i++) {
    int index=grid[i];if(index<0)continue;
    if(index>=527)return {};
    // Minimap refresh uses the active dimension's index map and 0xb8 descriptors.
    std::array<int,18> room{};
    if(!Read(game+0x14+index*0xb8,room)||room[2]!=index)return {};
    if(room[0]!=i)continue;
    if(!out.empty())out+=';';
    out+=std::to_string(index)+","+std::to_string(room[17]);
  }
  return out;
}
}
