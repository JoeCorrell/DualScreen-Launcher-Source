#pragma once
#include "Model.h"
#include "Layout.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>

namespace c3 {
struct Artwork {
    uint32_t width{}, height{};
    std::vector<uint8_t> rgba;
    static constexpr size_t MaxBytes=16*1024*1024;
    bool Load(const std::filesystem::path& path) {
        width=height=0; rgba.clear();
        std::ifstream input(path,std::ios::binary|std::ios::ate);
        if (!input) return false;
        auto length=input.tellg();
        if (length<16 || length>std::streamoff(MaxBytes+16)) return false;
        input.seekg(0);
        std::array<uint32_t,4> header{};
        if (!input.read(reinterpret_cast<char*>(header.data()),16) || header[0]!=0x41523343 || header[1]!=1 ||
            !header[2] || !header[3] || header[2]>2048 || header[3]>2048) return false;
        size_t bytes=size_t(header[2])*header[3]*4;
        if (bytes>MaxBytes || length!=std::streamoff(bytes+16)) return false;
        rgba.resize(bytes);
        if (!input.read(reinterpret_cast<char*>(rgba.data()),std::streamsize(bytes))) { rgba.clear(); return false; }
        width=header[2]; height=header[3]; return true;
    }
    Rect Fit(Rect destination) const {
        if (!width || !height) return {};
        const float scale=std::min(destination.w/width,destination.h/height);
        const float w=width*scale,h=height*scale;
        return {destination.x+(destination.w-w)/2,destination.y+(destination.h-h)/2,w,h};
    }
};
// Explicit catalog insertions performed before rendering, not arbitrary file
// lookups from game/UDP strings. Map identity is checked by the future adapter.
class ArtworkCatalog {
    std::map<Key,Artwork> maps_;
    std::map<std::string,Artwork,std::less<>> icons_;
    std::map<std::string,Artwork,std::less<>> models_;
    size_t bytes_{};
public:
    Artwork suit;
    bool AddModel(const std::string& key,Artwork image) {
        if(key.empty()||key.size()>64||models_.size()>=24||models_.count(key)||
            !image.width||!image.height||image.width>1024||image.height>512||
            image.rgba.size()!=size_t(image.width)*image.height*4||bytes_+image.rgba.size()>32*1024*1024)return false;
        bytes_+=image.rgba.size();models_.emplace(key,std::move(image));return true;
    }
    bool LoadModelView(const std::string& key,const std::filesystem::path& path) {
        Artwork image;return image.Load(path)&&AddModel(key,std::move(image));
    }
    const Artwork* Model(const std::string& key) const {
        const auto found=models_.find(key);return found==models_.end()?nullptr:&found->second;
    }
    bool AddIcon(const std::string& key,const std::filesystem::path& path) {
        if(key.empty() || key.size()>96 || icons_.size()>=128 || icons_.count(key)) return false;
        Artwork image;
        if(!image.Load(path) || bytes_+image.rgba.size()>32*1024*1024) return false;
        bytes_+=image.rgba.size(); icons_.emplace(key,std::move(image)); return true;
    }
    const Artwork* Icon(const std::string& key) const {
        const auto it=icons_.find(key); return it==icons_.end()?nullptr:&it->second;
    }
    bool AddMap(Key id,const std::filesystem::path& path) {
        if (!id || maps_.size()>=8 || maps_.count(id)) return false;
        Artwork image;
        if (!image.Load(path) || bytes_+image.rgba.size()>32*1024*1024) return false;
        bytes_+=image.rgba.size(); maps_.emplace(id,std::move(image)); return true;
    }
    const Artwork* Map(Key id) const {
        const auto found=maps_.find(id); return found==maps_.end()?nullptr:&found->second;
    }
};
} // namespace c3
