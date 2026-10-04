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

## Official + reference HID implementations

No public Sony PC HID spec exists (PlayStation support pages cover
pairing only). Closest to official, all mutually consistent:

- **Linux `hid-playstation.c`** (Sony-authored, Roderick Colenbrander):
  `DS_INPUT_REPORT_USB 0x01/64B`, `DS_INPUT_REPORT_BT 0x31/78B`,
  `DS_FEATURE_REPORT_CALIBRATION 0x05/41B`. Common payload
  (`struct dualsense_input_report`): sticks 0–3, triggers 4–5,
  counter 6, buttons/hat 7–10, seq 11–14, gyro 15–20, accel 21–26,
  timestamp 27–30. Gyro normalized to 1024 LSB/°/s, accel 8192 LSB/g;
  per-axis bias + plus/minus from the calibration feature report, with
  sanity checks that disable calibration on invalid data.
- **SDL `SDL_hidapi_ps5.c`** (game-facing native-HID precedent):
  USB `0x01` parsed at `&data[1]`; Bluetooth `0x31` parsed at `&data[2]`
  (2-byte BT header stripped), 78B incl. trailing CRC-32. 10-byte
  minimal BT reports carry sticks+buttons only (no gyro) — a parser
  must size-dispatch, not assume full reports. Alternate-report and
  enhanced-mode variants exist (`use_alternate_report`, enhanced
  reports over BT). Same 1024/8192 scale constants.
- **`nondebug/dualsense`**: raw + parsed USB/BT report descriptors
  (`report-descriptor-usb.txt`, `-bluetooth.txt`); confirms the byte
  maps above. VID `054C`, PID `0CE6`.
- **Microsoft (official Windows path)**: register gamepad (`0x01/0x05`)
  + joystick (`0x01/0x04`) via `RegisterRawInputDevices`, read
  `WM_INPUT` with `GetRawInputData` (drain backlog with
  `GetRawInputBuffer`); `HidD_GetInputReport` polls current state;
  `ReadFile`/`WriteFile` on HID paths for reports. Xbox pads are not
  native HID — XInput remains the correct Xbox path, HID the
  DualSense path.

Implication for this mod: the `HidRead` hook must strip 0/1/2 header
bytes by (report ID, size) — `(0x01,64)→+1`, `(0x31,78)→+2`,
`(0x01,10)→simple packet, sticks only` — then apply the common
offsets. Calibration (feature `0x05`) is gyro-only scope, parked.

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
