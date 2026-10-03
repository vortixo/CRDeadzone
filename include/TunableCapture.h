#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Read-only recon of the game's option binder: which code binds the
// deadzone/curve tunables and where the values land. Portable logic only
// (watchlist, capture registry, call-target clustering); the Windows hook
// itself lives in OptionsOverride.cpp.

namespace crdeadzone {

// Option names as spelled in the game binary (verified against
// CONTROLResonant.exe strings). Matching is exact and case-sensitive.
inline constexpr const char* kWatchedTunables[] = {
    "deadZone",
    "slideDeadzone",
    "stickInputPowerFactor",
    "movementInputCurve",
    "cameraInputCurve",
    "moveInputRemapCurve",
};
inline constexpr size_t kWatchedTunableCount = 6;

bool IsWatchedTunable(const char* name);

// Latest-wins registry of watched-name -> game-side field address.
// Re-registration rebinds (old addresses may dangle after a menu reopen).
class TunableCapture {
 public:
  void Record(const char* name, const void* field);
  const void* Find(const char* name) const;
  size_t Count() const;

 private:
  std::map<std::string, const void*> fields_;
};

struct RankedTarget {
  uint64_t rva = 0;
  size_t windows = 0;  // in how many tunable windows this target appears
  std::vector<std::string> names;
};

// Ranks call targets seen in per-tunable flow windows: the shared binder
// appears under every name, per-name helpers under one. Sorted by window
// count (desc), then address (asc, deterministic ties).
std::vector<RankedTarget> ClusterCallTargets(
    const std::map<std::string, std::vector<uint64_t>>& windows);

// A hook decision over ranked candidates: which RVAs to hook, which to
// log-but-skip. Rank 0 always hooks when the cap allows (at most 1 target
// when nothing is shared); lower ranks hook only with multi-window
// evidence, so arity-unknown per-name helpers stay untouched.
struct HookPlan {
  std::vector<uint64_t> hook;
  std::vector<uint64_t> skipped;
};

HookPlan SelectHookTargets(const std::vector<RankedTarget>& ranked, size_t maxHooks);

// A flat view of the game module for anchor scanning. RVAs in the results
// are offsets from base.
struct ModuleView {
  uint8_t* base = nullptr;
  size_t size = 0;
  uint8_t* text = nullptr;
  size_t textSize = 0;
};

// Collects, per watched tunable, the RVAs of call targets seen in the
// anchor flow windows (string ref -> LEA refs -> forward walk). Only
// watched names are searched; targets outside the module are dropped.
std::map<std::string, std::vector<uint64_t>> CollectTunableWindows(const ModuleView& mod);

}  // namespace crdeadzone
