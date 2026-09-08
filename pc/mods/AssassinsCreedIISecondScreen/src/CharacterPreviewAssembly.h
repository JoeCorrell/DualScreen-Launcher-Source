#pragma once
#include "CharacterDrawPacket.h"
#include "CharacterFramePlan.h"
namespace ac2ds {
// Render-thread-only assembly. The native hook must supply the exact frame,
// pass, instance/section set and identity-world camera; this class cannot infer
// any of those from D3D buffer identities shared with unrelated characters.
class CharacterPreviewAssembly {
    DWORD owner{};CharacterFramePlan plan;
    std::vector<std::unique_ptr<CharacterDrawPacket>> packets;
    preview::Constants passCamera{};
    bool OnThread()const{return owner && owner==GetCurrentThreadId();}
public:
    bool Invalidate(){
        if(owner && !OnThread())return false;
        plan.Reset();for(auto& packet:packets)packet->Invalidate();packets.clear();return true;
    }
    bool Begin(CharacterFrameTag tag,const std::vector<uint64_t>& expected,const preview::Constants& camera){
        if(owner && !OnThread())return false;
        owner=GetCurrentThreadId();Invalidate();
        if(!preview::Inverse(preview::FromConstants(camera)) || !plan.Begin(tag,expected))return false;
        passCamera=camera;
        for(size_t i=0;i<expected.size();++i)packets.push_back(std::make_unique<CharacterDrawPacket>());
        return true;
    }
    template<class Capture> bool Record(CharacterFrameTag tag,uint64_t key,Capture&& capture){
        if(!OnThread())return false;
        auto slot=plan.Slot(tag,key);if(!slot)return false;
        bool ok=false;try{ok=capture(*packets[*slot]);}catch(...){ok=false;}
        if(!plan.Accept(tag,key,ok)){Invalidate();return false;}
        return true;
    }
    bool Complete(CharacterFrameTag tag)const{return OnThread() && plan.Complete(tag);}
    // Only inside CharacterPreviewTarget::Submit, which saves and restores
    // actual device state around all parts. Failed/partial assemblies publish no image.
    bool Draw(IDirect3DDevice9* device,CharacterFrameTag tag,const preview::Matrix& camera){
        if(!Complete(tag))return false;
        for(auto& packet:packets)if(!packet->DrawNative(device,tag.generation,passCamera,camera))return false;
        return true;
    }
};
}
