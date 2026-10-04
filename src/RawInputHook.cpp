#include "RawInputHook.h"

#include <windows.h>
#include <hidusage.h>
#include <hidsdi.h>

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>

#include "Config.h"
#include "HidReport.h"
#include "HookLayer.h"
#include "Logger.h"
#include "MinHookWrapper.h"

namespace crdeadzone {
namespace {

using GetRawInputDataFn = UINT(WINAPI*)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);

class RawInputLayer final : public HookLayer, protected MinHookLayerMixin {
 public:
  std::string_view Name() const override { return "rawinput"; }

  bool Install(const Config& config) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (IsInstalled()) return true;

    SetConfig(&config);

    void* target = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetRawInputData"));
    if (!target) {
      Logger::Instance().Warn("GetRawInputData not found");
      return false;
    }

    // Use local handle and function pointer, only commit on success
    minhook::HookHandle hook;
    GetRawInputDataFn local_real = nullptr;

    std::error_code ec = CreateAndQueueHook(
        target, reinterpret_cast<void*>(&DetourGetRawInputData),
        reinterpret_cast<void**>(&local_real), hook);
    if (ec) {
      Logger::Instance().Warn("Failed to create GetRawInputData hook: {}", ec.message());
      return false;
    }

    ec = ApplyBatch();
    if (ec) {
      Logger::Instance().Warn("Failed to enable RawInput hook: {}", ec.message());
      DisableAll();
      // Local handle goes out of scope and cleans up
      return false;
    }

    // Success: move handle and function pointer to members
    hook_ = std::move(hook);
    real_getrawinputdata_ = local_real;

    Logger::Instance().Info("GetRawInputData hook installed");
    MarkInstalled(true);
    return true;
  }

  void Remove() override {
    std::lock_guard<std::mutex> lock(mutex_);
    DisableAll();
    hook_.Reset();
    real_getrawinputdata_ = nullptr;
    ClearConfig();
    MarkInstalled(false);
  }

 private:
  std::mutex mutex_;
  GetRawInputDataFn real_getrawinputdata_ = nullptr;
  minhook::HookHandle hook_;

  // Returns preparsed data for a RawInput device (caller frees with free()).
  static void* FetchPreparsed(HANDLE hDevice, USAGE& collectionOut) {
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

  static UINT WINAPI DetourGetRawInputData(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData,
                                           PUINT pcbSize, UINT cbSizeHeader) {
    auto& layer = Instance();
    const UINT res = layer.real_getrawinputdata_(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);
    if (res == (UINT)-1 || !pData || !pcbSize || uiCommand != RID_INPUT) return res;
    if (cbSizeHeader != sizeof(RAWINPUTHEADER)) return res;
    RAWINPUT* raw = static_cast<RAWINPUT*>(pData);
    if (raw->header.dwType != RIM_TYPEHID || !layer.ConfigPtr()) return res;

    HANDLE hDevice = raw->header.hDevice;
    if (!HasDevice(hDevice)) {
      USAGE collection = 0;
      void* ppd = FetchPreparsed(hDevice, collection);
      GetOrAddDevice(hDevice, ppd, true, collection);
      LogDeviceOnce(hDevice, "rawinput", collection);
    }
    uint8_t* report = raw->data.hid.bRawData;
    const size_t len = raw->data.hid.dwSizeHid * raw->data.hid.dwCount;
    if (RemapHidReport(hDevice, report, len, layer.ConfigPtr()->Get())) {
      layer.LogThrottled("live, remapping HID gamepad reports");
    }
    return res;
  }

  static RawInputLayer& Instance() {
    static RawInputLayer instance;
    return instance;
  }
};

RawInputLayer g_layer;

bool InstallRawInputHooks(const Config& config) {
  return g_layer.Install(config);
}

void RemoveRawInputHooks() {
  g_layer.Remove();
}

}  // namespace crdeadzone