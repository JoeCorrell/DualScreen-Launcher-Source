#pragma once
#include "NativeRead.h"
#include <array>
#include <algorithm>
#include <map>

namespace ac2ds::native {
// Bounded page copies turn a contiguous native marker pool into tens of reads
// instead of thousands of per-field system calls. Only bytes actually consumed
// by the reader participate in the final coherence check.
class ReadTransaction final : public Memory {
    // One bit per consumed byte, grouped by native 8-byte comparison words.
    struct Page { std::array<uint8_t,4096> bytes{};std::array<uint8_t,512> used{}; };
    static constexpr std::array<uint64_t,256> Masks() {
        std::array<uint64_t,256> result{};
        for(size_t mask=0;mask<result.size();++mask)
            for(unsigned byte=0;byte<8;++byte)
                if(mask&(1u<<byte))result[mask]|=uint64_t(255)<<(byte*8);
        return result;
    }
    const Memory& source_;mutable std::map<uint32_t,Page> pages_;mutable bool failed_{};
public:
    explicit ReadTransaction(const Memory& source):source_(source){}
    bool Copy(uint32_t address,void* output,size_t bytes)const override {
        if(address<0x10000 || bytes>65536 || address>UINT32_MAX-bytes){failed_=true;return false;}
        auto out=static_cast<uint8_t*>(output);
        while(bytes){
            auto base=address&~4095u;auto found=pages_.find(base);
            if(found==pages_.end()){
                if(pages_.size()>=512){failed_=true;return false;}
                Page page;if(!source_.Copy(base,page.bytes.data(),page.bytes.size())){failed_=true;return false;}
                found=pages_.emplace(base,std::move(page)).first;
            }
            auto offset=address-base;auto n=std::min(bytes,size_t(4096-offset));
            std::memcpy(out,found->second.bytes.data()+offset,n);
            for(size_t at=offset,end=offset+n;at<end;){
                auto count=std::min(size_t(8-at%8),end-at);
                found->second.used[at/8]|=uint8_t(((1u<<count)-1u)<<(at%8));
                at+=count;
            }
            address+=uint32_t(n);out+=n;bytes-=n;
        }
        return true;
    }
    bool Validate()const {
        if(failed_)return false;std::array<uint8_t,4096> after{};
        for(const auto& page:pages_){
            if(!source_.Copy(page.first,after.data(),after.size()))return false;
            if(!std::memcmp(page.second.bytes.data(),after.data(),after.size()))continue;
            static constexpr auto masks=Masks();
            for(size_t word=0;word<page.second.used.size();++word){
                auto used=page.second.used[word];if(!used)continue;
                uint64_t beforeWord{},afterWord{};
                std::memcpy(&beforeWord,page.second.bytes.data()+word*8,8);
                std::memcpy(&afterWord,after.data()+word*8,8);
                if((beforeWord^afterWord)&masks[used])return false;
            }
        }
        return true;
    }
};
}
