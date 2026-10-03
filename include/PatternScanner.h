#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Minimal update-resilient pattern scanner over the game module.
// All game addresses are discovered at runtime; nothing is hardcoded.

namespace crdeadzone {

struct PatternByte {
  bool wildcard = false;
  uint8_t value = 0;
};

class PatternScanner {
 public:
  PatternScanner(uint8_t* base, size_t size);

  // Pattern like "48 8D 0D ?? ?? ?? ?? 66 0F". Returns absolute address or null.
  std::optional<uint8_t*> Find(const std::string& pattern) const;
  // All matches, for diagnostics.
  std::vector<uint8_t*> FindAll(const std::string& pattern, size_t max = 16) const;

  static std::vector<PatternByte> Parse(const std::string& pattern);

 private:
  uint8_t* base_;
  size_t size_;
};

// Finds an ANSI string in the module; used to anchor deadzone code refs
// ("deadZone", "slideDeadzone", "stickInputPowerFactor").
std::optional<uint8_t*> FindStringRef(uint8_t* moduleBase, size_t moduleSize,
                                      const char* text);

// Scans .text for RIP-relative LEA instructions referencing target, so future
// game versions can be re-anchored from the log without a debugger.
std::vector<uint8_t*> FindLeaRefs(uint8_t* textBase, size_t textSize,
                                  const uint8_t* target, size_t max = 16);

}  // namespace crdeadzone
