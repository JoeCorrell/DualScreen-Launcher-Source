// Execute shipping controller iteration, visibility guards and cleanup. Native
// world iteration, deep vector/rotation math, anchor service and teleport are
// fixtures. No replacement navigation algorithm is installed by the mod.
#include "NativePlayerCombat.cpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>
using namespace wfcds;
extern "C" void RoleTestOwnerCall(void*,void*,void*,void*,void*);
extern "C" void RoleTestCameraSetCall(void*,void*,void*,void*);
namespace {
alignas(16) uint8_t pawns[3][0x900]{},controllers[3][0x800]{},buddy[0x900]{},action[0x100]{},anchor[0x200]{},destination[0x200]{},
    world[0x500]{},root[0x100]{},root_a[0x100]{},root_b[0x100]{},finder[0x100]{},navpoint[0x200]{},
    pawn_table[0x300]{},iterator[0x40]{},iterator_table[0x20]{},player_class[0x100]{},controller_class[0x100]{},output[560]{};
uint8_t* image=nullptr;bool mapped=false,live=true,can_anchor=true,find_anchor=true;
std::vector<int> sequence;size_t cursor=0;unsigned resolutions=0,iterations=0,destructions=0,teleports=0,anchor_calls=0,view_calls=0;
int current=0;bool flip_view=false;bool invalidate_iteration=false;
template<class T>T Read(const void* p,size_t o){T v{};std::memcpy(&v,static_cast<const uint8_t*>(p)+o,sizeof(v));return v;}
template<class T>void Put(void* p,size_t o,T v){std::memcpy(static_cast<uint8_t*>(p)+o,&v,sizeof(v));}
void* At(uintptr_t va){return image+va-native_reference_code::kImageBase;}
void* Compiled(void* p){return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(p)+0x10);}
void Jump(uintptr_t va,void* f){auto* p=static_cast<uint8_t*>(At(va));p[0]=0xE9;Put(p,1,uint32_t(reinterpret_cast<uintptr_t>(f)-reinterpret_cast<uintptr_t>(p)-5));}
bool Resolve(void* p,bool melee){assert(p==pawns[1] && !melee);++resolutions;
  asm volatile("fninit; fldz; pxor %%xmm0,%%xmm0; pxor %%xmm1,%%xmm1; pxor %%xmm2,%%xmm2; pxor %%xmm3,%%xmm3; pxor %%xmm4,%%xmm4; pxor %%xmm5,%%xmm5; pxor %%xmm6,%%xmm6; pxor %%xmm7,%%xmm7"
      :::"xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","memory");
  return live && Read<void*>(p,0x1D4)==controllers[0] && Read<void*>(controllers[0],0x1C8)==p;
}
void Position(void* p,float x,float y=0,float z=0){Put(p,0xD8,x);Put(p,0xDC,y);Put(p,0xE0,z);}
void Reset(bool mapping,int who){mapped=mapping;current=who;live=can_anchor=find_anchor=true;flip_view=invalidate_iteration=false;
  cursor=0;resolutions=iterations=destructions=teleports=anchor_calls=view_calls=0;sequence={who};
  for(int i=0;i<3;++i){auto* c=controllers[mapping && i<2 ? 1-i:i];Put(pawns[i],0x1D4,static_cast<void*>(c));Put(c,0x1C8,static_cast<void*>(pawns[i]));
    Put(c,0x98,static_cast<void*>(world));Put(c,0x66,uint8_t{3});Put(c,0x2D4,static_cast<void*>(nullptr));Put(c,0x314,90.0f);Position(pawns[i],0);}
  Put(action,0x94,static_cast<void*>(buddy));Put(buddy,0x98,static_cast<void*>(world));Put(buddy,0x2A4,static_cast<void*>(anchor));
  Put(buddy,0x2A8,10.0f);Put(world,0x338,10.0f);Put(world,0x39C,uint8_t{0});
  Position(anchor,-100);Position(buddy,-200);Position(destination,-300);
  PublishNativePlayerCombatPawns(mapping ? pawns[1]:nullptr,mapping ? pawns[0]:nullptr);
}
void Abi(){assert(Read<uint32_t>(output,0)==0x55667788 && Read<uint32_t>(output,4)==0x44556677 &&
    Read<uint32_t>(output,16)==0x11223344 && Read<uint32_t>(output,12)==Read<uint32_t>(output,40));
  // Native FOV/dot-product arithmetic changes x87 condition/precision flags;
  // control mode and the pre-existing stack entry must remain intact.
  assert(Read<uint16_t>(output,48)==0x37F && (Read<uint16_t>(output,50)&0x3800)==0x3800);}
