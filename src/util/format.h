#pragma once

#include <stdio.h>

#include <string>
#include <vector>

namespace cgo::impl {

// NOTE: this function will cause great performance lost
template <typename... Args>
std::string format(const char* fmt, Args... args) {
  constexpr size_t oldlen = 512;
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

#define FORMAT(fmt, ...) format("%s -> %s() [line: %d] " fmt, __FILE__, __FUNCTION__, __LINE__, ##__VA_ARGS__)

}  // namespace cgo::impl
