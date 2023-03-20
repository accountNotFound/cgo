#pragma once

#include <memory>

#include "core/event.h"

namespace cgo::impl {

class Timer {
 public:
  Timer(unsigned long long millisec);
  auto wait() -> Async<void>;
  auto chan() -> Channel<bool>;

#if defined(linux) || defined(__linux) || defined(__linux__)
 private:
  Fd _fd;
  Event _event;
#endif
};

extern auto sleep(unsigned long long millisec) -> Async<void> ;

}  // namespace cgo::impl
