#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace c3::mission_research {
// Exact-build, value-only research decoders. These do not read game memory,
// install hooks, establish ownership, localize strings, or authorize a Snapshot.
// See MAP-OBJECTIVES-CONTRACT.md for native evidence and remaining live gates.
constexpr size_t RecordBytes = 0x28;
enum class NativeStatus : uint32_t { Deactivated = 0, Complete = 1, Failed = 2, Active = 3 };
struct Record {
    uint32_t titleKeyAddress{}, descriptionKeyAddress{}, identityAddress{};
    uint32_t trackedEntityNameAddress{}, cachedEntityId{};
    NativeStatus status{};
    bool silent{}, secondary{};
};
inline uint32_t Word(const unsigned char* bytes) {
    return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
        (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}
inline bool DecodeRecord(const void* copied, size_t bytes, Record& result) {
    result = {};
    if (!copied || bytes != RecordBytes) return false;
    const auto* p = static_cast<const unsigned char*>(copied);
    const auto status = Word(p + 0x1c);
    if (status > 3 || p[0x24] > 1 || p[0x25] > 1) return false;
    // Addresses remain diagnostic addresses, never stable UI handles.
    result = { Word(p+4), Word(p+8), Word(p+0xc), Word(p+0x10), Word(p+0x14),
        static_cast<NativeStatus>(status), p[0x24] != 0, p[0x25] != 0 };
    return true;
}
inline bool MovieStatus(NativeStatus status, int32_t& result) {
    result = -1;
    switch (status) {
    case NativeStatus::Active: result = 0; return true;
    case NativeStatus::Complete: result = 1; return true;
    case NativeStatus::Failed: result = 2; return true;
    default: return false; // Native menu omits deactivated records.
    }
}
struct ChangedEvent { uint32_t recordAddress{}; bool rawFlag{}; };
// Copy of pool record header/count + two eight-byte entries; event ID 154
// must have been verified separately by the caller before decoding this payload.
inline bool DecodeChangedPayload(const void* copied, size_t bytes, ChangedEvent& result) {
    result = {};
    if (!copied || bytes != 24) return false;
    const auto* p = static_cast<const unsigned char*>(copied);
    if (Word(p+4) != 2 || p[12] != 4 || p[20] != 3 || p[16] > 1) return false;
    const auto address = Word(p+8);
    if (address < 0x10000 || address > UINT32_MAX - RecordBytes) return false;
    // Types and bool are bytes; producer leaves upper bytes unspecified.
    result = {address, p[16] != 0};
    return true;
}
struct Bounds { float startX{}, startY{}, endX{}, endY{}; };
struct Point { float x{}, y{}; };
struct LevelIdentity { uint32_t nameAddress{}, pathAddress{}; };
inline bool DecodeLevelHeader(const void* copied, size_t bytes, LevelIdentity& result) {
    result = {};
    if (!copied || bytes != 16) return false;
    const auto* p = static_cast<const unsigned char*>(copied);
    if (Word(p) != 0x398862c4 || Word(p+4) != 0x398862d0 || Word(p+8) < 0x10000 || Word(p+12) < 0x10000) return false;
    result = {Word(p+8), Word(p+12)};
    return true; // Current-level identity may exist during loading, not just gameplay.
}
struct MapMetadata { Bounds bounds{}; float scale{}, rotation{}; bool enabled{}; };
inline float FloatWord(const unsigned char* p) {
    const auto word = Word(p);
    float result{};
    static_assert(sizeof(result) == sizeof(word));
    std::memcpy(&result, &word, sizeof(result));
    return result;
}
struct Point3 { float x{}, y{}, z{}; };
struct EntityPose { uint32_t entityId{}; Point3 position{}, cachedForward{}; bool forwardKnown{}; };
// Exactly 0x88 copied bytes from the actual IEntity (actor +0x0c), NOT the
// game-object/extension owner at actor +4. Caller still owns epoch/phase checks.
inline bool DecodeEntityPose(const void* copied, size_t bytes, uint32_t expectedEntityId, EntityPose& result) {
    result = {};
    if (!copied || bytes != 0x88 || !expectedEntityId) return false;
    const auto* p = static_cast<const unsigned char*>(copied);
    if (Word(p) != 0x398eccc0 || Word(p+12) != expectedEntityId) return false;
    const Point3 position{FloatWord(p+0x58), FloatWord(p+0x68), FloatWord(p+0x78)};
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) return false;
    result.entityId = expectedEntityId;
    result.position = position;
    // Native GetWorldDir mutates this cache when dirty. Never invoke it on a
    // worker thread or display its old cached vector while the dirty bit is set.
    if ((p[8] & 2) == 0) {
        const Point3 forward{FloatWord(p+0x7c), FloatWord(p+0x80), FloatWord(p+0x84)};
        if (std::isfinite(forward.x) && std::isfinite(forward.y) && std::isfinite(forward.z) &&
            (forward.x != 0 || forward.y != 0 || forward.z != 0)) {
            result.cachedForward = forward;
            result.forwardKnown = true;
        }
    }
    return true; // Entity/body direction, not independently verified camera aim.
}
// Exactly 48 copied bytes starting at common-map-component +0x278.
inline bool DecodeMapMetadata(const void* copied, size_t bytes, MapMetadata& result) {
    result = {};
    if (!copied || bytes != 48) return false;
    const auto* p = static_cast<const unsigned char*>(copied);
    if (p[0x2c] > 1) return false;
    const MapMetadata next{{FloatWord(p+4), FloatWord(p+8), FloatWord(p+12), FloatWord(p+16)},
        FloatWord(p+20), FloatWord(p+28), p[44] != 0};
    if (!std::isfinite(next.bounds.startX) || !std::isfinite(next.bounds.startY) ||
        !std::isfinite(next.bounds.endX) || !std::isfinite(next.bounds.endY) ||
        !std::isfinite(next.scale) || !std::isfinite(next.rotation)) return false;
    const float spanX = next.bounds.endX - next.bounds.startX;
    const float spanY = next.bounds.endY - next.bounds.startY;
    if (next.enabled && (!std::isfinite(spanX) || !std::isfinite(spanY) ||
        spanX <= 0 || spanY <= 0 || next.scale <= 0)) return false;
    result = next;
    return true; // Does not prove active level, map owner, discovery or artwork.
}
inline bool Project(const Bounds& bounds, float worldX, float worldY, Point& result) {
    result = {};
    if (!std::isfinite(bounds.startX) || !std::isfinite(bounds.startY) ||
        !std::isfinite(bounds.endX) || !std::isfinite(bounds.endY) ||
        !std::isfinite(worldX) || !std::isfinite(worldY)) return false;
    const float dx = bounds.endX - bounds.startX, dy = bounds.endY - bounds.startY;
    if (!std::isfinite(dx) || !std::isfinite(dy) || dx <= 0 || dy <= 0) return false;
    const Point next{(worldX-bounds.startX)/dx, 1.0f-(worldY-bounds.startY)/dy};
    if (!std::isfinite(next.x) || !std::isfinite(next.y)) return false;
    result = next; // Do not clamp out-of-map positions into misleading markers.
    return true;
}
inline bool RotateMoviePoint(Point normalized, float radians, Point& result) {
    result = {};
    if (!std::isfinite(normalized.x) || !std::isfinite(normalized.y) || !std::isfinite(radians)) return false;
    const float x = normalized.x - 0.5f, y = normalized.y - 0.5f;
    const float cosine = std::cos(radians), sine = std::sin(radians);
    const Point next{x*cosine-y*sine+0.5f, x*sine+y*cosine+0.5f};
    if (!std::isfinite(next.x) || !std::isfinite(next.y)) return false;
    result = next;
    return true;
}
} // namespace c3::mission_research
