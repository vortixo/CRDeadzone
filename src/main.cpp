// CRDeadzone - in-game controller deadzone mod for CONTROL Resonant.
//
// Loaded by crloader from crmods/CRDeadzone/. Exposes the game's hidden stick
// and trigger deadzones through the CRModMenu MODS tab (deadzone.menu.json)
// and applies them live by intercepting controller state polling.
//
// Loader-lock safety: DllMain only stores the handle and spawns a worker
// thread. All scanning/hooking happens there, never under the loader lock.

#include <windows.h>

#include <MinHook.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

#include "Config.h"
#include "GameInputHook.h"
#include "HidReadHook.h"
#include "Logger.h"
#include "OptionsOverride.h"
#include "RawInputHook.h"
#include "XInputHook.h"

namespace {

constexpr const wchar_t* kIniFile = L"crdeadzone.ini";  // matches menu id "crdeadzone"

HMODULE g_module = nullptr;
std::atomic<bool> g_stop{false};
crdeadzone::Config* g_config = nullptr;

std::wstring DllDirectory() {
  wchar_t path[MAX_PATH] = {};
  GetModuleFileNameW(g_module, path, MAX_PATH);
  return std::filesystem::path(path).parent_path().wstring();
}

std::wstring ExeDirectory() {
  wchar_t path[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  return std::filesystem::path(path).parent_path().wstring();
}

std::wstring PickIniPath(const std::wstring& dllDir) {
  namespace fs = std::filesystem;
  // Descriptor ships next to the DLL: crmods/CRDeadzone/ModMenuConfig/.
  const auto nextToDll = fs::path(dllDir) / "ModMenuConfig" / kIniFile;
  std::error_code ec;
  if (fs::exists(nextToDll, ec)) return nextToDll.wstring();
  // Fallback: descriptor placed in the game folder instead.
  const auto nextToExe = fs::path(ExeDirectory()) / "ModMenuConfig" / kIniFile;
  if (fs::exists(nextToExe, ec)) return nextToExe.wstring();
  return nextToDll.wstring();  // defaults until CRModMenu writes it
}

void LogSettings(const crdeadzone::Settings& s) {
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "config: move(dz=%d outer=%d curve=%d) look(dz=%d outer=%d curve=%d) "
                "trig(L=%d R=%d) custom=%d perStick=%d trigSep=%d",
                s.movementDeadzone, s.movementOuter, s.movementCurve, s.lookDeadzone,
                s.lookOuter, s.lookCurve, s.triggerLeftDeadzone, s.triggerRightDeadzone,
                s.customCurvePower, s.perStick ? 1 : 0, s.triggerSeparate ? 1 : 0);
  crdeadzone::Logger::Instance().Info(buf);
}

void InitThread() {
  using namespace crdeadzone;
  const std::wstring dir = DllDirectory();
  Logger::Instance().Init(dir);
  Logger::Instance().Info("CRDeadzone v1.1.2 init (crloader)");

  {  // Kill switch: drop an empty disabled.txt next to the DLL if a future
    // build ever misbehaves; the mod then logs and installs nothing.
    std::error_code ec;
    if (std::filesystem::exists(std::filesystem::path(dir) / "disabled.txt", ec)) {
      Logger::Instance().Warn("disabled.txt present: hooks not installed");
      return;
    }
  }

  if (MH_Initialize() != MH_OK) {
    Logger::Instance().Error("MinHook init failed; hooks disabled");
  }

  g_config = new Config(PickIniPath(dir));
  LogSettings(g_config->Get());

  // Layer 1: XInput state polling (all variants + ordinal-resolved import).
  bool xinput = InstallXInputHooks(*g_config);
  // Layer 2: GameInput runtime wrapping (version-exact, v2/v3).
  bool gameinput = InstallGameInputHooks(*g_config);
  // Layer 3: RawInput HID reports (guaranteed import, usage-based remap).
  bool rawinput = InstallRawInputHooks(*g_config);
  // Layer 4: direct-HID ReadFile tracking (usage-based remap).
  bool hidread = InstallHidReadHooks(*g_config);
  // Layer 5: read-only discovery of the game's own deadzone tunables.
  DiscoverGameDeadzones();

  if (!xinput && !gameinput && !rawinput && !hidread) {
    Logger::Instance().Warn(
        "no input hook installed yet; menu/config still work and late-loaded "
        "input DLLs are retried for 60s");
  }

  // Live reload: CRModMenu writes the INI on every change; pick it up fast.
  // Late input modules (xinput variants, GameInput runtime) are retried too.
  int ticks = 0;
  while (!g_stop.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    if (g_config->PollForChanges(0)) {
      g_config->Load();
      Logger::Instance().Info("config reloaded (live apply)");
      LogSettings(g_config->Get());
    }
    if ((!xinput || !gameinput) && ++ticks < 120) {
      if (!xinput) {
        xinput = InstallXInputHooks(*g_config);
        if (xinput) Logger::Instance().Info("xinput hook installed on retry");
      }
      if (!gameinput) {
        gameinput = InstallGameInputHooks(*g_config);
        if (gameinput) Logger::Instance().Info("gameinput hook installed on retry");
      }
    }
  }
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID /*reserved*/) {
  switch (reason) {
    case DLL_PROCESS_ATTACH:
      g_module = module;
      DisableThreadLibraryCalls(module);
      CreateThread(nullptr, 0,
                   [](LPVOID) -> DWORD {
                     InitThread();
                     return 0;
                   },
                   nullptr, 0, nullptr);
      break;
    case DLL_PROCESS_DETACH:
      g_stop.store(true);
      crdeadzone::RemoveXInputHooks();
      crdeadzone::RemoveGameInputHooks();
      crdeadzone::RemoveRawInputHooks();
      crdeadzone::RemoveHidReadHooks();
      MH_Uninitialize();
      crdeadzone::Logger::Instance().Shutdown();
      delete g_config;
      g_config = nullptr;
      break;
  }
  return TRUE;
}
