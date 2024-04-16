#pragma once

#include <functional>

namespace cgo::util {

class Defer {
 public:
  Defer(std::function<void()>&& callback) : _callback(callback) {}
  ~Defer() { this->_callback(); }

 private:
  std::function<void()> _callback;
};

}  // namespace cgo::util
