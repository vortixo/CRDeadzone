#pragma once

#include <format>
#include <source_location>
#include <string>
#include <string_view>

namespace crdeadzone {

// Thread-safe file logger. The log lives next to the DLL (CRDeadzone.log).
// Uses std::format (C++20) and std::source_location for automatic context.
class Logger {
 public:
  static Logger& Instance();

  // Must be called once from the init thread with the DLL's own directory.
  void Init(const std::wstring& directory);
  void Shutdown();

  // Formatted logging with automatic source location.
  template <typename... Args>
  void Info(std::format_string<Args...> fmt, Args&&... args,
            std::source_location loc = std::source_location::current()) {
    Write("INFO", std::format(fmt, std::forward<Args>(args)...), loc);
  }

  template <typename... Args>
  void Warn(std::format_string<Args...> fmt, Args&&... args,
            std::source_location loc = std::source_location::current()) {
    Write("WARN", std::format(fmt, std::forward<Args>(args)...), loc);
  }

  template <typename... Args>
  void Error(std::format_string<Args...> fmt, Args&&... args,
             std::source_location loc = std::source_location::current()) {
    Write("ERROR", std::format(fmt, std::forward<Args>(args)...), loc);
  }

  // Legacy non-formatted overloads for gradual migration.
  void Info(std::string_view msg);
  void Warn(std::string_view msg);
  void Error(std::string_view msg);

 private:
  Logger() = default;
  void Write(std::string_view level, std::string_view msg,
             std::source_location loc);

  // Internal implementation with pre-formatted message.
  void WriteImpl(std::string_view level, std::string_view msg);
};

}  // namespace crdeadzone
