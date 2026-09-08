"""Compile the actual ARH2/Xbc reader functions against bounded in-memory VFS fixtures."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--compiler', default='g++')
args = parser.parse_args()
source = (Path(__file__).resolve().parents[2] /
          'src/android/app/src/main/jni/botw_romfs_assets.cpp').read_text()

def function(name):
    start = source.rfind('\n', 0, source.index(name + '(')) + 1
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

prefix = r'''
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <zlib.h>
#include <zstd.h>
using u8=uint8_t; using u32=uint32_t; using u64=uint64_t;
bool HasMagic(std::span<const u8> s, std::string_view m) {
    return s.size() >= m.size() && std::memcmp(s.data(),m.data(),m.size())==0;
}
u32 ReadU32(std::span<const u8> s,size_t p) { u32 v; std::memcpy(&v,s.data()+p,4); return v; }
u64 ReadU64(std::span<const u8> s,size_t p) { u64 v; std::memcpy(&v,s.data()+p,8); return v; }
struct Store { u64 size=256; u64 GetSize() { return size; } };
struct Root { std::shared_ptr<Store> store=std::make_shared<Store>();
    auto GetFileRelative(const char*) { return store; } };
namespace FileSys { using VirtualDir=std::shared_ptr<Root>; }
std::vector<u8> fixture;
auto ReadRomfsFile(const FileSys::VirtualDir&,const char*,size_t) {
    return std::optional(fixture);
}
struct XcxArchiveRange { u64 offset; u32 disk_size,expanded_size; };
std::unordered_map<u64,XcxArchiveRange> xcx_archive_index;
std::shared_ptr<Store> xcx_archive_store;
bool xcx_archive_loaded=false;
std::chrono::steady_clock::time_point xcx_archive_retry_after;
constexpr auto AssetRetryDelay=std::chrono::seconds(5);
constexpr u32 MaxDecodedSourceSize=64*1024*1024;
namespace Common::Compression {
std::vector<u8> DecompressDataZSTD(std::span<const u8> s) {
    auto size=ZSTD_getFrameContentSize(s.data(),s.size());
    if (size>MaxDecodedSourceSize) return {};
    std::vector<u8> out(size);
    if (ZSTD_decompress(out.data(),out.size(),s.data(),s.size())!=size) return {};
    return out;
}}
void set32(size_t at,u32 value) { std::memcpy(fixture.data()+at,&value,4); }
void set64(size_t at,u64 value) { std::memcpy(fixture.data()+at,&value,8); }
void reset() { xcx_archive_loaded=false; xcx_archive_retry_after={}; }
'''
tests = r'''
int main() {
    auto root=std::make_shared<Root>();
    fixture.resize(48);
    std::memcpy(fixture.data(),"arh2",4);
    set32(4,2); set32(8,16);
    set64(16,123); set32(24,17); set32(28,0);
    set64(32,456); set32(40,24); set32(44,100);
    assert(LoadXcxArchiveIndex(root));
    assert(xcx_archive_index.at(123).expanded_size==0);
    assert(xcx_archive_index.at(456).offset==32);
    reset(); root->store->size=55;
    assert(!LoadXcxArchiveIndex(root)); // The second member is truncated.
    root->store->size=256; reset(); set64(32,123);
    assert(!LoadXcxArchiveIndex(root)); // Ambiguous path hashes cannot win by insertion order.
    reset(); set64(32,456); set32(8,3);
    assert(!LoadXcxArchiveIndex(root));
    std::vector<u8> raw{'L','A','H','D',1,2,3};
    assert(ExpandXcxArchiveMember(raw,0)==raw);
    assert(ExpandXcxArchiveMember(raw,7)==raw);
    assert(!ExpandXcxArchiveMember(raw,8));
    fixture.assign(0x30+raw.size(),0);
    std::memcpy(fixture.data(),"xbc1",4);
    set32(4,0); set32(8,raw.size()); set32(12,raw.size());
    std::copy(raw.begin(),raw.end(),fixture.begin()+0x30);
    assert(ExpandXcxArchiveMember(fixture,0)==raw);
    assert(ExpandXcxArchiveMember(fixture,raw.size())==raw);
    assert(!ExpandXcxArchiveMember(fixture,raw.size()+1));
    set32(8,MaxDecodedSourceSize+1);
    assert(!ExpandXcxArchiveMember(fixture,0));
}
'''
with tempfile.TemporaryDirectory(prefix='xcx-archive-test-') as directory:
    path = Path(directory)
    cpp = path / 'test.cpp'
    exe = path / ('test.exe' if os.name == 'nt' else 'test')
    cpp.write_text(prefix + function('LoadXcxArchiveIndex') +
                   function('ExpandXcxArchiveMember') + tests)
    subprocess.run([args.compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    str(cpp), '-lz', '-lzstd', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('XCX ARH2/Xbc production reader regression cases passed')
