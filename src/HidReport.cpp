#include "HidReport.h"

#include <windows.h>

#include <hidusage.h>
#include <hidsdi.h>
#include <hidpi.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "Activity.h"
#include "Config.h"
#include "DeadzoneMath.h"
#include "HidMapping.h"
#include "Logger.h"

namespace crdeadzone {

struct DeviceEntry {
  HidMapping mapping;
  PHIDP_PREPARSED_DATA ppd = nullptr;
  bool ownsPpd = false;  // true: free(); false: HidD_FreePreparsedData
  bool logged = false;
};

namespace {

constexpr USAGE kPageDesktop = 0x01;
constexpr USAGE kX = 0x30, kY = 0x31, kZ = 0x32, kRx = 0x33, kRy = 0x34, kRz = 0x35;

std::mutex g_mutex;
std::unordered_map<HANDLE, DeviceEntry> g_devices;

bool FindRange(PHIDP_PREPARSED_DATA ppd, USAGE usage, HidAxisRange& out) {
  USHORT count = 0;
  if (HidP_GetValueCaps(HidP_Input, nullptr, &count, ppd) != HIDP_STATUS_SUCCESS) return false;
  std::vector<HIDP_VALUE_CAPS> caps(count);
  if (HidP_GetValueCaps(HidP_Input, caps.data(), &count, ppd) != HIDP_STATUS_SUCCESS) return false;
  for (USHORT i = 0; i < count; ++i) {
    const auto& c = caps[i];
    if (c.UsagePage != kPageDesktop) continue;
    const bool match = c.IsRange ? (c.Range.UsageMin <= usage && usage <= c.Range.UsageMax)
                                : (c.NotRange.Usage == usage);
    if (!match) continue;
    out.present = true;
    out.logicalMin = c.LogicalMin;
    out.logicalMax = c.LogicalMax;
    return true;
  }
  return false;
}

void LogDevice(const char* where, HANDLE h, const DeviceEntry& e, USAGE collection) {
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "hid: %s device %p collection=%02x present X%d Y%d Z%d Rx%d Ry%d Rz%d "
                "look=%s triggers=%s",
                where, h, collection, e.mapping.x.present, e.mapping.y.present,
                e.mapping.z.present, e.mapping.rx.present, e.mapping.ry.present,
                e.mapping.rz.present, e.mapping.lookUsesZRz ? "Z/Rz" : "Rx/Ry",
                e.mapping.triggersAreRxRy ? "Rx/Ry" : "none");
  Logger::Instance().Info(buf);
}

}  // namespace

// Looks up (or builds and caches) the entry for a device. ppd ownership
// transfers to the cache when provided.
DeviceEntry* GetOrAddDevice(void* key, void* ppd, bool ownsPpd, unsigned collection) {
  HANDLE h = static_cast<HANDLE>(key);
  PHIDP_PREPARSED_DATA preparsed = static_cast<PHIDP_PREPARSED_DATA>(ppd);
  const USAGE coll = static_cast<USAGE>(collection);
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_devices.find(h);
  if (it != g_devices.end()) {
    if (preparsed) {  // drop the spare copy
      if (ownsPpd) std::free(preparsed);
    }
    return &it->second;
  }
  DeviceEntry e;
  e.ppd = preparsed;
  e.ownsPpd = ownsPpd;
  if (preparsed && (coll == 0x04 || coll == 0x05)) {
    FindRange(ppd, kX, e.mapping.x);
    FindRange(ppd, kY, e.mapping.y);
    FindRange(ppd, kZ, e.mapping.z);
    FindRange(ppd, kRx, e.mapping.rx);
    FindRange(ppd, kRy, e.mapping.ry);
    FindRange(ppd, kRz, e.mapping.rz);
    const HidMapping decision = DecideMapping(
        e.mapping.x.present, e.mapping.y.present, e.mapping.z.present,
        e.mapping.rx.present, e.mapping.ry.present, e.mapping.rz.present);
    e.mapping.usable = decision.usable;
    e.mapping.lookUsesZRz = decision.lookUsesZRz;
    e.mapping.triggersAreRxRy = decision.triggersAreRxRy;
  }
  auto inserted = g_devices.emplace(h, std::move(e));
  return &inserted.first->second;
}

bool HasDevice(void* key) {
  HANDLE h = static_cast<HANDLE>(key);
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_devices.find(h) != g_devices.end();
}

void RemoveDevice(void* key) {
  HANDLE h = static_cast<HANDLE>(key);
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_devices.find(h);
  if (it == g_devices.end()) return;
  if (it->second.ppd) {
    if (it->second.ownsPpd) std::free(it->second.ppd);
    else HidD_FreePreparsedData(it->second.ppd);
  }
  g_devices.erase(it);
}

