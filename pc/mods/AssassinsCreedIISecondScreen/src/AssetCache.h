#pragma once
#include "ForgeAssets.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <thread>

namespace ac2ds::assets {
// Only visible UI controls request IDs. One worker owns all archive handles,
// indexes and decompression. The caller never waits for a file or decoder.
class AssetCache final : public ImageSource {
public:
    using Loader = std::function<std::shared_ptr<const Image>(uint32_t)>;
    using Logger = std::function<void(uint32_t, const char*)>;
private:
    struct Entry { std::shared_ptr<const Image> image; uint64_t used{}; bool pending{}; };
    const size_t budget_;
    Loader loader_;
    Logger logger_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::unordered_map<uint32_t,Entry> entries_;
    std::deque<uint32_t> queue_;
    size_t bytes_{};
    uint64_t clock_{};
    bool stopping_{};
    std::atomic<uint64_t> revision_{};
    std::thread worker_;
    void Log(uint32_t id,const char* message) noexcept { try { if(logger_)logger_(id,message); } catch(...) {} }
    void Work() noexcept {
        for (;;) {
            uint32_t id{};
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
                if (stopping_) return;
                id = queue_.front(); queue_.pop_front();
            }
            std::shared_ptr<const Image> image;
            try { image = loader_(id); }
            catch (const std::exception& error) { Log(id,error.what()); }
            catch (...) { Log(id,"decoder exception"); }
            size_t bytes = image ? image->bgra.size() * sizeof(uint32_t) : 0;
            if (bytes > budget_) { image.reset(); bytes = 0; }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stopping_) return;
                while (bytes_ + bytes > budget_) {
                    auto oldest = entries_.end();
                    for (auto it = entries_.begin(); it != entries_.end(); ++it)
                        if (it->first != id && !it->second.pending && it->second.image &&
                            (oldest == entries_.end() || it->second.used < oldest->second.used)) oldest = it;
                    if (oldest == entries_.end()) break;
                    bytes_ -= oldest->second.image->bgra.size() * sizeof(uint32_t); entries_.erase(oldest);
                }
                auto found = entries_.find(id);
                if (found != entries_.end()) { found->second.image = std::move(image); found->second.pending = false; bytes_ += bytes; }
                ++revision_;
            }
        }
    }
public:
    explicit AssetCache(Loader loader, Logger logger = {}, size_t budget = 32 * 1024 * 1024)
        : budget_(budget), loader_(std::move(loader)), logger_(std::move(logger)), worker_([this] { Work(); }) {}
    ~AssetCache() override {
        { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
        wake_.notify_one(); if (worker_.joinable()) worker_.join();
    }
    uint64_t Revision() const override { return revision_.load(); }
    std::shared_ptr<const Image> Request(uint32_t id) override {
        if (!id) return {};
        std::lock_guard<std::mutex> lock(mutex_);
        auto found = entries_.find(id);
        if (found != entries_.end()) { found->second.used = ++clock_; return found->second.image; }
        if (stopping_ || queue_.size() >= 64) return {};
        if (entries_.size() >= 4096) {
            auto oldest = entries_.end();
            for (auto it = entries_.begin(); it != entries_.end(); ++it)
                if (!it->second.pending && (oldest == entries_.end() || it->second.used < oldest->second.used)) oldest = it;
            if (oldest == entries_.end()) return {};
            if (oldest->second.image) bytes_ -= oldest->second.image->bgra.size() * sizeof(uint32_t);
            entries_.erase(oldest);
        }
        entries_.emplace(id, Entry{{},++clock_,true}); queue_.push_back(id); wake_.notify_one(); return {};
    }
};

// Game UI artwork uses the opposite vertical origin to the CPU bitmap. Trim
// fully transparent padding only; never remove authored borders or picture data.
inline std::shared_ptr<const Image> UiImage(const std::shared_ptr<const Image>& source) {
    if (!source) return {};
    int left = source->width, top = source->height, right = -1, bottom = -1;
    for (int y = 0; y < source->height; ++y) for (int x = 0; x < source->width; ++x)
        if (source->bgra[size_t(y) * source->width + x] >> 24) {
            left = std::min(left,x); top = std::min(top,y); right = std::max(right,x); bottom = std::max(bottom,y);
        }
    if (right < left || bottom < top) return {};
    auto result = std::make_shared<Image>(); result->width = right-left+1; result->height = bottom-top+1;
    result->bgra.resize(size_t(result->width) * result->height);
    for (int y = 0; y < result->height; ++y)
        std::copy_n(source->bgra.data() + size_t(bottom-y) * source->width + left,
            result->width, result->bgra.data() + size_t(y) * result->width);
    return result;
}

class GameArchives {
    std::filesystem::path directory_;
    std::vector<std::unique_ptr<ForgeArchive>> archives_;
    bool indexed_{};
    std::unique_ptr<SparseBundle> mapIcons_;
    std::unique_ptr<ForgeArchive> city_;
    std::unique_ptr<SparseBundle> cityResources_;
    bool cityIndexed_{};
public:
    explicit GameArchives(std::filesystem::path directory) : directory_(std::move(directory)) {}
    std::shared_ptr<const Image> Load(uint32_t id) {
        if (!indexed_) {
            indexed_ = true;
            for (const auto* name : {L"DataPC_extra.forge", L"DataPC.forge"}) {
                auto path = directory_ / name;
                if (std::filesystem::exists(path)) {
                    archives_.push_back(std::make_unique<ForgeArchive>(path));
                    // Original Animus_Map_Icons contains the Ezio icon and
                    // common marker art as embedded resources, not top-level IDs.
                    if(archives_.back()->Contains(0x2318d8a3))mapIcons_=archives_.back()->OpenBundle(0x2318d8a3);
                }
            }
        }
        for (auto& archive : archives_) if (archive->Contains(id)) return UiImage(archive->Load(id));
        if(mapIcons_ && mapIcons_->Contains(id))return UiImage(DecodeTexture(mapIcons_->Load(id)));
        if(!cityIndexed_){
            cityIndexed_=true;
            auto path=directory_/L"DataPC_Firenze.forge";
            if(std::filesystem::exists(path)){
                city_=std::make_unique<ForgeArchive>(path);cityResources_=city_->OpenBundle(0x5b29460a);
            }
        }
        if(cityResources_ && cityResources_->Contains(id))return UiImage(DecodeTexture(cityResources_->Load(id)));
        return {};
    }
};
}
