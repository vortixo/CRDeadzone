# Plan: tunable-capture (CRDeadzone v1.2.0)

Read-only recon of the game's option registrar. No game memory writes.
Goal: log which code binds `deadZone` etc. and capture the field addresses,
so a later version can drive the game's own tunables (all input paths).

## Background (from static analysis of CONTROLResonant.exe, build 25600401)

- Game polls `XInputGetState` per frame (2 sites found); our XInput hook sees it.
- No GameInput imports; RawInput is keyboard/mouse only; native pads go
  through statically-linked `libScePad_static` (invisible to our hooks).
- All tunables (`deadZone`, `slideDeadzone`, `stickInputPowerFactor`,
  `movementInputCurve`, `cameraInputCurve`, `moveInputRemapCurve`) resolve
  through option-binder call(s) taking the name string; ref windows show
  `call -> RVA` targets per anchor.
- Registrar prologue signature: 10 hits in this build. Getter prologue:
  147 hits. => NO hardcoded-signature hook. Cluster call targets from
  anchor flow windows instead (fully update-resilient).

## Approach

For each watched name: FindStringRef -> FindLeaRefs -> WalkFlow -> collect
call targets -> rank targets by in-how-many-names-windows -> hook top
candidates (max 4) with passthrough detours that log watched names ->
summary counts. Ambiguity (0 candidates) logs and skips; never writes.

## Tasks

### Task 1: portable capture logic + tests
- `include/TunableCapture.h`, `src/TunableCapture.cpp`:
  `kWatchedTunables` (6 exact names), `IsWatchedTunable`,
  `TunableCapture` registry (latest-wins, null-safe),
  `ClusterCallTargets` (rank by window count).
- `tests/test_tunable.cpp` wired into workflow + README.
- RED: tests reference missing header. GREEN: all pass + full suite green.

### Task 2: Windows capture wiring
- `OptionsOverride.h/cpp`: `InstallTunableCapture()` — anchor windows,
  cluster, hook top-4 via MinHook, passthrough detours logging watched
  names + args; summary lines. Called first in `InitThread` after MH init.
- Verification: Linux suite green; Windows build green on CI (only
  compiler that sees this TU); careful review of detour ABI safety.
- No Linux test possible for the hook itself (windows.h/MinHook).

### Task 3: release v1.2.0
- Version bumps (CMake, main log string, menu JSON), README (capture
  section + Steam-Input deadzone note), commit, push, CI green.

## Global constraints
- Read-only: no writes to game memory in this version.
- Max 4 hooks; 0 candidates => log + skip, never guess.
- Log lines must let the next step distinguish "registration ran before
  hook" (0 captures) from "hook live, game idle".
