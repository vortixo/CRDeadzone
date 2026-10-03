#pragma once

#include "HidMapping.h"

// HID report remap core. Operates on raw HID input reports using Microsoft's
// HidP usage APIs, so no per-device byte offsets are assumed.
//
// Axis mapping (logged per device, DualSense/DS4 convention):
//   movement = GenericDesktop X + Y
//   look     = GenericDesktop Z + Rz, else Rx + Ry
//   triggers = Rx + Ry (one-sided), only when look took Z + Rz
// Only joystick/gamepad collections (UsagePage 0x01, Usage 0x04/0x05) are
// touched; mice/keyboards are classified and skipped.

namespace crdeadzone {

struct Settings;
struct DeviceEntry;

DeviceEntry* GetOrAddDevice(void* key, void* ppd, bool ownsPpd, unsigned collection);
bool HasDevice(void* key);
void RemoveDevice(void* key);

// Applies current settings to a raw input report in place. No-op when the
// device is unknown/unusable or the GameInput wrapper is live.
bool RemapHidReport(void* key, uint8_t* report, size_t len, const Settings& s);
void LogDeviceOnce(void* key, const char* where, unsigned collection);

}  // namespace crdeadzone
