// Tunable-capture specs: the watched-name contract (exact game strings),
// the capture registry (latest-wins, null-safe), and call-target
// clustering (shared binder ranks above per-name helpers).

#include "Test.h"

#include <cstddef>
#include <cstdint>
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

}  // namespace

void TunableTests() {
  crtest::Suite("tunable-watchlist", Watchlist);
  crtest::Suite("tunable-registry", Registry);
  crtest::Suite("tunable-clustering", Clustering);
}
