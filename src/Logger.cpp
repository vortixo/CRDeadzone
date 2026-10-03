#include "Logger.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <mutex>

namespace crdeadzone {
namespace {

std::mutex g_mutex;
FILE* g_file = nullptr;

void Write(const char* level, const std::string& msg) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_file) return;
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char ts[32];
  std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);
  std::fprintf(g_file, "[%s] [%s] %s\n", ts, level, msg.c_str());
  std::fflush(g_file);
}

}  // namespace

Logger& Logger::Instance() {
  static Logger instance;
  return instance;
}

void Logger::Init(const std::wstring& directory) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_file) return;
  try {
    const auto path = std::filesystem::path(directory) / "CRDeadzone.log";
#if defined(_WIN32)
    _wfopen_s(&g_file, path.c_str(), L"w");
#else
    g_file = std::fopen(path.string().c_str(), "w");
#endif
  } catch (...) {
    g_file = nullptr;
  }
}

void Logger::Shutdown() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_file) {
    std::fclose(g_file);
    g_file = nullptr;
  }
}

void Logger::Info(const std::string& msg) { Write("INFO", msg); }
void Logger::Warn(const std::string& msg) { Write("WARN", msg); }
void Logger::Error(const std::string& msg) { Write("ERROR", msg); }

}  // namespace crdeadzone
