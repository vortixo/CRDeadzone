# CRDeadzone - Controller Deadzone Mod for CONTROL Resonant

Adjust controller stick and trigger deadzones **inside the game** through the
CRModMenu MODS tab. No Steam Input remapping, no external tools. Settings apply
instantly; the game never needs a restart.

## Requirements

- Steam version of CONTROL Resonant (tested on 1.4.0, build 25600401)
- [crloader](https://www.nexusmods.com/controlresonant/mods/9) - loads `crmods/*.dll`
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

The game polls controllers through several paths (GameInput, HID,
RawInput, XInput), so the mod layers hooks — first live layer wins, the
rest stand down so input is never deadzoned twice:

1. **GameInput wrapper (v2/v3)** - rewrites `GamepadState` sticks/triggers.
2. **RawInput HID remap** - remaps joystick/gamepad HID reports in place.
3. **Direct-HID ReadFile remap** - covers polling that bypasses RawInput.
4. **XInput interception** - covers XInput polling paths and Steam virtual pads.
5. **Game tunable capture (read-only)** - logs where the game's own
   `deadZone` / `slideDeadzone` / `stickInputPowerFactor` tunables live,
   so a later version can drive them directly.

Details: [docs/architecture.md](docs/architecture.md).
Input-system research notes: [docs/input-research.md](docs/input-research.md).

Recommended: turn Steam Input **off** for this game so the native path (and
this mod) sees raw controller values instead of Steam-remapped ones.
Steam Input applies its own deadzone *before* the game sees anything:
with it on, lowering the inner deadzone below Steam's floor changes
nothing, by design of Steam, not of this mod.

## Known limitations

- The game applies its own built-in deadzone **after** this mod's layers.
  The outer threshold always takes effect; the inner deadzone visibly
  works down to the game's baseline. Overriding the built-in value itself
  (via the captured tunables above) is planned to make the full range
  effective both ways.

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
  tests/test_config.cpp tests/test_hid.cpp tests/test_support.cpp tests/test_tunable.cpp \
  src/DeadzoneMath.cpp src/Config.cpp src/HidMapping.cpp src/Activity.cpp \
  src/PatternScanner.cpp src/Logger.cpp src/Disasm.cpp src/TunableCapture.cpp \
  -o /tmp/test_deadzone_math
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

## Credits

- [MinHook](https://github.com/TsudaKageyu/minhook) by TsudaKageyu (BSD license) - API hooking, fetched at build time
- [crloader](https://www.nexusmods.com/controlresonant/mods/9) by fame2gin - DLL mod loader
- [CRModMenu](https://www.nexusmods.com/controlresonant/mods/35) by kkyleeb21 - in-game settings tab
- [Microsoft.GameInput](https://www.nuget.org/packages/Microsoft.GameInput/) (NuGet) - version-exact GameInput headers for build-time vtable validation
- [Microsoft Win32 Raw Input / HID documentation](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getrawinputdata) (`GetRawInputData`, `RAWINPUT`/`RIM_TYPEHID`, HID APIs)
- [SpecialK](https://wiki.special-k.info/en/Advanced/Input) by Kaldaien - prior art on separating input APIs (XInput / libScePad / HID) per game
- DualSense HID layout: [`nondebug/dualsense`](https://github.com/nondebug/dualsense) report descriptor and Linux `hid-playstation.c` (Sony Interactive Entertainment, GPL-2.0)

## License

MIT. MinHook (TsudaKageyu) is fetched at build time under its BSD license.
