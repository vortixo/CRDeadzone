// Version-aware GameInput object wrapping (v3, confirmed layout only).
//
// Technique: per-object vtable COPY with selective slot patches. No vtables
// are modified in place, no signatures are guessed: every type comes from the
// headers (compile-checked), and the layout was asserted by the configure-time
// probe (cmake/GameInputProbeV3.cpp).
//
// Confirmed slot map (IUnknown occupies 0-2):
//   IGameInput v3: 4=GetCurrentReading, 5=GetNextReading,
//                  6=GetPreviousReading, 7=RegisterReadingCallback
//   IGameInputReading v3: 18=GetGamepadState
// (Other API versions: detected, never patched.)

#include "GameInputVersion.h"

#include <windows.h>

#include <GameInput.h>

#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "Activity.h"
#include "Config.h"
#include "DeadzoneMath.h"
#include "Logger.h"

namespace crdeadzone {
namespace {

const Config* g_cfg = nullptr;
std::recursive_mutex g_mutex;
// original-table -> patched copy (shared per version table).
std::unordered_map<void*, void*> g_patchedTables;
std::vector<void*> g_allocations;  // process-lifetime; never freed

template <typename Traits>
struct GiOrig {
  using GetCurrentFn =
      HRESULT(STDMETHODCALLTYPE*)(typename Traits::GI*, typename Traits::Kind,
                                  typename Traits::Device*, typename Traits::Reading**);
  using GetNextFn = HRESULT(STDMETHODCALLTYPE*)(typename Traits::GI*, typename Traits::Reading*,
                                               typename Traits::Kind, typename Traits::Device*,
                                               typename Traits::Reading**);
  using RegFn = HRESULT(STDMETHODCALLTYPE*)(typename Traits::GI*, typename Traits::Device*,
                                            typename Traits::Kind, void*,
                                            typename Traits::Callback, typename Traits::Token*);
  static inline GetCurrentFn getCurrent = nullptr;
  static inline GetNextFn getNext = nullptr;
  static inline GetNextFn getPrev = nullptr;  // same shape as GetNext
  static inline RegFn regReading = nullptr;
};

template <typename Traits>
struct ReadingOrig {
  using GetStateFn = bool(STDMETHODCALLTYPE*)(typename Traits::Reading*, typename Traits::State*);
  static inline GetStateFn getState = nullptr;
};

void ApplyState(float& lx, float& ly, float& rx, float& ry, float& lt, float& rt,
                const Settings& s) {
  ApplyGamepadState(lx, ly, rx, ry, lt, rt,
                    MakeGamepadSettings(s.movementDeadzone, s.movementOuter, s.movementCurve,
                                        s.lookDeadzone, s.lookOuter, s.lookCurve,
                                        s.triggerLeftDeadzone, s.triggerRightDeadzone,
                                        s.customCurvePower, s.perStick, s.triggerSeparate));
}

void* PatchedTable(void* table, size_t count) {
  auto it = g_patchedTables.find(table);
  if (it != g_patchedTables.end()) return it->second;
  void** copy = new void*[count];
  std::memcpy(copy, table, count * sizeof(void*));
  g_patchedTables[table] = copy;
  g_allocations.push_back(copy);
  return copy;
}

template <typename Traits>
void WrapReading(typename Traits::Reading* reading);

template <typename Traits>
HRESULT STDMETHODCALLTYPE DetourGetCurrent(typename Traits::GI* self, typename Traits::Kind kind,
                                           typename Traits::Device* dev,
                                           typename Traits::Reading** out);

template <typename Traits>
HRESULT STDMETHODCALLTYPE DetourGetNext(typename Traits::GI* self, typename Traits::Reading* ref,
                                        typename Traits::Kind kind, typename Traits::Device* dev,
                                        typename Traits::Reading** out);

template <typename Traits>
HRESULT STDMETHODCALLTYPE DetourGetPrev(typename Traits::GI* self, typename Traits::Reading* ref,
                                        typename Traits::Kind kind, typename Traits::Device* dev,
                                        typename Traits::Reading** out);

template <typename Traits>
HRESULT STDMETHODCALLTYPE DetourRegReading(typename Traits::GI* self, typename Traits::Device* dev,
                                            typename Traits::Kind kind, void* ctx,
                                            typename Traits::Callback cb,
                                            typename Traits::Token* tok);

template <typename Traits>
bool STDMETHODCALLTYPE DetourGetGamepadState(typename Traits::Reading* self,
                                              typename Traits::State* st);

template <typename Traits>
void WrapReading(typename Traits::Reading* reading) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  void** origTable = *reinterpret_cast<void***>(reading);
  void* patched = PatchedTable(origTable, Traits::kReadingSlots);
  static_cast<void**>(patched)[Traits::kGamepadSlot] =
      reinterpret_cast<void*>(&DetourGetGamepadState<Traits>);
  if (!ReadingOrig<Traits>::getState) {
    ReadingOrig<Traits>::getState =
        reinterpret_cast<typename ReadingOrig<Traits>::GetStateFn>(origTable[Traits::kGamepadSlot]);
  }
  *reinterpret_cast<void***>(reading) = static_cast<void**>(patched);
}

template <typename Traits>
HRESULT STDMETHODCALLTYPE DetourGetCurrent(typename Traits::GI* self, typename Traits::Kind kind,
                                           typename Traits::Device* dev,
                                           typename Traits::Reading** out) {
  const HRESULT hr = GiOrig<Traits>::getCurrent(self, kind, dev, out);
  if (hr >= 0 && out && *out) WrapReading<Traits>(*out);
  return hr;
}

template <typename Traits>
HRESULT STDMETHODCALLTYPE DetourGetNext(typename Traits::GI* self, typename Traits::Reading* ref,
                                        typename Traits::Kind kind, typename Traits::Device* dev,
                                        typename Traits::Reading** out) {
  const HRESULT hr = GiOrig<Traits>::getNext(self, ref, kind, dev, out);
  if (hr >= 0 && out && *out) WrapReading<Traits>(*out);
  return hr;
}

template <typename Traits>
HRESULT STDMETHODCALLTYPE DetourGetPrev(typename Traits::GI* self, typename Traits::Reading* ref,
                                        typename Traits::Kind kind, typename Traits::Device* dev,
                                        typename Traits::Reading** out) {
  const HRESULT hr = GiOrig<Traits>::getPrev(self, ref, kind, dev, out);
  if (hr >= 0 && out && *out) WrapReading<Traits>(*out);
  return hr;
}

template <typename Traits>
HRESULT STDMETHODCALLTYPE DetourRegReading(typename Traits::GI* self, typename Traits::Device* dev,
                                            typename Traits::Kind kind, void* ctx,
                                            typename Traits::Callback cb,
                                            typename Traits::Token* tok) {
  static bool logged = false;
  if (!logged) {
    logged = true;
    Logger::Instance().Info(
        "gameinput: game uses callback readings; polling hooks do not see them "
        "(deadzone applies to polled reads)");
  }
  return GiOrig<Traits>::regReading(self, dev, kind, ctx, cb, tok);
}

template <typename Traits>
bool STDMETHODCALLTYPE DetourGetGamepadState(typename Traits::Reading* self,
                                              typename Traits::State* st) {
  const bool ok = ReadingOrig<Traits>::getState(self, st);
  if (ok && st && g_cfg) {
    ApplyState(st->leftThumbstickX, st->leftThumbstickY, st->rightThumbstickX,
               st->rightThumbstickY, st->leftTrigger, st->rightTrigger, g_cfg->Get());
    MarkWrapperActive();
  }
  return ok;
}

struct V3Traits {
  using GI = GameInput::v3::IGameInput;
  using Reading = GameInput::v3::IGameInputReading;
  using Kind = GameInput::v3::GameInputKind;
  using Device = GameInput::v3::IGameInputDevice;
  using State = GameInput::v3::GameInputGamepadState;
  using Callback = GameInput::v3::GameInputReadingCallback;
  using Token = GameInput::v3::GameInputCallbackToken;
  static constexpr size_t kGiSlots = 19;
  static constexpr size_t kReadingSlots = 21;
  static constexpr size_t kGamepadSlot = 18;
};

template <typename Traits>
void WrapGi(typename Traits::GI* obj) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  void** origTable = *reinterpret_cast<void***>(obj);
  void* patched = PatchedTable(origTable, Traits::kGiSlots);
  void** table = static_cast<void**>(patched);
  // Slots confirmed against the v3 headers (see probe + research notes).
  table[4] = reinterpret_cast<void*>(&DetourGetCurrent<Traits>);
  table[5] = reinterpret_cast<void*>(&DetourGetNext<Traits>);
  table[6] = reinterpret_cast<void*>(&DetourGetPrev<Traits>);
  table[7] = reinterpret_cast<void*>(&DetourRegReading<Traits>);
  if (!GiOrig<Traits>::getCurrent) {
    GiOrig<Traits>::getCurrent =
        reinterpret_cast<typename GiOrig<Traits>::GetCurrentFn>(origTable[4]);
    GiOrig<Traits>::getNext =
        reinterpret_cast<typename GiOrig<Traits>::GetNextFn>(origTable[5]);
    GiOrig<Traits>::getPrev =
        reinterpret_cast<typename GiOrig<Traits>::GetNextFn>(origTable[6]);
    GiOrig<Traits>::regReading =
        reinterpret_cast<typename GiOrig<Traits>::RegFn>(origTable[7]);
  }
  *reinterpret_cast<void***>(obj) = table;
}

}  // namespace

GameInputVersion ProbeAndWrapObject(void* obj, const Config* cfg) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  g_cfg = cfg;
  if (!obj) return GameInputVersion::Unknown;
  IUnknown* unk = static_cast<IUnknown*>(obj);
  void* p = nullptr;
  if (SUCCEEDED(unk->QueryInterface(__uuidof(GameInput::v3::IGameInput), &p))) {
    static_cast<IUnknown*>(p)->Release();
    WrapGi<V3Traits>(static_cast<GameInput::v3::IGameInput*>(obj));
    return GameInputVersion::V3;
  }
  return GameInputVersion::Unknown;
}

}  // namespace crdeadzone
