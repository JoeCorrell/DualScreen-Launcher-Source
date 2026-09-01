#include "Game.h"

#include <windows.h>

#include <cstring>

#include "Engine.h"
#include "FieldCatalog.h"
#include "Log.h"
#include "Mem.h"
#include "Rtti.h"

namespace dis {
namespace {

Game g_game;

// Used only when an unfamiliar engine does not export ILevel::IsLoading. The
// UI-manager gate remains mandatory, and this short additional quiet period
// avoids probing on the exact publication frame without restoring the old
// frame-rate-dependent ten-second delay.
constexpr int kFallbackSettleFrames = 120;

// IsLoading changing to false is the authoritative edge. A short wall-clock
// stability window keeps discovery out of the same instant that the engine is
// publishing its UI/actor containers without making a 30-fps device wait twice
// as long as a 60-fps one.
constexpr ULONGLONG kMinimumLevelAgeMs = 250;
constexpr ULONGLONG kReadyStableMs = 150;
constexpr ULONGLONG kActorRetryMs = 250;

// A typical level reports roughly 500-600 actors. Ninety-six per frame binds
// the player in a handful of frames while retaining a strict upper bound and
// keeping a bad entry localised in the diagnostics.
constexpr int kActorsPerFrame = 96;

// `?GetObjectByID@ILevel@@QEBAPEAVIGSObject@@H@Z`
//     IGSObject* ILevel::GetObjectByID(int) const
// QEBA: non-virtual public const, so the export address is callable directly.
constexpr const char kLevelGetObjectById[] =
    "?GetObjectByID@ILevel@@QEBAPEAVIGSObject@@H@Z";

// `?FindObjectsInRadius@ILevel@@QEAA_NPEAV?$vector@PEAVIControlObject@@@ttl@@
//   AEBVvec3@@MPEBVCRTTI@@_NPEAH@Z`
//
//   bool ILevel::FindObjectsInRadius(ttl::vector<IControlObject*>* out,
//                                    const vec3& centre, float radius,
//                                    const CRTTI* filter, bool, int*)
//
// QEAA -- non-virtual -- and it takes an ILevel*, which is the pointer this mod
// already holds. That matters: the captured pointer was never "wrong", it was
// only wrong for CRTTIObject calls, because CLevel inherits both and the two
// sub-objects sit 16 bytes apart. Handed to an ILevel method it is exactly
// right.
//
// This is the search the id sweep should have been from the start. The sweep
// asked for ids and found one object in 65,536 tries; this asks the level for
// every IControlObject in a radius, which is what actors actually are.
constexpr const char kLevelFindInRadius[] =
    "?FindObjectsInRadius@ILevel@@QEAA_NPEAV?$vector@PEAVIControlObject@@@ttl@@"
    "AEBVvec3@@MPEBVCRTTI@@_NPEAH@Z";

using FnGetObjectById = IGSObject* (*)(const ILevel*, int);
using FnFindInRadius = bool (*)(ILevel*, void*, const float*, float,
                                const void*, bool, int*);
using FnNativeRealloc = void* (__cdecl*)(void*, size_t);

void* g_get_object_by_id = nullptr;
void* g_find_in_radius = nullptr;
FnNativeRealloc g_native_realloc = nullptr;
bool g_resolved = false;

FnNativeRealloc FindImportedRealloc(HMODULE module) {
  if (!module) return nullptr;
  auto* image = reinterpret_cast<uint8_t*>(module);
  IMAGE_DOS_HEADER dos = {};
  if (!Read(image, &dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
      dos.e_lfanew <= 0) return nullptr;
  IMAGE_NT_HEADERS64 nt = {};
  if (!Read(image + dos.e_lfanew, &nt) ||
      nt.Signature != IMAGE_NT_SIGNATURE) return nullptr;
  const auto& directory =
      nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!directory.VirtualAddress || !directory.Size) return nullptr;
  auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
      image + directory.VirtualAddress);
  const size_t maximum = directory.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);
  for (size_t module_index = 0; module_index < maximum; ++module_index,
              ++descriptor) {
    IMAGE_IMPORT_DESCRIPTOR current = {};
    if (!Read(descriptor, &current) || !current.Name) break;
    if (!current.OriginalFirstThunk || !current.FirstThunk) continue;
    auto* names = reinterpret_cast<IMAGE_THUNK_DATA64*>(
        image + current.OriginalFirstThunk);
    auto* functions = reinterpret_cast<IMAGE_THUNK_DATA64*>(
        image + current.FirstThunk);
    for (size_t import_index = 0; import_index < 65536;
         ++import_index, ++names, ++functions) {
      IMAGE_THUNK_DATA64 name_thunk = {};
      IMAGE_THUNK_DATA64 function_thunk = {};
      if (!Read(names, &name_thunk) || !Read(functions, &function_thunk) ||
          !name_thunk.u1.AddressOfData) break;
      if (IMAGE_SNAP_BY_ORDINAL64(name_thunk.u1.Ordinal)) continue;
      auto* by_name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
          image + name_thunk.u1.AddressOfData);
      const char* imported = SafeCStr(by_name->Name, 128);
      if (imported && std::strcmp(imported, "realloc") == 0 &&
          function_thunk.u1.Function) {
        return reinterpret_cast<FnNativeRealloc>(function_thunk.u1.Function);
      }
    }
  }
  return nullptr;
}

