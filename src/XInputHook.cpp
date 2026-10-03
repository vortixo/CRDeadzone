#include "XInputHook.h"

#include <windows.h>

#include <MinHook.h>
#include <XInput.h>

#include <mutex>
#include <string>
#include <vector>

#include "Activity.h"
#include "Config.h"
#include "DeadzoneMath.h"
#include "Logger.h"

namespace crdeadzone {
namespace {

static_assert(sizeof(void*) == 8, "CRDeadzone is x64 only");

const Config* g_config = nullptr;
std::mutex g_mutex;
bool g_installed = false;

using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
XInputGetStateFn g_realGetState = nullptr;
XInputGetStateFn g_realGetStateEx = nullptr;  // ordinal 100 on xinput1_3

void ApplyToGamepad(XINPUT_GAMEPAD& pad, const Settings& s) {
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

DWORD WINAPI DetourXInputGetState(DWORD userIndex, XINPUT_STATE* state) {
  const DWORD res = g_realGetState(userIndex, state);
  if (res == ERROR_SUCCESS && state && g_config && !WrapperRecentlyActive()) {
    ApplyToGamepad(state->Gamepad, g_config->Get());
  }
  return res;
}

DWORD WINAPI DetourXInputGetStateEx(DWORD userIndex, XINPUT_STATE* state) {
  const DWORD res = g_realGetStateEx(userIndex, state);
  if (res == ERROR_SUCCESS && state && g_config && !WrapperRecentlyActive()) {
    ApplyToGamepad(state->Gamepad, g_config->Get());
  }
  return res;
}

// Resolves what a DLL's export ordinal actually is, so we never assume
// "ordinal 2 == XInputGetState". Returns the export name or "".
std::string ExportNameForOrdinal(HMODULE mod, uint16_t ordinal) {
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

bool HookOne(const wchar_t* dllName, const char* procName, void* detour, void** realOut,
             const char* logName) {
  HMODULE mod = GetModuleHandleW(dllName);
  if (!mod) {
    Logger::Instance().Info(std::string("xinput: ") + logName + " module not loaded, skipped");
    return false;
  }
  void* target = reinterpret_cast<void*>(GetProcAddress(mod, procName));
  if (!target) {
    Logger::Instance().Info(std::string("xinput: ") + logName + " export missing, skipped");
    return false;
  }
  if (MH_CreateHook(target, detour, realOut) != MH_OK) {
    Logger::Instance().Warn(std::string("xinput: MH_CreateHook failed for ") + logName);
    return false;
  }
  if (MH_EnableHook(target) != MH_OK) {
    Logger::Instance().Warn(std::string("xinput: MH_EnableHook failed for ") + logName);
    return false;
  }
  Logger::Instance().Info(std::string("xinput: hooked ") + logName);
  return true;
}

}  // namespace

bool InstallXInputHooks(const Config& config) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_config = &config;
  bool any = g_installed;

  if (!g_installed) {
    // Standard named exports on every XInput variant that provides them.
    if (!g_realGetState) {
      if (HookOne(L"xinput1_4.dll", "XInputGetState",
                   reinterpret_cast<void*>(&DetourXInputGetState),
                   reinterpret_cast<void**>(&g_realGetState), "xinput1_4!XInputGetState"))
        any = true;
    }
    if (!g_realGetState) {
      if (HookOne(L"xinput1_3.dll", "XInputGetState",
                   reinterpret_cast<void*>(&DetourXInputGetState),
                   reinterpret_cast<void**>(&g_realGetState), "xinput1_3!XInputGetState"))
        any = true;
    }
    if (!g_realGetState) {
      if (HookOne(L"xinput9_1_0.dll", "XInputGetState",
                   reinterpret_cast<void*>(&DetourXInputGetState),
                   reinterpret_cast<void**>(&g_realGetState), "xinput9_1_0!XInputGetState"))
        any = true;
    }
    // Undocumented extended-state entry (ordinal 100, xinput1_3 era).
    if (!g_realGetStateEx) {
      HMODULE m13 = GetModuleHandleW(L"xinput1_3.dll");
      if (m13) {
        void* target = reinterpret_cast<void*>(GetProcAddress(m13, reinterpret_cast<LPCSTR>(100)));
        if (target && MH_CreateHook(target, reinterpret_cast<void*>(&DetourXInputGetStateEx),
                                    reinterpret_cast<void**>(&g_realGetStateEx)) == MH_OK &&
            MH_EnableHook(target) == MH_OK) {
          Logger::Instance().Info("xinput: hooked xinput1_3 ordinal 100 (GetStateEx)");
          any = true;
        }
      }
    }
    g_installed = any;
  }

  // Diagnostics: identify the game's single ordinal-2 XInput import so future
  // versions stay correct without guessing.
  HMODULE m14 = GetModuleHandleW(L"XINPUT1_4.dll");
  if (m14) {
    const std::string name = ExportNameForOrdinal(m14, 2);
    Logger::Instance().Info("xinput: XINPUT1_4 ordinal 2 resolves to '" + name + "'");
  }
  if (!any) {
    Logger::Instance().Info("xinput: no state polling hook installed (game likely polls via GameInput/HID)");
  }
  return any;
}

void RemoveXInputHooks() {
  std::lock_guard<std::mutex> lock(g_mutex);
  MH_DisableHook(MH_ALL_HOOKS);
  g_realGetState = nullptr;
  g_realGetStateEx = nullptr;
  g_installed = false;
  g_config = nullptr;
}

}  // namespace crdeadzone
