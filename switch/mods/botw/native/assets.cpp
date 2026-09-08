#include "host_shim.h"
#include <vector>
#include <span>
#include <string>
#include <string_view>
#include <optional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <cctype>
namespace {
constexpr u64 BotwTitleId=0x01007EF00011E000;
constexpr size_t MaxSourceSize=4*1024*1024,MaxArchiveSize=64*1024*1024,MaxDecodedSourceSize=64*1024*1024;
constexpr u32 MaxTextureDimension=2048;
constexpr u64 MaxTexturePixels=4*1024*1024;
constexpr auto AssetRetryDelay=std::chrono::seconds(5);
std::mutex mutex;
std::unordered_map<std::string,std::vector<u8>> botw_common_cache;
std::shared_ptr<std::vector<u8>> botw_common_archive;
std::vector<u8> botw_map_cache;
bool botw_common_loaded{};
std::chrono::steady_clock::time_point botw_map_retry_after{},botw_common_retry_after{};
struct TextureInfo { u32 width,height; };
struct DecodedTexture { u32 width,height; std::vector<u8> rgba; };
std::vector<u8> TakeBuffer(u8* data,size_t size) {
    if(!data) return {};
    const std::unique_ptr<u8,decltype(BotwHost->free_buffer)> owner(data,BotwHost->free_buffer);
    return std::vector<u8>(data,data+size);
}
std::optional<std::vector<u8>> ReadRomfsFile(int,std::string_view path,size_t maximum) {
    const std::string name(path); const auto size=BotwHost->romfs_size(name.c_str());
    if(!size || size>maximum) return {};
    std::vector<u8> bytes(size);
    if(BotwHost->romfs_read(name.c_str(),0,bytes.data(),bytes.size())!=size) return {};
    return bytes;
}
std::vector<u8> DecodeTexturePng(std::span<const u8> source,std::string_view texture,bool first=false) {
    size_t size{}; const std::string name(texture);
    auto* data=BotwHost->decode_texture(source.data(),source.size(),name.c_str(),first,&size);
    return TakeBuffer(data,size);
}
std::optional<DecodedTexture> DecodeTextureRgba(std::span<const u8> source,std::string_view texture,bool first=false) {
    size_t size{}; DecodedTexture result{}; const std::string name(texture);
    auto* data=BotwHost->decode_rgba(source.data(),source.size(),name.c_str(),first,&result.width,&result.height,&size);
    if(!data) return {}; result.rgba=TakeBuffer(data,size); return result;
}
std::vector<u8> EncodePng(const TextureInfo& texture,std::span<const u8> rgba) {
    size_t size{}; auto* data=BotwHost->encode_png(rgba.data(),rgba.size(),texture.width,texture.height,&size);
    return TakeBuffer(data,size);
}
#include "assets_logic.inc"
}
void ResetBotwAssets() {
    const std::scoped_lock lock(mutex);
    botw_common_cache.clear(); botw_common_archive.reset(); botw_map_cache.clear();
    botw_common_loaded=false; botw_map_retry_after={}; botw_common_retry_after={};
}
size_t BotwAsset(const char* name,u8* output,size_t capacity) {
    const std::scoped_lock lock(mutex);
    static thread_local std::vector<u8> result;
    try {
        if(!name) return 0;
        if(!output) result=LoadBotwAsset(name);
        else { if(capacity<result.size()) return 0; std::memcpy(output,result.data(),result.size()); }
        return result.size();
    } catch(...) { return 0; }
}
