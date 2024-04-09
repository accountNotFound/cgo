#pragma once

#include <string>

#ifdef USE_DEBUG
#undef USE_DEBUG
#define DEBUG(fmt, ...) printf("%s -> %s() [line: %d] " fmt "\n", __FILE__, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define DEBUG(fmt, ...)
#endif

#ifdef USE_ASSERT
#define ASSERT(expr, fmt, ...)                                                                    \
  {                                                                                               \
    if (!(expr)) {                                                                                \
      printf("%s -> %s() [line: %d] " fmt "\n", __FILE__, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
      std::terminate();                                                                           \
    }                                                                                             \
  }
#else
#define ASSERT(expr, fmt, ...)
#endif

namespace cgo::util {

// NOTE: this function will cause huge performance lost
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

}  // namespace cgo::util
