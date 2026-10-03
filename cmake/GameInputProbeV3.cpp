// Configure-time probe: validates the exact GameInput v3 constructs used by
// GameInputWrapperV3.cpp against the real headers. If this does not compile,
// v3 support is compiled out (safe) instead of breaking the build.
#include <cstddef>

#include <GameInput.h>

static_assert(offsetof(GameInput::v3::GameInputGamepadState, buttons) == 0);
static_assert(offsetof(GameInput::v3::GameInputGamepadState, leftTrigger) == 4);
static_assert(offsetof(GameInput::v3::GameInputGamepadState, rightTrigger) == 8);
static_assert(offsetof(GameInput::v3::GameInputGamepadState, leftThumbstickX) == 12);
static_assert(offsetof(GameInput::v3::GameInputGamepadState, leftThumbstickY) == 16);
static_assert(offsetof(GameInput::v3::GameInputGamepadState, rightThumbstickX) == 20);
static_assert(offsetof(GameInput::v3::GameInputGamepadState, rightThumbstickY) == 24);
static_assert(sizeof(GameInput::v3::GameInputGamepadState) == 28);

int main() {
  auto giCurrent = &GameInput::v3::IGameInput::GetCurrentReading;
  auto giNext = &GameInput::v3::IGameInput::GetNextReading;
  auto giPrev = &GameInput::v3::IGameInput::GetPreviousReading;
  auto giReg = &GameInput::v3::IGameInput::RegisterReadingCallback;
  auto rdState = &GameInput::v3::IGameInputReading::GetGamepadState;
  (void)giCurrent;
  (void)giNext;
  (void)giPrev;
  (void)giReg;
  (void)rdState;
  const auto& iid = __uuidof(GameInput::v3::IGameInput);
  (void)iid;
  GameInput::v3::GameInputCallbackToken tok{};
  (void)tok;
  return 0;
}