void ReleaseNativePointerVector(void* container, size_t container_bytes) {
  if (!container || !g_native_realloc) return;
  const int data_offset = TheRtti().vector_layout().data_offset;
  if (data_offset < 0 ||
      static_cast<size_t>(data_offset) + sizeof(void*) > container_bytes) {
    return;
  }
  void* data = nullptr;
  auto* storage = static_cast<uint8_t*>(container) + data_offset;
  if (Read(storage, &data) && data) {
    g_native_realloc(data, 0);
  }
  std::memset(container, 0, container_bytes);
}

void ResolveOnce() {
  if (g_resolved) return;
  g_resolved = true;
  HMODULE engine = GetModuleHandleW(L"engine_x64_rwdi.dll");
  if (!engine) return;
  g_get_object_by_id =
      reinterpret_cast<void*>(GetProcAddress(engine, kLevelGetObjectById));
  if (!g_get_object_by_id) {
    DIS_ERR("[game] MISSING ILevel::GetObjectByID");
  }
  g_find_in_radius =
      reinterpret_cast<void*>(GetProcAddress(engine, kLevelFindInRadius));
  if (!g_find_in_radius) {
    DIS_ERR("[game] MISSING ILevel::FindObjectsInRadius -- falling back to the"
            " id sweep, which is much weaker");
  }
  g_native_realloc = FindImportedRealloc(engine);
  if (!g_native_realloc) {
    // FindObjectsInRadius allocates its output through the engine module's own
    // imported realloc. Without that exact allocator there is no safe way to
    // release the returned ttl::vector, so do not issue a leaking query.
    DIS_ERR("[game] engine realloc import was not found; disabling the typed"
            " actor query and using the id sweep");
    g_find_in_radius = nullptr;
  }
}

}  // namespace

// PlayerDI is the spelling that appears in gamedll's string table. The rest are
// the shapes the same class could plausibly take; none of them are load-bearing
// because the structural test below finds the player without any of them.
const char* const kPlayerClassNames[] = {
    "PlayerDI", "CPlayerDI", "Player", "CPlayer", "LocalPlayer", "PlayerDI_MP",
};
const int kPlayerClassNameCount =
    static_cast<int>(sizeof(kPlayerClassNames) / sizeof(kPlayerClassNames[0]));

Game& TheGame() { return g_game; }

void Game::SetLevel(ILevel* level) {
  if (level == level_) return;
  level_ = level;
  player_ = nullptr;
  player_control_ = nullptr;
  player_vtable_ = nullptr;
  player_class_.clear();
  searching_ = level != nullptr;
  exhausted_ = false;
  next_id_ = 0;
  dumped_level_ = false;
  settle_ = kFallbackSettleFrames;
  level_seen_ms_ = GetTickCount64();
  ready_since_ms_ = 0;
  retry_after_ms_ = 0;
  enumerated_ = false;
  searched_actors_ = false;
  actors_.clear();
  actor_index_ = 0;
  found_.clear();
  seen_.clear();
  DIS_INFO("[game] level = %p; player search armed (%s)", level,
           TheEngine().HasLoadingState()
               ? "waiting for engine load completion"
               : "using conservative fallback delay");
}

void Game::Reset(const char* reason) {
  if (!level_ && !player_) return;
  DIS_INFO("[game] reset (%s)", reason ? reason : "unspecified");
  level_ = nullptr;
  player_ = nullptr;
  player_control_ = nullptr;
  player_vtable_ = nullptr;
  player_class_.clear();
  searching_ = false;
  exhausted_ = false;
  next_id_ = 0;
  dumped_level_ = false;
  settle_ = 0;
  level_seen_ms_ = 0;
  ready_since_ms_ = 0;
  retry_after_ms_ = 0;
  enumerated_ = false;
  searched_actors_ = false;
  actors_.clear();
  actor_index_ = 0;
  found_.clear();
  seen_.clear();
}

