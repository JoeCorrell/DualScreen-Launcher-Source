#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

namespace fo4ds {

class PipboyDatabase;

// Reads Fallout 4 1.11.240's already-populated native Pip-Boy value trees.
// The bridge never opens a menu and never touches the gameplay renderer.
class NativePipboyBridge {
 public:
  explicit NativePipboyBridge(PipboyDatabase* database);
  ~NativePipboyBridge();

  bool Start();
  void Stop();

 private:
  void Run();

  PipboyDatabase* database_ = nullptr;
  std::atomic<bool> running_{false};
  std::mutex lifecycle_mutex_;
  std::thread thread_;
};

}  // namespace fo4ds
