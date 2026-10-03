#pragma once

#include <string>

namespace crdeadzone {

// Thread-safe file logger. The log lives next to the DLL (CRDeadzone.log).
class Logger {
 public:
  static Logger& Instance();

  // Must be called once from the init thread with the DLL's own directory.
  void Init(const std::wstring& directory);
  void Shutdown();

  void Info(const std::string& msg);
  void Warn(const std::string& msg);
  void Error(const std::string& msg);

 private:
  Logger() = default;
};

}  // namespace crdeadzone