void Navigate(){RoleTestOwnerCall(At(native_player_combat_code::kNavigateVa),Compiled(action),output,Compiled(action),Compiled(destination));Abi();}
void* Find(){RoleTestCameraSetCall(At(native_player_combat_code::kSpawnFinderVa),Compiled(finder),output,Compiled(finder));Abi();return Read<void*>(output,28);}
DWORD WINAPI OffThread(void*){Reset(true,1);Navigate();assert(!teleports && destructions==1 && !resolutions);Reset(true,1);assert(Find()==Compiled(nullptr));assert(!anchor_calls && destructions==1 && !resolutions && !RemoveNativePlayerCombat());return 0;}
}
extern "C" void* NavigationClass(void* cache,void*){if(cache==At(0x12B6F3FC))return Compiled(controller_class);assert(cache==At(0x12B6EDDC));return Compiled(player_class);}
extern "C" uint32_t __attribute__((fastcall)) NavigationIsA(void* p,void*,void* type){assert(type==player_class);return p==pawns[0];}
extern "C" void NavigationBarrier(void*){}
extern "C" uint32_t __attribute__((fastcall)) NavigationCanAnchor(void* p,void*){assert(p==Compiled(buddy));return can_anchor;}
extern "C" void __attribute__((fastcall)) IteratorDestroy(void* p,void*,int release){assert(p==iterator && release==1);++destructions;}
extern "C" uint32_t __attribute__((fastcall)) Iterate(void* w,void*,void** state,void* type,void* result){
  assert(w==world && type==controller_class);++iterations;if(!*state)*state=iterator;else assert(*state==iterator);
  if(cursor==sequence.size())return 0;
  int index=sequence[cursor++];
  Put(result,0,static_cast<void*>(controllers[mapped && index<2 ? 1-index:index]));if(invalidate_iteration)live=false;return 1;
}
extern "C" void* __attribute__((stdcall)) NavigationAnchor(void* p,void* f){assert(f==Compiled(finder));++anchor_calls;
  assert(p==Compiled(pawns[current]));return Compiled(find_anchor ? navpoint:nullptr);}
