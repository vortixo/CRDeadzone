#include "Config.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace crdeadzone {
namespace {

// Trim whitespace from both ends of a string_view.
constexpr std::string_view Trim(std::string_view s) noexcept {
  size_t first = 0;
  while (first < s.size() && std::isspace(static_cast<unsigned char>(s[first]))) {
    ++first;
  }
  size_t last = s.size();
  while (last > first && std::isspace(static_cast<unsigned char>(s[last - 1]))) {
    --last;
  }
  return s.substr(first, last - first);
}

// Parse integer from string_view with fallback.
int ParseIntImpl(std::string_view s, int fallback) noexcept {
  s = Trim(s);
  if (s.empty()) return fallback;

  int value = 0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
  if (ec != std::errc{} || ptr != s.data() + s.size()) {
    return fallback;
  }
  return value;
}

// Clamp integer to range.
constexpr int ClampIntImpl(int v, int lo, int hi) noexcept {
  return v < lo ? lo : (v > hi ? hi : v);
}

// FNV-1a 64-bit hash.
uint64_t HashBytesImpl(std::string_view s) noexcept {
  uint64_t h = 1469598103934665603ULL;  // FNV offset basis
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ULL;  // FNV prime
  }
  return h == 0 ? 1 : h;  // Reserve 0 for "missing"
}

// Compute hash of file contents.
uint64_t FileHashImpl(const std::wstring& path) {
  std::ifstream f{std::filesystem::path(path), std::ios::binary};
  if (!f) return 0;
  std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (bytes.empty()) return 0;
  return HashBytesImpl(bytes);
}

}  // namespace

Config::Config(const std::wstring& iniPath) : iniPath_(iniPath) {
  Load();
}

int Config::ParseInt(const std::string& s, int fallback) {
  return ParseIntImpl(s, fallback);
}

int Config::ClampInt(int v, int lo, int hi) {
  return ClampIntImpl(v, lo, hi);
}

uint64_t Config::HashBytes(const std::string& s) {
  return HashBytesImpl(s);
}

void Config::Load() {
  Settings next;  // Start from defaults, overlay file values.

  std::ifstream f{std::filesystem::path(iniPath_)};
  if (f) {
    std::string line;
    std::string_view section;
    while (std::getline(f, line)) {
      std::string_view sv = Trim(line);
      if (sv.empty() || sv[0] == ';' || sv[0] == '#') continue;
      if (sv.front() == '[' && sv.back() == ']') {
        section = sv;
        continue;
      }
      const auto eq = sv.find('=');
      if (eq == std::string_view::npos) continue;

      const std::string_view key = Trim(sv.substr(0, eq));
      const std::string_view val = Trim(sv.substr(eq + 1));

      // Map keys to settings using a cleaner approach.
      if (key == "movement_deadzone")
        next.movementDeadzone = ClampIntImpl(ParseIntImpl(val, next.movementDeadzone), 0, 50);
      else if (key == "movement_outer_deadzone")
        next.movementOuter = ClampIntImpl(ParseIntImpl(val, next.movementOuter), 50, 100);
      else if (key == "movement_curve")
        next.movementCurve = ClampIntImpl(ParseIntImpl(val, next.movementCurve), 0, 3);
      else if (key == "look_deadzone")
        next.lookDeadzone = ClampIntImpl(ParseIntImpl(val, next.lookDeadzone), 0, 50);
      else if (key == "look_outer_deadzone")
        next.lookOuter = ClampIntImpl(ParseIntImpl(val, next.lookOuter), 50, 100);
      else if (key == "look_curve")
        next.lookCurve = ClampIntImpl(ParseIntImpl(val, next.lookCurve), 0, 3);
      else if (key == "trigger_left_deadzone")
        next.triggerLeftDeadzone = ClampIntImpl(ParseIntImpl(val, next.triggerLeftDeadzone), 0, 50);
      else if (key == "trigger_right_deadzone")
        next.triggerRightDeadzone = ClampIntImpl(ParseIntImpl(val, next.triggerRightDeadzone), 0, 50);
      else if (key == "custom_curve_power")
        next.customCurvePower = ClampIntImpl(ParseIntImpl(val, next.customCurvePower), 50, 300);
      else if (key == "enable_per_stick")
        next.perStick = ParseIntImpl(val, next.perStick ? 1 : 0) != 0;
      else if (key == "enable_trigger_separate")
        next.triggerSeparate = ParseIntImpl(val, next.triggerSeparate ? 1 : 0) != 0;
    }
  }

  std::lock_guard<std::mutex> lock(mutex_);
  settings_ = next;
  lastHash_ = FileHashImpl(iniPath_);
}

bool Config::PollForChanges(uint64_t /*nowMs*/) {
  const uint64_t h = FileHashImpl(iniPath_);
  std::lock_guard<std::mutex> lock(mutex_);
  if (h != 0 && h != lastHash_) return true;
  return false;
}

Settings Config::Get() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return settings_;
}

uint64_t Config::FileHash(const std::wstring& path) {
  return FileHashImpl(path);
}

}  // namespace crdeadzone