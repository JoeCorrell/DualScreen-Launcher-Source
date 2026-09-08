// Executes exact shipping Transform (including its enabled branch) and the
// StartVehicle missing-form branch. Movement/notification engine calls are
// fixtures; this does not test normal vehicle AI or a gameplay handoff.
#include "NativePawnTransform.cpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
using namespace wfcds;
extern "C" void* WfcCallEaxPointer(void*,void*,void*);
namespace {
uint8_t* image=nullptr;
alignas(16) uint8_t pawns[3][0x900]{},transformation[0x100]{};
unsigned moved=0,notified=0;
using TransformFn=uint32_t (__attribute__((fastcall)) *)(void*,void*,uint32_t,void*);
template<class T>T Read(const void* p,size_t o){T v{};std::memcpy(&v,static_cast<const uint8_t*>(p)+o,sizeof(v));return v;}
template<class T>void Put(void* p,size_t o,T v){std::memcpy(static_cast<uint8_t*>(p)+o,&v,sizeof(v));}
void* At(uintptr_t va){return image+va-native_reference_code::kImageBase;}
void __attribute__((stdcall)) Move(void* compiled,uint32_t form){
  assert(compiled==pawns[1]+0x10 && form<=1);
  assert(Read<uint8_t>(pawns[1],0x760)==form && Read<uint8_t>(pawns[1],0x670)==form);++moved;
}
void __attribute__((fastcall)) Notify(void* compiled,void*){assert(compiled==pawns[1]+0x10);++notified;}
uint32_t Transform(int index,uint32_t form,void* delegate){return reinterpret_cast<TransformFn>(At(native_pawn_transform_code::kTransformVa))(pawns[index]+0x10,nullptr,form,delegate);}
void Start(int index){WfcCallEaxPointer(At(native_pawn_transform_code::kStartVehicleVa),nullptr,pawns[index]+0x10);}
DWORD WINAPI OffThread(void*){
  assert(!RemoveNativePawnTransform());
  assert(!Transform(0,1,reinterpret_cast<void*>(1)));Start(0);return 0;
}
}
int main(int argc,char** argv){
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2400000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(image);
  const auto base=reinterpret_cast<uintptr_t>(image);const auto module=reinterpret_cast<HMODULE>(image);
  void* table=At(0x12688E0C);
  native_pawn_transform_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> r){auto b=native_reference_code::Relocate(code,base,r);std::memcpy(At(va),b.data(),b.size());return true;});
  TransformJump(static_cast<uint8_t*>(At(0x10B05D20)),At(0x10B05D20),reinterpret_cast<void*>(&Move));
  Put(table,0x310,reinterpret_cast<void*>(&Notify));
  Put(table,0x30C,At(native_pawn_transform_code::kTransformVa));
  for(auto& pawn:pawns)Put(pawn,0x10,static_cast<void*>(table));
  Put(pawns[1],0x774,static_cast<void*>(transformation));Put(transformation,0x48,uint32_t{1});
  if(argc>1){std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> exe{std::istreambuf_iterator<char>(f),{}};assert(exe.size()==37667328);
    assert(Read<uint32_t>(exe.data(),0x12688E0C+0x30C-0x10901200)==native_pawn_transform_code::kTransformVa);
    assert(Read<uint32_t>(exe.data(),0x12688E0C+0x310-0x10901200)==0x10905EE0);
    native_pawn_transform_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> r){
      assert(!std::memcmp(exe.data()+va-0x10900C00,code,sizeof(code)));
      auto expected=native_reference_code::Relocate(code,native_reference_code::kImageBase,{});
      for(size_t p=0x2148A00;p<0x2148A00+0x243ABC;){auto page=Read<uint32_t>(exe.data(),p),size=Read<uint32_t>(exe.data(),p+4);assert(size>=8);
        for(size_t q=p+8;q<p+size;q+=2){auto e=Read<uint16_t>(exe.data(),q);uintptr_t a=native_reference_code::kImageBase+page+(e&4095);
          if((e>>12)==3 && a>=va && a+4<=va+sizeof(code))Put(expected.data(),a-va,Read<uint32_t>(expected.data(),a-va)+uint32_t(base-native_reference_code::kImageBase));}p+=size;}
      assert(expected==native_reference_code::Relocate(code,base,r));return true;});}
  assert(ValidateNativePawnTransform(module));assert(!Transform(0,0,nullptr));Start(0);
  for(auto& pawn:pawns)assert(NativePawnTransformDispatchSupported(module,pawn));
  Put(table,0x30C,static_cast<void*>(nullptr));assert(!NativePawnTransformDispatchSupported(module,pawns[0]));
  Put(table,0x30C,reinterpret_cast<void*>(&Notify));assert(!NativePawnTransformDispatchSupported(module,pawns[0]));
  Put(table,0x30C,At(native_pawn_transform_code::kTransformVa));
  assert(!NativePawnTransformDispatchSupported(module,nullptr));
  assert(!NativePawnTransformDispatchSupported(module,reinterpret_cast<void*>(UINTPTR_MAX-1)));
  assert(!InstallNativePawnTransform(module,0));
  assert(InstallNativePawnTransform(module,GetCurrentThreadId()));assert(ValidateNativePawnTransform(module));
  assert(!InstallNativePawnTransform(module,GetCurrentThreadId()));
  PublishNativePawnTransformGuard(pawns[0]);
  // Poisoned delegate/vehicle prove the denied path never reads actor fields
  // or performs any stock writes. Other identities still use stock branches.
  Put(pawns[0],0x544,uint32_t{0x20});Put(pawns[0],0x580,reinterpret_cast<void*>(1));
  const std::vector<uint8_t> before(pawns[0],pawns[0]+sizeof(pawns[0]));
  for(int n=0;n<4000;++n){
    assert(!Transform(0,1,reinterpret_cast<void*>(1)));Start(0);
    assert(!std::memcmp(pawns[0],before.data(),before.size()));
    assert(!Transform(2,1,nullptr));Start(2);
    uint32_t delegate[4]={uint32_t(n),0x12345678,0xABCDEF01,0x90ABCDEF};
    Put(pawns[1],0x544,uint32_t{0x20});
    const uint32_t form=n&1;
    assert(Transform(1,form,delegate)==1);
    assert(Read<uint8_t>(pawns[1],0x760)==form && Read<uint8_t>(pawns[1],0x670)==form);
    assert(!std::memcmp(pawns[1]+0x784,delegate,sizeof(delegate)));
  }
  assert(moved==4000 && notified==4000);
  HANDLE thread=CreateThread(nullptr,0,OffThread,nullptr,0,nullptr);assert(thread);
  assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);CloseHandle(thread);
  native_pawn_transform_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){auto* p=static_cast<uint8_t*>(At(va));for(size_t i=0;i<sizeof(code);++i){p[i]^=1;assert(!ValidateNativePawnTransform(module));p[i]^=1;}return true;});
  for(auto& h:g_transform_hooks){auto* p=static_cast<uint8_t*>(h.trampoline);DWORD old=0,ignored=0;
    assert(VirtualProtect(p,h.stolen_bytes+5,PAGE_EXECUTE_READWRITE,&old));
    for(size_t i=0;i<h.stolen_bytes+5;++i){p[i]^=1;assert(!ValidateNativePawnTransform(module));assert(!RemoveNativePawnTransform());assert(WfcPawnTransformBlocked==reinterpret_cast<uintptr_t>(pawns[0]+0x10));p[i]^=1;}
    assert(VirtualProtect(p,h.stolen_bytes+5,old,&ignored));}
  assert(RemoveNativePawnTransform());assert(!NativePawnTransformInstalled());assert(!WfcPawnTransformBlocked);assert(ValidateNativePawnTransform(module));
  Put(pawns[0],0x544,uint32_t{0});Put(pawns[0],0x580,static_cast<void*>(nullptr));assert(!Transform(0,1,nullptr));Start(0);
  native_pawn_transform_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){auto* p=static_cast<uint8_t*>(At(va));for(size_t i=0;i<sizeof(code);++i){p[i]^=1;assert(!InstallNativePawnTransform(module,GetCurrentThreadId()));p[i]^=1;}return true;});
  assert(VirtualFree(image,0,MEM_RELEASE));std::puts("NativePawnTransformTests passed: 4,000 guarded/stock batches, enabled transform state/delegate preservation, off-thread denial, exact PE bytes/relocations and patch ownership (engine callbacks are fixtures)");
}
