#pragma once

// Direct-HID layer. Tracks HID device handles opened via CreateFileW and
// remaps synchronous ReadFile input reports through the HidReport core.
// File reads that are not from tracked HID handles pass through untouched.

namespace crdeadzone {

class Config;

bool InstallHidReadHooks(const Config& config);
void RemoveHidReadHooks();

}  // namespace crdeadzone
