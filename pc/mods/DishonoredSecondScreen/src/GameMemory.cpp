#include "GameMemory.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace dishonored {

bool ReadProcessBytes(Address address, void* output, std::size_t size, void*) noexcept {
    SIZE_T transferred{};
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(uintptr_t(address)),
        output, size, &transferred) && transferred == size;
}
bool Memory::Bytes(Address address, void* output, std::size_t size) noexcept {
    if (!size) return true;
    if (!reader_ || !output || !Span(address, size)) return Reject(Failure::InvalidSpan, address, size);
    if (size > bytesLeft_) return Reject(Failure::Budget, address, size);
    bytesLeft_ -= uint32_t(size);
    auto& cached = reads_[((address >> 2) ^ (address >> 12) ^ uint32_t(size * 17)) % reads_.size()];
    const bool cacheable = cacheReads_ && size <= cached.bytes.size();
    if (cacheable && cached.generation == readGeneration_ && cached.address == address && cached.size == size) {
        std::memcpy(output, cached.bytes.data(), size); ++cacheHits_; return true;
    }
    if (!callsLeft_) return Reject(Failure::Budget, address, size);
    --callsLeft_;
    if (!reader_(address, output, size, context_)) return Reject(Failure::Read, address, size);
    if (cacheable) {
        cached.address = address; cached.size = uint32_t(size); cached.generation = readGeneration_;
        std::memcpy(cached.bytes.data(), output, size);
    }
    return true;
}
bool Memory::Array(Address address, GameArray& output, uint32_t maximum, uint32_t stride) noexcept {
    GameArray next{};
    if (!Read(address, next)) return false;
    if (!stride || next.count < 0 || uint32_t(next.count) > maximum ||
        next.capacity < next.count || next.capacity > 16 * 1024 * 1024 ||
        (next.count && !Span(next.data, uint64_t(next.count) * stride))) return Reject(Failure::Array, address, sizeof(next));
    output = next; return true;
}
bool Memory::CachedAttribute(Address map, GameName name, float& value) noexcept {
    // 00c74240: FName hash bucket = Index & (HashSize-1), chain +58,
    // 96-byte sparse entries. 00c85e90 reads value+10 at entry+8 only when
    // the dirty flag (entry+14, bit1) is clear. A missing key returns zero.
    // Sparse allocation bits use the inline/secondary storage selected by
    // 00425190, preventing a freed entry from passing a name comparison.
    if (name.index <= 0 || name.number < 0 || !Span(map, 0x3c)) return false;
    GameArray entries{}; int32_t bitCount{}, bucketCount{};
    Address allocated{}, buckets{};
    if (!Array(map, entries, 65536, 96) || !Read(map + 0x20, bitCount) || bitCount != entries.count ||
        !Read(map + 0x1c, allocated) || !Read(map + 0x34, buckets) || !Read(map + 0x38, bucketCount) ||
        bucketCount < 0 || bucketCount > 65536 || (bucketCount && (bucketCount & (bucketCount - 1)))) return false;
    if (!bucketCount) { value = 0; return entries.count == 0; }
    // Allocation bits have four inline words; the hash has one inline bucket.
    if ((!allocated && bitCount > 128) || (!buckets && bucketCount > 1)) return false;
    if (!allocated) allocated = map + 0xc;
    if (!buckets) buckets = map + 0x30;
    if (!Span(allocated, uint64_t((bitCount + 31) / 32) * 4) || !Span(buckets, uint64_t(bucketCount) * 4)) return false;
    int32_t index{};
    if (!Read(buckets + (uint32_t(name.index) & uint32_t(bucketCount - 1)) * 4, index)) return false;
    // Bound both work and corrupt/cyclic chains without allocating on tick.
    for (uint32_t visited = 0; index != -1 && visited < 256; ++visited) {
        uint32_t bits{}; GameName key{};
        if (index < 0 || index >= entries.count ||
            !Read(allocated + uint32_t(index / 32) * 4, bits) || !(bits & (uint32_t(1) << (index & 31)))) return false;
        const Address entry = entries.data + uint32_t(index) * 96;
        if (!Read(entry, key)) return false;
        if (key.index == name.index && key.number == name.number) {
            return ModifiedAttribute(entry + 8, value);
        }
        if (!Read(entry + 0x58, index)) return false;
    }
    if (index != -1) return false;
    value = 0; return true;
}
bool Memory::ModifiedAttribute(Address attribute, float& value) noexcept {
    struct Header { float base, minimum, maximum; uint32_t flags; float cached; } header{};
    static_assert(sizeof(Header) == 0x14);
    if (!Read(attribute, header)) return false;
    if (!(header.flags & 2)) {
        if (!std::isfinite(header.cached)) return false;
        value = header.cached; return true;
    }
    // 00c85db0 walks allocated modifier records in ascending sparse-array
    // index order. Override modifiers make that order significant. Lifetime
    // expiration is owned by the engine; the getter itself does not filter it.
    const Address map = attribute + 0x14;
    GameArray modifiers{}; int32_t bitCount{}; Address allocated{};
    if (!std::isfinite(header.base) || !Array(map, modifiers, 4096, 0x1c) ||
        !Read(map + 0x20, bitCount) || bitCount != modifiers.count || !Read(map + 0x1c, allocated) ||
        (!allocated && bitCount > 128)) return false;
    if (!allocated) allocated = map + 0xc;
    if (!Span(allocated, uint64_t((bitCount + 31) / 32) * 4)) return false;
    // Explicit float stores reproduce the native scalar SSE rounding points,
    // including its two separate percentage multiplies (constant 00fb7e7c).
    volatile float result = header.base;
    uint32_t bits{};
    for (int32_t index = 0; index < modifiers.count; ++index) {
        if (!(index & 31) && !Read(allocated + uint32_t(index / 32) * 4, bits)) return false;
        if (!(bits & (uint32_t(1) << (index & 31)))) continue;
        struct Modifier { uint8_t type, padding[3]; float amount; } modifier{};
        if (!Read(modifiers.data + uint32_t(index) * 0x1c + 8, modifier)) return false;
        if (modifier.type > 2) continue; // Native getter ignores unknown operation bytes.
        if (!std::isfinite(modifier.amount)) return false;
        if (modifier.type == 0) result = result + modifier.amount;
        else if (modifier.type == 1) {
            volatile float percentage = modifier.amount * 0.01f;
            volatile float contribution = percentage * header.base;
            result = result + contribution;
        } else result = modifier.amount;
        if (!std::isfinite(float(result))) return false;
    }
    if (header.flags & 1) {
        if (!std::isfinite(header.minimum) || !std::isfinite(header.maximum) || header.minimum > header.maximum) return false;
        if (result < header.minimum) result = header.minimum;
        else if (header.maximum <= result) result = header.maximum;
    }
    value = result; return true;
}
bool Memory::String(Address address, char16_t* output, uint32_t capacity, uint32_t& length) noexcept {
    length = 0;
    if (!capacity || !output) return false;
    output[0] = 0;
    GameArray text{};
    if (!Array(address, text, capacity, sizeof(char16_t))) return false;
    if (!text.count) return true;
    if (!Bytes(text.data, output, uint32_t(text.count) * sizeof(char16_t)) || output[text.count - 1]) {
        output[0] = 0; return false;
    }
    // Embedded NUL would cause the renderer to show only a prefix. Reject that
    // malformed FString instead of calling it a complete document.
    for (int32_t i = 0; i + 1 < text.count; ++i) if (!output[i]) { output[0] = 0; return false; }
    length = uint32_t(text.count - 1); return true;
}
bool Memory::Name(GameName name, char16_t* output, uint32_t capacity, uint32_t& length) noexcept {
    length = 0;
    if (!output || capacity < 2 || name.index < 0 || name.number < 0) return false;
    output[0] = 0;
    auto& cached = (*names_)[uint32_t(name.index) % names_->size()];
    if (!name.number && cached.tag == uint32_t(name.index) + 1) {
        if (cached.length + 1 > capacity) return false;
        std::copy_n(cached.text.data(), cached.length + 1, output);
        length = cached.length; return true;
    }
    Address entries{}, entry{}; int32_t count{}; uint8_t flags{};
    if (!Read(image_ + layout::NameCount, count) || count <= 0 || name.index >= count ||
        count > 4 * 1024 * 1024 || !Read(image_ + layout::Names, entries) ||
        !Span(entries, uint64_t(count) * 4) || !Read(entries + uint32_t(name.index) * 4, entry) ||
        !Span(entry, 0x10 + 2048) || !Read(entry + 8, flags)) return false;
    const bool wide = (flags & 1) != 0;
    // FNameEntry allocations vary in length. Never bulk-read across the end
    // just to compare a short name; each checked read stops at the native NUL.
    for (uint32_t i = 0; i < 1024 && i + 1 < capacity; ++i) {
        char16_t character{};
        if (wide) {
            if (!Read(entry + 0x10 + i * 2, character)) return false;
        } else {
            uint8_t byte{};
            if (!Read(entry + 0x10 + i, byte)) return false;
            character = byte;
        }
        if (!character) {
            length = i;
            if (name.number) {
                char16_t digits[12]{}; uint32_t used{}, number = uint32_t(name.number - 1);
                do { digits[used++] = char16_t(u'0' + number % 10); number /= 10; } while (number);
                if (i + used + 2 > capacity) return false;
                output[length++] = u'_';
                while (used) output[length++] = digits[--used];
            }
            output[length] = 0;
            if (!name.number && length < cached.text.size()) {
                cached.tag = uint32_t(name.index) + 1; cached.length = length;
                std::copy_n(output, length + 1, cached.text.data());
            }
            return true;
        }
        output[i] = character;
    }
    output[0] = 0; return false;
}
bool Memory::ObjectName(Address object, std::u16string_view expected) noexcept {
    GameName name{}; char16_t text[1024]{}; uint32_t size{};
    return Span(object, 0x38) && Read(object + layout::ObjectName, name) &&
        Name(name, text, 1024, size) && std::u16string_view(text, size) == expected;
}
bool Memory::Object(Address object, Address& type) noexcept {
    // Every UObject has this complete 0x38-byte native header (UField's next
    // member begins at +38). Read it together instead of issuing three OS
    // reads for index/flags/class on every repeated validation. Membership
    // is still checked live; no UObject result survives in a pointer cache.
    std::array<uint32_t, 14> header{};
    static_assert(sizeof(header) == 0x38);
    type = 0;
    if (!Span(object, sizeof(header))) return false; // Optional null is not a failed memory read.
    if (!Read(object, header) || header[layout::ObjectIndex / 4] > uint32_t(INT32_MAX) ||
        !Registered(object, int32_t(header[layout::ObjectIndex / 4])) ||
        (header[layout::ObjectFlagsHigh / 4] & layout::PendingKillFlag) ||
        !Span(header[layout::ObjectClass / 4], 0x38)) return false;
    type = header[layout::ObjectClass / 4];
    return true;
}
bool Memory::Registered(Address object, int32_t index) noexcept {
    // 00464990 installs the object in GObjObjects[ObjectIndex] and writes
    // that same index to object+4. Cache only this tick's array header; never
    // cache the membership result or trust readable memory as proof of life.
    if (index < 0) return false;
    if (!registryReady_) {
        if (!Array(image_ + layout::Objects, registry_, 4 * 1024 * 1024, 4)) return false;
        registryReady_ = true;
    }
    Address registered{};
    return index < registry_.count && Read(registry_.data + uint32_t(index) * 4, registered) && registered == object;
}
bool Memory::ObjectsQuiescent() noexcept {
    // The exact two forbidden states checked by StaticFindObject 00463ad0.
    // The adapter never attempts to stop or wait for collection/serialization.
    uint32_t collecting{}, serializing{};
    return Read(image_ + layout::GarbageCollecting, collecting) && !collecting &&
        Read(image_ + layout::Serializing, serializing) && !serializing;
}
bool Memory::IsA(Address object, std::u16string_view className) noexcept {
    Address type{};
    if (!Object(object, type)) return false;
    uint32_t hash = type ^ (type >> 8) ^ (type >> 16);
    for (char16_t c : className) hash = (hash ^ uint32_t(c)) * 16777619u;
    auto& cached = types_[hash % types_.size()];
    const bool cacheable = cacheReads_ && !className.empty() && className.size() <= cached.name.size();
    if (cacheable && cached.generation == readGeneration_ && cached.type == type &&
        std::u16string_view(cached.name.data(), cached.length) == className) {
        ++typeCacheHits_; return cached.matches;
    }
    const Address originalType = type;
    const auto remember = [&](bool matches) {
        if (cacheable && !Failed()) {
            cached.type = originalType; cached.generation = readGeneration_;
            cached.length = uint32_t(className.size()); cached.matches = matches;
            std::copy(className.begin(), className.end(), cached.name.begin());
        }
        return matches;
    };
    for (uint32_t depth = 0; type && depth < 64; ++depth) {
        Address meta{};
        if (!Object(type, meta)) return false;
        if (ObjectName(type, className)) return remember(true);
        Address parent{};
        if (!Span(type, layout::StructSuper + 4) || !Read(type + layout::StructSuper, parent) || parent == type) return false;
        type = parent;
    }
    return type ? false : remember(false);
}
bool Memory::LiveActor(Address actor) noexcept {
    // Actor's own IsPendingKill override 004c6890 additionally checks bit 3
    // at +120; UObject pending-kill alone is insufficient for a player pawn.
    Address type{}; uint8_t state{};
    return Object(actor, type) && Span(actor, 0x121) && Read(actor + 0x120, state) && !(state & 8);
}

