#pragma once

// Internal: version-aware GameInput object wrapping. Implemented by
// GameInputWrapperV3.cpp when NuGet headers are available (verified by a
// configure-time try_compile probe), otherwise by the always-safe stub.

namespace crdeadzone {

enum class GameInputVersion { Unknown, V1, V2, V3 };

// Inspects a freshly created IGameInput object (typeless), wraps it in place
// when its version has confirmed vtable layouts (v2/v3), and returns the
// detected version. Never modifies v0/v1 or unknown objects.
class Config;
GameInputVersion ProbeAndWrapObject(void* gameInput, const Config* cfg);

}  // namespace crdeadzone
