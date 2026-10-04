#include "HidReadHook.h"

#include <windows.h>
#include <hidusage.h>
#include <hidsdi.h>

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>

#include "Config.h"
#include "HidReport.h"
#include "HookLayer.h"
#include "Logger.h"

namespace crdeadzone {
namespace {

using ReadFileFn = BOOL(WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using CloseHandleFn = BOOL(WINAPI*)(HANDLE);

class HidReadLayer final : public HookLayer {
 public:
  std::string_view Name() const override { return "hidread"; }

  bool Install(const Config& config) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (installed_) return true;

    SetConfig(&config);

    bool readOk = PatchGameIAT("KERNEL32.dll", "ReadFile",
                               reinterpret_cast<void*>(&DetourReadFile),
                               &readfile_slot_, &real_readfile_);
    bool closeOk = PatchGameIAT("KERNEL32.dll", "CloseHandle",
                                reinterpret_cast<void*>(&DetourCloseHandle),
                                &closehandle_slot_, &real_closehandle_);

    if (readOk) Logger::Instance().Info("Game IAT ReadFile patched");
    if (closeOk) Logger::Instance().Info("Game IAT CloseHandle patched");

    bool success = readOk && closeOk;
    if (!success) {
      Logger::Instance().Warn("IAT patch missed (imports by ordinal?)");
      RestoreSlot(readfile_slot_, real_readfile_);
      RestoreSlot(closehandle_slot_, real_closehandle_);
      readfile_slot_ = closehandle_slot_ = nullptr;
      real_readfile_ = real_closehandle_ = nullptr;
    }

    MarkInstalled(success);
    return success;
  }

  void Remove() override {
    std::lock_guard<std::mutex> lock(mutex_);
    RestoreSlot(readfile_slot_, real_readfile_);
    RestoreSlot(closehandle_slot_, real_closehandle_);
    readfile_slot_ = closehandle_slot_ = nullptr;
    real_readfile_ = real_closehandle_ = nullptr;
    ClearConfig();
    MarkInstalled(false);
  }

 private:
  std::mutex mutex_;
  void** readfile_slot_ = nullptr;
  void* real_readfile_ = nullptr;
  void** closehandle_slot_ = nullptr;
  void* real_closehandle_ = nullptr;

  std::unordered_set<HANDLE> hid_handles_;
  std::unordered_set<HANDLE> not_hid_;
  std::unordered_set<HANDLE> ever_logged_;

  // Case-insensitive string comparison
  static bool SameName(const char* a, const char* b) {
    while (*a && *b) {
      char ca = *a++, cb = *b++;
      if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
      if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
      if (ca != cb) return false;
    }
    return *a == *b;
  }

  // Patches one named import in the game module's own IAT.
  static bool PatchGameIAT(const char* dllName, const char* funcName, void* detour,
                           void*** slotOut, void** origOut) {
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    if (!base) return false;
    const auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;
    auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
    for (; desc->Name; ++desc) {
      const char* name = reinterpret_cast<const char*>(base + desc->Name);
      if (!SameName(name, dllName)) continue;
      auto* thunkName = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->OriginalFirstThunk);
      auto* thunkIAT = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->FirstThunk);
      for (; thunkName->u1.AddressOfData; ++thunkName, ++thunkIAT) {
        if (thunkName->u1.Ordinal & IMAGE_ORDINAL_FLAG64) continue;
        const auto* import = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + thunkName->u1.AddressOfData);
        if (!SameName(reinterpret_cast<const char*>(import->Name), funcName)) continue;
        DWORD old = 0;
        if (!VirtualProtect(&thunkIAT->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) {
          return false;
        }
        *origOut = reinterpret_cast<void*>(thunkIAT->u1.Function);
        thunkIAT->u1.Function = reinterpret_cast<ULONGLONG>(detour);
        VirtualProtect(&thunkIAT->u1.Function, sizeof(void*), old, &old);
        *slotOut = reinterpret_cast<void**>(&thunkIAT->u1.Function);
        return true;
      }
    }
    return false;
  }

