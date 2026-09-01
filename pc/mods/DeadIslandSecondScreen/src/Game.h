// Finding the things the panel talks about.
//
// Two problems, and neither is solved by an export on its own.
//
// The level. The engine exports its entire UI object model but no global
// accessor for the ILevel that owns it, so the pointer has to be caught rather
// than looked up: gamedll imports `ILevel::GetIUIManager` from the engine, and
// replacing that one entry in gamedll's import table hands us `this` the first
// time the game asks for its UI manager. Nothing is patched -- it is a pointer
// swap in a table the loader itself filled in. Mod.cpp owns that hook and calls
// SetLevel here.
//
// The player. There is no `ILevel::GetLocalPlayer`; the engine does not know
// what a player is, because the engine is ChromeEngine and the player is a
// gamedll class. What the engine does export is `ILevel::GetObjectByID(int)`,
// a plain (this, int) call with no container types in the signature, and
// `CRTTIObject::GetClassNameA`, which will say what any object it returns
// actually is. So the player is found by asking the level for objects and
// reading their class names until one of them is the player.
//
// That scan is spread across frames on purpose. The previous mod in this folder
// turned a level load into a crash by walking four thousand ids three times
// over from inside the present hook while the UI manager did not yet exist.
// Here the sweep is bounded per frame, runs only once, and stops at the first
// match; and every subsequent use of the pointer is revalidated rather than
// trusted, because an object that was the player before a load is a freed
// allocation after one.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dis {

using ILevel = void;
using IGSObject = void;
using IControlObject = void;
using CRTTIObject = void;

// Class names the player object might report. Ordered: `PlayerDI` is the one
// that appears in gamedll's string table and is tried first. Discovery settles
// the rest -- when the sweep runs it logs every distinct class name it saw, so
// a build that calls it something else names itself in the log rather than
// silently never matching.
extern const char* const kPlayerClassNames[];
extern const int kPlayerClassNameCount;

class Game {
 public:
  // Called from the IAT hook the moment gamedll first asks for its UI manager.
  void SetLevel(ILevel* level);
  ILevel* level() const { return level_; }
  bool has_level() const { return level_ != nullptr; }

  // Drops everything derived from the level. Called on a load or a return to
  // the menu, when every object pointer held here becomes a stale allocation.
  void Reset(const char* reason);

  // One bounded slice of the player search. Call once per frame; answers true
  // once the player has been found. Cheap and idempotent after that.
  bool Tick();

  // The player, revalidated. Answers null when it has gone away, which is the
  // signal for a page to draw its unavailable state rather than the last value
  // it happened to see.
  CRTTIObject* player();
  // The original interface pointer returned by FindObjectsInRadius. World
  // position/orientation methods belong to IControlObject, not to the adjusted
  // CRTTIObject base used for reflection, so both are retained explicitly.
  IControlObject* player_control() const { return player_control_; }


  bool searching() const { return searching_; }
  int scanned() const { return next_id_; }
  const std::string& player_class() const { return player_class_; }

  // Every distinct class name the sweep encountered, with how many objects
  // reported it. This is the discovery output that names the player class on a
  // build where the guesses above all miss.
  struct ClassCount { std::string name; int count = 0; };
  const std::vector<ClassCount>& seen_classes() const { return seen_; }

  // Bounds. Deliberately generous -- object ids are not dense, and the cost of
  // a miss is one integer call into the engine.
  void set_scan_limit(int max_id) { max_id_ = max_id; }
  void set_scan_budget(int ids_per_frame) { budget_ = ids_per_frame; }
  // Opt in to CRTTIObject::GetAllObjects instead of relying on the id sweep.
  void set_use_enumeration(bool on) { use_enumeration_ = on; }
  // Kill switch for the actor walk. It had none, which is why stopping the
  // crash it caused needed the whole mod disabled.
  void set_find_actors(bool on) { find_actors_ = on; }

 private:
  bool LooksLikePlayer(CRTTIObject* object, const std::string& class_name) const;
  void NoteClass(const std::string& name);
  // Writes what the level object declares to the log, once. Called when the id
  // sweep fails, because that is when it is the only lead left.
  void DumpLevel();

  ILevel* level_ = nullptr;
  // Objects the id sweep actually returned, kept so the failure path has
  // something correctly typed to inspect.
  //
  // The captured `level_` is deliberately NOT among them: it is an ILevel*, and
  // CLevel inherits ILevel and CRTTIObject separately, so that pointer aims at
  // the wrong sub-object and the reflection API reads garbage through it. The
  // pointers here came from GetObjectByID, which returns IGSObject*, and those
  // read back a clean class name.
  std::vector<CRTTIObject*> found_;
  CRTTIObject* player_ = nullptr;
  IControlObject* player_control_ = nullptr;
  void* player_vtable_ = nullptr;
  std::string player_class_;

  bool searching_ = false;
  bool exhausted_ = false;
  int next_id_ = 0;
  int max_id_ = 65536;
  int budget_ = 512;
  int revalidate_countdown_ = 0;
  bool dumped_level_ = false;
  // `settle_` is a compatibility fallback only. Supported Definitive Edition
  // builds use ILevel::IsLoading and the wall-clock readiness fields below.
  int settle_ = 0;
  uint64_t level_seen_ms_ = 0;
  uint64_t ready_since_ms_ = 0;
  uint64_t retry_after_ms_ = 0;
  bool use_enumeration_ = false;
  bool enumerated_ = false;
  bool searched_actors_ = false;
  bool find_actors_ = true;
  std::vector<void*> actors_;
  size_t actor_index_ = 0;

  std::vector<ClassCount> seen_;
};

Game& TheGame();

}  // namespace dis
