#pragma once
#include "NativeInventory.h"
#include "NativeArtwork.h"
#include <array>
#include <algorithm>

namespace ac2ds::native {
struct DatabaseRecord {
    uint64_t id{};
    uint32_t category{}, subcategory{}, title{}, description{}, imageHandle{};
    bool decoded{};
    uint32_t artworkId{};
};
template<size_t Words> struct DatabaseCopy {
    uint32_t slot{}, address{};
    std::array<uint32_t, Words> words{};
};
template<size_t Words>
bool CopyDatabaseArray(const Memory& memory, const PointerArray& array, uint32_t type,
        std::vector<DatabaseCopy<Words>>& records) {
    for (uint32_t i = 0; i < array.Count(); ++i) {
        DatabaseCopy<Words> record; record.slot = array.data + i * 4;
        if (!memory.Read(record.slot, record.address)) return false;
        if (record.address && (!memory.Read(record.address, record.words) || record.words[0] != type)) return false;
        records.push_back(record);
    }
    return true;
}
template<size_t Words>
bool SameDatabaseArray(const Memory& memory, const std::vector<DatabaseCopy<Words>>& records) {
    for (const auto& record : records) {
        uint32_t address{}; std::array<uint32_t, Words> after{};
        if (!memory.Read(record.slot, address) || address != record.address ||
            (address && (!memory.Read(address, after) || after != record.words))) return false;
    }
    return true;
}
// 017fc900: catalog+10 contains raw entry pointers (the filtered menu alone
// uses handles). 017f8570 / 017fcc60 enforce category and byte+60 discovery.
// 017fd184 / 017fd227 select original title and full description descriptors.
inline std::optional<std::vector<DatabaseRecord>> ReadDatabase(const Memory& memory, uint32_t progress) {
    uint32_t manager{}, type{}, codexHandle{}, codexGroup{}; uint8_t options{};
    PointerArray entries{}, documents{}, codex{}, pages{};
    if (!memory.Read(0x022553b0, manager) || !memory.Field(manager, 0, type) || type != 0x01ebb9b8 ||
        !ArrayHeader(memory, manager, 0x10, 1024, entries) ||
        !ArrayHeader(memory, manager, 0x20, 128, documents) ||
        !ArrayHeader(memory, manager, 0x18, 128, codex) ||
        !memory.Member(manager, 0x162, options)) return {};
    std::vector<DatabaseCopy<25>> historical;
    std::vector<DatabaseCopy<15>> letters;
    std::vector<DatabaseCopy<13>> codices;
    if (!CopyDatabaseArray(memory, entries, 0x01ebb8f4, historical) ||
        !CopyDatabaseArray(memory, documents, 0x01eba3ec, letters) ||
        !CopyDatabaseArray(memory, codex, 0x01ebbae8, codices)) return {};
    std::vector<DatabaseRecord> result;
    for (const auto& entry : historical) {
        const auto& row = entry.words;
        if (!entry.address || !(row[24] & 0xff) || (row[5] != 1 && row[5] != 2) || row[15] == UINT32_MAX) continue;
        result.push_back({uint64_t(row[4]) + 1, row[5], row[6], row[15], row[18], row[11], true});
    }
    // 017fb260 suppresses the corresponding older letter when a replacement
    // has been found. Reproduce its ordering on owned flags without mutating
    // the game's database or marking anything read.
    std::vector<bool> hidden;
    std::vector<uint32_t> linked;
    for (const auto& entry : letters) {
        hidden.push_back(!entry.address || ((entry.words[14] >> 8) & 0xff) != 0);
        uint32_t target{};
        if (entry.address && entry.words[4] && !memory.Reference(entry.words[4], target)) return {};
        linked.push_back(target);
    }
    for (size_t i = 0; i < letters.size(); ++i) if (!hidden[i] && linked[i]) {
        for (size_t j = 0; j < letters.size(); ++j) if (letters[j].address == linked[i]) hidden[j] = true;
    }
    for (size_t i = 0; i < letters.size(); ++i) {
        const auto& row = letters[i].words;
        if (!hidden[i] && row[5] != UINT32_MAX)
            result.push_back({(uint64_t(2) << 32) | (i + 1), 5,
                0, row[5], row[8], row[3], true});
    }
    // 017fb2b0 -> 0112d650 pairs the catalog with native CodexTracker pages
    // in ordinal order. The menu's cached +30 found/decoded bits are not used.
    struct PageCopy { uint32_t slot{}, handle{}, object{}, state{}; uint8_t decoded{}; };
    std::vector<PageCopy> pageCopies;
    if (codex.Count()) {
        if (!memory.Field(progress, 0x180, codexHandle) || !memory.Reference(codexHandle, codexGroup) ||
            !codexGroup || !ArrayHeader(memory, codexGroup, 0xc, 128, pages) || pages.Count() != codex.Count()) return {};
        for (uint32_t i = 0; i < pages.Count(); ++i) {
            PageCopy page; page.slot = pages.data + i * 4;
            if (!memory.Read(page.slot, page.handle) || !memory.Reference(page.handle, page.object)) return {};
            if (page.object && (!memory.Field(page.object, 0x10, page.state) ||
                !memory.Member(page.object, 0x38, page.decoded) || page.state > 2)) return {};
            pageCopies.push_back(page);
            const auto& entry = codices[i]; const auto& row = entry.words;
            bool specialPage = ((row[12] >> 8) & 0xff) != 0;
            if (!entry.address || !page.object || (!(options & 0x20) && specialPage) ||
                (page.state != 1 && page.state != 2) || row[6] == UINT32_MAX) continue;
            result.push_back({(uint64_t(3) << 32) | (i + 1), 7, 0, row[6], row[9], row[2], page.decoded != 0});
        }
    }
    for (size_t i = 0; i < letters.size(); ++i) {
        uint32_t target{};
        if (letters[i].address && letters[i].words[4] &&
            (!memory.Reference(letters[i].words[4], target) || target != linked[i])) return {};
    }
    for (const auto& page : pageCopies) {
        uint32_t handle{}, object{}, state{}; uint8_t decoded{};
        if (!memory.Read(page.slot, handle) || handle != page.handle ||
            !memory.Reference(handle, object) || object != page.object ||
            (object && (!memory.Field(object, 0x10, state) || state != page.state ||
                !memory.Member(object, 0x38, decoded) || decoded != page.decoded))) return {};
    }
    for (auto& entry : result) if (!ReadArtworkId(memory, entry.imageHandle, entry.artworkId)) return {};
    uint32_t check{}; uint8_t optionsAfter{};
    if (codex.Count() && (!memory.Field(progress, 0x180, check) || check != codexHandle ||
        !memory.Reference(codexHandle, check) || check != codexGroup || !SameArray(memory, codexGroup, 0xc, pages))) return {};
    if (!SameDatabaseArray(memory, historical) || !SameDatabaseArray(memory, letters) || !SameDatabaseArray(memory, codices) ||
        !SameArray(memory, manager, 0x10, entries) || !SameArray(memory, manager, 0x20, documents) ||
        !SameArray(memory, manager, 0x18, codex) || !memory.Member(manager, 0x162, optionsAfter) || optionsAfter != options ||
        !memory.Read(0x022553b0, check) || check != manager) return {};
    return result;
}
}
