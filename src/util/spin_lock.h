#pragma once

#include <atomic>
#include <memory>
#include <mutex>

namespace cgo::impl {

class SpinLock {
 public:
  void lock() {
    bool expected = false;
    while (!_flag.compare_exchange_weak(expected, true)) {
      expected = false;
    }
  }
  void unlock() { _flag.store(false); }

 private:
  std::atomic<bool> _flag = false;
};

}  // namespace cgo::impl
