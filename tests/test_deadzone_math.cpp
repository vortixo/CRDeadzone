// Portable logic tests: deadzone math + INI parsing helpers.
// Build (Linux/macOS/Windows): g++ -std=c++20 -Iinclude tests/test_deadzone_math.cpp
//   src/DeadzoneMath.cpp src/Config.cpp -o test_deadzone_math && ./test_deadzone_math

#include <cmath>
#include <cstdio>
#include <string>

#include "Config.h"
#include "DeadzoneMath.h"

namespace {

int g_fail = 0;

void Check(bool cond, const char* name) {
  if (!cond) {
    ++g_fail;
    std::printf("FAIL: %s\n", name);
  } else {
    std::printf("ok: %s\n", name);
  }
}

bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

void TestAxial() {
  using crdeadzone::ApplyAxialDeadzone;
  Check(ApplyAxialDeadzone(0.05f, 0.15f, 1.0f) == 0.0f, "axial below inner -> 0");
  Check(Near(ApplyAxialDeadzone(1.0f, 0.15f, 1.0f), 1.0f), "axial full deflection -> 1");
  Check(Near(ApplyAxialDeadzone(-1.0f, 0.15f, 1.0f), -1.0f), "axial negative full -> -1");
  Check(Near(ApplyAxialDeadzone(0.575f, 0.15f, 1.0f), 0.5f), "axial mid rescale");
  Check(ApplyAxialDeadzone(0.0f, 0.0f, 1.0f) == 0.0f, "axial zero deadzone keeps 0");
  Check(Near(ApplyAxialDeadzone(0.5f, 0.0f, 1.0f), 0.5f), "axial zero deadzone passthrough");
}

void TestRadial() {
  using crdeadzone::ApplyRadialDeadzone;
  float x = 0.05f, y = 0.0f;
  ApplyRadialDeadzone(x, y, 0.15f, 1.0f, 1.0f);
  Check(x == 0.0f && y == 0.0f, "radial inside deadzone -> 0");

  x = 1.0f;
  y = 0.0f;
  ApplyRadialDeadzone(x, y, 0.15f, 1.0f, 1.0f);
  Check(Near(x, 1.0f) && Near(y, 0.0f), "radial full deflection preserved");

  x = 0.6f;
  y = 0.8f;  // mag 1.0
  ApplyRadialDeadzone(x, y, 0.0f, 1.0f, 1.0f);
  Check(Near(x, 0.6f) && Near(y, 0.8f), "radial direction preserved linear");

  x = 0.3f;
  y = 0.4f;  // mag 0.5
  ApplyRadialDeadzone(x, y, 0.0f, 1.0f, 2.0f);
  Check(Near(x, 0.15f) && Near(y, 0.2f), "radial power curve on magnitude");

  x = 0.0f;
  y = 0.0f;
  ApplyRadialDeadzone(x, y, 0.15f, 1.0f, 1.0f);
  Check(x == 0.0f && y == 0.0f, "radial zero stays zero");
}

void TestTrigger() {
  using crdeadzone::ApplyTriggerDeadzone;
  Check(ApplyTriggerDeadzone(0.02f, 0.05f) == 0.0f, "trigger below dz -> 0");
  Check(Near(ApplyTriggerDeadzone(1.0f, 0.05f), 1.0f), "trigger full -> 1");
  Check(Near(ApplyTriggerDeadzone(0.525f, 0.05f), 0.5f), "trigger rescale");
  Check(ApplyTriggerDeadzone(0.5f, 0.0f) == 0.5f, "trigger zero dz passthrough");
}

void TestCurves() {
  using crdeadzone::CurvePowerForChoice;
  Check(CurvePowerForChoice(0, 100) == 1.0f, "curve linear");
  Check(CurvePowerForChoice(1, 100) > 1.0f, "curve mild > linear");
  Check(CurvePowerForChoice(2, 100) > CurvePowerForChoice(1, 100), "curve aggressive > mild");
  Check(Near(CurvePowerForChoice(3, 200), 2.0f), "curve custom slider");
  Check(Near(CurvePowerForChoice(3, 9999), 3.0f), "curve custom clamped high");
  Check(Near(CurvePowerForChoice(3, 0), 0.5f), "curve custom clamped low");
}

void TestConfigParse() {
  using crdeadzone::Config;
  Check(Config::ParseInt("15", 0) == 15, "parse int");
  Check(Config::ParseInt("  42  ", 0) == 42, "parse int trimmed");
  Check(Config::ParseInt("abc", 7) == 7, "parse int fallback");
  Check(Config::ParseInt("12x", 7) == 7, "parse int trailing junk fallback");
  Check(Config::ParseInt("", 7) == 7, "parse empty fallback");
  Check(Config::ClampInt(999, 0, 50) == 50, "clamp high");
  Check(Config::ClampInt(-5, 0, 50) == 0, "clamp low");
  Check(Config::ClampInt(25, 0, 50) == 25, "clamp passthrough");
}

}  // namespace

int main() {
  TestAxial();
  TestRadial();
  TestTrigger();
  TestCurves();
  TestConfigParse();
  if (g_fail == 0) {
    std::printf("ALL TESTS PASSED\n");
    return 0;
  }
  std::printf("%d TEST(S) FAILED\n", g_fail);
  return 1;
}
