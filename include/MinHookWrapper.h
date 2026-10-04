#pragma once

// MinHook RAII wrappers and utilities.
// Provides safe initialization, hook management, and batch operations
// following MinHook best practices (MH_QueueEnableHook / MH_ApplyQueued).

#include <MinHook.h>

#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace crdeadzone::minhook {

// Error category for MinHook status codes.
class MinHookErrorCategory : public std::error_category {
 public:
  const char* name() const noexcept override { return "MinHook"; }
  std::string message(int ev) const override {
    switch (static_cast<MH_STATUS>(ev)) {
      case MH_OK: return "Success";
      case MH_ERROR_ALREADY_INITIALIZED: return "Already initialized";
      case MH_ERROR_NOT_INITIALIZED: return "Not initialized";
      case MH_ERROR_ALREADY_CREATED: return "Hook already created";
      case MH_ERROR_NOT_CREATED: return "Hook not created";
      case MH_ERROR_ENABLED: return "Hook already enabled";
      case MH_ERROR_DISABLED: return "Hook already disabled";
      case MH_ERROR_NOT_EXECUTABLE: return "Target not executable";
      case MH_ERROR_UNSUPPORTED_FUNCTION: return "Unsupported function";
      case MH_ERROR_MEMORY_ALLOC: return "Memory allocation failed";
      case MH_ERROR_MEMORY_PROTECT: return "Memory protection failed";
      case MH_ERROR_MODULE_NOT_FOUND: return "Module not found";
      case MH_ERROR_FUNCTION_NOT_FOUND: return "Function not found";
      default: return "Unknown error";
    }
  }
};

inline const MinHookErrorCategory& MinHookCategory() {
  static MinHookErrorCategory cat;
  return cat;
}

inline std::error_code MakeError(MH_STATUS status) {
  return std::error_code(static_cast<int>(status), MinHookCategory());
}

// RAII wrapper for MinHook library initialization.
class MinHookLibrary {
 public:
  MinHookLibrary() {
    const MH_STATUS status = MH_Initialize();
    if (status != MH_OK) {
      throw std::system_error(MakeError(status), "MH_Initialize failed");
    }
    initialized_ = true;
  }

  ~MinHookLibrary() {
    if (initialized_) {
      MH_Uninitialize();
    }
  }

  MinHookLibrary(const MinHookLibrary&) = delete;
  MinHookLibrary& operator=(const MinHookLibrary&) = delete;

  MinHookLibrary(MinHookLibrary&& other) noexcept : initialized_(other.initialized_) {
    other.initialized_ = false;
  }

  MinHookLibrary& operator=(MinHookLibrary&& other) noexcept {
    if (this != &other) {
      if (initialized_) MH_Uninitialize();
      initialized_ = other.initialized_;
      other.initialized_ = false;
    }
    return *this;
  }

  bool IsInitialized() const noexcept { return initialized_; }

 private:
  bool initialized_ = false;
};

// RAII handle for a single hook. Automatically disables and removes on destruction.
class HookHandle {
 public:
  HookHandle() = default;

  // Creates but does NOT enable the hook. Use HookBatch for batch enabling.
  HookHandle(void* target, void* detour, void** original_out) {
    Create(target, detour, original_out);
  }

  ~HookHandle() {
    if (created_) {
      // Best effort cleanup - ignore errors in destructor
      MH_DisableHook(target_);
      MH_RemoveHook(target_);
    }
  }

  HookHandle(const HookHandle&) = delete;
  HookHandle& operator=(const HookHandle&) = delete;

  HookHandle(HookHandle&& other) noexcept
      : target_(other.target_), created_(other.created_) {
    other.target_ = nullptr;
    other.created_ = false;
  }

  HookHandle& operator=(HookHandle&& other) noexcept {
    if (this != &other) {
      if (created_) {
        MH_DisableHook(target_);
        MH_RemoveHook(target_);
      }
      target_ = other.target_;
      created_ = other.created_;
      other.target_ = nullptr;
      other.created_ = false;
    }
    return *this;
  }

  // Creates the hook in disabled state. Returns error_code on failure.
  std::error_code Create(void* target, void* detour, void** original_out) {
    if (created_) return MakeError(MH_ERROR_ALREADY_CREATED);

    const MH_STATUS status = MH_CreateHook(target, detour, original_out);
    if (status != MH_OK) return MakeError(status);

    target_ = target;
    created_ = true;
    return {};
  }

