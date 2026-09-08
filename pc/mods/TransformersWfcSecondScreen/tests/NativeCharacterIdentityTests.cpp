// Executes the retained setter, resolved/base copy and refresh bytes through
// the production EDI/EAX adapter. Allocator, class lookup and UI subscriber
// endpoints are fixtures, NOT an in-game handoff or native menu test.
#include "NativeCharacterIdentity.cpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <fstream>
#include <vector>
using namespace wfcds;
namespace {
struct Object { alignas(4) uint8_t bytes[0x900]{};uint32_t id=0;bool valid=true; };
Object pri,pawn,controller,world,provider,definition,pri_class,chassis,weapon_class,other;
Object* objects[]={&pri,&pawn,&controller,&world,&provider,&definition,&pri_class,&chassis,&weapon_class,&other};
uint8_t* image=nullptr;
bool ai=false,deleting=false,writes=true,bad_layout=false,wrong_pair=false;
unsigned callbacks=0,allocations=0,dirty=0,reentry=0;
Object* Find(void* p) { for(auto* o:objects) if(p==o->bytes && o->valid) return o;return nullptr; }
template<class T> T Read(const void* p,size_t n){T v{};std::memcpy(&v,static_cast<const uint8_t*>(p)+n,sizeof(v));return v;}
template<class T> void Put(void* p,size_t n,T v){std::memcpy(static_cast<uint8_t*>(p)+n,&v,sizeof(v));}
void Assign(UnrealArrayView* d,const UnrealArrayView* s,size_t width){
  if(d==s)return;
  void* allocation=s->count ? std::malloc(s->count*width):nullptr;
  if(allocation){assert(allocation);std::memcpy(allocation,s->data,s->count*width);++allocations;}
  if(d->data){std::free(d->data);--allocations;}
  *d={allocation,s->count,s->count};
}
void String(void* p,size_t offset,const wchar_t* value){
  UnrealArrayView s{const_cast<wchar_t*>(value),static_cast<int32_t>(std::wcslen(value)+1),static_cast<int32_t>(std::wcslen(value)+1)};
  Assign(reinterpret_cast<UnrealArrayView*>(static_cast<uint8_t*>(p)+offset),&s,2);
}
void Cleanup(){
  for(auto* base:{pri.bytes+0x3C4,pawn.bytes+0x500})for(uint32_t o:kStorageOffsets){
    auto* a=reinterpret_cast<UnrealArrayView*>(base+o);if(a->data){std::free(a->data);--allocations;}*a={};
  }
  auto* display=reinterpret_cast<UnrealArrayView*>(pri.bytes+0x484);
  if(display->data){std::free(display->data);--allocations;}*display={};assert(!allocations);
}
void InitializeValue(void* base,const wchar_t* name){
  String(base,0,name);String(base,0x10,L"Leader");
  for(size_t i=2;i<7;++i){
    void* types[]={weapon_class.bytes,chassis.bytes};uint8_t levels[]={1,2};
    UnrealArrayView s{i==3 ? static_cast<void*>(levels):static_cast<void*>(types),2,2};
    Assign(reinterpret_cast<UnrealArrayView*>(static_cast<uint8_t*>(base)+kStorageOffsets[i]),&s,kStorageWidths[i]);
  }
  Put(base,0x5C,static_cast<void*>(chassis.bytes));Put(base,0x6C,static_cast<void*>(weapon_class.bytes));
  Put(base,0x64,0xff123456u);Put(base,0x68,0xff789abcu);Put(base,0x84,0.7f);
  Put(base,0x80,0xa5a5a5a5u);
}
void Reset(){
  Cleanup();uint32_t id=1;for(auto* o:objects){*o={};o->id=id++;}
  ai=deleting=bad_layout=wrong_pair=false;writes=true;callbacks=dirty=reentry=0;
  Put(pri.bytes,0x2C,static_cast<void*>(controller.bytes));Put(pri.bytes,0x98,static_cast<void*>(world.bytes));
  Put(controller.bytes,0x98,static_cast<void*>(world.bytes));controller.bytes[0x66]=3;
  Put(controller.bytes,0x2E8,static_cast<void*>(provider.bytes));
  Put(chassis.bytes,8,0x800000u);Put(weapon_class.bytes,8,0x800000u);
  InitializeValue(pri.bytes+0x3C4,L"Bumblebee");InitializeValue(pawn.bytes+0x500,L"Optimus");
  String(pri.bytes,0x484,L"Scout");
  assert(NativeCharacterIdentityReady(reinterpret_cast<HMODULE>(image),GetCurrentThreadId(),pri.bytes,pawn.bytes));
}
void Jump(uintptr_t va,void* fn){auto* p=image+va-native_reference_code::kImageBase;p[0]=0xe9;
  Put(p,1,static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fn)-reinterpret_cast<uintptr_t>(p+5)));}
