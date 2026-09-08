// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/companion/external_module.h"
#include "common/logging.h"
#include <mutex>
#include <atomic>
#ifdef __ANDROID__
#include <dlfcn.h>
#endif
namespace ExternalCompanion {
namespace {
std::mutex mutex;
DsHostApi host_api{};
const DsModuleApi* module{};
std::string loaded_path;
std::atomic<size_t> mailbox_capacity{};
}
bool Load(const std::string& path, const DsHostApi& host, u64 title, const std::string& version) {
    const std::scoped_lock lock{mutex};
    if (module && path == loaded_path) return module->title_id == title && version == module->version;
    DsSessionInfo info{}; info.size=sizeof(info);
    if (!host.session(host.context,&info) || info.running) return false;
#ifdef __ANDROID__
    // The Java package store has authenticated and made this private file read-only.
    void* handle=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);
    if (!handle) { LOG_ERROR(Loader,"[Companion] dlopen failed: {}",dlerror()); return false; }
    auto entry=reinterpret_cast<DsModuleEntry>(dlsym(handle,"ds_companion_module_v1"));
    host_api=host;
    const auto* candidate=entry ? entry(&host_api) : nullptr;
    if (!candidate || candidate->size<offsetof(DsModuleApi,query) || candidate->abi!=DS_COMPANION_ABI ||
        !candidate->title_id || !candidate->version || !candidate->probe || !candidate->install ||
        !candidate->snapshot || !candidate->action || !candidate->asset || candidate->title_id != title || version != candidate->version) {
        dlclose(handle); return false;
    }
    // Do not unload old code while Android may still be detaching an old view.
    // Handles are pinned until process exit. Activation only happens outside a game.
    module=candidate; loaded_path=path; mailbox_capacity=0;
    LOG_INFO(Loader,"[Companion] external module {} title={:016X}",module->version,module->title_id);
    return true;
#else
    return false;
#endif
}
u64 TitleId() { const std::scoped_lock lock{mutex}; return module ? module->title_id : 0; }
std::optional<Reservation> Probe(const DsNsoInfo& info,const std::string& name) {
    const std::scoped_lock lock{mutex};
    Reservation r{};
    if (!module || !module->probe(&info,name.c_str(),&r.mailbox_size,&r.bridge_size)) return {};
    if (!r.mailbox_size || (r.mailbox_size&0xfff) || (r.bridge_size&0xfff) ||
        r.mailbox_size>0x100000 || r.bridge_size>0x100000) return {};
    mailbox_capacity=r.mailbox_size;
    return r;
}
bool Install(std::vector<u8>& image,size_t start,u32 mailbox,u32 bridge,u32 size) {
    const std::scoped_lock lock{mutex};
    return module && module->install(image.data(),image.size(),start,mailbox,bridge,size);
}
int Snapshot(void* out,size_t size) {
    const std::scoped_lock lock{mutex}; return module ? module->snapshot(out,size) : 0;
}
int Action(int operation,int item,int target) {
    const std::scoped_lock lock{mutex}; return module ? module->action(operation,item,target) : 15;
}
size_t MailboxCapacity() { return mailbox_capacity.load(); }
std::string Query(int kind, const std::string& input) {
    const std::scoped_lock lock{mutex};
    if (!module || module->size < sizeof(DsModuleApi) || !module->query) return {};
    const auto size=module->query(kind,input.c_str(),nullptr,0);
    if (!size || size>4*1024*1024) return {};
    std::string result(size,'\0');
    return module->query(kind,input.c_str(),reinterpret_cast<u8*>(result.data()),size)==size ? result : std::string{};
}
bool Action64(int operation,int64_t argument) {
    const std::scoped_lock lock{mutex};
    return module && module->size >= sizeof(DsModuleApi) && module->action64 && module->action64(operation,argument);
}
std::vector<u8> Asset(const std::string& path) {
    const DsModuleApi* current;
    { const std::scoped_lock lock{mutex}; current = module; }
    if (!current) return {};
    const auto size = current->asset(path.c_str(), nullptr, 0);
    if (!size || size > 8 * 1024 * 1024) return {};
    std::vector<u8> data(size);
    if (current->asset(path.c_str(), data.data(), data.size()) != size) return {};
    return data;
}
}
