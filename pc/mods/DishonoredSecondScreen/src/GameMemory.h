#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <type_traits>

namespace dishonored {

using Address = uint32_t;
struct GameArray { Address data{}; int32_t count{}, capacity{}; };
struct GameName { int32_t index{}, number{}; };
static_assert(sizeof(GameArray) == 12 && sizeof(GameName) == 8);

// Read-only interface also permits offline verification with captured buffers.
// Budget exhaustion is an explicit failure, not a partial successful sample.
class Memory {
public:
    enum class Failure : uint8_t { None, InvalidSpan, Budget, Read, Array };
    struct FailureInfo { Failure reason{}; Address address{}; uint32_t bytes{}; };
    using Reader = bool (*)(Address, void*, std::size_t, void*) noexcept;
    Memory(Address image, Reader reader, void* context = nullptr)
        : image_(image), reader_(reader), context_(context) {}
    void Begin(uint32_t byteBudget = 512 * 1024, uint32_t callBudget = 8192, bool cacheReadOnlySample = false) noexcept {
        bytesLeft_ = byteBudget; callsLeft_ = callBudget; failed_ = false;
        initialBytes_ = byteBudget; initialCalls_ = callBudget; failure_ = {};
        registryReady_ = false;
        cacheReads_ = cacheReadOnlySample; cacheHits_ = 0; typeCacheHits_ = 0;
        if (!++readGeneration_) {
            for (auto& entry : reads_) entry.generation = 0;
            for (auto& entry : types_) entry.generation = 0;
            ++readGeneration_;
        }
    }
    bool Bytes(Address address, void* output, std::size_t size) noexcept;
    template<class T> bool Read(Address address, T& output) noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        T next{};
        if (!Bytes(address, &next, sizeof(next))) return false;
        output = next;
        return true;
    }
    bool Array(Address address, GameArray& output, uint32_t maximum, uint32_t stride) noexcept;
    bool String(Address address, char16_t* output, uint32_t capacity, uint32_t& length) noexcept;
    bool Name(GameName name, char16_t* output, uint32_t capacity, uint32_t& length) noexcept;
    void ClearNames() noexcept { for (auto& entry : *names_) entry.tag = 0; }
    bool ObjectName(Address object, std::u16string_view expected) noexcept;
    bool Object(Address object, Address& type) noexcept;
    bool IsA(Address object, std::u16string_view className) noexcept;
    bool LiveActor(Address actor) noexcept;
    bool Registered(Address object, int32_t index) noexcept;
    bool ObjectsQuiescent() noexcept;
    // Exact DisAttributes map lookup, including sparse allocation validation.
    // Dirty values are evaluated from live modifiers without writing the cache.
    bool CachedAttribute(Address map, GameName name, float& value) noexcept;
    Address Image() const noexcept { return image_; }
    bool Failed() const noexcept { return failed_; }
    // Deliberately copy these 12 bytes: a diagnostic survives the next Begin.
    // cppcheck-suppress returnByReference
    FailureInfo LastFailure() const noexcept { return failure_; }
    uint32_t BytesUsed() const noexcept { return initialBytes_ - bytesLeft_; }
    uint32_t CallsUsed() const noexcept { return initialCalls_ - callsLeft_; }
    uint32_t CacheHits() const noexcept { return cacheHits_; }
    uint32_t TypeCacheHits() const noexcept { return typeCacheHits_; }
    static bool Span(Address address, uint64_t size) noexcept {
        return address >= 0x10000 && size <= UINT32_MAX && size <= uint64_t(UINT32_MAX) + 1 - address;
    }
private:
    bool Reject(Failure reason, Address address, uint64_t bytes) noexcept {
        if (!failed_) failure_ = {reason, address, uint32_t(bytes > UINT32_MAX ? UINT32_MAX : bytes)};
        failed_ = true; return false;
    }
    bool ModifiedAttribute(Address attribute, float& value) noexcept;
    Address image_{};
    Reader reader_{};
    void* context_{};
    uint32_t bytesLeft_{}, callsLeft_{};
    uint32_t initialBytes_{}, initialCalls_{};
    FailureInfo failure_{};
    bool failed_{};
    GameArray registry_{};
    bool registryReady_{};
    // Exact successful reads only; never read beyond the requested object.
    // This cache lives for one read-only sampling pass, and is disabled for
    // the post-sample validation and native commands that can mutate state.
    struct CachedRead { Address address{}; uint32_t size{}, generation{}; std::array<uint8_t, 64> bytes{}; };
    std::array<CachedRead, 512> reads_{};
    uint32_t readGeneration_{}, cacheHits_{};
    bool cacheReads_{};
    // Class ancestry is reused only inside the same read-only sample. Every
    // lookup still checks the instance's current header/registry membership.
    struct CachedType {
        Address type{}; uint32_t generation{}, length{}; bool matches{};
        std::array<char16_t, 96> name{};
    };
    std::array<CachedType, 256> types_{};
    uint32_t typeCacheHits_{};
    struct CachedName { uint32_t tag{}, length{}; std::array<char16_t, 96> text{}; };
    // Reflection may walk up to 4096 properties. A 512-name direct-mapped
    // cache evicted the beginning of a large class walk before it completed,
    // forcing every retry to decode the same names one byte at a time. Keep
    // the bounded name working set on the heap, outside the game-thread stack.
    std::unique_ptr<std::array<CachedName, 4096>> names_{new std::array<CachedName, 4096>{}};
};

bool ReadProcessBytes(Address address, void* output, std::size_t size, void*) noexcept;

// This layout is traced in native-identities/reflection-access and the health
// registration assertions at 00636198..00636295 for this exact executable.
namespace layout {
constexpr Address ObjectIndex = 0x4, ObjectFlagsHigh = 0xc, ObjectName = 0x28, ObjectClass = 0x30;
constexpr Address FieldNext = 0x38, StructSuper = 0x44, StructChildren = 0x48, StructSize = 0x4c;
constexpr Address PropertyOffset = 0x5c, ClassCastFlags = 0xd0;
constexpr uint32_t PropertyCastFlag = 0x8000, PendingKillFlag = 0x20000000;
constexpr Address Names = 0x01029664, NameCount = 0x01029668;
constexpr Address Objects = 0x01017620;
constexpr Address GarbageCollecting = 0x01017390, Serializing = 0x01006980;
}

struct Field {
    Address owner{}, property{}, type{}, offset{};
    uint32_t ownerSize{};
    bool Contains(uint32_t bytes) const noexcept {
        return property && offset < ownerSize && bytes <= ownerSize - offset;
    }
};

// Cache only schema, never live collection storage or element pointers.
// Clear at lifecycle boundaries. Negative lookups are not cached because
// classes may finish loading after an earlier incomplete attempt.
class Reflection {
public:
    explicit Reflection(Memory& memory) noexcept : memory_(memory) {}
    void Clear() noexcept { count_ = 0; memory_.ClearNames(); }
    bool Resolve(Address type, std::u16string_view name, std::u16string_view kind, Field& out) noexcept;
    template<class T> bool Read(Address object, const Field& field, T& output) noexcept {
        Address actualType{};
        return field.Contains(sizeof(T)) && Memory::Span(object, field.ownerSize) &&
            memory_.Object(object, actualType) && actualType == field.owner &&
            memory_.Read(object + field.offset, output);
    }
private:
    struct Cached {
        Field field{};
        std::array<char16_t, 96> name{}, kind{};
        uint32_t nameLength{}, kindLength{};
    };
    Memory& memory_;
    std::array<Cached, 192> cache_{};
    uint32_t count_{};
};

} // namespace dishonored
