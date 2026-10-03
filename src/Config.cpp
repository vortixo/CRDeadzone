#include "Config.h"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace crdeadzone {
namespace {

std::string Trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

}  // namespace

Config::Config(const std::wstring& iniPath) : iniPath_(iniPath) {
  Load();
}

int Config::ParseInt(const std::string& s, int fallback) {
  const std::string t = Trim(s);
  if (t.empty()) return fallback;
  try {
    size_t pos = 0;
    int v = std::stoi(t, &pos);
    if (pos != t.size()) return fallback;
    return v;
  } catch (...) {
    return fallback;
  }
}

int Config::ClampInt(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

uint64_t Config::FileWriteMs(const std::wstring& path) {
  try {
    auto t = std::filesystem::last_write_time(path);
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(t.time_since_epoch()).count());
  } catch (...) {
    return 0;
  }
}

void Config::Load() {
  Settings next;  // start from defaults, overlay file values
  std::ifstream f{std::filesystem::path(iniPath_)};
  if (f) {
    std::string line, section;
    while (std::getline(f, line)) {
      line = Trim(line);
      if (line.empty() || line[0] == ';' || line[0] == '#') continue;
      if (line.front() == '[' && line.back() == ']') {
        section = line;
        continue;
      }
      const auto eq = line.find('=');
      if (eq == std::string::npos) continue;
      const std::string key = Trim(line.substr(0, eq));
      const std::string val = Trim(line.substr(eq + 1));
      if (key == "movement_deadzone") next.movementDeadzone = ClampInt(ParseInt(val, next.movementDeadzone), 0, 50);
      else if (key == "movement_outer_deadzone") next.movementOuter = ClampInt(ParseInt(val, next.movementOuter), 50, 100);
      else if (key == "movement_curve") next.movementCurve = ClampInt(ParseInt(val, next.movementCurve), 0, 3);
      else if (key == "look_deadzone") next.lookDeadzone = ClampInt(ParseInt(val, next.lookDeadzone), 0, 50);
      else if (key == "look_outer_deadzone") next.lookOuter = ClampInt(ParseInt(val, next.lookOuter), 50, 100);
      else if (key == "look_curve") next.lookCurve = ClampInt(ParseInt(val, next.lookCurve), 0, 3);
      else if (key == "trigger_left_deadzone") next.triggerLeftDeadzone = ClampInt(ParseInt(val, next.triggerLeftDeadzone), 0, 50);
      else if (key == "trigger_right_deadzone") next.triggerRightDeadzone = ClampInt(ParseInt(val, next.triggerRightDeadzone), 0, 50);
      else if (key == "custom_curve_power") next.customCurvePower = ClampInt(ParseInt(val, next.customCurvePower), 50, 300);
      else if (key == "enable_per_stick") next.perStick = ParseInt(val, next.perStick ? 1 : 0) != 0;
      else if (key == "enable_trigger_separate") next.triggerSeparate = ParseInt(val, next.triggerSeparate ? 1 : 0) != 0;
    }
  }
  std::lock_guard<std::mutex> lock(mutex_);
  settings_ = next;
  lastWriteMs_ = FileWriteMs(iniPath_);
}

bool Config::PollForChanges(uint64_t /*nowMs*/) {
  const uint64_t w = FileWriteMs(iniPath_);
  std::lock_guard<std::mutex> lock(mutex_);
  if (w != 0 && w != lastWriteMs_) return true;
  return false;
}

Settings Config::Get() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return settings_;
}

}  // namespace crdeadzone
