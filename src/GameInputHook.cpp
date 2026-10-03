#include "GameInputHook.h"

#include <windows.h>

#include <MinHook.h>

#include <cstdio>
#include <mutex>
#include <string>

#include "Config.h"
#include "Logger.h"

// GameInput diagnostic layer (header-free, no assumptions).
//
// What the exe analysis showed:
//  - The game references GameInputCreate but imports no GameInput DLL and has
//    no delay-load table, so it most likely uses the GDK-static GameInput
//    (GamepadType::GDK / GamepadType::SCE) and/or direct HID.
//  - Therefore this module does NOT pretend to rewrite gamepad state. It
//    detects a dynamically loaded GameInput runtime, intercepts its creation
//    call to prove which input path is live, and records everything in the
//    log. The captured evidence drives the v1.1 reading hook.
//
// If a future SDK-validated reading wrapper lands, it plugs in here behind
// CRDEADZONE_HAVE_GAMEINPUT without touching anything else.

namespace crdeadzone {
namespace {

const Config* g_config = nullptr;
std::mutex g_mutex;
bool g_installed = false;

using GameInputCreateFn = HRESULT (*)(void**);
GameInputCreateFn g_realGameInputCreate = nullptr;

HRESULT DetourGameInputCreate(void** out) {
  Logger::Instance().Info("gameinput: GameInputCreate called by game");
  const HRESULT hr = g_realGameInputCreate(out);
  if (hr >= 0 && out) {
    char buf[128];
    snprintf(buf, sizeof(buf), "gameinput: IGameInput created @ %p (hr=0x%08lx)",
             *out, static_cast<unsigned long>(hr));
    Logger::Instance().Info(buf);
  } else {
    char buf[64];
    snprintf(buf, sizeof(buf), "gameinput: create failed hr=0x%08lx",
             static_cast<unsigned long>(hr));
    Logger::Instance().Warn(buf);
  }
  return hr;
}

void* FindGameInputCreate() {
  static const wchar_t* kMods[] = {L"GameInput.dll", L"gameinput.dll"};
  for (const wchar_t* m : kMods) {
    HMODULE mod = GetModuleHandleW(m);
    if (!mod) continue;
    void* p = reinterpret_cast<void*>(GetProcAddress(mod, "GameInputCreate"));
    if (p) {
      std::wstring w(m);
      Logger::Instance().Info(
          "gameinput: runtime module present: " + std::string(w.begin(), w.end()));
      return p;
    }
  }
  return nullptr;
}

}  // namespace

bool InstallGameInputHooks(const Config& config) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_config = &config;
  if (g_installed) return true;
  void* target = FindGameInputCreate();
  if (!target) {
    Logger::Instance().Info(
        "gameinput: no dynamic GameInput runtime loaded; game likely uses GDK-static GameInput/HID "
        "(see options discovery below)");
    return false;
  }
  if (MH_CreateHook(target, reinterpret_cast<void*>(&DetourGameInputCreate),
                    reinterpret_cast<void**>(&g_realGameInputCreate)) != MH_OK) {
    Logger::Instance().Warn("gameinput: MH_CreateHook failed");
    return false;
  }
  if (MH_EnableHook(target) != MH_OK) {
    Logger::Instance().Warn("gameinput: MH_EnableHook failed");
    return false;
  }
  g_installed = true;
  Logger::Instance().Info("gameinput: creation hook installed");
  return true;
}

void RemoveGameInputHooks() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_realGameInputCreate = nullptr;
  g_installed = false;
  g_config = nullptr;
}

}  // namespace crdeadzone
