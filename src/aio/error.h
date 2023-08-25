#pragma once

#include <exception>

#include "util/format.h"

#if defined(linux) || defined(__linux) || defined(__linux__)
#include <errno.h>

namespace cgo::impl {

class AioException : public std::exception {
 public:
  AioException(const std::string& msg) : _code(errno), _msg(format("%s, [errno=%d]", msg.data(), errno)) {}
  const char* what() const noexcept { return this->_msg.data(); }

 private:
  const std::string _msg;
  int _code;
};

}  // namespace cgo::impl
#endif
