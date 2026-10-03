#include "OptionsOverride.h"

#include <windows.h>

#include <MinHook.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "Disasm.h"
#include "Logger.h"
#include "PatternScanner.h"
#include "TunableCapture.h"

namespace crdeadzone {
namespace {

// Keys found in CONTROLResonant.exe via strings analysis. Sensitivity keys are
// intentionally absent: the game already exposes sensitivity in its own menu.
const char* kAnchors[] = {
    "deadZone",
    "slideDeadzone",
    "stickInputPowerFactor",
    "movementInputCurve",
    "cameraInputCurve",
    "moveInputRemapCurve",
};

struct Section {
  std::string name;
  uint8_t* base = nullptr;
  size_t size = 0;
};

std::vector<Section> ModuleSections(uint8_t* modBase) {
  std::vector<Section> out;
  const auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(modBase);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return out;
  const auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(modBase + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return out;
  const auto* sec = IMAGE_FIRST_SECTION(nt);
  for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
    char name[9] = {};
    std::memcpy(name, sec->Name, 8);
    Section s;
    s.name = name;
    s.base = modBase + sec->VirtualAddress;
    s.size = sec->Misc.VirtualSize ? sec->Misc.VirtualSize : sec->SizeOfRawData;
    out.push_back(s);
  }
  return out;
}

const Section* FindSection(const std::vector<Section>& sections, const char* name) {
  for (const auto& s : sections) {
    if (s.name == name) return &s;
  }
  return nullptr;
}

// Whole-module committed size for string-anchor scanning.
size_t ModuleSize(uint8_t* modBase) {
  MEMORY_BASIC_INFORMATION mbi{};
  size_t size = 0;
  for (uint8_t* p = modBase;
       VirtualQuery(p, &mbi, sizeof(mbi)) == sizeof(mbi) && mbi.AllocationBase == modBase;
       p += mbi.RegionSize) {
    size += mbi.RegionSize;
    if (size > 1024ULL * 1024 * 1024) break;  // sanity cap
  }
  return size;
}

// Read-only capture state: at most kMaxBinderHooks shared-binder candidates.
constexpr size_t kMaxBinderHooks = 4;
struct BinderHook {
  void* target = nullptr;
  void* real = nullptr;
  uint64_t rva = 0;
};
BinderHook g_binderHooks[kMaxBinderHooks];
size_t g_binderHookCount = 0;
TunableCapture g_capture;
std::mutex g_captureMutex;
bool g_completeLogged = false;

using BinderFn = void*(WINAPI*)(void*, void*, void*, const char*);

// Logs one watched-name hit. Separate from the detour so the detour's
// __try block shares a function with no destructible temporaries.
void LogTunableHit(const char* name, uint64_t rva, void* a, void* b, void* c,
                   const char* rawName) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "tunable: '%s' via RVA 0x%llX args(%p,%p,%p,%p)", name,
                static_cast<unsigned long long>(rva), a, b, c, rawName);
  Logger::Instance().Info(buf);
  bool complete = false;
  {
    std::lock_guard<std::mutex> lock(g_captureMutex);
    // Provisional field = rdx (out-param slot at the observed binder sites;
    // the full arg dump above disambiguates before anything ever writes).
    // Ruling: record rdx, log everything; cost if wrong is ~zero — this
    // build never writes game memory, and the log carries the evidence.
    g_capture.Record(name, b);
    if (!g_completeLogged && g_capture.Count() >= kWatchedTunableCount) {
      g_completeLogged = true;
      complete = true;
    }
  }
  if (complete) Logger::Instance().Info("tunable: all watched names captured");
}

template <int N>
void* WINAPI DetourBinder(void* a, void* b, void* c, const char* name) {
  auto real = reinterpret_cast<BinderFn>(g_binderHooks[N].real);
  // The binder contract passes a name string in r9 at every observed site;
  // a fault here must degrade to silent passthrough, never a crash.
  char watched[64] = {};
  bool isWatched = false;
  __try {
    if (name) {
      size_t i = 0;
      while (i + 1 < sizeof(watched) && name[i] != '\0') {
        watched[i] = name[i];
        ++i;
      }
      watched[i] = '\0';
      isWatched = IsWatchedTunable(watched);
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    isWatched = false;
  }
  void* ret = real(a, b, c, name);
  if (isWatched) LogTunableHit(watched, g_binderHooks[N].rva, a, b, c, name);
  return ret;
}

template <int N>
bool HookBinderSlot(void* target, uint64_t rva) {
  g_binderHooks[N].target = target;
  g_binderHooks[N].rva = rva;
  if (MH_CreateHook(target, reinterpret_cast<void*>(&DetourBinder<N>),
                    &g_binderHooks[N].real) != MH_OK) {
    return false;
  }
  if (MH_EnableHook(target) != MH_OK) return false;
  return true;
}

}  // namespace

