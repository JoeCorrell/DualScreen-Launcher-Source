#include "CharacterPreviewTarget.h"
#include "CharacterPreviewAssembly.h"
#include <d3dcompiler.h>
#include <cassert>
#include <cstdio>
using namespace ac2ds;
int main(){
    HWND window=CreateWindowExW(0,L"STATIC",L"Private preview target test",WS_POPUP,0,0,128,128,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);
    assert(window); // Never shown; no game process, input or engine resources.
    IDirect3D9* api=Direct3DCreate9(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS p{};p.Windowed=TRUE;p.SwapEffect=D3DSWAPEFFECT_DISCARD;
    p.hDeviceWindow=window;p.BackBufferWidth=p.BackBufferHeight=128;p.BackBufferFormat=D3DFMT_X8R8G8B8;
    p.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* device{};
    HRESULT hr=api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&device);
    if(FAILED(hr)){std::printf("D3D9 device unavailable: %08lx\n",static_cast<unsigned long>(hr));api->Release();DestroyWindow(window);return 77;}
    {
        CharacterPreviewTarget target;assert(target.Initialize(device,48,64));
        PreviewCom<IDirect3DSurface9> original;assert(SUCCEEDED(device->GetRenderTarget(0,original.Put())));
        D3DVIEWPORT9 view{12,16,80,72,.1f,.9f};RECT clip{14,18,50,60};
        assert(SUCCEEDED(device->SetViewport(&view)) && SUCCEEDED(device->SetScissorRect(&clip)));
        assert(SUCCEEDED(device->SetRenderState(D3DRS_LIGHTING,TRUE)));
        auto restored=[&]{
            PreviewCom<IDirect3DSurface9> surface;D3DVIEWPORT9 v{};RECT r{};DWORD lighting{};
            assert(SUCCEEDED(device->GetRenderTarget(0,surface.Put())) && surface.Get()==original.Get());
            assert(SUCCEEDED(device->GetViewport(&v)) && std::memcmp(&v,&view,sizeof(v))==0);
            assert(SUCCEEDED(device->GetScissorRect(&r)) && EqualRect(&r,&clip));
            assert(SUCCEEDED(device->GetRenderState(D3DRS_LIGHTING,&lighting)) && lighting==TRUE);
        };
        auto draw=[](IDirect3DDevice9* d){
            return SUCCEEDED(d->SetRenderState(D3DRS_LIGHTING,FALSE)) &&
                SUCCEEDED(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff89181f,1,0));
        };
        assert(SUCCEEDED(device->BeginScene()));
        assert(!target.Submit(1,100,false,draw));assert(target.Submit(1,100,true,draw));restored();
        assert(!target.Submit(1,200,true,draw)); // One outstanding readback only.
        assert(SUCCEEDED(device->EndScene()));assert(SUCCEEDED(device->Present(nullptr,nullptr,nullptr,nullptr)));
        std::shared_ptr<const Image> image;
        for(unsigned attempt=0;attempt<1000 && !image;++attempt){image=target.Poll(1,true);if(!image)Sleep(1);}
        assert(image && image->width==48 && image->height==64);
        for(auto pixel:image->bgra)assert(pixel==0xff89181f);
        assert(SUCCEEDED(device->BeginScene()));
        assert(!target.Submit(1,150,true,draw)); // 15Hz pacing.
        assert(!target.Submit(1,200,true,[&](IDirect3DDevice9* d){draw(d);return false;}));restored();
        assert(!target.Submit(1,200,true,[&](IDirect3DDevice9* d)->bool{draw(d);throw 1;}));restored();
        assert(target.Submit(1,200,true,draw));assert(!target.Poll(2,true)); // Old world is discarded.
        assert(SUCCEEDED(device->EndScene()));
        assert(target.Invalidate());assert(!target.Poll(2,true));
        assert(target.Initialize(device,32,32));assert(target.Invalidate());
        // Actual indexed geometry through a retained draw packet. This private
        // triangle validates D3D plumbing; it is never a character/UI asset.
        struct Vertex {float x,y,z;DWORD color;};
        Vertex points[]{{-.8f,-.8f,.5f,0xff00ff00},{0,.8f,.5f,0xff00ff00},{.8f,-.8f,.5f,0xff00ff00}};
        uint16_t order[]{0,1,2};PreviewCom<IDirect3DVertexBuffer9> vb;PreviewCom<IDirect3DIndexBuffer9> ib;
        assert(SUCCEEDED(device->CreateVertexBuffer(sizeof(points),0,D3DFVF_XYZ|D3DFVF_DIFFUSE,D3DPOOL_MANAGED,vb.Put(),nullptr)));
        assert(SUCCEEDED(device->CreateIndexBuffer(sizeof(order),0,D3DFMT_INDEX16,D3DPOOL_MANAGED,ib.Put(),nullptr)));
        void* data{};assert(SUCCEEDED(vb->Lock(0,0,&data,0)));std::memcpy(data,points,sizeof(points));assert(SUCCEEDED(vb->Unlock()));
        assert(SUCCEEDED(ib->Lock(0,0,&data,0)));std::memcpy(data,order,sizeof(order));assert(SUCCEEDED(ib->Unlock()));
        assert(SUCCEEDED(device->SetStreamSource(0,vb.Get(),0,sizeof(Vertex))) && SUCCEEDED(device->SetIndices(ib.Get())));
        assert(SUCCEEDED(device->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE)) && SUCCEEDED(device->SetVertexShader(nullptr)));
        assert(SUCCEEDED(device->SetPixelShader(nullptr)) && SUCCEEDED(device->SetRenderState(D3DRS_LIGHTING,FALSE)));
        assert(SUCCEEDED(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE)) && SUCCEEDED(device->SetRenderState(D3DRS_ZENABLE,FALSE)));
        CharacterDrawPacket packet;
        auto capture=[&](unsigned count,unsigned first){return packet.Capture(device,7,reinterpret_cast<uintptr_t>(vb.Get()),
            reinterpret_cast<uintptr_t>(ib.Get()),D3DPT_TRIANGLELIST,0,count,first,1);};
        assert(!capture(4,0));assert(!capture(3,1));assert(capture(3,0));
        constexpr char shaderSource[]=
            "float4x4 camera:register(c0);struct V{float4 p:POSITION;float4 c:COLOR0;};"
            "V main(float4 p:POSITION,float4 c:COLOR0){V o;o.p=mul(p,camera);o.c=c;return o;}";
        PreviewCom<ID3DBlob> code,errors;PreviewCom<IDirect3DVertexShader9> shader;
        assert(SUCCEEDED(D3DCompile(shaderSource,sizeof(shaderSource)-1,nullptr,nullptr,nullptr,"main","vs_2_0",0,0,code.Put(),errors.Put())));
        assert(SUCCEEDED(device->CreateVertexShader(static_cast<const DWORD*>(code->GetBufferPointer()),shader.Put())));
        preview::Constants identityConstants{};for(unsigned i=0;i<16;i+=5)identityConstants[i]=1;
        assert(SUCCEEDED(device->SetVertexShader(shader.Get())) && SUCCEEDED(device->SetVertexShaderConstantF(0,identityConstants.data(),4)));
        assert(capture(3,0) && packet.CapturedCamera(7) && !packet.CapturedCamera(8));
        assert(SUCCEEDED(device->SetStreamSource(0,nullptr,0,0)) && SUCCEEDED(device->SetIndices(nullptr)));
        vb.Reset();ib.Reset(); // The packet must retain the original resources.
        auto camera=[](IDirect3DDevice9* d){
            D3DMATRIX m{};m._11=m._22=m._33=m._44=1;
            return SUCCEEDED(d->SetTransform(D3DTS_WORLD,&m)) && SUCCEEDED(d->SetTransform(D3DTS_VIEW,&m)) &&
                SUCCEEDED(d->SetTransform(D3DTS_PROJECTION,&m));
        };
        assert(target.Initialize(device,48,64));assert(SUCCEEDED(device->BeginScene()));
        assert(!target.Submit(8,1000,true,[&](IDirect3DDevice9* d){return packet.Draw(d,8,camera);}));
        assert(target.Submit(7,1000,true,[&](IDirect3DDevice9* d){return packet.Draw(d,7,camera);}));
        assert(SUCCEEDED(device->EndScene()));assert(SUCCEEDED(device->Present(nullptr,nullptr,nullptr,nullptr)));
        image.reset();
        for(unsigned attempt=0;attempt<1000 && !image;++attempt){image=target.Poll(7,true);if(!image)Sleep(1);}
        assert(image && std::count(image->bgra.begin(),image->bgra.end(),0xff00ff00u)>100);
        auto center=[](const Image& picture){double x=0,n=0;
            for(size_t i=0;i<picture.bgra.size();++i)if(picture.bgra[i]==0xff00ff00u){x+=i%picture.width;++n;}
            assert(n>30);return x/n;
        };
        auto oldCenter=center(*image);auto newCamera=preview::FromConstants(identityConstants);newCamera[0]=.4;newCamera[12]=.6;
        assert(SUCCEEDED(device->BeginScene()));
        assert(target.Submit(7,1100,true,[&](IDirect3DDevice9* d){return packet.DrawNative(d,7,identityConstants,newCamera);}));
        assert(SUCCEEDED(device->EndScene()));assert(SUCCEEDED(device->Present(nullptr,nullptr,nullptr,nullptr)));
        image.reset();for(unsigned attempt=0;attempt<1000 && !image;++attempt){image=target.Poll(7,true);if(!image)Sleep(1);}
        assert(image && center(*image)>oldCenter+10); // Shader register camera changed, original vertex data retained.
        assert(packet.Invalidate() && target.Invalidate());
        // The alternate native path uses a reused ring in stream0 and a
        // separate static stream1. Replaying after ring overwrite must retain
        // the captured positions, not render another draw's vertices.
        PreviewCom<IDirect3DVertexBuffer9> ring,colors;
        PreviewCom<IDirect3DIndexBuffer9> dynamicPathIndices;
        PreviewCom<IDirect3DVertexDeclaration9> twoStreams;
        D3DVERTEXELEMENT9 declaration[]{
            {0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
            {1,0,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};
        assert(SUCCEEDED(device->CreateVertexDeclaration(declaration,twoStreams.Put())));
        assert(SUCCEEDED(device->CreateVertexBuffer(sizeof(Vertex)*8,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,
            D3DPOOL_DEFAULT,ring.Put(),nullptr)));
        assert(SUCCEEDED(device->CreateVertexBuffer(sizeof(DWORD)*3,0,0,D3DPOOL_MANAGED,colors.Put(),nullptr)));
        assert(SUCCEEDED(device->CreateIndexBuffer(sizeof(order),0,D3DFMT_INDEX16,D3DPOOL_MANAGED,dynamicPathIndices.Put(),nullptr)));
        assert(SUCCEEDED(ring->Lock(0,0,&data,D3DLOCK_DISCARD)));std::memset(data,0,sizeof(Vertex)*8);
        std::memcpy(static_cast<uint8_t*>(data)+sizeof(Vertex)*4,points,sizeof(points));assert(SUCCEEDED(ring->Unlock()));
        DWORD green[]{0xff00ff00,0xff00ff00,0xff00ff00};
        assert(SUCCEEDED(colors->Lock(0,0,&data,0)));std::memcpy(data,green,sizeof(green));assert(SUCCEEDED(colors->Unlock()));
        assert(SUCCEEDED(dynamicPathIndices->Lock(0,0,&data,0)));std::memcpy(data,order,sizeof(order));assert(SUCCEEDED(dynamicPathIndices->Unlock()));
        assert(SUCCEEDED(device->SetVertexDeclaration(twoStreams.Get())));
        assert(SUCCEEDED(device->SetStreamSource(0,ring.Get(),sizeof(Vertex)*4,sizeof(Vertex))));
        assert(SUCCEEDED(device->SetStreamSource(1,colors.Get(),0,sizeof(DWORD))));
        assert(SUCCEEDED(device->SetIndices(dynamicPathIndices.Get())));
        assert(SUCCEEDED(device->SetVertexShaderConstantF(0,identityConstants.data(),4)));
        CharacterVertexSnapshot snapshot;snapshot.stride=sizeof(Vertex);snapshot.bytes.resize(sizeof(points));
        std::memcpy(snapshot.bytes.data(),points,sizeof(points));
        auto dynamicCapture=[&](const CharacterVertexSnapshot* bytes){return packet.Capture(device,9,
            reinterpret_cast<uintptr_t>(ring.Get()),reinterpret_cast<uintptr_t>(dynamicPathIndices.Get()),
            D3DPT_TRIANGLELIST,0,3,0,1,bytes);};
        assert(!dynamicCapture(nullptr));
        auto truncated=snapshot;truncated.bytes.resize(sizeof(Vertex)*2);assert(!dynamicCapture(&truncated));
        auto badStride=snapshot;badStride.stride=12;assert(!dynamicCapture(&badStride));
        assert(SUCCEEDED(device->SetStreamSource(1,colors.Get(),4,sizeof(DWORD))));assert(!dynamicCapture(&snapshot));
        assert(SUCCEEDED(device->SetStreamSource(1,colors.Get(),0,sizeof(DWORD))));assert(dynamicCapture(&snapshot));
        CharacterPreviewAssembly assembly;CharacterFrameTag frame{9,30,1};
        assert(assembly.Begin(frame,{100,101},identityConstants));
        auto capturePart=[&](CharacterDrawPacket& part){return part.Capture(device,9,
            reinterpret_cast<uintptr_t>(ring.Get()),reinterpret_cast<uintptr_t>(dynamicPathIndices.Get()),
            D3DPT_TRIANGLELIST,0,3,0,1,&snapshot);};
        assert(assembly.Record(frame,100,capturePart) && !assembly.Complete(frame));
        assert(assembly.Record(frame,101,capturePart) && assembly.Complete(frame));
        assert(SUCCEEDED(ring->Lock(0,0,&data,D3DLOCK_DISCARD)));
        auto overwrite=static_cast<Vertex*>(data);for(unsigned i=0;i<8;++i)overwrite[i]={100,100,.5f,0xffff0000};
        assert(SUCCEEDED(ring->Unlock()));
        std::fill(snapshot.bytes.begin(),snapshot.bytes.end(),0); // Upload owns a separate copy.
        assert(target.Initialize(device,48,64));assert(SUCCEEDED(device->BeginScene()));
        assert(target.Submit(9,2000,true,[&](IDirect3DDevice9* d){return packet.Draw(d,9,camera);}));
        assert(SUCCEEDED(device->EndScene()));assert(SUCCEEDED(device->Present(nullptr,nullptr,nullptr,nullptr)));
        image.reset();for(unsigned attempt=0;attempt<1000 && !image;++attempt){image=target.Poll(9,true);if(!image)Sleep(1);}
        assert(image && std::count(image->bgra.begin(),image->bgra.end(),0xff00ff00u)>100);
        assert(SUCCEEDED(device->BeginScene()));
        assert(target.Submit(9,2100,true,[&](IDirect3DDevice9* d){return assembly.Draw(d,frame,preview::FromConstants(identityConstants));}));
        assert(SUCCEEDED(device->EndScene()));assert(SUCCEEDED(device->Present(nullptr,nullptr,nullptr,nullptr)));
        image.reset();for(unsigned attempt=0;attempt<1000 && !image;++attempt){image=target.Poll(9,true);if(!image)Sleep(1);}
        assert(image && std::count(image->bgra.begin(),image->bgra.end(),0xff00ff00u)>100);
        assert(assembly.Invalidate() && !assembly.Complete(frame));
        assert(packet.Invalidate() && target.Invalidate());
    }
    device->Release();api->Release();DestroyWindow(window);
    std::puts("Preview target: real indexed drawing/readback, retained buffers, state restoration, failed draws, generation discard and pacing passed");
}
