#include "OptionsOverride.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Logger.h"
#include "PatternScanner.h"

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
    char buf[160];
    snprintf(buf, sizeof(buf), "options: anchor '%s' @ %p", anchor, *hit);
    log.Info(buf);

    const auto refs = FindLeaRefs(text->base, text->size, *hit, 8);
    snprintf(buf, sizeof(buf), "options: '%s' referenced by %zu code site(s)", anchor, refs.size());
    log.Info(buf);
    for (const auto* r : refs) {
      snprintf(buf, sizeof(buf), "options:   ref @ %p", r);
      log.Info(buf);
    }
  }

  log.Info("options: discovery complete (read-only; no game memory was modified)");
}

}  // namespace crdeadzone
