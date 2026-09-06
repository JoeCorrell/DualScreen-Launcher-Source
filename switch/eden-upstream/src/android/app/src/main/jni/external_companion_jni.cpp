// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <filesystem>
#include <jni.h>
#include <cstdlib>
#include <cstring>
#include "common/logging.h"
#include "core/companion/external_module.h"
#include "core/hle/kernel/k_process.h"
#include "core/memory.h"
#include "jni/native.h"
#include "jni/botw_romfs_assets.h"

namespace {
uint32_t Session(void*, DsSessionInfo* out) {
    if (!out || out->size < sizeof(*out)) return 0;
    auto& session = EmulationSession::GetInstance();
    *out = {}; out->size = sizeof(*out); out->running = session.IsRunning();
    if (!out->running) return 1;
    auto& system = session.System();
    out->title_id = system.GetApplicationProcessProgramID();
    if (auto* process = system.ApplicationProcess()) out->process_id = process->GetProcessId();
    const auto [base, size] = system.GetApplicationProcessMainModule();
    out->main_base = base; out->main_size = size;
    out->mailbox = system.GetApplicationProcessCompanionMailbox();
    const auto build = system.GetApplicationProcessBuildID();
    std::copy(build.begin(), build.end(), out->build_id);
    return 1;
}
uint32_t Valid(void*, uint64_t address, size_t size) {
    auto& session = EmulationSession::GetInstance();
    return session.IsRunning() && size && size <= 16 * 1024 * 1024 &&
        address + size >= address &&
        session.System().ApplicationMemory().IsValidVirtualAddressRange(address, size);
}
uint32_t Read(void*, uint64_t address, void* out, size_t size) {
    if (!out || !Valid(nullptr, address, size)) return 0;
    return EmulationSession::GetInstance().System().ApplicationMemory().ReadBlockUnsafe(address, out, size);
}
uint32_t Write(void*, uint64_t address, const void* data, size_t size) {
    if (!data || !Valid(nullptr, address, size)) return 0;
    return EmulationSession::GetInstance().System().ApplicationMemory().WriteBlock(address, data, size);
}
void* MapMailbox(void*, uint64_t address, size_t size) {
    auto& system = EmulationSession::GetInstance().System();
    const auto mailbox = system.GetApplicationProcessCompanionMailbox();
    // Only the reserved mailbox is atomically mapped; game objects use checked reads.
    if (!mailbox || address < mailbox || address - mailbox >= 0x1000 ||
        size > 0x1000 - (address - mailbox) || !Valid(nullptr, address, size)) return nullptr;
    return system.ApplicationMemory().GetPointerSilent(address);
}
void Log(uint32_t level, const char* message) {
    if (!message) return;
    if (level >= 2) LOG_ERROR(Frontend, "{}", message);
    else if (level == 1) LOG_WARNING(Frontend, "{}", message);
    else LOG_INFO(Frontend, "{}", message);
}
uint64_t FileSize(const char* path) { return DualScreenRomfsAssets::ExternalFileSize(EmulationSession::GetInstance(), path); }
size_t ReadFile(const char* path, uint64_t offset, void* out, size_t size) {
    return DualScreenRomfsAssets::ExternalReadFile(EmulationSession::GetInstance(), path, offset, out, size);
}
uint8_t* Decode(const uint8_t* data, size_t size, const char* name, uint32_t first, size_t* length) {
    if (!length) return nullptr;
    *length = 0;
    if (!data || !name || size > 64 * 1024 * 1024) return nullptr;
    const auto png = DualScreenRomfsAssets::ExternalDecodeTexture({data, size}, name, first != 0);
    auto* out = static_cast<uint8_t*>(std::malloc(png.size()));
    if (!out) return nullptr;
    std::memcpy(out, png.data(), png.size()); *length = png.size(); return out;
}
const DsHostApi Host{sizeof(DsHostApi), DS_COMPANION_ABI, nullptr, Session, Read, Write, Valid, MapMailbox, Log,
    FileSize, ReadFile, Decode, std::free};
}
extern "C" JNIEXPORT jbyteArray JNICALL
Java_org_yuzu_yuzu_1emu_NativeLibrary_externalCompanionAsset(JNIEnv* env, jobject, jstring file) {
    if (!file) return nullptr;
    const char* raw = env->GetStringUTFChars(file, nullptr);
    if (!raw) return nullptr;
    const std::string path(raw); env->ReleaseStringUTFChars(file, raw);
    auto lock = EmulationSession::GetInstance().LockSystemAccess();
    const auto bytes = ExternalCompanion::Asset(path);
    if (bytes.empty()) return nullptr;
    auto result = env->NewByteArray(bytes.size());
    if (result) env->SetByteArrayRegion(result, 0, bytes.size(), reinterpret_cast<const jbyte*>(bytes.data()));
    return result;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_yuzu_yuzu_1emu_NativeLibrary_loadExternalCompanion(JNIEnv* env, jobject, jstring file, jlong title, jstring version) {
    if (!file || !version) return false;
    const char* raw = env->GetStringUTFChars(file, nullptr);
    if (!raw) return false;
    const std::string path(raw); env->ReleaseStringUTFChars(file, raw);
    const char* version_raw = env->GetStringUTFChars(version, nullptr);
    if (!version_raw) return false;
    const std::string expected_version(version_raw); env->ReleaseStringUTFChars(version, version_raw);
    // Java verifies publisher signatures and extracts immutable files in app-private storage.
    // Reject shared storage paths even if an accidental caller bypasses that loader.
    if (path.find("/files/companion-modules/versions/") == std::string::npos ||
        (path.rfind("/data/user/", 0) != 0 && path.rfind("/data/data/", 0) != 0) ||
        path.find("..") != std::string::npos) return false;
    auto lock = EmulationSession::GetInstance().LockSystemAccess();
    return ExternalCompanion::Load(path, Host, static_cast<u64>(title), expected_version);
}
