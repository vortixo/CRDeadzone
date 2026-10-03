#pragma once

// Pure deadzone math, platform independent and unit tested.
// All stick inputs are normalized to [-1, 1], triggers to [0, 1].
// Deadzones are fractions in [0, 1).

namespace crdeadzone {

// Per-axis deadzone with rescaling: values below inner map to 0, values at or
// above outer map to +/-1, everything in between is linearly rescaled.
float ApplyAxialDeadzone(float value, float inner, float outer);

// Radial (magnitude based) deadzone for a stick. Direction is preserved;
// the magnitude is rescaled from [inner, outer] to [0, 1] and the response
// curve (power) is applied to the rescaled magnitude.
void ApplyRadialDeadzone(float& x, float& y, float inner, float outer, float power);

// Trigger deadzone for a [0, 1] input.
float ApplyTriggerDeadzone(float value, float deadzone);

// Response curve presets used by the menu choices.
enum class CurvePreset : int {
  Linear = 0,
  Mild = 1,
  Aggressive = 2,
  Custom = 3,
};

// Resolves a menu choice + custom slider value (50-300, 100 = linear)
// to a power exponent.
float CurvePowerForChoice(int choice, int customSlider);

// XInput SHORT (-32768..32767) normalization helpers.
float XInputShortToFloat(int s);
int FloatToXInputShort(float f);

struct GamepadSettings {
  float moveInner = 0.15f;
  float moveOuter = 1.0f;
  float movePower = 1.0f;
  float lookInner = 0.10f;
  float lookOuter = 1.0f;
  float lookPower = 1.0f;
  float triggerLeft = 0.05f;
  float triggerRight = 0.05f;
};

// Full gamepad state application shared by every input layer:
// left stick = movement, right stick = look, triggers one-sided.
// All values normalized (sticks [-1, 1], triggers [0, 1]).
void ApplyGamepadState(float& lx, float& ly, float& rx, float& ry, float& lt, float& rt,
                       const GamepadSettings& s);

// Builds GamepadSettings from raw menu values (percents, curve choices,
// custom slider 50-300, link switches). Pure and unit tested.
GamepadSettings MakeGamepadSettings(int moveDz, int moveOuter, int moveCurve, int lookDz,
                                    int lookOuter, int lookCurve, int trigL, int trigR,
                                    int customPower, bool perStick, bool trigSeparate);

}  // namespace crdeadzone
