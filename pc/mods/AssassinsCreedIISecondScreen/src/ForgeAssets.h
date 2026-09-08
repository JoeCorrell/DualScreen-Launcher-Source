#pragma once
#include "State.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include <functional>
#include <lzo/lzo2a.h>

namespace ac2ds::assets {
using Bytes = std::vector<uint8_t>;
inline uint16_t U16(const Bytes& b, size_t at) {
    if (at > b.size() || b.size() - at < 2) throw std::runtime_error("asset u16 bounds");
    return uint16_t(b[at]) | uint16_t(b[at + 1]) << 8;
}
inline uint32_t U32(const Bytes& b, size_t at) { return uint32_t(U16(b, at)) | uint32_t(U16(b, at + 2)) << 16; }
inline uint64_t U64(const Bytes& b, size_t at) { return uint64_t(U32(b, at)) | uint64_t(U32(b, at + 4)) << 32; }
inline bool Range(size_t length, size_t at, size_t size) { return at <= length && size <= length - at; }

// v25 FILEDATA has independently bounded LZO2A blocks; no scanning, guessed
// texture offsets, external processes or game allocation are involved.
inline std::vector<Bytes> Unpack(const Bytes& packed) {
    constexpr uint64_t magic = 0x1004fa9957fbaa33ULL;
    if (packed.size() < 8 || U64(packed, 0) != magic) return {packed};
    size_t cursor = 0, total = 0; std::vector<Bytes> streams;
    while (cursor < packed.size()) {
        if (packed.size() - cursor == 4 && !U32(packed, cursor)) break;
        if (!Range(packed.size(), cursor, 17) || U64(packed, cursor) != magic ||
            packed[cursor + 10] != 2 || packed[cursor + 14] != 128 || streams.size() >= 4)
            throw std::runtime_error("unsupported compressed stream");
        const uint32_t count = U16(packed, cursor + 15); cursor += 17;
        if (!count || !Range(packed.size(), cursor, count * 4)) throw std::runtime_error("asset block count");
        auto sizes = cursor; cursor += count * 4; Bytes stream;
        for (uint32_t i = 0; i < count; ++i) {
            const auto expanded = U16(packed, sizes + i * 4), compressed = U16(packed, sizes + i * 4 + 2);
            if (!expanded || !compressed || !Range(packed.size(), cursor, size_t(compressed) + 4) ||
                total + expanded > 64 * 1024 * 1024) throw std::runtime_error("asset block extent");
            total += expanded; cursor += 4; // Preserved checksum, algorithm not assumed.
            auto begin = stream.size(); stream.resize(begin + expanded);
            if (compressed == expanded) std::copy_n(packed.data() + cursor, expanded, stream.data() + begin);
            else {
                lzo_uint length = expanded;
                if (lzo2a_decompress_safe(packed.data() + cursor, compressed, stream.data() + begin, &length, nullptr) != LZO_E_OK ||
                    length != expanded) throw std::runtime_error("LZO2A decode rejected");
            }
            cursor += compressed;
        }
        streams.push_back(std::move(stream));
    }
    return streams;
}

inline uint32_t Color565(uint16_t value) {
    uint32_t r = (value >> 11) & 31, g = (value >> 5) & 63, b = value & 31;
    return ((r * 255 / 31) << 16) | ((g * 255 / 63) << 8) | (b * 255 / 31);
}
inline std::shared_ptr<const Image> DecodeTexture(const Bytes& texture) {
    if (texture.size() < 13 || U32(texture, 0) != 0xa2b7e917) throw std::runtime_error("not a v25 texture");
    size_t name = U32(texture, 8), body = 13 + name;
    if (name > 1024 || !Range(texture.size(), body, 85) || texture[body - 1] ||
        U32(texture, 4) != texture.size() - body) throw std::runtime_error("unsupported texture header");
    uint32_t width = U32(texture, body + 9), height = U32(texture, body + 13);
    const auto format = U32(texture, body + 21);
    if (width < 4 || height < 4 || width > 4096 || height > 4096 || width % 4 || height % 4 ||
        U32(texture, body + 17) != 1 || (format != 0 && format != 2 && format != 4 && format != 5))
        throw std::runtime_error("unsupported texture format");
    // Native D3D9 table 020dddb8, indexed at 00b12803:
    // Scimitar format 0 -> D3DFMT_A8R8G8B8 (21), little-endian BGRA.
    // The original 16/32px doctor, tailor, blacksmith and art-shop icons
    // use this uncompressed format, not a DXT block stream.
    const auto mipSize = format == 0 ? width * height * 4 : width * height / (format == 2 ? 2 : 1);
    if (U32(texture, body + 81) != mipSize || !Range(texture.size(), body + 85, mipSize))
        throw std::runtime_error("unsupported block-compressed mip");
    auto result = std::make_shared<Image>(); result->width = int(width); result->height = int(height);
    result->bgra.resize(size_t(width) * height);
    size_t cursor = body + 85;
    if (format == 0) {
        for (size_t i = 0; i < result->bgra.size(); ++i) result->bgra[i] = U32(texture, cursor + i * 4);
        return result;
    }
    for (uint32_t y = 0; y < height; y += 4) for (uint32_t x = 0; x < width; x += 4) {
        const auto colorAt = cursor + (format == 2 ? 0 : 8);
        const auto c0 = U16(texture, colorAt), c1 = U16(texture, colorAt + 2);
        const bool threeColor = format == 2 && c0 <= c1;
        uint32_t colors[4]{Color565(c0), Color565(c1), 0, 0};
        for (unsigned shift : {0u, 8u, 16u}) {
            auto a = (colors[0] >> shift) & 255, b = (colors[1] >> shift) & 255;
            colors[2] |= (threeColor ? (a + b) / 2 : (2 * a + b) / 3) << shift;
            if (!threeColor) colors[3] |= ((a + 2 * b) / 3) << shift;
        }
        uint32_t alphas[8]{}; uint64_t alphaBits{};
        if (format == 4) alphaBits = U64(texture, cursor);
        else if (format == 5) {
            alphas[0] = texture[cursor]; alphas[1] = texture[cursor + 1];
            alphaBits = U64(texture, cursor) >> 16;
            const unsigned denominator = alphas[0] > alphas[1] ? 7 : 5;
            for (unsigned i = 1; i < denominator; ++i)
                alphas[i + 1] = ((denominator - i) * alphas[0] + i * alphas[1]) / denominator;
            if (denominator == 5) { alphas[6] = 0; alphas[7] = 255; }
        }
        auto selectors = U32(texture, colorAt + 4); cursor += format == 2 ? 8 : 16;
        for (uint32_t i = 0; i < 16; ++i) {
            const auto selector = (selectors >> (i * 2)) & 3;
            const uint32_t alpha = format == 4 ? ((alphaBits >> (i * 4)) & 15) * 17 :
                format == 5 ? alphas[(alphaBits >> (i * 3)) & 7] : threeColor && selector == 3 ? 0 : 255;
            result->bgra[size_t(y + i / 4) * width + x + i % 4] = colors[selector] | alpha << 24;
        }
    }
    return result;
}

// A UI MapDesc bundle may contain metadata alongside its texture. Reject
// ambiguity instead of displaying whichever resource happens to come first.
inline std::shared_ptr<const Image> DecodeBundle(const std::vector<Bytes>& streams) {
    if (streams.size() != 2 || streams[0].size() < 6) throw std::runtime_error("unsupported resource bundle");
    const auto& index = streams[0]; const auto& data = streams[1]; uint32_t count = U16(index, 0);
    if (!count || index.size() != 6 + count * 10 || U32(index, index.size() - 4)) throw std::runtime_error("resource index extent");
    size_t cursor = 0; std::shared_ptr<const Image> result;
    for (uint32_t i = 0; i < count; ++i) {
        auto size = U32(index, 2 + i * 10 + 4);
        if (size < 13 || !Range(data.size(), cursor, size)) throw std::runtime_error("resource size");
        if (U32(data, cursor) == 0xa2b7e917) {
            if (result) throw std::runtime_error("multiple textures in UI bundle");
            result = DecodeTexture(Bytes(data.begin() + cursor, data.begin() + cursor + size));
        }
        cursor += size;
    }
    if (cursor != data.size() || !result) throw std::runtime_error("missing UI texture");
    return result;
}

// Resolve a resource by its native UID inside a world FILEDATA. Validate every
// extent before copying the requested resource; never search payload bytes for
// a plausible texture signature or accept duplicate IDs.
inline Bytes BundleResource(const std::vector<Bytes>& streams, uint32_t requested) {
    if (streams.size()!=2 || streams[0].size()<6) throw std::runtime_error("world resource streams");
    const auto& index=streams[0]; const auto& data=streams[1]; const auto count=U16(index,0);
    if (!count || index.size()!=6+size_t(count)*10 || U32(index,index.size()-4)) throw std::runtime_error("world index extent");
    std::unordered_set<uint32_t> ids; size_t cursor=0, found=0, length=0;
    for(uint32_t i=0;i<count;++i){
        auto id=U32(index,2+i*10), size=U32(index,6+i*10);
        if(U16(index,10+i*10)!=0xffff || size<13 || !Range(data.size(),cursor,size) || !ids.insert(id).second)
            throw std::runtime_error("world resource record");
        if(id==requested){found=cursor;length=size;}
        cursor+=size;
    }
    if(cursor!=data.size())throw std::runtime_error("world resource extent");
    return length?Bytes(data.begin()+found,data.begin()+found+length):Bytes{};
}

// Seekable world decoder. Large city FILEDATAs contain many unrelated meshes
// and textures. Read their small index, then decompress only the LZO blocks
// intersecting a requested UI resource, keeping world geometry out of memory.
class SparseBundle {
public:
    using Reader=std::function<Bytes(size_t,size_t)>;
private:
    struct Block{size_t source{},target{};uint16_t packed{},expanded{};};
    struct Stream{size_t size{};std::vector<Block> blocks;};
    struct Resource{size_t offset{},size{};};
    Reader read_;size_t length_{};std::vector<Stream> streams_;
    std::unordered_map<uint32_t,Resource> resources_;
    Bytes Read(size_t at,size_t size){
        if(!Range(length_,at,size) || size>16*1024*1024)throw std::runtime_error("sparse archive read bounds");
        auto bytes=read_(at,size);if(bytes.size()!=size)throw std::runtime_error("sparse archive short read");return bytes;
    }
    Bytes Slice(const Stream& stream,size_t at,size_t size){
        if(!Range(stream.size,at,size) || size>16*1024*1024)throw std::runtime_error("sparse resource bounds");
        Bytes result(size);size_t copied=0;
        for(const auto& block:stream.blocks){
            if(block.target+block.expanded<=at)continue;if(block.target>=at+size)break;
            auto source=Read(block.source,block.packed);Bytes decoded(block.expanded);
            if(block.packed==block.expanded)decoded=std::move(source);
            else{lzo_uint got=block.expanded;
                if(lzo2a_decompress_safe(source.data(),source.size(),decoded.data(),&got,nullptr)!=LZO_E_OK || got!=block.expanded)
                    throw std::runtime_error("sparse LZO2A decode rejected");}
            auto begin=std::max(at,block.target),end=std::min(at+size,block.target+block.expanded);
            std::copy_n(decoded.data()+begin-block.target,end-begin,result.data()+begin-at);copied+=end-begin;
        }
        if(copied!=size)throw std::runtime_error("sparse resource gap");return result;
    }
public:
    SparseBundle(Reader read,size_t length):read_(std::move(read)),length_(length){
        if(length>256*1024*1024)throw std::runtime_error("world packed extent");
        size_t cursor=0,total=0;
        while(cursor<length_){
            if(length_-cursor==4 && !U32(Read(cursor,4),0))break;
            if(streams_.size()>=2)throw std::runtime_error("world stream count");
            auto header=Read(cursor,17);cursor+=17;
            if(U64(header,0)!=0x1004fa9957fbaa33ULL || header[10]!=2 || header[14]!=128)throw std::runtime_error("world stream encoding");
            auto count=U16(header,15);if(!count)throw std::runtime_error("world block count");
            auto sizes=Read(cursor,size_t(count)*4);cursor+=sizes.size();Stream stream;
            for(uint32_t i=0;i<count;++i){
                auto expanded=U16(sizes,i*4),packed=U16(sizes,i*4+2);
                if(!expanded || !packed || !Range(length_,cursor,size_t(packed)+4) || total+expanded>512*1024*1024)
                    throw std::runtime_error("world block extent");
                stream.blocks.push_back({cursor+4,stream.size,packed,expanded});
                cursor+=size_t(packed)+4;stream.size+=expanded;total+=expanded;
            }
            streams_.push_back(std::move(stream));
        }
        if(streams_.size()!=2 || streams_[0].size>1024*1024)throw std::runtime_error("world index stream");
        auto index=Slice(streams_[0],0,streams_[0].size);auto count=U16(index,0);
        if(!count || index.size()!=6+size_t(count)*10 || U32(index,index.size()-4))throw std::runtime_error("sparse index extent");
        cursor=0;
        for(uint32_t i=0;i<count;++i){
            auto id=U32(index,2+i*10),size=U32(index,6+i*10);
            if(size<13 || U16(index,10+i*10)!=0xffff || !Range(streams_[1].size,cursor,size) ||
                !resources_.emplace(id,Resource{cursor,size}).second)throw std::runtime_error("sparse index record");
            cursor+=size;
        }
        if(cursor!=streams_[1].size)throw std::runtime_error("sparse resource extent");
    }
    bool Contains(uint32_t id)const{return resources_.count(id)!=0;}
    Bytes Load(uint32_t id){auto found=resources_.find(id);return found==resources_.end()?Bytes{}:Slice(streams_[1],found->second.offset,found->second.size);}
};

class ForgeArchive {
    struct Entry { uint64_t offset{}; uint32_t size{}; };
    std::ifstream file_;
    uint64_t length_{};
    std::unordered_map<uint32_t, Entry> entries_;
    Bytes Read(uint64_t offset, size_t size) {
        if (size > 64 * 1024 * 1024 || offset > length_ || size > length_ - offset) throw std::runtime_error("archive file bounds");
        Bytes data(size); file_.clear(); file_.seekg(std::streamoff(offset)); file_.read(reinterpret_cast<char*>(data.data()), std::streamsize(size));
        if (!file_ || size_t(file_.gcount()) != size) throw std::runtime_error("archive short read");
        return data;
    }
public:
    explicit ForgeArchive(const std::filesystem::path& path) : file_(path, std::ios::binary) {
        if (!file_) throw std::runtime_error("archive unavailable");
        file_.seekg(0, std::ios::end); auto size = file_.tellg();
        if (size < 21) throw std::runtime_error("archive truncated"); length_ = uint64_t(size);
        auto header = Read(0, 21);
        if (!std::equal(header.begin(), header.begin() + 9, "scimitar\0") || U32(header, 9) != 25) throw std::runtime_error("not Scimitar v25");
        auto table = Read(U64(header, 13), 40); uint32_t total = U32(table, 0); uint64_t current = U64(table, 32);
        if (!total || total > 250000) throw std::runtime_error("archive entry limit");
        std::unordered_set<uint64_t> seen;
        while (current != UINT64_MAX) {
            if (seen.size() >= 1024 || !seen.insert(current).second) throw std::runtime_error("archive file-set cycle");
            auto set = Read(current, 48); auto count = U32(set, 0);
            if (count > total - entries_.size()) throw std::runtime_error("archive count mismatch");
            auto index = Read(U64(set, 8), size_t(count) * 16); current = U64(set, 16);
            for (uint32_t i = 0; i < count; ++i) {
                Entry e{U64(index, i * 16), U32(index, i * 16 + 12)}; auto id = U32(index, i * 16 + 8);
                if (e.offset > length_ || uint64_t(e.size) + 444 > length_ - e.offset || !entries_.emplace(id, e).second)
                    throw std::runtime_error("invalid archive entry");
            }
        }
        if (entries_.size() != total) throw std::runtime_error("incomplete archive index");
    }
    bool Contains(uint32_t id) const { return entries_.find(id) != entries_.end(); }
    std::unique_ptr<SparseBundle> OpenBundle(uint32_t id){
        const auto found=entries_.find(id);if(found==entries_.end())return {};
        const auto entry=found->second;auto header=Read(entry.offset,444);
        if(!std::equal(header.begin(),header.begin()+8,"FILEDATA") || U32(header,395)!=entry.size)
            throw std::runtime_error("sparse FILEDATA header");
        return std::make_unique<SparseBundle>([this,entry](size_t offset,size_t size){
            if(!Range(entry.size,offset,size))throw std::runtime_error("sparse FILEDATA extent");
            return Read(entry.offset+444+offset,size);},entry.size);
    }
    std::vector<Bytes> WorldBundle(uint32_t id) {
        const auto found=entries_.find(id);if(found==entries_.end())throw std::runtime_error("world bundle missing");
        const auto& entry=found->second;
        if(entry.size>32*1024*1024)throw std::runtime_error("world packed size limit");
        auto header=Read(entry.offset,444);
        if(!std::equal(header.begin(),header.begin()+8,"FILEDATA") || U32(header,395)!=entry.size)
            throw std::runtime_error("world FILEDATA header");
        return Unpack(Read(entry.offset+444,entry.size));
    }
    std::shared_ptr<const Image> Load(uint32_t id) {
        const auto found = entries_.find(id); if (found == entries_.end()) return {};
        const auto& entry = found->second;
        if (entry.size > 16 * 1024 * 1024) throw std::runtime_error("UI packed size limit");
        auto header = Read(entry.offset, 444);
        if (!std::equal(header.begin(), header.begin() + 8, "FILEDATA") || U32(header, 395) != entry.size)
            throw std::runtime_error("invalid FILEDATA");
        return DecodeBundle(Unpack(Read(entry.offset + 444, entry.size)));
    }
};
}
