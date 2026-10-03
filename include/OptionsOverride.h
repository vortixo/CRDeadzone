#pragma once

// Best-effort discovery of the game's own deadzone tunables
// ("deadZone", "slideDeadzone", "stickInputPowerFactor").
//
// v1 behavior is intentionally conservative: it locates the string anchors
// and any code referencing them and records everything in CRDeadzone.log so
// exact patches can be validated for future game builds. It never writes to
// game memory unless a pattern matches exactly with a validated size, so a
// game update can at worst disable this module, never crash the game.

namespace crdeadzone {

void DiscoverGameDeadzones();

}  // namespace crdeadzone