void Game::NoteClass(const std::string& name) {
  if (name.empty()) return;
  for (ClassCount& c : seen_) {
    if (c.name == name) { ++c.count; return; }
  }
  // Bounded: a level holds thousands of objects but only tens of classes, and
  // an unbounded list here would be a slow leak on a long session.
  if (seen_.size() < 256) seen_.push_back(ClassCount{name, 1});
}

// Identity by capability, not by name.
//
// Matching a class name is the fast path and it is also the brittle one -- it
// depends on this file having guessed what Techland called the class. The test
// that actually decides is structural: the local player is the object that has
// a health field *and* at least one field that only a player has. Zombies have
// health. Nothing but the player has Fury, experience or skill points.
//
// That means the search still works on a build where every name in
// kPlayerClassNames is wrong, and it is why those names are allowed to be
// guesses.
bool Game::LooksLikePlayer(CRTTIObject* object, const std::string& class_name) const {
  const Rtti& rtti = TheRtti();
  if (!object) return false;

  for (int i = 0; i < kPlayerClassNameCount; ++i) {
    if (class_name == kPlayerClassNames[i]) return true;
  }

  bool has_health = false;
  for (int i = 0; i < static_cast<int>(sizeof(catalog::kHealth) /
                                       sizeof(catalog::kHealth[0])); ++i) {
    if (rtti.HasField(object, catalog::kHealth[i])) { has_health = true; break; }
  }
  if (!has_health) return false;

  static const char* const kPlayerOnly[] = {
      "FuryPoints", "FuryLevel", "Fury", "ExpToNextLevel", "Experience",
      "SkillPoints", "AllSkillsList",
  };
  for (const char* name : kPlayerOnly) {
    if (rtti.HasField(object, name)) return true;
  }
  return false;
}