extern "C" void NavigationAnchorThunk();
asm(".text\n.globl _NavigationAnchorThunk\n_NavigationAnchorThunk:\n popl %edx\n pushl %edi\n pushl %edx\n jmp _NavigationAnchor@8\n");
extern "C" void* __attribute__((stdcall)) NavigationNormalize(void* in,void* out){float values[3];std::memcpy(values,in,12);const float d=std::sqrt(values[0]*values[0]+values[1]*values[1]+values[2]*values[2]);assert(d>0);for(int i=0;i<3;++i)Put(out,i*4,values[i]/d);return out;}
extern "C" void NavigationNormalizeThunk();
asm(".text\n.globl _NavigationNormalizeThunk\n_NavigationNormalizeThunk:\n pushl %edi\n pushl %esi\n call _NavigationNormalize@8\n ret\n");
extern "C" void* __attribute__((stdcall)) NavigationView(void* out,void* p){assert(p==Compiled(pawns[current]));++view_calls;Put(out,0,0);Put(out,4,flip_view ? 32768:0);Put(out,8,0);return out;}
extern "C" void NavigationViewThunk();
asm(".text\n.globl _NavigationViewThunk\n_NavigationViewThunk:\n popl %edx\n pushl %esi\n pushl %edx\n jmp _NavigationView@8\n");
extern "C" void* __attribute__((stdcall)) NavigationVector(void* in,void* out){Put(out,0,Read<int>(in,4) ? -1.0f:1.0f);Put(out,4,0.0f);Put(out,8,0.0f);return out;}
extern "C" void NavigationVectorThunk();
asm(".text\n.globl _NavigationVectorThunk\n_NavigationVectorThunk:\n pushl %esi\n pushl %ecx\n call _NavigationVector@8\n ret\n");
extern "C" void* __attribute__((stdcall)) NavigationRotator(void* in,void* out){for(int i=0;i<3;++i)assert(Read<float>(in,i*4)==Read<float>(destination,0xD8+i*4)-Read<float>(anchor,0xD8+i*4));Put(out,0,11);Put(out,4,22);Put(out,8,33);return out;}
extern "C" void NavigationRotatorThunk();
// The rotation helper is cdecl: its caller, unlike GetViewRotation, pops the
// original output pointer. Keep that argument on the shipping stack.
asm(".text\n.globl _NavigationRotatorThunk\n_NavigationRotatorThunk:\n pushl 4(%esp)\n pushl %esi\n call _NavigationRotator@8\n ret\n");
extern "C" void __attribute__((fastcall)) NavigationTeleport(void* p,void*,void* location,void* rotation){assert(p==Compiled(buddy) && location==anchor+0xD8);assert(Read<int>(rotation,0)==11 && Read<int>(rotation,4)==22 && Read<int>(rotation,8)==33 && destructions==1);++teleports;}
LONG WINAPI NavigationException(PEXCEPTION_POINTERS p) {
    std::fprintf(stderr,"Navigation fixture exception %08lx at %08lx (image %p), eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx esp=%08lx; iteration=%u view=%u teleport=%u\n",
        p->ExceptionRecord->ExceptionCode,p->ContextRecord->Eip,image,p->ContextRecord->Eax,p->ContextRecord->Ebx,p->ContextRecord->Ecx,p->ContextRecord->Edx,p->ContextRecord->Esi,p->ContextRecord->Edi,p->ContextRecord->Esp,iterations,view_calls,teleports);
    return EXCEPTION_CONTINUE_SEARCH;
}
int main(int argc,char** argv){
  AddVectoredExceptionHandler(1,&NavigationException);
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2400000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(image);
  const auto base=reinterpret_cast<uintptr_t>(image);const auto module=reinterpret_cast<HMODULE>(image);
  native_player_combat_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> r){auto b=native_reference_code::Relocate(code,base,r);std::memcpy(At(va),b.data(),b.size());return true;});
  Jump(0x110031A0,reinterpret_cast<void*>(&NavigationClass));Jump(0x10901430,reinterpret_cast<void*>(&NavigationIsA));Jump(0x116FC820,reinterpret_cast<void*>(&NavigationBarrier));
  Jump(0x114AF380,reinterpret_cast<void*>(&Iterate));Jump(0x10B505A0,reinterpret_cast<void*>(&NavigationAnchorThunk));
  Jump(0x1094D260,reinterpret_cast<void*>(&NavigationNormalizeThunk));Jump(0x11782F30,reinterpret_cast<void*>(&NavigationViewThunk));
  Jump(0x116D1AB0,reinterpret_cast<void*>(&NavigationVectorThunk));Jump(0x1094D390,reinterpret_cast<void*>(&NavigationRotatorThunk));
  Put(buddy,0x10,static_cast<void*>(pawn_table));Put(pawn_table,0x1F8,reinterpret_cast<void*>(&NavigationCanAnchor));Put(pawn_table,0x218,reinterpret_cast<void*>(&NavigationTeleport));
  Put(iterator,0,static_cast<void*>(iterator_table));Put(iterator_table,0,reinterpret_cast<void*>(&IteratorDestroy));
  Put(At(0x12B10A9C),0,static_cast<void*>(root));Put(root,0x40,static_cast<void*>(root_a));Put(root_a,0x28,static_cast<void*>(root_b));Put(root_b,0,static_cast<void*>(world));
  Put(At(0x129D2330),0,uint64_t{0x3F91DF46A0000000});Put(At(0x129D2138),0,uint64_t{0x3FE0000000000000});
  if(argc>1){std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> exe{std::istreambuf_iterator<char>(f),{}};assert(exe.size()==37667328);
  for(uintptr_t va:{0x129D2330u,0x129D2138u})assert(!std::memcmp(At(va),exe.data()+va-0x10901200,8));
  native_player_combat_code::VisitNavigationRole([&](uintptr_t va,const auto& code,std::initializer_list<size_t> r){assert(!std::memcmp(exe.data()+va-0x10900C00,code,sizeof(code)));
    auto expected=native_reference_code::Relocate(code,native_reference_code::kImageBase,{});
    for(size_t p=0x2148A00;p<0x2148A00+0x243ABC;){auto page=Read<uint32_t>(exe.data(),p),size=Read<uint32_t>(exe.data(),p+4);assert(size>=8);
      for(size_t q=p+8;q<p+size;q+=2){auto e=Read<uint16_t>(exe.data(),q);uintptr_t a=native_reference_code::kImageBase+page+(e&4095);
        if((e>>12)==3 && a>=va && a+4<=va+sizeof(code))Put(expected.data(),a-va,Read<uint32_t>(expected.data(),a-va)+uint32_t(base-native_reference_code::kImageBase));}p+=size;}
    assert(expected==native_reference_code::Relocate(code,base,r));return true;});}
  assert(ValidateNativePlayerCombat(module));assert(InstallNativePlayerCombat(module,GetCurrentThreadId(),Resolve,+[](void*,void*,void*){return false;}));unsigned cases=0;
  for(unsigned repeat=0;repeat<64;++repeat)for(bool mapping:{false,true})for(unsigned flags=0;flags<16;++flags){
    Reset(mapping,mapping ? 1:0);Position(anchor,(flags&1) ? 100.0f:-100.0f);Position(buddy,(flags&2) ? 200.0f:-200.0f);flip_view=(flags&4)!=0;
    if(flags&8)sequence.insert(sequence.begin(),2);
    auto before=std::vector<uint8_t>(buddy,buddy+sizeof(buddy));Navigate();
    const bool visible=flip_view ? !(flags&1)||!(flags&2):(flags&1)||(flags&2);assert(teleports==unsigned(!visible) && destructions==1);
    assert(!std::memcmp(buddy,before.data(),before.size()));++cases;
    Reset(mapping,mapping ? 1:0);find_anchor=(flags&1)!=0;if(flags&8)sequence.insert(sequence.begin(),2);
    assert(Find()==Compiled(find_anchor ? navpoint:nullptr));assert(anchor_calls==1 && destructions==1);++cases;
  }
  for(int failure=0;failure<3;++failure){Reset(true,1);if(failure==0)Put(buddy,0x2A8,9.0f);if(failure==1)Put(buddy,0x2A4,static_cast<void*>(nullptr));if(failure==2)can_anchor=false;Navigate();assert(!teleports && !iterations && !destructions);}
  Reset(true,1);live=false;Navigate();assert(!teleports && destructions==1 && !view_calls);
  Reset(true,1);invalidate_iteration=true;Navigate();assert(!teleports && destructions==1 && !view_calls);
  Reset(true,0);Navigate();assert(!teleports && destructions==1);Reset(true,1);Put(pawns[1],0x1D4,static_cast<void*>(controllers[2]));Navigate();assert(!teleports && destructions==1);
  Reset(true,1);live=false;assert(Find()==Compiled(nullptr) && !anchor_calls && destructions==1);
  Reset(true,1);sequence.clear();Navigate();assert(teleports==1 && destructions==1);Reset(true,1);sequence.clear();assert(Find()==Compiled(nullptr) && !anchor_calls && destructions==1);
  HANDLE thread=CreateThread(nullptr,0,OffThread,nullptr,0,nullptr);assert(thread);assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);CloseHandle(thread);
  native_player_combat_code::VisitNavigationRole([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){auto* p=static_cast<uint8_t*>(At(va));for(size_t i=0;i<sizeof(code);++i){p[i]^=1;assert(!ValidateNativePlayerCombat(module));p[i]^=1;}return true;});
  for(size_t i=18;i<20;++i){auto& p=g_combat_patches[i];p.target[0]^=1;assert(!RemoveNativePlayerCombat());p.target[0]^=1;}
  assert(RemoveNativePlayerCombat());assert(ValidateNativePlayerCombat(module));Reset(false,0);assert(Find()==Compiled(navpoint));
  assert(VirtualFree(image,0,MEM_RELEASE));std::printf("NativePlayerNavigationTests passed: %u native visibility/finder cases, iterator cleanup, FOV/anchor guards, abort-not-teleport on invalid mapped observer, stale/thread/ABI/FP/signatures (world/math/anchor/teleport fixtures, not gameplay)\n",cases);
}
