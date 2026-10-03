// Portable logic tests: deadzone math + INI parsing + HID mapping helpers.
// Build (Linux/macOS/Windows): g++ -std=c++20 -Iinclude tests/test_deadzone_math.cpp
//   src/DeadzoneMath.cpp src/Config.cpp src/HidMapping.cpp src/Activity.cpp
//   src/PatternScanner.cpp src/Logger.cpp -o test_deadzone_math && ./test_deadzone_math

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "Activity.h"
#include "Config.h"
#include "DeadzoneMath.h"
#include "HidMapping.h"
#include "Logger.h"
#include "PatternScanner.h"

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
  Check(Config::HashBytes("abc") == Config::HashBytes("abc"), "hash stable");
  Check(Config::HashBytes("abc") != Config::HashBytes("abd"), "hash differs");
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

void TestXInputConversion() {
  using namespace crdeadzone;
  Check(XInputShortToFloat(32767) == 1.0f, "xinput max -> 1");
  Check(XInputShortToFloat(-32768) == -1.0f, "xinput min -> -1");
  Check(XInputShortToFloat(0) == 0.0f, "xinput zero -> 0");
  Check(XInputShortToFloat(99999) == 1.0f, "xinput clamp high");
  Check(XInputShortToFloat(-99999) == -1.0f, "xinput clamp low");
  Check(FloatToXInputShort(1.0f) == 32767, "xinput back max");
  Check(FloatToXInputShort(-1.0f) == -32767, "xinput back min");
  Check(FloatToXInputShort(2.0f) == 32767, "xinput back clamp");
  // Round trip within 1 LSB.
  Check(std::abs(XInputShortToFloat(FloatToXInputShort(0.5f)) - 0.5f) < 0.0001f,
        "xinput roundtrip");
}

void TestGamepadSettings() {
  using namespace crdeadzone;
  auto linked = MakeGamepadSettings(15, 100, 0, 10, 100, 0, 5, 5, 100, false, false);
  Check(linked.lookInner == linked.moveInner, "linked look follows movement");
  Check(linked.triggerRight == linked.triggerLeft, "linked trigger follows left");
  auto split = MakeGamepadSettings(15, 90, 1, 10, 80, 2, 5, 20, 100, true, true);
  Check(Near(split.moveInner, 0.15f) && Near(split.moveOuter, 0.9f), "split movement values");
  Check(Near(split.lookInner, 0.10f) && Near(split.lookOuter, 0.8f), "split look values");
  Check(split.movePower < split.lookPower, "split curve presets differ");
  Check(Near(split.triggerRight, 0.20f), "split trigger right");

  float lx = 0.05f, ly = 0.0f, rx = 0.05f, ry = 0.0f, lt = 0.02f, rt = 0.9f;
  GamepadSettings gs;
  gs.moveInner = 0.15f;
  gs.lookInner = 0.0f;  // look passes through
  gs.triggerLeft = 0.05f;
  gs.triggerRight = 0.05f;
  ApplyGamepadState(lx, ly, rx, ry, lt, rt, gs);
  Check(lx == 0.0f && ly == 0.0f, "combined movement deadzoned");
  Check(Near(rx, 0.05f), "combined look passthrough");
  Check(lt == 0.0f && Near(rt, (0.9f - 0.05f) / 0.95f), "combined triggers");
}

void TestPatternScanner() {
  using namespace crdeadzone;
  auto bytes = PatternScanner::Parse("48 8D ?? 0D");
  Check(bytes.size() == 4 && !bytes[0].wildcard && bytes[2].wildcard && bytes[3].value == 0x0D,
        "pattern parse wildcards");

  uint8_t buf[64] = {};
  buf[10] = 0x48;
  buf[11] = 0x8D;
  buf[12] = 0x3D;  // any modrm/r+m with mod=00... (here: 0x3D passes the mask check only in FindLeaRefs)
  buf[13] = 0xAA;
  PatternScanner sc(buf, sizeof(buf));
  auto hit = sc.Find("48 8D ?? AA");
  Check(hit && *hit == buf + 10, "pattern find");
  Check(!sc.Find("FF FF FF").has_value(), "pattern miss");
  auto all = sc.FindAll("48", 16);
  Check(all.size() == 1, "pattern findall");

  const char text[] = {'d', 'e', 'a', 'd', 'Z', 'o', 'n', 'e', 0, 'x'};
  auto str = FindStringRef(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(text)),
                           sizeof(text), "deadZone");
  Check(str && *str == reinterpret_cast<const uint8_t*>(text), "string anchor found");
  Check(!FindStringRef(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(text)),
                       sizeof(text), "missing")
             .has_value(),
        "string anchor miss");

  // LEA rcx, [rip+disp] -> 48 8D 0D <disp32>; point it at text.
  uint8_t code[32] = {};
  const uint8_t* target = reinterpret_cast<const uint8_t*>(text);
  code[4] = 0x48;
  code[5] = 0x8D;
  code[6] = 0x0D;
  int32_t disp = static_cast<int32_t>(target - (code + 4 + 7));
  std::memcpy(code + 7, &disp, 4);
  auto refs = FindLeaRefs(code, sizeof(code), target, 8);
  Check(refs.size() == 1 && refs[0] == code + 4, "LEA ref found");
}

void TestConfigFile() {
  using namespace crdeadzone;
  const auto dir = std::filesystem::temp_directory_path() / "crdeadzone_test_cfg";
  std::filesystem::create_directories(dir);
  const auto ini = dir / "crdeadzone.ini";
  {
    std::ofstream f(ini);
    f << "[Settings]\nmovement_deadzone=25\nlook_curve=2\nenable_per_stick=0\n";
  }
  std::string narrow = ini.string();
  Config c(std::wstring(narrow.begin(), narrow.end()));
  auto s = c.Get();
  Check(s.movementDeadzone == 25 && s.lookCurve == 2 && !s.perStick, "config file load");
  Check(s.lookDeadzone == 10, "config file defaults kept");
  // External edit is picked up.
  {
    std::ofstream f(ini);
    f << "[Settings]\nmovement_deadzone=30\n";
  }
  Check(c.PollForChanges(0), "config change detected");
  c.Load();
  Check(c.Get().movementDeadzone == 30, "config reload applies");
  Check(!c.PollForChanges(0), "config quiet when unchanged");
  std::filesystem::remove_all(dir);
}

void TestLogger() {
  using namespace crdeadzone;
  const auto dir = std::filesystem::temp_directory_path() / "crdeadzone_test_log";
  std::filesystem::create_directories(dir);
  std::string narrow = dir.string();
  Logger::Instance().Init(std::wstring(narrow.begin(), narrow.end()));
  Logger::Instance().Info("hello-info");
  Logger::Instance().Warn("hello-warn");
  Logger::Instance().Error("hello-error");
  Logger::Instance().Shutdown();
  std::ifstream f(dir / "CRDeadzone.log");
  const std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  Check(content.find("[INFO] hello-info") != std::string::npos, "logger info line");
  Check(content.find("[WARN] hello-warn") != std::string::npos, "logger warn line");
  Check(content.find("[ERROR] hello-error") != std::string::npos, "logger error line");
  std::filesystem::remove_all(dir);
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
  TestXInputConversion();
  TestGamepadSettings();
  TestPatternScanner();
  TestConfigFile();
  TestLogger();
  if (g_fail == 0) {
    std::printf("ALL TESTS PASSED\n");
    return 0;
  }
  std::printf("%d TEST(S) FAILED\n", g_fail);
  return 1;
}
