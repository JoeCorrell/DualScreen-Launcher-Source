namespace {
void ViewReset(Object* old_target=nullptr) {
  Reset(); view_testing=true; view_dirty=0; g_view_target_ready=true;
  Link(player,0x2D0,&view_local); Link(player,0x1CC,&view_pri); Link(view_pri,0x2C,&player);
  Link(camera,0x1C8,&player); Link(buddy,0x3C0,&view_pri);
  Link(player,0x30C,old_target ? old_target:&player); Link(camera,0x31C,old_target ? old_target:&player);
  Link(camera,0x344,&view_pri); // Engine clears the old PRI for the owned target.
  Put(camera.bytes,0x1D0,0x42B40000); Put(camera.bytes,0x1E0,0x3FE38E39);
  Put(At(0x12B11274),0,reinterpret_cast<uintptr_t>(view_local_class.bytes));
  Put(At(0x12B08260),0,0); Put(context,0x2C,1);
  for(auto item:{std::pair<Object*,bool>{&player,true},{&pawn,false},{&buddy,false}}) {
    auto* o=item.first; const bool pc=item.second;
    const auto table=pc ? 0x129C4000u:0x129C4400u;
    Put(o->bytes,0,reinterpret_cast<uintptr_t>(At(table)));
    Put(o->bytes,0x10,reinterpret_cast<uintptr_t>(At(0x129C4800)));
    Put(o->bytes,8,0x200); // Normal non-template gameplay object.
    Put(At(table),0x48,reinterpret_cast<uintptr_t>(At(view_code::kPendingKillVa)));
    Put(At(table),0x4C,reinterpret_cast<uintptr_t>(At(view_code::kCanExecuteVa)));
    Put(At(table),0x2DC,reinterpret_cast<uintptr_t>(At(pc ? 0x10903000:0x10903010)));
    Put(At(table),0x2E4,reinterpret_cast<uintptr_t>(At(pc ? 0x10903010:0x10903000)));
    Put(At(table),0x354,reinterpret_cast<uintptr_t>(At(view_code::kLocalVa)));
  }
  Put(At(0x129C4800),0x118,reinterpret_cast<uintptr_t>(At(0x10903020)));
}
NativeCameraResult SetView() {
  return NativeSetPlayerViewTarget(player.bytes,current_pawn->bytes,current_pawn==&pawn ? buddy.bytes:pawn.bytes,move_lease,look_lease);
}
void ViewRejected() {
  const auto pc=player,c=camera,p=pawn,b=buddy; const auto calls=view_calls;
  assert(SetView()==NativeCameraResult::kRejected && calls==view_calls);
  assert(!std::memcmp(player.bytes,pc.bytes,sizeof(pc.bytes)) && !std::memcmp(camera.bytes,c.bytes,sizeof(c.bytes)) &&
      !std::memcmp(pawn.bytes,p.bytes,sizeof(p.bytes)) && !std::memcmp(buddy.bytes,b.bytes,sizeof(b.bytes)));
}
DWORD WINAPI ViewWrongThread(void*) { ViewRejected(); return 0; }
void TestNativeViewTarget(const std::vector<uint8_t>& exe) {
  view_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    if(!exe.empty()) assert(!std::memcmp(exe.data()+va-0x10900C00,code,sizeof(code)));
    const auto bytes=native_reference_code::Relocate(code,reinterpret_cast<uintptr_t>(image),operands);
    std::memcpy(At(va),bytes.data(),bytes.size()); return true;
  });
  Put(At(0x1262720C),0,0); Put(At(0x126269A4),0,0x40000000);
  assert(BindViewTargetImage());
  view_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>) {
    for(size_t i=0;i<sizeof(code);++i) { At(va)[i]^=1; assert(!BindViewTargetImage()); At(va)[i]^=1; }
    return true;
  });
  for(const auto va:{0x1262720Cu,0x126269A4u}) {
    if(!exe.empty()) assert(Read(exe.data(),va-0x10901200)==Read(At(va),0));
    At(va)[0]^=1; assert(!BindViewTargetImage()); At(va)[0]^=1;
  }
  const uint8_t self[]={0x8B,0xC1,0xC3},zero[]={0x33,0xC0,0xC3},no_event[]={0xC2,4,0};
  std::memcpy(At(0x10903000),self,3); std::memcpy(At(0x10903010),zero,3); std::memcpy(At(0x10903020),no_event,3);
  FlushInstructionCache(GetCurrentProcess(),image,0x2300000);
  Object* old_targets[]={&player,&pawn,&buddy};
  Object* forms[]={&car,&truck,&plane,&tank};
  for(unsigned i=0;i<3000;++i) {
    ViewReset(old_targets[i%3]); Select(buddy,*forms[i%4],(i/4)%2);
    const auto pc=player,p=pawn,b=buddy,c=camera,m=manager;
    const auto calls=view_calls;
    assert(SetView()==NativeCameraResult::kApplied);
    assert(view_calls==calls+(i%3==2 ? 0:1)); // Same target must not reactivate.
    assert(Read(player.bytes,0x30C)==reinterpret_cast<uintptr_t>(buddy.bytes));
    assert(Read(camera.bytes,0x31C)==reinterpret_cast<uintptr_t>(buddy.bytes));
    assert(Read(player.bytes,0x310)==0 && Read(camera.bytes,0x344)==0 && Read(camera.bytes,0x348)==0);
    assert(!std::memcmp(pawn.bytes,p.bytes,sizeof(p.bytes)) && !std::memcmp(buddy.bytes,b.bytes,sizeof(b.bytes)) && !std::memcmp(manager.bytes,m.bytes,sizeof(m.bytes)));
    for(size_t j=0;j<sizeof(pc.bytes);++j) if(j<0x30C || j>=0x314) assert(player.bytes[j]==pc.bytes[j]);
    for(size_t j=0;j<sizeof(c.bytes);++j) if(j<0x31C || j>=0x34C) assert(camera.bytes[j]==c.bytes[j]);
    const auto same=camera; assert(SetView()==NativeCameraResult::kApplied && view_calls==calls+(i%3==2 ? 0:1));
    assert(!std::memcmp(camera.bytes,same.bytes,sizeof(same.bytes)));
    // Return direction uses the exact same actors/camera, not new fixtures.
    Select(pawn,*forms[(i+1)%4],(i/8)%2); Link(pawn,0x3C0,&view_pri); Link(buddy,0x3C0,nullptr);
    assert(SetView()==NativeCameraResult::kApplied);
    assert(Read(camera.bytes,0x31C)==reinterpret_cast<uintptr_t>(pawn.bytes));
  }
  ViewReset(); assert(!PrepareNativeViewTarget(player.bytes,buddy.bytes,pawn.bytes)); // Synthetic image isn't process EXE.
  ViewReset(); look_lease.held=false; ViewRejected();
  ViewReset(); g_camera_handoff_busy=true; ViewRejected(); g_camera_handoff_busy=false;
  ViewReset(); bad_offset=true; ViewRejected();
  ViewReset(); world.bytes[0x39C]=2; ViewRejected();
  ViewReset(); Link(camera,0x348,&pawn); ViewRejected(); // Never erase an existing blend.
  ViewReset(); Link(player,0x30C,&foreign); ViewRejected();
  ViewReset(); Link(camera,0x31C,&foreign); ViewRejected();
  ViewReset(); Link(camera,0x1C8,nullptr); ViewRejected();
  ViewReset(); Link(view_pri,0x2C,&foreign); ViewRejected();
  ViewReset(); Link(buddy,0x3C0,&foreign); ViewRejected();
  ViewReset(); Link(camera,0x344,&foreign); ViewRejected();
  ViewReset(); Link(player,0x2D0,&foreign); ViewRejected();
  ViewReset(); Put(At(0x12B11274),0,0); ViewRejected();
  ViewReset(); Put(buddy.bytes,0x78,0x80); ViewRejected();
  ViewReset(); Put(buddy.bytes,0xC,0x20000000); ViewRejected();
  ViewReset(); Put(At(0x129C4000),0x354,reinterpret_cast<uintptr_t>(At(0x10903000))); ViewRejected();
  ViewReset(); At(0x10903020)[0]=0x90; ViewRejected(); At(0x10903020)[0]=0xC2;
  for(auto* o:{&player,&pawn,&buddy,&camera,&view_local,&view_pri,&view_local_class}) { ViewReset(); o->valid=false; ViewRejected(); }
  for(unsigned failure=1;failure<=8;++failure) {
    ViewReset(); view_dirty=failure;
    assert(SetView()==NativeCameraResult::kStateMismatch && !g_camera_handoff_busy);
  }
  ViewReset(); HANDLE thread=CreateThread(nullptr,0,ViewWrongThread,nullptr,0,nullptr); assert(thread);
  assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0); CloseHandle(thread);
  std::puts("Native view target: 6000 shipping transfers plus idempotence, actual camera/PRI/default/virtual chain, ABI, guards and dirty failures passed (metadata/class lookup fixtures, not gameplay)");
  view_testing=false;
}
}