bool Game::Tick() {
  ResolveOnce();
  if (!level_ || !g_get_object_by_id || !TheRtti().ready()) return false;

  const ULONGLONG now = GetTickCount64();
  Engine& engine = TheEngine();

  // This is checked even after binding. A same-ILevel transition can begin
  // before Deactivate publishes retirement; stop reading the old player the
  // moment the engine itself says that level is loading again.
  if (engine.HasLoadingState() && engine.IsLoading(level_)) {
    if (player_ || !actors_.empty() || searched_actors_ || next_id_ != 0) {
      DIS_INFO("[game] level entered loading state; dropping live player data");
      player_ = nullptr;
      player_control_ = nullptr;
      player_vtable_ = nullptr;
      player_class_.clear();
      searching_ = true;
      exhausted_ = false;
      next_id_ = 0;
      searched_actors_ = false;
      actors_.clear();
      actor_index_ = 0;
      found_.clear();
      seen_.clear();
      retry_after_ms_ = 0;
    }
    ready_since_ms_ = 0;
    return false;
  }

  // Already have one: revalidate periodically rather than every frame. A freed
  // object usually fails the vtable read immediately, but a recycled
  // allocation can read cleanly while being something else entirely, so the
  // class name is rechecked too.
  if (player_) {
    // A single pointer read catches destruction/vtable replacement immediately
    // without reparsing the whole MSVC RTTI hierarchy every frame.
    void* live_vtable = nullptr;
    const bool identity_ok = Read(player_, &live_vtable) &&
                             live_vtable == player_vtable_;
    if (identity_ok && --revalidate_countdown_ > 0) return true;
    revalidate_countdown_ = 120;
    // Periodically re-derive the reflection base from MSVC RTTI before making a virtual
    // call. A merely readable stale allocation is not proof that the old
    // CRTTIObject interface is still valid.
    CRTTIObject* current = identity_ok ? TheRtti().AsRttiObject(player_) : nullptr;
    if (current == player_) {
      const std::string current_class = TheRtti().ClassName(current);
      if (current_class == player_class_) return true;
      DIS_INFO("[game] player pointer changed class (%s -> %s); re-searching",
               player_class_.c_str(), current_class.c_str());
    } else {
      DIS_INFO("[game] player pointer went stale; re-searching");
    }
    player_ = nullptr;
    player_control_ = nullptr;
    player_vtable_ = nullptr;
    player_class_.clear();
    searching_ = true;
    exhausted_ = false;
    next_id_ = 0;
    searched_actors_ = false;
    actors_.clear();
    actor_index_ = 0;
    retry_after_ms_ = now + kActorRetryMs;
    found_.clear();
    seen_.clear();
  }

  // The shipped engine tells us exactly when its main level is no longer
  // loading. Pair that with the existing safe UI-manager predicate and require
  // a short stable edge before issuing the first actor query. Unknown builds
  // retain the old conservative wait instead of assuming readiness.
  if (engine.HasLoadingState()) {
    if (!engine.IsUIManagerVisible(level_)) {
      ready_since_ms_ = 0;
      return false;
    }
    if (ready_since_ms_ == 0) {
      ready_since_ms_ = now;
      return false;
    }
    if (now - level_seen_ms_ < kMinimumLevelAgeMs ||
        now - ready_since_ms_ < kReadyStableMs) {
      return false;
    }
    settle_ = 0;
  } else {
    if (settle_ > 0) {
      --settle_;
      return false;
    }
    if (!engine.IsUIManagerVisible(level_)) return false;
  }

  const Rtti& rtti = TheRtti();

  if (actors_.empty() && now < retry_after_ms_) return false;

  // Ask the level for its actors.
  //
  // Runs once, after settling, and before the id sweep is given any weight.
  // A radius of 1e9 around the origin is "everything": the call filters by
  // class rather than by distance, and IControlObject is the base every actor
  // in the level derives from, so this returns the population the player is
  // in without needing to know where the player is first.
  if (!searched_actors_ && find_actors_ && g_find_in_radius &&
      rtti.vector_layout().known()) {
    searched_actors_ = true;

    alignas(16) uint8_t container[64] = {};
    const float centre[3] = {0.0f, 0.0f, 0.0f};
    const void* filter =
        rtti.NativeClass("?GetNativeClass@IControlObject@@SAPEBVCRTTI@@XZ");
    int extra = 0;

    DIS_INFO("[game] asking the level for IControlObjects (filter %p)", filter);
    const bool query_ok = reinterpret_cast<FnFindInRadius>(g_find_in_radius)(
        level_, container, centre, 1.0e9f, filter, false, &extra);

    const bool read_ok = query_ok &&
        rtti.ReadVector(container, &actors_, "FindObjectsInRadius");
    ReleaseNativePointerVector(container, sizeof(container));

    if (read_ok) {
      actor_index_ = 0;
      DIS_INFO("[game] walking %d actors, %d per frame",
               static_cast<int>(actors_.size()), kActorsPerFrame);
    } else if (!query_ok) {
      DIS_INFO("[game] actor query is not ready yet");
    }
    if (actors_.empty()) {
      // IsLoading is already false, so a temporarily empty actor container only
      // needs a short bounded retry rather than another ten-second load delay.
      searched_actors_ = false;
      retry_after_ms_ = now + kActorRetryMs;
      DIS_INFO("[game] no actors available yet; retrying in %llu ms",
               static_cast<unsigned long long>(kActorRetryMs));
      return false;
    }
  }

  // Walk the actors a slice at a time.
  //
  // Sliced for two reasons. It keeps a 577-object walk out of a single Present
  // call, and it means that if one entry does take the process down, the log
  // says which index it reached instead of stopping at a header. The previous
  // version walked all 577 in one go and died with nothing to show for it.
  if (!actors_.empty() && actor_index_ < actors_.size()) {
    const size_t end = (actor_index_ + kActorsPerFrame < actors_.size())
                           ? actor_index_ + kActorsPerFrame : actors_.size();
    for (; actor_index_ < end; ++actor_index_) {
      // The critical conversion. These pointers are IControlObject*, and the
      // CRTTIObject base is somewhere else inside the same object.
      CRTTIObject* object = rtti.AsRttiObject(actors_[actor_index_]);
      if (!object) continue;
      const std::string cls = rtti.ClassName(object);
      if (cls.empty()) continue;
      NoteClass(cls);
      if (LooksLikePlayer(object, cls)) {
        player_ = object;
        player_control_ = static_cast<IControlObject*>(actors_[actor_index_]);
        Read(player_, &player_vtable_);
        player_class_ = cls;
        searching_ = false;
        revalidate_countdown_ = 120;
        DIS_INFO("[game] player found: class '%s' at %p (actor %d)",
                 cls.c_str(), object, static_cast<int>(actor_index_));
        actors_.clear();
        return true;
      }
    }
    if (actor_index_ >= actors_.size()) {
      DIS_INFO("[game] walked %d actors, %d distinct classes:",
               static_cast<int>(actors_.size()), static_cast<int>(seen_.size()));
      for (const ClassCount& c : seen_) {
        DIS_INFO("[game]   %-40s x%d", c.name.c_str(), c.count);
      }
      DIS_ERR("[game] no actor matched the player test");
      actors_.clear();
      actor_index_ = 0;
      searched_actors_ = false;
      retry_after_ms_ = now + kActorRetryMs;
    }
    return false;
  }

  if (!searching_ || exhausted_) return false;

  auto get_object = reinterpret_cast<FnGetObjectById>(g_get_object_by_id);

  const int end = (next_id_ + budget_ < max_id_) ? next_id_ + budget_ : max_id_;
  for (int id = next_id_; id < end; ++id) {
    IGSObject* interface_pointer = get_object(level_, id);
    if (!interface_pointer) continue;
    CRTTIObject* object = rtti.AsRttiObject(interface_pointer);
    if (!object) continue;

    const std::string class_name = rtti.ClassName(object);
    if (class_name.empty()) continue;
    NoteClass(class_name);
    if (found_.size() < 8) found_.push_back(object);

    if (LooksLikePlayer(object, class_name)) {
      player_ = object;
      // GetObjectByID returns IGSObject, not IControlObject. It is valid for
      // reflection after AsRttiObject, but cannot safely be used for world
      // transforms; the map remains unavailable on this fallback path.
      player_control_ = nullptr;
      Read(player_, &player_vtable_);
      player_class_ = class_name;
      searching_ = false;
      revalidate_countdown_ = 120;
      DIS_INFO("[game] player found: id %d, class '%s', object %p",
               id, class_name.c_str(), object);
      return true;
    }
  }
  next_id_ = end;

  if (next_id_ >= max_id_) {
    exhausted_ = true;
    searching_ = false;
    DIS_ERR("[game] player not found in ids 0..%d; %d distinct classes seen",
            max_id_, static_cast<int>(seen_.size()));
    for (const ClassCount& c : seen_) {
      DIS_INFO("[game]   class %-40s x%d", c.name.c_str(), c.count);
    }
    // The id sweep came up empty, so say what the level itself holds. On the
    // first device run this was the whole story: one object in 65,536 ids, and
    // it was the level. Whatever owns the player hangs off it.
    DumpLevel();
  }
  return false;
}

