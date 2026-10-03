#pragma once

#include <cstdint>

// Cross-layer arbitration: the high-level GameInput wrapper marks activity
// whenever it rewrites a gamepad state. The lower HID/XInput layers skip
// their own rewrite while the wrapper is live so one input is never
// deadzoned twice (which would stack the effect).

namespace crdeadzone {

// Window in which wrapper activity suppresses the lower layers.
inline constexpr uint64_t kWrapperActivityWindowMs = 2000;

void MarkWrapperActive();
bool WrapperRecentlyActive();

// Pure arbitration rule, unit tested with explicit timestamps:
// lastActiveMs == 0 means "never active".
bool IsRecentlyActive(uint64_t lastActiveMs, uint64_t nowMs);

}  // namespace crdeadzone
