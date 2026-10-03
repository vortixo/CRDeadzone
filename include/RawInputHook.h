#pragma once

// RawInput hook layer. The game imports GetRawInputData/RegisterRawInputDevices
// from USER32, so this hook point is guaranteed present. HID-type reports from
// joystick/gamepad collections are remapped in place via the HidReport core;
// everything else (keyboard, mouse, unknown collections) passes through.

namespace crdeadzone {

class Config;

bool InstallRawInputHooks(const Config& config);
void RemoveRawInputHooks();

}  // namespace crdeadzone
