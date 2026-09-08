#pragma once
#include "State.h"
#include <windows.h>
#include <d3d9.h>
#include <cstring>

namespace ac2ds {
template<class T> class PreviewCom {
    T* value{};
public:
    PreviewCom()=default;
    PreviewCom(const PreviewCom&)=delete;
    PreviewCom& operator=(const PreviewCom&)=delete;
    ~PreviewCom(){Reset();}
    void Reset(){if(value)value->Release();value=nullptr;}
    T* Get()const{return value;}
    T** Put(){Reset();return &value;}
    T* operator->()const{return value;}
};

// Called ONLY on the owning game render thread, within an existing BeginScene.
// The draw callback may use D3D9 directly; it must NOT call the engine renderer
// (which also mutates engine-side state caches), BeginScene, EndScene or Present.
// This target does not install hooks or consume the diagnostic raw pointer graph.
class CharacterPreviewTarget {
    PreviewCom<IDirect3DSurface9> color,depth,readback;
    PreviewCom<IDirect3DQuery9> completion;
    IDirect3DDevice9* device{}; // Identity only; device lifetime belongs to caller.
    DWORD owner{};
    unsigned width{},height{};
    uint64_t generation{},submittedAt{};
    bool pending{},submitted{},disabled{};

    struct SavedState {
        IDirect3DDevice9* device;
        PreviewCom<IDirect3DStateBlock9> state;
        std::array<PreviewCom<IDirect3DSurface9>,4> targets;
        PreviewCom<IDirect3DSurface9> depth;
        D3DVIEWPORT9 viewport{};RECT scissor{};
        DWORD count{};bool captured{},restored{};
        explicit SavedState(IDirect3DDevice9* d):device(d){}
        bool Capture(){
            D3DCAPS9 caps{};
            if(FAILED(device->GetDeviceCaps(&caps)) || !caps.NumSimultaneousRTs || caps.NumSimultaneousRTs>4)return false;
            count=caps.NumSimultaneousRTs;
            if(FAILED(device->CreateStateBlock(D3DSBT_ALL,state.Put())) || FAILED(state->Capture()) ||
               FAILED(device->GetViewport(&viewport)) || FAILED(device->GetScissorRect(&scissor)))return false;
            for(DWORD i=0;i<count;++i){HRESULT hr=device->GetRenderTarget(i,targets[i].Put());
                if(FAILED(hr) && (i==0 || hr!=D3DERR_NOTFOUND))return false;}
            HRESULT hr=device->GetDepthStencilSurface(depth.Put());
            if(FAILED(hr) && hr!=D3DERR_NOTFOUND)return false;
            captured=true;return true;
        }
        bool Restore(){
            if(!captured || restored)return restored;
            bool ok=true;
            auto check=[&](HRESULT hr){if(FAILED(hr))ok=false;};
            check(device->SetDepthStencilSurface(nullptr));
            for(DWORD i=1;i<count;++i)check(device->SetRenderTarget(i,nullptr));
            check(device->SetRenderTarget(0,targets[0].Get()));
            for(DWORD i=1;i<count;++i)check(device->SetRenderTarget(i,targets[i].Get()));
            check(device->SetDepthStencilSurface(depth.Get()));
            check(state->Apply());
            check(device->SetViewport(&viewport));check(device->SetScissorRect(&scissor));
            restored=true;return ok;
        }
        ~SavedState(){if(captured && !restored)Restore();}
    };
    bool OnThread()const{return owner && owner==GetCurrentThreadId();}
public:
    // Before device Reset/destruction, invalidate on the same render thread.
    bool Invalidate(){
        if(owner && !OnThread())return false;
        completion.Reset();readback.Reset();depth.Reset();color.Reset();
        device=nullptr;generation=0;pending=submitted=disabled=false;width=height=0;
        return true;
    }
    bool Initialize(IDirect3DDevice9* d,unsigned w=278,unsigned h=351){
        if(!d || !w || !h || w>1024 || h>1024 || (owner && !OnThread()))return false;
        owner=GetCurrentThreadId();Invalidate();
        if(FAILED(d->CreateRenderTarget(w,h,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,color.Put(),nullptr)) ||
           FAILED(d->CreateOffscreenPlainSurface(w,h,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,readback.Put(),nullptr)) ||
           FAILED(d->CreateQuery(D3DQUERYTYPE_EVENT,completion.Put()))){Invalidate();return false;}
        HRESULT hr=d->CreateDepthStencilSurface(w,h,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,depth.Put(),nullptr);
        if(FAILED(hr))hr=d->CreateDepthStencilSurface(w,h,D3DFMT_D16,D3DMULTISAMPLE_NONE,0,TRUE,depth.Put(),nullptr);
        if(FAILED(hr)){Invalidate();return false;}
        device=d;width=w;height=h;return true;
    }
    template<class Draw> bool Submit(uint64_t worldGeneration,uint64_t now,bool equipmentVisible,Draw&& draw){
        if(!OnThread() || !device || disabled || !equipmentVisible || !worldGeneration || pending ||
           (submitted && now>=submittedAt && now-submittedAt<67))return false;
        SavedState saved(device);if(!saved.Capture())return false;
        bool ok=SUCCEEDED(device->SetDepthStencilSurface(nullptr));
        for(DWORD i=1;i<saved.count;++i)if(FAILED(device->SetRenderTarget(i,nullptr)))ok=false;
        D3DVIEWPORT9 viewport{0,0,width,height,0,1};RECT clip{0,0,LONG(width),LONG(height)};
        if(FAILED(device->SetRenderTarget(0,color.Get())) || FAILED(device->SetDepthStencilSurface(depth.Get())) ||
           FAILED(device->SetViewport(&viewport)) || FAILED(device->SetScissorRect(&clip)) ||
           FAILED(device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE)))ok=false;
        if(ok)ok=SUCCEEDED(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xfff7f8f6,1,0));
        try{if(ok)ok=bool(draw(device));}catch(...){ok=false;}
        if(ok)ok=SUCCEEDED(completion->Issue(D3DISSUE_END));
        if(!saved.Restore()){disabled=true;return false;}
        if(!ok)return false;
        generation=worldGeneration;submittedAt=now;submitted=pending=true;return true;
    }
    // Nonblocking fence poll, no D3DGETDATA_FLUSH. The bounded readback itself
    // still needs device profiling; no claim of zero driver stalls is made.
    std::shared_ptr<const Image> Poll(uint64_t worldGeneration,bool equipmentVisible){
        if(!OnThread() || !device || disabled || !pending)return {};
        if(!equipmentVisible || worldGeneration!=generation){pending=false;return {};}
        HRESULT hr=completion->GetData(nullptr,0,0);
        if(hr==S_FALSE)return {};
        pending=false;if(FAILED(hr))return {};
        if(FAILED(device->GetRenderTargetData(color.Get(),readback.Get())))return {};
        D3DLOCKED_RECT lock{};
        if(FAILED(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY)))return {};
        struct Unlock {IDirect3DSurface9* surface;~Unlock(){surface->UnlockRect();}} unlock{readback.Get()};
        if(!lock.pBits || lock.Pitch<LONG(width*4))return {};
        auto image=std::make_shared<Image>();image->width=int(width);image->height=int(height);
        image->bgra.resize(size_t(width)*height);
        for(unsigned y=0;y<height;++y)std::memcpy(image->bgra.data()+size_t(y)*width,
            static_cast<const uint8_t*>(lock.pBits)+size_t(y)*lock.Pitch,size_t(width)*4);
        return image;
    }
};
}
