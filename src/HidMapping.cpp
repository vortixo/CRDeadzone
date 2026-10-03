#include "HidMapping.h"

namespace crdeadzone {

HidMapping DecideMapping(bool hasX, bool hasY, bool hasZ, bool hasRx, bool hasRy,
                         bool hasRz) {
  HidMapping m;
  if (!(hasX && hasY)) return m;  // no movement pair: unusable
  const bool hasZRz = hasZ && hasRz;
  const bool hasRxRy = hasRx && hasRy;
  if (!hasZRz && !hasRxRy) return m;  // no look pair: unusable
  m.usable = true;
  m.lookUsesZRz = hasZRz;
  m.triggersAreRxRy = hasZRz && hasRxRy;
  return m;
}

float HidToSigned(int32_t raw, int32_t lo, int32_t hi) {
  if (hi <= lo) return 0.0f;
  float u = static_cast<float>(raw - lo) / static_cast<float>(hi - lo);
  if (u < 0.0f) u = 0.0f;
  if (u > 1.0f) u = 1.0f;
  return u * 2.0f - 1.0f;
}

float HidToUnit(int32_t raw, int32_t lo, int32_t hi) {
  if (hi <= lo) return 0.0f;
  float u = static_cast<float>(raw - lo) / static_cast<float>(hi - lo);
  if (u < 0.0f) u = 0.0f;
  if (u > 1.0f) u = 1.0f;
  return u;
}

int32_t SignedToHid(float v, int32_t lo, int32_t hi) {
  if (v < -1.0f) v = -1.0f;
  if (v > 1.0f) v = 1.0f;
  return lo + static_cast<int32_t>((v * 0.5f + 0.5f) * (hi - lo) + 0.5f);
}

int32_t UnitToHid(float v, int32_t lo, int32_t hi) {
  if (v < 0.0f) v = 0.0f;
  if (v > 1.0f) v = 1.0f;
  return lo + static_cast<int32_t>(v * (hi - lo) + 0.5f);
}

}  // namespace crdeadzone
