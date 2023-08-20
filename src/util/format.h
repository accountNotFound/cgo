#pragma once

#include <stdio.h>

#include <string>
#include <vector>

namespace cgo::impl {
template <typename... Args>
std::string format(const char* fmt, Args... args) {
  constexpr size_t oldlen = BUFSIZ;
  std::string buffer(oldlen, '\0');

  size_t newlen = snprintf(buffer.data(), oldlen, fmt, args...);
  newlen++;

  if (newlen > oldlen) {
    std::string newbuffer(newlen, '\0');
    snprintf(newbuffer.data(), newlen, fmt, args...);
    return newbuffer;
  }
  buffer.resize(newlen);
  return buffer;
}
}  // namespace cgo::impl
