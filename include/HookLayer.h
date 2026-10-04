#pragma once

// Common base class for all input hook layers.
// Encapsulates: config reference, installation state, thread safety, logging.

#include "Config.h"
#include "Logger.h"
#include "MinHookWrapper.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <string_view>

namespace crdeadzone {

// Abstract base for a hook layer that can be installed/removed.
class HookLayer {
 public:
  virtual ~HookLayer() = default;

  // Install hooks. Returns true if at least one hook installed.
  // Called from init thread; must be thread-safe for concurrent Poll().
  virtual bool Install(const Config& config) = 0;

  // Remove all hooks. Called from DllMain on process detach.
  virtual void Remove() = 0;

  // Human-readable layer name for logging.
  virtual std::string_view Name() const = 0;

  // Whether installation was attempted (success or failure).
  bool IsInstalled() const noexcept { return installed_.load(); }

 protected:
  // Called by derived Install() to mark as attempted.
  void MarkInstalled(bool success) { installed_.store(success); }

  // Thread-safe access to current config (valid after successful Install).
  const Config* ConfigPtr() const noexcept {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return config_;
  }

  // Sets config pointer (called from Install).
  void SetConfig(const Config* cfg) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_ = cfg;
  }

  // Clears config pointer (called from Remove).
  void ClearConfig() {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_ = nullptr;
  }

  // Throttled logging: logs at most once per `interval_ms` milliseconds.
  // Returns true if logged (i.e., interval elapsed).
  bool LogThrottled(std::string_view msg, uint64_t interval_ms = 10000) {
    const uint64_t now = NowMs();
    std::lock_guard<std::mutex> lock(log_mutex_);
    if (now - last_log_ms_ < interval_ms) return false;
    last_log_ms_ = now;
    Logger::Instance().Info("{}: {}", Name(), msg);
    return true;
  }

  // Current time in milliseconds using QueryPerformanceCounter.
  static uint64_t NowMs() {
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return static_cast<uint64_t>(counter.QuadPart * 1000 / freq.QuadPart);
  }

 private:
  std::atomic<bool> installed_{false};
  mutable std::mutex config_mutex_;
  const Config* config_ = nullptr;
  mutable std::mutex log_mutex_;
  uint64_t last_log_ms_ = 0;
};

// Mixin for layers that use MinHook and want batch enabling.
// Provides a HookBatch member and helper to create+queue hooks.
class MinHookLayerMixin {
 protected:
  minhook::HookBatch batch_;
  std::vector<void*> hook_targets_;  // Track targets for targeted cleanup

  // Creates a hook and queues it for batch enable.
  // Returns error_code; on success, hook is created but NOT yet enabled.
  template <typename Fn>
  std::error_code CreateAndQueueHook(void* target, Fn detour, void** original_out,
                                     minhook::HookHandle& handle) {
    std::error_code ec = handle.Create(target, reinterpret_cast<void*>(detour), original_out);
    if (ec) return ec;
    ec = batch_.QueueEnable(handle);
    if (!ec) hook_targets_.push_back(target);
    return ec;
  }

  // Applies all queued hooks. Call after creating all hooks in Install().
  std::error_code ApplyBatch() {
    std::error_code ec = batch_.Apply();
    return ec;
  }

  // Disables all hooks managed by this layer (targeted, not global).
  // Call from Remove() before destroying handles.
  void DisableAll() {
    for (void* target : hook_targets_) {
      MH_DisableHook(target);  // Ignore errors - best effort
    }
    hook_targets_.clear();
  }
};

}  // namespace crdeadzone