  // Enables this hook individually (not batched).
  std::error_code Enable() {
    if (!created_) return MakeError(MH_ERROR_NOT_CREATED);
    const MH_STATUS status = MH_EnableHook(target_);
    if (status == MH_ERROR_ENABLED) return {};  // Already enabled is OK
    return status == MH_OK ? std::error_code{} : MakeError(status);
  }

  // Disables this hook individually.
  std::error_code Disable() {
    if (!created_) return MakeError(MH_ERROR_NOT_CREATED);
    const MH_STATUS status = MH_DisableHook(target_);
    if (status == MH_ERROR_DISABLED) return {};  // Already disabled is OK
    return status == MH_OK ? std::error_code{} : MakeError(status);
  }

  // Removes the hook permanently.
  std::error_code Remove() {
    if (!created_) return MakeError(MH_ERROR_NOT_CREATED);
    const MH_STATUS status = MH_RemoveHook(target_);
    if (status == MH_OK) created_ = false;
    return status == MH_OK ? std::error_code{} : MakeError(status);
  }

  // Disables and removes the hook. Ignores errors (for use in destructors/cleanup).
  void Reset() noexcept {
    if (created_) {
      MH_DisableHook(target_);
      MH_RemoveHook(target_);
      target_ = nullptr;
      created_ = false;
    }
  }

  void* Target() const noexcept { return target_; }
  bool IsCreated() const noexcept { return created_; }
  explicit operator bool() const noexcept { return created_; }

 private:
  void* target_ = nullptr;
  bool created_ = false;
};

// Batch hook enabler - queues multiple hooks and applies once (single thread suspension).
// Follows MinHook best practice: https://github.com/TsudaKageyu/minhook/blob/master/_autodocs/advanced-usage.md
class HookBatch {
 public:
  HookBatch() = default;

  // Adds a hook to the enable queue. Hook must already be created.
  std::error_code QueueEnable(HookHandle& hook) {
    if (!hook.IsCreated()) return MakeError(MH_ERROR_NOT_CREATED);
    const MH_STATUS status = MH_QueueEnableHook(hook.Target());
    if (status != MH_OK) return MakeError(status);
    queued_.push_back(hook.Target());
    return {};
  }

  // Adds a raw target to the enable queue (for hooks managed externally).
  std::error_code QueueEnable(void* target) {
    const MH_STATUS status = MH_QueueEnableHook(target);
    if (status != MH_OK) return MakeError(status);
    queued_.push_back(target);
    return {};
  }

  // Applies all queued enables in a single thread suspension.
  std::error_code Apply() {
    if (queued_.empty()) return {};
    const MH_STATUS status = MH_ApplyQueued();
    if (status == MH_OK) queued_.clear();
    return status == MH_OK ? std::error_code{} : MakeError(status);
  }

  // Disables all queued hooks (if Apply hasn't been called yet).
  void Clear() { queued_.clear(); }

  bool Empty() const noexcept { return queued_.empty(); }

 private:
  std::vector<void*> queued_;
};

// Convenience: create hook from API name in a system DLL (like MH_CreateHookApi).
inline std::error_code CreateHookApi(const wchar_t* module_name,
                                     const char* proc_name,
                                     void* detour,
                                     void** original_out,
                                     HookHandle& out_handle) {
  HMODULE mod = GetModuleHandleW(module_name);
  if (!mod) return std::error_code(ERROR_MOD_NOT_FOUND, std::system_category());

  void* target = reinterpret_cast<void*>(GetProcAddress(mod, proc_name));
  if (!target) return std::error_code(ERROR_PROC_NOT_FOUND, std::system_category());

  return out_handle.Create(target, detour, original_out);
}

// Convenience: create hook from ordinal in a system DLL.
inline std::error_code CreateHookOrdinal(const wchar_t* module_name,
                                         uint16_t ordinal,
                                         void* detour,
                                         void** original_out,
                                         HookHandle& out_handle) {
  HMODULE mod = GetModuleHandleW(module_name);
  if (!mod) return std::error_code(ERROR_MOD_NOT_FOUND, std::system_category());

  void* target = reinterpret_cast<void*>(GetProcAddress(mod, reinterpret_cast<LPCSTR>(ordinal)));
  if (!target) return std::error_code(ERROR_PROC_NOT_FOUND, std::system_category());

  return out_handle.Create(target, detour, original_out);
}

}  // namespace crdeadzone::minhook