bool Reflection::Resolve(Address type, std::u16string_view name, std::u16string_view kind, Field& out) noexcept {
    out = {};
    if (name.empty() || kind.empty() || name.size() >= 96 || kind.size() >= 96) return false;
    for (uint32_t i = 0; i < count_; ++i) {
        const auto& cached = cache_[i];
        if (cached.field.owner == type && std::u16string_view(cached.name.data(), cached.nameLength) == name &&
            std::u16string_view(cached.kind.data(), cached.kindLength) == kind) {
            Address actualKind{}, ownerType{};
            if (!memory_.Object(type, ownerType) || !memory_.Object(cached.field.property, actualKind) ||
                actualKind != cached.field.type) return false;
            out = cached.field; return true;
        }
    }
    uint32_t ownerSize{}; Address schema = type;
    if (!Memory::Span(type, layout::StructSize + 4) || !memory_.Read(type + layout::StructSize, ownerSize) ||
        ownerSize < 0x38 || ownerSize > 1024 * 1024) return false;
    uint32_t remaining = 4096;
    for (uint32_t depth = 0; schema && depth < 64; ++depth) {
        Address child{}, parent{};
        if (!Memory::Span(schema, layout::StructChildren + 4) ||
            !memory_.Read(schema + layout::StructChildren, child) ||
            !memory_.Read(schema + layout::StructSuper, parent) || parent == schema) return false;
        while (child && remaining) {
            --remaining;
            Address next{}, childType{}; uint32_t flags{};
            if (!Memory::Span(child, layout::PropertyOffset + 4) || !memory_.Object(child, childType) ||
                !Memory::Span(childType, layout::ClassCastFlags + 4) ||
                !memory_.Read(childType + layout::ClassCastFlags, flags) ||
                !memory_.Read(child + layout::FieldNext, next) || next == child) return false;
            if ((flags & layout::PropertyCastFlag) && memory_.ObjectName(child, name)) {
                Address offset{};
                if (!memory_.ObjectName(childType, kind) || !memory_.Read(child + layout::PropertyOffset, offset) ||
                    offset < 0x38 || offset >= ownerSize) return false;
                out = {type, child, childType, offset, ownerSize};
                if (count_ < cache_.size()) {
                    auto& cached = cache_[count_++]; cached.field = out;
                    cached.nameLength = uint32_t(name.size()); cached.kindLength = uint32_t(kind.size());
                    std::copy(name.begin(), name.end(), cached.name.begin());
                    std::copy(kind.begin(), kind.end(), cached.kind.begin());
                }
                return true;
            }
            child = next;
        }
        if (child) return false;
        schema = parent;
    }
    return false;
}

} // namespace dishonored
