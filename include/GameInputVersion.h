#pragma once

// Internal: version-aware GameInput object wrapping. Implemented by
// GameInputWrapperV3.cpp when NuGet headers are available (verified by a
// configure-time try_compile probe), otherwise by the always-safe stub.

namespace crdeadzone {

enum class GameInputVersion { Unknown, V1, V2, V3 };

// Inspects a freshly created IGameInput object (typeless), wraps it in place
// when it speaks the confirmed v3 layout, and returns the detected version.
// Anything else is never modified.
class Config;
GameInputVersion ProbeAndWrapObject(void* gameInput, const Config* cfg);

}  // namespace crdeadzone
