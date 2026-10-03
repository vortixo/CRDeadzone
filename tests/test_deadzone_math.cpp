// Deadzone math specs: every branch of ApplyAxialDeadzone,
// ApplyRadialDeadzone, ApplyTriggerDeadzone, CurvePowerForChoice, the
// XInput conversion helpers, and the shared MakeGamepadSettings /
// ApplyGamepadState path all input layers use.

#include "Test.h"

#include "DeadzoneMath.h"

namespace {

using namespace crdeadzone;

void Axial() {
  // Basic rescale: below inner reads 0, outer reads full, linear between.
  EXPECT_EQ(ApplyAxialDeadzone(0.05f, 0.15f, 1.0f), 0.0f);
  EXPECT_NEAR(ApplyAxialDeadzone(1.0f, 0.15f, 1.0f), 1.0f, 1e-6f);
  EXPECT_NEAR(ApplyAxialDeadzone(-1.0f, 0.15f, 1.0f), -1.0f, 1e-6f);
  // (0.575 - 0.15) / (1.0 - 0.15) == 0.5 exactly.
  EXPECT_NEAR(ApplyAxialDeadzone(0.575f, 0.15f, 1.0f), 0.5f, 1e-6f);
  // Zero deadzone is a pure passthrough, including zero itself.
  EXPECT_EQ(ApplyAxialDeadzone(0.0f, 0.0f, 1.0f), 0.0f);
  EXPECT_NEAR(ApplyAxialDeadzone(0.5f, 0.0f, 1.0f), 0.5f, 1e-6f);
  // Degenerate range: any deflection reads full, zero stays zero.
  EXPECT_EQ(ApplyAxialDeadzone(0.3f, 0.5f, 0.5f), 1.0f);
  EXPECT_EQ(ApplyAxialDeadzone(-0.3f, 0.5f, 0.5f), -1.0f);
  EXPECT_EQ(ApplyAxialDeadzone(0.0f, 0.5f, 0.5f), 0.0f);
  EXPECT_EQ(ApplyAxialDeadzone(0.2f, 0.6f, 0.4f), 1.0f);
}

void Radial() {
  float x = 0.05f, y = 0.0f;
  ApplyRadialDeadzone(x, y, 0.15f, 1.0f, 1.0f);
  EXPECT_TRUE(x == 0.0f && y == 0.0f);

  x = 1.0f;
  y = 0.0f;
  ApplyRadialDeadzone(x, y, 0.15f, 1.0f, 1.0f);
  EXPECT_NEAR(x, 1.0f, 1e-6f);
  EXPECT_NEAR(y, 0.0f, 1e-6f);

  // Magnitude 1.0, no deadzone, linear: direction preserved.
  x = 0.6f;
  y = 0.8f;
  ApplyRadialDeadzone(x, y, 0.0f, 1.0f, 1.0f);
  EXPECT_NEAR(x, 0.6f, 1e-6f);
  EXPECT_NEAR(y, 0.8f, 1e-6f);

  // Power curve applies to the rescaled magnitude: 0.5^2 == 0.25.
  x = 0.3f;
  y = 0.4f;
  ApplyRadialDeadzone(x, y, 0.0f, 1.0f, 2.0f);
  EXPECT_NEAR(x, 0.15f, 1e-6f);
  EXPECT_NEAR(y, 0.2f, 1e-6f);

  x = 0.0f;
  y = 0.0f;
  ApplyRadialDeadzone(x, y, 0.15f, 1.0f, 1.0f);
  EXPECT_TRUE(x == 0.0f && y == 0.0f);

  // Zero input with zero deadzone must stay (0,0): no 0/0 NaN.
  x = 0.0f;
  y = 0.0f;
  ApplyRadialDeadzone(x, y, 0.0f, 1.0f, 1.0f);
  EXPECT_TRUE(x == 0.0f && y == 0.0f);

  // Degenerate range behaves like the axial one.
  x = 0.2f;
  y = 0.0f;
  ApplyRadialDeadzone(x, y, 0.5f, 0.5f, 1.0f);
  EXPECT_TRUE(x == 0.0f && y == 0.0f);
  x = 1.0f;
  y = 0.0f;
  ApplyRadialDeadzone(x, y, 0.5f, 0.5f, 1.0f);
  EXPECT_NEAR(x, 1.0f, 1e-6f);

  // Non-positive power falls back to linear.
  x = 0.3f;
  y = 0.4f;
  ApplyRadialDeadzone(x, y, 0.0f, 1.0f, 0.0f);
  EXPECT_NEAR(x, 0.3f, 1e-6f);
  EXPECT_NEAR(y, 0.4f, 1e-6f);
}

void Trigger() {
  EXPECT_EQ(ApplyTriggerDeadzone(0.02f, 0.05f), 0.0f);
  EXPECT_EQ(ApplyTriggerDeadzone(0.0f, 0.05f), 0.0f);
  EXPECT_NEAR(ApplyTriggerDeadzone(1.0f, 0.05f), 1.0f, 1e-6f);
  // (0.525 - 0.05) / (1 - 0.05) == 0.5 exactly.
  EXPECT_NEAR(ApplyTriggerDeadzone(0.525f, 0.05f), 0.5f, 1e-6f);
  EXPECT_EQ(ApplyTriggerDeadzone(0.5f, 0.0f), 0.5f);
  // Degenerate deadzone swallows everything, including full press.
  EXPECT_EQ(ApplyTriggerDeadzone(1.0f, 1.0f), 0.0f);
}

void Curves() {
  EXPECT_EQ(CurvePowerForChoice(0, 100), 1.0f);
  EXPECT_TRUE(CurvePowerForChoice(1, 100) > 1.0f);
  EXPECT_TRUE(CurvePowerForChoice(2, 100) > CurvePowerForChoice(1, 100));
  EXPECT_NEAR(CurvePowerForChoice(3, 200), 2.0f, 1e-6f);
  EXPECT_NEAR(CurvePowerForChoice(3, 9999), 3.0f, 1e-6f);
  EXPECT_NEAR(CurvePowerForChoice(3, 0), 0.5f, 1e-6f);
  // Unknown choice falls back to linear, never to garbage.
  EXPECT_EQ(CurvePowerForChoice(99, 100), 1.0f);
}

void XInputConversion() {
  EXPECT_EQ(XInputShortToFloat(32767), 1.0f);
  EXPECT_EQ(XInputShortToFloat(-32768), -1.0f);
  EXPECT_EQ(XInputShortToFloat(0), 0.0f);
  EXPECT_EQ(XInputShortToFloat(99999), 1.0f);
  EXPECT_EQ(XInputShortToFloat(-99999), -1.0f);
  EXPECT_EQ(FloatToXInputShort(1.0f), 32767);
  // Truncation toward zero makes the negative extreme asymmetric (-32767,
  // not -32768); pinned so any change is deliberate.
  EXPECT_EQ(FloatToXInputShort(-1.0f), -32767);
  EXPECT_EQ(FloatToXInputShort(2.0f), 32767);
  EXPECT_EQ(FloatToXInputShort(-2.0f), -32767);
  // 0.5 * 32767 == 16383.5, truncated.
  EXPECT_EQ(FloatToXInputShort(0.5f), 16383);
  // Round trip stays within 1 LSB.
  EXPECT_NEAR(XInputShortToFloat(FloatToXInputShort(0.5f)), 0.5f, 1e-4f);
}

void GamepadState() {
  const auto linked = MakeGamepadSettings(15, 100, 0, 10, 100, 0, 5, 5, 100, false, false);
  EXPECT_EQ(linked.lookInner, linked.moveInner);
  EXPECT_EQ(linked.lookOuter, linked.moveOuter);
  EXPECT_EQ(linked.triggerRight, linked.triggerLeft);

  const auto split = MakeGamepadSettings(15, 90, 1, 10, 80, 2, 5, 20, 100, true, true);
  EXPECT_NEAR(split.moveInner, 0.15f, 1e-6f);
  EXPECT_NEAR(split.moveOuter, 0.9f, 1e-6f);
  EXPECT_NEAR(split.lookInner, 0.10f, 1e-6f);
  EXPECT_NEAR(split.lookOuter, 0.8f, 1e-6f);
  EXPECT_TRUE(split.movePower < split.lookPower);
  EXPECT_NEAR(split.triggerRight, 0.20f, 1e-6f);

  // Custom curve choice honors the shared exponent slider.
  const auto custom = MakeGamepadSettings(15, 100, 3, 10, 100, 3, 5, 5, 200, true, true);
  EXPECT_NEAR(custom.movePower, 2.0f, 1e-6f);
  EXPECT_NEAR(custom.lookPower, 2.0f, 1e-6f);

  // End to end through the real settings builder: movement deadzoned, look
  // with zero inner passes through, triggers rescaled.
  float lx = 0.05f, ly = 0.0f, rx = 0.05f, ry = 0.0f, lt = 0.02f, rt = 0.9f;
  const auto gs =
      MakeGamepadSettings(15, 100, 0, 0, 100, 0, 5, 5, 100, true, false);
  ApplyGamepadState(lx, ly, rx, ry, lt, rt, gs);
  EXPECT_TRUE(lx == 0.0f && ly == 0.0f);
  EXPECT_NEAR(rx, 0.05f, 1e-6f);
  EXPECT_EQ(lt, 0.0f);
  EXPECT_NEAR(rt, (0.9f - 0.05f) / 0.95f, 1e-6f);
}

}  // namespace

void DeadzoneMathTests() {
  crtest::Suite("axial", Axial);
  crtest::Suite("radial", Radial);
  crtest::Suite("trigger", Trigger);
  crtest::Suite("curves", Curves);
  crtest::Suite("xinput-convert", XInputConversion);
  crtest::Suite("gamepad-state", GamepadState);
}
