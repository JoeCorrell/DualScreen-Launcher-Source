// Actual shipping native getter bodies + production inline hooks/ABI thunks.
// UObject identity/class services and the mapping resolver are fixtures.
#include "NativeRolePawn.cpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
using namespace wfcds;
extern "C" void RoleTestCall(void*,void*,void*);
extern "C" uint32_t RoleTestBuddyCall(void*,void*);
extern "C" void RoleTestBuddyStateCall(void*,void*,void*);
namespace {
alignas(16) uint8_t player[0x900]{},ai[0x900]{},player_pawn[0x900]{},ai_pawn[0x900]{};
alignas(16) uint8_t output[560]{};
uint8_t* image=nullptr;
bool live=true;uint32_t resolutions=0,casts=0;
template<class T>T Read(const void* p,size_t n){T v{};std::memcpy(&v,static_cast<const uint8_t*>(p)+n,sizeof(v));return v;}
template<class T>void Put(void* p,size_t n,T v){std::memcpy(static_cast<uint8_t*>(p)+n,&v,sizeof(v));}
void* At(uintptr_t va){return image+va-native_reference_code::kImageBase;}
void Jump(uintptr_t va,void* fn){JumpBytes(static_cast<uint8_t*>(At(va)),At(va),fn);}
void* Resolver(void* controller,bool is_ai){++resolutions;
  assert(controller==(is_ai ? ai:player));
  // Deliberately damage caller-saved/native-private floating-point registers.
  asm volatile("fninit; fldz; pxor %%xmm0,%%xmm0; pxor %%xmm1,%%xmm1; pxor %%xmm2,%%xmm2; pxor %%xmm3,%%xmm3; pxor %%xmm4,%%xmm4; pxor %%xmm5,%%xmm5; pxor %%xmm6,%%xmm6; pxor %%xmm7,%%xmm7"
      ::: "xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","memory");
  auto* pawn=is_ai ? player_pawn:ai_pawn;
  return live && Read<void*>(controller,0x1C8)==pawn && Read<void*>(pawn,0x1D4)==controller ? pawn:nullptr;
}
void Call(bool is_ai,void* controller,void* expected,bool adapted) {
  RoleTestCall(At(is_ai ? native_role_pawn_code::kAiVa:native_role_pawn_code::kPlayerVa),
      static_cast<uint8_t*>(controller)+0x10,output);
  assert(Read<uintptr_t>(output,28)==(expected ? reinterpret_cast<uintptr_t>(expected)+0x10:0x10));
  assert(Read<uint32_t>(output,0)==0x55667788 && Read<uint32_t>(output,4)==0x44556677 &&
      Read<uint32_t>(output,16)==0x11223344);
  const auto flags=Read<uint32_t>(output,32);
  assert((flags&0x40)==(expected ? 0u:0x40u));assert(!(flags&(0x800|0x80|1)));
  if(adapted){assert(Read<uint32_t>(output,20)==0x33445566 && Read<uint32_t>(output,24)==0x22334455);
    for(size_t i=48+160;i<48+160+128;++i)assert(output[i]==0xff);
    assert(Read<uint16_t>(output,48)==0x37f && Read<uint16_t>(output,50)==0x3800);
    assert(Read<uint64_t>(output,48+32)==0x8000000000000000ull && Read<uint16_t>(output,48+40)==0x3fff);
  }
}
void Bind(bool cross){Put(player,0x1C8,static_cast<void*>(cross ? ai_pawn:player_pawn));
  Put(ai,0x1C8,static_cast<void*>(cross ? player_pawn:ai_pawn));
  Put(player_pawn,0x1D4,static_cast<void*>(cross ? ai:player));Put(ai_pawn,0x1D4,static_cast<void*>(cross ? player:ai));}
uint32_t Buddy(){return RoleTestBuddyCall(At(native_role_pawn_code::kBuddyVa),ai+0x10);}
void SetBuddy(bool adapted) {
  RoleTestBuddyStateCall(At(native_role_pawn_code::kBuddySetVa),ai+0x10,output);
  assert(Read<void*>(output,0)==ai+0x10 && Read<uint32_t>(output,4)==0x44556677 &&
      Read<uint32_t>(output,16)==0x11223344);
  if(adapted) {
    assert(Read<uint32_t>(output,28)==0x66778899 && Read<uint32_t>(output,20)==0x33445566 &&
        Read<uint32_t>(output,24)==0x22334455);
    for(size_t i=48+160;i<48+160+128;++i)assert(output[i]==0xff);
    assert(Read<uint16_t>(output,48)==0x37f && Read<uint16_t>(output,50)==0x3800);
    assert(Read<uint64_t>(output,48+32)==0x8000000000000000ull && Read<uint16_t>(output,48+40)==0x3fff);
  }
}
DWORD WINAPI OtherThread(void*){const auto old=resolutions;Call(false,player,nullptr,true);assert(resolutions==old);
  const auto old_casts=casts;SetBuddy(true);assert(resolutions==old && casts==old_casts);
  assert(!RemoveNativeRolePawnGetters());return 0;}
}
extern "C" void* RoleTestClass(void* cache,void*){
  const auto va=reinterpret_cast<uintptr_t>(cache)-reinterpret_cast<uintptr_t>(image)+native_reference_code::kImageBase;
  assert(va==0x12B6EDDC || va==0x12B6ECD0);return (va==0x12B6EDDC ? player_pawn:ai_pawn)+0x10;
}
extern "C" int RoleTestCastImpl(void* object,void* type){++casts;return object==type;}
extern "C" __attribute__((naked)) void RoleTestCast(){asm volatile("pushl 4(%esp)\n pushl %ecx\n call _RoleTestCastImpl\n addl $8,%esp\n ret $4");}
int main(int argc,char** argv) {
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2400000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(image);
  const auto base=reinterpret_cast<uintptr_t>(image);const auto module=reinterpret_cast<HMODULE>(image);
  native_role_pawn_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> relocs){
    const auto bytes=native_reference_code::Relocate(code,base,relocs);std::memcpy(At(va),bytes.data(),bytes.size());return true;});
  Jump(0x110031A0,reinterpret_cast<void*>(&RoleTestClass));Jump(0x10901430,reinterpret_cast<void*>(&RoleTestCast));
  if(argc>1){std::ifstream stream(argv[1],std::ios::binary);std::vector<uint8_t> exe{std::istreambuf_iterator<char>(stream),{}};assert(exe.size()==37667328);
    native_role_pawn_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> relocs){
      assert(!std::memcmp(exe.data()+va-0x10900C00,code,sizeof(code)));
      auto expected=native_reference_code::Relocate(code,native_reference_code::kImageBase,{});
      for(size_t p=0x2148A00;p<0x2148A00+0x243ABC;){const auto page=Read<uint32_t>(exe.data(),p),size=Read<uint32_t>(exe.data(),p+4);assert(size>=8);
        for(size_t q=p+8;q<p+size;q+=2){const auto entry=Read<uint16_t>(exe.data(),q);const uintptr_t address=native_reference_code::kImageBase+page+(entry&4095);
          if((entry>>12)==3 && address>=va && address+4<=va+sizeof(code))Put(expected.data(),address-va,Read<uint32_t>(expected.data(),address-va)+static_cast<uint32_t>(base-native_reference_code::kImageBase));}p+=size;
      }assert(expected==native_reference_code::Relocate(code,base,relocs));return true;});
  }
  assert(ValidateNativeRolePawnCode(module));Bind(false);Call(false,player,player_pawn,false);Call(true,ai,ai_pawn,false);
  assert(InstallNativeRolePawnGetters(module,GetCurrentThreadId(),&Resolver));assert(ValidateNativeRolePawnCode(module));
  assert(!InstallNativeRolePawnGetters(module,GetCurrentThreadId(),&Resolver));
  for(int i=0;i<1000;++i){
    Bind(false);PublishNativeRolePawnControllers(nullptr,nullptr);const auto count=resolutions;
    Call(false,player,player_pawn,false);Call(true,ai,ai_pawn,false);assert(resolutions==count);
    Put(ai_pawn,0x8BC,0u);assert(Buddy()==0);Put(ai_pawn,0x8BC,0x20u);assert(Buddy()==1);
    Put(ai_pawn,0x8BC,0xA5000001u);Put(ai_pawn,0x30,0x12340001u);
    SetBuddy(false);assert(Read<uint32_t>(ai_pawn,0x8BC)==0xA5000021u && Read<uint32_t>(ai_pawn,0x30)==0x12340009u);
    SetBuddy(false);assert(Read<uint32_t>(ai_pawn,0x8BC)==0xA5000021u && Buddy()==1);
    // Stock hardcasts reject the opposite physical class. We do not alter IsA.
    Bind(true);Call(false,player,nullptr,false);Call(true,ai,nullptr,false);assert(Buddy()==0);
    PublishNativeRolePawnControllers(player,ai);const auto native_calls=casts;
    const auto original_storage=Read<uint32_t>(player_pawn,0x8BC);
    std::array<uint8_t,sizeof(player_pawn)> saved_player{};std::memcpy(saved_player.data(),player_pawn,sizeof(player_pawn));
    const auto saved_resolutions=resolutions;SetBuddy(true);assert(resolutions==saved_resolutions);
    assert(!std::memcmp(saved_player.data(),player_pawn,sizeof(player_pawn)));
    live=true;Call(false,player,ai_pawn,true);Call(true,ai,player_pawn,true);assert(Buddy()==1);
    live=false;Call(false,player,nullptr,true);Call(true,ai,nullptr,true);assert(Buddy()==0);SetBuddy(true);live=true;
    assert(Read<uint32_t>(player_pawn,0x8BC)==original_storage);
    Put(ai_pawn,0x1D4,static_cast<void*>(ai));Call(false,player,nullptr,true);Put(ai_pawn,0x1D4,static_cast<void*>(player));
    assert(casts==native_calls); // Adaptation never invokes the stock class cast.
  }
  HANDLE thread=CreateThread(nullptr,0,OtherThread,nullptr,0,nullptr);assert(thread);assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);CloseHandle(thread);
  // Every target byte (including beyond the stolen prefix) is still audited.
  native_role_pawn_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){auto* p=static_cast<uint8_t*>(At(va));
    for(size_t i=0;i<sizeof(code);++i){p[i]^=1;assert(!ValidateNativeRolePawnCode(module));p[i]^=1;}return true;});
  for(const auto& hook:g_role_getters) {
    auto* trampoline=static_cast<uint8_t*>(hook.trampoline);DWORD protect=0;
    assert(VirtualProtect(trampoline,12,PAGE_EXECUTE_READWRITE,&protect));
    for(size_t i=0;i<12;++i){trampoline[i]^=1;assert(!ValidateNativeRolePawnCode(module));assert(!RemoveNativeRolePawnGetters());trampoline[i]^=1;}
    DWORD ignored=0;assert(VirtualProtect(trampoline,12,protect,&ignored));
  }
  auto* target=static_cast<uint8_t*>(At(native_role_pawn_code::kAiVa));target[1]^=1;
  assert(!RemoveNativeRolePawnGetters());assert(NativeRolePawnGettersInstalled());target[1]^=1;
  assert(RemoveNativeRolePawnGetters());assert(!NativeRolePawnGettersInstalled());assert(ValidateNativeRolePawnCode(module));
  Bind(false);Call(false,player,player_pawn,false);Call(true,ai,ai_pawn,false);
  Put(ai_pawn,0x8BC,0u);SetBuddy(false);assert(Read<uint32_t>(ai_pawn,0x8BC)==0x20u);
  native_role_pawn_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){auto* p=static_cast<uint8_t*>(At(va));
    for(size_t i=0;i<sizeof(code);++i){p[i]^=1;assert(!InstallNativeRolePawnGetters(module,GetCurrentThreadId(),&Resolver));p[i]^=1;}return true;});
  assert(VirtualFree(image,0,MEM_RELEASE));std::puts("NativeRolePawnTests passed (native code/ABI; mapping and class metadata are fixtures, not gameplay)");
}
