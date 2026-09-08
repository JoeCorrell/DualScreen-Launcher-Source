#include "../src/VitalsCapture.h"
#include <cassert>
#include <iostream>
#include <limits>

int main() {
    // Synthetic callback copy, not a captured event or runtime ABI test.
    std::array<uint8_t,24> payload{};
    auto make=[&](float energy,int32_t channel) {
        payload.fill(0xcd); // Native publisher leaves type-word high bytes dirty.
        uint32_t count=2; std::memcpy(payload.data()+4,&count,4);
        std::memcpy(payload.data()+8,&energy,4); payload[12]=2;
        std::memcpy(payload.data()+16,&channel,4); payload[20]=1;
    };
    c3::ObservedEnergy decoded{};
    make(89.2461f,0);
    assert(c3::DecodeEnergyEvent(payload.data(),payload.size(),decoded));
    assert(std::abs(decoded.value-89.2461f)<0.0001f && decoded.channel==0);
    assert(!c3::DecodeEnergyEvent(nullptr,24,decoded));
    for (size_t n=0;n<24;++n) assert(!c3::DecodeEnergyEvent(payload.data(),n,decoded));
    assert(!c3::DecodeEnergyEvent(payload.data(),25,decoded));
    for (float energy:{-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        make(energy,0); assert(!c3::DecodeEnergyEvent(payload.data(),24,decoded));
    }
    for (int channel:{-1,2,100}) {
        make(10,channel); assert(!c3::DecodeEnergyEvent(payload.data(),24,decoded));
    }
    make(100,0); payload[12]=1; assert(!c3::DecodeEnergyEvent(payload.data(),24,decoded));
    make(100,0); payload[20]=2; assert(!c3::DecodeEnergyEvent(payload.data(),24,decoded));
    make(100,0); payload[4]=3; assert(!c3::DecodeEnergyEvent(payload.data(),24,decoded));
    make(100,0);
    c3::VitalsCapture capture;
    assert(!capture.Energy(1,1,1000,payload.data(),24));
    capture.Invalidate(1);
    assert(capture.Energy(1,1,1000,payload.data(),24));
    payload.fill(0); // Reader owns values; does not retain input storage.
    assert(capture.Read(1,1000).energy[0].value==100);
    assert(!capture.Read(1,1000).energy[1].known);
    assert(capture.Mode(1,2,1001,2));
    assert(capture.Read(1,1001).mode.value==c3::ObservedSuitMode::Stealth);
    assert(!capture.Mode(1,2,1002,0)); // Duplicate cannot change state.
    assert(!capture.Mode(2,3,1002,0));
    assert(!capture.Mode(1,3,999,0)); // Clock reversal.
    assert(capture.Read(1,1750).energy[0].known);
    assert(!capture.Read(1,1751).energy[0].known);
    assert(capture.Read(1,1751).mode.known);
    assert(!capture.Read(1,1752).mode.known);
    assert(!capture.Read(1,999).mode.known);
    assert(!capture.Read(2,1001).mode.known);
    assert(!capture.Mode(1,3,1002,3)); assert(!capture.Read(1,1002).mode.known);
    make(45,1); assert(capture.Energy(1,4,1003,payload.data(),24));
    assert(capture.Read(1,1003).energy[1].value==45);
    assert(!capture.Energy(1,5,1004,nullptr,24));
    assert(!capture.Read(1,1004).energy[0].known && !capture.Read(1,1004).energy[1].known);
    capture.Invalidate(2); assert(!capture.Read(2,1004).mode.known);
    assert(!capture.Energy(1,6,1005,payload.data(),24));
    assert(capture.Mode(2,1,1005,1));
    assert(capture.Read(2,1005).mode.value==c3::ObservedSuitMode::Armor);
    assert(capture.Mode(2,2,1006,0));
    assert(capture.Read(2,1006).mode.value==c3::ObservedSuitMode::Tactical);
    std::cout<<"PASS: observed vitals decoding, byte tags, owned values, invalidation, stale/duplicate/epoch rejection\n";
}
