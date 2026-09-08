#include "host_shim.h"
#include "jni/botw_companion.h"
#include <vector>
#include <string>
namespace Loader { bool InstallBotwCompanionEquipHook(std::vector<u8>&,size_t,u32,u32,u32); }
size_t BotwAsset(const char*,u8*,size_t);
void ResetBotwAssets();
namespace {
uint32_t Probe(const DsNsoInfo* n,const char* name,u32* mailbox,u32* bridge) {
    constexpr u8 build[32]{0xCD,0x57,0xB2,0x3F,0xA4,0xBB,0xAD,0x65,0x80,0x3D,0x97,0x88,0xC0,0x18,0x21,0xEE};
    if(!n || !name || std::strcmp(name,"main") || std::memcmp(n->build_id,build,32) ||
        n->text_address!=0 || n->text_size!=0x15A1C64 || n->ro_address!=0x15A2000 || n->ro_size!=0x5D7330 ||
        n->data_address!=0x1B7A000 || n->data_size!=0x2381D8 || n->bss_size!=0xE5E28) return 0;
    *mailbox=0x4000; *bridge=0x1000; return 1;
}
uint32_t Install(u8* bytes,size_t length,size_t start,u32 mailbox,u32 bridge,u32 size) {
    try {
        if(!bytes || length>0x20000000 || start>length) return 0;
        std::vector<u8> image(bytes,bytes+length);
        if(!Loader::InstallBotwCompanionEquipHook(image,start,mailbox,bridge,size)) return 0;
        ResetBotwAssets(); std::memcpy(bytes,image.data(),length); return 1;
    } catch(...) { return 0; }
}
size_t Query(int32_t kind,const char* input,u8* output,size_t capacity) {
    static thread_local std::string response;
    try {
        if(!output) {
            EmulationSession session;
            switch(kind) {
                case 0: response=BotwCompanion::GetSnapshot(session); break;
                case 1: response=BotwCompanion::GetSnapshot(session,true); break;
                case 2: response=BotwCompanion::GetHeldPreview(session); break;
                case 3: response=BotwCompanion::GetMapDiscovery(session,input ? input : ""); break;
                default: response.clear();
            }
        } else { if(capacity<response.size()) return 0; std::memcpy(output,response.data(),response.size()); }
        return response.size();
    } catch(...) { BotwHost->log(2,"[BOTW Module] Query failed"); return 0; }
}
int32_t Action64(int32_t op,int64_t argument) {
    try { EmulationSession session; return BotwCompanion::PerformAction(session,op,argument); }
    catch(...) { BotwHost->log(2,"[BOTW Module] Action failed; no retry"); return 0; }
}
int32_t Snapshot(void*,size_t) { return 0; }
int32_t Action(int32_t op,int32_t low,int32_t high) { return Action64(op,int64_t(u64(u32(low))|(u64(u32(high))<<32))); }
const DsModuleApi api{sizeof(DsModuleApi),DS_COMPANION_ABI,0x01007EF00011E000,
    BOTW_PACKAGE_VERSION,Probe,Install,Snapshot,Action,BotwAsset,Query,Action64};
}
extern "C" __attribute__((visibility("default"))) const DsModuleApi* ds_companion_module_v1(const DsHostApi* host) {
    if(!host || host->size<sizeof(DsHostApi) || host->abi!=DS_COMPANION_ABI ||
        !host->session || !host->read || !host->write || !host->valid || !host->map_mailbox ||
        !host->log || !host->romfs_size || !host->romfs_read || !host->decode_texture ||
        !host->decode_rgba || !host->encode_png || !host->free_buffer) return nullptr;
    BotwHost=host; return &api;
}
