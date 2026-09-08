#pragma once
#include <string>

namespace dishonored {
// Diagnostic observer only: never swallows or resumes a native exception.
// The bootstrap pins this module for the lifetime of any callback in flight.
class FaultCapture {
public:
    FaultCapture() = default;
    ~FaultCapture();
    FaultCapture(const FaultCapture&) = delete;
    FaultCapture& operator=(const FaultCapture&) = delete;
    bool Open(const std::wstring& path) noexcept;
private:
    void* registration_{};
    void* file_{};
};
} // namespace dishonored
