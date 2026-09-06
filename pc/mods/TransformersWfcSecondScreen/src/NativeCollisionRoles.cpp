#include "NativeCollisionRoles.h"
#include "UnrealBridge.h"
#include <array>
#include <cstring>
namespace wfcds {
namespace {
DWORD g_collision_thread=0;
thread_local bool g_collision_busy=false;
constexpr std::array<const wchar_t*,21> kCollisionChannels={L"Regular",L"Nothing",L"Pawn",L"Vehicle",L"Water",
    L"GameplayPhysics",L"EffectPhysics",L"Untitled1",L"Untitled2",L"Untitled3",L"Untitled4",L"SoftBody",
    L"FracturedMeshPart",L"BlockingVolume",L"DeadPawn",L"PlayerOnlyBlockingVolume",L"Team0BlockingVolume",
    L"Team1BlockingVolume",L"Team2BlockingVolume",L"Team3BlockingVolume",L"Team4BlockingVolume"};
bool CollisionLive(void* p,const wchar_t* type) {
  bool dead=true;
  return UnrealObjectIsA(p,type) && UnrealObjectIndex(p) && UnrealReadBoolProperty(p,L"bDeleteMe",&dead) && !dead;
}
struct CollisionCapture {
  // Pawn, collision service, controller, world, robot mesh, optional vehicle mesh.
  std::array<void*,6> objects{};
  std::array<uint32_t,6> indices{};
  std::array<std::array<bool,21>,2> channels{};
  bool player=false;
};
bool CaptureCollision(void* pawn,void* service,CollisionCapture* c) {
  if (!c || !g_collision_thread || GetCurrentThreadId()!=g_collision_thread || !CollisionLive(pawn,L"TnPawn")) return false;
  *c={}; auto& o=c->objects; o[0]=pawn; o[1]=service; void* value=nullptr; char type[64]{}; uint8_t role=0,mode=255;
  if (!UnrealObjectClassName(service,type,sizeof(type)) || std::strcmp(type,"TnPawnCollisionController") ||
      !UnrealReadObjectProperty(pawn,L"_CollisionController",&value) || value!=service ||
      !UnrealReadObjectProperty(service,L"_Owner",&value) || value!=pawn ||
      !UnrealReadObjectProperty(pawn,L"Controller",&o[2]) ||
      !UnrealReadObjectProperty(o[2],L"Pawn",&value) || value!=pawn ||
      !UnrealReadObjectProperty(pawn,L"WorldInfo",&o[3]) || !CollisionLive(o[3],L"WorldInfo") ||
      !UnrealReadByteProperty(o[3],L"NetMode",&mode) || mode!=0 ||
      !UnrealReadObjectProperty(o[2],L"WorldInfo",&value) || value!=o[3] ||
      !UnrealReadByteProperty(pawn,L"Role",&role) || role!=3 ||
      !UnrealReadByteProperty(o[2],L"Role",&role) || role!=3) return false;
  c->player=CollisionLive(o[2],L"TnPlayerController");
  if (c->player ? (!UnrealReadObjectProperty(o[2],L"AcknowledgedPawn",&value) || value!=pawn):!CollisionLive(o[2],L"TnAiController")) return false;
  if (!UnrealReadObjectProperty(service,L"_RobotMeshComponent",&o[4]) || !o[4] ||
      !UnrealReadObjectProperty(service,L"_VehicleMeshComponent",&o[5]) || o[4]==o[5]) return false;
  for (size_t i=0;i<2;++i) {
    void* mesh=o[4+i]; if (!mesh) continue;
    if (!UnrealObjectIsA(mesh,L"SkeletalMeshComponent") ||
        !UnrealReadObjectProperty(mesh,L"Owner",&value) || value!=pawn ||
        !UnrealValidateCallContract(mesh,L"SetRBCollidesWithChannel",nullptr,
            {{L"Channel",L"ByteProperty",1},{L"bNewCollides",L"BoolProperty",4}})) return false;
    for (size_t n=0;n<kCollisionChannels.size();++n)
      if (!UnrealReadStructBoolProperty(mesh,L"RBCollideWithChannels",kCollisionChannels[n],&c->channels[i][n])) return false;
  }
  for (size_t i=0;i<o.size();++i) { c->indices[i]=UnrealObjectIndex(o[i]); if (o[i] && !c->indices[i]) return false; }
  return true;
}
bool CollisionContext(const CollisionCapture& original,const CollisionCapture& current,
                      const std::array<std::array<bool,21>,2>& expected) {
  return original.objects==current.objects && original.indices==current.indices &&
      original.player==current.player && current.channels==expected;
}
}
bool PrepareNativeCollisionRole(DWORD thread,void* pawn,void* service) {
  if (g_collision_busy || !thread || thread!=GetCurrentThreadId() || (g_collision_thread && g_collision_thread!=thread)) return false;
  g_collision_thread=thread; CollisionCapture c;
  return CaptureCollision(pawn,service,&c);
}
NativePlayerServiceResult NativeSetCollisionRole(void* pawn,void* service,bool player) {
  CollisionCapture before;
  if (g_collision_busy || !CaptureCollision(pawn,service,&before) || before.player!=player) return NativePlayerServiceResult::kRejected;
  g_collision_busy=true; struct ClearBusy { ~ClearBusy() { g_collision_busy=false; } } busy;
  auto expected=before.channels;
  for (size_t i=0;i<2;++i) {
    if (!before.objects[4+i]) continue;
    // Engine export 1512 takes bNewCollides, not the script wrapper's Value.
    // Run even when the stored bit already matches: this is a native physics
    // synchronization call, not a UI Boolean assignment.
    if (!UnrealCallVoidByteBool(before.objects[4+i],L"SetRBCollidesWithChannel",L"Channel",15,L"bNewCollides",player))
      return NativePlayerServiceResult::kStateMismatch;
    expected[i][15]=player; CollisionCapture after;
    // Never call the second mesh after a dirty first callback. The outer
    // journal restores the already-modified component through this same path.
    if (!CaptureCollision(pawn,service,&after) || !CollisionContext(before,after,expected)) return NativePlayerServiceResult::kStateMismatch;
  }
  return NativePlayerServiceResult::kApplied;
}
}
