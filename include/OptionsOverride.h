#pragma once

// Best-effort discovery of the game's own deadzone tunables
// ("deadZone", "slideDeadzone", "stickInputPowerFactor", ...).
//
// v1 behavior is intentionally conservative: it locates the string anchors
// and any code referencing them and records everything in CRDeadzone.log so
// exact patches can be validated for future game builds.
//
// v1.2 adds read-only capture: the call targets in the anchor flow windows
// are clustered (shared binder ranks above per-name helpers) and the top
// candidates hooked with passthrough detours. Watched option names are
// logged with their arguments; nothing is written to game memory. If no
// candidate is found, or registration already ran, the log says so and the
// mod carries on with the input layers.

namespace crdeadzone {

void DiscoverGameDeadzones();

// Installs the read-only tunable-capture hooks. Safe to call once after
// MH_Initialize; returns true when at least one candidate was hooked.
bool InstallTunableCapture();

}  // namespace crdeadzone
