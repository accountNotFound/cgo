#pragma once

#include <cstddef>

namespace cgo::util {

constexpr unsigned int s2i(const char *s, std::size_t l, unsigned int h) {
  return l == 0 ? h : s2i(s + 1, l - 1, (h * 33) + static_cast<unsigned char>(*s) - 'a' + 1);
}

}  // namespace  cgo::util

constexpr unsigned int operator""_u(const char *s, std::size_t l) { return cgo::util::s2i(s, l, 0); }