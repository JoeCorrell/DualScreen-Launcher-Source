#include "NativeReadTransaction.h"
#include <cassert>
#include <iostream>
using namespace ac2ds::native;
struct Pages final : Memory {
    std::vector<uint8_t> bytes=std::vector<uint8_t>(513*4096,0x5a);
    bool readable=true;
    bool Copy(uint32_t address,void* out,size_t count)const override {
        if(!readable || address<0x10000 || uint64_t(address)-0x10000+count>bytes.size())return false;
        std::memcpy(out,bytes.data()+address-0x10000,count);return true;
    }
};
int main(){
    Pages source;std::array<uint8_t,8192> out{};
    // Exhaust every alignment and mask combination, including gaps where a
    // native retain count can change without invalidating consumed identity.
    for(unsigned mask=1;mask<256;++mask){
        ReadTransaction tx(source);
        for(unsigned bit=0;bit<8;++bit)if(mask&(1u<<bit))assert(tx.Copy(0x10000+bit,out.data(),1));
        for(unsigned bit=0;bit<8;++bit){
            source.bytes[bit]^=1;assert(tx.Validate()==!(mask&(1u<<bit)));source.bytes[bit]^=1;
        }
    }
    for(unsigned offset: {0u,1u,7u,8u,63u,64u,4093u,4095u}){
        for(unsigned count: {1u,2u,7u,8u,9u,64u,4096u}){
            ReadTransaction tx(source);assert(tx.Copy(0x10000+offset,out.data(),count));assert(tx.Validate());
            for(unsigned changed: {offset,offset+count-1}){
                source.bytes[changed]^=1;assert(!tx.Validate());source.bytes[changed]^=1;
            }
            source.bytes[offset+count]^=1;assert(tx.Validate());source.bytes[offset+count]^=1;
        }
    }
    ReadTransaction disappearing(source);assert(disappearing.Copy(0x10000,out.data(),4));
    source.readable=false;assert(!disappearing.Validate());source.readable=true;
    ReadTransaction capped(source);
    for(unsigned page=0;page<512;++page)assert(capped.Copy(0x10000+page*4096,out.data(),1));
    assert(!capped.Copy(0x10000+512*4096,out.data(),1) && !capped.Validate());
    ReadTransaction overflow(source);assert(!overflow.Copy(UINT32_MAX-1,out.data(),4) && !overflow.Validate());
    std::cout<<"Native read transactions: exact byte masks, cross-page boundaries, changing/unmapped memory and bounds passed\n";
}
