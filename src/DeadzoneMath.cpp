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

}  // namespace crdeadzone
