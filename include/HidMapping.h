#pragma once

#include <cstddef>
#include <cstdint>

// Portable HID axis mapping math (no OS headers, unit tested).
// See HidReport.h for the device/convention documentation.

namespace crdeadzone {

struct HidAxisRange {
  bool present = false;
  int32_t logicalMin = 0;
  int32_t logicalMax = 0;
};

struct HidMapping {
  bool usable = false;
  HidAxisRange x, y, z, rx, ry, rz;
  bool lookUsesZRz = false;
  bool triggersAreRxRy = false;
};

// Pure mapping decision, unit-testable without HID headers.
HidMapping DecideMapping(bool hasX, bool hasY, bool hasZ, bool hasRx, bool hasRy,
                         bool hasRz);

// Normalization helpers over logical ranges.
float HidToSigned(int32_t raw, int32_t lo, int32_t hi);    // sticks: [-1, 1]
float HidToUnit(int32_t raw, int32_t lo, int32_t hi);      // triggers: [0, 1]
int32_t SignedToHid(float v, int32_t lo, int32_t hi);
int32_t UnitToHid(float v, int32_t lo, int32_t hi);

}  // namespace crdeadzone
