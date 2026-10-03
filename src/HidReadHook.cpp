#include "HidReadHook.h"

#include <windows.h>

#include <MinHook.h>
#include <hidsdi.h>

#include <mutex>
#include <string>
#include <unordered_set>

#include "Config.h"
#include "HidReport.h"
#include "Logger.h"

namespace crdeadzone {
namespace {

const Config* g_config = nullptr;
std::mutex g_mutex;
bool g_installed = false;
uint64_t g_liveLoggedMs = 0;
std::unordered_set<HANDLE> g_hidHandles;

using CreateFileWFn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD,
                                       DWORD, HANDLE);
using ReadFileFn = BOOL(WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using CloseHandleFn = BOOL(WINAPI*)(HANDLE);

CreateFileWFn g_realCreateFileW = nullptr;
ReadFileFn g_realReadFile = nullptr;
CloseHandleFn g_realCloseHandle = nullptr;

uint64_t NowMs() {
  LARGE_INTEGER f, c;
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&c);
  return static_cast<uint64_t>(c.QuadPart * 1000 / f.QuadPart);
}

bool IsHidPath(LPCWSTR name) {
  if (!name) return false;
  std::wstring s(name);
  for (auto& ch : s) ch = static_cast<wchar_t>(towlower(ch));
  return s.find(L"hid#") != std::wstring::npos || s.find(L"hid\\") != std::wstring::npos;
}

HANDLE WINAPI DetourCreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                                 DWORD disp, DWORD flags, HANDLE tmpl) {
  HANDLE h = g_realCreateFileW(name, access, share, sa, disp, flags, tmpl);
  if (h != INVALID_HANDLE_VALUE && IsHidPath(name)) {
    // Cheap verify: real HID handles answer HidD_GetAttributes.
    HIDD_ATTRIBUTES attr{};
    attr.Size = sizeof(attr);
    if (HidD_GetAttributes(h, &attr)) {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_hidHandles.insert(h);
      char buf[128];
      std::snprintf(buf, sizeof(buf), "hid: tracking handle %p vid=%04x pid=%04x", h,
                    attr.VendorID, attr.ProductID);
      Logger::Instance().Info(buf);
    }
  }
  return h;
}

BOOL WINAPI DetourCloseHandle(HANDLE h) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_hidHandles.erase(h);
  }
  RemoveDevice(h);
  return g_realCloseHandle(h);
}

void ClassifyHandle(HANDLE h) {
  PHIDP_PREPARSED_DATA ppd = nullptr;
  if (!HidD_GetPreparsedData(h, &ppd) || !ppd) return;
  HIDP_CAPS caps{};
  USAGE collection = 0;
  if (HidP_GetCaps(ppd, &caps) == HIDP_STATUS_SUCCESS && caps.UsagePage == 0x01) {
    collection = caps.Usage;
  }
  GetOrAddDevice(h, ppd, false, collection);
  LogDeviceOnce(h, "hidread", collection);
}

BOOL WINAPI DetourReadFile(HANDLE h, LPVOID buf, DWORD toRead, LPDWORD readOut,
                            LPOVERLAPPED ov) {
  const BOOL ok = g_realReadFile(h, buf, toRead, readOut, ov);
  if (!ok || !buf || !readOut || *readOut == 0 || ov || !g_config) return ok;
  bool tracked = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    tracked = g_hidHandles.find(h) != g_hidHandles.end();
  }
  if (!tracked) return ok;
  if (!HasDevice(h)) ClassifyHandle(h);
  if (RemapHidReport(h, static_cast<uint8_t*>(buf), *readOut, g_config->Get())) {
    const uint64_t now = NowMs();
    std::lock_guard<std::mutex> lock(g_mutex);
    if (now - g_liveLoggedMs > 10000) {
      g_liveLoggedMs = now;
      Logger::Instance().Info("hidread: live, remapping HID reports");
    }
  }
  return ok;
}

bool HookOne(const char* name, void* detour, void** realOut, const char* logName) {
  void* target =
      reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), name));
  if (!target) return false;
  if (MH_CreateHook(target, detour, realOut) != MH_OK) return false;
  if (MH_EnableHook(target) != MH_OK) return false;
  Logger::Instance().Info(std::string("hidread: hooked ") + logName);
  return true;
}

}  // namespace

bool InstallHidReadHooks(const Config& config) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_config = &config;
  if (g_installed) return true;
  bool ok = true;
  if (!g_realCreateFileW) ok = HookOne("CreateFileW", reinterpret_cast<void*>(&DetourCreateFileW),
                                       reinterpret_cast<void**>(&g_realCreateFileW),
                                       "CreateFileW") &&
                                ok;
  if (!g_realReadFile) ok = HookOne("ReadFile", reinterpret_cast<void*>(&DetourReadFile),
                                    reinterpret_cast<void**>(&g_realReadFile), "ReadFile") &&
                             ok;
  if (!g_realCloseHandle) ok = HookOne("CloseHandle", reinterpret_cast<void*>(&DetourCloseHandle),
                                       reinterpret_cast<void**>(&g_realCloseHandle),
                                       "CloseHandle") &&
                                ok;
  g_installed = ok;
  if (!ok) Logger::Instance().Warn("hidread: one or more hooks failed");
  return ok;
}

void RemoveHidReadHooks() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_realCreateFileW = nullptr;
  g_realReadFile = nullptr;
  g_realCloseHandle = nullptr;
  g_installed = false;
  g_config = nullptr;
}

}  // namespace crdeadzone
