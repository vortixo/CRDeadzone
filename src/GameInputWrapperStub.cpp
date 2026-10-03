// Fallback when no GameInput headers are available at build time.
// Version detection is impossible, so every object passes through untouched
// and the HID/XInput layers do the work.

#include "GameInputVersion.h"

namespace crdeadzone {

GameInputVersion ProbeAndWrapObject(void* /*gameInput*/, const Config* /*cfg*/) {
  return GameInputVersion::Unknown;
}

}  // namespace crdeadzone
