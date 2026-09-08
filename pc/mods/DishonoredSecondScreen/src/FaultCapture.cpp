#include "FaultCapture.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <cstdint>

namespace dishonored {
namespace {
std::atomic<HANDLE> output{};
std::atomic<uint32_t> active{}, count{};
std::atomic_flag writing = ATOMIC_FLAG_INIT;
struct Record {
    uint32_t magic, size, code, flags;
    uint64_t tick;
    uint32_t thread, address, eip, esp, ebp, eax, ebx, ecx, edx, esi, edi;
    uint32_t informationCount, information0, information1;
};
static_assert(sizeof(Record) == 80);
LONG CALLBACK Observe(EXCEPTION_POINTERS* exception) noexcept {
    if (!exception || !exception->ExceptionRecord || !exception->ContextRecord) return EXCEPTION_CONTINUE_SEARCH;
    const auto& fault = *exception->ExceptionRecord;
    // Do not perform diagnostic I/O on an exhausted stack: this observer does
    // not reserve a stack guarantee or own the game's exception recovery.
    if (fault.ExceptionCode != EXCEPTION_ACCESS_VIOLATION && fault.ExceptionCode != EXCEPTION_ILLEGAL_INSTRUCTION &&
        fault.ExceptionCode != EXCEPTION_INT_DIVIDE_BY_ZERO)
        return EXCEPTION_CONTINUE_SEARCH;
    active.fetch_add(1, std::memory_order_acq_rel);
    const HANDLE file = output.load(std::memory_order_acquire);
    if (file && !writing.test_and_set(std::memory_order_acquire)) {
        if (count.load(std::memory_order_relaxed) >= 32) {
            writing.clear(std::memory_order_release);
            active.fetch_sub(1, std::memory_order_acq_rel);
            return EXCEPTION_CONTINUE_SEARCH;
        }
        const auto& cpu = *exception->ContextRecord;
        const Record record = {0x31464844, sizeof(Record), fault.ExceptionCode, fault.ExceptionFlags, GetTickCount64(),
            GetCurrentThreadId(), uint32_t(uintptr_t(fault.ExceptionAddress)), cpu.Eip, cpu.Esp, cpu.Ebp,
            cpu.Eax, cpu.Ebx, cpu.Ecx, cpu.Edx, cpu.Esi, cpu.Edi, fault.NumberParameters,
            fault.NumberParameters > 0 ? uint32_t(fault.ExceptionInformation[0]) : 0,
            fault.NumberParameters > 1 ? uint32_t(fault.ExceptionInformation[1]) : 0};
        // Exceptional path only. No formatting, allocation, stack walking,
        // application locks or attempt to repair an invalid game pointer.
        DWORD written{};
        WriteFile(file, &record, sizeof(record), &written, nullptr);
        count.fetch_add(1, std::memory_order_relaxed);
        writing.clear(std::memory_order_release);
    }
    active.fetch_sub(1, std::memory_order_acq_rel);
    return EXCEPTION_CONTINUE_SEARCH;
}
}
bool FaultCapture::Open(const std::wstring& path) noexcept {
    if (registration_ || file_ || output.load(std::memory_order_acquire)) return false;
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart > 1024 * 1024 - 32 * sizeof(Record)) { CloseHandle(file); return false; }
    count.store(0, std::memory_order_relaxed);
    output.store(file, std::memory_order_release);
    registration_ = AddVectoredExceptionHandler(0, Observe);
    if (!registration_) { output.store(nullptr, std::memory_order_release); CloseHandle(file); return false; }
    file_ = file; return true;
}
FaultCapture::~FaultCapture() {
    if (registration_) RemoveVectoredExceptionHandler(registration_);
    if (!file_) return;
    output.store(nullptr, std::memory_order_release);
    // Only the worker waits. A callback may already hold the handle; do not
    // close/reuse it until the observer finishes. If exceptional I/O stalls,
    // retain the handle until process exit rather than racing CloseHandle.
    for (uint32_t i = 0; i < 250 && active.load(std::memory_order_acquire); ++i) Sleep(1);
    if (!active.load(std::memory_order_acquire)) CloseHandle(file_);
}
} // namespace dishonored
