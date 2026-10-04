#include "XInputHook.h"

#include <windows.h>
#include <xinput.h>

#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "Activity.h"
#include "Config.h"
#include "DeadzoneMath.h"
#include "HookLayer.h"
#include "Logger.h"
#include "MinHookWrapper.h"

namespace crdeadzone {
namespace {

using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

class XInputLayer final : public HookLayer, protected MinHookLayerMixin {
 public:
  std::string_view Name() const override { return "xinput"; }

  bool Install(const Config& config) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (IsInstalled()) return true;

    SetConfig(&config);

    bool any = false;

    // Use local handles and function pointers, only commit on success
    minhook::HookHandle getstate_hook, getstateex_hook;
    XInputGetStateFn local_real_getstate = nullptr;
    XInputGetStateFn local_real_getstateex = nullptr;

    // Standard named exports on every XInput variant
    struct Variant {
      const wchar_t* dll_name;
      const char* log_name;
    };

    const Variant variants[] = {
        {L"xinput1_4.dll", "xinput1_4!XInputGetState"},
        {L"xinput1_3.dll", "xinput1_3!XInputGetState"},
        {L"xinput9_1_0.dll", "xinput9_1_0!XInputGetState"},
    };

    for (const auto& v : variants) {
      if (!local_real_getstate) {
        std::error_code ec = minhook::CreateHookApi(
            v.dll_name, "XInputGetState",
            reinterpret_cast<void*>(&DetourXInputGetState),
            reinterpret_cast<void**>(&local_real_getstate), getstate_hook);
        if (!ec) {
          ec = batch_.QueueEnable(getstate_hook);
        }
        if (!ec) {
          any = true;
          Logger::Instance().Info("Hooked {}", v.log_name);
        } else {
          Logger::Instance().Warn("Failed to hook {}: {}", v.log_name, ec.message());
        }
      }
    }

    // Undocumented extended-state entry (ordinal 100, xinput1_3 era)
    if (!local_real_getstateex) {
      std::error_code ec = minhook::CreateHookOrdinal(
          L"xinput1_3.dll", 100,
          reinterpret_cast<void*>(&DetourXInputGetStateEx),
          reinterpret_cast<void**>(&local_real_getstateex), getstateex_hook);
      if (!ec) {
        ec = batch_.QueueEnable(getstateex_hook);
      }
      if (!ec) {
        any = true;
        Logger::Instance().Info("Hooked xinput1_3 ordinal 100 (GetStateEx)");
      } else {
        Logger::Instance().Warn("Failed to hook xinput1_3 ordinal 100: {}", ec.message());
      }
    }

    if (any) {
      std::error_code ec = ApplyBatch();
      if (ec) {
        Logger::Instance().Error("Failed to enable XInput hooks: {}", ec.message());
        DisableAll();
        // Local handles go out of scope and clean up automatically
        any = false;
      } else {
        // Success: move handles and function pointers to members
        getstate_hook_ = std::move(getstate_hook);
        getstateex_hook_ = std::move(getstateex_hook);
        real_getstate_ = local_real_getstate;
        real_getstateex_ = local_real_getstateex;
      }
    }

    // Diagnostics: identify the game's single ordinal-2 XInput import
    if (HMODULE m14 = GetModuleHandleW(L"XINPUT1_4.dll")) {
      const std::string name = ExportNameForOrdinal(m14, 2);
      Logger::Instance().Info("XINPUT1_4 ordinal 2 resolves to '{}'", name);
    }

    if (!any) {
      Logger::Instance().Info(
          "No state polling hook installed (game likely polls via GameInput/HID)");
    }

    MarkInstalled(any);
    return any;
  }

  void Remove() override {
    std::lock_guard<std::mutex> lock(mutex_);
    DisableAll();
    getstate_hook_.Reset();
    getstateex_hook_.Reset();
    real_getstate_ = nullptr;
    real_getstateex_ = nullptr;
    ClearConfig();
    MarkInstalled(false);
  }

