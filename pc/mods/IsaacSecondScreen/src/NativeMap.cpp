#include "NativeMap.h"
#include "Log.h"
#include <windows.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace isaacds {
namespace {
bool Copy(uintptr_t address,void* out,size_t size) {
  SIZE_T copied=0;
  return address>=0x10000 && size<=65536 && address+size>=address &&
    ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),out,size,&copied) && copied==size;
}
template<class T> bool Read(uintptr_t address,T& out) { return Copy(address,&out,sizeof(out)); }
bool String(uintptr_t address,std::string& out) {
  std::array<uint32_t,6> data{};
  if(!Read(address,data) || data[4]>256 || data[5]<data[4]) return false;
  out.resize(data[4]);
  return out.empty() || Copy(data[5]>15 ? data[0] : address,out.data(),out.size());
}
struct Point { float x,y; };
struct Command { int actor,frame; std::string animation; Point position,scale; std::array<float,4> color; };
using Render=void (__fastcall *)(void*,void*,Point*,Point*,Point*);
using Refresh=void (__fastcall *)(void*,void*);
Render originalRender=nullptr;
Refresh originalRefresh=nullptr;
thread_local std::vector<Command>* capture=nullptr;
thread_local bool failed=false;
std::string lastCommands;
bool installed=false;

void __fastcall ObserveRender(void* sprite,void*,Point* position,Point* tl,Point* br) {
  if(capture && !failed) {
    const uintptr_t state=reinterpret_cast<uintptr_t>(sprite);
    uint32_t address=0,animation=0,frames=0;
    // Empty overlay states are routinely rendered by the vanilla caller;
    // AnimationState::Render treats them as no-ops, not bad map snapshots.
    uint32_t layers=0;
    if(Read(state+4,animation) && (!animation || (Read(animation+0x1c,layers) && layers==0))) {
      originalRender(sprite,nullptr,position,tl,br);return;
    }
    std::string filename;
    Command command{};
    if(capture->size()>=2048 || !Read(state,address) || !String(address,filename) ||
       !Read(state+4,animation) || !String(animation,command.animation) ||
       !Read(state+8,frames) || !Read(frames,command.frame) ||
       !Read(reinterpret_cast<uintptr_t>(position),command.position) ||
       !Read(address+0x9c,command.scale) || !Read(address+0xa8,command.color)) failed=true;
    else {
      if(command.frame<0) { originalRender(sprite,nullptr,position,tl,br);return; }
      for(char& c:filename) { if(c=='\\') c='/'; if(c>='A'&&c<='Z') c+='a'-'A'; }
      filename=filename.substr(filename.find_last_of('/')+1);
      command.actor=filename=="minimap1.anm2" ? 0 : filename=="minimap2.anm2" ? 1 : filename=="minimap_icons.anm2" ? 2 : -1;
      const auto finite=[](float n) { return std::isfinite(n) && std::abs(n)<=4096; };
      if(command.actor<0 || command.frame<0 || command.frame>127 || command.animation.empty() ||
         command.animation.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_")!=std::string::npos ||
         !finite(command.position.x) || !finite(command.position.y) ||
         !finite(command.scale.x) || !finite(command.scale.y)) failed=true;
      for(float c:command.color) if(!std::isfinite(c) || c<0 || c>1) failed=true;
      if(!failed) capture->push_back(std::move(command));
    }
  }
  originalRender(sprite,nullptr,position,tl,br);
}

