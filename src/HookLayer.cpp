// HookLayer mixin implementation (kept out of the header so the
// function-pointer -> void* conversion stays at call sites, matching
// what MH_CreateHook callers have always written).

#include "HookLayer.h"

namespace crdeadzone {

std::error_code MinHookLayerMixin::CreateAndQueueHook(void* target, void* detour,
                                                      void** original_out,
                                                      minhook::HookHandle& handle) {
  std::error_code ec = handle.Create(target, detour, original_out);
  if (ec) return ec;
  ec = batch_.QueueEnable(handle);
  if (!ec) hook_targets_.push_back(target);
  return ec;
}

}  // namespace crdeadzone
