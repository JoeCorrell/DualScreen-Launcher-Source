#include "MapCache.h"
#include <cassert>
#include <chrono>
#include <cstdio>
using namespace ac2ds;using namespace ac2ds::assets;
int main(){
    Image art;art.width=2;art.height=2;art.bgra={0xffff0000,0xff00ff00,0xff0000ff,0xffffffff};
    MapSurface mask{1,2,2,{255,0,127,255}};auto image=DiscoveredMap(art,mask);
    assert(image->bgra[0]==0xffff0000 && image->bgra[1]==0xffdadee0 && image->bgra[3]==0xffffffff);
    assert(image->bgra[2]!=art.bgra[2] && image->bgra[2]!=0xffdadee0);
    auto invalid=mask;invalid.discovery.pop_back();assert(!ValidSurface(invalid));
    bool rejected=false;try{DiscoveredMap(art,invalid);}catch(...){rejected=true;}assert(rejected);
    std::mutex mutex;std::condition_variable wake;bool entered=false,release=false;unsigned loads=0;
    MapCache cache([&](uint32_t id){
        std::unique_lock<std::mutex> lock(mutex);++loads;
        if(id==1){entered=true;wake.notify_all();wake.wait(lock,[&]{return release;});}
        return std::make_shared<const Image>(art);
    });
    assert(!cache.Request(1,mask));
    {std::unique_lock<std::mutex> lock(mutex);assert(wake.wait_for(lock,std::chrono::seconds(3),[&]{return entered;}));}
    // Requests must return while the loader is explicitly blocked, deduplicate,
    // and supersede earlier generations instead of returning their stale map.
    for(int i=0;i<20;++i)assert(!cache.Request(1,mask));
    auto next=mask;next.artwork=2;next.discovery.assign(4,0);assert(!cache.Request(2,next));
    {std::lock_guard<std::mutex> lock(mutex);release=true;}wake.notify_all();
    for(int i=0;i<300 && !(image=cache.Request(2,next));++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(image);for(auto pixel:image->bgra)assert(pixel==0xffdadee0);
    {std::lock_guard<std::mutex> lock(mutex);assert(loads==2);}
    auto held=image;next.discovery.assign(4,255);assert(!cache.Request(2,next));
    for(int i=0;i<300 && !(image=cache.Request(2,next));++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(image && image->bgra==art.bgra && held->bgra[0]==0xffdadee0);
    {std::lock_guard<std::mutex> lock(mutex);assert(loads==2);} // Mask change reuses original city art.
    assert(!cache.Request(3,next));
    std::puts("Map fog composition, nonblocking worker, generation isolation and city cache passed");
}
