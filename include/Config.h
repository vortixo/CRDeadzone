#pragma once

#include <cstdint>
#include <mutex>
#include <string>

// Live settings. Values mirror deadzone.menu.json and its defaults.
// The INI file is written by CRModMenu; this mod only reads it and polls
// for changes so settings apply instantly without a restart.

namespace crdeadzone {

struct Settings {
  int movementDeadzone = 15;       // % inner, 0-50
  int movementOuter = 100;         // % outer, 50-100
  int movementCurve = 0;           // 0 linear, 1 mild, 2 aggressive, 3 custom
  int lookDeadzone = 10;           // % inner, 0-50
  int lookOuter = 100;             // % outer, 50-100
  int lookCurve = 0;               // same presets as movementCurve
  int triggerLeftDeadzone = 5;     // % 0-50
  int triggerRightDeadzone = 5;    // % 0-50
  int customCurvePower = 100;      // 50-300, 100 = linear
  bool perStick = true;
  bool triggerSeparate = false;
};

class Config {
 public:
  explicit Config(const std::wstring& iniPath);

  // (Re)loads the INI file. Missing file or keys keep current/default values.
  void Load();
  // Returns true once when the file changed on disk since the last check.
  bool PollForChanges(uint64_t nowMs);
  Settings Get() const;

  // Pure helpers, unit tested.
  static int ParseInt(const std::string& s, int fallback);
  static int ClampInt(int v, int lo, int hi);

 private:
  std::wstring iniPath_;
  mutable std::mutex mutex_;
  Settings settings_;
  uint64_t lastWriteMs_ = 0;

  static uint64_t FileWriteMs(const std::wstring& path);
};

}  // namespace crdeadzone
