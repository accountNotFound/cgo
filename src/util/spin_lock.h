#pragma once

#include <memory>
#include <mutex>

namespace cgo::impl {

class SpinLock {
 public:
  SpinLock();
  ~SpinLock();

  void lock();
  void unlock();

 private:
  class Impl;
  std::unique_ptr<Impl> _impl;
};

}  // namespace cgo::impl
