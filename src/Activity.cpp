#include "Activity.h"

#include <atomic>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#endif

namespace crdeadzone {
namespace {

std::atomic<uint64_t> g_lastWrapperMs{0};
std::atomic<uint64_t> g_tickOverride{0};
std::atomic<bool> g_hasOverride{false};

uint64_t NowMs() {
  if (g_hasOverride.load()) return g_tickOverride.load();
#ifdef _WIN32
  return GetTickCount64();
#else
  return 0;
#endif
}

}  // namespace

void MarkWrapperActive() { g_lastWrapperMs.store(NowMs()); }

bool WrapperRecentlyActive() {
  const uint64_t last = g_lastWrapperMs.load();
  if (last == 0) return false;
  return NowMs() - last < 2000;
}

void SetTickOverride(uint64_t ms) {
  g_tickOverride.store(ms);
  g_hasOverride.store(true);
}

void ClearTickOverride() { g_hasOverride.store(false); }

}  // namespace crdeadzone
