// Portable logic tests: deadzone math + INI parsing + HID mapping helpers.
// Build (Linux/macOS/Windows): g++ -std=c++20 -Iinclude tests/test_deadzone_math.cpp
//   src/DeadzoneMath.cpp src/Config.cpp src/HidMapping.cpp src/Activity.cpp
//   -o test_deadzone_math && ./test_deadzone_math

#include <cmath>
#include <cstdio>
#include <string>

#include "Activity.h"
#include "Config.h"
#include "DeadzoneMath.h"
#include "HidMapping.h"

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

void TestHidMapping() {
  using crdeadzone::DecideMapping;
  auto ds4 = DecideMapping(true, true, true, true, true, true);
  Check(ds4.usable && ds4.lookUsesZRz && ds4.triggersAreRxRy, "hid mapping full set (DS4)");
  auto xbox = DecideMapping(true, true, false, true, true, false);
  Check(xbox.usable && !xbox.lookUsesZRz && !xbox.triggersAreRxRy, "hid mapping X/Y+Rx/Ry");
  auto noLook = DecideMapping(true, true, false, false, false, false);
  Check(!noLook.usable, "hid mapping no look pair unusable");
  auto noMove = DecideMapping(false, true, true, true, true, true);
  Check(!noMove.usable, "hid mapping no movement pair unusable");
}

void TestHidNorm() {
  using namespace crdeadzone;
  Check(HidToSigned(128, 0, 255) > -0.01f && HidToSigned(128, 0, 255) < 0.01f,
        "hid center ~= 0");
  Check(HidToSigned(0, 0, 255) == -1.0f, "hid min -> -1");
  Check(HidToSigned(255, 0, 255) == 1.0f, "hid max -> 1");
  Check(HidToUnit(0, 0, 255) == 0.0f && HidToUnit(255, 0, 255) == 1.0f, "hid unit ends");
  Check(SignedToHid(0.0f, 0, 255) == 127 || SignedToHid(0.0f, 0, 255) == 128,
        "hid roundtrip center");
  Check(UnitToHid(1.0f, 0, 255) == 255, "hid roundtrip full");
  Check(HidToSigned(5, 10, 10) == 0.0f, "hid degenerate range safe");
}

void TestActivity() {
  using namespace crdeadzone;
  SetTickOverride(10000);
  Check(!WrapperRecentlyActive(), "activity initially idle");
  MarkWrapperActive();
  SetTickOverride(11000);
  Check(WrapperRecentlyActive(), "activity live within window");
  SetTickOverride(13000);
  Check(!WrapperRecentlyActive(), "activity expires after window");
  ClearTickOverride();
}

}  // namespace

int main() {
  TestAxial();
  TestRadial();
  TestTrigger();
  TestCurves();
  TestConfigParse();
  TestHidMapping();
  TestHidNorm();
  TestActivity();
  if (g_fail == 0) {
    std::printf("ALL TESTS PASSED\n");
    return 0;
  }
  std::printf("%d TEST(S) FAILED\n", g_fail);
  return 1;
}
