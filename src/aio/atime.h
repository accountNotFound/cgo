#pragma once

#include <memory>

#include "core/event.h"

namespace cgo::impl {

class Timer {
 public:
  Timer(unsigned long long millisec);
  Timer(const Timer&) = delete;
  auto wait() -> Async<void>;
  auto chan() const -> Channel<Event::Type> { return this->_event.chan(); }

 private:
  Fd _fd;
  Event _event;
};

extern auto sleep(unsigned long long millisec) -> Async<void>;

}  // namespace cgo::impl
