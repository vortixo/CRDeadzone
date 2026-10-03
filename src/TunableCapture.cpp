#include "TunableCapture.h"

#include <algorithm>
#include <cstring>

#include "Disasm.h"
#include "PatternScanner.h"

namespace crdeadzone {

bool IsWatchedTunable(const char* name) {
  if (!name || *name == '\0') return false;
  for (size_t i = 0; i < kWatchedTunableCount; ++i) {
    if (std::strcmp(name, kWatchedTunables[i]) == 0) return true;
  }
  return false;
}

void TunableCapture::Record(const char* name, const void* field) {
  if (!IsWatchedTunable(name) || !field) return;
  fields_[name] = field;
}

const void* TunableCapture::Find(const char* name) const {
  if (!name) return nullptr;
  const auto it = fields_.find(name);
  return it == fields_.end() ? nullptr : it->second;
}

size_t TunableCapture::Count() const { return fields_.size(); }

std::vector<RankedTarget> ClusterCallTargets(
    const std::map<std::string, std::vector<uint64_t>>& windows) {
  std::map<uint64_t, RankedTarget> byTarget;
  for (const auto& [name, targets] : windows) {
    // One call site each: duplicates inside a window count once.
    std::vector<uint64_t> unique = targets;
    std::sort(unique.begin(), unique.end());
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
    for (uint64_t rva : unique) {
      RankedTarget& entry = byTarget[rva];
      entry.rva = rva;
      ++entry.windows;
      entry.names.push_back(name);
    }
  }
  std::vector<RankedTarget> ranked;
  ranked.reserve(byTarget.size());
  for (auto& [rva, entry] : byTarget) ranked.push_back(std::move(entry));
  std::sort(ranked.begin(), ranked.end(), [](const RankedTarget& a, const RankedTarget& b) {
    if (a.windows != b.windows) return a.windows > b.windows;
    return a.rva < b.rva;
  });
  return ranked;
}

std::map<std::string, std::vector<uint64_t>> CollectTunableWindows(const ModuleView& mod) {
  std::map<std::string, std::vector<uint64_t>> windows;
  if (!mod.base || !mod.text || mod.size == 0 || mod.textSize == 0) return windows;
  constexpr size_t kMaxRefsPerName = 8;
  constexpr size_t kWalkBytes = 64;
  for (size_t i = 0; i < kWatchedTunableCount; ++i) {
    const char* name = kWatchedTunables[i];
    const auto anchor = FindStringRef(mod.base, mod.size, name);
    if (!anchor) continue;
    const auto refs = FindLeaRefs(mod.text, mod.textSize, *anchor, kMaxRefsPerName);
    std::vector<uint64_t> targets;
    for (const uint8_t* ref : refs) {
      const size_t avail = mod.textSize - static_cast<size_t>(ref - mod.text);
      const FlowInfo flow = WalkFlow(ref, kWalkBytes, avail);
      for (size_t t : flow.callTargets) {
        const uint8_t* target = ref + t;
        if (target < mod.base || target >= mod.base + mod.size) continue;  // outside module
        targets.push_back(static_cast<uint64_t>(target - mod.base));
      }
    }
    if (!targets.empty()) windows[name] = std::move(targets);
  }
  return windows;
}

}  // namespace crdeadzone
