// Read-only supported native GetCooldownTime semantics. There is deliberately
// no caller-supplied skill index, no hardcoded Cooldown[1], and no HUD estimate.
namespace {
bool AbilityValuePointer(const void* p,size_t offset,void** out) {
  if (!p || !ServiceMemory(static_cast<const uint8_t*>(p)+offset,4)) return false;
  std::memcpy(out,static_cast<const uint8_t*>(p)+offset,4); return true;
}
struct AbilitySkillCapture { void *world=nullptr,*game=nullptr,*game_class=nullptr,*defaults=nullptr,*method=nullptr; int32_t index=-1; };
bool AbilitySkill(void* world,AbilitySkillCapture* out) {
  if (!out || !g_player_services.module || GetCurrentThreadId()!=g_player_services.thread) return false;
  *out={}; out->world=world;
  const auto base=reinterpret_cast<uintptr_t>(g_player_services.module);
  if (!native_ability_skill_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    const auto relocated=native_reference_code::Relocate(code,base,operands);
    const auto* p=reinterpret_cast<void*>(base+va-native_reference_code::kImageBase);
    return ServiceMemory(p,relocated.size(),g_player_services.module,true) && !std::memcmp(p,relocated.data(),relocated.size());
  })) return false;
  void *engine=nullptr,*context=nullptr,*slot=nullptr,*actual_world=nullptr,*table=nullptr;
  if (!AbilityValuePointer(reinterpret_cast<void*>(base+0x12B10A9C-native_reference_code::kImageBase),0,&engine) ||
      !AbilityValuePointer(engine,0x40,&context) || !AbilityValuePointer(context,0x28,&slot) ||
      !AbilityValuePointer(slot,0,&actual_world) || actual_world!=world ||
      !ServiceField(world,L"WorldInfo",0x98,&actual_world) || actual_world!=world ||
      !ServiceField(world,L"Game",0x3C4,&out->game) || !ServiceActor(out->game,L"TnGame") ||
      !SameServiceOwner(out->game,L"WorldInfo",world) || !(out->game_class=UnrealObjectClass(out->game)) ||
      !UnrealObjectIsA(out->game_class,L"Class") ||
      !AbilityValuePointer(out->game_class,0x9C,&out->defaults) || !UnrealObjectIsA(out->defaults,L"TnGame") ||
      UnrealObjectClass(out->defaults)!=out->game_class || !AbilityValuePointer(out->defaults,0x10,&table) ||
      !ServiceMemory(table,0x358,g_player_services.module) ||
      !AbilityValuePointer(table,0x354,&out->method) || !ServiceMemory(out->method,6,g_player_services.module,true)) return false;
  // Verified GetCurrentSkillDataIndex tail-dispatches the game CDO slot +354.
  // Evaluate ONLY a complete, side-effect-free literal return body. Dynamic,
  // patched or unknown selectors are not silently replaced with campaign 1.
  const auto* code=static_cast<const uint8_t*>(out->method);
  if (code[0]==0xB8 && code[5]==0xC3) std::memcpy(&out->index,code+1,4);
  else if (code[0]==0x33 && code[1]==0xC0 && code[2]==0xC3) out->index=0;
  else return false;
  return out->index>=0 && out->index<32 && UnrealObjectIndex(out->defaults)!=0;
}
bool AbilityValueArray(void* object,const wchar_t* name,int32_t limit,UnrealArrayView* out) {
  return UnrealReadArrayProperty(object,name,out) && out->count>=0 && out->count<=limit &&
      out->capacity>=out->count && out->capacity<=4096 && (!out->capacity || out->data) &&
      (!out->count || ServiceMemory(out->data,static_cast<size_t>(out->count)*4));
}
bool AbilityFindBuff(void* service,void* expected_class,void** result) {
  *result=nullptr;
  if (!service) return true;
  if (!AbilityServiceClass(service,{"TnBuffManager"}) ||
      (expected_class && !UnrealObjectIsA(expected_class,L"Class"))) return false;
  UnrealArrayView buffs;
  if (!AbilityValueArray(service,L"Buffs",256,&buffs)) return false;
  for (int32_t i=0;i<buffs.count;++i) {
    void* buff=nullptr;
    if (!UnrealReadObjectArrayElement(buffs,i,&buff)) return false;
    if (!buff) continue;
    if (!UnrealObjectIsA(buff,L"TnBuff") || !UnrealObjectIndex(buff)) return false;
    // Native GetBuff tests exact class equality, NOT IsA and not expiry time.
    void* type=UnrealObjectClass(buff);
    if (!type) return false;
    if (type==expected_class) { *result=buff; return true; }
  }
  return true;
}
bool AbilityHasCooldownBuff(void* service,bool* has_buff) {
  *has_buff=false;
  if (!service) return true;
  void *expected_class=nullptr,*buff=nullptr;
  if (!UnrealResolveClassByPath(L"TransGame.TnBuffAbilityCooldown",&expected_class) ||
      !AbilityFindBuff(service,expected_class,&buff)) return false;
  *has_buff=buff!=nullptr; return true;
}
} // namespace
bool NativeReadAbilityCooldowns(void* player,void* pawn,std::array<float,3>* output) {
  if (!output) return false;
  *output={}; AbilityStateCapture before;
  if (g_player_services_busy || !CaptureAbilityState(player,pawn,&before)) return false;
  AbilitySkillCapture skill;
  void* service=nullptr; bool has_buff=false;
  if (!AbilitySkill(before.services.objects[2],&skill) || !ResolveAbilityBuffService(player,pawn,&service) ||
      !AbilityHasCooldownBuff(service,&has_buff)) return false;
  std::array<float,3> values{};
  for (uint32_t i=0;i<before.state.count;++i) {
    void* ability=before.state.abilities[i]; if (!ability) continue;
    void* owner=nullptr; UnrealArrayView cooldown,raw;
    if (!ServiceField(ability,L"OwnerPawn",0x28,&owner) || owner!=pawn ||
        !UnrealReadArrayProperty(ability,L"Cooldown",&cooldown) ||
        !ServiceMemory(static_cast<uint8_t*>(ability)+0x44,sizeof(raw))) return false;
    std::memcpy(&raw,static_cast<uint8_t*>(ability)+0x44,sizeof(raw));
    if (std::memcmp(&raw,&cooldown,sizeof(raw)) || cooldown.count<0 || cooldown.count>32) return false;
    float value=0,modifier=1;
    if (cooldown.count && !UnrealReadFloatArrayElement(cooldown,skill.index<cooldown.count ? skill.index:0,&value)) return false;
    if (has_buff) {
      uint32_t offset=0;
      if (!UnrealResolveFloatPropertyOffset(ability,L"CooldownReductionModifier",&offset) || offset!=0x50 ||
          !UnrealReadFloatProperty(ability,L"CooldownReductionModifier",&modifier)) return false;
    }
    if (!std::isfinite(value) || value<0 || !std::isfinite(modifier) || modifier<0) return false;
    values[i]=value*modifier;
    if (!std::isfinite(values[i])) return false;
  }
  AbilityStateCapture after;
  if (!CaptureAbilityState(player,pawn,&after) || !AbilityStateUnchanged(before,after)) return false;
  *output=values; return true;
}

