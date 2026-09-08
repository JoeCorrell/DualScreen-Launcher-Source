#pragma once
#include "CharacterPreviewTarget.h"
#include "CharacterPreviewCamera.h"
namespace ac2ds {
// Owned CPU bytes copied at the verified native upload site on the render
// thread. Bytes begin at vertex zero of this draw, not at the shared ring's
// offset. A remote diagnostic pointer is never an acceptable source here.
struct CharacterVertexSnapshot { std::vector<uint8_t> bytes; UINT stride{}; };
// A render-thread-owned copy of an already-bound draw's device state. Capture
// must occur at the verified character draw site, after native material/pose
// setup. GetStreamSource/GetIndices return retained COM objects; diagnostic
// addresses are compared as identities only and are never dereferenced here.
// This is not a native hook and does not guess camera or shader registers.
class CharacterDrawPacket {
    PreviewCom<IDirect3DStateBlock9> state;
    PreviewCom<IDirect3DVertexBuffer9> vertices;
    PreviewCom<IDirect3DIndexBuffer9> indices;
    PreviewCom<IDirect3DVertexBuffer9> isolatedVertices;
    UINT isolatedStride{};
    IDirect3DDevice9* device{};DWORD owner{};
    uint64_t generation{};
    D3DPRIMITIVETYPE primitive{};
    UINT minimum{},count{},start{},primitives{};
    std::optional<preview::Constants> cameraConstants;
public:
    bool Capture(IDirect3DDevice9* d,uint64_t worldGeneration,uintptr_t expectedVertices,uintptr_t expectedIndices,
                 D3DPRIMITIVETYPE kind,UINT minVertex,UINT vertexCount,UINT startIndex,UINT primitiveCount,
                 const CharacterVertexSnapshot* snapshot=nullptr){
        if(owner && owner!=GetCurrentThreadId())return false;
        generation=0;cameraConstants.reset();isolatedVertices.Reset();isolatedStride=0;
        if(!d || !worldGeneration || !expectedVertices || !expectedIndices || !vertexCount || !primitiveCount ||
           (kind!=D3DPT_TRIANGLELIST && kind!=D3DPT_TRIANGLESTRIP))return false;
        owner=GetCurrentThreadId();
        UINT offset{},stride{};
        if(FAILED(d->GetStreamSource(0,vertices.Put(),&offset,&stride)) || !stride ||
           reinterpret_cast<uintptr_t>(vertices.Get())!=expectedVertices ||
           FAILED(d->GetIndices(indices.Put())) || reinterpret_cast<uintptr_t>(indices.Get())!=expectedIndices)return false;
        D3DVERTEXBUFFER_DESC vb{};D3DINDEXBUFFER_DESC ib{};
        if(FAILED(vertices->GetDesc(&vb)) || FAILED(indices->GetDesc(&ib)) ||
           (ib.Format!=D3DFMT_INDEX16 && ib.Format!=D3DFMT_INDEX32))return false;
        uint64_t required=kind==D3DPT_TRIANGLELIST?uint64_t(primitiveCount)*3:uint64_t(primitiveCount)+2;
        if(uint64_t(offset)+(uint64_t(minVertex)+vertexCount)*stride>vb.Size ||
           (uint64_t(startIndex)+required)*(ib.Format==D3DFMT_INDEX16?2:4)>ib.Size)return false;
        // AC II's b089e0/b08920 path wraps and overwrites a shared dynamic
        // stream. COM retention preserves its lifetime, not its contents.
        if((vb.Usage&D3DUSAGE_DYNAMIC) && !snapshot)return false;
        if(ib.Usage&D3DUSAGE_DYNAMIC)return false; // No verified index snapshot path yet.
        PreviewCom<IDirect3DVertexDeclaration9> declaration;
        D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH+1]{};UINT elementCount=MAXD3DDECLLENGTH+1;
        if(FAILED(d->GetVertexDeclaration(declaration.Put())) || !declaration.Get() ||
           FAILED(declaration->GetDeclaration(elements,&elementCount)) || !elementCount ||
           elementCount>MAXD3DDECLLENGTH+1)return false;
        std::array<bool,16> checked{};
        for(UINT i=0;i<elementCount && elements[i].Stream!=0xff;++i){
            UINT stream=elements[i].Stream;
            if(stream>=checked.size())return false;
            if(checked[stream])continue;checked[stream]=true;
            UINT frequency{};
            if(FAILED(d->GetStreamSourceFreq(stream,&frequency)) || frequency!=1)return false;
            if(!stream)continue;
            PreviewCom<IDirect3DVertexBuffer9> extra;UINT extraOffset{},extraStride{};D3DVERTEXBUFFER_DESC desc{};
            if(FAILED(d->GetStreamSource(stream,extra.Put(),&extraOffset,&extraStride)) || !extra.Get() || !extraStride ||
               FAILED(extra->GetDesc(&desc)) || (desc.Usage&D3DUSAGE_DYNAMIC) ||
               uint64_t(extraOffset)+(uint64_t(minVertex)+vertexCount)*extraStride>desc.Size)return false;
        }
        if(!checked[0])return false;
        if(snapshot){
            if(snapshot->stride!=stride || snapshot->bytes.empty() || snapshot->bytes.size()>1024*1024 ||
               snapshot->bytes.size()%stride ||
               (uint64_t(minVertex)+vertexCount)*stride>snapshot->bytes.size())return false;
            if(FAILED(d->CreateVertexBuffer(UINT(snapshot->bytes.size()),D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,
                                           isolatedVertices.Put(),nullptr)))return false;
            void* data{};
            if(FAILED(isolatedVertices->Lock(0,UINT(snapshot->bytes.size()),&data,0)))return false;
            if(!data){isolatedVertices->Unlock();return false;}
            std::memcpy(data,snapshot->bytes.data(),snapshot->bytes.size());
            if(FAILED(isolatedVertices->Unlock()))return false;
            isolatedStride=stride;
        }
        if(device!=d || !state.Get()){
            if(FAILED(d->CreateStateBlock(D3DSBT_ALL,state.Put())))return false;
            device=d;
        }
        if(FAILED(state->Capture()))return false;
        PreviewCom<IDirect3DVertexShader9> shader;
        if(FAILED(d->GetVertexShader(shader.Put())))return false;
        if(shader.Get()){
            preview::Constants values{};
            if(FAILED(d->GetVertexShaderConstantF(0,values.data(),4)))return false;
            for(float value:values)if(!std::isfinite(value))return false;
            cameraConstants=values;
        }
        generation=worldGeneration;primitive=kind;minimum=minVertex;count=vertexCount;start=startIndex;primitives=primitiveCount;
        return true;
    }
    // The camera callback is mandatory: the caller must supply a verified
    // preview camera/material policy. A captured world camera is not a preview.
    // Invoke only inside CharacterPreviewTarget::Submit's protected draw scope.
    template<class Camera> bool Draw(IDirect3DDevice9* d,uint64_t worldGeneration,Camera&& camera){
        if(owner!=GetCurrentThreadId() || !generation || generation!=worldGeneration || device!=d)return false;
        D3DVIEWPORT9 viewport{};RECT clip{};
        if(FAILED(d->GetViewport(&viewport)) || FAILED(d->GetScissorRect(&clip)) || FAILED(state->Apply()) ||
           FAILED(d->SetViewport(&viewport)) || FAILED(d->SetScissorRect(&clip)) ||
           FAILED(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE)))return false;
        if(!camera(d))return false;
        if(isolatedVertices.Get() && FAILED(d->SetStreamSource(0,isolatedVertices.Get(),0,isolatedStride)))return false;
        return SUCCEEDED(d->DrawIndexedPrimitive(primitive,0,minimum,count,start,primitives));
    }
    // Use only for the exact-build AC II shader path verified in c05e60.
    // passCamera must be captured from a skinned identity-world draw in the
    // SAME native pass/frame; never substitute a previous-frame camera.
    bool DrawNative(IDirect3DDevice9* d,uint64_t worldGeneration,const preview::Constants& passCamera,
                    const preview::Matrix& newCamera){
        if(!cameraConstants)return false;
        auto constants=preview::Reframe(*cameraConstants,passCamera,newCamera);if(!constants)return false;
        return Draw(d,worldGeneration,[&](IDirect3DDevice9* target){
            return SUCCEEDED(target->SetVertexShaderConstantF(0,constants->data(),4));
        });
    }
    std::optional<preview::Constants> CapturedCamera(uint64_t worldGeneration)const{
        if(owner!=GetCurrentThreadId() || !generation || generation!=worldGeneration)return {};
        return cameraConstants;
    }
    bool Invalidate(){
        if(owner && owner!=GetCurrentThreadId())return false;
        generation=0;cameraConstants.reset();device=nullptr;state.Reset();indices.Reset();vertices.Reset();
        isolatedVertices.Reset();isolatedStride=0;return true;
    }
};
}