void DiscoverGameDeadzones() {
  auto& log = Logger::Instance();
  uint8_t* modBase = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
  if (!modBase) {
    log.Error("options: could not get game module handle");
    return;
  }

  char addr[32];
  snprintf(addr, sizeof(addr), "%p", modBase);
  log.Info(std::string("options: game module base ") + addr);

  const auto sections = ModuleSections(modBase);
  const Section* text = FindSection(sections, ".text");
  if (!text) {
    log.Warn("options: .text section not found, discovery skipped");
    return;
  }

  // Whole-module string anchors (they live in .rdata, but scan everything so
  // packed/relocated layouts still resolve).
  MEMORY_BASIC_INFORMATION mbi{};
  size_t moduleSize = 0;
  {
    uint8_t* p = modBase;
    while (VirtualQuery(p, &mbi, sizeof(mbi)) == sizeof(mbi) &&
           mbi.AllocationBase == modBase) {
      moduleSize += mbi.RegionSize;
      p += mbi.RegionSize;
      if (moduleSize > 1024ULL * 1024 * 1024) break;  // sanity cap
    }
  }

  for (const char* anchor : kAnchors) {
    const auto hit = FindStringRef(modBase, moduleSize, anchor);
    if (!hit) {
      log.Info(std::string("options: anchor '") + anchor + "' not found");
      continue;
    }
    char buf[192];
    std::snprintf(buf, sizeof(buf), "options: anchor '%s' @ RVA 0x%llX", anchor,
                  static_cast<unsigned long long>(*hit - modBase));
    log.Info(buf);

    const auto refs = FindLeaRefs(text->base, text->size, *hit, 8);
    std::snprintf(buf, sizeof(buf), "options: '%s' referenced by %zu code site(s)", anchor, refs.size());
    log.Info(buf);
    size_t logged = 0;
    for (const auto* r : refs) {
      const auto rva = static_cast<unsigned long long>(r - modBase);
      std::snprintf(buf, sizeof(buf), "options:   ref RVA 0x%llX", rva);
      log.Info(buf);
      if (logged++ >= 3) continue;  // full window for the first 3 only
      const size_t avail = text->size - static_cast<size_t>(r - text->base);
      const FlowInfo flow = WalkFlow(r, 64, avail);
      for (const auto& in : flow.insns) {
        std::snprintf(buf, sizeof(buf), "options:     +0x%02llX %-18s",
                      static_cast<unsigned long long>(in.offset), in.text.c_str());
        log.Info(buf);
      }
      for (size_t t : flow.callTargets) {
        const auto* target = r + t;
        const bool inModule = target >= modBase && target < modBase + moduleSize;
        std::snprintf(buf, sizeof(buf), "options:     call -> %s 0x%llX",
                      inModule ? "RVA" : "outside",
                      inModule ? static_cast<unsigned long long>(target - modBase)
                               : static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(target)));
        log.Info(buf);
      }
      for (size_t d : flow.dataRefs) {
        const auto* ref = r + d;
        if (ref == *hit) continue;  // the anchor itself
        const bool inModule = ref >= modBase && ref < modBase + moduleSize;
        if (!inModule) continue;
        std::snprintf(buf, sizeof(buf), "options:     data -> RVA 0x%llX",
                      static_cast<unsigned long long>(ref - modBase));
        log.Info(buf);
      }
      if (flow.truncated) log.Info("options:     (walk stopped: indirect/unknown)");
    }
  }

  log.Info("options: discovery complete (read-only; no game memory was modified)");
}

bool InstallTunableCapture() {
  auto& log = Logger::Instance();
  uint8_t* modBase = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
  if (!modBase) {
    log.Error("tunable: could not get game module handle");
    return false;
  }
  const size_t moduleSize = ModuleSize(modBase);
  const auto sections = ModuleSections(modBase);
  const Section* text = FindSection(sections, ".text");
  if (!text || moduleSize == 0) {
    log.Warn("tunable: module layout unavailable, capture skipped");
    return false;
  }

  const ModuleView view{modBase, moduleSize, text->base, text->size};
  const auto windows = CollectTunableWindows(view);
  char buf[256];
  for (size_t i = 0; i < kWatchedTunableCount; ++i) {
    const char* name = kWatchedTunables[i];
    const auto it = windows.find(name);
    const size_t targets = it == windows.end() ? 0 : it->second.size();
    std::snprintf(buf, sizeof(buf), "tunable: '%s' %zu call target(s) in anchor windows", name,
                  targets);
    log.Info(buf);
  }

  const auto ranked = ClusterCallTargets(windows);
  const size_t shown = ranked.size() < 8 ? ranked.size() : 8;
  for (size_t i = 0; i < shown; ++i) {
    std::snprintf(buf, sizeof(buf), "tunable: candidate RVA 0x%llX in %zu/%zu windows",
                  static_cast<unsigned long long>(ranked[i].rva), ranked[i].windows,
                  kWatchedTunableCount);
    log.Info(buf);
  }
  if (ranked.empty()) {
    log.Warn("tunable: no binder candidates (anchors moved?) - capture skipped");
    return false;
  }

  size_t hooked = 0;
  for (const auto& t : ranked) {
    if (hooked >= kMaxBinderHooks) break;
    void* target = modBase + t.rva;
    bool ok = false;
    switch (hooked) {
      case 0: ok = HookBinderSlot<0>(target, t.rva); break;
      case 1: ok = HookBinderSlot<1>(target, t.rva); break;
      case 2: ok = HookBinderSlot<2>(target, t.rva); break;
      case 3: ok = HookBinderSlot<3>(target, t.rva); break;
      default: break;
    }
    std::snprintf(buf, sizeof(buf), "tunable: %s RVA 0x%llX (%zu/%zu windows)",
                  ok ? "hooked" : "HOOK FAILED", static_cast<unsigned long long>(t.rva),
                  t.windows, kWatchedTunableCount);
    if (ok) {
      log.Info(buf);
      ++hooked;
    } else {
      log.Warn(buf);
    }
  }
  g_binderHookCount = hooked;
  if (hooked == 0) {
    log.Warn("tunable: no candidates hooked - capture disabled");
    return false;
  }
  log.Info(
      "tunable: capture live (read-only; watched option bindings will be logged, "
      "0 captures means registration ran before this hook)");
  return true;
}

}  // namespace crdeadzone
