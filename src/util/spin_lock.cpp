#include "spin_lock.h"

#include <atomic>
#include <mutex>
#include <thread>

namespace cgo::impl {

class SpinLock::Impl {
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

SpinLock::SpinLock() : _impl(std::make_unique<Impl>()) {}

SpinLock::~SpinLock() = default;

void SpinLock::lock() { _impl->lock(); }

void SpinLock::unlock() { _impl->unlock(); }

}  // namespace cgo::impl
