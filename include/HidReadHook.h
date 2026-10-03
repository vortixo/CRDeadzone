#pragma once

// Direct-HID layer. Intercepts the GAME MODULE's own ReadFile/CloseHandle
// imports (IAT patching: pointer swap, no thread freezing, other modules
// untouched) and remaps synchronous HID input reports through HidReport.
// File reads that are not from tracked HID handles pass through untouched,
// with a negative cache keeping the hot path to two hash lookups.

namespace crdeadzone {

class Config;

bool InstallHidReadHooks(const Config& config);
void RemoveHidReadHooks();

}  // namespace crdeadzone
