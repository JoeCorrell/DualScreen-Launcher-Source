#include "AssetCache.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <future>
using namespace ac2ds;
using namespace ac2ds::assets;
template<class F> static void Wait(F ready) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!ready()) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
int main() {
    std::atomic<unsigned> calls{}, errors{};
    std::promise<void> release; auto gate = release.get_future().share();
    AssetCache cache([&](uint32_t id) -> std::shared_ptr<const Image> {
        ++calls; if (id == 1) gate.wait();
        if (id == 3) throw std::runtime_error("bad texture");
        if (id == 4) return {};
        auto image = std::make_shared<Image>(); image->width=4; image->height=4; image->bgra.resize(16, id); return image;
    }, [&](uint32_t, const char*) { ++errors; }, 64);
    assert(!cache.Request(0)); assert(!cache.Request(1));
    Wait([&] { return calls.load() == 1; });
    // A blocked archive worker never blocks Request, and repeated visible
    // controls do not enqueue duplicate decompression jobs.
    for (int i=0;i<100;++i) assert(!cache.Request(1));
    assert(calls == 1); release.set_value(); Wait([&] { return cache.Revision() == 1; });
    auto held = cache.Request(1); assert(held && held->bgra[0]==1);
    cache.Request(2); Wait([&] { return cache.Revision() == 2; });
    assert(cache.Request(2)); assert(held->bgra[0]==1); // Eviction cannot invalidate an in-flight paint.
    cache.Request(3); Wait([&] { return cache.Revision() == 3; });
    assert(errors==1); for(int i=0;i<100;++i) assert(!cache.Request(3)); assert(calls==3);
    cache.Request(4); Wait([&] { return cache.Revision() == 4; });
    for(int i=0;i<100;++i) assert(!cache.Request(4)); assert(calls==4);
    auto raw=std::make_shared<Image>(); raw->width=4;raw->height=4;raw->bgra.resize(16);
    raw->bgra[1]=0xffff0000;raw->bgra[5]=0xff00ff00;
    auto prepared=UiImage(raw);assert(prepared->width==1 && prepared->height==2);
    assert(prepared->bgra[0]==0xff00ff00 && prepared->bgra[1]==0xffff0000);
    std::puts("Nonblocking artwork requests, deduplication, eviction, owned images and decoder failure tests passed");
}
