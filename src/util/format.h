#pragma once

#include <stdio.h>

#include <string>
#include <vector>

namespace cgo::impl {
template <typename... Args>
inline std::string format(const char* format, Args... args) {
  constexpr size_t oldlen = BUFSIZ;
  char buffer[oldlen];

  size_t newlen = snprintf(&buffer[0], oldlen, format, args...);
  newlen++;

  if (newlen > oldlen) {
    std::vector<char> newbuffer(newlen);
    snprintf(newbuffer.data(), newlen, format, args...);
    return std::string(newbuffer.data());
  }
  return buffer;
}
}  // namespace cgo::impl
