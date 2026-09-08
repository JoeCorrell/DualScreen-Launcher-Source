#include "../src/NativeMissionRead.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <limits>
using namespace c3::mission_research;
template<size_t N> void Put(std::array<unsigned char,N>& bytes, size_t at, uint32_t value) {
    for (size_t i=0;i<4;++i) bytes[at+i]=static_cast<unsigned char>(value>>(i*8));
}
int main() {
    std::array<unsigned char,RecordBytes> bytes{};
    Put(bytes,4,0x10000); Put(bytes,8,0x20000); Put(bytes,12,0x30000);
    Put(bytes,16,0x40000); Put(bytes,20,0x7777); Put(bytes,28,3);
    Record record{};
    assert(DecodeRecord(bytes.data(),bytes.size(),record));
    assert(record.titleKeyAddress==0x10000 && record.descriptionKeyAddress==0x20000);
    assert(record.identityAddress==0x30000 && record.cachedEntityId==0x7777);
    assert(record.status==NativeStatus::Active && !record.secondary);
    int32_t movie{};
    for(uint32_t state=0;state<4;++state) {
        Put(bytes,28,state); assert(DecodeRecord(bytes.data(),bytes.size(),record));
        assert(MovieStatus(record.status,movie)==(state!=0));
        assert(movie==(state==0?-1:state==3?0:static_cast<int32_t>(state)));
    }
    Put(bytes,28,4); assert(!DecodeRecord(bytes.data(),bytes.size(),record));
    assert(record.identityAddress==0);
    Put(bytes,28,3); bytes[0x25]=2;
    assert(!DecodeRecord(bytes.data(),bytes.size(),record));
    bytes[0x25]=1; bytes[0x24]=1;
    assert(DecodeRecord(bytes.data(),bytes.size(),record) && record.secondary && record.silent);
    assert(!DecodeRecord(nullptr,RecordBytes,record));
    assert(!DecodeRecord(bytes.data(),RecordBytes-1,record));
    assert(!DecodeRecord(bytes.data(),RecordBytes+1,record));
    Put(bytes,28,UINT32_MAX);
    assert(!DecodeRecord(bytes.data(),bytes.size(),record) && record.identityAddress==0);
    assert(!MovieStatus(static_cast<NativeStatus>(UINT32_MAX),movie) && movie==-1);
    Put(bytes,28,3); bytes[0x24]=255;
    assert(!DecodeRecord(bytes.data(),bytes.size(),record));
    bytes[0x24]=0; bytes[0x25]=0;
    std::array<unsigned char,24> event{}; event.fill(0xaa);
    Put(event,4,2); Put(event,8,0x12340000); event[12]=4; event[20]=3; event[16]=1;
    ChangedEvent changed{};
    assert(DecodeChangedPayload(event.data(),event.size(),changed));
    assert(changed.recordAddress==0x12340000 && changed.rawFlag);
    event[16]=2; assert(!DecodeChangedPayload(event.data(),event.size(),changed));
    event[16]=0; event[20]=1; assert(!DecodeChangedPayload(event.data(),event.size(),changed));
    event[20]=3; Put(event,8,0xfffffff0);
    assert(!DecodeChangedPayload(event.data(),event.size(),changed));
    assert(changed.recordAddress==0 && !changed.rawFlag);
    Put(event,8,0x10000);
    for(uint32_t count : {0u,1u,3u,UINT32_MAX}) {
        Put(event,4,count);
        assert(!DecodeChangedPayload(event.data(),event.size(),changed));
    }
    Put(event,4,2); event[12]=2;
    assert(!DecodeChangedPayload(event.data(),event.size(),changed));
    event[12]=4; Put(event,8,0xffff);
    assert(!DecodeChangedPayload(event.data(),event.size(),changed));
    Put(event,8,0x10000);
    assert(DecodeChangedPayload(event.data(),event.size(),changed));
    assert(!DecodeChangedPayload(event.data(),23,changed) && changed.recordAddress==0);
    assert(!DecodeChangedPayload(event.data(),25,changed));
    assert(!DecodeChangedPayload(nullptr,24,changed));
    std::array<unsigned char,16> level{};
    Put(level,0,0x398862c4); Put(level,4,0x398862d0);
    Put(level,8,0x11000); Put(level,12,0x12000);
    LevelIdentity identity{};
    assert(DecodeLevelHeader(level.data(),level.size(),identity));
    assert(identity.nameAddress==0x11000 && identity.pathAddress==0x12000);
    Put(level,4,0x398862c4);
    assert(!DecodeLevelHeader(level.data(),level.size(),identity));
    assert(identity.nameAddress==0 && identity.pathAddress==0);
    Put(level,4,0x398862d0); Put(level,8,0);
    assert(!DecodeLevelHeader(level.data(),level.size(),identity));
    Put(level,8,0x11000); Put(level,12,0xffff);
    assert(!DecodeLevelHeader(level.data(),level.size(),identity));
    assert(!DecodeLevelHeader(nullptr,16,identity));
    assert(!DecodeLevelHeader(level.data(),15,identity));
    assert(!DecodeLevelHeader(level.data(),17,identity));
    std::array<unsigned char,0x88> entity{};
    Put(entity,0,0x398eccc0); Put(entity,12,0x7777);
    Put(entity,0x58,0x3f800000); Put(entity,0x68,0x40000000); Put(entity,0x78,0x40400000);
    Put(entity,0x80,0x3f800000);
    EntityPose pose{};
    assert(DecodeEntityPose(entity.data(),entity.size(),0x7777,pose));
    assert(pose.position.x==1 && pose.position.y==2 && pose.position.z==3);
    assert(pose.forwardKnown && pose.cachedForward.y==1);
    entity[8]=2;
    assert(DecodeEntityPose(entity.data(),entity.size(),0x7777,pose) && !pose.forwardKnown);
    assert(pose.cachedForward.y==0); // Dirty cache never resurrects old direction.
    entity[8]=0; Put(entity,0x80,0x7fc00001);
    assert(DecodeEntityPose(entity.data(),entity.size(),0x7777,pose) && !pose.forwardKnown);
    Put(entity,0x80,0);
    assert(DecodeEntityPose(entity.data(),entity.size(),0x7777,pose) && !pose.forwardKnown);
    Put(entity,0,0x398a60c0); // Actual observed actor+4 GameObject interface.
    assert(!DecodeEntityPose(entity.data(),entity.size(),0x7777,pose) && !pose.entityId);
    Put(entity,0,0x398eccc0);
    assert(!DecodeEntityPose(entity.data(),entity.size(),0x9999,pose));
    assert(!DecodeEntityPose(entity.data(),entity.size(),0,pose));
    assert(!DecodeEntityPose(entity.data(),entity.size()-1,0x7777,pose));
    assert(!DecodeEntityPose(entity.data(),entity.size()+1,0x7777,pose));
    assert(!DecodeEntityPose(nullptr,entity.size(),0x7777,pose));
    Put(entity,0x58,0x7f800000);
    assert(!DecodeEntityPose(entity.data(),entity.size(),0x7777,pose));
    std::array<unsigned char,48> metadata{};
    Put(metadata,12,0x42c80000); Put(metadata,16,0x43480000); // 100,200
    Put(metadata,20,0x3f800000); metadata[44]=1; // Scale1, enabled
    MapMetadata map{};
    assert(DecodeMapMetadata(metadata.data(),metadata.size(),map));
    assert(map.enabled && map.bounds.endX==100 && map.bounds.endY==200);
    metadata[44]=2;
    assert(!DecodeMapMetadata(metadata.data(),metadata.size(),map));
    metadata[44]=1; Put(metadata,20,0);
    assert(!DecodeMapMetadata(metadata.data(),metadata.size(),map));
    metadata[44]=0;
    assert(DecodeMapMetadata(metadata.data(),metadata.size(),map) && !map.enabled);
    // New invalid/disabled samples must not leave the previous enabled result.
    Put(metadata,20,0x3f800000); metadata[44]=1;
    const auto validMetadata=metadata;
    for(size_t offset : {4u,8u,12u,16u,20u,28u}) {
        for(uint32_t bad : {0x7f800000u,0xff800000u,0x7fc00001u}) {
            metadata=validMetadata; Put(metadata,offset,bad);
            assert(!DecodeMapMetadata(metadata.data(),metadata.size(),map));
            assert(!map.enabled && map.bounds.endX==0 && map.scale==0);
        }
    }
    metadata=validMetadata; Put(metadata,4,0xff7fffff); Put(metadata,12,0x7f7fffff);
    assert(!DecodeMapMetadata(metadata.data(),metadata.size(),map)); // Finite endpoints, overflowing X span.
    metadata=validMetadata; Put(metadata,8,0xff7fffff); Put(metadata,16,0x7f7fffff);
    assert(!DecodeMapMetadata(metadata.data(),metadata.size(),map));
    metadata=validMetadata; Put(metadata,12,0);
    assert(!DecodeMapMetadata(metadata.data(),metadata.size(),map)); // Zero width.
    metadata=validMetadata; Put(metadata,16,0xbf800000);
    assert(!DecodeMapMetadata(metadata.data(),metadata.size(),map)); // Negative height.
    metadata=validMetadata; Put(metadata,20,0xbf800000);
    assert(!DecodeMapMetadata(metadata.data(),metadata.size(),map)); // Negative scale.
    metadata=validMetadata;
    assert(DecodeMapMetadata(metadata.data(),metadata.size(),map) && map.enabled);
    assert(!DecodeMapMetadata(metadata.data(),47,map) && !map.enabled);
    assert(!DecodeMapMetadata(metadata.data(),49,map));
    assert(!DecodeMapMetadata(nullptr,48,map));
    Point point{};
    const Bounds bounds{10,20,110,220};
    assert(Project(bounds,10,20,point) && point.x==0 && point.y==1);
    assert(Project(bounds,110,220,point) && point.x==1 && point.y==0);
    assert(Project(bounds,60,120,point) && point.x==0.5f && point.y==0.5f);
    assert(Project(bounds,210,120,point) && point.x==2);
    assert(!Project({1,0,1,1},0,0,point));
    assert(!Project(bounds,std::numeric_limits<float>::infinity(),0,point));
    const float max=std::numeric_limits<float>::max();
    assert(!Project({-max,0,max,1},0,0,point));
    // x86 x87 may retain an extended-precision numerator and return finite 2;
    // implementations rounding the intermediate to float instead reject it.
    const bool extremeProjected=Project({-max,0,0,1},max,0,point);
    assert(extremeProjected ? (std::isfinite(point.x) && std::isfinite(point.y)) : (point.x==0 && point.y==0));
    assert(!Project({0,0,std::numeric_limits<float>::min(),1},max,0,point));
    assert(point.x==0 && point.y==0);
    assert(RotateMoviePoint({1,0.5f},1.57079632679f,point));
    assert(std::abs(point.x-0.5f)<0.00001f && std::abs(point.y-1)<0.00001f);
    assert(!RotateMoviePoint({1,1},std::numeric_limits<float>::quiet_NaN(),point));
    std::puts("Native mission copied-value tests passed; no runtime adapter authorized.");
}
