#include "RawInputHook.h"

#include <windows.h>

#include <MinHook.h>

#include <cstdlib>
#include <cstring>
#include <mutex>

#include "Config.h"
#include "HidReport.h"
#include "Logger.h"

namespace crdeadzone {
namespace {

const Config* g_config = nullptr;
std::mutex g_mutex;
bool g_installed = false;
uint64_t g_liveLoggedMs = 0;

using GetRawInputDataFn = UINT(WINAPI*)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
GetRawInputDataFn g_realGetRawInputData = nullptr;

uint64_t NowMs() {
  LARGE_INTEGER f, c;
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&c);
  return static_cast<uint64_t>(c.QuadPart * 1000 / f.QuadPart);
}

void LogLiveThrottled() {
  const uint64_t now = NowMs();
  std::lock_guard<std::mutex> lock(g_mutex);
  if (now - g_liveLoggedMs < 10000) return;
  g_liveLoggedMs = now;
  Logger::Instance().Info("rawinput: live, remapping HID gamepad reports");
}

// Returns preparsed data for a RawInput device (caller frees with free()).
void* FetchPreparsed(HANDLE hDevice, USAGE& collectionOut) {
  collectionOut = 0;
  RID_DEVICE_INFO info{};
  UINT size = sizeof(info);
  if (GetRawInputDeviceInfoW(hDevice, RIDI_DEVICEINFO, &info, &size) != size) return nullptr;
  if (info.dwType != RIM_TYPEHID) return nullptr;
  if (info.hid.usUsagePage != 0x01) return nullptr;
  collectionOut = info.hid.usUsage;
  UINT ppdSize = 0;
  if (GetRawInputDeviceInfoW(hDevice, RIDI_PREPARSEDDATA, nullptr, &ppdSize) != 0) {
    return nullptr;
  }
  void* ppd = std::malloc(ppdSize);
  if (!ppd) return nullptr;
  if (GetRawInputDeviceInfoW(hDevice, RIDI_PREPARSEDDATA, ppd, &ppdSize) == (UINT)-1) {
    std::free(ppd);
    return nullptr;
  }
  return ppd;
}

UINT WINAPI DetourGetRawInputData(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData,
                                  PUINT pcbSize, UINT cbSizeHeader) {
  const UINT res = g_realGetRawInputData(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);
  if (res == (UINT)-1 || !pData || !pcbSize || uiCommand != RID_INPUT) return res;
  if (cbSizeHeader != sizeof(RAWINPUTHEADER)) return res;
  RAWINPUT* raw = static_cast<RAWINPUT*>(pData);
  if (raw->header.dwType != RIM_TYPEHID || !g_config) return res;

  HANDLE hDevice = raw->header.hDevice;
  if (!HasDevice(hDevice)) {
    USAGE collection = 0;
    void* ppd = FetchPreparsed(hDevice, collection);
    // Register (or find) the device; registry drops spare copies.
    GetOrAddDevice(hDevice, ppd, true, collection);
    LogDeviceOnce(hDevice, "rawinput", collection);
  }
  uint8_t* report = raw->data.hid.bRawData;
  const size_t len = raw->data.hid.dwSizeHid * raw->data.hid.dwCount;
  if (RemapHidReport(hDevice, report, len, g_config->Get())) {
    LogLiveThrottled();
  }
  return res;
}

}  // namespace

bool InstallRawInputHooks(const Config& config) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_config = &config;
  if (g_installed) return true;
  void* target = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                        "GetRawInputData"));
  if (!target) {
    Logger::Instance().Warn("rawinput: GetRawInputData not found");
    return false;
  }
  if (MH_CreateHook(target, reinterpret_cast<void*>(&DetourGetRawInputData),
                    reinterpret_cast<void**>(&g_realGetRawInputData)) != MH_OK) {
    Logger::Instance().Warn("rawinput: MH_CreateHook failed");
    return false;
  }
  if (MH_EnableHook(target) != MH_OK) {
    Logger::Instance().Warn("rawinput: MH_EnableHook failed");
    return false;
  }
  g_installed = true;
  Logger::Instance().Info("rawinput: GetRawInputData hook installed");
  return true;
}

void RemoveRawInputHooks() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_realGetRawInputData = nullptr;
  g_installed = false;
  g_config = nullptr;
}

}  // namespace crdeadzone