namespace {

bool ReadUsage(DeviceEntry& e, uint8_t* report, size_t len, USAGE usage, ULONG& out) {
  return HidP_GetUsageValue(HidP_Input, kPageDesktop, 0, usage, &out, e.ppd,
                            reinterpret_cast<PCHAR>(report),
                            static_cast<ULONG>(len)) == HIDP_STATUS_SUCCESS;
}

bool WriteUsage(DeviceEntry& e, uint8_t* report, size_t len, USAGE usage, ULONG value) {
  return HidP_SetUsageValue(HidP_Input, kPageDesktop, 0, usage, value, e.ppd,
                            reinterpret_cast<PCHAR>(report),
                            static_cast<ULONG>(len)) == HIDP_STATUS_SUCCESS;
}

// Applies the mod settings to one movement/look stick pair in the report.
bool RemapPair(DeviceEntry& e, uint8_t* report, size_t len, USAGE u1, USAGE u2,
               const HidAxisRange& r1, const HidAxisRange& r2, float inner, float outer,
               float power) {
  ULONG v1 = 0, v2 = 0;
  if (!ReadUsage(e, report, len, u1, v1)) return false;
  if (!ReadUsage(e, report, len, u2, v2)) return false;
  float f1 = HidToSigned(static_cast<int32_t>(v1), r1.logicalMin, r1.logicalMax);
  float f2 = HidToSigned(static_cast<int32_t>(v2), r2.logicalMin, r2.logicalMax);
  ApplyRadialDeadzone(f1, f2, inner, outer, power);
  const ULONG w1 = static_cast<ULONG>(SignedToHid(f1, r1.logicalMin, r1.logicalMax));
  const ULONG w2 = static_cast<ULONG>(SignedToHid(f2, r2.logicalMin, r2.logicalMax));
  bool ok = WriteUsage(e, report, len, u1, w1);
  ok = WriteUsage(e, report, len, u2, w2) && ok;
  return ok;
}

}  // namespace

// Core remap. Returns true when the report was modified.
bool RemapHidReport(void* key, uint8_t* report, size_t len, const Settings& s) {
  HANDLE h = static_cast<HANDLE>(key);
  if (WrapperRecentlyActive()) return false;  // high-level layer owns it
  DeviceEntry* dev = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_devices.find(h);
    if (it == g_devices.end() || !it->second.mapping.usable) return false;
    dev = &it->second;
  }
  DeviceEntry& e = *dev;
  const float moveInner = s.movementDeadzone / 100.0f;
  const float moveOuter = s.movementOuter / 100.0f;
  const float movePower = CurvePowerForChoice(s.movementCurve, s.customCurvePower);
  float lookInner = moveInner, lookOuter = moveOuter, lookPower = movePower;
  if (s.perStick) {
    lookInner = s.lookDeadzone / 100.0f;
    lookOuter = s.lookOuter / 100.0f;
    lookPower = CurvePowerForChoice(s.lookCurve, s.customCurvePower);
  }

  bool changed = false;
  changed = RemapPair(e, report, len, kX, kY, e.mapping.x, e.mapping.y, moveInner,
                      moveOuter, movePower) ||
            changed;
  if (e.mapping.lookUsesZRz) {
    changed = RemapPair(e, report, len, kZ, kRz, e.mapping.z, e.mapping.rz, lookInner,
                        lookOuter, lookPower) ||
              changed;
    if (e.mapping.triggersAreRxRy) {
      const float ltDz = s.triggerLeftDeadzone / 100.0f;
      const float rtDz =
          (s.triggerSeparate ? s.triggerRightDeadzone : s.triggerLeftDeadzone) / 100.0f;
      ULONG lt = 0, rt = 0;
      if (ReadUsage(e, report, len, kRx, lt) && ReadUsage(e, report, len, kRy, rt)) {
        const float flt = ApplyTriggerDeadzone(
            HidToUnit(static_cast<int32_t>(lt), e.mapping.rx.logicalMin,
                      e.mapping.rx.logicalMax),
            ltDz);
        const float frt = ApplyTriggerDeadzone(
            HidToUnit(static_cast<int32_t>(rt), e.mapping.ry.logicalMin,
                      e.mapping.ry.logicalMax),
            rtDz);
        changed = WriteUsage(e, report, len, kRx,
                             static_cast<ULONG>(UnitToHid(
                                 flt, e.mapping.rx.logicalMin, e.mapping.rx.logicalMax))) ||
                  changed;
        changed = WriteUsage(e, report, len, kRy,
                             static_cast<ULONG>(UnitToHid(
                                 frt, e.mapping.ry.logicalMin, e.mapping.ry.logicalMax))) ||
                  changed;
      }
    }
  } else {
    changed = RemapPair(e, report, len, kRx, kRy, e.mapping.rx, e.mapping.ry, lookInner,
                        lookOuter, lookPower) ||
              changed;
  }
  return changed;
}

void LogDeviceOnce(void* key, const char* where, unsigned collection) {
  HANDLE h = static_cast<HANDLE>(key);
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_devices.find(h);
  if (it == g_devices.end() || it->second.logged) return;
  it->second.logged = true;
  LogDevice(where, h, it->second, static_cast<USAGE>(collection));
}

}  // namespace crdeadzone
