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

}  // namespace crdeadzone
