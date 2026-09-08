namespace {
bool SameModifierTuning(const NativeHeatModifierState& a,const NativeHeatModifierState& b) {
  return a.kind==b.kind && a.tuning==b.tuning && a.event==b.event && a.timer_offset==b.timer_offset;
}
struct ModifierTemplates {
  void* data=nullptr;
  uint32_t index=0;
  float maximum=0;
  UnrealArrayView array{};
  std::array<NativeHeatModifierState,64> items{};
};
bool CaptureRetainedTemplates(void* data,ModifierTemplates* out);
bool CaptureModifierTemplates(void* weapon,void* data,ModifierTemplates* out) {
  *out={};void* selected=nullptr;
  return UnrealReadObjectProperty(weapon,L"PlayerData",&selected) && selected==data && CaptureRetainedTemplates(data,out);
}
bool CaptureRetainedTemplates(void* data,ModifierTemplates* out) {
  *out={};UnrealStructValueView properties{};
  if(!UnrealObjectIsA(data,L"TnWeaponData") || !(out->index=UnrealObjectIndex(data)) ||
      !UnrealReadStructProperty(data,L"HeatProperties",&properties) ||
      !UnrealReadStructFloatProperty(data,L"HeatProperties",L"HeatMax",&out->maximum) ||
      !std::isfinite(out->maximum) || out->maximum<0 ||
      !UnrealReadStructTypedArray(properties,L"Modifiers",L"ObjectProperty",4,&out->array,64) ||
      out->array.count<0 || out->array.count>64 || out->array.capacity<out->array.count || out->array.capacity>64 ||
      (out->array.count && !ModifierMemory(out->array.data,out->array.count*4)))return false;
  out->data=data;
  for(int32_t i=0;i<out->array.count;++i) {
    void* item=nullptr;
    if(!UnrealReadObjectArrayElement(out->array,i,&item) || !CaptureModifier(item,&out->items[i],false) || out->items[i].owner)return false;
  }
  return true;
}
bool SameModifierTemplates(const ModifierTemplates& a,const ModifierTemplates& b) {
  if (a.data!=b.data || a.index!=b.index || a.maximum!=b.maximum ||
      a.array.data!=b.array.data || a.array.count!=b.array.count || a.array.capacity!=b.array.capacity) return false;
  for (int32_t i=0;i<a.array.count;++i) {
    const auto& x=a.items[i];const auto& y=b.items[i];
    if (x.object!=y.object || x.index!=y.index || !SameModifierTuning(x,y) || x.flags!=y.flags || x.timer!=y.timer) return false;
  }
  return true;
}
bool ModifierCloneRuntimeUnchanged(const NativeWeaponModifierInitialization& j,
                                  const ModifierTemplates& templates) {
  if(!j.clones.pending)return true;
  const auto& clones=j.clones;
  UnrealArrayView live{};UnrealStructValueView properties{};
  if(clones.owner!=j.before.heat || clones.owner_index!=j.before.heat_index ||
      clones.staged.count<0 || clones.staged.count>templates.array.count ||
      clones.staged.capacity!=templates.array.count ||
      !UnrealReadStructProperty(j.before.heat,L"HeatProperties",&properties) ||
      !UnrealReadStructTypedArray(properties,L"Modifiers",L"ObjectProperty",4,&live,64) ||
      live.data!=clones.staged.data || live.count!=clones.staged.count || live.capacity!=clones.staged.capacity)
    return false;
  for(int32_t i=0;i<live.count;++i) {
    void* object=nullptr;NativeHeatModifierState current{};
    if(!UnrealReadObjectArrayElement(live,i,&object) || object!=clones.objects[i] ||
        !CaptureModifier(object,&current) || current.index!=clones.indices[i] ||
        UnrealObjectOuter(object)!=j.before.heat ||
        (current.owner && current.owner!=j.before.context.objects[0]) ||
        !SameModifierTuning(current,templates.items[i]) ||
        current.flags!=templates.items[i].flags || current.timer!=templates.items[i].timer)
      return false;
  }
  return true;
}
bool ModifierInitBoundary(const NativeWeaponModifiers& before,const ModifierTemplates& templates,bool seeded=false) {
  NativeWeaponProgress current{};void* heat=nullptr;void* owner=nullptr;float maximum=0;ModifierTemplates fresh{};
  const auto& context=before.context;
  return NativeCaptureWeaponProgress(context.thread,context.objects[0],context.objects[1],&current) &&
      current.objects==context.objects && current.indices==context.indices &&
      current.interval==context.interval && current.spread==context.spread &&
      UnrealReadObjectProperty(context.objects[1],L"WeaponHeat",&heat) && heat==before.heat &&
      UnrealObjectIndex(heat)==before.heat_index && UnrealReadObjectProperty(heat,L"OwnerPawn",&owner) && owner==context.objects[0] &&
      UnrealReadStructFloatProperty(heat,L"HeatProperties",L"HeatMax",&maximum) && maximum==(seeded ? templates.maximum:before.heat_maximum) &&
      ((before.array.count!=0 || (templates.array.count==0 && !seeded)) ||
       NativeWeaponHeatInitializationStable(context.objects[0],context.objects[1])) &&
      CaptureModifierTemplates(context.objects[1],templates.data,&fresh) && SameModifierTemplates(templates,fresh);
}
}
bool NativeWeaponModifiersMatchData(const NativeWeaponModifiers& live,void* data) {
  ModifierTemplates expected{};
  if(g_modifiers_busy || !ValidSavedModifiers(live) || live.context.thread!=GetCurrentThreadId() ||
      !CaptureRetainedTemplates(data,&expected) || live.heat_maximum!=expected.maximum || live.array.count!=expected.array.count)return false;
  for(int32_t i=0;i<live.array.count;++i)
    if(!SameModifierTuning(live.modifiers[i],expected.items[i]))return false;
  return true;
}
NativePlayerServiceResult NativeBeginWeaponModifierInitialization(DWORD thread,void* pawn,void* weapon,
    void* player_data,NativeWeaponModifierInitialization* j) {
  if (!j || j->clones.pending || j->capacity_seeded || j->initialized.captured || g_modifiers_busy)
    return NativePlayerServiceResult::kRejected;
  *j={};j->journal_address=j;ModifierTemplates templates{};
  if (!CaptureModifiers(thread,pawn,weapon,&j->before) || !CaptureModifierTemplates(weapon,player_data,&templates) ||
      !ModifierInitBoundary(j->before,templates)) return NativePlayerServiceResult::kRejected;
  const auto count=templates.array.count;
  // Never overwrite a live character's different capacity or initialize a
  // modifier-bearing service with zero usable heat capacity.
  const bool seed=j->before.heat_maximum==0 && templates.maximum>0 && j->before.array.count==0;
  if ((!seed && j->before.heat_maximum!=templates.maximum) || (count && templates.maximum==0))
    return NativePlayerServiceResult::kRejected;
  // Reject an unavailable allocator before changing even the scalar capacity.
  if (!j->before.array.count && count && !UnrealObjectCloningReady()) return NativePlayerServiceResult::kRejected;
  j->player_data=player_data;j->player_data_index=templates.index;
  const auto failed=[&](){
    // A captured candidate is not a successful Begin. In particular, the final
    // template/runtime checks below can fail after CaptureModifiers populated
    // it. Retain recovery ownership, but never permit Finish(commit=true).
    j->initialized={};
    // Restore the exact cloned array BEFORE undoing a capacity seed. A lost
    // owner/world/template leaves both journals intact for explicit recovery.
    // CurrentHeat==0 does not prove no event happened. Charging/holding and
    // loss-delay events can change only modifier flags/timers. Do not remove
    // their rooted instances or undo capacity underneath such an event.
    if (!ModifierInitBoundary(j->before,templates,j->capacity_seeded) ||
        !ModifierCloneRuntimeUnchanged(*j,templates))
      return NativePlayerServiceResult::kStateMismatch;
    if (j->clones.pending && !UnrealFinishObjectCloneArray(&j->clones,false))
      return NativePlayerServiceResult::kStateMismatch;
    if (j->capacity_seeded) {
      if (NativeSeedWeaponHeatCapacity(pawn,weapon,player_data,true)!=NativePlayerServiceResult::kApplied)
        return NativePlayerServiceResult::kStateMismatch;
      j->capacity_seeded=false;
    }
    j->initialized={};return NativePlayerServiceResult::kRejected;
  };
  if (seed || (!j->before.array.count && count)) {
    // Includes already-positive empty services: validate cold, zero-current,
    // AI ownership before cloning. No reset/observer/state dispatch is used.
    j->capacity_seeded=seed;
    const auto prepared=NativeSeedWeaponHeatCapacity(pawn,weapon,player_data,false);
    if (prepared==NativePlayerServiceResult::kRejected) { j->capacity_seeded=false;return prepared; }
    if (prepared!=NativePlayerServiceResult::kApplied) return NativePlayerServiceResult::kStateMismatch;
    if (!ModifierInitBoundary(j->before,templates,j->capacity_seeded)) return NativePlayerServiceResult::kStateMismatch;
  }
  if (j->before.array.count || !count) {
    // Already initialized: preserve every existing instance, timer and event
    // flag. Only accept a configuration compatible with the player templates.
    if (j->before.array.count!=count) return NativePlayerServiceResult::kRejected;
    for (int32_t i=0;i<count;++i)
      if (!SameModifierTuning(j->before.modifiers[i],templates.items[i]) || j->before.modifiers[i].owner!=pawn)
        return NativePlayerServiceResult::kRejected;
    if (!CaptureModifiers(thread,pawn,weapon,&j->initialized)) return failed();
    return NativePlayerServiceResult::kApplied;
  }
  if (!UnrealObjectCloningReady()) return failed();
  g_modifiers_busy=true;struct Clear { ~Clear(){g_modifiers_busy=false;} } clear;
  if (!UnrealBeginObjectCloneArray(j->before.heat,L"HeatProperties",L"Modifiers",count,&j->clones))
    return failed();
  for (int32_t i=0;i<count;++i) {
    void* clone=nullptr;
    if (!ModifierInitBoundary(j->before,templates,j->capacity_seeded) ||
        !UnrealAppendObjectClone(&j->clones,templates.items[i].object,&clone) ||
        !ModifierInitBoundary(j->before,templates,j->capacity_seeded)) return failed();
    NativeHeatModifierState copied{};
    if (!CaptureModifier(clone,&copied) || copied.owner || !SameModifierTuning(copied,templates.items[i]) ||
        copied.flags!=templates.items[i].flags || copied.timer!=templates.items[i].timer ||
        !UnrealWriteObjectPropertyExpected(clone,L"WeaponOwner",nullptr,pawn) ||
        !ModifierInitBoundary(j->before,templates,j->capacity_seeded)) return failed();
  }
  if (!CaptureModifiers(thread,pawn,weapon,&j->initialized) || j->initialized.array.count!=count ||
      !ModifierInitBoundary(j->before,templates,j->capacity_seeded)) return failed();
  for (int32_t i=0;i<count;++i) {
    const auto& item=j->initialized.modifiers[i];const auto& source=templates.items[i];
    if (item.object!=j->clones.objects[i] || item.index!=j->clones.indices[i] || item.owner!=pawn ||
        !SameModifierTuning(item,source) || item.flags!=source.flags || item.timer!=source.timer) return failed();
  }
  return NativePlayerServiceResult::kApplied;
}
NativePlayerServiceResult NativeFinishWeaponModifierInitialization(NativeWeaponModifierInitialization* j,bool commit) {
  if (!j || j->journal_address!=j || g_modifiers_busy || !j->before.captured || j->before.context.thread!=GetCurrentThreadId())
    return NativePlayerServiceResult::kRejected;
  if (!j->clones.pending && !j->capacity_seeded && !j->initialized.captured)
    return NativePlayerServiceResult::kRejected;
  void* heat=nullptr;void* owner=nullptr;
  if (!UnrealObjectValid(j->before.context.objects[0]) || !UnrealObjectValid(j->before.context.objects[1]) ||
      UnrealObjectIndex(j->before.context.objects[0])!=j->before.context.indices[0] ||
      UnrealObjectIndex(j->before.context.objects[1])!=j->before.context.indices[1] ||
      !UnrealReadObjectProperty(j->before.context.objects[1],L"WeaponHeat",&heat) || heat!=j->before.heat ||
      UnrealObjectIndex(heat)!=j->before.heat_index || !UnrealReadObjectProperty(heat,L"OwnerPawn",&owner) ||
      owner!=j->before.context.objects[0] || UnrealObjectIndex(j->player_data)!=j->player_data_index)
    return NativePlayerServiceResult::kStateMismatch;
  // Finish occurs before any control/Data transfer. Preserve that boundary on
  // both commit and rollback, rather than freeing storage after ownership loss.
  ModifierTemplates templates{};
  if (!CaptureModifierTemplates(j->before.context.objects[1],j->player_data,&templates) ||
      !ModifierInitBoundary(j->before,templates,j->capacity_seeded)) return NativePlayerServiceResult::kStateMismatch;
  if(!commit && !ModifierCloneRuntimeUnchanged(*j,templates))return NativePlayerServiceResult::kStateMismatch;
  if (commit) {
    NativeWeaponModifiers live{};
    if (!CaptureModifiers(j->before.context.thread,owner,j->before.context.objects[1],&live) ||
        // Begin/Finish form a single quiescent pulse. Identity alone would
        // accept a heat event/cooldown tick occurring during initialization.
        // Keep the journal pending rather than committing changed runtime.
        !SameWeaponModifierRuntime(j->initialized,live)) return NativePlayerServiceResult::kStateMismatch;
  }
  g_modifiers_busy=true;struct Clear { ~Clear(){g_modifiers_busy=false;} } clear;
  if (j->clones.pending && !UnrealFinishObjectCloneArray(&j->clones,commit)) return NativePlayerServiceResult::kStateMismatch;
  if (j->capacity_seeded && !commit &&
      NativeSeedWeaponHeatCapacity(owner,j->before.context.objects[1],j->player_data,true)!=NativePlayerServiceResult::kApplied)
    return NativePlayerServiceResult::kStateMismatch;
  j->capacity_seeded=false;
  // Finish consumes this transaction on both paths. Leaving initialized set
  // after commit allowed a later Finish(false) to report a rollback even
  // though clone ownership had already been committed and the capacity seed
  // was no longer reversible. A fresh Begin must capture the now-live state.
  j->initialized={};
  return NativePlayerServiceResult::kApplied;
}
