#pragma once

#include <memory>

#include "core/event.h"

namespace cgo::impl {

class Timer {
 public:
  Timer(unsigned long long millisec);
  Timer(const Timer&) = delete;
  ~Timer();
  auto wait() -> Async<void>;
  auto chan() const -> Channel<Event> { return this->_chan; }

 private:
  Fd _fd;
  Channel<Event> _chan;
};

extern auto sleep(unsigned long long millisec) -> Async<void>;

}  // namespace cgo::impl
