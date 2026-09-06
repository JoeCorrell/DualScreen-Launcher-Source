// Read-only equivalent of the supported GetService('TnBuffService') path.
// Do not confuse the pawn-wide _BuffManager with the CURRENT service. Native
// TnPawn.GetService first asks its shared container, then its current form.
namespace {
bool AbilityServiceClass(void* object,std::initializer_list<const char*> allowed);
bool AbilityServiceContainer(void* container,void** service) {
  *service=nullptr;
  if (!UnrealObjectIsA(container,L"TnServiceContainer") ||
      !AbilityServiceClass(container,{"TnServiceContainer"})) return false;
  UnrealStructArrayView rows;
  if (!UnrealReadStructArrayProperty(container,L"_ServiceDescriptions",&rows) ||
      rows.count<0 || rows.count>128) return false;
  for (int32_t i=0;i<rows.count;++i) {
    bool matches=false;
    if (!UnrealStructArrayNameEquals(rows,i,L"Type",L"TnBuffService",&matches)) return false;
    // FindServiceIndex returns FIRST match, even if that entry stores None.
    if (matches) return UnrealReadStructArrayObjectField(rows,i,L"Service",service);
  }
  return true;
}
bool AbilityServiceClass(void* object,std::initializer_list<const char*> allowed) {
  char name[80]{};
  if (!UnrealObjectClassName(object,name,sizeof(name))) return false;
  for (const auto* candidate:allowed) if (!std::strcmp(name,candidate)) return true;
  return false;
}
bool ResolveAbilityBuffService(void* player,void* pawn,void** service) {
  *service=nullptr;
  void *controlled=nullptr,*controller=nullptr,*container=nullptr,*form=nullptr,*transformation=nullptr;
  ServiceCapture c;
  if (!g_player_services.module || GetCurrentThreadId()!=g_player_services.thread ||
      !ServiceField(player,L"Pawn",0x1C8,&controlled) ||
      !ServiceActor(pawn,L"TnPawn") ||
      !AbilityServiceClass(pawn,{"TnPlayerPawn","TnAiPawn"}) ||
      !ServiceField(pawn,L"Controller",0x1D4,&controller) ||
      !(controller==player || ServiceActor(controller,L"TnAiController")) ||
      !SameServiceOwner(controller,L"Pawn",pawn) ||
      !CaptureServices(controlled,{pawn,controller},&c) || c.objects[0]!=player ||
      !SameServiceOwner(pawn,L"WorldInfo",c.objects[2]) || !SameServiceOwner(controller,L"WorldInfo",c.objects[2])) return false;
  uint8_t role=0; float blend=0;
  if (!UnrealReadByteProperty(pawn,L"Role",&role) || role!=3 ||
      !UnrealReadByteProperty(controller,L"Role",&role) || role!=3 ||
      !UnrealReadObjectProperty(pawn,L"_CurrentTransformation",&transformation) || transformation ||
      !UnrealReadFloatProperty(pawn,L"_TransformBlendTimeRemaining",&blend) || !std::isfinite(blend) || blend>0 ||
      !UnrealReadObjectProperty(pawn,L"_Services",&container)) return false;
  void* resolved=nullptr;
  if (!AbilityServiceContainer(container,&resolved)) return false;
  if (!resolved) {
    if (!UnrealReadObjectProperty(pawn,L"_CurrentForm",&form)) return false;
    if (form) {
      // Reject unknown overrides rather than silently assuming base behavior.
      const bool robot=AbilityServiceClass(form,{"TnRobotForm"});
      if (!robot && !AbilityServiceClass(form,{"TnCarForm","TnTruckForm","TnPlaneForm","TnTankForm"})) return false;
      void* registered_form=nullptr;
      if (!SameServiceOwner(form,L"_OwnerPawn",pawn) ||
          !UnrealReadObjectProperty(pawn,robot ? L"_RobotForm":L"_VehicleForm",&registered_form) || registered_form!=form ||
          !UnrealReadObjectProperty(form,L"_Services",&container) || !AbilityServiceContainer(container,&resolved)) return false;
      if (!resolved && robot && !UnrealReadObjectProperty(form,L"_BuffManager",&resolved)) return false;
      // RobotForm.OnGetService supplies _BuffManager and caches it; this
      // read-only query deliberately leaves the native cache untouched.
      // VehicleForm.OnGetService has no TnBuffService case: None is genuine.
    }
  }
  if (resolved && (!UnrealObjectIsA(resolved,L"TnBuffManager") || !UnrealObjectIndex(resolved) ||
      !SameServiceOwner(resolved,L"OwnerPawn",pawn))) return false;
  if (!ServicesCurrent(c)) return false;
  *service=resolved;
  return true;
}
} // namespace
bool NativeResolveAbilityBuffService(void* player,void* pawn,void** service) {
  if (!service) return false;
  *service=nullptr;
  return !g_player_services_busy && ResolveAbilityBuffService(player,pawn,service);
}
