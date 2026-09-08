#pragma once
#include "Artwork.h"
#include <cstring>
#include <limits>

namespace c3 {
// Local, bounded mesh renderer. Never reads game memory or calls game rendering.
// Render at panel initialization, then cache pixels; not in the telemetry hook.
class WeaponModel {
    struct V { float x{},y{},z{}; V operator+(V b) const{return{x+b.x,y+b.y,z+b.z};}
        V operator-(V b)const{return{x-b.x,y-b.y,z-b.z};} V operator*(float f)const{return{x*f,y*f,z*f};} };
    static float Dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
    static V Cross(V a,V b){return{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
    static V Unit(V v){const float len=std::sqrt(Dot(v,v));return len>1e-8f?v*(1/len):V{0,0,1};}
    struct Vertex { V p,n; float u{},v{}; };
    struct Texture { uint32_t w{},h{}; std::vector<uint8_t> bytes;
        std::array<float,4> At(float u,float v)const {
            if(bytes.empty())return{1,1,1,1};
            u=(u-std::floor(u))*w-.5f; v=(v-std::floor(v))*h-.5f;
            const int ix=int(std::floor(u)),iy=int(std::floor(v));
            const float fx=u-ix,fy=v-iy;
            std::array<float,4> out{};
            for(int y=0;y<2;++y)for(int x=0;x<2;++x){
                const int xx=(ix+x+int(w))%int(w), yy=(iy+y+int(h))%int(h);
                const float weight=(x?fx:1-fx)*(y?fy:1-fy)/255;
                for(int c=0;c<4;++c)out[c]+=bytes[(size_t(yy)*w+xx)*4+c]*weight;
            }return out;
        }
    };
    struct Part { std::array<float,8> material{};uint32_t flags{};std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;std::array<Texture,3> textures; };
    std::vector<Part> parts_;
public:
    bool Load(const std::filesystem::path& path) {
        parts_.clear();
        std::ifstream in(path,std::ios::binary|std::ios::ate);
        if(!in)return false;
        const auto length=in.tellg();
        if(length<16 || length>32*1024*1024)return false;
        in.seekg(0);
        size_t remaining=size_t(length);
        auto read=[&](void* data,size_t size){
            if(size>remaining || !in.read(static_cast<char*>(data),std::streamsize(size)))return false;
            remaining-=size;return true;
        };
        std::array<uint32_t,4> header{};
        if(!read(header.data(),16)||header[0]!=0x4d334343||header[1]!=2||!header[2]||header[2]>32||header[3])return false;
        std::vector<Part> next;
        size_t totalVertices=0,totalIndices=0;
        for(uint32_t i=0;i<header[2];++i){
            Part part;std::array<uint32_t,2> counts{};
            if(!read(counts.data(),8)||!counts[0]||counts[0]>65536||!counts[1]||counts[1]>196608||counts[1]%3)return false;
            totalVertices+=counts[0];totalIndices+=counts[1];
            if(totalVertices>262144||totalIndices>786432)return false;
            if(!read(part.material.data(),32))return false;
            if(!read(&part.flags,4)||part.flags>1)return false;
            for(float f:part.material)if(!std::isfinite(f)||f<0||f>4)return false;
            part.vertices.resize(counts[0]);part.indices.resize(counts[1]);
            static_assert(sizeof(Vertex)==32);
            if(!read(part.vertices.data(),counts[0]*32)||!read(part.indices.data(),counts[1]*4))return false;
            for(const auto& v:part.vertices){
                const std::array<float,8> fields{v.p.x,v.p.y,v.p.z,v.n.x,v.n.y,v.n.z,v.u,v.v};
                for(float f:fields)if(!std::isfinite(f)||std::abs(f)>10000)return false;
            }
            for(auto index:part.indices)if(index>=counts[0])return false;
            for(auto& texture:part.textures){
                if(!read(&texture.w,4)||!read(&texture.h,4)||texture.w>512||texture.h>512||bool(texture.w)!=bool(texture.h))return false;
                texture.bytes.resize(size_t(texture.w)*texture.h*4);
                if(!texture.bytes.empty()&&!read(texture.bytes.data(),texture.bytes.size()))return false;
            }
            next.push_back(std::move(part));
        }
        if(remaining)return false;
        parts_=std::move(next);return true;
    }
    Artwork Render(uint32_t width,uint32_t height)const {
        Artwork output;
        if(parts_.empty()||width<32||height<32||width>1024||height>512)return output;
        output.width=width;output.height=height;output.rgba.resize(size_t(width)*height*4);
        std::vector<float> depth(size_t(width)*height,-std::numeric_limits<float>::infinity());
        const V right=Unit({.20f,0,-1}), up=Unit({.08f,1,.016f}), toward=Unit(Cross(right,up));
        auto view=[&](V p){return V{Dot(p,right),Dot(p,up),Dot(p,toward)};};
        V lo{1e10f,1e10f,1e10f},hi{-1e10f,-1e10f,-1e10f};
        for(const auto& part:parts_)for(uint32_t index:part.indices){const auto p=view(part.vertices[index].p);
            lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);}
        if(hi.x-lo.x<1e-6f||hi.y-lo.y<1e-6f)return {};
        const float scale=std::min((width-24)/(hi.x-lo.x),(height-24)/(hi.y-lo.y));
        auto screen=[&](V p){p=view(p);return V{width*.5f+(p.x-(lo.x+hi.x)*.5f)*scale,height*.5f-(p.y-(lo.y+hi.y)*.5f)*scale,p.z};};
        const V light=Unit({-.35f,.6f,.72f}),fill=Unit({.65f,.25f,.3f}),half=Unit(light+V{0,0,1});
        for(const auto& part:parts_)for(size_t ti=0;ti<part.indices.size();ti+=3){
            const auto& a=part.vertices[part.indices[ti]];const auto& b=part.vertices[part.indices[ti+1]];const auto& c=part.vertices[part.indices[ti+2]];
            const auto p=screen(a.p),q=screen(b.p),r=screen(c.p);
            const float den=(q.y-r.y)*(p.x-r.x)+(r.x-q.x)*(p.y-r.y);
            if(std::abs(den)<1e-6f)continue;
            const int x0=std::max(0,int(std::floor(std::min({p.x,q.x,r.x})))),x1=std::min(int(width)-1,int(std::ceil(std::max({p.x,q.x,r.x}))));
            const int y0=std::max(0,int(std::floor(std::min({p.y,q.y,r.y})))),y1=std::min(int(height)-1,int(std::ceil(std::max({p.y,q.y,r.y}))));
            const float du1=b.u-a.u,dv1=b.v-a.v,du2=c.u-a.u,dv2=c.v-a.v,uvdet=du1*dv2-du2*dv1;
            V tangent{},bitangent{};
            if(std::abs(uvdet)>1e-8f){tangent=Unit(((b.p-a.p)*dv2-(c.p-a.p)*dv1)*(1/uvdet));bitangent=Unit(((c.p-a.p)*du1-(b.p-a.p)*du2)*(1/uvdet));}
            for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
                const float wa=((q.y-r.y)*(x+.5f-r.x)+(r.x-q.x)*(y+.5f-r.y))/den;
                const float wb=((r.y-p.y)*(x+.5f-r.x)+(p.x-r.x)*(y+.5f-r.y))/den,wc=1-wa-wb;
                if(wa<0||wb<0||wc<0)continue;
                const float z=wa*p.z+wb*q.z+wc*r.z;const size_t pixel=size_t(y)*width+x;
                if(z<depth[pixel])continue;
                const float u=wa*a.u+wb*b.u+wc*c.u,v=wa*a.v+wb*b.v+wc*c.v;
                auto color=part.textures[0].At(u,v);const auto spec=part.textures[2].At(u,v);
                V n=Unit(a.n*wa+b.n*wb+c.n*wc);
                if(!part.textures[1].bytes.empty()&&std::abs(uvdet)>1e-8f){
                    const auto map=part.textures[1].At(u,v);const float nx=map[0]*2-1,ny=map[1]*2-1;
                    n=Unit(tangent*nx+bitangent*ny+n*std::sqrt(std::max(0.0f,1-nx*nx-ny*ny)));
                }
                n=Unit(view(n));if(n.z<0)n=n*-1;
                const float illumination=.35f+1.5f*std::max(0.0f,Dot(n,light))+.55f*std::max(0.0f,Dot(n,fill));
                const float shine=std::pow(std::max(0.0f,Dot(n,half)),16+part.material[7]*112);
                const float alpha=std::clamp(part.material[3]*((part.flags&1)?color[3]:1),0.0f,1.0f);
                if(alpha<.01f)continue;
                const float oldAlpha=output.rgba[pixel*4+3]/255.0f;
                const float outAlpha=alpha+oldAlpha*(1-alpha);
                for(int channel=0;channel<3;++channel){
                    const float reflection=.06f+.12f*std::max(0.0f,n.y)+.18f*std::pow(1-std::abs(n.z),3);
                    const float linear=std::pow(color[channel],2.2f)*part.material[channel]*illumination+spec[channel]*part.material[4+channel]*(shine+reflection);
                    const float mapped=1-std::exp(-linear*4);
                    const float value=std::pow(std::max(0.0f,mapped),1/2.2f)*255;
                    output.rgba[pixel*4+channel]=uint8_t(std::clamp((value*alpha+output.rgba[pixel*4+channel]*oldAlpha*(1-alpha))/outAlpha,0.0f,255.0f));
                }
                output.rgba[pixel*4+3]=uint8_t(255*outAlpha);depth[pixel]=z;
            }
        }
        return output;
    }
};
} // namespace c3
