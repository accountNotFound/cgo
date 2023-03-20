#include "atime.h"

#include <sys/timerfd.h>

namespace cgo::impl {

Timer::Timer(unsigned long long millisec)
    : _fd(::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK)), _event(_fd, Event::IN | Event::ONESHOT) {
  ::itimerspec ts;
  ts.it_interval = timespec{0, 0};
  ts.it_value = timespec{(long)millisec / 1000, ((long)millisec % 1000) * 1000000};
  timerfd_settime(_fd, 0, &ts, nullptr);
  Context::current().handler().add(this->_fd, this->_event);
}

auto Timer::wait() -> Async<void> { co_await this->_event.chan().recv(); }

auto Timer::chan() -> Channel<bool> { return this->_event.chan(); }

auto sleep(unsigned long long millisec) -> Async<void> { co_await Timer(millisec).wait(); }

}  // namespace cgo::impl
