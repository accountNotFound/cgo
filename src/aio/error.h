#pragma once

#include <exception>

#include "util/format.h"

#if defined(linux) || defined(__linux) || defined(__linux__)
#include <errno.h>

namespace cgo::impl {

class AioException: public std::exception {
 public:
  AioException(const std::string& msg) : _msg(msg), _code(errno) {}
  const char* what() const noexcept { return format("[errno=%d] %s", this->_code, this->_msg.data()).data(); }

 private:
  const std::string _msg;
  int _code;
};

}  // namespace cgo::impl
#endif
