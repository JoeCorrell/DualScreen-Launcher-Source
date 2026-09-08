// Exercises production chapter-lifetime helpers; native game calls/objects
// are fixtures. This is not a campaign-spawn or checkpoint gameplay test.
#include "CampaignClassSystem.cpp"
#include <cassert>

namespace {
struct TestObject {
  const wchar_t* type = L"Object";
  uint32_t index = 1;
  bool valid = true;
  TestObject* world = nullptr;
  TestObject* game = nullptr;
  TestObject* gri = nullptr;
  TestObject* actor = nullptr;
  int32_t map = 16;
};
TestObject engine, local, controller, lobby, campaign, frontend, world, gri, replacement;
bool mutate_index = false, replace_gri = false, fail_int = false;
void* player_element = nullptr;
void ResetFixture() {
  engine = {}; local = {}; controller = {}; lobby = {}; campaign = {};
  frontend = {}; world = {}; gri = {}; replacement = {};
  engine.type=L"GameEngine"; local.type=L"LocalPlayer";
  controller.type=L"TnPlayerController"; lobby.type=L"TnGameLobbyGameCampaign";
  campaign.type=L"TnCampaignGameBase"; frontend.type=L"TnFrontendGame";
  world.type=L"WorldInfo"; gri.type=L"TnGameReplicationInfo";
  replacement.type=L"TnGameReplicationInfo";
  controller.world=&world; lobby.world=&world; campaign.world=&world;
  frontend.world=&world; world.game=&lobby; world.gri=&gri;
  local.actor=&controller; player_element=&local;
  mutate_index=replace_gri=fail_int=false;
  wfcds::g_flow={}; wfcds::g_configuration={};
  wfcds::g_configuration_observe_at=0;
  auto& flow=wfcds::g_flow;
  flow.active=true; flow.controller=&controller; flow.controller_index=controller.index;
  flow.lobby_game=&lobby; flow.lobby_game_index=lobby.index;
  flow.game_replication_info=&gri; flow.roster_count=3;
  for(size_t i=0;i<3;++i) {
    const char* name=i==0 ? "Bumblebee":i==1 ? "Optimus":"Ratchet";
    assert(wfcds::CopyName(name,std::strlen(name),&flow.roster[i]));
    flow.squad[i]=flow.roster[i];
  }
}
void EnterCampaign() {
  wfcds::g_flow.travel_forwarded=true;
  world.game=&campaign;
}
}
namespace wfcds {
bool UnrealObjectValid(void* p) { return p && static_cast<TestObject*>(p)->valid; }
uint32_t UnrealObjectIndex(void* p) {
  return UnrealObjectValid(p) ? static_cast<TestObject*>(p)->index:0;
}
bool UnrealObjectIsA(void* p,const wchar_t* type) {
  return UnrealObjectValid(p) && !std::wcscmp(static_cast<TestObject*>(p)->type,type);
}
bool UnrealReadObjectProperty(void* p,const wchar_t* field,void** out) {
  if(!out || !UnrealObjectValid(p))return false;
  auto& o=*static_cast<TestObject*>(p);
  if(!std::wcscmp(field,L"WorldInfo"))*out=o.world;
  else if(!std::wcscmp(field,L"Game"))*out=o.game;
  else if(!std::wcscmp(field,L"GRI"))*out=o.gri;
  else if(!std::wcscmp(field,L"Actor"))*out=o.actor;
  else return false;
  return true;
}
bool UnrealReadIntProperty(void* p,const wchar_t* field,int32_t* out) {
  if(!out || !UnrealObjectValid(p) || std::wcscmp(field,L"SelectedMapID") || fail_int)return false;
  auto& o=*static_cast<TestObject*>(p);*out=o.map;
  if(mutate_index)++o.index;
  if(replace_gri)world.gri=&replacement;
  return true;
}
bool UnrealReadArrayProperty(void* p,const wchar_t* field,UnrealArrayView* out) {
  if(p!=&engine || std::wcscmp(field,L"GamePlayers") || !out)return false;
  *out={&player_element,1,1};return true;
}
bool UnrealReadObjectArrayElement(const UnrealArrayView& a,int32_t i,void** out) {
  if(!out || a.data!=&player_element || i!=0 || a.count!=1)return false;
  *out=player_element;return true;
}
void Log(LogLevel,const char*,...) {}
}
int main() {
  using namespace wfcds;
  for(unsigned i=0;i<2000;++i) {
    ResetFixture();gri.map=static_cast<int32_t>(i);
    assert(CommitConfiguration(g_flow.squad[i%3]));
    assert(g_configuration.map_id==static_cast<int32_t>(i));
    assert(g_configuration.initial_slot==i%3);
    assert(ContextBelongsToConfiguredCampaign(&controller));
    EnterCampaign();
    assert(ContextBelongsToConfiguredCampaign(&controller));
    assert(ObserveConfigurationWorld(&engine,10000));
    assert(g_configuration.active && g_configuration.campaign_world_seen);
    // A fresh world/GRI for the same chapter is allowed, never a stale pointer key.
    replacement.map=gri.map;world.gri=&replacement;
    assert(ContextBelongsToConfiguredCampaign(&controller));
    replacement.map++;
    assert(!ContextBelongsToConfiguredCampaign(&controller));
    assert(ObserveConfigurationWorld(&engine,12000));
    assert(!g_configuration.active && g_configuration.map_id==-1);
  }
  for(unsigned fault=0;fault<7;++fault) {
    ResetFixture();
    if(fault==0)gri.map=-1;
    if(fault==1)gri.valid=false;
    if(fault==2)mutate_index=true;
    if(fault==3)replace_gri=true;
    if(fault==4)fail_int=true;
    if(fault==5)world.game=&frontend;
    if(fault==6)g_flow.controller_index++;
    assert(!CommitConfiguration(g_flow.squad[0]));
    assert(!g_configuration.active);
  }
  ResetFixture();assert(CommitConfiguration(g_flow.squad[0]));EnterCampaign();
  world.gri=nullptr;
  assert(!ContextBelongsToConfiguredCampaign(&controller));
  assert(ObserveConfigurationWorld(&engine,10000));
  assert(g_configuration.active && !g_configuration.campaign_world_seen);
  world.gri=&gri;assert(ContextBelongsToConfiguredCampaign(&controller));
  world.game=&frontend;
  assert(!ContextBelongsToConfiguredCampaign(&controller));
  assert(ObserveConfigurationWorld(&engine,12000));assert(!g_configuration.active);
  std::puts("Campaign chapter binding: 2000 transitions, stale IDs, travel gaps and retirement passed");
}