void __fastcall ObserveRefresh(void* config,void*) {
  const uintptr_t base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  uint32_t game=0;
  const bool target=Read(base+0x871678,game) && game &&
      reinterpret_cast<uintptr_t>(config)==game+0x26044;
  if(!target || capture) { originalRefresh(config,nullptr); return; }
  std::vector<Command> commands; commands.reserve(512);
  capture=&commands; failed=false;
  originalRefresh(config,nullptr);
  capture=nullptr;
  if(failed) {
    lastCommands.clear();
    LogRateLimited("native-map-decode",5000,LogLevel::kWarning,"[NativeMap] draw capture unavailable; descriptor fallback retained");
    return;
  }
  std::string result;
  for(const auto& command:commands) {
    if(!result.empty()) result+=';';
    result+=std::to_string(command.actor)+","+command.animation+","+std::to_string(command.frame);
    const float values[]={command.position.x,command.position.y,command.scale.x,command.scale.y,
      command.color[0],command.color[1],command.color[2],command.color[3]};
    for(float value:values) { result+=','; result+=std::to_string(value); }
  }
  if(result.size()>60000) { lastCommands.clear(); return; }
  lastCommands=std::to_string(commands.size())+"|"+result;
  static bool logged=false;
  if(!logged && !commands.empty()) { logged=true; Log(LogLevel::kInfo,"[NativeMap] authoritative draw snapshot ready commands=%zu bytes=%zu",commands.size(),result.size()); }
}
}

std::string ReadNativeMapCommands() { return lastCommands; }

bool InstallNativeMapObserver() {
  if(installed) return true;
  const uintptr_t base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  // Every direct AnimationState::Render call in J460 MinimapConfig::Render, 0098b5e0..0098d3e7.
  // ECX is AnimationState; its first pointer references the owning ANM2.
  constexpr uintptr_t renderSites[]={0x58bb1b,0x58bb31,0x58bc2e,0x58bc43,0x58bfc9,0x58bfd6,0x58bfeb,0x58c0dc,0x58c0f2,0x58c0fc,0x58c111,0x58cd33,0x58cd8a,0x58cda3,0x58d2c8,0x58d2dc,0x58d2f5};
  constexpr uintptr_t refreshSites[]={0x33f9de,0x33f9e9,0x347a95,0x347aa0,0x58db5d,0x58db68};
  struct Patch { uintptr_t site,target,original; DWORD protection=0; };
  std::vector<Patch> patches;
  auto renderHook=&ObserveRender;auto refreshHook=&ObserveRefresh;
  uintptr_t renderTarget=0,refreshTarget=0;
  std::memcpy(&renderTarget,&renderHook,sizeof(renderTarget));
  std::memcpy(&refreshTarget,&refreshHook,sizeof(refreshTarget));
  for(auto site:renderSites) patches.push_back({base+site,renderTarget,base+0xa030,0});
  for(auto site:refreshSites) patches.push_back({base+site,refreshTarget,base+0x58b5e0,0});
  for(const auto& patch:patches) {
    std::array<uint8_t,5> bytes{};uint32_t displacement=0;
    if(!Read(patch.site,bytes) || bytes[0]!=0xe8) return false;
    std::memcpy(&displacement,bytes.data()+1,4);
    if(static_cast<uint32_t>(patch.site+5+displacement)!=patch.original) return false;
  }
  uintptr_t renderAddress=base+0xa030,refreshAddress=base+0x58b5e0;
  std::memcpy(&originalRender,&renderAddress,sizeof(renderAddress));
  std::memcpy(&originalRefresh,&refreshAddress,sizeof(refreshAddress));
  // Acquire all page protections before any mutation; failure changes no calls.
  for(size_t i=0;i<patches.size();i++) {
    if(!VirtualProtect(reinterpret_cast<void*>(patches[i].site),5,PAGE_EXECUTE_READWRITE,&patches[i].protection)) {
      for(size_t j=i;j>0;j--) { DWORD ignored=0; auto& p=patches[j-1]; VirtualProtect(reinterpret_cast<void*>(p.site),5,p.protection,&ignored); }
      return false;
    }
  }
  for(auto& patch:patches) {
    const uint32_t displacement=static_cast<uint32_t>(patch.target-patch.site-5);
    std::memcpy(reinterpret_cast<void*>(patch.site+1),&displacement,4);
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(patch.site),5);
  }
  for(size_t i=patches.size();i>0;i--) { DWORD ignored=0;auto& p=patches[i-1];VirtualProtect(reinterpret_cast<void*>(p.site),5,p.protection,&ignored); }
  installed=true;
  Log(LogLevel::kInfo,"[NativeMap] J460 minimap refresh and draw observers installed; original rendering preserved");
  return true;
}
}
