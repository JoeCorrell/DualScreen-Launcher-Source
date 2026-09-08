// Complete shipping aim/GOAP comparison bodies. Controller aiming, class
// lookup, AI aimer and role identity are fixtures, NOT gameplay validation.
#include "NativePawnTargeting.cpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
using namespace wfcds;
extern "C" {
alignas(16) uint32_t AimXmmSentinel[4]={0xA1B2C3D4,0x10203040,0x50607080,0x91827364};
alignas(16) uint32_t AimXmmObserved[4]{};
void* LocationObserverOriginal=nullptr;
void* LocationObservedPawn=nullptr;
void* LocationObservedRequester=nullptr;
uint32_t LocationObservedAlternate=0;
}
extern "C" __attribute__((naked)) void LocationObserver() {
  asm volatile("movl %ecx,_LocationObservedPawn; pushl %eax; movl 12(%esp),%eax; movl %eax,_LocationObservedRequester; movl 16(%esp),%eax; movl %eax,_LocationObservedAlternate; popl %eax; jmp *_LocationObserverOriginal");
}
extern "C" __attribute__((naked)) void* AimCall(void*,void*,void*,void*,void*) {
  asm volatile("pushl %ebp; movl %esp,%ebp; movl 12(%ebp),%ecx; pushl 24(%ebp); pushl 20(%ebp); pushl 16(%ebp); movdqa _AimXmmSentinel,%xmm0; call *8(%ebp); leave; ret");
}
extern "C" __attribute__((naked)) uint32_t SameCall(void*,void*,void*) {
  asm volatile("movl 12(%esp),%ecx; movl 8(%esp),%eax; jmp *4(%esp)");
}
namespace {
alignas(16) uint8_t pawns[2][0x900]{},controllers[2][0x500]{},player[0x500]{},world[0x500]{},
    aimers[2][0x100]{},collections[2][0x100]{},entries[2][48]{},target_class[0x100]{},
    target_default[0x100]{},targets[2][32]{},weapon[32]{};
uint8_t* image=nullptr;
uint32_t output[3]{},start[3]={0x41000000,0x42000000,0x43000000};
unsigned player_calls=0,base_calls=0,ai_calls=0,cast_calls=0,class_calls=0,resolutions=0,location_calls=0;
bool live=true;
template<class T>T Read(const void* p,size_t off){T v{};std::memcpy(&v,static_cast<const uint8_t*>(p)+off,sizeof(v));return v;}
template<class T>void Put(void* p,size_t off,T v){std::memcpy(static_cast<uint8_t*>(p)+off,&v,sizeof(v));}
void* At(uintptr_t va){return image+va-native_reference_code::kImageBase;}
void Jump(uintptr_t va,void* target){TargetJump(static_cast<uint8_t*>(At(va)),At(va),target);}
bool Resolve(void* p) {
  ++resolutions;assert(p==pawns[0]);
  // Deliberate FP/SIMD clobbers ensure the native caller does not inherit
  // C++ callback state. Identity/reciprocal checks remain fixtures here.
  asm volatile("pxor %%xmm0,%%xmm0; pxor %%xmm1,%%xmm1":::"xmm0","xmm1");
  return live && Read<void*>(p,0x1D4)==player && Read<void*>(player,0x1F8)==p;
}
void Reset(bool mapped) {
  player_calls=base_calls=ai_calls=cast_calls=class_calls=resolutions=location_calls=0;live=true;
  for(int i=0;i<2;++i) {
    Put(pawns[i],0x1D4,static_cast<void*>(controllers[i]));Put(pawns[i],0x87C,static_cast<void*>(controllers[i]));
    Put(pawns[i],0x66,uint8_t{3});Put(controllers[i],0x66,uint8_t{3});
    Put(entries[i],0,static_cast<void*>(target_class));Put(entries[i],4,static_cast<void*>(targets[0]));
    Put(collections[i],0x2C,uint32_t{1});
  }
  Put(world,0x39C,uint8_t{0});Put(player,0x1F8,static_cast<void*>(pawns[0]));
  if(mapped){Put(pawns[0],0x1D4,static_cast<void*>(player));Put(pawns[0],0x87C,static_cast<void*>(nullptr));}
  PublishNativePawnTargetingPlayer(mapped ? pawns[0]:nullptr);
  std::memset(output,0xAB,sizeof(output));
}
void Aim(bool mapped,bool valid=true,int fallback=0) {
  Reset(mapped);live=valid;
  if(fallback==1)Put(pawns[0],0x66,uint8_t{1});
  // Direct base oracle exercises None fallback; an admitted role cannot have
  // a None controller, so this is not passed through the mapped resolver.
  if(fallback==2)Put(pawns[0],0x1D4,static_cast<void*>(nullptr));
  auto* fn=At(fallback==2 ? native_pawn_targeting_code::kBaseAimVa:native_pawn_targeting_code::kAimVa);
  auto* result=AimCall(fn,pawns[0]+0x10,output,weapon+0x10,start);assert(result==output);
  const bool fallback_call=fallback==2 || (mapped && valid && fallback==1);
  const unsigned expected=fallback_call ? 30:(!mapped ? 10:(valid ? 20:0));
  for(unsigned i=0;i<3;++i)assert(output[i]==(expected ? expected+i:0));
  assert(base_calls==unsigned(fallback_call));
  assert(player_calls==unsigned(mapped && valid && !fallback));assert(ai_calls==unsigned(!mapped && fallback!=2));
}
void Same(bool mapped) {
  Reset(mapped);auto* fn=At(native_pawn_targeting_code::kSameTargetVa);
  assert(SameCall(fn,pawns[0]+0x10,pawns[1]+0x10)==unsigned(!mapped));
  assert(SameCall(fn,pawns[1]+0x10,pawns[0]+0x10)==unsigned(!mapped));
  assert(SameCall(fn,pawns[1]+0x10,reinterpret_cast<void*>(0x10))==0);
  if(mapped){assert(cast_calls==0 && class_calls==0);return;}
  Put(entries[1],4,static_cast<void*>(targets[1]));assert(SameCall(fn,pawns[0]+0x10,pawns[1]+0x10)==0);
  Put(entries[1],4,static_cast<void*>(nullptr));assert(SameCall(fn,pawns[0]+0x10,pawns[1]+0x10)==0);
  Put(collections[1],0x2C,uint32_t{0});assert(SameCall(fn,pawns[0]+0x10,pawns[1]+0x10)==0);
  Put(world,0x39C,uint8_t{1});Put(pawns[0],0x66,uint8_t{1});
  const auto before=cast_calls;assert(SameCall(fn,pawns[0]+0x10,pawns[1]+0x10)==0);assert(cast_calls==before);
}
void Location(bool mapped,bool valid=true,bool requester=true,uint32_t alternate=0) {
  Reset(mapped);live=valid;
  const uint32_t position[]={0x41234567,0xC2345678,0x00000000};
  std::memcpy(pawns[0]+0xD8,position,sizeof(position));
  // Adjacent sentinel catches compiled/raw receiver mixups.
  Put(pawns[0],0xE8,uint32_t{0xFEE1DEAD});
  const auto before=std::vector<uint8_t>(pawns[0],pawns[0]+sizeof(pawns[0]));
  auto* result=AimCall(At(native_pawn_targeting_code::kLocationVa),pawns[0]+0x10,output,
      requester ? weapon+0x10:reinterpret_cast<void*>(0x10),reinterpret_cast<void*>(alternate));
  assert(result==output && !std::memcmp(pawns[0],before.data(),before.size()));
  for(unsigned i=0;i<3;++i)assert(output[i]==(!mapped ? 40+i:(valid ? position[i]:0)));
  assert(location_calls==unsigned(!mapped));assert(!player_calls && !ai_calls && !base_calls);
}
DWORD WINAPI OtherThread(void*) {
  Reset(true);assert(AimCall(At(native_pawn_targeting_code::kAimVa),pawns[0]+0x10,output,weapon+0x10,start)==output);
  assert(output[0]==0 && output[1]==0 && output[2]==0 && resolutions==0 && player_calls==0);
  assert(AimCall(At(native_pawn_targeting_code::kLocationVa),pawns[0]+0x10,output,
      reinterpret_cast<void*>(0x10),nullptr)==output);
  assert(output[0]==0 && output[1]==0 && output[2]==0 && resolutions==0 && location_calls==0);
  Same(true);assert(!RemoveNativePawnTargeting());return 0;
}
}
extern "C" void* TargetClass(void* cache,void*) {assert(cache==At(0x12B6EBC0));++class_calls;return target_class+0x10;}
extern "C" void* TargetCastImpl(void* field) {
  ++cast_calls;void* c=Read<void*>(field,0);assert(c==controllers[0] || c==controllers[1]);
  return static_cast<uint8_t*>(c)+0x10;
}
extern "C" __attribute__((naked)) void TargetCast(){asm volatile("pushl %eax; call _TargetCastImpl; addl $4,%esp; ret");}
extern "C" void* PlayerAimImpl(void* self,void* out,void* w,void* from) {
  assert(self==player+0x10 && out==output && w==weapon+0x10 && from==start);++player_calls;
  assert(!std::memcmp(AimXmmSentinel,AimXmmObserved,sizeof(AimXmmSentinel)));
  for(unsigned i=0;i<3;++i)static_cast<uint32_t*>(out)[i]=20+i;
  return out;
}
extern "C" __attribute__((naked)) void PlayerAim(){asm volatile("movdqa %xmm0,_AimXmmObserved; movl %esp,%eax; pushl 12(%eax); pushl 8(%eax); pushl 4(%eax); pushl %ecx; call _PlayerAimImpl; addl $16,%esp; ret $12");}
extern "C" void* BaseAimImpl(void* self,void* out) {
  assert(self==pawns[0]+0x10 && out==output);++base_calls;
  for(unsigned i=0;i<3;++i)static_cast<uint32_t*>(out)[i]=30+i;
  return out;
}
extern "C" __attribute__((naked)) void BaseAim(){asm volatile("pushl 4(%esp); pushl %ecx; call _BaseAimImpl; addl $8,%esp; ret $4");}
extern "C" void* AiAimImpl(void* self,void* from,void* out) {
  assert(self==aimers[0]+0x10 && from==start && out==output);++ai_calls;
  for(unsigned i=0;i<3;++i)static_cast<uint32_t*>(out)[i]=10+i;
  return out;
}
extern "C" __attribute__((naked)) void AiAim(){asm volatile("pushl 4(%esp); pushl %ecx; pushl %eax; call _AiAimImpl; addl $12,%esp; ret $4");}
extern "C" void* AiLocationImpl(void* self,void* out) {
  assert(self==aimers[0]+0x10 && out==output);++location_calls;
  for(unsigned i=0;i<3;++i)static_cast<uint32_t*>(out)[i]=40+i;
  return out;
}
extern "C" __attribute__((naked)) void AiLocation(){asm volatile("pushl 4(%esp); pushl %eax; call _AiLocationImpl; addl $8,%esp; ret $4");}
int main(int argc,char** argv) {
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2400000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(image);
  const auto base=reinterpret_cast<uintptr_t>(image);const auto module=reinterpret_cast<HMODULE>(image);
  native_pawn_targeting_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> r){
    const auto b=native_reference_code::Relocate(code,base,r);std::memcpy(At(va),b.data(),b.size());return true;});
  Jump(0x110031A0,reinterpret_cast<void*>(&TargetClass));Jump(0x1097F890,reinterpret_cast<void*>(&TargetCast));
  Jump(0x10BC86E0,reinterpret_cast<void*>(&AiAim));
  Jump(0x10BC7ED0,reinterpret_cast<void*>(&AiLocation));
  for(int i=0;i<2;++i) {
    Put(pawns[i],0x98,static_cast<void*>(world));Put(controllers[i],0x98,static_cast<void*>(world));
    Put(controllers[i],0x2FC,static_cast<void*>(aimers[i]));Put(controllers[i],0x300,static_cast<void*>(collections[i]));
    Put(collections[i],0x28,static_cast<void*>(entries[i]));
  }
  Put(target_class,0x9C,static_cast<void*>(target_default));Put(target_default,0x28,uint32_t{0});
  Put(pawns[0],0x10,At(0x12B20000));Put(At(0x12B20000),0x238,reinterpret_cast<void*>(&BaseAim));
  Put(pawns[0],0,At(0x12B22000));Put(At(0x12B22000),0x1A0,At(native_pawn_targeting_code::kActorLocationVa));
  assert(NativePawnLocationDispatchSupported(module,pawns[0]));
  assert(!NativePawnLocationDispatchSupported(nullptr,pawns[0]));assert(!NativePawnLocationDispatchSupported(module,nullptr));
  assert(!NativePawnLocationDispatchSupported(module,reinterpret_cast<void*>(1)));
  Put(player,0x10,At(0x12B21000));Put(At(0x12B21000),0x240,reinterpret_cast<void*>(&PlayerAim));
  if(argc>1){std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> exe{std::istreambuf_iterator<char>(f),{}};assert(exe.size()==37667328);
    native_pawn_targeting_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> r){
      assert(!std::memcmp(exe.data()+va-0x10900C00,code,sizeof(code)));
      auto expected=native_reference_code::Relocate(code,native_reference_code::kImageBase,{});
      for(size_t p=0x2148A00;p<0x2148A00+0x243ABC;){auto page=Read<uint32_t>(exe.data(),p),size=Read<uint32_t>(exe.data(),p+4);assert(size>=8);
        for(size_t q=p+8;q<p+size;q+=2){auto e=Read<uint16_t>(exe.data(),q);uintptr_t a=native_reference_code::kImageBase+page+(e&4095);
          if((e>>12)==3 && a>=va && a+4<=va+sizeof(code))Put(expected.data(),a-va,Read<uint32_t>(expected.data(),a-va)+uint32_t(base-native_reference_code::kImageBase));}p+=size;}
      assert(expected==native_reference_code::Relocate(code,base,r));return true;});}
  assert(ValidateNativePawnTargeting(module));Aim(false);Same(false);Location(false);
  assert(!InstallNativePawnTargeting(module,GetCurrentThreadId(),nullptr));
  assert(InstallNativePawnTargeting(module,GetCurrentThreadId(),Resolve));assert(ValidateNativePawnTargeting(module));
  assert(!InstallNativePawnTargeting(module,GetCurrentThreadId(),Resolve));
  for(int i=0;i<2000;++i){Aim(false);Aim(true);Aim(true,false);Aim(true,true,1);Aim(true,true,2);Same(false);Same(true);
    Location(false);Location(true);Location(true,false);Location(true,true,false,1);Location(true,true,true,1);}
  Reset(true);Put(At(0x12B22000),0x1A0,At(native_pawn_targeting_code::kBaseAimVa));
  assert(!NativePawnLocationDispatchSupported(module,pawns[0]));
  assert(AimCall(At(native_pawn_targeting_code::kLocationVa),pawns[0]+0x10,output,reinterpret_cast<void*>(0x10),nullptr)==output);
  assert(output[0]==0 && output[1]==0 && output[2]==0);
  Put(At(0x12B22000),0x1A0,At(native_pawn_targeting_code::kActorLocationVa));
  Reset(true);Put(pawns[0],0,reinterpret_cast<void*>(1));
  assert(AimCall(At(native_pawn_targeting_code::kLocationVa),pawns[0]+0x10,output,reinterpret_cast<void*>(0x10),nullptr)==output);
  assert(output[0]==0 && output[1]==0 && output[2]==0);
  Put(pawns[0],0,At(0x12B22000));
  // Observe ABI arguments before executing the full original actor body.
  // This temporary fixture hook is removed before production signature tests.
  InlineHook observer{};
  assert(InstallInlineHook(&observer,At(native_pawn_targeting_code::kActorLocationVa),
      reinterpret_cast<void*>(&LocationObserver),native_pawn_targeting_code::kActorLocation,6,&LocationObserverOriginal));
  Location(true,true,true,1);
  assert(LocationObservedPawn==pawns[0] && LocationObservedRequester==weapon && LocationObservedAlternate==1);
  Location(true,true,false,0);
  assert(LocationObservedPawn==pawns[0] && LocationObservedRequester==nullptr && LocationObservedAlternate==0);
  assert(RemoveInlineHook(&observer));LocationObserverOriginal=nullptr;
  assert(ValidateNativePawnTargeting(module));
  HANDLE t=CreateThread(nullptr,0,OtherThread,nullptr,0,nullptr);assert(t);assert(WaitForSingleObject(t,10000)==WAIT_OBJECT_0);CloseHandle(t);
  native_pawn_targeting_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){auto* p=static_cast<uint8_t*>(At(va));
    for(size_t i=0;i<sizeof(code);++i){p[i]^=1;assert(!ValidateNativePawnTargeting(module));p[i]^=1;}return true;});
  for(const auto& h:g_target_hooks){auto* p=static_cast<uint8_t*>(h.trampoline);DWORD previous=0,ignored=0;assert(VirtualProtect(p,11,PAGE_EXECUTE_READWRITE,&previous));
    for(size_t i=0;i<11;++i){p[i]^=1;assert(!ValidateNativePawnTargeting(module));assert(!RemoveNativePawnTargeting());p[i]^=1;}
    assert(VirtualProtect(p,11,previous,&ignored));}
  for(const auto& h:g_target_hooks){auto* p=static_cast<uint8_t*>(h.target);p[1]^=1;assert(!RemoveNativePawnTargeting());p[1]^=1;}
  assert(RemoveNativePawnTargeting());assert(!NativePawnTargetingInstalled());assert(ValidateNativePawnTargeting(module));Aim(false);Same(false);Location(false);
  native_pawn_targeting_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){auto* p=static_cast<uint8_t*>(At(va));
    for(size_t i=0;i<sizeof(code);++i){p[i]^=1;assert(!InstallNativePawnTargeting(module,GetCurrentThreadId(),Resolve));p[i]^=1;}return true;});
  assert(VirtualFree(image,0,MEM_RELEASE));
  std::puts("NativePawnTargetingTests passed: 2,000 native aim/kill-target/location cycles, raw actor ABI, dispatch/identity guards and SIMD preservation; engine/identity fixtures, not gameplay");
}
