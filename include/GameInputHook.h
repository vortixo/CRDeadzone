#pragma once

// GameInput high-level layer. CONTROL Resonant ships GDK GameInput statically
// linked, but GameInput.lib is only a loader shim: polling always goes through
// the inbox GameInput runtime DLL (GameInputInitialize / v0 GameInputCreate
// exports). Hooking those exports and wrapping the returned objects covers
// every controller the runtime supports (GDK and SCE types) with Microsoft's
// own COM ABI, so game updates cannot break it.
//
// Version safety: IGameInput/IGameInputReading vtable layouts differ between
// GameInput API versions. Slots are patched only for versions whose layouts
// are confirmed (v2/v3); anything else is detected, logged, and passed
// through untouched.

namespace crdeadzone {

class Config;

bool InstallGameInputHooks(const Config& config);
void RemoveGameInputHooks();

}  // namespace crdeadzone
