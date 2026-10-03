# CRDeadzone - Controller Deadzone Mod for CONTROL Resonant

Adjust controller stick and trigger deadzones **inside the game** through the
CRModMenu MODS tab. No Steam Input remapping, no external tools. Settings apply
instantly; the game never needs a restart.

## Requirements

- Steam version of CONTROL Resonant (tested on 1.4.0, build 25600401)
- [crloader (ftg DLL Mod Loader)](https://www.nexusmods.com/controlresonant/mods/9) - loads `crmods/*.dll`
- [CRModMenu](https://www.nexusmods.com/controlresonant/mods/35) v1.3.0+ - renders the in-game settings tab

## Install

1. Install crloader and CRModMenu per their own instructions.
2. Download `CRDeadzone-vX.Y.Z.zip` from GitHub Releases.
3. Extract the archive into the game folder (the one with
   CONTROLResonant.exe) so you get `crmods/CRDeadzone/CRDeadzone.dll` and
   `crmods/CRDeadzone/deadzone.menu.json`.
4. Launch the game, open Options, switch to the MODS tab, expand
   Controller Deadzone, and tune away.

Linux / Steam Deck (Proton): set the game's launch options to
`WINEDLLOVERRIDES="winmm=n,b" %command%` (required by crloader).

## Settings

| Setting | Range | Default | Notes |
|---|---|---|---|
| Movement inner deadzone | 0-50% | 15% | Left stick; raise for drift |
| Movement outer threshold | 50-100% | 100% | Lower if the stick can't reach full deflection |
| Movement response curve | Linear/Mild/Aggressive/Custom | Linear | Mid-travel shape |
| Look inner deadzone | 0-50% | 10% | Right stick (camera/aim) |
| Look outer threshold | 50-100% | 100% | Same as movement |
| Look response curve | Linear/Mild/Aggressive/Custom | Linear | Same as movement |
| Left trigger deadzone | 0-50% | 5% | LT activation point |
| Right trigger deadzone | 0-50% | 5% | RT activation point |
| Custom curve exponent | 50-300 | 100 | 100 = linear; used by Custom curves |
| Separate stick settings | on/off | on | Off = look follows movement |
| Separate trigger settings | on/off | off | Off = RT follows LT |

Sensitivity is **not** duplicated here: the game already exposes stick
sensitivity in its own options menu.

Values are stored in `crmods/CRDeadzone/ModMenuConfig/crdeadzone.ini` by
CRModMenu and re-read every 500 ms, so edits (in-game or external) apply live.

## How it works

CONTROL Resonant polls controllers through a GDK GameInput layer
(`GamepadType::GDK` / `GamepadType::SCE`), direct HID (PlayStation feature
reports), RawInput (keyboard/mouse), and a single XInput import used for
rumble/capabilities - there is **no** `XInputGetState` import, so classic
XInput wrapping alone cannot work. The mod therefore layers (first live layer
wins; the rest stand down automatically so input is never deadzoned twice):

1. **GameInput wrapper (v2/v3)** - hooks the runtime's `GameInputInitialize`
   export and wraps returned objects with version-exact vtable copies
   (slots validated against the Microsoft.GameInput headers at build time).
   Rewrites `GamepadState` sticks/triggers directly. v0/v1 objects are
   detected, logged, and passed through.
2. **RawInput HID remap** - hooks `GetRawInputData` (guaranteed import) and
   remaps joystick/gamepad HID reports via `HidP_` usage APIs. Mice and
   keyboards are classified and never touched.
3. **Direct-HID ReadFile remap** - tracks HID device handles and remaps
   synchronous reads the same way. Covers polling that bypasses RawInput.
4. **XInput interception** - hooks `XInputGetState` on every loaded XInput
   variant plus the undocumented ordinal-100 entry. Fires for any XInput
   polling path (wrappers, Steam virtual pads). The game's ordinal-2 import
   is resolved by name at runtime and logged, never assumed.
5. **Game tunable discovery (read-only)** - locates the `deadZone`,
   `slideDeadzone`, `stickInputPowerFactor`, and input-curve anchors in memory
   and logs every referencing code site. Nothing is written; the log gives
   exact anchors for validated patches in future versions.

Everything is signature/address based - no hardcoded offsets. If a game update
moves things, the affected layer logs the miss and disables itself instead of
crashing. Attach `crmods/CRDeadzone/CRDeadzone.log` to bug reports.

Recommended: turn Steam Input **off** for this game so the native path (and
this mod) sees raw controller values instead of Steam-remapped ones.

## Known limitations (v1.1.x)

- The game applies its own built-in deadzone **after** this mod's layers. The
  outer threshold always takes effect; the inner deadzone visibly works down
  to the game's baseline, but lowering it below that baseline needs the
  built-in value itself overridden. The log's `options:` section records every
  code site referencing the game's tunables (`deadZone`, `slideDeadzone`,
  `stickInputPowerFactor`, ...); a named-getter hook is in the works to make
  the full range effective both ways.

## Build

Windows (MSVC, x64):

```bat
cmake -B build -A x64
cmake --build build --config Release --parallel
```

MinHook is fetched automatically via CMake FetchContent. Static CRT is used,
so players need no extra redistributable.

Logic tests + descriptor validation (any platform with g++/python3):

```sh
python3 tools/validate_menu.py deadzone.menu.json
python3 tools/test_validate_menu.py
g++ -std=c++20 -Wall -Wextra -Iinclude tests/test_main.cpp tests/test_deadzone_math.cpp \
  tests/test_config.cpp tests/test_hid.cpp tests/test_support.cpp \
  src/DeadzoneMath.cpp src/Config.cpp src/HidMapping.cpp src/Activity.cpp \
  src/PatternScanner.cpp src/Logger.cpp src/Disasm.cpp -o /tmp/test_deadzone_math
/tmp/test_deadzone_math
```

Releases are built by GitHub Actions on every `v*` tag and attached to the
GitHub Releases tab as `CRDeadzone-<tag>.zip`.

## Troubleshooting

- **No MODS tab**: install CRModMenu, and keep `deadzone.menu.json` next to
  the DLL (the tab only appears when a supporting mod is installed).
- **Settings do nothing**: open `CRDeadzone.log` - it states which hooks
  installed. If nothing installed, paste the log into an issue.
- **Game won't start with the mod**: create an empty file
  `crmods/CRDeadzone/disabled.txt` to make the DLL log and exit without
  hooking anything, then report with the log attached.
- **Steam controller glyphs wrong**: unrelated to this mod; see CRModMenu docs.

## License

MIT. MinHook (TsudaKageyu) is fetched at build time under its BSD license.