  static void RestoreSlot(void** slot, void* orig) {
    if (!slot || !orig) return;
    DWORD old = 0;
    if (VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
      *slot = orig;
      VirtualProtect(slot, sizeof(void*), old, &old);
    }
  }

  static void ClassifyHandle(HANDLE h, HidReadLayer& layer) {
    HIDD_ATTRIBUTES attr{};
    attr.Size = sizeof(attr);
    if (!HidD_GetAttributes(h, &attr)) {
      layer.not_hid_.insert(h);
      return;
    }
    PHIDP_PREPARSED_DATA ppd = nullptr;
    if (!HidD_GetPreparsedData(h, &ppd) || !ppd) {
      layer.not_hid_.insert(h);
      return;
    }
    HIDP_CAPS caps{};
    USAGE collection = 0;
    if (HidP_GetCaps(ppd, &caps) == HIDP_STATUS_SUCCESS && caps.UsagePage == 0x01) {
      collection = caps.Usage;
    }
    GetOrAddDevice(h, ppd, false, collection);
    LogDeviceOnce(h, "hidread", collection);

    const bool usable = IsDeviceUsable(h);
    if (usable) {
      layer.hid_handles_.insert(h);
      if (layer.ever_logged_.insert(h).second) {
        Logger::Instance().Info("Tracking handle {} vid={:04x} pid={:04x}",
                                static_cast<void*>(h), attr.VendorID, attr.ProductID);
      }
    } else {
      layer.not_hid_.insert(h);
      if (layer.ever_logged_.insert(h).second) {
        Logger::Instance().Info("Skipping handle {} vid={:04x} pid={:04x} collection={:02x} (no stick usages)",
                                static_cast<void*>(h), attr.VendorID, attr.ProductID, collection);
      }
    }
  }

  static BOOL WINAPI DetourReadFile(HANDLE h, LPVOID buf, DWORD toRead, LPDWORD readOut,
                                    LPOVERLAPPED ov) {
    auto& layer = Instance();
    const auto real = reinterpret_cast<ReadFileFn>(layer.real_readfile_);
    const BOOL ok = real(h, buf, toRead, readOut, ov);
    if (!ok || !buf || !readOut || *readOut == 0 || ov || !layer.ConfigPtr()) return ok;

    bool tracked = false;
    {
      std::lock_guard<std::mutex> lock(layer.mutex_);
      tracked = layer.hid_handles_.find(h) != layer.hid_handles_.end();
      if (!tracked && layer.not_hid_.find(h) != layer.not_hid_.end()) return ok;
    }
    if (!tracked) {
      if (!HasDevice(h)) ClassifyHandle(h, layer);
      std::lock_guard<std::mutex> lock(layer.mutex_);
      tracked = layer.hid_handles_.find(h) != layer.hid_handles_.end();
      if (!tracked) return ok;
    }
    if (RemapHidReport(h, static_cast<uint8_t*>(buf), *readOut, layer.ConfigPtr()->Get())) {
      layer.LogThrottled("live, remapping HID reports");
    }
    return ok;
  }

  static BOOL WINAPI DetourCloseHandle(HANDLE h) {
    auto& layer = Instance();
    const auto real = reinterpret_cast<CloseHandleFn>(layer.real_closehandle_);
    {
      std::lock_guard<std::mutex> lock(layer.mutex_);
      layer.hid_handles_.erase(h);
      layer.not_hid_.erase(h);
    }
    RemoveDevice(h);
    return real(h);
  }

  static HidReadLayer& Instance() {
    static HidReadLayer instance;
    return instance;
  }
};

HidReadLayer g_layer;

bool InstallHidReadHooks(const Config& config) {
  return g_layer.Install(config);
}

void RemoveHidReadHooks() {
  g_layer.Remove();
}

}  // namespace crdeadzone