#include "../src/WeaponModel.h"
#include <cassert>
#include <chrono>
#include <iostream>

int main(int argc,char** argv){
    assert(argc==2);
    const std::filesystem::path root(argv[1]);std::filesystem::create_directories(root);
    std::vector<uint8_t> bytes;
    auto word=[&](uint32_t value){for(int i=0;i<4;++i)bytes.push_back(uint8_t(value>>(i*8)));};
    auto number=[&](float value){uint32_t raw;std::memcpy(&raw,&value,4);word(raw);};
    word(0x4d334343);word(2);word(1);word(0);word(3);word(3);
    for(float f:{.2f,.2f,.2f,1.f,.1f,.1f,.1f,.5f})number(f);
    word(0);
    for(const std::array<float,8> vertex:{std::array<float,8>{0,0,0,1,0,0,0,0},
            {0,1,0,1,0,0,0,1},{0,0,1,1,0,0,1,0}})
        for(float f:vertex)number(f);
    word(0);word(1);word(2);
    word(1);word(1);word(0xff4080ff); // genuine test-only colored texel
    word(0);word(0);word(0);word(0);
    auto save=[&](const char* name,const std::vector<uint8_t>& data){
        const auto path=root/name;std::ofstream out(path,std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));out.close();return path;
    };
    const auto good=save("TEST-ONLY-triangle.c3m",bytes);
    c3::WeaponModel model;assert(model.Load(good));
    auto art=model.Render(128,64);assert(art.width==128&&art.height==64&&art.rgba.size()==128*64*4);
    size_t visible=0;for(size_t i=3;i<art.rgba.size();i+=4)visible+=art.rgba[i]!=0;
    assert(visible>50&&visible<128*64);
    assert(model.Render(0,64).rgba.empty());assert(model.Render(2048,64).rgba.empty());
    c3::ArtworkCatalog catalog;assert(catalog.AddModel("TEST",art));assert(!catalog.AddModel("TEST",art));
    assert(catalog.Model("TEST")&&!catalog.Model("UNKNOWN"));assert(!catalog.AddModel("BAD",{}));
    for(const size_t cut:{size_t(0),size_t(15),size_t(60),bytes.size()-1}){
        auto bad=bytes;bad.resize(cut);assert(!model.Load(save("TEST-ONLY-truncated.c3m",bad)));
        assert(model.Render(128,64).rgba.empty()); // failed load clears prior geometry
    }
    for(const auto offset:{size_t(4),size_t(8),size_t(16),size_t(56),size_t(156)}){
        auto bad=bytes;for(size_t i=0;i<4;++i)bad[offset+i]=255;
        assert(!model.Load(save("TEST-ONLY-invalid.c3m",bad)));
    }
    auto bad=bytes;bad.push_back(0);assert(!model.Load(save("TEST-ONLY-trailing.c3m",bad)));
    bad=bytes;bad[60]=0;bad[61]=0;bad[62]=192;bad[63]=127;
    assert(!model.Load(save("TEST-ONLY-nan.c3m",bad)));
    assert(model.Load(good));
    std::cout<<"PASS: mesh bounds, indices, flags, versions, truncation, NaN, dimensions, cache identity, failed-load clearing\n";
}
