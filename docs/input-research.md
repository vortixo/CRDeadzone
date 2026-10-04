# Input research

Findings from static analysis of `CONTROLResonant.exe` (Steam build
25600401) and related mods. Evidence for future work (native DualSense
support, tunable overwrite, gyro).

## Game input paths (binary evidence)

- **GameInput**: `GameInputCreate` string, `GamepadType::GDK` /
  `GamepadType::SCE` / `GamepadType::None`, `GameInput:Print Debug Info`.
  Runtime-loaded (`GameInput.dll`), not statically linked.
- **RawInput**: `GetRawInputData` + `RegisterRawInputDevices` imports
  from `USER32.dll`.
- **Direct HID**: `HID.DLL` imports include `HidD_GetAttributes`,
  `HidD_GetPreparsedData`, `HidP_GetCaps`, `HidP_GetValueCaps`,
  `HidD_GetFeature`, `HidD_SetFeature`.
- **XInput**: single import — ordinal 2 from `XINPUT1_4.dll`
  (resolved at runtime to `XInputGetState`). Wwise uses
  `XInputGetCapabilities` / `XInputSetState` for rumble.
- **libScePad (static)**: `Failed to initialize the libScePad_static
  library for PlayStation controller support`, plus `DualSense
  Controller (User %d) (ScePad Haptics)` / `DualShock 4 Controller (User
  %d) (ScePad Rumble)` strings and Wwise `AkScePad*Sink` sources. No
  `libScePad.dll` in imports — `scePadRead` / `scePadReadState` are
  internal calls, not hookable by export name.

## Game tunables (string anchors, all referenced by game code)

`deadZone`, `slideDeadzone`, `stickInputPowerFactor`,
`movementInputCurve` (+`GID`), `cameraInputCurve` (+`GID`),
`speedCameraInputCurve` (+`GID`), `moveInputRemapCurve`,
`stickSensitivityX/YSettingScaleCurve`, `maxInputAngle`
(`NoInput` variant). See `options:` lines in `CRDeadzone.log` for
per-build code sites.

## DualSense USB HID layout

Report ID `0x01` (64 bytes USB; Bluetooth uses `0x31`, 78 bytes with
CRC-32): bytes are `X, Y, Z, Rz, Rx, Ry` (0–255), then hat/buttons,
gyro (bytes 16–21), accel (22–27), touch, triggers. Our HID convention
(move X+Y, look Z+Rz, triggers Rx+Ry) matches this layout exactly.
Sources: `nondebug/dualsense` report descriptor, Linux
`hid-playstation.c` (`struct dualsense_input_report`), VID `054C`,
PID `0CE6` (USB).

## Related mods (concept reference)

- **SpecialK** treats input APIs as independent layers
  (`[Input.libScePad]`, `[Input.XInput]`, `[Input.Gamepad]`
  HID/DirectInput/WindowsGamingInput) — same separation this mod uses.
- **crpad** (Steam Controller 2 support, see `miscellaneous/`) shows
  the proven recipe for controller features in this game: resolve APIs
  at runtime (it uses `SteamAPI_SteamInput_v006`, incl.
  `GetMotionData` for gyro), pattern-scan game functions for camera
  injection (`engine.cam`), cache results, degrade gracefully when
  patterns miss.

## Known transport gaps (native DualSense)

Currently hooked: `GetRawInputData`/`RID_INPUT`,
synchronous `ReadFile`. Not hooked: `GetRawInputBuffer`, overlapped
`ReadFile`, `DeviceIoControl` (`IOCTL_HID_*`), `HidD_GetFeature` /
`HidD_GetInputReport`. A statically-linked libScePad may use any of
these, which is why native-DualSense logs (`vid=054c` tracking vs
skipping lines) decide what to hook next.
