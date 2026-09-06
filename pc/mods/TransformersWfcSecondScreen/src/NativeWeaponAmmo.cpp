#include "NativeWeaponAmmo.h"
#include <limits>
namespace wfcds {
namespace {
thread_local bool g_weapon_ammo_busy=false;
bool CaptureAmmo(DWORD thread,void* pawn,void* weapon,void* target,NativeWeaponAmmo* out) {
  *out={};auto& s=*out;UnrealStructValueView character{};
  if (!NativeCaptureWeaponProgress(thread,pawn,weapon,&s.context) ||
      !UnrealObjectIsA(target,L"TnWeaponData") || !(s.target_index=UnrealObjectIndex(target)) ||
      !UnrealReadObjectProperty(weapon,L"IncreaseAmmoSkill",&s.skill) ||
      (s.skill && (!UnrealObjectIsA(s.skill,L"Class") || !(s.skill_index=UnrealObjectIndex(s.skill)))) ||
      !UnrealReadBoolProperty(weapon,L"IsEffectedByIncreaseAmmoSkill",&s.affected) ||
      !UnrealReadIntProperty(weapon,L"AmmoIncrease",&s.increase) || s.increase<0 ||
      !UnrealReadIntProperty(weapon,L"AmmoCount",&s.ammo) || s.ammo<0 ||
      !UnrealReadIntProperty(weapon,L"AmmoClipCount",&s.clip) || s.clip<0 ||
      !UnrealReadIntProperty(target,L"MaxAmmoCount",&s.target_maximum) || s.target_maximum<0 ||
      !UnrealReadIntProperty(target,L"MaxAmmoClipCount",&s.target_clip) || s.target_clip<0 ||
      !UnrealReadStructProperty(pawn,L"_CurrentCharacter",&character) ||
      !UnrealReadStructTypedArray(character,L"Skills",L"ClassProperty",4,&s.skills,64) ||
      s.skills.count<0 || s.skills.count>64 || s.skills.capacity<s.skills.count || s.skills.capacity>64) return false;
  bool has_skill=false;
  for (int32_t i=0;i<s.skills.count;++i) {
    void*& item=s.skill_objects[i];
    if (!UnrealReadObjectArrayElement(s.skills,i,&item) ||
        (item && (!UnrealObjectIsA(item,L"Class") || !(s.skill_indices[i]=UnrealObjectIndex(item))))) return false;
    // TnPawn.HasSkill uses Find, not an instance/class-inheritance test.
    if (item==s.skill) has_skill=true;
  }
  s.target=target;s.desired_increase=s.increase;
  // Holstered weapons skip InitializeFromInstigator in stock UpdateData.
  // The false branch intentionally does NOT zero an existing bonus.
  if (s.context.objects[6] && s.affected && has_skill) {
    static constexpr const wchar_t* paths[]={L"TransGame.TnSkillIncreaseAmmo",
      L"TransGame.TnSkillExtraGrenade",L"TransGame.TnSkillIncreaseAmmoFromClips"};
    bool known=false;
    for (size_t i=0;i<3;++i) {
      void* type=nullptr;
      if (!UnrealResolveClassByPath(paths[i],&type) || type!=s.skill) continue;
      if (i==2 && s.target_clip>std::numeric_limits<int32_t>::max()/2) return false;
      s.desired_increase=i==2 ? s.target_clip*2:static_cast<int32_t>(i);known=true;break;
    }
    if (!known) return false; // No guesses for another skill's static override.
  }
  if (s.desired_increase>std::numeric_limits<int32_t>::max()-s.target_maximum) return false;
  s.captured=true;return true;
}
bool AmmoActors(const NativeWeaponAmmo& a,const NativeWeaponAmmo& b) {
  for (size_t i=0;i<4;++i)
    if (a.context.objects[i]!=b.context.objects[i] || a.context.indices[i]!=b.context.indices[i]) return false;
  return a.context.objects[6]==b.context.objects[6] && a.context.indices[6]==b.context.indices[6];
}
bool AmmoConfiguration(const NativeWeaponAmmo& a,const NativeWeaponAmmo& b) {
  return SameWeaponAmmoConfiguration(a,b);
}
}
bool NativeCaptureWeaponAmmo(DWORD thread,void* pawn,void* weapon,void* target,NativeWeaponAmmo* out) {
  if (!out) return false;
  *out={};return !g_weapon_ammo_busy && CaptureAmmo(thread,pawn,weapon,target,out);
}
NativePlayerServiceResult NativeRetargetWeaponAmmo(const NativeWeaponAmmo& s,bool rollback) {
  NativeWeaponAmmo before{};
  if (g_weapon_ammo_busy || !s.captured || !s.context.captured || s.increase<0 || s.desired_increase<0 ||
      !CaptureAmmo(s.context.thread,s.context.objects[0],s.context.objects[1],s.target,&before) ||
      !AmmoActors(s,before) || !AmmoConfiguration(s,before) ||
      UnrealObjectIndex(s.context.objects[4])!=s.context.indices[4] ||
      before.context.objects[4]!=(rollback ? s.context.objects[4]:s.target) ||
      before.ammo!=s.ammo || before.clip!=s.clip ||
      (before.increase!=s.increase && before.increase!=s.desired_increase)) return NativePlayerServiceResult::kRejected;
  const auto desired=rollback ? s.increase:s.desired_increase;
  // If Instigator or character skills changed, don't publish a stale bonus.
  if (!rollback && before.desired_increase!=s.desired_increase) return NativePlayerServiceResult::kRejected;
  g_weapon_ammo_busy=true;struct Clear { ~Clear(){g_weapon_ammo_busy=false;} } clear;
  if (!UnrealWriteIntPropertyExpected(s.context.objects[1],L"AmmoIncrease",before.increase,desired))
    return NativePlayerServiceResult::kRejected;
  NativeWeaponAmmo after{};
  if (!CaptureAmmo(s.context.thread,s.context.objects[0],s.context.objects[1],s.target,&after) ||
      !AmmoConfiguration(before,after) || before.context.objects!=after.context.objects || before.context.indices!=after.context.indices ||
      before.context.interval!=after.context.interval || before.context.spread!=after.context.spread ||
      after.increase!=desired || after.ammo!=s.ammo || after.clip!=s.clip) return NativePlayerServiceResult::kStateMismatch;
  return NativePlayerServiceResult::kApplied;
}
}
