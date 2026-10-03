#include "DeadzoneMath.h"

#include <cmath>

namespace crdeadzone {

float ApplyAxialDeadzone(float value, float inner, float outer) {
  if (outer <= inner) return (value >= 0.0f ? 1.0f : -1.0f) * (value != 0.0f ? 1.0f : 0.0f);
  const float mag = std::fabs(value);
  if (mag < inner) return 0.0f;
  if (mag >= outer) return value >= 0.0f ? 1.0f : -1.0f;
  const float sign = value >= 0.0f ? 1.0f : -1.0f;
  return sign * (mag - inner) / (outer - inner);
}

void ApplyRadialDeadzone(float& x, float& y, float inner, float outer, float power) {
  if (outer <= inner) outer = inner + 1e-6f;
  if (power <= 0.0f) power = 1.0f;
  const float mag = std::sqrt(x * x + y * y);
  if (mag < inner || mag == 0.0f) {
    x = 0.0f;
    y = 0.0f;
    return;
  }
  const float nx = x / mag;
  const float ny = y / mag;
  float scaled;
  if (mag >= outer) {
    scaled = 1.0f;
  } else {
    scaled = (mag - inner) / (outer - inner);
    scaled = std::pow(scaled, power);
  }
  if (scaled > 1.0f) scaled = 1.0f;
  x = nx * scaled;
  y = ny * scaled;
}

float ApplyTriggerDeadzone(float value, float deadzone) {
  if (value < deadzone) return 0.0f;
  if (deadzone >= 1.0f) return 0.0f;
  float out = (value - deadzone) / (1.0f - deadzone);
  if (out > 1.0f) out = 1.0f;
  return out;
}

float CurvePowerForChoice(int choice, int customSlider) {
  switch (static_cast<CurvePreset>(choice)) {
    case CurvePreset::Mild:
      return 1.3f;
    case CurvePreset::Aggressive:
      return 2.0f;
    case CurvePreset::Custom: {
      float p = static_cast<float>(customSlider) / 100.0f;
      if (p < 0.5f) p = 0.5f;
      if (p > 3.0f) p = 3.0f;
      return p;
    }
    case CurvePreset::Linear:
    default:
      return 1.0f;
  }
}

float XInputShortToFloat(int s) {
  if (s < -32768) s = -32768;
  if (s > 32767) s = 32767;
  return s < 0 ? static_cast<float>(s) / 32768.0f : static_cast<float>(s) / 32767.0f;
}

int FloatToXInputShort(float f) {
  if (f > 1.0f) f = 1.0f;
  if (f < -1.0f) f = -1.0f;
  return static_cast<int>(f * 32767.0f);
}

void ApplyGamepadState(float& lx, float& ly, float& rx, float& ry, float& lt, float& rt,
                       const GamepadSettings& s) {
  ApplyRadialDeadzone(lx, ly, s.moveInner, s.moveOuter, s.movePower);
  ApplyRadialDeadzone(rx, ry, s.lookInner, s.lookOuter, s.lookPower);
  lt = ApplyTriggerDeadzone(lt, s.triggerLeft);
  rt = ApplyTriggerDeadzone(rt, s.triggerRight);
}

GamepadSettings MakeGamepadSettings(int moveDz, int moveOuter, int moveCurve, int lookDz,
                                    int lookOuter, int lookCurve, int trigL, int trigR,
                                    int customPower, bool perStick, bool trigSeparate) {
  GamepadSettings s;
  s.moveInner = moveDz / 100.0f;
  s.moveOuter = moveOuter / 100.0f;
  s.movePower = CurvePowerForChoice(moveCurve, customPower);
  s.lookInner = s.moveInner;
  s.lookOuter = s.moveOuter;
  s.lookPower = s.movePower;
  if (perStick) {
    s.lookInner = lookDz / 100.0f;
    s.lookOuter = lookOuter / 100.0f;
    s.lookPower = CurvePowerForChoice(lookCurve, customPower);
  }
  s.triggerLeft = trigL / 100.0f;
  s.triggerRight = trigSeparate ? trigR / 100.0f : s.triggerLeft;
  return s;
}

}  // namespace crdeadzone
