#pragma once

#include <cstdint>

// Cross-layer arbitration: the high-level GameInput wrapper marks activity
// whenever it rewrites a gamepad state. The lower HID/XInput layers skip
// their own rewrite while the wrapper is live so one input is never
// deadzoned twice (which would stack the effect).

namespace crdeadzone {

void MarkWrapperActive();
bool WrapperRecentlyActive();
// Test hook: override the clock (milliseconds).
void SetTickOverride(uint64_t ms);
void ClearTickOverride();

}  // namespace crdeadzone