void Game::DumpLevel() {
  if (dumped_level_ || !TheRtti().ready()) return;
  dumped_level_ = true;

  const Rtti& rtti = TheRtti();
  if (found_.empty()) {
    DIS_ERR("[game] the id sweep returned no usable object to inspect");
    return;
  }

  // Every object the sweep found, swept for the names that would reach a
  // player. Deliberately not the captured ILevel*: that pointer is the wrong
  // sub-object for reflection and reading through it is what crashed the game
  // on a save load. Rtti::ClassName now rejects it outright.
  for (CRTTIObject* object : found_) {
    const std::string cls = rtti.ClassName(object);
    if (cls.empty()) continue;
    DIS_INFO("[game] inspecting '%s' at %p", cls.c_str(), object);

    // Real enumeration first: every field the class declares, whatever it is
    // called. The name dictionary is only the fallback now, and on the last
    // device run it matched nothing on LevelDI -- which is exactly the failure
    // mode a dictionary has and enumeration does not.
    std::vector<Rtti::Probe> hits = rtti.DumpAllFields(object);
    const bool enumerated = !hits.empty();
    if (!enumerated) {
      hits = rtti.ProbeFields(object, DIS_NAMES(catalog::kLevelSweep));
    }
    if (hits.empty()) {
      DIS_ERR("[game]   no fields found by enumeration or by name");
      continue;
    }
    DIS_INFO("[game]   %d fields (%s):", static_cast<int>(hits.size()),
             enumerated ? "enumerated" : "matched from the name list");
    for (const Rtti::Probe& hit : hits) {
      DIS_INFO("[game]     %-24s %-7s %s", hit.name.c_str(), hit.kind,
               hit.value.c_str());
    }
  }
}

CRTTIObject* Game::player() {
  // Tick owns periodic validation and level retirement clears the cache. Do
  // not reparse the complete MSVC class hierarchy on every rendered frame.
  return player_;
}

}  // namespace dis