 private:
  std::mutex mutex_;
  XInputGetStateFn real_getstate_ = nullptr;
  XInputGetStateFn real_getstateex_ = nullptr;
  minhook::HookHandle getstate_hook_;
  minhook::HookHandle getstateex_hook_;

  // Resolves what a DLL's export ordinal actually is
  static std::string ExportNameForOrdinal(HMODULE mod, uint16_t ordinal) {
    if (!mod) return "";
    auto* dos = reinterpret_cast<uint8_t*>(mod);
    if (dos[0] != 'M' || dos[1] != 'Z') return "";
    const auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(dos + reinterpret_cast<IMAGE_DOS_HEADER*>(dos)->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return "";
    const auto& expDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!expDir.VirtualAddress) return "";
    const auto* exp = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(dos + expDir.VirtualAddress);
    const uint32_t base = exp->Base;
    if (ordinal < base || ordinal >= base + exp->NumberOfFunctions) return "";
    const auto* names = reinterpret_cast<uint32_t*>(dos + exp->AddressOfNames);
    const auto* ordinals = reinterpret_cast<uint16_t*>(dos + exp->AddressOfNameOrdinals);
    for (uint32_t i = 0; i < exp->NumberOfNames; ++i) {
      if (base + ordinals[i] == ordinal) {
        return reinterpret_cast<const char*>(dos + names[i]);
      }
    }
    return "";
  }

  // Apply deadzone to gamepad state
  static void ApplyToGamepad(XINPUT_GAMEPAD& pad, const Settings& s) {
    const GamepadSettings gs = MakeGamepadSettings(
        s.movementDeadzone, s.movementOuter, s.movementCurve, s.lookDeadzone, s.lookOuter,
        s.lookCurve, s.triggerLeftDeadzone, s.triggerRightDeadzone, s.customCurvePower,
        s.perStick, s.triggerSeparate);

    float lx = XInputShortToFloat(pad.sThumbLX);
    float ly = XInputShortToFloat(pad.sThumbLY);
    float rx = XInputShortToFloat(pad.sThumbRX);
    float ry = XInputShortToFloat(pad.sThumbRY);
    float lt = pad.bLeftTrigger / 255.0f;
    float rt = pad.bRightTrigger / 255.0f;
    ApplyGamepadState(lx, ly, rx, ry, lt, rt, gs);
    pad.sThumbLX = static_cast<SHORT>(FloatToXInputShort(lx));
    pad.sThumbLY = static_cast<SHORT>(FloatToXInputShort(ly));
    pad.sThumbRX = static_cast<SHORT>(FloatToXInputShort(rx));
    pad.sThumbRY = static_cast<SHORT>(FloatToXInputShort(ry));
    pad.bLeftTrigger = static_cast<BYTE>(lt * 255.0f);
    pad.bRightTrigger = static_cast<BYTE>(rt * 255.0f);
  }

  static DWORD WINAPI DetourXInputGetState(DWORD userIndex, XINPUT_STATE* state) {
    auto& layer = Instance();
    const DWORD res = layer.real_getstate_(userIndex, state);
    if (res == ERROR_SUCCESS && state && layer.ConfigPtr() && !WrapperRecentlyActive()) {
      ApplyToGamepad(state->Gamepad, layer.ConfigPtr()->Get());
      layer.LogThrottled("live, remapping polled state");
    }
    return res;
  }

  static DWORD WINAPI DetourXInputGetStateEx(DWORD userIndex, XINPUT_STATE* state) {
    auto& layer = Instance();
    const DWORD res = layer.real_getstateex_(userIndex, state);
    if (res == ERROR_SUCCESS && state && layer.ConfigPtr() && !WrapperRecentlyActive()) {
      ApplyToGamepad(state->Gamepad, layer.ConfigPtr()->Get());
      layer.LogThrottled("live (ex), remapping polled state");
    }
    return res;
  }

  static XInputLayer& Instance() {
    static XInputLayer instance;
    return instance;
  }
};

XInputLayer g_layer;

}  // namespace


bool InstallXInputHooks(const Config& config) {
  return g_layer.Install(config);
}

void RemoveXInputHooks() {
  g_layer.Remove();
}

}  // namespace crdeadzone
