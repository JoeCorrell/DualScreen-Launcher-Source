#include "Hook.h"
#include <windows.h>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <limits>
using namespace wfcds;
extern "C" uint32_t RelocationReplacement(){return 17;}
using Call=uint32_t(*)();
void CallBytes(uint8_t* at,uint8_t* destination) {
  at[0]=0xE8;
  const uint32_t rel=reinterpret_cast<uintptr_t>(destination)-reinterpret_cast<uintptr_t>(at)-5;
  std::memcpy(at+1,&rel,4);
}
int main() {
  auto* code=static_cast<uint8_t*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(code);
  // Two real CALLs with both negative and positive displacement. Each helper
  // increments EAX; the call stack/return addresses must remain correct.
  const uint8_t inc[]={0x40,0xC3};
  std::memcpy(code,inc,2);std::memcpy(code+128,inc,2);
  auto* entry=code+32;
  entry[0]=0x31;entry[1]=0xC0;CallBytes(entry+2,code);CallBytes(entry+7,code+128);entry[12]=0xC3;
  uint8_t expected[12]{};std::memcpy(expected,entry,12);
  auto fn=reinterpret_cast<Call>(entry);assert(fn()==2);
  for(int i=0;i<2000;++i) {
    InlineHook h{};void* original=nullptr;
    assert(InstallInlineHook(&h,entry,reinterpret_cast<void*>(&RelocationReplacement),expected,12,&original,{2,7}));
    assert(fn()==17 && reinterpret_cast<Call>(original)()==2);
    assert(!std::memcmp(h.original,expected,12));
    for(size_t off:{size_t{2},size_t{7}}){
      auto* bytes=static_cast<uint8_t*>(original);int32_t displacement=0;
      std::memcpy(&displacement,bytes+off+1,4);assert(bytes[off]==0xE8);
      assert(reinterpret_cast<uintptr_t>(bytes)+off+5+uintptr_t(displacement)==reinterpret_cast<uintptr_t>(off==2 ? code:code+128));
    }
    assert(RemoveInlineHook(&h));assert(fn()==2 && !std::memcmp(entry,expected,12));
  }
  for(auto offsets:{std::initializer_list<size_t>{1}, {2,2}, {7,2}, {2,8}, {std::numeric_limits<size_t>::max()}}) {
    InlineHook h{};void* original=nullptr;
    assert(!InstallInlineHook(&h,entry,reinterpret_cast<void*>(&RelocationReplacement),expected,12,&original,offsets));
    assert(!h.target && !original && fn()==2 && !std::memcmp(entry,expected,12));
  }
  CallBytes(entry+2,entry+1);std::memcpy(expected,entry,12);
  InlineHook h{};void* original=nullptr;
  assert(!InstallInlineHook(&h,entry,reinterpret_cast<void*>(&RelocationReplacement),expected,12,&original,{2,7}));
  assert(!h.target && !original && !std::memcmp(entry,expected,12));
  assert(VirtualFree(code,0,MEM_RELEASE));
  std::puts("InlineHookRelocationTests passed: 2,000 real two-CALL trampoline cycles and malformed/internal-target rejection");
}
