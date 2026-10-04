#pragma once

#include <format>
#include <string>
#include <string_view>

namespace crdeadzone {

// Thread-safe file logger. The log lives next to the DLL (CRDeadzone.log).
//
// Formatting goes through std::vformat (runtime format strings) rather
// than std::format_string: MSVC rejects format_string parameters combined
// with defaulted trailing arguments, so this spelling compiles on both
// MSVC and GCC/Clang.
class Logger {
 public:
  static Logger& Instance();

  // Must be called once from the init thread with the DLL's own directory.
  void Init(const std::wstring& directory);
  void Shutdown();

  // Formatted logging. Single-argument calls use the plain overloads
  // below so braces in game-derived strings are never interpreted.
  template <typename... Args>
  void Info(std::string_view fmt, Args... args) {
    WriteImpl("INFO", std::vformat(fmt, std::make_format_args(args...)));
  }

  template <typename... Args>
  void Warn(std::string_view fmt, Args... args) {
    WriteImpl("WARN", std::vformat(fmt, std::make_format_args(args...)));
  }

  template <typename... Args>
  void Error(std::string_view fmt, Args... args) {
    WriteImpl("ERROR", std::vformat(fmt, std::make_format_args(args...)));
  }

  // Plain (non-formatted) overloads for pre-built messages.
  void Info(std::string_view msg);
  void Warn(std::string_view msg);
  void Error(std::string_view msg);

 private:
  Logger() = default;

  // Internal implementation with pre-formatted message.
  void WriteImpl(std::string_view level, std::string_view msg);
};

}  // namespace crdeadzone
