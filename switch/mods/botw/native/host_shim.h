// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <utility>
#include "core/companion/module_api.h"
#define FMT_HEADER_ONLY
#include <fmt/format.h>
using u8=uint8_t; using u16=uint16_t; using u32=uint32_t; using u64=uint64_t;
using s8=int8_t; using s16=int16_t; using s32=int32_t; using s64=int64_t; using VAddr=uint64_t;
inline const DsHostApi* BotwHost{};
template<class... A> void ModuleLog(u32 level, fmt::format_string<A...> format, A&&... args) {
    const auto message = fmt::format(format, std::forward<A>(args)...);
    BotwHost->log(level, message.c_str());
}
#define LOG_INFO(category, ...) ModuleLog(0, __VA_ARGS__)
#define LOG_WARNING(category, ...) ModuleLog(1, __VA_ARGS__)
#define LOG_ERROR(category, ...) ModuleLog(2, __VA_ARGS__)
#define ASSERT_MSG(condition, ...) do { if (!(condition)) throw std::runtime_error(fmt::format(__VA_ARGS__)); } while (false)
namespace Core {
namespace Memory {
class Memory {
public:
    bool IsValidVirtualAddressRange(u64 address, size_t size) const { return BotwHost->valid(BotwHost->context,address,size); }
    bool ReadBlockUnsafe(u64 address, void* out, size_t size) const { return BotwHost->read(BotwHost->context,address,out,size); }
    bool WriteBlock(u64 address, const void* in, size_t size) { return BotwHost->write(BotwHost->context,address,in,size); }
    void* GetPointerSilent(u64 address) { return BotwHost->map_mailbox(BotwHost->context,address,1); }
    template<class T> T* GetPointer(u64 address) { return static_cast<T*>(BotwHost->map_mailbox(BotwHost->context,address,sizeof(T))); }
};
}
class Process {
public:
    u64 id{};
    u64 GetProcessId() const { return id; }
};
class System {
    Memory::Memory memory;
    Process process;
public:
    DsSessionInfo info{};
    System() { info.size=sizeof(info); BotwHost->session(BotwHost->context,&info); process.id=info.process_id; }
    u64 GetApplicationProcessProgramID() const { return info.title_id; }
    std::pair<u64,u64> GetApplicationProcessMainModule() const { return {info.main_base,info.main_size}; }
    std::array<u8,32> GetApplicationProcessBuildID() const {
        std::array<u8,32> value{}; std::copy_n(info.build_id,32,value.begin()); return value;
    }
    u64 GetApplicationProcessCompanionMailbox() const { return info.mailbox; }
    Memory::Memory& ApplicationMemory() { return memory; }
    Process* ApplicationProcess() { return process.id ? &process : nullptr; }
};
}
class EmulationSession {
    Core::System system;
public:
    struct AccessGuard {};
    AccessGuard LockSystemAccess() { return {}; } // Host holds the actual session lock around callbacks.
    bool IsRunning() const { return system.info.running; }
    Core::System& System() { return system; }
};
