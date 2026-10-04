// GameInput export interception. Hooks the runtime's creation entry points
// and delegates version-aware wrapping to ProbeAndWrapObject.

#include "GameInputHook.h"

#include <windows.h>

#include <mutex>
#include <string>
#include <string_view>

#include "Activity.h"
#include "Config.h"
#include "GameInputVersion.h"
#include "HookLayer.h"
#include "Logger.h"
#include "MinHookWrapper.h"

namespace crdeadzone {
namespace {

using InitFn = HRESULT(STDAPICALLTYPE*)(const GUID*, void**);
using CreateFn = HRESULT(STDAPICALLTYPE*)(void**);

const char* VersionName(GameInputVersion v) {
  switch (v) {
    case GameInputVersion::V1: return "v1";
    case GameInputVersion::V2: return "v2";
    case GameInputVersion::V3: return "v3";
    default: return "unknown/v0";
  }
}

class GameInputLayer final : public HookLayer, protected MinHookLayerMixin {
 public:
  std::string_view Name() const override { return "gameinput"; }

  bool Install(const Config& config) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (installed_) return true;

    SetConfig(&config);

    bool any = false;
    minhook::HookHandle init_hook, create_hook;
    InitFn local_real_init = nullptr;
    CreateFn local_real_create = nullptr;

    // Hook GameInputInitialize
    if (void* t = FindGameInputExport("GameInputInitialize")) {
      std::error_code ec = CreateAndQueueHook(
          t, &DetourInit, reinterpret_cast<void**>(&local_real_init), init_hook);
      if (!ec) {
        any = true;
      } else {
        Logger::Instance().Warn("Failed to create GameInputInitialize hook: {}", ec.message());
      }
    }

    // Hook GameInputCreate
    if (void* t = FindGameInputExport("GameInputCreate")) {
      std::error_code ec = CreateAndQueueHook(
          t, &DetourCreate, reinterpret_cast<void**>(&local_real_create), create_hook);
      if (!ec) {
        any = true;
      } else {
        Logger::Instance().Warn("Failed to create GameInputCreate hook: {}", ec.message());
      }
    }

    if (any) {
      std::error_code ec = ApplyBatch();
      if (ec) {
        Logger::Instance().Error("Failed to enable GameInput hooks: {}", ec.message());
        DisableAll();
        // Local handles go out of scope and clean up automatically
        any = false;
      } else {
        // Success: move handles and function pointers to members
        init_hook_ = std::move(init_hook);
        create_hook_ = std::move(create_hook);
        real_init_ = local_real_init;
        real_create_ = local_real_create;
        Logger::Instance().Info("GameInput hooks installed (init={}, create={})",
                                init_hook_ ? "yes" : "no", create_hook_ ? "yes" : "no");
      }
    }

    if (!any) {
      Logger::Instance().Info(
          "GameInput runtime not loaded yet; HID layers apply, retrying for 60s");
    }

    MarkInstalled(any);
    return any;
  }

  void Remove() override {
    std::lock_guard<std::mutex> lock(mutex_);
    DisableAll();
    init_hook_.Reset();  // Destructor cleans up
    create_hook_.Reset();
    real_init_ = nullptr;
    real_create_ = nullptr;
    ClearConfig();
    MarkInstalled(false);
  }

 private:
  std::mutex mutex_;
  InitFn real_init_ = nullptr;
  CreateFn real_create_ = nullptr;
  minhook::HookHandle init_hook_;
  minhook::HookHandle create_hook_;

  void* FindGameInputExport(const char* name) {
    static const wchar_t* kMods[] = {
        L"GameInput.dll", L"gameinput.dll", L"GameInputRedist.dll", L"gameinputredist.dll"};
    for (const wchar_t* m : kMods) {
      HMODULE mod = GetModuleHandleW(m);
      if (mod) {
        void* p = reinterpret_cast<void*>(GetProcAddress(mod, name));
        if (p) {
          Logger::Instance().Info("Runtime module present: {}", name);
          return p;
        }
      }
    }
    return nullptr;
  }

  // Detour functions (must be static or free functions for function pointers)
  static HRESULT STDAPICALLTYPE DetourInit(const GUID* riid, void** out) {
    auto& layer = Instance();
    const HRESULT hr = layer.real_init_(riid, out);
    if (hr < 0 || !out || !*out) return hr;

    layer.LogThrottled("Runtime object created");
    const GameInputVersion v = ProbeAndWrapObject(*out, layer.ConfigPtr());
    std::string msg = std::string("Version ") + VersionName(v);
    msg += (v == GameInputVersion::V3)
               ? " (wrapped: deadzone active)"
               : " (pass-through: layout unconfirmed, HID layers still apply)";
    layer.LogThrottled(msg);
    return hr;
  }

  static HRESULT STDAPICALLTYPE DetourCreate(void** out) {
    auto& layer = Instance();
    const HRESULT hr = layer.real_create_(out);
    if (hr < 0 || !out || !*out) return hr;

    layer.LogThrottled("v0-style object created (pass-through, HID layers apply)");
    ProbeAndWrapObject(*out, layer.ConfigPtr());  // Detection logging only
    return hr;
  }

  // Singleton access for static detour functions
  static GameInputLayer& Instance() {
    static GameInputLayer instance;
    return instance;
  }
};

GameInputLayer g_layer;

bool InstallGameInputHooks(const Config& config) {
  return g_layer.Install(config);
}

void RemoveGameInputHooks() {
  g_layer.Remove();
}

}  // namespace crdeadzone