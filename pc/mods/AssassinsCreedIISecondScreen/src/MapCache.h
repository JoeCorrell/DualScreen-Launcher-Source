#pragma once
#include "ForgeAssets.h"
#include "KnownCityMaps.h"
#include <condition_variable>
#include <functional>
#include <thread>

namespace ac2ds::assets {
inline bool ValidSurface(const MapSurface& s) {
    return s.artwork && s.width && s.height && s.width<=512 && s.height<=512 && s.discovery.size()==size_t(s.width)*s.height;
}
inline std::shared_ptr<const Image> DiscoveredMap(const Image& art,const MapSurface& surface) {
    if(!ValidSurface(surface) || art.width<=0 || art.height<=0 || art.width>2048 || art.height>2048 ||
        art.bgra.size()!=size_t(art.width)*art.height)throw std::runtime_error("map image extent");
    auto result=std::make_shared<Image>();result->width=art.width;result->height=art.height;result->bgra.resize(art.bgra.size());
    for(int y=0;y<art.height;++y)for(int x=0;x<art.width;++x){
        float mx=std::clamp((x+.5f)*surface.width/art.width-.5f,0.f,float(surface.width-1));
        float my=std::clamp((y+.5f)*surface.height/art.height-.5f,0.f,float(surface.height-1));
        auto x0=unsigned(mx),y0=unsigned(my),x1=std::min(x0+1,surface.width-1),y1=std::min(y0+1,surface.height-1);
        auto alpha=[&](unsigned xx,unsigned yy){return surface.discovery[yy*surface.width+xx];};
        float a=alpha(x0,y0)*(1-(mx-x0))+alpha(x1,y0)*(mx-x0);
        float b=alpha(x0,y1)*(1-(mx-x0))+alpha(x1,y1)*(mx-x0);
        unsigned visibility=unsigned(a*(1-(my-y0))+b*(my-y0));
        uint32_t source=art.bgra[size_t(y)*art.width+x],output=0xff000000;
        visibility=visibility*(source>>24)/255;
        constexpr uint32_t background=0x00dadee0;
        for(unsigned shift:{0u,8u,16u})output|=((((source>>shift)&255)*visibility+((background>>shift)&255)*(255-visibility))/255)<<shift;
        result->bgra[size_t(y)*art.width+x]=output;
    }
    return result;
}

// The native FEU -> MapDesc -> texture chain is verified for each main city.
// Read only the compressed blocks containing those three resources.
inline std::shared_ptr<const Image> LoadCityMap(const std::filesystem::path& directory,uint32_t id) {
    const CityMapBinding* binding=nullptr;
    for(const auto& candidate:CityMapBindings)if(candidate.art==id){binding=&candidate;break;}
    if(!binding)return {};
    ForgeArchive archive(directory/binding->file);auto bundle=archive.OpenBundle(binding->world);
    if(!bundle)throw std::runtime_error("city map bundle missing");
    auto check=[&](uint32_t uid,size_t size,uint32_t expected){
        auto bytes=bundle->Load(uid);uint32_t hash=2166136261u;
        for(auto value:bytes)hash=(hash^value)*16777619u;
        if(bytes.size()!=size || hash!=expected)throw std::runtime_error("unsupported city map binding");
        return bytes;
    };
    check(id,binding->artSize,binding->artHash);
    check(binding->description,binding->descriptionSize,binding->descriptionHash);
    auto raw=DecodeTexture(check(binding->texture,binding->textureSize,binding->textureHash));
    auto image=std::make_shared<Image>();image->width=raw->width;image->height=raw->height;image->bgra.resize(raw->bgra.size());
    for(int y=0;y<raw->height;++y)std::copy_n(raw->bgra.data()+size_t(raw->height-1-y)*raw->width,
        raw->width,image->bgra.data()+size_t(y)*raw->width);
    return image; // Preserve full native canvas; never trim map padding.
}

// One latest request, one worker, one decoded city, one masked result. Reading
// archives and composing a million pixels never run on the game/UI thread.
class MapCache final : public MapSource {
public:
    using Loader=std::function<std::shared_ptr<const Image>(uint32_t)>;
    using Logger=std::function<void(uint32_t,const char*)>;
private:
    std::mutex mutex_;std::condition_variable wake_;bool stop_{},pending_{},have_{};
    uint64_t generation_{},serial_{};MapSurface requested_;
    std::shared_ptr<const Image> result_;Loader loader_;Logger logger_;std::thread worker_;
    void Log(uint32_t id,const char* message) noexcept {try{if(logger_)logger_(id,message);}catch(...){}}
    void Work() noexcept {
        uint32_t loaded{};std::shared_ptr<const Image> art;
        for(;;){
            MapSurface request;uint64_t serial{};
            try{
                {std::unique_lock<std::mutex> lock(mutex_);wake_.wait(lock,[&]{return stop_||pending_;});if(stop_)return;
                    request=requested_;serial=serial_;pending_=false;}
                if(loaded!=request.artwork){art.reset();loaded=request.artwork;
                    try{art=loader_(loaded);Log(loaded,art?"city artwork decoded":"city artwork binding unavailable");}
                    catch(const std::exception& e){Log(loaded,e.what());}catch(...){Log(loaded,"city decoder exception");}}
                auto image=art?DiscoveredMap(*art,request):std::shared_ptr<const Image>{};
                {std::lock_guard<std::mutex> lock(mutex_);if(stop_)return;if(serial==serial_)result_=std::move(image);}
            }catch(const std::exception& e){Log(request.artwork,e.what());}catch(...){Log(request.artwork,"map worker exception");}
        }
    }
public:
    explicit MapCache(Loader loader,Logger logger={}):loader_(std::move(loader)),logger_(std::move(logger)),worker_([this]{Work();}){}
    ~MapCache()override{{std::lock_guard<std::mutex> lock(mutex_);stop_=true;}wake_.notify_one();if(worker_.joinable())worker_.join();}
    std::shared_ptr<const Image> Request(uint64_t generation,const MapSurface& surface) override {
        if(!ValidSurface(surface))return {};
        std::lock_guard<std::mutex> lock(mutex_);
        if(have_ && generation_==generation && requested_==surface)return result_;
        requested_=surface;generation_=generation;have_=true;++serial_;result_.reset();pending_=true;wake_.notify_one();return {};
    }
};
}