template<size_t N> void Body(uintptr_t va,const std::array<uint8_t,N>& code){std::memcpy(image+va-native_reference_code::kImageBase,code.data(),N);}
bool Ready(){return NativeCharacterIdentityReady(reinterpret_cast<HMODULE>(image),GetCurrentThreadId(),pri.bytes,pawn.bytes);}
void Reject(){const auto before=pri;assert(!Ready());assert(NativeAssignCurrentCharacter(pri.bytes,pawn.bytes)==NativePlayerServiceResult::kRejected);
  assert(!std::memcmp(before.bytes,pri.bytes,sizeof(pri.bytes)));assert(!callbacks);}
DWORD WINAPI OtherThread(void*){assert(!Ready());assert(NativeAssignCurrentCharacter(pri.bytes,pawn.bytes)==NativePlayerServiceResult::kRejected);return 0;}
}
extern "C" void IdentityTestStringImpl(void* d,const UnrealArrayView* s){Assign(static_cast<UnrealArrayView*>(d),s,2);}
extern "C" __attribute__((naked)) void IdentityTestString(){asm volatile("pushl 4(%esp)\n pushl %ecx\n call _IdentityTestStringImpl\n addl $8,%esp\n ret $4");}
extern "C" void IdentityTestArray(UnrealArrayView* d,const UnrealArrayView* s,unsigned width){Assign(d,s,width);}
extern "C" __attribute__((naked)) void IdentityTestClassArray(){asm volatile("pushl $4\n pushl %edi\n pushl %eax\n call _IdentityTestArray\n addl $12,%esp\n ret");}
extern "C" __attribute__((naked)) void IdentityTestByteArray(){asm volatile("pushl $1\n pushl %edi\n pushl %eax\n call _IdentityTestArray\n addl $12,%esp\n ret");}
extern "C" void* IdentityTestClass(void*,void*){return pri_class.bytes+0x10;}
extern "C" int IdentityTestCastImpl(void* object,void*){assert(object==controller.bytes);return ai ? 0:1;}
extern "C" __attribute__((naked)) void IdentityTestCast(){asm volatile("pushl 4(%esp)\n pushl %ecx\n call _IdentityTestCastImpl\n addl $8,%esp\n ret $4");}
extern "C" void __attribute__((stdcall)) IdentityTestRefresh(void* self,uint32_t field,uint32_t invalidate,void* ignored){
  assert(self==provider.bytes+0x10 && field==0 && invalidate==1 && ignored==reinterpret_cast<void*>(0x10));++callbacks;
  if(reentry){assert(!Ready());assert(NativeAssignCurrentCharacter(pri.bytes,pawn.bytes)==NativePlayerServiceResult::kRejected);}
  if(dirty==1)++pawn.id;
  if(dirty==2)Put(controller.bytes,0x98,static_cast<void*>(other.bytes));
  if(dirty==3)pawn.bytes[0x500+0x64]^=1;
  if(dirty==4)pri.bytes[0x314]^=1;
  if(dirty==5)wrong_pair=true;
  if(dirty==6)Put(controller.bytes,0x2E8,static_cast<void*>(other.bytes));
}
namespace wfcds {
bool UnrealObjectValid(void* p){return Find(p)!=nullptr;}
uint32_t UnrealObjectIndex(void* p){auto* o=Find(p);return o ? o->id:0;}
void* UnrealObjectClass(void* p){return Find(p) ? pri_class.bytes:nullptr;}
bool UnrealObjectReferenceWritesReady(){return writes;}
bool UnrealObjectIsA(void* p,const wchar_t* type){if(!Find(p))return false;
  if(p==pri.bytes)return !std::wcscmp(type,L"TnPlayerReplicationInfo");
  if(p==pawn.bytes)return !std::wcscmp(type,L"TnPawn");
  if(p==controller.bytes)return !std::wcscmp(type,L"Controller") || !std::wcscmp(type,ai ? L"TnAiController":L"TnPlayerController");
  if(p==world.bytes)return !std::wcscmp(type,L"WorldInfo");
  if(p==provider.bytes)return !std::wcscmp(type,L"PlayerOwnerDataStore");
  if(p==chassis.bytes)return !std::wcscmp(type,L"Class") || !std::wcscmp(type,L"TnTransformer");
  if(p==weapon_class.bytes)return !std::wcscmp(type,L"Class");
  return false;
}
bool UnrealReadBoolProperty(void*,const wchar_t* field,bool* out){if(std::wcscmp(field,L"bDeleteMe"))return false;*out=deleting;return true;}
bool UnrealReadByteProperty(void* p,const wchar_t* field,uint8_t* out){
  if(p==world.bytes && !std::wcscmp(field,L"NetMode")){*out=world.bytes[0x39C];return true;}
  if(p==controller.bytes && !std::wcscmp(field,L"Role")){*out=controller.bytes[0x66];return true;}return false;
}
bool UnrealResolveObjectPropertyOffset(void*,const wchar_t* field,uint32_t* out){
  if(!std::wcscmp(field,L"WorldInfo"))*out=0x98;else if(!std::wcscmp(field,L"Owner"))*out=0x2C;
  else if(!std::wcscmp(field,L"CurrentPlayerData"))*out=0x2E8;else return false;
  return true;
}
bool UnrealReadObjectProperty(void* p,const wchar_t* field,void** out){
  if(p==controller.bytes && !std::wcscmp(field,L"Pawn")){*out=wrong_pair ? other.bytes:pawn.bytes;return true;}
  if(p==controller.bytes && !std::wcscmp(field,L"PlayerReplicationInfo")){*out=pri.bytes;return true;}
  if(p==pawn.bytes && !std::wcscmp(field,L"Controller")){*out=controller.bytes;return true;}
  if(p==pawn.bytes && !std::wcscmp(field,L"WorldInfo")){*out=world.bytes;return true;}
  uint32_t offset=0;if(!UnrealResolveObjectPropertyOffset(p,field,&offset))return false;*out=Read<void*>(p,offset);return true;
}
bool UnrealReadStructProperty(void* p,const wchar_t* field,UnrealStructValueView* out){
  if(!std::wcscmp(field,L"_CurrentCharacter") && (p==pri.bytes || p==pawn.bytes)){
    *out={static_cast<uint8_t*>(p)+(p==pri.bytes ? 0x3C4:0x500),0x88,definition.bytes};return true;}
  if(p==pri.bytes && !std::wcscmp(field,L"_SelectedCharacter")){*out={pri.bytes+0x314,0xB0,definition.bytes};return true;}
  if(p==pri.bytes && !std::wcscmp(field,L"ReplicatedCharacterData")){*out={pri.bytes+0x250,0xB4,definition.bytes};return true;}
  return false;
}
bool UnrealResolveStructPropertyOffset(void* p,const wchar_t* field,uint32_t size,uint32_t* out){
  if(p!=pri.bytes || std::wcscmp(field,L"_CurrentCharacter") || size!=0x88)return false;
  *out=0x3C4;return true;
}
bool UnrealResolveStructFieldOffset(const UnrealStructValueView& v,const wchar_t* name,const wchar_t* type,uint32_t size,uint32_t* out){
  if(bad_layout)return false;
  if(v.data==pri.bytes){if(std::wcscmp(type,L"StrProperty") || size!=12)return false;
    *out=!std::wcscmp(name,L"CurrentCharacterString") ? 0x484:0x490;return true;}
  for(const auto& f:kIdentityFields)if(!std::wcscmp(f.name,name) && !std::wcscmp(f.type,type) && size==f.size){*out=f.offset;return true;}
  return false;
}
bool UnrealReadStructTypedArray(const UnrealStructValueView& v,const wchar_t* field,const wchar_t*,uint32_t,UnrealArrayView* out,uint32_t){
  for(const auto& f:kIdentityFields)if(!std::wcscmp(f.name,field)){std::memcpy(out,static_cast<const uint8_t*>(v.data)+f.offset,12);return true;}
  return false;
}
}
int main(int argc,char** argv){
  using namespace native_character_identity_code;
  const size_t image_size=0x2400000;
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,image_size,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(image);
  const auto base=reinterpret_cast<uintptr_t>(image);
  const auto install=[&](){Body(kSetterVa,Setter(base));Body(kCopyVa,Copy(base));Body(kBaseCopyVa,BaseCopy(base));Body(kRefreshVa,Refresh(base));
    Body(native_reference_code::kBarrierVa,native_reference_code::Relocate(native_reference_code::kBarrier,base,{15,38}));};
  install();
  Jump(0x10901150,reinterpret_cast<void*>(&IdentityTestString));Jump(0x1097B580,reinterpret_cast<void*>(&IdentityTestClassArray));
  Jump(0x1097B0E0,reinterpret_cast<void*>(&IdentityTestByteArray));Jump(0x110031A0,reinterpret_cast<void*>(&IdentityTestClass));
  Jump(0x10901430,reinterpret_cast<void*>(&IdentityTestCast));Jump(0x117A78C0,reinterpret_cast<void*>(&IdentityTestRefresh));
  if(argc>1){std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> exe{std::istreambuf_iterator<char>(f),{}};assert(exe.size()==37667328);
    const auto check=[&](uintptr_t va,const auto& bytes,const auto& relocated){
      assert(!std::memcmp(exe.data()+va-0x10900c00,bytes,sizeof(bytes)));
      std::vector<uint8_t> expected(std::begin(bytes),std::end(bytes));
      for(size_t p=0x2148A00;p<0x2148A00+0x243ABC;){uint32_t page=Read<uint32_t>(exe.data(),p),length=Read<uint32_t>(exe.data(),p+4);assert(length>=8);
        for(size_t n=p+8;n<p+length;n+=2){uint16_t entry=Read<uint16_t>(exe.data(),n);const uintptr_t operand=native_reference_code::kImageBase+page+(entry&4095);
          if((entry>>12)==3 && operand>=va && operand+4<=va+sizeof(bytes)){
            const auto o=operand-va;Put(expected.data(),o,Read<uint32_t>(expected.data(),o)+static_cast<uint32_t>(base-native_reference_code::kImageBase));}}
        p+=length;
      }
      assert(!std::memcmp(expected.data(),relocated.data(),sizeof(bytes)));
    };
    check(kSetterVa,kSetter,Setter(base));check(kCopyVa,kCopy,Copy(base));check(kBaseCopyVa,kBaseCopy,BaseCopy(base));check(kRefreshVa,kRefresh,Refresh(base));
  }
  Reset();
  for(unsigned i=0;i<1000;++i){String(pawn.bytes+0x500,0,i%2 ? L"Optimus":L"Bumblebee");
    const auto old_pawn=pawn;IdentityValue before{};assert(IdentityValueRead(pawn.bytes,false,&before));
    assert(NativeAssignCurrentCharacter(pri.bytes,pawn.bytes)==NativePlayerServiceResult::kApplied);
    assert(!std::memcmp(old_pawn.bytes,pawn.bytes,sizeof(pawn.bytes)));
    IdentityValue now{};assert(IdentityValueRead(pri.bytes,true,&now));assert(IdentityValueSame(before,now,false));
    assert(IdentityDisjoint(before,now));assert(Read<uint32_t>(image,0x12B10474-native_reference_code::kImageBase)==1);
  }
  assert(callbacks==1000);Reset();ai=true;assert(NativeAssignCurrentCharacter(pri.bytes,pawn.bytes)==NativePlayerServiceResult::kApplied);assert(!callbacks);
  Reset();Put(controller.bytes,0x2E8,static_cast<void*>(nullptr));assert(NativeAssignCurrentCharacter(pri.bytes,pawn.bytes)==NativePlayerServiceResult::kApplied);assert(!callbacks);
  Reset();reentry=1;assert(NativeAssignCurrentCharacter(pri.bytes,pawn.bytes)==NativePlayerServiceResult::kApplied);assert(callbacks==1);
  for(unsigned d=1;d<=6;++d){Reset();dirty=d;assert(NativeAssignCurrentCharacter(pri.bytes,pawn.bytes)==NativePlayerServiceResult::kStateMismatch);assert(callbacks==1);}
  Reset();wrong_pair=true;assert(Ready());assert(NativeAssignCurrentCharacter(pri.bytes,pawn.bytes)==NativePlayerServiceResult::kRejected);
  Reset();bad_layout=true;Reject();Reset();deleting=true;Reject();Reset();writes=false;Reject();Reset();world.bytes[0x39C]=1;Reject();
  Reset();controller.bytes[0x66]=2;Reject();Reset();definition.valid=false;Reject();
  Reset();auto* a=reinterpret_cast<UnrealArrayView*>(pawn.bytes+0x500);const auto good=*a;a->capacity=2048;Reject();*a=good;
  Reset();a=reinterpret_cast<UnrealArrayView*>(pri.bytes+0x484);const auto display=*a;*a=Read<UnrealArrayView>(pawn.bytes,0x510);Reject();*a=display;
  Reset();a=reinterpret_cast<UnrealArrayView*>(pri.bytes+0x3C4);const auto name=*a;*a=Read<UnrealArrayView>(pawn.bytes,0x500);Reject();*a=name;
  Reset();HANDLE thread=CreateThread(nullptr,0,OtherThread,nullptr,0,nullptr);assert(thread);assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);CloseHandle(thread);
  const auto corrupt=[&](uintptr_t va,size_t n){for(size_t i=0;i<n;++i){Reset();image[va-native_reference_code::kImageBase+i]^=1;Reject();install();}};
  corrupt(kSetterVa,sizeof(kSetter));corrupt(kCopyVa,sizeof(kCopy));corrupt(kBaseCopyVa,sizeof(kBaseCopy));corrupt(kRefreshVa,sizeof(kRefresh));
  Reset();DWORD old=0;assert(VirtualProtect(image,image_size,PAGE_READWRITE,&old));Reject();assert(VirtualProtect(image,image_size,old,&old));
  Cleanup();assert(VirtualFree(image,0,MEM_RELEASE));std::puts("NativeCharacterIdentityTests passed (native setter ABI/copy/refresh; fixture engine services).");
}
