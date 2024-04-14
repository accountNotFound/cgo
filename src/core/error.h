#pragma once

#include <exception>
#include <string>

namespace cgo {

class TimeoutException : public std::exception {
 public:
  TimeoutException(int64_t timeout_ms)
      : _timeout_ms(timeout_ms), _msg("timeout after " + std::to_string(timeout_ms) + "ms") {}
  const char* waht() const noexcept { return this->_msg.data(); }

 private:
  int64_t _timeout_ms;
  std::string _msg;
};

}  // namespace cgo
