#include "atime.h"

#include "error.h"
#include "util/format.h"

#if defined(linux) || defined(__linux) || defined(__linux__)
#include <sys/timerfd.h>

namespace cgo::impl {

Timer::Timer(unsigned long long millisec) : _chan(1) {
  this->_fd = ::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
  ::itimerspec ts;
  ts.it_interval = timespec{0, 0};
  ts.it_value = timespec{(long)millisec / 1000, ((long)millisec % 1000) * 1000000};
  timerfd_settime(_fd, 0, &ts, nullptr);
  Context::current().handler().add(this->_fd, Event::IN | Event::ONESHOT, this->_chan);
}

Timer::~Timer() {
  Context::current().handler().del(this->_fd);
  ::close(this->_fd);
}

auto Timer::wait() -> Async<void> {
  auto ev = co_await this->_chan.recv();
  if (ev & Event::ERR) {
    throw AioException(format("Timer{fd=%d} exception", this->_fd));
  }
}

auto sleep(unsigned long long millisec) -> Async<void> { co_await Timer(millisec).wait(); }

}  // namespace cgo::impl
#endif
