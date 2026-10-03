// Configure-time probe: validates the exact GameInput v2 constructs used by
// GameInputWrapperV3.cpp. v2 layout equals v3 minus the trailing raw-report
// entry, so GetGamepadState shares slot 18.
#include <cstddef>

#include <GameInput.h>

static_assert(offsetof(GameInput::v2::GameInputGamepadState, buttons) == 0);
static_assert(offsetof(GameInput::v2::GameInputGamepadState, leftTrigger) == 4);
static_assert(offsetof(GameInput::v2::GameInputGamepadState, rightTrigger) == 8);
static_assert(offsetof(GameInput::v2::GameInputGamepadState, leftThumbstickX) == 12);
static_assert(offsetof(GameInput::v2::GameInputGamepadState, leftThumbstickY) == 16);
static_assert(offsetof(GameInput::v2::GameInputGamepadState, rightThumbstickX) == 20);
static_assert(offsetof(GameInput::v2::GameInputGamepadState, rightThumbstickY) == 24);
static_assert(sizeof(GameInput::v2::GameInputGamepadState) == 28);

int main() {
  auto giCurrent = &GameInput::v2::IGameInput::GetCurrentReading;
  auto giNext = &GameInput::v2::IGameInput::GetNextReading;
  auto giPrev = &GameInput::v2::IGameInput::GetPreviousReading;
  auto giReg = &GameInput::v2::IGameInput::RegisterReadingCallback;
  auto rdState = &GameInput::v2::IGameInputReading::GetGamepadState;
  (void)giCurrent;
  (void)giNext;
  (void)giPrev;
  (void)giReg;
  (void)rdState;
  const auto& iid = __uuidof(GameInput::v2::IGameInput);
  (void)iid;
  GameInput::v2::GameInputCallbackToken tok{};
  (void)tok;
  return 0;
}