namespace {
bool AbilityExactClass(void* object,const wchar_t* path) {
  void* type=nullptr;
  return UnrealObjectIndex(object) && UnrealResolveClassByPath(path,&type) && type &&
      UnrealObjectClass(object)==type;
}
// ResourceAmountModifier passes OptionalInfo=None. Target predicates therefore
// return false; they must NOT be tested against the owning pawn/controller.
// Melee and unknown predicates are deliberately unsupported until their service
// dispatch is verified. Never treat an unreadable condition as an absent buff.
bool AbilityResourceConditions(void* buff,void* pawn,bool* applies) {
  *applies=false;
  UnrealArrayView conditions;
  if (!AbilityValueArray(buff,L"Conditions",32,&conditions)) return false;
  for (int32_t i=0;i<conditions.count;++i) {
    void* condition=nullptr;
    if (!UnrealReadObjectArrayElement(conditions,i,&condition) ||
        !UnrealObjectIsA(condition,L"TnBuffCondition") || !UnrealObjectIndex(condition)) return false;
    if (AbilityExactClass(condition,L"TransGame.TnBuffConditionDowned")) {
      bool downed=false;
      if (!UnrealReadBoolProperty(pawn,L"_IsDowned",&downed)) return false;
      if (!downed) return true;
    } else if (AbilityExactClass(condition,L"TransGame.TnBuffConditionBehindTarget") ||
               AbilityExactClass(condition,L"TransGame.TnBuffConditionTargetIsAi") ||
               AbilityExactClass(condition,L"TransGame.TnBuffConditionTargetIsPlayer") ||
               AbilityExactClass(condition,L"TransGame.TnBuffConditionTargetIsSentry")) return true;
    else return false;
  }
  *applies=true; return true;
}
bool AbilityResourceBuffValue(void* buff,float* value) {
  // These concrete class identities use the inherited TnBuffFloatModifier
  // getter and base condition loop in the retained package. Do not assume an
  // arbitrary subclass has the same virtual methods.
  if (!AbilityExactClass(buff,L"TransGame.TnBuffFloatModifier") &&
      !AbilityExactClass(buff,L"TransGame.TnBuffDifficultyAbilityResourceModifier")) return false;
  int32_t level=-1; UnrealArrayView modifiers;
  if (!UnrealReadIntProperty(buff,L"BuffLevel",&level) || level<0 ||
      !AbilityValueArray(buff,L"FloatModifierSP",32,&modifiers)) return false;
  // GetResourceRequired runs this path only for skill index 1. Native SP values
  // fall back to FloatModifier if no SP array was configured. An out-of-range
  // positive BuffLevel uses the LAST entry, unlike ability cooldown selection.
  if (!modifiers.count && !AbilityValueArray(buff,L"FloatModifier",32,&modifiers)) return false;
  if (!modifiers.count || !UnrealReadFloatArrayElement(modifiers,
      level<modifiers.count ? level:modifiers.count-1,value)) return false;
  return std::isfinite(*value) && *value>=0;
}
bool AbilityResourceValue(void* ability,void* pawn,void* service,float* value) {
  uint32_t offset=0; void *owner=nullptr,*modifier=nullptr;
  if (!ServiceField(ability,L"OwnerPawn",0x28,&owner) || owner!=pawn ||
      !UnrealResolveFloatPropertyOffset(ability,L"ResourceRequired",&offset) || offset!=0x54 ||
      !UnrealReadFloatProperty(ability,L"ResourceRequired",value) || !std::isfinite(*value) || *value<0 ||
      !ServiceField(ability,L"ResourceAmountModifier",0x90,&modifier) ||
      !AbilityExactClass(modifier,L"TransGame.TnValueTimesProductBuffModifier")) return false;
  // Native ModifyFloatInternal returns the base value without iterating its
  // class array when there is no resolved buff service (e.g. a vehicle form).
  if (!service) return true;
  UnrealArrayView requested,raw;
  if (!AbilityValueArray(modifier,L"BuffsWhichModifyProperty",32,&requested) ||
      !ServiceMemory(static_cast<uint8_t*>(modifier)+0x28,sizeof(raw))) return false;
  std::memcpy(&raw,static_cast<uint8_t*>(modifier)+0x28,sizeof(raw));
  if (std::memcmp(&requested,&raw,sizeof(raw))) return false;
  float product=1;
  for (int32_t i=0;i<requested.count;++i) {
    void *type=nullptr,*buff=nullptr;
    if (!UnrealReadObjectArrayElement(requested,i,&type) || !AbilityFindBuff(service,type,&buff)) return false;
    if (!buff) continue;
    // Validate the ShouldBuffEffect implementation BEFORE evaluating Conditions.
    if (!AbilityExactClass(buff,L"TransGame.TnBuffFloatModifier") &&
        !AbilityExactClass(buff,L"TransGame.TnBuffDifficultyAbilityResourceModifier")) return false;
    bool applies=false;
    if (!AbilityResourceConditions(buff,pawn,&applies)) return false;
    if (!applies) continue;
    float factor=0;
    if (!AbilityResourceBuffValue(buff,&factor)) return false;
    // Preserve order and duplicates from BuffsWhichModifyProperty, matching
    // AggregateFloatModifier's single-precision storage after every factor.
    const volatile float rounded=factor*product;
    product=rounded;
    if (!std::isfinite(product)) return false;
  }
  *value=product * *value;
  return std::isfinite(*value);
}
} // namespace
bool NativeReadAbilityResourceRequired(void* player,void* pawn,float* output) {
  if (!output) return false;
  *output=0; AbilityStateCapture before;
  if (g_player_services_busy || !CaptureAbilityState(player,pawn,&before)) return false;
  const auto base=reinterpret_cast<uintptr_t>(g_player_services.module);
  if (!native_ability_resource_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    const auto relocated=native_reference_code::Relocate(code,base,operands);
    const auto* p=reinterpret_cast<void*>(base+va-native_reference_code::kImageBase);
    return ServiceMemory(p,relocated.size(),g_player_services.module,true) && !std::memcmp(p,relocated.data(),relocated.size());
  })) return false;
  float value=0;
  if (before.state.count && before.state.abilities[0]) {
    AbilitySkillCapture skill;
    if (!AbilitySkill(before.services.objects[2],&skill)) return false;
    if (skill.index==1) {
      void* service=nullptr;
      if (!ResolveAbilityBuffService(player,pawn,&service) ||
          !AbilityResourceValue(before.state.abilities[0],pawn,service,&value)) return false;
    }
  }
  AbilityStateCapture after;
  if (!CaptureAbilityState(player,pawn,&after) || !AbilityStateUnchanged(before,after)) return false;
  *output=value; return true;
}
