// SPDX-License-Identifier: GPL-3.0-or-later
#include "host_shim.h"
#include "jni/plza_companion.h"
#include <vector>
#ifndef PLZA_PACKAGE_VERSION
#define PLZA_PACKAGE_VERSION "1.0.0"
#endif
namespace Loader {
bool InstallPlzaCompanionItemHook(std::vector<u8>&,size_t,u32,u32,u32);
}
size_t PlzaAsset(const char*, uint8_t*, size_t);
void ResetPlzaAssets();
namespace {
uint32_t Probe(const DsNsoInfo* nso,const char* name,u32* mailbox,u32* bridge) {
    constexpr u8 build[32]{0xB1,0xF1,0x2F,0xD9,0x19,0xEA,0xE8,0x6A,0xB8,0xA9,0x78,0x31,
        0x76,0x77,0xE6,0x4B,0xCE,0x44,0x3D,0x1F};
    if (!nso || !name || std::strcmp(name,"main") || std::memcmp(nso->build_id,build,32) ||
        nso->text_address!=0 || nso->text_size!=0x3163f70 || nso->ro_address!=0x3164000 ||
        nso->ro_size!=0xa5e000 || nso->data_address!=0x3bc2000 || nso->data_size!=0x3bc430 ||
        nso->bss_size!=0x21fcbd0) return 0;
    *mailbox=0x1000; *bridge=0x4000; return 1;
}
uint32_t Install(u8* bytes,size_t length,size_t start,u32 mailbox,u32 bridge,u32 size) {
    try {
        if (!bytes || length>0x20000000 || start>length) return 0;
        std::vector<u8> image(bytes,bytes+length);
        if (!Loader::InstallPlzaCompanionItemHook(image,start,mailbox,bridge,size)) return 0;
        ResetPlzaAssets();
        std::memcpy(bytes,image.data(),length); return 1;
    } catch (...) { return 0; }
}
int32_t Snapshot(void* out,size_t size) {
    try { EmulationSession session; return PlzaCompanion::FillSnapshot(session,out,size); }
    catch (...) { PlzaHost->log(3,"[PLZA] external snapshot failed"); return 0; }
}
int32_t Action(int32_t op,int32_t item,int32_t target) {
    try { EmulationSession session; return int(PlzaCompanion::PerformItemAction(session,op,item,target)); }
    catch (...) { PlzaHost->log(3,"[PLZA] external action failed; no automatic retry"); return 12; }
}
const DsModuleApi api{sizeof(DsModuleApi),DS_COMPANION_ABI,0x0100F43008C44000,
    PLZA_PACKAGE_VERSION,Probe,Install,Snapshot,Action,PlzaAsset};
}
extern "C" __attribute__((visibility("default")))
const DsModuleApi* ds_companion_module_v1(const DsHostApi* host) {
    if (!host || host->size<sizeof(DsHostApi) || host->abi!=DS_COMPANION_ABI ||
        !host->session || !host->read || !host->write || !host->valid || !host->map_mailbox || !host->log ||
        !host->romfs_size || !host->romfs_read || !host->decode_texture || !host->free_buffer) return nullptr;
    PlzaHost=host; return &api;
}
