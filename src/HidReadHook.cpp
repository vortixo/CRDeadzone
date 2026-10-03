#include "HidReadHook.h"

#include <windows.h>

#include <hidusage.h>
#include <hidsdi.h>

#include <mutex>
#include <string>
#include <unordered_map>
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

// Game-module IAT slots we patched (for restore).
void** g_readFileSlot = nullptr;
void* g_realReadFile = nullptr;
void** g_closeHandleSlot = nullptr;
void* g_realCloseHandle = nullptr;

std::unordered_set<HANDLE> g_hidHandles;
std::unordered_set<HANDLE> g_notHid;  // negative cache: ordinary files
std::unordered_set<HANDLE> g_everLogged;  // closed+reopened handles stay quiet

using ReadFileFn = BOOL(WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using CloseHandleFn = BOOL(WINAPI*)(HANDLE);

uint64_t NowMs() {
  LARGE_INTEGER f, c;
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&c);
  return static_cast<uint64_t>(c.QuadPart * 1000 / f.QuadPart);
}

bool SameName(const char* a, const char* b) {
  while (*a && *b) {
    char ca = *a++, cb = *b++;
    if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
    if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
    if (ca != cb) return false;
  }
  return *a == *b;
}

// Patches one named import in the game module's own IAT. No thread freeze:
// a single pointer write under VirtualProtect.
bool PatchGameIAT(const char* dllName, const char* funcName, void* detour, void*** slotOut,
                  void** origOut) {
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

void ClassifyHandle(HANDLE h) {
  HIDD_ATTRIBUTES attr{};
  attr.Size = sizeof(attr);
  if (!HidD_GetAttributes(h, &attr)) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_notHid.insert(h);
    return;
  }
  PHIDP_PREPARSED_DATA ppd = nullptr;
  if (!HidD_GetPreparsedData(h, &ppd) || !ppd) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_notHid.insert(h);
    return;
  }
  HIDP_CAPS caps{};
  USAGE collection = 0;
  if (HidP_GetCaps(ppd, &caps) == HIDP_STATUS_SUCCESS && caps.UsagePage == 0x01) {
    collection = caps.Usage;
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (collection == 0x04 || collection == 0x05) {
      g_hidHandles.insert(h);
      if (g_everLogged.insert(h).second) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "hid: tracking handle %p vid=%04x pid=%04x", h,
                      attr.VendorID, attr.ProductID);
        Logger::Instance().Info(buf);
      }
    } else {
      g_notHid.insert(h);
    }
  }
  GetOrAddDevice(h, ppd, false, collection);
  LogDeviceOnce(h, "hidread", collection);
}

BOOL WINAPI DetourReadFile(HANDLE h, LPVOID buf, DWORD toRead, LPDWORD readOut,
                            LPOVERLAPPED ov) {
  const auto real = reinterpret_cast<ReadFileFn>(g_realReadFile);
  const BOOL ok = real(h, buf, toRead, readOut, ov);
  if (!ok || !buf || !readOut || *readOut == 0 || ov || !g_config) return ok;
  bool tracked = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    tracked = g_hidHandles.find(h) != g_hidHandles.end();
    if (!tracked && g_notHid.find(h) != g_notHid.end()) return ok;
  }
  if (!tracked) {
    if (!HasDevice(h)) ClassifyHandle(h);
    std::lock_guard<std::mutex> lock(g_mutex);
    tracked = g_hidHandles.find(h) != g_hidHandles.end();
    if (!tracked) return ok;
  }
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

BOOL WINAPI DetourCloseHandle(HANDLE h) {
  const auto real = reinterpret_cast<CloseHandleFn>(g_realCloseHandle);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_hidHandles.erase(h);
    g_notHid.erase(h);
  }
  RemoveDevice(h);
  return real(h);
}

void RestoreSlot(void** slot, void* orig) {
  if (!slot || !orig) return;
  DWORD old = 0;
  if (VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
    *slot = orig;
    VirtualProtect(slot, sizeof(void*), old, &old);
  }
}

}  // namespace

bool InstallHidReadHooks(const Config& config) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_config = &config;
  if (g_installed) return true;
  bool readOk = PatchGameIAT("KERNEL32.dll", "ReadFile", reinterpret_cast<void*>(&DetourReadFile),
                              &g_readFileSlot, &g_realReadFile);
  bool closeOk = PatchGameIAT("KERNEL32.dll", "CloseHandle",
                               reinterpret_cast<void*>(&DetourCloseHandle), &g_closeHandleSlot,
                               &g_realCloseHandle);
  if (readOk) Logger::Instance().Info("hidread: game IAT ReadFile patched");
  if (closeOk) Logger::Instance().Info("hidread: game IAT CloseHandle patched");
  g_installed = readOk && closeOk;
  if (!g_installed) {
    Logger::Instance().Warn("hidread: IAT patch missed (imports by ordinal?)");
    RestoreSlot(g_readFileSlot, g_realReadFile);
    RestoreSlot(g_closeHandleSlot, g_realCloseHandle);
    g_readFileSlot = g_closeHandleSlot = nullptr;
    g_realReadFile = g_realCloseHandle = nullptr;
  }
  return g_installed;
}

void RemoveHidReadHooks() {
  std::lock_guard<std::mutex> lock(g_mutex);
  RestoreSlot(g_readFileSlot, g_realReadFile);
  RestoreSlot(g_closeHandleSlot, g_realCloseHandle);
  g_readFileSlot = g_closeHandleSlot = nullptr;
  g_realReadFile = g_realCloseHandle = nullptr;
  g_installed = false;
  g_config = nullptr;
}

}  // namespace crdeadzone
