#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

namespace c3probe {
inline bool ReadBytes(uintptr_t address,void* output,size_t bytes) noexcept {
    SIZE_T count{};
    return address>=0x10000 && bytes && bytes<=65536 &&
        uint64_t(address)+bytes<=0x100000000ull &&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),output,bytes,&count) && count==bytes;
}
template<class T> bool Read(uintptr_t address,T& output) noexcept { return ReadBytes(address,&output,sizeof(output)); }
class Digest {
    BCRYPT_ALG_HANDLE algorithm_{};
    BCRYPT_HASH_HANDLE hash_{};
    std::vector<UCHAR> storage_;
    bool valid_{};
public:
    Digest() {
        DWORD bytes{},returned{};
        if (BCryptOpenAlgorithmProvider(&algorithm_,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0 ||
            BCryptGetProperty(algorithm_,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&bytes),4,&returned,0)<0 ||
            !bytes || bytes>65536) return;
        storage_.resize(bytes);
        valid_=BCryptCreateHash(algorithm_,&hash_,storage_.data(),bytes,nullptr,0,0)>=0;
    }
    ~Digest() { if(hash_) BCryptDestroyHash(hash_); if(algorithm_) BCryptCloseAlgorithmProvider(algorithm_,0); }
    Digest(const Digest&)=delete;
    Digest& operator=(const Digest&)=delete;
    bool Add(const void* data,ULONG bytes) {
        valid_=valid_ && BCryptHashData(hash_,reinterpret_cast<PUCHAR>(const_cast<void*>(data)),bytes,0)>=0;
        return valid_;
    }
    std::string Finish() {
        std::array<UCHAR,32> bytes{}; char hex[65]{};
        if(!valid_ || BCryptFinishHash(hash_,bytes.data(),DWORD(bytes.size()),0)<0) return {};
        valid_=false;
        for(size_t i=0;i<bytes.size();++i) std::snprintf(hex+2*i,3,"%02x",bytes[i]);
        return hex;
    }
};
inline std::string MemoryHash(uintptr_t address,size_t bytes) {
    if(!bytes || bytes>65536) return {};
    std::vector<uint8_t> copy(bytes),again(bytes);
    if(!ReadBytes(address,copy.data(),bytes) || !ReadBytes(address,again.data(),bytes) || copy!=again) return {};
    Digest digest; if(!digest.Add(copy.data(),ULONG(bytes))) return {}; return digest.Finish();
}
inline std::string FileHash(const wchar_t* path,uint64_t expectedBytes) {
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER size{}; bool ok=GetFileSizeEx(file,&size) && uint64_t(size.QuadPart)==expectedBytes;
    std::string result;
    try {
        Digest digest; std::array<uint8_t,65536> buffer{};
        while(ok) {
            DWORD count{};
            if(!ReadFile(file,buffer.data(),DWORD(buffer.size()),&count,nullptr)) { ok=false; break; }
            if(!count) break;
            ok=digest.Add(buffer.data(),count);
        }
        if(ok) result=digest.Finish();
    } catch(...) { CloseHandle(file); throw; }
    CloseHandle(file); return result;
}
} // namespace c3probe
