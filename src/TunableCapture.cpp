#include "TunableCapture.h"

#include <algorithm>
#include <cstring>

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

}  // namespace crdeadzone
