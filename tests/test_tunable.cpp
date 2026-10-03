// Tunable-capture specs: the watched-name contract (exact game strings),
// the capture registry (latest-wins, null-safe), and call-target
// clustering (shared binder ranks above per-name helpers).

#include "Test.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "TunableCapture.h"

namespace {

using namespace crdeadzone;

void Watchlist() {
  // Break caught: a mistyped name silently captures nothing in-game.
  EXPECT_TRUE(IsWatchedTunable("deadZone"));
  EXPECT_TRUE(IsWatchedTunable("slideDeadzone"));
  EXPECT_TRUE(IsWatchedTunable("stickInputPowerFactor"));
  EXPECT_TRUE(IsWatchedTunable("movementInputCurve"));
  EXPECT_TRUE(IsWatchedTunable("cameraInputCurve"));
  EXPECT_TRUE(IsWatchedTunable("moveInputRemapCurve"));
  // Break caught: relaxed (case-insensitive) matching capturing the wrong
  // option, or an empty name matching everything.
  EXPECT_TRUE(!IsWatchedTunable("Deadzone"));
  EXPECT_TRUE(!IsWatchedTunable("deadzone"));
  EXPECT_TRUE(!IsWatchedTunable(""));
  EXPECT_TRUE(!IsWatchedTunable(nullptr));
  EXPECT_TRUE(!IsWatchedTunable("movement_deadzone"));
}

void Registry() {
  TunableCapture cap;
  EXPECT_EQ(cap.Count(), size_t{0});
  EXPECT_TRUE(cap.Find("deadZone") == nullptr);

  const char fieldA = 0, fieldB = 0;
  cap.Record("deadZone", &fieldA);
  EXPECT_EQ(cap.Count(), size_t{1});
  EXPECT_TRUE(cap.Find("deadZone") == &fieldA);
  // Re-registration rebinds: latest wins, count unchanged. Break caught:
  // a stale (possibly dangling) field address surviving a rebind.
  cap.Record("deadZone", &fieldB);
  EXPECT_EQ(cap.Count(), size_t{1});
  EXPECT_TRUE(cap.Find("deadZone") == &fieldB);

  cap.Record("slideDeadzone", &fieldA);
  EXPECT_EQ(cap.Count(), size_t{2});
  EXPECT_TRUE(cap.Find("slideDeadzone") == &fieldA);
  // Unwatched names are not recorded: the registry mirrors the watchlist.
  cap.Record("bogus", &fieldA);
  EXPECT_EQ(cap.Count(), size_t{2});
  EXPECT_TRUE(cap.Find("bogus") == nullptr);
  // Defensive: nulls never crash, never record.
  cap.Record(nullptr, &fieldA);
  cap.Record("deadZone", nullptr);
  EXPECT_EQ(cap.Count(), size_t{2});
  EXPECT_TRUE(cap.Find(nullptr) == nullptr);
}

void Clustering() {
  // Hand-built windows: 0x1000 appears under all three names (the shared
  // binder), 0x2000 under two, 0x3000 under one (a per-name helper).
  const std::map<std::string, std::vector<uint64_t>> windows = {
      {"deadZone", {0x1000, 0x2000, 0x3000}},
      {"slideDeadzone", {0x1000, 0x2000}},
      {"stickInputPowerFactor", {0x1000}},
  };
  const auto ranked = ClusterCallTargets(windows);
  // Break caught: hooking a per-name helper instead of the shared binder.
  EXPECT_EQ(ranked.size(), size_t{3});
  EXPECT_EQ(ranked[0].rva, uint64_t{0x1000});
  EXPECT_EQ(ranked[0].windows, size_t{3});
  EXPECT_EQ(ranked[1].rva, uint64_t{0x2000});
  EXPECT_EQ(ranked[1].windows, size_t{2});
  EXPECT_EQ(ranked[2].rva, uint64_t{0x3000});
  EXPECT_EQ(ranked[2].windows, size_t{1});
  // The ranking carries which names each candidate covers (diagnostics).
  EXPECT_EQ(ranked[0].names.size(), size_t{3});
  EXPECT_TRUE(ranked[0].names[0] == "deadZone");
  // Duplicate targets inside one window count once (one call site each).
  const std::map<std::string, std::vector<uint64_t>> dup = {
      {"deadZone", {0x1000, 0x1000}},
  };
  const auto single = ClusterCallTargets(dup);
  EXPECT_EQ(single.size(), size_t{1});
  EXPECT_EQ(single[0].windows, size_t{1});
  // No windows: no candidates (caller must skip hooking, never guess).
  EXPECT_TRUE(ClusterCallTargets({}).empty());
}

void Collection() {
  // Hand-built fake module (offsets hand-derived, not computed by the
  // code under test). Break caught: wrong RVA math hooks the wrong
  // address in-game.
  //   string "deadZone\0" at 40;
  //   LEA rcx,[rip+disp] at 8, disp = 40 - (8 + 7) = 25;
  //   CALL rel32 at 15 targeting 50, rel = 50 - (15 + 5) = 30.
  std::vector<uint8_t> image(64, 0);
  const char* name = "deadZone";
  std::memcpy(image.data() + 40, name, std::strlen(name) + 1);
  image[8] = 0x48;
  image[9] = 0x8D;
  image[10] = 0x0D;
  const int32_t leaDisp = 25;
  std::memcpy(image.data() + 11, &leaDisp, 4);
  image[15] = 0xE8;
  const int32_t callRel = 30;
  std::memcpy(image.data() + 16, &callRel, 4);
  // A second call escaping the module must be dropped, never hooked.
  image[20] = 0xE8;
  const int32_t outsideRel = static_cast<int32_t>(200 - (20 + 5));
  std::memcpy(image.data() + 21, &outsideRel, 4);
  const ModuleView mod{image.data(), image.size(), image.data(), image.size()};
  const auto windows = CollectTunableWindows(mod);
  // Exactly one window (unwatched names are never searched), one target.
  EXPECT_EQ(windows.size(), size_t{1});
  const auto it = windows.find("deadZone");
  EXPECT_TRUE(it != windows.end());
  EXPECT_EQ(it->second.size(), size_t{1});
  EXPECT_EQ(it->second[0], uint64_t{50});
  // Empty view: no windows, no crash.
  EXPECT_TRUE(CollectTunableWindows(ModuleView{}).empty());
}

void HookSelection() {
  // Break caught: hooking arity-unknown per-name helpers (ranks 2-4) as if
  // they were the shared binder. Rank 0 always hooks (at most 1 when lone);
  // lower ranks only with multi-window evidence.
  const std::vector<RankedTarget> mixed = {
      {0x1000, 3, {"a", "b", "c"}},
      {0x2000, 2, {"a", "b"}},
      {0x3000, 1, {"a"}},
      {0x4000, 1, {"a"}},
  };
  const auto plan = SelectHookTargets(mixed, 4);
  EXPECT_EQ(plan.hook.size(), size_t{2});
  EXPECT_EQ(plan.hook[0], uint64_t{0x1000});
  EXPECT_EQ(plan.hook[1], uint64_t{0x2000});
  EXPECT_EQ(plan.skipped.size(), size_t{2});
  EXPECT_EQ(plan.skipped[0], uint64_t{0x3000});
  EXPECT_EQ(plan.skipped[1], uint64_t{0x4000});

  // Lone-helper result: hook rank 0 only, never the rest.
  const std::vector<RankedTarget> lone = {
      {0x1000, 1, {"a"}},
      {0x2000, 1, {"b"}},
  };
  const auto lonePlan = SelectHookTargets(lone, 4);
  EXPECT_EQ(lonePlan.hook.size(), size_t{1});
  EXPECT_EQ(lonePlan.hook[0], uint64_t{0x1000});
  EXPECT_EQ(lonePlan.skipped.size(), size_t{1});

  // Cap respected even when everything qualifies.
  const std::vector<RankedTarget> many = {
      {0x1000, 5, {}}, {0x2000, 4, {}}, {0x3000, 3, {}}, {0x4000, 2, {}}, {0x5000, 2, {}},
  };
  const auto capped = SelectHookTargets(many, 4);
  EXPECT_EQ(capped.hook.size(), size_t{4});
  EXPECT_EQ(capped.skipped.size(), size_t{1});
  EXPECT_EQ(capped.skipped[0], uint64_t{0x5000});

  // Empty ranking: hook nothing, skip nothing, caller stands down.
  const auto empty = SelectHookTargets({}, 4);
  EXPECT_TRUE(empty.hook.empty() && empty.skipped.empty());
}

}  // namespace

void TunableTests() {
  crtest::Suite("tunable-watchlist", Watchlist);
  crtest::Suite("tunable-registry", Registry);
  crtest::Suite("tunable-clustering", Clustering);
  crtest::Suite("tunable-collect", Collection);
  crtest::Suite("tunable-hookplan", HookSelection);
}
