#include "ForgeAssets.h"
#include <cassert>
#include <cstdio>
using namespace ac2ds::assets;
static void Put16(Bytes& b, size_t p, uint16_t n) { b[p] = uint8_t(n); b[p + 1] = uint8_t(n >> 8); }
static void Put32(Bytes& b, size_t p, uint32_t n) { Put16(b, p, uint16_t(n)); Put16(b, p + 2, uint16_t(n >> 16)); }
static Bytes Texture() {
    Bytes b(114); Put32(b, 0, 0xa2b7e917); Put32(b, 4, 101);
    Put32(b, 22, 4); Put32(b, 26, 4); Put32(b, 30, 1); Put32(b, 34, 4); Put32(b, 94, 16);
    for (int i = 0; i < 8; ++i) b[98 + i] = 0xff;
    Put16(b, 106, 0xf800); Put16(b, 108, 0x07e0);
    return b;
}
template<class F> static void Reject(F f) { bool rejected = false; try { f(); } catch (const std::exception&) { rejected = true; } assert(rejected); }
int main(int argc, char** argv) {
    auto b = Texture(); auto texture = DecodeTexture(b);
    assert(texture->width == 4 && texture->height == 4 && texture->bgra.size() == 16);
    for (auto pixel : texture->bgra) assert(pixel == 0xffff0000);
    Put32(b, 110, 0xe4e4e4e4); texture = DecodeTexture(b);
    assert(texture->bgra[0] == 0xffff0000 && texture->bgra[1] == 0xff00ff00);
    assert(texture->bgra[2] == 0xffaa5500 && texture->bgra[3] == 0xff55aa00);
    b[98] = 0x70; texture = DecodeTexture(b); assert(!(texture->bgra[0] >> 24) && texture->bgra[1] >> 24 == 119);
    for (size_t length = 0; length < b.size(); ++length) {
        auto truncated = b; truncated.resize(length); Reject([&] { DecodeTexture(truncated); });
    }
    auto invalid = b; Put32(invalid, 22, UINT32_MAX); Reject([&] { DecodeTexture(invalid); });
    invalid = b; Put32(invalid, 34, 3); Reject([&] { DecodeTexture(invalid); });
    invalid = b; invalid[12] = 1; Reject([&] { DecodeTexture(invalid); });
    auto bgra = Texture(); bgra.resize(98 + 64); Put32(bgra, 4, uint32_t(bgra.size() - 13));
    Put32(bgra, 34, 0); Put32(bgra, 94, 64);
    for (size_t i = 0; i < 16; ++i) Put32(bgra, 98 + i * 4, uint32_t(i) * 0x11000000 | 0x00123456);
    auto raw = DecodeTexture(bgra);
    for (size_t i = 0; i < 16; ++i) assert(raw->bgra[i] == (uint32_t(i) * 0x11000000 | 0x00123456));
    invalid = bgra; Put32(invalid, 94, 63); Reject([&] { DecodeTexture(invalid); });
    invalid = bgra; invalid.pop_back(); Put32(invalid, 4, uint32_t(invalid.size() - 13)); Reject([&] { DecodeTexture(invalid); });
    auto dxt1 = Texture(); dxt1.resize(106); Put32(dxt1, 4, 93); Put32(dxt1, 34, 2); Put32(dxt1, 94, 8);
    Put16(dxt1, 98, 0); Put16(dxt1, 100, 0xffff); Put32(dxt1, 102, 0xe4e4e4e4);
    auto decoded = DecodeTexture(dxt1);
    assert(decoded->bgra[0] == 0xff000000 && decoded->bgra[1] == 0xffffffff);
    assert(decoded->bgra[2] == 0xff7f7f7f && decoded->bgra[3] == 0);
    Put16(dxt1, 98, 0xf800); Put16(dxt1, 100, 0x07e0); decoded = DecodeTexture(dxt1);
    assert(decoded->bgra[2] == 0xffaa5500 && decoded->bgra[3] == 0xff55aa00);
    auto dxt5 = Texture(); Put32(dxt5, 34, 5);
    for (bool descending : {false, true}) {
        dxt5[98] = descending ? 255 : 0; dxt5[99] = descending ? 0 : 255;
        uint64_t bits = 0; for (unsigned i = 0; i < 16; ++i) bits |= uint64_t(i % 8) << (3 * i);
        for (unsigned i = 0; i < 6; ++i) dxt5[100 + i] = uint8_t(bits >> (8 * i));
        decoded = DecodeTexture(dxt5);
        const unsigned expected[2][8]{{0,255,51,102,153,204,0,255},{255,0,218,182,145,109,72,36}};
        for (unsigned i = 0; i < 16; ++i) assert(decoded->bgra[i] >> 24 == expected[descending][i % 8]);
    }
    Bytes index(16); Put16(index, 0, 1); Put32(index, 2, 7); Put32(index, 6, uint32_t(b.size()));
    assert(DecodeBundle({index,b})->width == 4);
    Put16(index,10,0xffff);assert(BundleResource({index,b},7)==b);assert(BundleResource({index,b},8).empty());
    auto worldIndex=index;Put16(worldIndex,10,0);Reject([&]{BundleResource({worldIndex,b},7);});
    auto worldData=b;worldData.push_back(0);Reject([&]{BundleResource({index,worldData},7);});
    worldIndex.resize(26);Put16(worldIndex,0,2);Put32(worldIndex,12,7);Put32(worldIndex,16,uint32_t(b.size()));Put16(worldIndex,20,0xffff);
    worldData=b;worldData.insert(worldData.end(),b.begin(),b.end());Reject([&]{BundleResource({worldIndex,worldData},7);});
    auto extra = b; extra.push_back(0); Reject([&] { DecodeBundle({index,extra}); });
    Bytes stream(29); const uint8_t magic[]{0x33,0xaa,0xfb,0x57,0x99,0xfa,0x04,0x10};
    std::copy_n(magic,8,stream.data()); stream[10]=2;stream[14]=128;Put16(stream,15,1);Put16(stream,17,4);Put16(stream,19,4);
    stream[25]=1;stream[26]=2;stream[27]=3;stream[28]=4;
    auto unpacked=Unpack(stream);assert(unpacked.size()==1 && unpacked[0]==Bytes({1,2,3,4}));
    for(size_t n=8;n<stream.size();++n){auto truncated=stream;truncated.resize(n);Reject([&]{Unpack(truncated);});}
    auto corrupt=stream;Put16(corrupt,17,10);Reject([&]{Unpack(corrupt);});
    auto rawStream=[&](const Bytes& data){
        Bytes output(25);std::copy_n(magic,8,output.data());output[10]=2;output[14]=128;
        Put16(output,15,1);Put16(output,17,uint16_t(data.size()));Put16(output,19,uint16_t(data.size()));
        output.insert(output.end(),data.begin(),data.end());return output;
    };
    auto packed=rawStream(index), dataStream=rawStream(b);packed.insert(packed.end(),dataStream.begin(),dataStream.end());
    size_t readBytes=0;
    auto reader=[&](size_t at,size_t count){assert(Range(packed.size(),at,count));readBytes+=count;return Bytes(packed.begin()+at,packed.begin()+at+count);};
    SparseBundle sparse(reader,packed.size());assert(sparse.Contains(7) && !sparse.Contains(8));
    assert(readBytes<packed.size());assert(sparse.Load(7)==b && sparse.Load(8).empty());
    for(size_t n=0;n<packed.size();++n)Reject([&]{SparseBundle bad(reader,n);});
    auto invalidPacked=packed;invalidPacked[10]=9;
    Reject([&]{SparseBundle bad([&](size_t at,size_t n){return Bytes(invalidPacked.begin()+at,invalidPacked.begin()+at+n);},invalidPacked.size());});
    // Optional private archive check. Original bytes never enter fixtures or
    // payload packaging; validate the known UI texture and portrait bundles.
    if (argc == 2) {
        ForgeArchive archive{std::filesystem::path(argv[1])};
        assert(archive.Contains(0x58e12f7a) && archive.Contains(0xa5776e76));
        auto medicine=archive.Load(0x58e12f7a), portrait=archive.Load(0xa5776e76);
        assert(medicine && medicine->width==512 && medicine->height==256);
        assert(portrait && portrait->width==256 && portrait->height==512);
        for (const auto& sample : {std::make_pair("medicine",medicine), std::make_pair("leonardo",portrait)}) {
            std::ofstream out(std::string(".work/ac2/") + sample.first + ".bgra", std::ios::binary);
            out.write(reinterpret_cast<const char*>(sample.second->bgra.data()), sample.second->bgra.size() * 4);
        }
        assert(!archive.Load(0));
    }
    std::puts("Scimitar bundles, bounded LZO2A, DXT1/3/5 alpha/color and private UI assets passed");
}
