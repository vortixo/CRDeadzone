// Config specs: integer parsing/clamping, content hashing, and the INI
// file round trip (including CRLF line endings as written on Windows and
// same-size value edits, which file-mtime polling can miss).

#include "Test.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "Config.h"

namespace {

using namespace crdeadzone;

void Parse() {
  EXPECT_EQ(Config::ParseInt("15", 0), 15);
  EXPECT_EQ(Config::ParseInt("  42  ", 0), 42);
  EXPECT_EQ(Config::ParseInt("-3", 0), -3);
  EXPECT_EQ(Config::ParseInt("abc", 7), 7);
  EXPECT_EQ(Config::ParseInt("12x", 7), 7);
  EXPECT_EQ(Config::ParseInt("", 7), 7);
  EXPECT_EQ(Config::ClampInt(999, 0, 50), 50);
  EXPECT_EQ(Config::ClampInt(-5, 0, 50), 0);
  EXPECT_EQ(Config::ClampInt(25, 0, 50), 25);
}

void Hash() {
  EXPECT_TRUE(Config::HashBytes("abc") == Config::HashBytes("abc"));
  EXPECT_TRUE(Config::HashBytes("abc") != Config::HashBytes("abd"));
  EXPECT_TRUE(Config::HashBytes("") != 0);
}

std::filesystem::path UniqueDir(const char* tag) {
  static int counter = 0;
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  auto dir = std::filesystem::temp_directory_path() /
             ("crdeadzone_" + std::string(tag) + "_" + std::to_string(stamp) + "_" +
              std::to_string(++counter));
  std::filesystem::create_directories(dir);
  return dir;
}

void FileRoundTrip() {
  const auto dir = UniqueDir("cfg");
  const auto ini = dir / "crdeadzone.ini";
  {
    // CRLF endings, as the file has on a Windows install.
    std::ofstream f(ini, std::ios::binary);
    f << "[Settings]\r\nmovement_deadzone=25\r\nlook_curve=2\r\nenable_per_stick=0\r\n"
         "bogus_key=99\r\n";
  }
  const std::string narrow = ini.string();
  Config c(std::wstring(narrow.begin(), narrow.end()));

  const auto s = c.Get();
  EXPECT_EQ(s.movementDeadzone, 25);
  EXPECT_EQ(s.lookCurve, 2);
  EXPECT_TRUE(!s.perStick);
  // Untouched keys keep descriptor defaults; unknown keys are ignored.
  EXPECT_EQ(s.lookDeadzone, 10);
  EXPECT_EQ(s.movementOuter, 100);

  // Same byte length before/after: the change must still be detected.
  {
    std::ofstream f(ini, std::ios::binary);
    f << "[Settings]\r\nmovement_deadzone=30\r\n";
  }
  EXPECT_TRUE(c.PollForChanges(0));
  c.Load();
  EXPECT_EQ(c.Get().movementDeadzone, 30);
  EXPECT_TRUE(!c.PollForChanges(0));
  std::filesystem::remove_all(dir);
}

void MissingFile() {
  const auto dir = UniqueDir("missing");
  const std::string narrow = (dir / "does_not_exist.ini").string();
  Config c(std::wstring(narrow.begin(), narrow.end()));
  // No file, no crash: descriptor defaults throughout, nothing to detect.
  EXPECT_EQ(c.Get().movementDeadzone, 15);
  EXPECT_TRUE(!c.PollForChanges(0));
  std::filesystem::remove_all(dir);
}

}  // namespace

void ConfigTests() {
  crtest::Suite("config-parse", Parse);
  crtest::Suite("config-hash", Hash);
  crtest::Suite("config-file", FileRoundTrip);
  crtest::Suite("config-missing", MissingFile);
}
