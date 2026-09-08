#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ac2ds {
// Exact AC II 00612ab0..00613110 format: big-endian dictionary pairs, ID
// groups and offsets, then variable-width tokens. Pair expansion visits
// the second child before the first, exactly as the game's stack decoder.
// This only decodes owned byte copies; it never reads a game pointer.
inline std::optional<std::wstring> DecodeLocalization(const std::vector<uint8_t>& bytes, uint32_t textId) {
    if (bytes.size() < 10 || bytes.size() > 16 * 1024 * 1024 || textId > INT32_MAX) return {};
    auto has = [&](uint64_t at, uint64_t size) { return at <= bytes.size() && size <= bytes.size() - at; };
    auto u16 = [&](size_t at) -> uint32_t { return uint32_t(bytes[at]) << 8 | bytes[at + 1]; };
    auto u32 = [&](size_t at) -> uint32_t { return u16(at) << 16 | u16(at + 2); };
    const uint32_t threshold = u16(0), dictionaryCount = u16(2);
    if (!threshold || threshold > 255 || dictionaryCount < 2) return {};
    uint64_t groupTable = 4ull + dictionaryCount * 4ull;
    if (!has(groupTable, 2)) return {};
    uint32_t groupCount = u16(size_t(groupTable));
    groupTable += 2;
    if (!groupCount || groupCount > 4096 || !has(groupTable, groupCount * 12ull)) return {};
    uint32_t baseId{}, data{}, index{};
    bool selected = false;
    uint32_t previousId{};
    for (uint32_t i = 0; i < groupCount; ++i) {
        size_t row = size_t(groupTable + i * 12ull);
        uint32_t id = u32(row);
        if (id > INT32_MAX || (i && id <= previousId)) return {};
        previousId = id;
        if (id <= textId) { baseId = id; data = u32(row + 4); index = u32(row + 8); selected = true; }
    }
    if (!selected || textId - baseId >= 65536 || !has(index, 4)) return {};
    uint32_t rowCount = u16(index);
    if (!has(index, 4ull + rowCount * 4ull)) return {};
    uint32_t start{}, end{};
    if (textId == baseId) end = u16(index + 2);
    else {
        uint32_t low = 0, high = rowCount;
        const uint32_t delta = textId - baseId;
        while (low < high) {
            uint32_t middle = low + (high - low) / 2;
            if (u16(index + 4 + middle * 4) < delta) low = middle + 1;
            else high = middle;
        }
        if (low == rowCount || u16(index + 4 + low * 4) != delta) return {};
        start = u16(index + 2 + low * 4); end = u16(index + 6 + low * 4);
    }
    if (end < start || !has(data, end)) return {};
    size_t cursor = size_t(data) + start, finish = size_t(data) + end;
    std::wstring output;
    std::vector<uint32_t> stack;
    uint32_t steps = 0;
    while (cursor < finish) {
        uint32_t symbol = bytes[cursor++];
        if (symbol >= threshold) {
            if (symbol == 255) {
                if (finish - cursor < 2) return {};
                symbol = u16(cursor); cursor += 2;
            } else {
                if (cursor == finish) return {};
                symbol = threshold + (symbol - threshold) * 256 + bytes[cursor++];
            }
        }
        stack.push_back(symbol + 1);
        while (!stack.empty()) {
            uint32_t node = stack.back(); stack.pop_back();
            if (node >= dictionaryCount || ++steps > 131072 || stack.size() > 512) return {};
            uint32_t left = u16(4 + node * 4), right = u16(6 + node * 4);
            if (right) { stack.push_back(left); stack.push_back(right); }
            else {
                if (!left || output.size() >= 32768) return {};
                output.push_back(wchar_t(left));
            }
        }
    }
    return output;
}
}
