#include "Activity.h"

#include <atomic>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#endif

namespace crdeadzone {
namespace {

std::atomic<uint64_t> g_lastWrapperMs{0};

uint64_t NowMs() {
#ifdef _WIN32
  return GetTickCount64();
#else
  return 0;
#endif
}

}  // namespace

void MarkWrapperActive() { g_lastWrapperMs.store(NowMs()); }

bool WrapperRecentlyActive() { return IsRecentlyActive(g_lastWrapperMs.load(), NowMs()); }

bool IsRecentlyActive(uint64_t lastActiveMs, uint64_t nowMs) {
  if (lastActiveMs == 0) return false;  // never marked: always idle
  return nowMs - lastActiveMs < kWrapperActivityWindowMs;
}

}  // namespace crdeadzone
