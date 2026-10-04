#include "Logger.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>

namespace crdeadzone {
namespace {

std::mutex g_mutex;
FILE* g_file = nullptr;

void WriteTimestamped(FILE* file, std::string_view level, std::string_view msg) {
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
  std::fprintf(file, "[%s] [%s] %s\n", ts, level.data(), msg.data());
  std::fflush(file);
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

void Logger::Info(std::string_view msg) { WriteImpl("INFO", msg); }
void Logger::Warn(std::string_view msg) { WriteImpl("WARN", msg); }
void Logger::Error(std::string_view msg) { WriteImpl("ERROR", msg); }

void Logger::WriteImpl(std::string_view level, std::string_view msg) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_file) return;
  WriteTimestamped(g_file, level, msg);
}

}  // namespace crdeadzone