// Pure initializer semantics; metadata and GC-aware writer are fixtures.
// The shared real reference barrier is exercised by UnrealBridgeLookupTests.
#include "NativeDialogOwner.cpp"
#include <cassert>
#include <cstdio>
#include <cwchar>
using namespace wfcds;
namespace {
struct Object { alignas(16) uint8_t bytes[0x800]{}; const wchar_t* type; uint32_t index=1; bool valid=true,dead=false; };
Object pc{{},L"TnPlayerController"},ai{{},L"TnAiController"},first{{},L"TnAiPawn"},second{{},L"TnAiPawn"},
    third{{},L"TnAiPawn"},world{{},L"WorldInfo"},foreign{{},L"WorldInfo"},pd{{},L"TnDialogManagerPlayer"},ad{{},L"TnDialogManagerAi"};
Object* objects[]={&pc,&ai,&first,&second,&third,&world,&foreign,&pd,&ad};
bool refs=true,bad_offset=false; unsigned writes=0,fail_write=0,dirty=0;
void *request_controller=nullptr,*request_pawn=nullptr,*request_other=nullptr; bool request_restore=false;
Object* Lookup(void* p) { for(auto* o:objects) if(o->valid && o->bytes==p) return o; return nullptr; }
uint32_t Read(void* p,uint32_t off) { uint32_t v; std::memcpy(&v,static_cast<uint8_t*>(p)+off,4); return v; }
void Put(void* p,uint32_t off,uintptr_t value) { uint32_t v=static_cast<uint32_t>(value); std::memcpy(static_cast<uint8_t*>(p)+off,&v,4); }
void Link(Object& o,uint32_t off,Object* target) { Put(o.bytes,off,reinterpret_cast<uintptr_t>(target ? target->bytes:nullptr)); }
bool Offset(void* p,const wchar_t* name,uint32_t* out) {
  auto* o=Lookup(p); if(!o) return false;
  const auto f=[&](const wchar_t* field,uint32_t off) { if(std::wcscmp(name,field)) return false; *out=off; return true; };
  if(f(L"WorldInfo",0x98)) return true;
  if(o==&pc||o==&ai) return f(L"Pawn",0x1C8)||f(L"AcknowledgedPawn",0x300)||f(L"_DialogManager",0x500);
  if(o==&first||o==&second||o==&third) return f(L"Controller",0x1D4);
  if(o==&pd||o==&ad) return f(L"OwnerPawn",0x28)||f(o==&pd ? L"OwnerPlayerPawn":L"OwnerAiPawn",0x60);
  return false;
}
void Pair(Object& c,Object& p) { Link(c,0x1C8,&p); Link(c,0x300,&p); Link(p,0x1D4,&c); }
void Reset() {
  uint32_t id=1; for(auto* o:objects) { std::memset(o->bytes,0,sizeof(o->bytes)); o->index=id++; o->valid=true; o->dead=false; }
  first.type=second.type=third.type=L"TnAiPawn"; pd.type=L"TnDialogManagerPlayer"; ad.type=L"TnDialogManagerAi";
  for(auto* o:{&pc,&ai,&first,&second,&third}) { Link(*o,0x98,&world); o->bytes[0x66]=3; }
  Pair(pc,first); Pair(ai,second); Link(pc,0x500,&pd); Link(ai,0x500,&ad);
  Link(pd,0x28,&first); Link(pd,0x60,&first); Link(ad,0x28,&second); Link(ad,0x60,&second);
  for(auto* o:{&pd,&ad}) for(size_t i=0x70;i<sizeof(o->bytes);++i) o->bytes[i]=static_cast<uint8_t>(i); // Cached sounds/timers untouched.
  refs=true; bad_offset=false; writes=fail_write=dirty=0; g_dialog_thread=GetCurrentThreadId(); g_dialog_busy=false;
}
NativePlayerServiceResult Apply(Object& c,Object& p,Object& other,bool restore=false) {
  request_controller=c.bytes; request_pawn=p.bytes; request_other=other.bytes; request_restore=restore;
  return NativeInitializeDialogOwner(c.bytes,p.bytes,other.bytes,restore);
}
void Reject(Object& c,Object& p,Object& other,bool restore=false) {
  const auto a=pd,b=ad; const auto w=writes;
  assert(Apply(c,p,other,restore)==NativePlayerServiceResult::kRejected && writes==w);
  assert(!std::memcmp(pd.bytes,a.bytes,sizeof(a.bytes)) && !std::memcmp(ad.bytes,b.bytes,sizeof(b.bytes)));
}
DWORD WINAPI WrongThread(void*) { Reject(pc,first,second); return 0; }
}
namespace wfcds {
bool UnrealObjectIsA(void* p,const wchar_t* type) { auto* o=Lookup(p); return o && (!std::wcscmp(o->type,type) || (!std::wcscmp(type,L"TnPawn") && (o==&first||o==&second||o==&third))); }
uint32_t UnrealObjectIndex(void* p) { auto* o=Lookup(p); return o ? o->index:0; }
bool UnrealObjectClassName(void* p,char* out,size_t size) { auto* o=Lookup(p); if(!o) return false; std::snprintf(out,size,"%ls",o->type); return true; }
bool UnrealReadBoolProperty(void* p,const wchar_t* name,bool* out) { auto* o=Lookup(p); if(!o || std::wcscmp(name,L"bDeleteMe")) return false; *out=o->dead; return true; }
bool UnrealReadByteProperty(void* p,const wchar_t* name,uint8_t* out) { if(!Lookup(p)) return false; if(!std::wcscmp(name,L"Role")) *out=static_cast<uint8_t*>(p)[0x66]; else if(!std::wcscmp(name,L"NetMode")) *out=static_cast<uint8_t*>(p)[0x39C]; else return false; return true; }
bool UnrealObjectReferenceWritesReady() { return refs; }
bool UnrealResolveObjectPropertyOffset(void* p,const wchar_t* name,uint32_t* out) { if(!Offset(p,name,out)) return false; if(bad_offset) *out+=4; return true; }
bool UnrealReadObjectProperty(void* p,const wchar_t* name,void** out) { uint32_t off=0; if(!Offset(p,name,&off)) return false; *out=reinterpret_cast<void*>(Read(p,off)); return true; }
bool UnrealWriteObjectPropertyExpected(void* p,const wchar_t* name,void* old,void* value) {
  ++writes; assert(g_dialog_busy);
  assert(NativeInitializeDialogOwner(request_controller,request_pawn,request_other,request_restore)==NativePlayerServiceResult::kRejected);
  if(writes==fail_write) return false;
  uint32_t off=0; assert(Offset(p,name,&off) && Read(p,off)==reinterpret_cast<uintptr_t>(old));
  Put(p,off,reinterpret_cast<uintptr_t>(value));
  if(dirty && writes==1) {
    if(dirty==1) ++pc.index;
    if(dirty==2) ++pd.index;
    if(dirty==3) Link(pc,0x300,nullptr);
    if(dirty==4) world.bytes[0x39C]=3;
    if(dirty==5) Link(pc,0x500,&ad);
    if(dirty==6) Link(second,0x1D4,nullptr);
    if(dirty==7) ++first.index;
  }
  return true;
}
}
int main() {
  for(unsigned i=0;i<3000;++i) {
    Reset(); const auto p=first,q=second,a=pd,b=ad;
    assert(PrepareNativeDialogOwners(GetCurrentThreadId(),pc.bytes,ai.bytes,first.bytes,second.bytes) && !writes);
    Pair(pc,second); Pair(ai,first);
    assert(Apply(pc,second,first)==NativePlayerServiceResult::kApplied);
    assert(Apply(ai,first,second)==NativePlayerServiceResult::kApplied);
    assert(Read(pd.bytes,0x28)==reinterpret_cast<uintptr_t>(second.bytes) && Read(pd.bytes,0x60)==Read(pd.bytes,0x28));
    assert(Read(ad.bytes,0x28)==reinterpret_cast<uintptr_t>(first.bytes) && Read(ad.bytes,0x60)==Read(ad.bytes,0x28));
    Pair(pc,first); Pair(ai,second);
    assert(Apply(pc,first,second)==NativePlayerServiceResult::kApplied);
    assert(Apply(ai,second,first)==NativePlayerServiceResult::kApplied && writes==8);
    assert(!std::memcmp(first.bytes,p.bytes,sizeof(p.bytes)) && !std::memcmp(second.bytes,q.bytes,sizeof(q.bytes)));
    assert(!std::memcmp(pd.bytes,a.bytes,sizeof(a.bytes)) && !std::memcmp(ad.bytes,b.bytes,sizeof(b.bytes)));
  }
  // Physical-player -> AI-player role: player manager aliases support shared
  // pawn data, but the stock AI dialog must NOT own the physical player.
  Reset(); first.type=L"TnPlayerPawn"; Pair(pc,second); Pair(ai,first);
  assert(Apply(pc,second,first)==NativePlayerServiceResult::kApplied); Reject(ai,first,second);
  // Third-member swap: proxy's retained AI dialog owner is neither selected
  // actor. Restore it only via the explicit rollback path, not normal init.
  Link(ad,0x28,&third); Link(ad,0x60,&third);
  assert(PrepareNativeDialogOwners(GetCurrentThreadId(),pc.bytes,ai.bytes,second.bytes,first.bytes));
  assert(Apply(ai,third,first,true)==NativePlayerServiceResult::kApplied); Reject(ai,third,first);
  Pair(pc,first); Pair(ai,second);
  assert(Apply(ai,second,first)==NativePlayerServiceResult::kApplied);
  Pair(pc,second); Pair(ai,first);
  assert(Apply(ai,third,first,true)==NativePlayerServiceResult::kApplied);
  for(unsigned fail=1;fail<=2;++fail) {
    Reset(); Pair(pc,second); Pair(ai,first); fail_write=fail;
    assert(Apply(pc,second,first)==(fail==1 ? NativePlayerServiceResult::kRejected:NativePlayerServiceResult::kStateMismatch));
    Pair(pc,first); Pair(ai,second); fail_write=0;
    assert(Apply(pc,first,second)==NativePlayerServiceResult::kApplied); // Repairs partial base/alias pair.
    assert(Read(pd.bytes,0x28)==reinterpret_cast<uintptr_t>(first.bytes) && Read(pd.bytes,0x60)==Read(pd.bytes,0x28));
  }
  for(unsigned d=1;d<=7;++d) {
    Reset(); Pair(pc,second); Pair(ai,first); dirty=d;
    assert(Apply(pc,second,first)==NativePlayerServiceResult::kStateMismatch && writes==1 && !g_dialog_busy);
  }
  Reset(); bad_offset=true; Reject(pc,first,second);
  Reset(); refs=false; Reject(pc,first,second);
  Reset(); g_dialog_busy=true; Reject(pc,first,second); g_dialog_busy=false;
  Reset(); pc.bytes[0x66]=1; Reject(pc,first,second);
  Reset(); second.bytes[0x66]=1; Reject(pc,first,second);
  Reset(); world.bytes[0x39C]=2; Reject(pc,first,second);
  Reset(); Link(first,0x98,&foreign); Reject(pc,first,second);
  Reset(); Link(pc,0x300,&second); Reject(pc,first,second);
  Reset(); Link(pd,0x60,&foreign); Reject(pc,first,second);
  Reset(); pd.type=L"UnknownDialogOverride"; Reject(pc,first,second);
  Reset(); Reject(pc,first,second,true); Reject(ai,second,first,true);
  for(auto* o:{&pc,&first,&second,&world,&pd}) { Reset(); o->valid=false; Reject(pc,first,second); }
  Reset(); HANDLE thread=CreateThread(nullptr,0,WrongThread,nullptr,0,nullptr); assert(thread);
  assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0); CloseHandle(thread);
  std::puts("NativeDialogOwnerTests passed: 12000 owner initializations, preserved manager/pawn state, partial rollback, third-member proxy restoration, guards and dirty failures (metadata/reference writer fixtures, not gameplay)");
}
