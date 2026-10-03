#pragma once

// GameInput hook layer. CONTROL Resonant polls controllers through GameInput
// (GamepadType::GDK / GamepadType::SCE in the exe, GameInputCreate import,
// no XInputGetState import), so this is the primary input path.
//
// Implementation hooks GameInputCreate, captures the IGameInput interface and
// detours the reading path to apply deadzones. Requires the official
// GameInput.h from the Windows SDK at build time; otherwise this module
// compiles to a logged no-op and the XInput + Options paths still work.

namespace crdeadzone {

class Config;

bool InstallGameInputHooks(const Config& config);
void RemoveGameInputHooks();

}  // namespace crdeadzone
