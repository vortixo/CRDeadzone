#pragma once

// XInput hook layer. Covers Xbox controllers and anything presenting as
// XInput (including Steam Input emulation). Installed per-DLL variant
// (xinput1_4, xinput1_3, xinput9_1_0); missing variants are skipped silently.
// Harmless when the game polls via GameInput instead.

namespace crdeadzone {

class Config;

bool InstallXInputHooks(const Config& config);
void RemoveXInputHooks();

}  // namespace crdeadzone
