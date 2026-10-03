// GameInput export interception (header-free). Hooks the runtime's creation
// entry points and delegates version-aware wrapping to ProbeAndWrapObject.

#include "GameInputHook.h"

#include <windows.h>

#include <MinHook.h>

#include <mutex>
#include <string>

#include "Activity.h"
#include "Config.h"
#include "GameInputVersion.h"
#include "Logger.h"
namespace crdeadzone {
namespace {

const Config* g_config = nullptr;
std::mutex g_mutex;
bool g_installed = false;

using InitFn = HRESULT(STDAPICALLTYPE*)(const GUID*, void**);
using CreateFn = HRESULT(STDAPICALLTYPE*)(void**);
InitFn g_realInit = nullptr;
CreateFn g_realCreate = nullptr;

const char* VersionName(GameInputVersion v) {
  switch (v) {
    case GameInputVersion::V1:
      return "v1";
    case GameInputVersion::V2:
      return "v2";
    case GameInputVersion::V3:
      return "v3";
    default:
      return "unknown/v0";
  }
}

HRESULT STDAPICALLTYPE DetourInit(const GUID* riid, void** out) {
  const HRESULT hr = g_realInit(riid, out);
  if (hr < 0 || !out || !*out) return hr;
  Logger::Instance().Info("gameinput: runtime object created");
  const GameInputVersion v = ProbeAndWrapObject(*out, g_config);
  std::string msg = std::string("gameinput: version ") + VersionName(v);
  msg += (v == GameInputVersion::V2 || v == GameInputVersion::V3)
             ? " (wrapped: deadzone active)"
             : " (pass-through: layouts unconfirmed, HID layers still apply)";
  Logger::Instance().Info(msg);
  return hr;
}

HRESULT STDAPICALLTYPE DetourCreate(void** out) {
  const HRESULT hr = g_realCreate(out);
  if (hr < 0 || !out || !*out) return hr;
  Logger::Instance().Info("gameinput: v0-style object created (pass-through, HID layers apply)");
  ProbeAndWrapObject(*out, g_config);  // detection logging only for unconfirmed layouts
  return hr;
}

void* FindExport(const wchar_t* module, const char* name) {
  HMODULE mod = GetModuleHandleW(module);
  if (!mod) return nullptr;
  return reinterpret_cast<void*>(GetProcAddress(mod, name));
}

void* FindGameInputExport(const char* name) {
  static const wchar_t* kMods[] = {L"GameInput.dll", L"gameinput.dll",
                                   L"GameInputRedist.dll", L"gameinputredist.dll"};
  for (const wchar_t* m : kMods) {
    if (void* p = FindExport(m, name)) {
      std::wstring w(m);
      Logger::Instance().Info("gameinput: runtime module present: " +
                              std::string(w.begin(), w.end()));
      return p;
    }
  }
  return nullptr;
}

}  // namespace

bool InstallGameInputHooks(const Config& config) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_config = &config;
  (void)g_config;
  if (g_installed) return true;
  bool any = false;
  if (void* t = FindGameInputExport("GameInputInitialize")) {
    if (MH_CreateHook(t, reinterpret_cast<void*>(&DetourInit),
                      reinterpret_cast<void**>(&g_realInit)) == MH_OK &&
        MH_EnableHook(t) == MH_OK) {
      Logger::Instance().Info("gameinput: GameInputInitialize hook installed");
      any = true;
    }
  }
  if (void* t = FindGameInputExport("GameInputCreate")) {
    if (MH_CreateHook(t, reinterpret_cast<void*>(&DetourCreate),
                      reinterpret_cast<void**>(&g_realCreate)) == MH_OK &&
        MH_EnableHook(t) == MH_OK) {
      Logger::Instance().Info("gameinput: GameInputCreate hook installed");
      any = true;
    }
  }
  if (!any) {
    Logger::Instance().Info(
        "gameinput: no runtime module loaded yet; polling is GDK-static or HID "
        "(HID layers still apply, retrying 60s)");
  }
  g_installed = any;
  return any;
}

void RemoveGameInputHooks() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_realInit = nullptr;
  g_realCreate = nullptr;
  g_installed = false;
  g_config = nullptr;
}

}  // namespace crdeadzone
