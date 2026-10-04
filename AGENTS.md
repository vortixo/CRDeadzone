# AGENTS.md — CRDeadzone

Windows x64 DLL mod for CONTROL Resonant (loaded by crloader). C++20, CMake, MinHook via FetchContent.

## Verify (exact commands)

```sh
python3 tools/validate_menu.py deadzone.menu.json
python3 tools/test_validate_menu.py
g++ -std=c++20 -Wall -Wextra -Werror -Iinclude tests/test_main.cpp tests/test_deadzone_math.cpp \
  tests/test_config.cpp tests/test_hid.cpp tests/test_support.cpp tests/test_tunable.cpp \
  src/DeadzoneMath.cpp src/Config.cpp src/HidMapping.cpp src/Activity.cpp \
  src/PatternScanner.cpp src/Logger.cpp src/Disasm.cpp src/TunableCapture.cpp \
  -o /tmp/test_deadzone_math && /tmp/test_deadzone_math
```

## The blind spot (read this first)

The Linux test build above does **not** compile the Windows-only TUs:
`src/main.cpp`, `src/{XInput,GameInput,RawInput,HidRead}Hook.cpp`,
`src/OptionsOverride.cpp`, `src/GameInputWrapper*.cpp`,
`src/HookLayer.cpp`, `src/MinHookWrapper.cpp`.
A green Linux run says **nothing** about those files. Before pushing changes to them, verify with either:
- Compiler Explorer MSVC (`vcpp_*_x64`, `/std:c++latest /EHsc`): inline quoted
  project includes + upstream `MinHook.h` into one file. Note the API field
  is `userArguments`, not `userOptions`.
- CI: `test` (Linux unit tests) + `build` (MSVC, needs `test`) +
  `mingw-check` (MinGW cross-compile of all TUs except
  `OptionsOverride.cpp`, which uses MSVC-only SEH `__try`/`__except`).

## Portability rules (MSVC is the authority)

- System includes must be lowercase (`<xinput.h>`, never `<XInput.h>`).
  Windows ignores case; Linux/MinGW do not.
- Never use `std::format_string` parameters with defaulted trailing
  arguments (MSVC rejects). Use `Logger::Info/Warn/Error(fmt, args...)`
  (vformat-based). Single-argument calls use the plain overloads so braces
  in game-derived strings are never interpreted.
- `Install*/Remove*` free functions live at `crdeadzone` scope, never
  inside `namespace {` (else `LNK2019`). Singletons stay anonymous.
- Every opened namespace must be closed; MSVC reports the imbalance as a
  bare `error C1075` at the TU's first `{` with no other diagnostic.
- Hook layers use the `MinHookWrapper.h` RAII + `HookLayer.h` base.
  History: the original unification broke MSVC with a bare `C1075`
  (dropped anonymous-namespace `}` in 4 files) plus `LNK2019` (free
  functions at internal linkage). Both fixed and verified TU-by-TU on
  Compiler Explorer MSVC pre-push — see `REFACTORING_PLAN.md`.

## Repo notes

- Background reading: `docs/architecture.md`, `docs/input-research.md`,
  `plans/tunable-capture.md`.
- `miscellaneous/` (game exe, third-party mods) is gitignored local
  reference — never commit it.
- `deadzone.menu.json` is validated by CI; keep option count/defaults in
  sync with `Settings` in `include/Config.h`.
- Push needs auth: GitHub PAT (0600) at `~/.config/crdeadzone/github_pat`;
  use a one-shot `credential.helper` reading that file, never paste the token.
