// HID specs: collection classification, the DecideMapping table
// (including the DualSense-style Z/Rz look pair with no Rx/Ry triggers),
// and normalization over logical ranges with exact pins.

#include "Test.h"

#include "HidMapping.h"

namespace {

using namespace crdeadzone;

void Collections() {
  EXPECT_TRUE(IsGamepadCollection(0x04));  // joystick
  EXPECT_TRUE(IsGamepadCollection(0x05));  // gamepad
  EXPECT_TRUE(!IsGamepadCollection(0x01));  // pointer
  EXPECT_TRUE(!IsGamepadCollection(0x02));  // mouse
  EXPECT_TRUE(!IsGamepadCollection(0x06));  // keyboard
  EXPECT_TRUE(!IsGamepadCollection(0x00));
}

void Mapping() {
  // Full DualSense-style set: look on Z/Rz, triggers on Rx/Ry.
  const auto ds4 = DecideMapping(true, true, true, true, true, true);
  EXPECT_TRUE(ds4.usable && ds4.lookUsesZRz && ds4.triggersAreRxRy);
  // Xbox-style: look on Rx/Ry, no separate triggers.
  const auto xbox = DecideMapping(true, true, false, true, true, false);
  EXPECT_TRUE(xbox.usable && !xbox.lookUsesZRz && !xbox.triggersAreRxRy);
  // Z/Rz look pair without Rx/Ry: still a usable gamepad, no triggers.
  const auto noTrig = DecideMapping(true, true, true, false, false, true);
  EXPECT_TRUE(noTrig.usable && noTrig.lookUsesZRz && !noTrig.triggersAreRxRy);
  // No look pair at all: unusable, never remapped.
  EXPECT_TRUE(!DecideMapping(true, true, false, false, false, false).usable);
  EXPECT_TRUE(!DecideMapping(true, true, true, false, false, false).usable);
  // No movement pair: unusable even with a full look set.
  EXPECT_TRUE(!DecideMapping(false, true, true, true, true, true).usable);
}

void Normalization() {
  EXPECT_EQ(HidToSigned(0, 0, 255), -1.0f);
  EXPECT_EQ(HidToSigned(255, 0, 255), 1.0f);
  // Midpoint of an even range quantizes just above zero; pin the band.
  EXPECT_NEAR(HidToSigned(128, 0, 255), 0.0f, 0.01f);
  EXPECT_TRUE(HidToSigned(64, 0, 255) < 0.0f);
  EXPECT_TRUE(HidToSigned(192, 0, 255) > 0.0f);
  // Out-of-range reports clamp instead of wrapping.
  EXPECT_EQ(HidToSigned(-5, 0, 255), -1.0f);
  EXPECT_EQ(HidToSigned(300, 0, 255), 1.0f);
  EXPECT_EQ(HidToUnit(0, 0, 255), 0.0f);
  EXPECT_EQ(HidToUnit(255, 0, 255), 1.0f);
  EXPECT_EQ(HidToUnit(-5, 0, 255), 0.0f);
  EXPECT_EQ(HidToUnit(300, 0, 255), 1.0f);
  // Exact pins: 0.5 * 255 + 0.5 == 128.0, no float ambiguity.
  EXPECT_EQ(SignedToHid(0.0f, 0, 255), 128);
  EXPECT_EQ(SignedToHid(-1.0f, 0, 255), 0);
  EXPECT_EQ(SignedToHid(1.0f, 0, 255), 255);
  EXPECT_EQ(UnitToHid(0.0f, 0, 255), 0);
  EXPECT_EQ(UnitToHid(1.0f, 0, 255), 255);
  // Degenerate ranges read as zero, never divide.
  EXPECT_EQ(HidToSigned(5, 10, 10), 0.0f);
  EXPECT_EQ(HidToUnit(5, 10, 10), 0.0f);
}

}  // namespace

void HidTests() {
  crtest::Suite("hid-collections", Collections);
  crtest::Suite("hid-mapping", Mapping);
  crtest::Suite("hid-normalization", Normalization);
}
