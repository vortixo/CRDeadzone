#include "PatternScanner.h"

#include <cctype>
#include <cstring>

namespace crdeadzone {

PatternScanner::PatternScanner(uint8_t* base, size_t size) : base_(base), size_(size) {}

std::vector<PatternByte> PatternScanner::Parse(const std::string& pattern) {
  std::vector<PatternByte> out;
  std::string tok;
  auto flush = [&] {
    if (tok.empty()) return;
    if (tok == "?" || tok == "??") {
      out.push_back(PatternByte{true, 0});
    } else {
      unsigned v = 0;
      for (char c : tok) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
      }
      out.push_back(PatternByte{false, static_cast<uint8_t>(v & 0xFF)});
    }
    tok.clear();
  };
  for (char c : pattern) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      flush();
    } else {
      tok += c;
    }
  }
  flush();
  return out;
}

std::optional<uint8_t*> PatternScanner::Find(const std::string& pattern) const {
  const auto bytes = Parse(pattern);
  if (bytes.empty() || bytes.size() > size_) return std::nullopt;
  for (size_t i = 0; i + bytes.size() <= size_; ++i) {
    bool ok = true;
    for (size_t j = 0; j < bytes.size(); ++j) {
      if (!bytes[j].wildcard && base_[i + j] != bytes[j].value) {
        ok = false;
        break;
      }
    }
    if (ok) return base_ + i;
  }
  return std::nullopt;
}

std::vector<uint8_t*> PatternScanner::FindAll(const std::string& pattern, size_t max) const {
  std::vector<uint8_t*> out;
  const auto bytes = Parse(pattern);
  if (bytes.empty() || bytes.size() > size_) return out;
  for (size_t i = 0; i + bytes.size() <= size_ && out.size() < max; ++i) {
    bool ok = true;
    for (size_t j = 0; j < bytes.size(); ++j) {
      if (!bytes[j].wildcard && base_[i + j] != bytes[j].value) {
        ok = false;
        break;
      }
    }
    if (ok) out.push_back(base_ + i);
  }
  return out;
}

std::optional<uint8_t*> FindStringRef(uint8_t* moduleBase, size_t moduleSize,
                                      const char* text) {
  const std::string s(text);
  if (s.empty() || s.size() > moduleSize) return std::nullopt;
  for (size_t i = 0; i + s.size() <= moduleSize; ++i) {
    if (std::memcmp(moduleBase + i, s.data(), s.size()) == 0 &&
        moduleBase[i + s.size()] == 0) {
      return moduleBase + i;
    }
  }
  return std::nullopt;
}

std::vector<uint8_t*> FindLeaRefs(uint8_t* textBase, size_t textSize,
                                  const uint8_t* target, size_t max) {
  // x64 RIP-relative LEA: REX.W + 8D /r with modrm mod=00 (0x0D,0x15,...,0x3D).
  // Displacement = target - (instr + 7).
  std::vector<uint8_t*> out;
  if (textSize < 7) return out;
  for (size_t i = 0; i + 7 <= textSize && out.size() < max; ++i) {
    const uint8_t rex = textBase[i];
    if (rex < 0x48 || rex > 0x4F) continue;
    if (textBase[i + 1] != 0x8D) continue;
    const uint8_t modrm = textBase[i + 2];
    if ((modrm & 0xC7) != 0x05) continue;  // mod=00, r/m=101
    int32_t disp = 0;
    std::memcpy(&disp, textBase + i + 3, 4);
    const uint8_t* ref = textBase + i + 7 + disp;
    if (ref == target) out.push_back(textBase + i);
  }
  return out;
}

}  // namespace crdeadzone
