#pragma once
#include <cstdint>
#include <string_view>

namespace c3::research {
inline bool IsGameProcessName(std::wstring_view name) {
    const auto separator=name.find_last_of(L"/\\");
    if(separator!=std::wstring_view::npos) name.remove_prefix(separator+1);
    constexpr std::wstring_view expected=L"crysis3.exe";
    if(name.size()!=expected.size()) return false;
    for(size_t i=0;i<name.size();++i) {
        const auto c=name[i]>=L'A' && name[i]<=L'Z'?name[i]+(L'a'-L'A'):name[i];
        if(c!=expected[i]) return false;
    }
    return true;
}
// Candidate names identify only which process may be inspected. Capture still
// checks its module, full image SHA-256 and loaded PE layout before copying.
struct ProcessSelection {
    uint32_t pid{};
    bool ambiguous{};
    void Observe(uint32_t candidate,std::wstring_view name) {
        if(!candidate || !IsGameProcessName(name)) return;
        if(pid && pid!=candidate) ambiguous=true;
        else pid=candidate;
    }
    bool Unique() const { return pid && !ambiguous; }
};
} // namespace c3::research
