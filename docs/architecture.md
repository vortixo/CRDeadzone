# Architecture

How CRDeadzone applies deadzones, and why it uses five layers.

## Layer stack

CONTROL Resonant polls controllers through several paths, so the mod
layers hooks (first live layer wins; the rest stand down via
`WrapperRecentlyActive()` in `src/Activity.cpp`, so input is never
deadzoned twice):

1. **GameInput wrapper (v2/v3)** — `src/GameInputHook.cpp` hooks the
   runtime's `GameInputInitialize` / `GameInputCreate` exports and wraps
   returned objects with version-exact vtable copies (slots validated
   against the Microsoft.GameInput headers at build time in
   `CMakeLists.txt`). Rewrites `GamepadState` sticks/triggers directly.
   v0/v1 objects are detected, logged, and passed through.
2. **RawInput HID remap** — `src/RawInputHook.cpp` hooks
   `GetRawInputData` (a guaranteed `USER32.dll` import) and remaps
   joystick/gamepad HID reports via `HidP_` usage APIs. Mice and
   keyboards are classified and never touched (`RIM_TYPEHID` +
   usage page `0x01` filter).
3. **Direct-HID ReadFile remap** — `src/HidReadHook.cpp` patches the game
   module's own `ReadFile`/`CloseHandle` IAT slots (pointer swap, no
   thread freeze) and remaps synchronous reads the same way. Covers
   polling that bypasses RawInput. Overlapped (`ov != NULL`) reads pass
   through.
4. **XInput interception** — `src/XInputHook.cpp` hooks `XInputGetState`
   on every loaded XInput variant plus the undocumented ordinal-100
   entry. Fires for XInput polling paths (wrappers, Steam virtual pads).
   The game's ordinal-2 import is resolved by name at runtime and logged,
   never assumed.
5. **Game tunable capture (read-only)** — `src/OptionsOverride.cpp`
   clusters call targets in the anchor flow windows (shared binder ranks
   above per-name helpers) and hooks the top candidates with passthrough
   detours. Bindings of `deadZone`, `slideDeadzone`,
   `stickInputPowerFactor` and the input curves are logged with their
   game-side addresses. Nothing is written; the log tells the next
   version exactly where to apply values, on every input path.

Shared math lives in `src/DeadzoneMath.cpp` (pure, unit tested):
radial stick deadzones with response-curve power, one-sided trigger
rescaling. HID report parsing lives in `src/HidReport.cpp` /
`src/HidMapping.cpp` (usage-based, no per-device byte offsets).

## Common infrastructure

- `include/MinHookWrapper.h` — RAII wrappers following MinHook best
  practices: `MinHookLibrary` (init/uninit), `HookHandle`
  (auto disable/remove), `HookBatch` (`MH_QueueEnableHook` +
  `MH_ApplyQueued`, single thread suspension). Targeted disable per
  layer, never global `MH_ALL_HOOKS` side effects between layers.
- `include/HookLayer.h` — common base for all layers: config reference,
  install state, throttled logging.
- `src/Logger.cpp` — thread-safe file log next to the DLL
  (`CRDeadzone.log`). `std::format` + `source_location`.
- `src/Config.cpp` — INI parsing (`string_view` + `from_chars`),
  content-hash change polling, live reload every 500 ms.

## Update resilience

Everything is signature/address based — no hardcoded offsets. If a game
update moves things, the affected layer logs the miss and disables
itself instead of crashing.
